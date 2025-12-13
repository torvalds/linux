// SPDX-License-Identifier: GPL-2.0

//! Falcon microprocessor base support

use core::ops::Deref;
use hal::FalconHal;
use kernel::device;
use kernel::dma::DmaAddress;
use kernel::prelude::*;
use kernel::sync::aref::ARef;
use kernel::time::Delta;

use crate::dma::DmaObject;
use crate::driver::Bar0;
use crate::gpu::Chipset;
use crate::regs;
use crate::regs::macros::RegisterBase;
use crate::util;

pub(crate) mod gsp;
mod hal;
pub(crate) mod sec2;

// TODO[FPRI]: Replace with `ToPrimitive`.
macro_rules! impl_from_enum_to_u32 {
    ($enum_type:ty) => {
        impl From<$enum_type> for u32 {
            fn from(value: $enum_type) -> Self {
                value as u32
            }
        }
    };
}

/// Revision number of a falcon core, used in the [`crate::regs::NV_PFALCON_FALCON_HWCFG1`]
/// register.
#[repr(u8)]
#[derive(Debug, Default, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub(crate) enum FalconCoreRev {
    #[default]
    Rev1 = 1,
    Rev2 = 2,
    Rev3 = 3,
    Rev4 = 4,
    Rev5 = 5,
    Rev6 = 6,
    Rev7 = 7,
}
impl_from_enum_to_u32!(FalconCoreRev);

// TODO[FPRI]: replace with `FromPrimitive`.
impl TryFrom<u8> for FalconCoreRev {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self> {
        use FalconCoreRev::*;

        let rev = match value {
            1 => Rev1,
            2 => Rev2,
            3 => Rev3,
            4 => Rev4,
            5 => Rev5,
            6 => Rev6,
            7 => Rev7,
            _ => return Err(EINVAL),
        };

        Ok(rev)
    }
}

/// Revision subversion number of a falcon core, used in the
/// [`crate::regs::NV_PFALCON_FALCON_HWCFG1`] register.
#[repr(u8)]
#[derive(Debug, Default, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub(crate) enum FalconCoreRevSubversion {
    #[default]
    Subversion0 = 0,
    Subversion1 = 1,
    Subversion2 = 2,
    Subversion3 = 3,
}
impl_from_enum_to_u32!(FalconCoreRevSubversion);

// TODO[FPRI]: replace with `FromPrimitive`.
impl TryFrom<u8> for FalconCoreRevSubversion {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self> {
        use FalconCoreRevSubversion::*;

        let sub_version = match value & 0b11 {
            0 => Subversion0,
            1 => Subversion1,
            2 => Subversion2,
            3 => Subversion3,
            _ => return Err(EINVAL),
        };

        Ok(sub_version)
    }
}

/// Security model of a falcon core, used in the [`crate::regs::NV_PFALCON_FALCON_HWCFG1`]
/// register.
#[repr(u8)]
#[derive(Debug, Default, Copy, Clone)]
/// Security mode of the Falcon microprocessor.
///
/// See `falcon.rst` for more details.
pub(crate) enum FalconSecurityModel {
    /// Non-Secure: runs unsigned code without privileges.
    #[default]
    None = 0,
    /// Light-Secured (LS): Runs signed code with some privileges.
    /// Entry into this mode is only possible from 'Heavy-secure' mode, which verifies the code's
    /// signature.
    ///
    /// Also known as Low-Secure, Privilege Level 2 or PL2.
    Light = 2,
    /// Heavy-Secured (HS): Runs signed code with full privileges.
    /// The code's signature is verified by the Falcon Boot ROM (BROM).
    ///
    /// Also known as High-Secure, Privilege Level 3 or PL3.
    Heavy = 3,
}
impl_from_enum_to_u32!(FalconSecurityModel);

// TODO[FPRI]: replace with `FromPrimitive`.
impl TryFrom<u8> for FalconSecurityModel {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self> {
        use FalconSecurityModel::*;

        let sec_model = match value {
            0 => None,
            2 => Light,
            3 => Heavy,
            _ => return Err(EINVAL),
        };

        Ok(sec_model)
    }
}

/// Signing algorithm for a given firmware, used in the [`crate::regs::NV_PFALCON2_FALCON_MOD_SEL`]
/// register. It is passed to the Falcon Boot ROM (BROM) as a parameter.
#[repr(u8)]
#[derive(Debug, Default, Copy, Clone, PartialEq, Eq)]
pub(crate) enum FalconModSelAlgo {
    /// AES.
    #[expect(dead_code)]
    Aes = 0,
    /// RSA3K.
    #[default]
    Rsa3k = 1,
}
impl_from_enum_to_u32!(FalconModSelAlgo);

// TODO[FPRI]: replace with `FromPrimitive`.
impl TryFrom<u8> for FalconModSelAlgo {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self> {
        match value {
            1 => Ok(FalconModSelAlgo::Rsa3k),
            _ => Err(EINVAL),
        }
    }
}

/// Valid values for the `size` field of the [`crate::regs::NV_PFALCON_FALCON_DMATRFCMD`] register.
#[repr(u8)]
#[derive(Debug, Default, Copy, Clone, PartialEq, Eq)]
pub(crate) enum DmaTrfCmdSize {
    /// 256 bytes transfer.
    #[default]
    Size256B = 0x6,
}
impl_from_enum_to_u32!(DmaTrfCmdSize);

// TODO[FPRI]: replace with `FromPrimitive`.
impl TryFrom<u8> for DmaTrfCmdSize {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self> {
        match value {
            0x6 => Ok(Self::Size256B),
            _ => Err(EINVAL),
        }
    }
}

/// Currently active core on a dual falcon/riscv (Peregrine) controller.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub(crate) enum PeregrineCoreSelect {
    /// Falcon core is active.
    #[default]
    Falcon = 0,
    /// RISC-V core is active.
    Riscv = 1,
}
impl_from_enum_to_u32!(PeregrineCoreSelect);

impl From<bool> for PeregrineCoreSelect {
    fn from(value: bool) -> Self {
        match value {
            false => PeregrineCoreSelect::Falcon,
            true => PeregrineCoreSelect::Riscv,
        }
    }
}

/// Different types of memory present in a falcon core.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum FalconMem {
    /// Instruction Memory.
    Imem,
    /// Data Memory.
    Dmem,
}

/// Defines the Framebuffer Interface (FBIF) aperture type.
/// This determines the memory type for external memory access during a DMA transfer, which is
/// performed by the Falcon's Framebuffer DMA (FBDMA) engine. See falcon.rst for more details.
#[derive(Debug, Clone, Default)]
pub(crate) enum FalconFbifTarget {
    /// VRAM.
    #[default]
    /// Local Framebuffer (GPU's VRAM memory).
    LocalFb = 0,
    /// Coherent system memory (System DRAM).
    CoherentSysmem = 1,
    /// Non-coherent system memory (System DRAM).
    NoncoherentSysmem = 2,
}
impl_from_enum_to_u32!(FalconFbifTarget);

// TODO[FPRI]: replace with `FromPrimitive`.
impl TryFrom<u8> for FalconFbifTarget {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self> {
        let res = match value {
            0 => Self::LocalFb,
            1 => Self::CoherentSysmem,
            2 => Self::NoncoherentSysmem,
            _ => return Err(EINVAL),
        };

        Ok(res)
    }
}

/// Type of memory addresses to use.
#[derive(Debug, Clone, Default)]
pub(crate) enum FalconFbifMemType {
    /// Virtual memory addresses.
    #[default]
    Virtual = 0,
    /// Physical memory addresses.
    Physical = 1,
}
impl_from_enum_to_u32!(FalconFbifMemType);

/// Conversion from a single-bit register field.
impl From<bool> for FalconFbifMemType {
    fn from(value: bool) -> Self {
        match value {
            false => Self::Virtual,
            true => Self::Physical,
        }
    }
}

/// Type used to represent the `PFALCON` registers address base for a given falcon engine.
pub(crate) struct PFalconBase(());

/// Type used to represent the `PFALCON2` registers address base for a given falcon engine.
pub(crate) struct PFalcon2Base(());

/// Trait defining the parameters of a given Falcon engine.
///
/// Each engine provides one base for `PFALCON` and `PFALCON2` registers. The `ID` constant is used
/// to identify a given Falcon instance with register I/O methods.
pub(crate) trait FalconEngine:
    Send + Sync + RegisterBase<PFalconBase> + RegisterBase<PFalcon2Base> + Sized
{
    /// Singleton of the engine, used to identify it with register I/O methods.
    const ID: Self;
}

/// Represents a portion of the firmware to be loaded into a particular memory (e.g. IMEM or DMEM).
#[derive(Debug, Clone)]
pub(crate) struct FalconLoadTarget {
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

/// Trait for providing load parameters of falcon firmwares.
pub(crate) trait FalconLoadParams {
    /// Returns the load parameters for `IMEM`.
    fn imem_load_params(&self) -> FalconLoadTarget;

    /// Returns the load parameters for `DMEM`.
    fn dmem_load_params(&self) -> FalconLoadTarget;

    /// Returns the parameters to write into the BROM registers.
    fn brom_params(&self) -> FalconBromParams;

    /// Returns the start address of the firmware.
    fn boot_addr(&self) -> u32;
}

/// Trait for a falcon firmware.
///
/// A falcon firmware can be loaded on a given engine, and is presented in the form of a DMA
/// object.
pub(crate) trait FalconFirmware: FalconLoadParams + Deref<Target = DmaObject> {
    /// Engine on which this firmware is to be loaded.
    type Target: FalconEngine;
}

/// Contains the base parameters common to all Falcon instances.
pub(crate) struct Falcon<E: FalconEngine> {
    hal: KBox<dyn FalconHal<E>>,
    dev: ARef<device::Device>,
}

impl<E: FalconEngine + 'static> Falcon<E> {
    /// Create a new falcon instance.
    ///
    /// `need_riscv` is set to `true` if the caller expects the falcon to be a dual falcon/riscv
    /// controller.
    pub(crate) fn new(
        dev: &device::Device,
        chipset: Chipset,
        bar: &Bar0,
        need_riscv: bool,
    ) -> Result<Self> {
        let hwcfg1 = regs::NV_PFALCON_FALCON_HWCFG1::read(bar, &E::ID);
        // Check that the revision and security model contain valid values.
        let _ = hwcfg1.core_rev()?;
        let _ = hwcfg1.security_model()?;

        if need_riscv {
            let hwcfg2 = regs::NV_PFALCON_FALCON_HWCFG2::read(bar, &E::ID);
            if !hwcfg2.riscv() {
                dev_err!(
                    dev,
                    "riscv support requested on a controller that does not support it\n"
                );
                return Err(EINVAL);
            }
        }

        Ok(Self {
            hal: hal::falcon_hal(chipset)?,
            dev: dev.into(),
        })
    }

    /// Wait for memory scrubbing to complete.
    fn reset_wait_mem_scrubbing(&self, bar: &Bar0) -> Result {
        // TIMEOUT: memory scrubbing should complete in less than 20ms.
        util::wait_on(Delta::from_millis(20), || {
            if regs::NV_PFALCON_FALCON_HWCFG2::read(bar, &E::ID).mem_scrubbing_done() {
                Some(())
            } else {
                None
            }
        })
    }

    /// Reset the falcon engine.
    fn reset_eng(&self, bar: &Bar0) -> Result {
        let _ = regs::NV_PFALCON_FALCON_HWCFG2::read(bar, &E::ID);

        // According to OpenRM's `kflcnPreResetWait_GA102` documentation, HW sometimes does not set
        // RESET_READY so a non-failing timeout is used.
        let _ = util::wait_on(Delta::from_micros(150), || {
            let r = regs::NV_PFALCON_FALCON_HWCFG2::read(bar, &E::ID);
            if r.reset_ready() {
                Some(())
            } else {
                None
            }
        });

        regs::NV_PFALCON_FALCON_ENGINE::alter(bar, &E::ID, |v| v.set_reset(true));

        // TODO[DLAY]: replace with udelay() or equivalent once available.
        // TIMEOUT: falcon engine should not take more than 10us to reset.
        let _: Result = util::wait_on(Delta::from_micros(10), || None);

        regs::NV_PFALCON_FALCON_ENGINE::alter(bar, &E::ID, |v| v.set_reset(false));

        self.reset_wait_mem_scrubbing(bar)?;

        Ok(())
    }

    /// Reset the controller, select the falcon core, and wait for memory scrubbing to complete.
    pub(crate) fn reset(&self, bar: &Bar0) -> Result {
        self.reset_eng(bar)?;
        self.hal.select_core(self, bar)?;
        self.reset_wait_mem_scrubbing(bar)?;

        regs::NV_PFALCON_FALCON_RM::default()
            .set_value(regs::NV_PMC_BOOT_0::read(bar).into())
            .write(bar, &E::ID);

        Ok(())
    }

    /// Perform a DMA write according to `load_offsets` from `dma_handle` into the falcon's
    /// `target_mem`.
    ///
    /// `sec` is set if the loaded firmware is expected to run in secure mode.
    fn dma_wr<F: FalconFirmware<Target = E>>(
        &self,
        bar: &Bar0,
        fw: &F,
        target_mem: FalconMem,
        load_offsets: FalconLoadTarget,
        sec: bool,
    ) -> Result {
        const DMA_LEN: u32 = 256;

        // For IMEM, we want to use the start offset as a virtual address tag for each page, since
        // code addresses in the firmware (and the boot vector) are virtual.
        //
        // For DMEM we can fold the start offset into the DMA handle.
        let (src_start, dma_start) = match target_mem {
            FalconMem::Imem => (load_offsets.src_start, fw.dma_handle()),
            FalconMem::Dmem => (
                0,
                fw.dma_handle_with_offset(load_offsets.src_start as usize)?,
            ),
        };
        if dma_start % DmaAddress::from(DMA_LEN) > 0 {
            dev_err!(
                self.dev,
                "DMA transfer start addresses must be a multiple of {}",
                DMA_LEN
            );
            return Err(EINVAL);
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
                dev_err!(self.dev, "DMA transfer length overflow");
                return Err(EOVERFLOW);
            }
            Some(upper_bound) if upper_bound as usize > fw.size() => {
                dev_err!(self.dev, "DMA transfer goes beyond range of DMA object");
                return Err(EINVAL);
            }
            Some(_) => (),
        };

        // Set up the base source DMA address.

        regs::NV_PFALCON_FALCON_DMATRFBASE::default()
            .set_base((dma_start >> 8) as u32)
            .write(bar, &E::ID);
        regs::NV_PFALCON_FALCON_DMATRFBASE1::default()
            .set_base((dma_start >> 40) as u16)
            .write(bar, &E::ID);

        let cmd = regs::NV_PFALCON_FALCON_DMATRFCMD::default()
            .set_size(DmaTrfCmdSize::Size256B)
            .set_imem(target_mem == FalconMem::Imem)
            .set_sec(if sec { 1 } else { 0 });

        for pos in (0..num_transfers).map(|i| i * DMA_LEN) {
            // Perform a transfer of size `DMA_LEN`.
            regs::NV_PFALCON_FALCON_DMATRFMOFFS::default()
                .set_offs(load_offsets.dst_start + pos)
                .write(bar, &E::ID);
            regs::NV_PFALCON_FALCON_DMATRFFBOFFS::default()
                .set_offs(src_start + pos)
                .write(bar, &E::ID);
            cmd.write(bar, &E::ID);

            // Wait for the transfer to complete.
            // TIMEOUT: arbitrarily large value, no DMA transfer to the falcon's small memories
            // should ever take that long.
            util::wait_on(Delta::from_secs(2), || {
                let r = regs::NV_PFALCON_FALCON_DMATRFCMD::read(bar, &E::ID);
                if r.idle() {
                    Some(())
                } else {
                    None
                }
            })?;
        }

        Ok(())
    }

    /// Perform a DMA load into `IMEM` and `DMEM` of `fw`, and prepare the falcon to run it.
    pub(crate) fn dma_load<F: FalconFirmware<Target = E>>(&self, bar: &Bar0, fw: &F) -> Result {
        regs::NV_PFALCON_FBIF_CTL::alter(bar, &E::ID, |v| v.set_allow_phys_no_ctx(true));
        regs::NV_PFALCON_FALCON_DMACTL::default().write(bar, &E::ID);
        regs::NV_PFALCON_FBIF_TRANSCFG::alter(bar, &E::ID, 0, |v| {
            v.set_target(FalconFbifTarget::CoherentSysmem)
                .set_mem_type(FalconFbifMemType::Physical)
        });

        self.dma_wr(bar, fw, FalconMem::Imem, fw.imem_load_params(), true)?;
        self.dma_wr(bar, fw, FalconMem::Dmem, fw.dmem_load_params(), true)?;

        self.hal.program_brom(self, bar, &fw.brom_params())?;

        // Set `BootVec` to start of non-secure code.
        regs::NV_PFALCON_FALCON_BOOTVEC::default()
            .set_value(fw.boot_addr())
            .write(bar, &E::ID);

        Ok(())
    }

    /// Runs the loaded firmware and waits for its completion.
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
        if let Some(mbox0) = mbox0 {
            regs::NV_PFALCON_FALCON_MAILBOX0::default()
                .set_value(mbox0)
                .write(bar, &E::ID);
        }

        if let Some(mbox1) = mbox1 {
            regs::NV_PFALCON_FALCON_MAILBOX1::default()
                .set_value(mbox1)
                .write(bar, &E::ID);
        }

        match regs::NV_PFALCON_FALCON_CPUCTL::read(bar, &E::ID).alias_en() {
            true => regs::NV_PFALCON_FALCON_CPUCTL_ALIAS::default()
                .set_startcpu(true)
                .write(bar, &E::ID),
            false => regs::NV_PFALCON_FALCON_CPUCTL::default()
                .set_startcpu(true)
                .write(bar, &E::ID),
        }

        // TIMEOUT: arbitrarily large value, firmwares should complete in less than 2 seconds.
        util::wait_on(Delta::from_secs(2), || {
            let r = regs::NV_PFALCON_FALCON_CPUCTL::read(bar, &E::ID);
            if r.halted() {
                Some(())
            } else {
                None
            }
        })?;

        let (mbox0, mbox1) = (
            regs::NV_PFALCON_FALCON_MAILBOX0::read(bar, &E::ID).value(),
            regs::NV_PFALCON_FALCON_MAILBOX1::read(bar, &E::ID).value(),
        );

        Ok((mbox0, mbox1))
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
}
