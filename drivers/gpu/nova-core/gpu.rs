// SPDX-License-Identifier: GPL-2.0

use core::ops::Range;

use kernel::{
    device,
    dma::Device,
    fmt,
    gpu::buddy::GpuBuddyParams,
    io::Io,
    num::Bounded,
    pci,
    prelude::*,
    ptr::Alignment,
    sizes::{
        SizeConstants,
        SZ_4K, //
    },
    uapi, //
};

use crate::{
    bounded_enum,
    driver::{
        Bar0,
        Bar1, //
    },
    falcon::{
        gsp::Gsp as GspFalcon,
        sec2::Sec2 as Sec2Falcon,
        Falcon, //
    },
    fb::SysmemFlush,
    fsp::Fsp,
    gsp::{
        self,
        cmdq::Cmdq,
        commands::GetGspStaticInfoReply,
        Gsp,
        GspBootContext, //
    },
    irq::{
        self,
        gsp::GspIrq,
        interrupt_tree::{
            TopEnableGuard,
            Tree, //
        }, //
    },
    mm::{
        bar_user::BarUser,
        pagetable::MmuVersion,
        GpuMm,
        VramAddress, //
    },
    vgpu::VgpuManager, //
};

mod hal;
mod regs;

macro_rules! define_chipset {
    ({ $($variant:ident),* $(,)* }) =>
    {
        ::kernel::macros::paste!(
        /// Enum representation of the GPU chipset.
        #[derive(fmt::Debug, Copy, Clone, PartialOrd, Ord, PartialEq, Eq)]
        #[repr(u32)]
        #[allow(missing_docs)]
        pub enum Chipset {
            $($variant = uapi::[<drm_nova_chipid_NOVA_DRM_CHIPID_ $variant:upper>]),*,
        }

        impl Chipset {
            /// All chipsets known to the driver.
            pub const ALL: &'static [Chipset] = &[
                $( Chipset::$variant, )*
            ];

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
        }

        // TODO[FPRI]: replace with something like derive(FromPrimitive)
        impl TryFrom<u32> for Chipset {
            type Error = kernel::error::Error;

            fn try_from(value: u32) -> Result<Self, Self::Error> {
                match value {
                    $(
                        uapi::[<drm_nova_chipid_NOVA_DRM_CHIPID_ $variant:upper>] => {
                            Ok(Chipset::$variant)
                        }
                    )*
                    _ => Err(ENODEV),
                }
            }
        }
    );
    }
}

define_chipset!({
    // Turing
    TU102,
    TU104,
    TU106,
    TU117,
    TU116,
    // Ampere
    GA100,
    GA102,
    GA103,
    GA104,
    GA106,
    GA107,
    // Hopper
    GH100,
    // Ada
    AD102,
    AD103,
    AD104,
    AD106,
    AD107,
    // Blackwell GB10x
    GB100,
    GB102,
    // Blackwell GB20x
    GB202,
    GB203,
    GB205,
    GB206,
    GB207,
});

impl Chipset {
    /// Returns the [`Architecture`] generation of this chipset.
    pub const fn arch(self) -> Architecture {
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

    /// Returns the MMU version for this chipset.
    pub(crate) fn mmu_version(self) -> MmuVersion {
        MmuVersion::from(self.arch())
    }
}

impl From<Chipset> for u32 {
    #[inline]
    fn from(value: Chipset) -> Self {
        // CAST: `Chipset` is `repr(u32)` and can thus be cast losslessly.
        value as u32
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
    #[repr(u32)]
    pub enum Architecture with TryFrom<Bounded<u32, 6>> {
        /// Turing (TU1xx).
        Turing = uapi::drm_nova_architecture_NOVA_DRM_ARCHITECTURE_TURING,
        /// Ampere (GA10x).
        Ampere = uapi::drm_nova_architecture_NOVA_DRM_ARCHITECTURE_AMPERE,
        /// Hopper (GH100).
        Hopper = uapi::drm_nova_architecture_NOVA_DRM_ARCHITECTURE_HOPPER,
        /// Ada Lovelace (AD10x).
        Ada = uapi::drm_nova_architecture_NOVA_DRM_ARCHITECTURE_ADA,
        /// Blackwell (GB10x).
        BlackwellGB10x =
            uapi::drm_nova_architecture_NOVA_DRM_ARCHITECTURE_BLACKWELL_GB10X,
        /// Blackwell (GB20x).
        BlackwellGB20x =
            uapi::drm_nova_architecture_NOVA_DRM_ARCHITECTURE_BLACKWELL_GB20X,
    }
}

impl From<Architecture> for u32 {
    #[inline]
    fn from(value: Architecture) -> Self {
        // CAST: `Architecture` is `repr(u32)` and can thus be cast losslessly.
        value as u32
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
pub struct Spec {
    /// The GPU chipset.
    pub chipset: Chipset,
    revision: Revision,
}

impl Spec {
    pub(crate) fn new(dev: &device::Device, bar: Bar0<'_>) -> Result<Spec> {
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
    gsp: Gsp<'gpu>,
    /// GSP unload firmware bundle, if any.
    unload_bundle: Option<gsp::UnloadBundle<'gpu>>,
}

/// The GSP event handler's registration and the enable of its subtree at `TOP`.
///
/// The two drop as a unit, the handler first, on the drop of [`Gpu`] and on the error path of
/// its constructor alike, so that the subtree is disabled only after the handler is freed.
#[pin_data]
struct GspSubtree<'a> {
    #[pin]
    irq: GspIrq<'a>,
    /// Must be kept declared *after* `irq`. A handler still in flight enables the subtree again
    /// through its rearm.
    _top: TopEnableGuard<'a>,
}

impl<'a> GspSubtree<'a> {
    /// Returns an initializer that registers the GSP event handler and then enables its subtree at
    /// `TOP`.
    ///
    /// # Safety
    ///
    /// Callers must not `mem::forget()` the initialized `GspSubtree` or otherwise prevent its
    /// [`Drop`] implementation, which runs `free_irq`, from running.
    unsafe fn new(
        pdev: &'a pci::Device<device::Bound>,
        tree: &'a Tree<'a>,
        falcon: &'a Falcon<'a, GspFalcon>,
        cmdq: &'a Cmdq<'a>,
    ) -> impl PinInit<Self, Error> + 'a {
        try_pin_init!(Self {
            // SAFETY: this function's caller must not leak the `GspSubtree` that owns this
            // registration, so the registration's `Drop` runs.
            irq <- unsafe { GspIrq::new(pdev, tree, falcon, cmdq) },
            _top: tree.enable_top_guarded(),
        })
    }
}

/// Structure holding the resources required to operate the GPU.
#[pin_data]
pub(crate) struct Gpu<'gpu> {
    pub(crate) spec: Spec,
    /// GSP event interrupt registration, and the enable of its subtree.
    ///
    /// Must be kept declared *before* `gsp_resources`, so that the handler is unregistered, and
    /// any in-flight run of it has finished, before the command queue that it drains and the
    /// falcon that it reads are freed, and before the GSP is unloaded.
    #[pin]
    _gsp_subtree: GspSubtree<'gpu>,
    /// Static GPU information as provided by the GSP.
    pub(crate) gsp_static_info: GetGspStaticInfoReply,
    /// GPU memory manager owning memory management resources.
    ///
    /// Must be kept declared *before* `gsp_resources`, so that its components are dropped while
    /// the GSP is still operational.
    mm: GpuMm<'gpu>,
    /// BAR1 user interface for CPU access to GPU virtual memory.
    #[pin]
    bar_user: BarUser<'gpu>,
    /// GSP and its resources.
    #[pin]
    gsp_resources: GspResources<'gpu>,
    /// System memory page required for flushing all pending GPU-side memory writes done through
    /// PCIE into system memory, via sysmembar (A GPU-initiated HW memory-barrier operation).
    ///
    /// Must be kept declared *after* `gsp_resources`, as the latter's `PinnedDrop` implementation
    /// requires the sysmem flush page to be in place.
    sysmem_flush: SysmemFlush<'gpu>,
    /// Borrow of `tree` that `_gsp_subtree` holds. A field that borrows a sibling field is
    /// self-referential, which `pin_init` cannot express, so the borrow is taken by hand.
    tree_ref: &'gpu Tree<'gpu>,
    /// The GIN CPU interrupt tree and the PCI vectors that deliver it.
    ///
    /// Must be kept declared *after* `_gsp_subtree`, which holds a borrow of it.
    #[pin]
    tree: Tree<'gpu>,
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
        bar1: Bar1<'gpu>,
    ) -> impl PinInit<Self, Error> + use<'gpu, 'a> {
        let dev = pdev.as_ref();

        try_pin_init!(Self {
            spec: Spec::new(dev, bar).inspect(|spec| {
                dev_info!(dev,"NVIDIA ({})\n", spec);
            })?,

            tree: Tree::new(pdev, bar, spec.chipset, irq::gsp::GSP_SUBTREE.into())?,

            // SAFETY: `tree` is initialized above, is pinned at a stable address, and is dropped
            // after every field that uses `tree_ref` (struct field drop order).
            tree_ref: unsafe { &*core::ptr::from_ref(tree.as_ref().get_ref()) },

            _: {
                let dma_mask = hal::gpu_hal(spec.chipset).dma_mask();

                // SAFETY: `Gpu` owns all DMA allocations for this device, and we are
                // still constructing it, so no concurrent DMA allocations can exist.
                unsafe { pdev.dma_set_mask_and_coherent(dma_mask)? };

                // Nova walks SG segments to build page tables, so their length is
                // irrelevant to the device.
                //
                // SAFETY: `Gpu` owns all DMA allocations for this device, and we are
                // still constructing it, so no concurrent DMA allocations can exist.
                unsafe { pdev.dma_set_max_seg_size(u32::MAX) };

                // We must wait for GFW_BOOT completion before doing any significant setup on the GPU.
                Self::wait_gfw_boot_completion(dev, bar, spec.chipset)?;
            },

            // Initialize this early because `gsp_resources` depends on it.
            sysmem_flush: SysmemFlush::register(dev, bar, spec.chipset)?,

            gsp_resources <- try_pin_init!(GspResources {
                device: pdev,

                spec: *spec,

                bar,

                gsp_falcon: Falcon::new(dev, spec.chipset, bar)?,

                sec2_falcon: Falcon::new(dev, spec.chipset, bar)?,

                fsp: Fsp::try_new(dev, bar, spec.chipset)?,

                vgpu: VgpuManager::new(pdev, spec.chipset, fsp.as_mut()),

                gsp <- Gsp::new(pdev, bar),

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

            _: {
                irq::gsp::quiesce(tree_ref, &gsp_resources.gsp_falcon);
            },

            // SAFETY: the GSP falcon and the command queue are fields of `gsp_resources`, which
            // is initialized above and pinned, so both references outlive the registration. The
            // registration is a field of `Gpu` and is never leaked, so its `Drop` runs, and field
            // drop order runs it before either is freed.
            _gsp_subtree <- unsafe {
                GspSubtree::new(
                    pdev,
                    tree_ref,
                    &*core::ptr::from_ref(&gsp_resources.gsp_falcon),
                    &*core::ptr::from_ref(&gsp_resources.gsp.cmdq),
                )
            },

            // No interrupt announces the messages that the GSP posted during boot, before the
            // SWGEN0 latch was cleared.
            _: {
                gsp_resources.gsp.cmdq.drain()?;
            },

            gsp_static_info: {
                // Obtain and display basic GPU information.
                let info = gsp_resources.gsp.get_static_info()?;
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
                        info.vram_size() / u64::SZ_1M
                    );
                }

                info
            },

            // Create GPU memory manager owning memory management resources.
            mm: {
                let usable_vram = gsp_static_info.usable_fb_regions.first().ok_or(ENODEV)?;
                let buddy_params = GpuBuddyParams {
                    base_offset: usable_vram.start,
                    size: usable_vram.end - usable_vram.start,
                    chunk_size: Alignment::new::<SZ_4K>(),
                };

                GpuMm::new(
                    bar,
                    gsp_resources.spec.chipset,
                    buddy_params,
                    VramAddress::from_raw(gsp_static_info.total_fb_end),
                )?
            },

            // Create BAR1 user interface for CPU access to GPU virtual memory.
            bar_user <- {
                let pdb_addr = VramAddress::from_raw(gsp_static_info.bar1_pde_base);
                let bar1_idx = crate::driver::bar1_resource_index(pdev)?;
                let bar1_size = pdev.resource_len(bar1_idx)?;

                BarUser::new(
                    pdb_addr,
                    gsp_resources.spec.chipset,
                    bar1_size,
                    bar1,
                )
            },
        })
    }

    /// Waits for GFW, the GPU's boot firmware, to report completion.
    ///
    /// The driver must not program the GPU before then.
    ///
    /// # Errors
    ///
    /// `ETIMEDOUT` if GFW does not report completion in time.
    pub(crate) fn wait_gfw_boot_completion(
        dev: &device::Device<device::Bound>,
        bar: Bar0<'_>,
        chipset: Chipset,
    ) -> Result {
        hal::gpu_hal(chipset)
            .wait_gfw_boot_completion(bar)
            .inspect_err(|_| dev_err!(dev, "GFW boot did not complete\n"))
    }

    /// Runs self-tests on the constructed [`Gpu`], logging failures without failing probe.
    #[cfg(CONFIG_NOVA_CORE_SELFTESTS)]
    pub(crate) fn run_selftests(self: Pin<&mut Self>, pdev: &pci::Device<device::Bound>) {
        let this = self.project();
        let dev = pdev.as_ref();
        let regions = &this.gsp_static_info.usable_fb_regions;

        if let Err(err) = crate::mm::selftest::run(
            dev,
            this.mm,
            regions,
            &this.bar_user,
            this.gsp_static_info.bar1_pde_base,
            this.spec.chipset,
        ) {
            dev_err!(dev, "self-tests failed: {:?}\n", err);
        }
    }
}

/// Reads the boot0 register and returns its raw value.
pub(crate) fn boot_0_raw(bar: Bar0<'_>) -> u32 {
    bar.read(regs::NV_PMC_BOOT_0).into_raw()
}
