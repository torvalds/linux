// SPDX-License-Identifier: GPL-2.0

use core::ops::Range;

use kernel::{
    device,
    dma::Device,
    fmt,
    io::Io,
    num::Bounded,
    pci,
    prelude::*,
    sizes::SizeConstants, //
};

use crate::{
    bounded_enum,
    driver::Bar0,
    falcon::{
        gsp::Gsp as GspFalcon,
        sec2::Sec2 as Sec2Falcon,
        Falcon, //
    },
    fb::SysmemFlush,
    fsp::Fsp,
    gsp::{
        self,
        commands::GetGspStaticInfoReply,
        Gsp,
        GspBootContext, //
    },
    regs,
    vgpu::VgpuManager, //
};

mod hal;

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
    // Hopper
    GH100 = 0x180,
    // Ada
    AD102 = 0x192,
    AD103 = 0x193,
    AD104 = 0x194,
    AD106 = 0x196,
    AD107 = 0x197,
    // Blackwell GB10x
    GB100 = 0x1a0,
    GB102 = 0x1a2,
    // Blackwell GB20x
    GB202 = 0x1b2,
    GB203 = 0x1b3,
    GB205 = 0x1b5,
    GB206 = 0x1b6,
    GB207 = 0x1b7,
});

impl Chipset {
    pub(crate) const fn arch(self) -> Architecture {
        match self {
            Self::TU102 | Self::TU104 | Self::TU106 | Self::TU117 | Self::TU116 => {
                Architecture::Turing
            }
            Self::GA100 | Self::GA102 | Self::GA103 | Self::GA104 | Self::GA106 | Self::GA107 => {
                Architecture::Ampere
            }
            Self::GH100 => Architecture::Hopper,
            Self::AD102 | Self::AD103 | Self::AD104 | Self::AD106 | Self::AD107 => {
                Architecture::Ada
            }
            Self::GB100 | Self::GB102 => Architecture::BlackwellGB10x,
            Self::GB202 | Self::GB203 | Self::GB205 | Self::GB206 | Self::GB207 => {
                Architecture::BlackwellGB20x
            }
        }
    }

    /// Returns the address range of the PCI config mirror space.
    pub(crate) fn pci_config_mirror_range(self) -> Range<u32> {
        hal::gpu_hal(self).pci_config_mirror_range()
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

bounded_enum! {
    /// Enum representation of the GPU generation.
    #[derive(fmt::Debug, Copy, Clone)]
    pub(crate) enum Architecture with TryFrom<Bounded<u32, 6>> {
        Turing = 0x16,
        Ampere = 0x17,
        Hopper = 0x18,
        Ada = 0x19,
        BlackwellGB10x = 0x1a,
        BlackwellGB20x = 0x1b,
    }
}

#[derive(Clone, Copy)]
pub(crate) struct Revision {
    major: Bounded<u8, 4>,
    minor: Bounded<u8, 4>,
}

impl From<regs::NV_PMC_BOOT_42> for Revision {
    fn from(boot0: regs::NV_PMC_BOOT_42) -> Self {
        Self {
            major: boot0.major_revision().cast(),
            minor: boot0.minor_revision().cast(),
        }
    }
}

impl fmt::Display for Revision {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:x}.{:x}", self.major, self.minor)
    }
}

/// Structure holding a basic description of the GPU: `Chipset` and `Revision`.
#[derive(Clone, Copy)]
pub(crate) struct Spec {
    chipset: Chipset,
    revision: Revision,
}

impl Spec {
    fn new(dev: &device::Device, bar: Bar0<'_>) -> Result<Spec> {
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

        let boot0 = bar.read(regs::NV_PMC_BOOT_0);

        if boot0.is_older_than_fermi() {
            return Err(ENODEV);
        }

        let boot42 = bar.read(regs::NV_PMC_BOOT_42);
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

/// Self-contained resources to operate and drop the GSP.
#[pin_data(PinnedDrop)]
struct GspResources<'gpu> {
    /// Device owning the GPU.
    device: &'gpu pci::Device<device::Bound>,
    /// Details about the chipset.
    spec: Spec,
    /// MMIO mapping of PCI BAR 0.
    bar: Bar0<'gpu>,
    /// GSP falcon instance, used for GSP boot up and cleanup.
    gsp_falcon: Falcon<'gpu, GspFalcon>,
    /// SEC2 falcon instance, used for GSP boot up and cleanup.
    sec2_falcon: Falcon<'gpu, Sec2Falcon>,
    /// FSP instance, if on an arch that supports it.
    // TODO: use different resource types for each boot method, and make the relevant Gsp methods
    // generic against them.
    fsp: Option<Fsp<'gpu>>,
    /// vGPU state detected before GSP boot.
    vgpu: VgpuManager,
    /// GSP runtime data.
    #[pin]
    gsp: Gsp,
    /// GSP unload firmware bundle, if any.
    unload_bundle: Option<gsp::UnloadBundle>,
}

/// Structure holding the resources required to operate the GPU.
#[pin_data]
pub(crate) struct Gpu<'gpu> {
    spec: Spec,
    /// Static GPU information as provided by the GSP.
    gsp_static_info: GetGspStaticInfoReply,
    /// GSP and its resources.
    #[pin]
    gsp_resources: GspResources<'gpu>,
    /// System memory page required for flushing all pending GPU-side memory writes done through
    /// PCIE into system memory, via sysmembar (A GPU-initiated HW memory-barrier operation).
    ///
    /// Must be kept declared *after* `gsp_resources`, as the latter's `PinnedDrop` implementation
    /// requires the sysmem flush page to be in place.
    sysmem_flush: SysmemFlush<'gpu>,
}

#[pinned_drop]
impl PinnedDrop for GspResources<'_> {
    fn drop(self: Pin<&mut Self>) {
        let this = self.project();
        let device = *this.device;
        let bar = *this.bar;
        let bundle = this.unload_bundle.take();

        let _ = this
            .gsp
            .as_ref()
            .get_ref()
            .unload(
                GspBootContext {
                    pdev: device,
                    bar,
                    chipset: this.spec.chipset,
                    gsp_falcon: &*this.gsp_falcon,
                    sec2_falcon: &*this.sec2_falcon,
                    fsp: this.fsp.as_mut(),
                    vgpu: &*this.vgpu,
                },
                bundle,
            )
            .inspect_err(|e| dev_err!(device, "failed to unload GSP: {:?}\n", e));
    }
}

impl<'gpu> Gpu<'gpu> {
    pub(crate) fn new<'a>(
        pdev: &'gpu pci::Device<device::Core<'a>>,
        bar: Bar0<'gpu>,
    ) -> impl PinInit<Self, Error> + use<'gpu, 'a> {
        let dev = pdev.as_ref();

        try_pin_init!(Self {
            spec: Spec::new(dev, bar).inspect(|spec| {
                dev_info!(dev,"NVIDIA ({})\n", spec);
            })?,

            // We must wait for GFW_BOOT completion before doing any significant setup on the GPU.
            _: {
                let hal = hal::gpu_hal(spec.chipset);
                let dma_mask = hal.dma_mask();

                // SAFETY: `Gpu` owns all DMA allocations for this device, and we are
                // still constructing it, so no concurrent DMA allocations can exist.
                unsafe { pdev.dma_set_mask_and_coherent(dma_mask)? };

                hal.wait_gfw_boot_completion(bar)
                    .inspect_err(|_| dev_err!(dev, "GFW boot did not complete\n"))?;
            },

            // Initialize this early because `gsp_resources` depends on it.
            sysmem_flush: SysmemFlush::register(dev, bar, spec.chipset)?,

            gsp_resources <- try_pin_init!(GspResources {
                device: pdev,

                spec: *spec,

                bar,

                gsp_falcon: Falcon::new(
                    dev,
                    spec.chipset,
                    bar
                )
                .inspect(|falcon| falcon.clear_swgen0_intr())?,

                sec2_falcon: Falcon::new(dev, spec.chipset, bar)?,

                fsp: Fsp::try_new(dev, bar, spec.chipset)?,

                vgpu: VgpuManager::new(pdev, spec.chipset, fsp.as_mut()),

                gsp <- Gsp::new(pdev),

                // This member must be initialized last, so the `UnloadBundle` can never be dropped
                // from outside of the constructed `GspResources`, ensuring that the unload sequence
                // is properly run in case of failure.
                unload_bundle: gsp.boot(GspBootContext {
                    pdev,
                    bar,
                    chipset: spec.chipset,
                    gsp_falcon,
                    sec2_falcon,
                    fsp: fsp.as_mut(),
                    vgpu,
                })?,
            }),

            gsp_static_info: {
                // Obtain and display basic GPU information.
                let info = gsp_resources.gsp.get_static_info(bar)?;
                match info.gpu_name() {
                    Ok(name) => dev_info!(dev, "GPU name: {}\n", name),
                    Err(e) => dev_warn!(dev, "GPU name unavailable: {:?}\n", e),
                }

                if !info.usable_fb_regions.is_empty() {
                    dev_dbg!(dev, "Usable FB regions:\n");
                    for region in &info.usable_fb_regions {
                        dev_dbg!(dev, "  - {:#x?}\n", region);
                    }

                    dev_dbg!(
                        dev,
                        "Total usable VRAM: {} MiB\n",
                        info.usable_fb_regions.iter().fold(0u64, |res, region| res
                            .saturating_add(region.end - region.start))
                            / u64::SZ_1M
                    );
                }

                info
            }
        })
    }
}
