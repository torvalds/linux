// SPDX-License-Identifier: GPL-2.0 or MIT

//! Address space module.
//!
//! This module handles the hardware interaction for MMU operations through
//! MMIO register access.
//!

use core::ops::Range;

use kernel::{
    device::{
        Bound,
        Device, //
    }, //
    error::Result,
    io::{
        poll,
        register::Array,
        Io, //
    },
    iommu::pgtable::{
        Config,
        IoPageTable,
        ARM64LPAES1, //
    },
    num::Bounded,
    prelude::*,
    sizes::{
        SZ_2M,
        SZ_4K, //
    },
    sync::{
        Arc,
        ArcBorrow,
        LockedBy, //
    },
    time::Delta, //
};

use crate::{
    driver::IoMem,
    mmu::{
        AsSlotManager,
        Mmu, //
    },
    regs::{
        mmu_control::mmu_as_control,
        mmu_control::mmu_as_control::*,
        MAX_AS, //
    },
    slot::{
        LockedSeat,
        Seat,
        SlotOperations, //
    }, //
};

/// Address space configuration values to be written to MMU registers.
#[derive(Clone, Copy)]
struct AddressSpaceConfig {
    /// Translation configuration. Configures how the MMU walks the page table for this
    /// address space.
    transcfg: u64,

    /// Translation table base address. The address of the page table.
    transtab: u64,

    /// Memory attributes such as cacheability.
    memattr: u64,
}

/// Virtual memory (VM) address space data for use in MMU operations.
#[pin_data]
pub(crate) struct VmAsData<'drm> {
    /// This address-space seat tracks this VM's binding to a hardware address space slot.
    /// It can only be accessed when holding the `Mmu::as_manager` lock.
    as_seat: LockedSeat<AddressSpaceManager<'drm>, MAX_AS>,

    /// Virtual address bits for this address space.
    va_bits: u8,

    /// The page table which maps GPU virtual addresses to physical addresses for this VM.
    #[pin]
    pub(crate) page_table: IoPageTable<'drm, ARM64LPAES1>,
}

impl<'drm> VmAsData<'drm> {
    /// Creates VM address space data by initializing all of its fields.
    pub(crate) fn new<'a>(
        mmu: &'a Mmu<'drm>,
        dev: &'drm Device<Bound>,
        va_bits: u32,
        pa_bits: u32,
    ) -> impl pin_init::PinInit<VmAsData<'drm>, Error> + 'a {
        let pt_config = Config {
            quirks: 0,
            pgsize_bitmap: SZ_4K | SZ_2M,
            ias: va_bits,
            oas: pa_bits,
            coherent_walk: false,
        };

        let page_table_init = IoPageTable::new(dev, pt_config);

        try_pin_init!(Self {
            as_seat: LockedBy::new(&mmu.as_manager, Seat::NoSeat),
            va_bits: va_bits as u8,
            page_table <- page_table_init,
        }? Error)
    }

    /// Computes the hardware configuration for this address space.
    fn as_config(&self) -> Result<AddressSpaceConfig> {
        let pt = &self.page_table;
        // The hardware computes the valid input address range as:
        //   INA_BITS_VALID = min(HW_INA_BITS, 55 - INA_BITS)
        // To configure our desired va_bits, we solve for INA_BITS:
        //   INA_BITS = 55 - va_bits
        // This assumes HW_INA_BITS (hardware capability) >= va_bits.
        let field = 55u64.checked_sub(self.va_bits.into()).ok_or(EINVAL)?;
        let ina_bits =
            match mmu_as_control::InaBits::try_from(Bounded::try_new(field).ok_or(EINVAL)?)? {
                mmu_as_control::InaBits::Reset => return Err(EINVAL),
                bits => bits,
            };

        let transcfg = mmu_as_control::TRANSCFG::zeroed()
            .with_ptw_memattr(mmu_as_control::PtwMemattr::WriteBack)
            .with_r_allocate(true)
            .with_mode(mmu_as_control::AddressSpaceMode::Aarch64_4K)
            .with_ina_bits(ina_bits)
            .into_raw();

        Ok(AddressSpaceConfig {
            transcfg,
            // SAFETY: The SlotManager holds an `Arc<VmAsData>` as SlotData while this
            // TTBR is programmed and stores that Arc in the active slot before
            // returning. Eviction flushes and disables the slot before releasing
            // the Arc; if eviction fails, the slot retains it. Therefore the page
            // table cannot be dropped while the GPU is using it.
            transtab: unsafe { pt.ttbr() },
            memattr: MEMATTR::from_mair(pt.mair()).into_raw(),
        })
    }
}

/// Coordinates all hardware-level address space operations through MMIO register
/// operations including enabling, disabling, flushing, and updating address spaces.
pub(crate) struct AddressSpaceManager<'drm> {
    /// Parent device used for logging.
    dev: &'drm Device<Bound>,

    /// Memory-mapped I/O region for GPU register access.
    iomem: Arc<IoMem<'drm>>,

    /// Bitmask of present address space slots from GPU_AS_PRESENT register.
    as_present: u32,
}

impl<'drm> AddressSpaceManager<'drm> {
    /// Creates a new address space manager.
    ///
    /// Initializes the manager with references to the platform device and
    /// I/O memory region, along with the bitmask of available AS slots.
    pub(super) fn new(
        dev: &'drm Device<Bound>,
        iomem: Arc<IoMem<'drm>>,
        as_present: u32,
    ) -> Result<AddressSpaceManager<'drm>> {
        if as_present.trailing_ones() != as_present.count_ones() {
            dev_err!(
                dev,
                "Sparse AS_PRESENT mask is unsupported: {:#x}",
                as_present
            );
            return Err(EINVAL);
        }
        Ok(Self {
            dev,
            iomem,
            as_present,
        })
    }

    /// Validates that an AS slot number is within range and present in hardware.
    ///
    /// Checks that the slot index is less than [`MAX_AS`] and that
    /// the corresponding bit is set in the `as_present` mask read from the GPU.
    ///
    /// Returns [`EINVAL`] if the slot is out of range or not present in hardware.
    fn validate_as_slot(&self, as_nr: usize) -> Result {
        if as_nr >= MAX_AS {
            dev_err!(
                self.dev,
                "AS slot {} out of valid range (max {})",
                as_nr,
                MAX_AS
            );
            return Err(EINVAL);
        }

        if (self.as_present & (1 << as_nr)) == 0 {
            dev_err!(
                self.dev,
                "AS slot {} not present in hardware (AS_PRESENT={:#x})",
                as_nr,
                self.as_present
            );
            return Err(EINVAL);
        }
        Ok(())
    }

    /// Waits for an AS slot to become ready (not active).
    ///
    /// Returns an error if polling times out after 10ms or if register access fails.
    fn as_wait_ready(&self, as_nr: usize) -> Result {
        let io = &*self.iomem;
        let op = || {
            let status_reg = STATUS::try_at(as_nr).ok_or(EINVAL)?;
            Ok(io.read(status_reg))
        };
        let cond = |status: &STATUS| -> bool { !status.active_ext() };
        poll::read_poll_timeout(op, cond, Delta::from_micros(50), Delta::from_millis(10))?;

        Ok(())
    }

    /// Sends a command to an AS slot.
    ///
    /// Returns an error if waiting for ready times out or if register write fails.
    fn as_send_cmd(&mut self, as_nr: usize, cmd: MmuCommand) -> Result {
        self.as_wait_ready(as_nr)?;
        let io = &*self.iomem;
        let command_reg = COMMAND::try_at(as_nr).ok_or(EINVAL)?;
        io.write(command_reg, COMMAND::zeroed().with_command(cmd));
        Ok(())
    }

    /// Sends a command to an AS slot and waits for completion.
    ///
    /// Returns an error if sending the command fails or if waiting for completion times out.
    fn as_send_cmd_and_wait(&mut self, as_nr: usize, cmd: MmuCommand) -> Result {
        self.as_send_cmd(as_nr, cmd)?;
        self.as_wait_ready(as_nr)?;
        Ok(())
    }

    /// Enables an AS slot with the provided configuration.
    ///
    /// Returns an error if the slot is invalid or if register writes/commands fail.
    fn as_enable(&mut self, as_nr: usize, as_config: &AddressSpaceConfig) -> Result {
        self.validate_as_slot(as_nr)?;

        let io = &*self.iomem;

        let transtab = as_config.transtab;
        io.write(
            TRANSTAB_LO::try_at(as_nr).ok_or(EINVAL)?,
            TRANSTAB_LO::from_raw(transtab as u32),
        );
        io.write(
            TRANSTAB_HI::try_at(as_nr).ok_or(EINVAL)?,
            TRANSTAB_HI::from_raw((transtab >> 32) as u32),
        );

        let transcfg = as_config.transcfg;
        io.write(
            TRANSCFG_LO::try_at(as_nr).ok_or(EINVAL)?,
            TRANSCFG_LO::from_raw(transcfg as u32),
        );
        io.write(
            TRANSCFG_HI::try_at(as_nr).ok_or(EINVAL)?,
            TRANSCFG_HI::from_raw((transcfg >> 32) as u32),
        );

        let memattr = as_config.memattr;
        io.write(
            MEMATTR_LO::try_at(as_nr).ok_or(EINVAL)?,
            MEMATTR_LO::from_raw(memattr as u32),
        );
        io.write(
            MEMATTR_HI::try_at(as_nr).ok_or(EINVAL)?,
            MEMATTR_HI::from_raw((memattr >> 32) as u32),
        );

        self.as_send_cmd_and_wait(as_nr, MmuCommand::Update)?;

        Ok(())
    }

    /// Disables an AS slot and clears its configuration.
    ///
    /// Returns an error if the slot is invalid or if register writes/commands fail.
    fn as_disable(&mut self, as_nr: usize) -> Result {
        self.validate_as_slot(as_nr)?;

        // Flush AS before disabling
        self.as_send_cmd_and_wait(as_nr, MmuCommand::FlushMem)?;

        let io = &*self.iomem;

        io.write(
            TRANSTAB_LO::try_at(as_nr).ok_or(EINVAL)?,
            TRANSTAB_LO::from_raw(0),
        );
        io.write(
            TRANSTAB_HI::try_at(as_nr).ok_or(EINVAL)?,
            TRANSTAB_HI::from_raw(0),
        );

        io.write(
            MEMATTR_LO::try_at(as_nr).ok_or(EINVAL)?,
            MEMATTR_LO::from_raw(0),
        );
        io.write(
            MEMATTR_HI::try_at(as_nr).ok_or(EINVAL)?,
            MEMATTR_HI::from_raw(0),
        );

        let transcfg = TRANSCFG::zeroed()
            .with_mode(AddressSpaceMode::Unmapped)
            .into_raw();

        io.write(
            TRANSCFG_LO::try_at(as_nr).ok_or(EINVAL)?,
            TRANSCFG_LO::from_raw(transcfg as u32),
        );
        io.write(
            TRANSCFG_HI::try_at(as_nr).ok_or(EINVAL)?,
            TRANSCFG_HI::from_raw((transcfg >> 32) as u32),
        );

        self.as_send_cmd_and_wait(as_nr, MmuCommand::Update)?;

        Ok(())
    }

    /// Locks a region of the translation tables for an atomic update.
    ///
    /// Programs the MMU [`LOCKADDR`] register for the given address space and issues
    /// the lock command. The hardware rounds the requested range up to a
    /// power-of-two region aligned to its size.
    ///
    /// Returns an error if the slot is invalid or if register writes/commands fail.
    fn as_start_update(&mut self, as_nr: usize, region: &Range<u64>) -> Result {
        self.validate_as_slot(as_nr)?;

        // Avoid both an empty range and an inverted range.
        if region.start >= region.end {
            return Err(EINVAL);
        }

        // The lock operates on full 64-byte cache lines of translation table entries.
        // Since each translation table entry (TTE) is 8 bytes, a cache line has 8 TTEs.
        // Since each TTE maps one page, the minimum locked region size will be 8 pages.
        //
        // With 4KiB pages (Aarch64_4K mode), the minimum locked region is 32KiB.
        let lock_region_min_size: u64 = 4096 * 8;

        // Count the number of trailing zero bits (zeros at the right/least-significant
        // end of the binary representation). For a power-of-two value, this equals the
        // base-2 exponent (e.g., 32 KiB = 2^15 → 15).
        let lock_region_min_size_log2 = lock_region_min_size.trailing_zeros() as u8;

        // XOR the first and last addresses to identify which bits differ between them.
        // The highest set bit in the result determines the exponent of the smallest
        // power-of-two region that can contain both addresses.
        //
        // Example:
        //   addr_xor = 0x1000 ^ 0x2FFF = 0x3FFF
        //   highest set bit in 0x3FFF is bit 13
        //   minimum region size = 2^(13 + 1) = 16 KiB
        let addr_xor = region.start ^ (region.end - 1);
        let region_size_log2 = 64 - addr_xor.leading_zeros() as u8;

        let lock_region_log2 = core::cmp::max(region_size_log2, lock_region_min_size_log2);

        let lock_region_size = 1u64.checked_shl(lock_region_log2.into()).ok_or(EINVAL)?;
        // Align the LOCKADDR base address down to the lock region size (1 << lock_region_log2).
        //
        // The MMU ignores the low lock_region_log2 bits of LOCKADDR base, so ensure
        // they are cleared in software to avoid ambiguity.
        //
        // Example:
        //   lock_region_log2 = 14 (16 KiB)
        //   region.start = 0x1000
        //   lockaddr_base = 0x1000 & ~(0x3FFF) = 0x0000
        let lockaddr_base = region.start & !(lock_region_size - 1);

        // The LOCKADDR size field encodes the lock region size as log2(size) - 1,
        // per the hardware definition. For example, a 32 KiB region is encoded as 14
        // because log2(32 KiB) = 15.
        let lockaddr_size = lock_region_log2 - 1;

        let io = &*self.iomem;

        // The LOCKADDR base field stores address bits 63:12, so remove the low 12 bits
        // before passing this value to the register macro helper.
        // These bits are guaranteed to be zero anyway because of the minimum
        // size of the locked region.
        let lockaddr_base_field = lockaddr_base >> 12;
        let lockaddr_val = LOCKADDR::zeroed()
            .try_with_size(lockaddr_size)?
            .try_with_base(lockaddr_base_field)?
            .into_raw();

        io.write(
            LOCKADDR_LO::try_at(as_nr).ok_or(EINVAL)?,
            LOCKADDR_LO::from_raw(lockaddr_val as u32),
        );
        io.write(
            LOCKADDR_HI::try_at(as_nr).ok_or(EINVAL)?,
            LOCKADDR_HI::from_raw((lockaddr_val >> 32) as u32),
        );

        self.as_send_cmd_and_wait(as_nr, MmuCommand::Lock)
    }

    /// Completes an atomic translation table update.
    ///
    /// Returns an error if the slot is invalid or if the flush command fails.
    fn as_end_update(&mut self, as_nr: usize) -> Result {
        self.validate_as_slot(as_nr)?;
        self.as_send_cmd_and_wait(as_nr, MmuCommand::FlushPt)?;
        Ok(())
    }

    /// Flushes the translation table cache for an AS slot.
    ///
    /// Returns an error if the slot is invalid or if the flush command fails.
    fn as_flush(&mut self, as_nr: usize) -> Result {
        self.validate_as_slot(as_nr)?;
        self.as_send_cmd_and_wait(as_nr, MmuCommand::FlushPt)
    }
}

impl<'drm> SlotOperations<MAX_AS> for AddressSpaceManager<'drm> {
    /// VM address space data associated with a hardware slot.
    type SlotData = Arc<VmAsData<'drm>>;

    fn seat(slot_data: &Self::SlotData) -> &LockedSeat<Self, MAX_AS> {
        &slot_data.as_seat
    }

    /// Activates a VM in a hardware slot.
    fn activate(&mut self, slot_idx: usize, slot_data: &Self::SlotData) -> Result {
        let as_config = slot_data.as_config()?;
        self.as_enable(slot_idx, &as_config)
    }

    /// Evicts a VM from a hardware slot.
    fn evict(&mut self, slot_idx: usize, _slot_data: &Self::SlotData) -> Result {
        self.as_flush(slot_idx)?;
        self.as_disable(slot_idx)?;
        Ok(())
    }
}

impl<'drm> AsSlotManager<'drm> {
    /// Locks a region for translation table updates if the VM has an active slot.
    pub(super) fn start_vm_update(
        &mut self,
        vm_as_data: &VmAsData<'drm>,
        region: &Range<u64>,
    ) -> Result {
        let seat = vm_as_data.as_seat.access(self);
        match seat.slot() {
            Some(slot) => {
                let as_nr = slot as usize;
                self.as_start_update(as_nr, region)
            }
            _ => Ok(()),
        }
    }

    /// Completes translation table updates and unlocks the region.
    pub(super) fn end_vm_update(&mut self, vm_as_data: &VmAsData<'drm>) -> Result {
        let seat = vm_as_data.as_seat.access(self);
        match seat.slot() {
            Some(slot) => {
                let as_nr = slot as usize;
                self.as_end_update(as_nr)
            }
            _ => Ok(()),
        }
    }

    /// Flushes the translation table cache if the VM has an active slot.
    pub(super) fn flush_vm(&mut self, vm_as_data: &VmAsData<'drm>) -> Result {
        let seat = vm_as_data.as_seat.access(self);
        match seat.slot() {
            Some(slot) => {
                let as_nr = slot as usize;
                self.as_flush(as_nr)
            }
            _ => Ok(()),
        }
    }

    /// Activates a VM by assigning it to a hardware slot.
    pub(super) fn activate_vm(&mut self, vm_as_data: ArcBorrow<'_, VmAsData<'drm>>) -> Result {
        self.activate(vm_as_data.into())
    }

    /// Deactivates a VM by evicting it from its hardware slot.
    pub(super) fn deactivate_vm(&mut self, vm_as_data: &VmAsData<'drm>) -> Result {
        self.evict(&vm_as_data.as_seat)
    }
}
