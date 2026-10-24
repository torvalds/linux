// SPDX-License-Identifier: GPL-2.0

use kernel::{
    auxiliary,
    device::{
        Bound,
        Core, //
    },
    io::resource,
    pci,
    pci::{
        Class,
        ClassMask,
        Vendor, //
    },
    prelude::*,
    sizes::SZ_16M,
    sync::atomic::{
        Atomic,
        Relaxed, //
    },
    types::ForLt,
};

use crate::{
    api::NovaCoreApi,
    gpu::Gpu, //
};

/// Counter for generating unique auxiliary device IDs.
static AUXILIARY_ID_COUNTER: Atomic<u32> = Atomic::new(0);

#[pin_data]
pub(crate) struct NovaCore<'bound> {
    #[allow(clippy::type_complexity)]
    _reg: auxiliary::Registration<'bound, ForLt!(NovaCoreApi<'_>)>,
    #[pin]
    pub(crate) gpu: Gpu<'bound>,
    bar: pci::Bar<'bound, BAR0_SIZE>,
    bar1: pci::Bar<'bound>,
}

pub(crate) struct NovaCoreDriver;

const BAR0_SIZE: usize = SZ_16M;

pub(crate) type Bar0<'a> = &'a pci::Bar<'a, BAR0_SIZE>;
pub(crate) type NovaRegisters = kernel::io::Region<BAR0_SIZE>;
pub(crate) type Bar1<'a> = &'a pci::Bar<'a>;

/// Returns the Linux PCI resource index that holds BAR1 for an NVIDIA GPU.
///
/// On Maxwell through Ada, BAR0 is a 32-bit memory BAR occupying a single
/// Linux PCI resource slot, so BAR1 lives at index 1. Starting with Blackwell
/// (and on some Ampere GA100 / Hopper SKUs) BAR0 is a 64-bit memory BAR that
/// consumes two consecutive resource slots: index 0 holds the low 32 bits and
/// index 1 holds the high 32 bits (with no `flags` / or size of its own),
/// shifting BAR1 to index 2.
pub(crate) fn bar1_resource_index(pdev: &pci::Device<Bound>) -> Result<u32> {
    // Probe the `IORESOURCE_MEM_64` flag of BAR0 as a robust way of exposing
    // if BAR0 and hence BAR1 is 64-bit.
    let flags0 = pdev.resource_flags(0)?;
    if flags0.contains(resource::Flags::IORESOURCE_MEM_64) {
        Ok(2)
    } else {
        Ok(1)
    }
}

kernel::pci_device_table!(
    PCI_TABLE,
    <NovaCoreDriver as pci::Driver>::IdInfo,
    [
        // Modern NVIDIA GPUs will show up as either VGA or 3D controllers.
        (
            pci::DeviceId::from_class_and_vendor(
                Class::DISPLAY_VGA,
                ClassMask::ClassSubclass,
                Vendor::NVIDIA
            ),
            ()
        ),
        (
            pci::DeviceId::from_class_and_vendor(
                Class::DISPLAY_3D,
                ClassMask::ClassSubclass,
                Vendor::NVIDIA
            ),
            ()
        ),
    ]
);

impl pci::Driver for NovaCoreDriver {
    type IdInfo = ();
    type Data<'bound> = NovaCore<'bound>;
    const ID_TABLE: pci::IdTable<Self::IdInfo> = &PCI_TABLE;

    fn probe<'bound>(
        pdev: &'bound pci::Device<Core<'_>>,
        _info: Option<&'bound Self::IdInfo>,
    ) -> impl PinInit<Self::Data<'bound>, Error> + 'bound {
        dev_dbg!(pdev, "Probe Nova Core GPU driver.\n");

        try_pin_init!(NovaCore {
            _: {
                pdev.enable_device_mem()?;
                pdev.set_master();
            },

            bar: pdev.iomap_region_sized::<BAR0_SIZE>(0, c"nova-core/bar0")?,

            bar1: {
                let bar1_idx = bar1_resource_index(pdev)?;
                pdev.iomap_region(bar1_idx, c"nova-core/bar1")?
            },

            // Run self-tests that do not depend on the `Gpu` instance.
            #[cfg(CONFIG_NOVA_CORE_SELFTESTS)]
            _: {
                let spec = crate::gpu::Spec::new(pdev.as_ref(), bar)?;

                // We must wait for GFW_BOOT completion before doing any significant setup on
                // the GPU.
                Gpu::wait_gfw_boot_completion(pdev.as_ref(), bar, spec.chipset)?;

                // The self-test disables and drains the whole tree, so it has to run before
                // `Gpu::new` boots the GSP.
                crate::irq::doorbell_test::run_selftest(pdev, bar, spec.chipset)?;
            },

            // TODO: Use self-referential pin-init syntax once available.
            gpu <- Gpu::new(
                pdev,
                // SAFETY: `bar` is initialized above, pinned, and outlives `gpu`.
                unsafe { &*core::ptr::from_ref(bar) },
                // SAFETY: `bar1` is initialized above, pinned, and outlives `gpu`.
                unsafe { &*core::ptr::from_ref(bar1) },
            ).pin_chain(|_gpu| {
                #[cfg(CONFIG_NOVA_CORE_SELFTESTS)]
                _gpu.run_selftests(pdev);
                Ok(())
            }),

            _reg: {
                // TODO: Use `&gpu` self-referential pin-init syntax once available.
                //
                // SAFETY: `gpu` is initialized before this expression is evaluated
                // (`try_pin_init!()` initializes fields in initializer order), lives at
                // a pinned stable address, and is dropped after `_reg` (struct field
                // drop order).
                let gpu = unsafe {
                    Pin::new_unchecked(&*core::ptr::from_ref(gpu.as_ref().get_ref()))
                };

                // SAFETY: `NovaCore` is dropped when the device is unbound;
                // i.e. `mem::forget()` is never called on it.
                unsafe {
                    auxiliary::Registration::new_with_lt(
                        pdev.as_ref(),
                        c"nova-drm",
                        // TODO[XARR]: Use XArray or perhaps IDA for proper ID
                        // allocation/recycling. For now, use a simple atomic counter that
                        // never recycles IDs.
                        AUXILIARY_ID_COUNTER.fetch_add(1, Relaxed),
                        crate::MODULE_NAME,
                        NovaCoreApi { gpu, pdev },
                    )?
                }
            },
        })
    }
}
