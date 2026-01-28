// SPDX-License-Identifier: GPL-2.0

use kernel::{
    device,
    devres::Devres,
    fmt,
    pci,
    prelude::*,
    sync::Arc, //
};

use crate::{
    driver::Bar0,
    falcon::{
        gsp::Gsp as GspFalcon,
        sec2::Sec2 as Sec2Falcon,
        Falcon, //
    },
    fb::SysmemFlush,
    gfw,
    gsp::Gsp,
    regs,
};

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

            ::kernel::macros::paste!(
            /// Returns the name of this chipset, in lowercase.
            ///
            /// # Examples
            ///
            /// ```
            /// let chipset = Chipset::GA102;
            /// assert_eq!(chipset.name(), "ga102");
            /// ```
            pub(crate) const fn name(&self) -> &'static str {
                match *self {
                $(
                    Chipset::$variant => stringify!([<$variant:lower>]),
                )*
                }
            }
            );
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
///
/// TODO: remove the `Default` trait implementation, and the `#[default]`
/// attribute, once the register!() macro (which creates Architecture items) no
/// longer requires it for read-only fields.
#[derive(fmt::Debug, Default, Copy, Clone)]
#[repr(u8)]
pub(crate) enum Architecture {
    #[default]
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

impl From<Architecture> for u8 {
    fn from(value: Architecture) -> Self {
        // CAST: `Architecture` is `repr(u8)`, so this cast is always lossless.
        value as u8
    }
}

pub(crate) struct Revision {
    major: u8,
    minor: u8,
}

impl From<regs::NV_PMC_BOOT_42> for Revision {
    fn from(boot0: regs::NV_PMC_BOOT_42) -> Self {
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

/// Structure holding a basic description of the GPU: `Chipset` and `Revision`.
pub(crate) struct Spec {
    chipset: Chipset,
    revision: Revision,
}

impl Spec {
    fn new(dev: &device::Device, bar: &Bar0) -> Result<Spec> {
        // Some brief notes about boot0 and boot42, in chronological order:
        //
        // NV04 through NV50:
        //
        //    Not supported by Nova. boot0 is necessary and sufficient to identify these GPUs.
        //    boot42 may not even exist on some of these GPUs.
        //
        // Fermi through Volta:
        //
        //     Not supported by Nova. boot0 is still sufficient to identify these GPUs, but boot42
        //     is also guaranteed to be both present and accurate.
        //
        // Turing and later:
        //
        //     Supported by Nova. Identified by first checking boot0 to ensure that the GPU is not
        //     from an earlier (pre-Fermi) era, and then using boot42 to precisely identify the GPU.
        //     Somewhere in the Rubin timeframe, boot0 will no longer have space to add new GPU IDs.

        let boot0 = regs::NV_PMC_BOOT_0::read(bar);

        if boot0.is_older_than_fermi() {
            return Err(ENODEV);
        }

        let boot42 = regs::NV_PMC_BOOT_42::read(bar);
        Spec::try_from(boot42).inspect_err(|_| {
            dev_err!(dev, "Unsupported chipset: {}\n", boot42);
        })
    }
}

impl TryFrom<regs::NV_PMC_BOOT_42> for Spec {
    type Error = Error;

    fn try_from(boot42: regs::NV_PMC_BOOT_42) -> Result<Self> {
        Ok(Self {
            chipset: boot42.chipset()?,
            revision: boot42.into(),
        })
    }
}

impl fmt::Display for Spec {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_fmt(fmt!(
            "Chipset: {}, Architecture: {:?}, Revision: {}",
            self.chipset,
            self.chipset.arch(),
            self.revision
        ))
    }
}

/// Structure holding the resources required to operate the GPU.
#[pin_data]
pub(crate) struct Gpu {
    spec: Spec,
    /// MMIO mapping of PCI BAR 0
    bar: Arc<Devres<Bar0>>,
    /// System memory page required for flushing all pending GPU-side memory writes done through
    /// PCIE into system memory, via sysmembar (A GPU-initiated HW memory-barrier operation).
    sysmem_flush: SysmemFlush,
    /// GSP falcon instance, used for GSP boot up and cleanup.
    gsp_falcon: Falcon<GspFalcon>,
    /// SEC2 falcon instance, used for GSP boot up and cleanup.
    sec2_falcon: Falcon<Sec2Falcon>,
    /// GSP runtime data. Temporarily an empty placeholder.
    #[pin]
    gsp: Gsp,
}

impl Gpu {
    pub(crate) fn new<'a>(
        pdev: &'a pci::Device<device::Bound>,
        devres_bar: Arc<Devres<Bar0>>,
        bar: &'a Bar0,
    ) -> impl PinInit<Self, Error> + 'a {
        try_pin_init!(Self {
            spec: Spec::new(pdev.as_ref(), bar).inspect(|spec| {
                dev_info!(pdev.as_ref(),"NVIDIA ({})\n", spec);
            })?,

            // We must wait for GFW_BOOT completion before doing any significant setup on the GPU.
            _: {
                gfw::wait_gfw_boot_completion(bar)
                    .inspect_err(|_| dev_err!(pdev.as_ref(), "GFW boot did not complete\n"))?;
            },

            sysmem_flush: SysmemFlush::register(pdev.as_ref(), bar, spec.chipset)?,

            gsp_falcon: Falcon::new(
                pdev.as_ref(),
                spec.chipset,
            )
            .inspect(|falcon| falcon.clear_swgen0_intr(bar))?,

            sec2_falcon: Falcon::new(pdev.as_ref(), spec.chipset)?,

            gsp <- Gsp::new(pdev),

            _: { gsp.boot(pdev, bar, spec.chipset, gsp_falcon, sec2_falcon)? },

            bar: devres_bar,
        })
    }

    /// Called when the corresponding [`Device`](device::Device) is unbound.
    ///
    /// Note: This method must only be called from `Driver::unbind`.
    pub(crate) fn unbind(&self, dev: &device::Device<device::Core>) {
        kernel::warn_on!(self
            .bar
            .access(dev)
            .inspect(|bar| self.sysmem_flush.unregister(bar))
            .is_err());
    }
}
