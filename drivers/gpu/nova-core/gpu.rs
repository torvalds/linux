// SPDX-License-Identifier: GPL-2.0

use kernel::{device, devres::Devres, error::code::*, pci, prelude::*, sync::Arc};

use crate::driver::Bar0;
use crate::falcon::{gsp::Gsp, sec2::Sec2, Falcon};
use crate::fb::FbLayout;
use crate::fb::SysmemFlush;
use crate::firmware::fwsec::{FwsecCommand, FwsecFirmware};
use crate::firmware::{Firmware, FIRMWARE_VERSION};
use crate::gfw;
use crate::regs;
use crate::util;
use crate::vbios::Vbios;
use core::fmt;

macro_rules! define_chipset {
    ({ $($variant:ident = $value:expr),* $(,)* }) =>
    {
        /// Enum representation of the GPU chipset.
        #[derive(fmt::Debug, Copy, Clone, PartialOrd, Ord, PartialEq, Eq)]
        pub(crate) enum Chipset {
            $($variant = $value),*,
        }

        impl Chipset {
            pub(crate) const ALL: &'static [Chipset] = &[
                $( Chipset::$variant, )*
            ];

            pub(crate) const NAMES: [&'static str; Self::ALL.len()] = [
                $( util::const_bytes_to_str(
                        util::to_lowercase_bytes::<{ stringify!($variant).len() }>(
                            stringify!($variant)
                        ).as_slice()
                ), )*
            ];
        }

        // TODO[FPRI]: replace with something like derive(FromPrimitive)
        impl TryFrom<u32> for Chipset {
            type Error = kernel::error::Error;

            fn try_from(value: u32) -> Result<Self, Self::Error> {
                match value {
                    $( $value => Ok(Chipset::$variant), )*
                    _ => Err(ENODEV),
                }
            }
        }
    }
}

define_chipset!({
    // Turing
    TU102 = 0x162,
    TU104 = 0x164,
    TU106 = 0x166,
    TU117 = 0x167,
    TU116 = 0x168,
    // Ampere
    GA100 = 0x170,
    GA102 = 0x172,
    GA103 = 0x173,
    GA104 = 0x174,
    GA106 = 0x176,
    GA107 = 0x177,
    // Ada
    AD102 = 0x192,
    AD103 = 0x193,
    AD104 = 0x194,
    AD106 = 0x196,
    AD107 = 0x197,
});

impl Chipset {
    pub(crate) fn arch(&self) -> Architecture {
        match self {
            Self::TU102 | Self::TU104 | Self::TU106 | Self::TU117 | Self::TU116 => {
                Architecture::Turing
            }
            Self::GA100 | Self::GA102 | Self::GA103 | Self::GA104 | Self::GA106 | Self::GA107 => {
                Architecture::Ampere
            }
            Self::AD102 | Self::AD103 | Self::AD104 | Self::AD106 | Self::AD107 => {
                Architecture::Ada
            }
        }
    }
}

// TODO
//
// The resulting strings are used to generate firmware paths, hence the
// generated strings have to be stable.
//
// Hence, replace with something like strum_macros derive(Display).
//
// For now, redirect to fmt::Debug for convenience.
impl fmt::Display for Chipset {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{self:?}")
    }
}

/// Enum representation of the GPU generation.
#[derive(fmt::Debug)]
pub(crate) enum Architecture {
    Turing = 0x16,
    Ampere = 0x17,
    Ada = 0x19,
}

impl TryFrom<u8> for Architecture {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self> {
        match value {
            0x16 => Ok(Self::Turing),
            0x17 => Ok(Self::Ampere),
            0x19 => Ok(Self::Ada),
            _ => Err(ENODEV),
        }
    }
}

pub(crate) struct Revision {
    major: u8,
    minor: u8,
}

impl Revision {
    fn from_boot0(boot0: regs::NV_PMC_BOOT_0) -> Self {
        Self {
            major: boot0.major_revision(),
            minor: boot0.minor_revision(),
        }
    }
}

impl fmt::Display for Revision {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:x}.{:x}", self.major, self.minor)
    }
}

/// Structure holding the metadata of the GPU.
pub(crate) struct Spec {
    chipset: Chipset,
    /// The revision of the chipset.
    revision: Revision,
}

impl Spec {
    fn new(bar: &Bar0) -> Result<Spec> {
        let boot0 = regs::NV_PMC_BOOT_0::read(bar);

        Ok(Self {
            chipset: boot0.chipset()?,
            revision: Revision::from_boot0(boot0),
        })
    }
}

/// Structure holding the resources required to operate the GPU.
#[pin_data(PinnedDrop)]
pub(crate) struct Gpu {
    spec: Spec,
    /// MMIO mapping of PCI BAR 0
    bar: Arc<Devres<Bar0>>,
    fw: Firmware,
    /// System memory page required for flushing all pending GPU-side memory writes done through
    /// PCIE into system memory, via sysmembar (A GPU-initiated HW memory-barrier operation).
    sysmem_flush: SysmemFlush,
}

#[pinned_drop]
impl PinnedDrop for Gpu {
    fn drop(self: Pin<&mut Self>) {
        // Unregister the sysmem flush page before we release it.
        self.bar
            .try_access_with(|b| self.sysmem_flush.unregister(b));
    }
}

impl Gpu {
    /// Helper function to load and run the FWSEC-FRTS firmware and confirm that it has properly
    /// created the WPR2 region.
    ///
    /// TODO: this needs to be moved into a larger type responsible for booting the whole GSP
    /// (`GspBooter`?).
    fn run_fwsec_frts(
        dev: &device::Device<device::Bound>,
        falcon: &Falcon<Gsp>,
        bar: &Bar0,
        bios: &Vbios,
        fb_layout: &FbLayout,
    ) -> Result<()> {
        // Check that the WPR2 region does not already exists - if it does, we cannot run
        // FWSEC-FRTS until the GPU is reset.
        if regs::NV_PFB_PRI_MMU_WPR2_ADDR_HI::read(bar).higher_bound() != 0 {
            dev_err!(
                dev,
                "WPR2 region already exists - GPU needs to be reset to proceed\n"
            );
            return Err(EBUSY);
        }

        let fwsec_frts = FwsecFirmware::new(
            dev,
            falcon,
            bar,
            bios,
            FwsecCommand::Frts {
                frts_addr: fb_layout.frts.start,
                frts_size: fb_layout.frts.end - fb_layout.frts.start,
            },
        )?;

        // Run FWSEC-FRTS to create the WPR2 region.
        fwsec_frts.run(dev, falcon, bar)?;

        // SCRATCH_E contains the error code for FWSEC-FRTS.
        let frts_status = regs::NV_PBUS_SW_SCRATCH_0E::read(bar).frts_err_code();
        if frts_status != 0 {
            dev_err!(
                dev,
                "FWSEC-FRTS returned with error code {:#x}",
                frts_status
            );

            return Err(EIO);
        }

        // Check that the WPR2 region has been created as we requested.
        let (wpr2_lo, wpr2_hi) = (
            regs::NV_PFB_PRI_MMU_WPR2_ADDR_LO::read(bar).lower_bound(),
            regs::NV_PFB_PRI_MMU_WPR2_ADDR_HI::read(bar).higher_bound(),
        );

        match (wpr2_lo, wpr2_hi) {
            (_, 0) => {
                dev_err!(dev, "WPR2 region not created after running FWSEC-FRTS\n");

                Err(EIO)
            }
            (wpr2_lo, _) if wpr2_lo != fb_layout.frts.start => {
                dev_err!(
                    dev,
                    "WPR2 region created at unexpected address {:#x}; expected {:#x}\n",
                    wpr2_lo,
                    fb_layout.frts.start,
                );

                Err(EIO)
            }
            (wpr2_lo, wpr2_hi) => {
                dev_dbg!(dev, "WPR2: {:#x}-{:#x}\n", wpr2_lo, wpr2_hi);
                dev_dbg!(dev, "GPU instance built\n");

                Ok(())
            }
        }
    }

    pub(crate) fn new(
        pdev: &pci::Device<device::Bound>,
        devres_bar: Arc<Devres<Bar0>>,
    ) -> Result<impl PinInit<Self>> {
        let bar = devres_bar.access(pdev.as_ref())?;
        let spec = Spec::new(bar)?;
        let fw = Firmware::new(pdev.as_ref(), spec.chipset, FIRMWARE_VERSION)?;

        dev_info!(
            pdev.as_ref(),
            "NVIDIA (Chipset: {}, Architecture: {:?}, Revision: {})\n",
            spec.chipset,
            spec.chipset.arch(),
            spec.revision
        );

        // We must wait for GFW_BOOT completion before doing any significant setup on the GPU.
        gfw::wait_gfw_boot_completion(bar)
            .inspect_err(|_| dev_err!(pdev.as_ref(), "GFW boot did not complete"))?;

        let sysmem_flush = SysmemFlush::register(pdev.as_ref(), bar, spec.chipset)?;

        let gsp_falcon = Falcon::<Gsp>::new(
            pdev.as_ref(),
            spec.chipset,
            bar,
            spec.chipset > Chipset::GA100,
        )?;
        gsp_falcon.clear_swgen0_intr(bar);

        let _sec2_falcon = Falcon::<Sec2>::new(pdev.as_ref(), spec.chipset, bar, true)?;

        let fb_layout = FbLayout::new(spec.chipset, bar)?;
        dev_dbg!(pdev.as_ref(), "{:#x?}\n", fb_layout);

        let bios = Vbios::new(pdev, bar)?;

        Self::run_fwsec_frts(pdev.as_ref(), &gsp_falcon, bar, &bios, &fb_layout)?;

        Ok(pin_init!(Self {
            spec,
            bar: devres_bar,
            fw,
            sysmem_flush,
        }))
    }
}
