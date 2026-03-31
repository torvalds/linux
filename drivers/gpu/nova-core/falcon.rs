// SPDX-License-Identifier: GPL-2.0

//! Falcon microprocessor base support

use hal::FalconHal;

use kernel::{
    device::{
        self,
        Device, //
    },
    dma::{
        Coherent,
        DmaAddress,
        DmaMask, //
    },
    io::{
        poll::read_poll_timeout,
        register::{
            RegisterBase,
            WithBase, //
        },
        Io,
    },
    prelude::*,
    sync::aref::ARef,
    time::Delta,
};

use crate::{
    bounded_enum,
    driver::Bar0,
    falcon::hal::LoadMethod,
    gpu::Chipset,
    num::{
        self,
        FromSafeCast, //
    },
    regs,
};

pub(crate) mod gsp;
mod hal;
pub(crate) mod sec2;

/// Alignment (in bytes) of falcon memory blocks.
pub(crate) const MEM_BLOCK_ALIGNMENT: usize = 256;

bounded_enum! {
    /// Revision number of a falcon core, used in the [`crate::regs::NV_PFALCON_FALCON_HWCFG1`]
    /// register.
    #[derive(Debug, Copy, Clone)]
    pub(crate) enum FalconCoreRev with TryFrom<Bounded<u32, 4>> {
        Rev1 = 1,
        Rev2 = 2,
        Rev3 = 3,
        Rev4 = 4,
        Rev5 = 5,
        Rev6 = 6,
        Rev7 = 7,
    }
}

bounded_enum! {
    /// Revision subversion number of a falcon core, used in the
    /// [`crate::regs::NV_PFALCON_FALCON_HWCFG1`] register.
    #[derive(Debug, Copy, Clone)]
    pub(crate) enum FalconCoreRevSubversion with From<Bounded<u32, 2>> {
        Subversion0 = 0,
        Subversion1 = 1,
        Subversion2 = 2,
        Subversion3 = 3,
    }
}

bounded_enum! {
    /// Security mode of the Falcon microprocessor.
    ///
    /// See `falcon.rst` for more details.
    #[derive(Debug, Copy, Clone)]
    pub(crate) enum FalconSecurityModel with TryFrom<Bounded<u32, 2>> {
        /// Non-Secure: runs unsigned code without privileges.
        None = 0,
        /// Light-Secured (LS): Runs signed code with some privileges.
        /// Entry into this mode is only possible from 'Heavy-secure' mode, which verifies the
        /// code's signature.
        ///
        /// Also known as Low-Secure, Privilege Level 2 or PL2.
        Light = 2,
        /// Heavy-Secured (HS): Runs signed code with full privileges.
        /// The code's signature is verified by the Falcon Boot ROM (BROM).
        ///
        /// Also known as High-Secure, Privilege Level 3 or PL3.
        Heavy = 3,
    }
}

bounded_enum! {
    /// Signing algorithm for a given firmware, used in the
    /// [`crate::regs::NV_PFALCON2_FALCON_MOD_SEL`] register. It is passed to the Falcon Boot ROM
    /// (BROM) as a parameter.
    #[derive(Debug, Copy, Clone)]
    pub(crate) enum FalconModSelAlgo with TryFrom<Bounded<u32, 8>> {
        /// AES.
        Aes = 0,
        /// RSA3K.
        Rsa3k = 1,
    }
}

bounded_enum! {
    /// Valid values for the `size` field of the [`crate::regs::NV_PFALCON_FALCON_DMATRFCMD`]
    /// register.
    #[derive(Debug, Copy, Clone)]
    pub(crate) enum DmaTrfCmdSize with TryFrom<Bounded<u32, 3>> {
        /// 256 bytes transfer.
        Size256B = 0x6,
    }
}

bounded_enum! {
    /// Currently active core on a dual falcon/riscv (Peregrine) controller.
    #[derive(Debug, Copy, Clone, PartialEq, Eq)]
    pub(crate) enum PeregrineCoreSelect with From<Bounded<u32, 1>> {
        /// Falcon core is active.
        Falcon = 0,
        /// RISC-V core is active.
        Riscv = 1,
    }
}

/// Different types of memory present in a falcon core.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub(crate) enum FalconMem {
    /// Secure Instruction Memory.
    ImemSecure,
    /// Non-Secure Instruction Memory.
    #[expect(unused)]
    ImemNonSecure,
    /// Data Memory.
    Dmem,
}

bounded_enum! {
    /// Defines the Framebuffer Interface (FBIF) aperture type.
    /// This determines the memory type for external memory access during a DMA transfer, which is
    /// performed by the Falcon's Framebuffer DMA (FBDMA) engine. See falcon.rst for more details.
    #[derive(Debug, Copy, Clone)]
    pub(crate) enum FalconFbifTarget with TryFrom<Bounded<u32, 2>> {
        /// Local Framebuffer (GPU's VRAM memory).
        LocalFb = 0,
        /// Coherent system memory (System DRAM).
        CoherentSysmem = 1,
        /// Non-coherent system memory (System DRAM).
        NoncoherentSysmem = 2,
    }
}

bounded_enum! {
    /// Type of memory addresses to use.
    #[derive(Debug, Copy, Clone)]
    pub(crate) enum FalconFbifMemType with From<Bounded<u32, 1>> {
        /// Virtual memory addresses.
        Virtual = 0,
        /// Physical memory addresses.
        Physical = 1,
    }
}

/// Type used to represent the `PFALCON` registers address base for a given falcon engine.
pub(crate) struct PFalconBase(());

/// Type used to represent the `PFALCON2` registers address base for a given falcon engine.
pub(crate) struct PFalcon2Base(());

/// Trait defining the parameters of a given Falcon engine.
///
/// Each engine provides one base for `PFALCON` and `PFALCON2` registers.
pub(crate) trait FalconEngine:
    Send + Sync + RegisterBase<PFalconBase> + RegisterBase<PFalcon2Base> + Sized
{
}

/// Represents a portion of the firmware to be loaded into a particular memory (e.g. IMEM or DMEM)
/// using DMA.
#[derive(Debug, Clone)]
pub(crate) struct FalconDmaLoadTarget {
    /// Offset from the start of the source object to copy from.
    pub(crate) src_start: u32,
    /// Offset from the start of the destination memory to copy into.
    pub(crate) dst_start: u32,
    /// Number of bytes to copy.
    pub(crate) len: u32,
}

/// Parameters for the falcon boot ROM.
#[derive(Debug, Clone)]
pub(crate) struct FalconBromParams {
    /// Offset in `DMEM`` of the firmware's signature.
    pub(crate) pkc_data_offset: u32,
    /// Mask of engines valid for this firmware.
    pub(crate) engine_id_mask: u16,
    /// ID of the ucode used to infer a fuse register to validate the signature.
    pub(crate) ucode_id: u8,
}

/// Trait implemented by falcon firmwares that can be loaded using DMA.
pub(crate) trait FalconDmaLoadable {
    /// Returns the firmware data as a slice of bytes.
    fn as_slice(&self) -> &[u8];

    /// Returns the load parameters for Secure `IMEM`.
    fn imem_sec_load_params(&self) -> FalconDmaLoadTarget;

    /// Returns the load parameters for Non-Secure `IMEM`,
    /// used only on Turing and GA100.
    fn imem_ns_load_params(&self) -> Option<FalconDmaLoadTarget>;

    /// Returns the load parameters for `DMEM`.
    fn dmem_load_params(&self) -> FalconDmaLoadTarget;

    /// Returns an adapter that provides the required parameter to load this firmware using PIO.
    ///
    /// This can only fail if some `u32` fields cannot be converted to `u16`, or if the indices in
    /// the headers are invalid.
    fn try_as_pio_loadable(&self) -> Result<FalconDmaFirmwarePioAdapter<'_, Self>> {
        let new_pio_imem = |params: FalconDmaLoadTarget, secure| {
            let start = usize::from_safe_cast(params.src_start);
            let end = start + usize::from_safe_cast(params.len);
            let data = self.as_slice().get(start..end).ok_or(EINVAL)?;

            let dst_start = u16::try_from(params.dst_start).map_err(|_| EINVAL)?;

            Ok::<_, Error>(FalconPioImemLoadTarget {
                data,
                dst_start,
                secure,
                start_tag: dst_start >> 8,
            })
        };

        let imem_sec = new_pio_imem(self.imem_sec_load_params(), true)?;

        let imem_ns = if let Some(params) = self.imem_ns_load_params() {
            Some(new_pio_imem(params, false)?)
        } else {
            None
        };

        let dmem = {
            let params = self.dmem_load_params();
            let start = usize::from_safe_cast(params.src_start);
            let end = start + usize::from_safe_cast(params.len);
            let data = self.as_slice().get(start..end).ok_or(EINVAL)?;

            let dst_start = u16::try_from(params.dst_start).map_err(|_| EINVAL)?;

            FalconPioDmemLoadTarget { data, dst_start }
        };

        Ok(FalconDmaFirmwarePioAdapter {
            fw: self,
            imem_sec,
            imem_ns,
            dmem,
        })
    }
}

/// Represents a portion of the firmware to be loaded into IMEM using PIO.
#[derive(Clone)]
pub(crate) struct FalconPioImemLoadTarget<'a> {
    pub(crate) data: &'a [u8],
    pub(crate) dst_start: u16,
    pub(crate) secure: bool,
    pub(crate) start_tag: u16,
}

/// Represents a portion of the firmware to be loaded into DMEM using PIO.
#[derive(Clone)]
pub(crate) struct FalconPioDmemLoadTarget<'a> {
    pub(crate) data: &'a [u8],
    pub(crate) dst_start: u16,
}

/// Trait for providing PIO load parameters of falcon firmwares.
pub(crate) trait FalconPioLoadable {
    /// Returns the load parameters for Secure `IMEM`, if any.
    fn imem_sec_load_params(&self) -> Option<FalconPioImemLoadTarget<'_>>;

    /// Returns the load parameters for Non-Secure `IMEM`, if any.
    fn imem_ns_load_params(&self) -> Option<FalconPioImemLoadTarget<'_>>;

    /// Returns the load parameters for `DMEM`.
    fn dmem_load_params(&self) -> FalconPioDmemLoadTarget<'_>;
}

/// Adapter type that makes any DMA-loadable firmware also loadable via PIO.
///
/// Created using [`FalconDmaLoadable::try_as_pio_loadable`].
pub(crate) struct FalconDmaFirmwarePioAdapter<'a, T: FalconDmaLoadable + ?Sized> {
    /// Reference to the DMA firmware.
    fw: &'a T,
    /// Validated secure IMEM parameters.
    imem_sec: FalconPioImemLoadTarget<'a>,
    /// Validated non-secure IMEM parameters.
    imem_ns: Option<FalconPioImemLoadTarget<'a>>,
    /// Validated DMEM parameters.
    dmem: FalconPioDmemLoadTarget<'a>,
}

impl<'a, T> FalconPioLoadable for FalconDmaFirmwarePioAdapter<'a, T>
where
    T: FalconDmaLoadable + ?Sized,
{
    fn imem_sec_load_params(&self) -> Option<FalconPioImemLoadTarget<'_>> {
        Some(self.imem_sec.clone())
    }

    fn imem_ns_load_params(&self) -> Option<FalconPioImemLoadTarget<'_>> {
        self.imem_ns.clone()
    }

    fn dmem_load_params(&self) -> FalconPioDmemLoadTarget<'_> {
        self.dmem.clone()
    }
}

impl<'a, T> FalconFirmware for FalconDmaFirmwarePioAdapter<'a, T>
where
    T: FalconDmaLoadable + FalconFirmware + ?Sized,
{
    type Target = <T as FalconFirmware>::Target;

    fn brom_params(&self) -> FalconBromParams {
        self.fw.brom_params()
    }

    fn boot_addr(&self) -> u32 {
        self.fw.boot_addr()
    }
}

/// Trait for a falcon firmware.
///
/// A falcon firmware can be loaded on a given engine.
pub(crate) trait FalconFirmware {
    /// Engine on which this firmware is to be loaded.
    type Target: FalconEngine;

    /// Returns the parameters to write into the BROM registers.
    fn brom_params(&self) -> FalconBromParams;

    /// Returns the start address of the firmware.
    fn boot_addr(&self) -> u32;
}

/// Contains the base parameters common to all Falcon instances.
pub(crate) struct Falcon<E: FalconEngine> {
    hal: KBox<dyn FalconHal<E>>,
    dev: ARef<device::Device>,
}

impl<E: FalconEngine + 'static> Falcon<E> {
    /// Create a new falcon instance.
    pub(crate) fn new(dev: &device::Device, chipset: Chipset) -> Result<Self> {
        Ok(Self {
            hal: hal::falcon_hal(chipset)?,
            dev: dev.into(),
        })
    }

    /// Resets DMA-related registers.
    pub(crate) fn dma_reset(&self, bar: &Bar0) {
        bar.update(regs::NV_PFALCON_FBIF_CTL::of::<E>(), |v| {
            v.with_allow_phys_no_ctx(true)
        });

        bar.write(
            WithBase::of::<E>(),
            regs::NV_PFALCON_FALCON_DMACTL::zeroed(),
        );
    }

    /// Reset the controller, select the falcon core, and wait for memory scrubbing to complete.
    pub(crate) fn reset(&self, bar: &Bar0) -> Result {
        self.hal.reset_eng(bar)?;
        self.hal.select_core(self, bar)?;
        self.hal.reset_wait_mem_scrubbing(bar)?;

        bar.write(
            WithBase::of::<E>(),
            regs::NV_PFALCON_FALCON_RM::from(bar.read(regs::NV_PMC_BOOT_0).into_raw()),
        );

        Ok(())
    }

    /// Falcons supports up to four ports, but we only ever use one, so just hard-code it.
    const PIO_PORT: usize = 0;

    /// Write a slice to Falcon IMEM memory using programmed I/O (PIO).
    ///
    /// Returns `EINVAL` if `img.len()` is not a multiple of 4.
    fn pio_wr_imem_slice(&self, bar: &Bar0, load_offsets: FalconPioImemLoadTarget<'_>) -> Result {
        // Rejecting misaligned images here allows us to avoid checking
        // inside the loops.
        if load_offsets.data.len() % 4 != 0 {
            return Err(EINVAL);
        }

        bar.write(
            WithBase::of::<E>().at(Self::PIO_PORT),
            regs::NV_PFALCON_FALCON_IMEMC::zeroed()
                .with_secure(load_offsets.secure)
                .with_aincw(true)
                .with_offs(load_offsets.dst_start),
        );

        for (n, block) in load_offsets.data.chunks(MEM_BLOCK_ALIGNMENT).enumerate() {
            let n = u16::try_from(n)?;
            let tag: u16 = load_offsets.start_tag.checked_add(n).ok_or(ERANGE)?;
            bar.write(
                WithBase::of::<E>().at(Self::PIO_PORT),
                regs::NV_PFALCON_FALCON_IMEMT::zeroed().with_tag(tag),
            );
            for word in block.chunks_exact(4) {
                let w = [word[0], word[1], word[2], word[3]];
                bar.write(
                    WithBase::of::<E>().at(Self::PIO_PORT),
                    regs::NV_PFALCON_FALCON_IMEMD::zeroed().with_data(u32::from_le_bytes(w)),
                );
            }
        }

        Ok(())
    }

    /// Write a slice to Falcon DMEM memory using programmed I/O (PIO).
    ///
    /// Returns `EINVAL` if `img.len()` is not a multiple of 4.
    fn pio_wr_dmem_slice(&self, bar: &Bar0, load_offsets: FalconPioDmemLoadTarget<'_>) -> Result {
        // Rejecting misaligned images here allows us to avoid checking
        // inside the loops.
        if load_offsets.data.len() % 4 != 0 {
            return Err(EINVAL);
        }

        bar.write(
            WithBase::of::<E>().at(Self::PIO_PORT),
            regs::NV_PFALCON_FALCON_DMEMC::zeroed()
                .with_aincw(true)
                .with_offs(load_offsets.dst_start),
        );

        for word in load_offsets.data.chunks_exact(4) {
            let w = [word[0], word[1], word[2], word[3]];
            bar.write(
                WithBase::of::<E>().at(Self::PIO_PORT),
                regs::NV_PFALCON_FALCON_DMEMD::zeroed().with_data(u32::from_le_bytes(w)),
            );
        }

        Ok(())
    }

    /// Perform a PIO copy into `IMEM` and `DMEM` of `fw`, and prepare the falcon to run it.
    pub(crate) fn pio_load<F: FalconFirmware<Target = E> + FalconPioLoadable>(
        &self,
        bar: &Bar0,
        fw: &F,
    ) -> Result {
        bar.update(regs::NV_PFALCON_FBIF_CTL::of::<E>(), |v| {
            v.with_allow_phys_no_ctx(true)
        });

        bar.write(
            WithBase::of::<E>(),
            regs::NV_PFALCON_FALCON_DMACTL::zeroed(),
        );

        if let Some(imem_ns) = fw.imem_ns_load_params() {
            self.pio_wr_imem_slice(bar, imem_ns)?;
        }
        if let Some(imem_sec) = fw.imem_sec_load_params() {
            self.pio_wr_imem_slice(bar, imem_sec)?;
        }
        self.pio_wr_dmem_slice(bar, fw.dmem_load_params())?;

        self.hal.program_brom(self, bar, &fw.brom_params())?;

        bar.write(
            WithBase::of::<E>(),
            regs::NV_PFALCON_FALCON_BOOTVEC::zeroed().with_value(fw.boot_addr()),
        );

        Ok(())
    }

    /// Perform a DMA write according to `load_offsets` from `dma_handle` into the falcon's
    /// `target_mem`.
    ///
    /// `sec` is set if the loaded firmware is expected to run in secure mode.
    fn dma_wr(
        &self,
        bar: &Bar0,
        dma_obj: &Coherent<[u8]>,
        target_mem: FalconMem,
        load_offsets: FalconDmaLoadTarget,
    ) -> Result {
        const DMA_LEN: u32 = num::usize_into_u32::<{ MEM_BLOCK_ALIGNMENT }>();

        // For IMEM, we want to use the start offset as a virtual address tag for each page, since
        // code addresses in the firmware (and the boot vector) are virtual.
        //
        // For DMEM we can fold the start offset into the DMA handle.
        let (src_start, dma_start) = match target_mem {
            FalconMem::ImemSecure | FalconMem::ImemNonSecure => {
                (load_offsets.src_start, dma_obj.dma_handle())
            }
            FalconMem::Dmem => (
                0,
                dma_obj.dma_handle() + DmaAddress::from(load_offsets.src_start),
            ),
        };
        if dma_start % DmaAddress::from(DMA_LEN) > 0 {
            dev_err!(
                self.dev,
                "DMA transfer start addresses must be a multiple of {}\n",
                DMA_LEN
            );
            return Err(EINVAL);
        }

        // The DMATRFBASE/1 register pair only supports a 49-bit address.
        if dma_start > DmaMask::new::<49>().value() {
            dev_err!(self.dev, "DMA address {:#x} exceeds 49 bits\n", dma_start);
            return Err(ERANGE);
        }

        // DMA transfers can only be done in units of 256 bytes. Compute how many such transfers we
        // need to perform.
        let num_transfers = load_offsets.len.div_ceil(DMA_LEN);

        // Check that the area we are about to transfer is within the bounds of the DMA object.
        // Upper limit of transfer is `(num_transfers * DMA_LEN) + load_offsets.src_start`.
        match num_transfers
            .checked_mul(DMA_LEN)
            .and_then(|size| size.checked_add(load_offsets.src_start))
        {
            None => {
                dev_err!(self.dev, "DMA transfer length overflow\n");
                return Err(EOVERFLOW);
            }
            Some(upper_bound) if usize::from_safe_cast(upper_bound) > dma_obj.size() => {
                dev_err!(self.dev, "DMA transfer goes beyond range of DMA object\n");
                return Err(EINVAL);
            }
            Some(_) => (),
        };

        // Set up the base source DMA address.

        bar.write(
            WithBase::of::<E>(),
            regs::NV_PFALCON_FALCON_DMATRFBASE::zeroed().with_base(
                // CAST: `as u32` is used on purpose since we do want to strip the upper bits,
                // which will be written to `NV_PFALCON_FALCON_DMATRFBASE1`.
                (dma_start >> 8) as u32,
            ),
        );
        bar.write(
            WithBase::of::<E>(),
            regs::NV_PFALCON_FALCON_DMATRFBASE1::zeroed().try_with_base(dma_start >> 40)?,
        );

        let cmd = regs::NV_PFALCON_FALCON_DMATRFCMD::zeroed()
            .with_size(DmaTrfCmdSize::Size256B)
            .with_falcon_mem(target_mem);

        for pos in (0..num_transfers).map(|i| i * DMA_LEN) {
            // Perform a transfer of size `DMA_LEN`.
            bar.write(
                WithBase::of::<E>(),
                regs::NV_PFALCON_FALCON_DMATRFMOFFS::zeroed()
                    .try_with_offs(load_offsets.dst_start + pos)?,
            );
            bar.write(
                WithBase::of::<E>(),
                regs::NV_PFALCON_FALCON_DMATRFFBOFFS::zeroed().with_offs(src_start + pos),
            );

            bar.write(WithBase::of::<E>(), cmd);

            // Wait for the transfer to complete.
            // TIMEOUT: arbitrarily large value, no DMA transfer to the falcon's small memories
            // should ever take that long.
            read_poll_timeout(
                || Ok(bar.read(regs::NV_PFALCON_FALCON_DMATRFCMD::of::<E>())),
                |r| r.idle(),
                Delta::ZERO,
                Delta::from_secs(2),
            )?;
        }

        Ok(())
    }

    /// Perform a DMA load into `IMEM` and `DMEM` of `fw`, and prepare the falcon to run it.
    fn dma_load<F: FalconFirmware<Target = E> + FalconDmaLoadable>(
        &self,
        dev: &Device<device::Bound>,
        bar: &Bar0,
        fw: &F,
    ) -> Result {
        // Create DMA object with firmware content as the source of the DMA engine.
        let dma_obj = Coherent::from_slice(dev, fw.as_slice(), GFP_KERNEL)?;

        self.dma_reset(bar);
        bar.update(regs::NV_PFALCON_FBIF_TRANSCFG::of::<E>().at(0), |v| {
            v.with_target(FalconFbifTarget::CoherentSysmem)
                .with_mem_type(FalconFbifMemType::Physical)
        });

        self.dma_wr(
            bar,
            &dma_obj,
            FalconMem::ImemSecure,
            fw.imem_sec_load_params(),
        )?;
        self.dma_wr(bar, &dma_obj, FalconMem::Dmem, fw.dmem_load_params())?;

        self.hal.program_brom(self, bar, &fw.brom_params())?;

        // Set `BootVec` to start of non-secure code.
        bar.write(
            WithBase::of::<E>(),
            regs::NV_PFALCON_FALCON_BOOTVEC::zeroed().with_value(fw.boot_addr()),
        );

        Ok(())
    }

    /// Wait until the falcon CPU is halted.
    pub(crate) fn wait_till_halted(&self, bar: &Bar0) -> Result<()> {
        // TIMEOUT: arbitrarily large value, firmwares should complete in less than 2 seconds.
        read_poll_timeout(
            || Ok(bar.read(regs::NV_PFALCON_FALCON_CPUCTL::of::<E>())),
            |r| r.halted(),
            Delta::ZERO,
            Delta::from_secs(2),
        )?;

        Ok(())
    }

    /// Start the falcon CPU.
    pub(crate) fn start(&self, bar: &Bar0) -> Result<()> {
        match bar
            .read(regs::NV_PFALCON_FALCON_CPUCTL::of::<E>())
            .alias_en()
        {
            true => bar.write(
                WithBase::of::<E>(),
                regs::NV_PFALCON_FALCON_CPUCTL_ALIAS::zeroed().with_startcpu(true),
            ),
            false => bar.write(
                WithBase::of::<E>(),
                regs::NV_PFALCON_FALCON_CPUCTL::zeroed().with_startcpu(true),
            ),
        }

        Ok(())
    }

    /// Writes values to the mailbox registers if provided.
    pub(crate) fn write_mailboxes(&self, bar: &Bar0, mbox0: Option<u32>, mbox1: Option<u32>) {
        if let Some(mbox0) = mbox0 {
            bar.write(
                WithBase::of::<E>(),
                regs::NV_PFALCON_FALCON_MAILBOX0::zeroed().with_value(mbox0),
            );
        }

        if let Some(mbox1) = mbox1 {
            bar.write(
                WithBase::of::<E>(),
                regs::NV_PFALCON_FALCON_MAILBOX1::zeroed().with_value(mbox1),
            );
        }
    }

    /// Reads the value from `mbox0` register.
    pub(crate) fn read_mailbox0(&self, bar: &Bar0) -> u32 {
        bar.read(regs::NV_PFALCON_FALCON_MAILBOX0::of::<E>())
            .value()
    }

    /// Reads the value from `mbox1` register.
    pub(crate) fn read_mailbox1(&self, bar: &Bar0) -> u32 {
        bar.read(regs::NV_PFALCON_FALCON_MAILBOX1::of::<E>())
            .value()
    }

    /// Reads values from both mailbox registers.
    pub(crate) fn read_mailboxes(&self, bar: &Bar0) -> (u32, u32) {
        let mbox0 = self.read_mailbox0(bar);
        let mbox1 = self.read_mailbox1(bar);

        (mbox0, mbox1)
    }

    /// Start running the loaded firmware.
    ///
    /// `mbox0` and `mbox1` are optional parameters to write into the `MBOX0` and `MBOX1` registers
    /// prior to running.
    ///
    /// Wait up to two seconds for the firmware to complete, and return its exit status read from
    /// the `MBOX0` and `MBOX1` registers.
    pub(crate) fn boot(
        &self,
        bar: &Bar0,
        mbox0: Option<u32>,
        mbox1: Option<u32>,
    ) -> Result<(u32, u32)> {
        self.write_mailboxes(bar, mbox0, mbox1);
        self.start(bar)?;
        self.wait_till_halted(bar)?;
        Ok(self.read_mailboxes(bar))
    }

    /// Returns the fused version of the signature to use in order to run a HS firmware on this
    /// falcon instance. `engine_id_mask` and `ucode_id` are obtained from the firmware header.
    pub(crate) fn signature_reg_fuse_version(
        &self,
        bar: &Bar0,
        engine_id_mask: u16,
        ucode_id: u8,
    ) -> Result<u32> {
        self.hal
            .signature_reg_fuse_version(self, bar, engine_id_mask, ucode_id)
    }

    /// Check if the RISC-V core is active.
    ///
    /// Returns `true` if the RISC-V core is active, `false` otherwise.
    pub(crate) fn is_riscv_active(&self, bar: &Bar0) -> bool {
        self.hal.is_riscv_active(bar)
    }

    /// Load a firmware image into Falcon memory, using the preferred method for the current
    /// chipset.
    pub(crate) fn load<F: FalconFirmware<Target = E> + FalconDmaLoadable>(
        &self,
        dev: &Device<device::Bound>,
        bar: &Bar0,
        fw: &F,
    ) -> Result {
        match self.hal.load_method() {
            LoadMethod::Dma => self.dma_load(dev, bar, fw),
            LoadMethod::Pio => self.pio_load(bar, &fw.try_as_pio_loadable()?),
        }
    }

    /// Write the application version to the OS register.
    pub(crate) fn write_os_version(&self, bar: &Bar0, app_version: u32) {
        bar.write(
            WithBase::of::<E>(),
            regs::NV_PFALCON_FALCON_OS::zeroed().with_value(app_version),
        );
    }
}
