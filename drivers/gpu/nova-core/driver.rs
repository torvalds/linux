// SPDX-License-Identifier: GPL-2.0

use kernel::{
    auxiliary,
    device::Core,
    devres::Devres,
    dma::Device,
    dma::DmaMask,
    pci,
    pci::{
        Class,
        ClassMask,
        Vendor, //
    },
    prelude::*,
    sizes::SZ_16M,
    sync::Arc, //
};

use crate::gpu::Gpu;

#[pin_data]
pub(crate) struct NovaCore {
    #[pin]
    pub(crate) gpu: Gpu,
    #[pin]
    _reg: Devres<auxiliary::Registration>,
}

const BAR0_SIZE: usize = SZ_16M;

// For now we only support Ampere which can use up to 47-bit DMA addresses.
//
// TODO: Add an abstraction for this to support newer GPUs which may support
// larger DMA addresses. Limiting these GPUs to smaller address widths won't
// have any adverse affects, unless installed on systems which require larger
// DMA addresses. These systems should be quite rare.
const GPU_DMA_BITS: u32 = 47;

pub(crate) type Bar0 = pci::Bar<BAR0_SIZE>;

kernel::pci_device_table!(
    PCI_TABLE,
    MODULE_PCI_TABLE,
    <NovaCore as pci::Driver>::IdInfo,
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

impl pci::Driver for NovaCore {
    type IdInfo = ();
    const ID_TABLE: pci::IdTable<Self::IdInfo> = &PCI_TABLE;

    fn probe(pdev: &pci::Device<Core>, _info: &Self::IdInfo) -> impl PinInit<Self, Error> {
        pin_init::pin_init_scope(move || {
            dev_dbg!(pdev.as_ref(), "Probe Nova Core GPU driver.\n");

            pdev.enable_device_mem()?;
            pdev.set_master();

            // SAFETY: No concurrent DMA allocations or mappings can be made because
            // the device is still being probed and therefore isn't being used by
            // other threads of execution.
            unsafe { pdev.dma_set_mask_and_coherent(DmaMask::new::<GPU_DMA_BITS>())? };

            let bar = Arc::pin_init(
                pdev.iomap_region_sized::<BAR0_SIZE>(0, c"nova-core/bar0"),
                GFP_KERNEL,
            )?;

            Ok(try_pin_init!(Self {
                gpu <- Gpu::new(pdev, bar.clone(), bar.access(pdev.as_ref())?),
                _reg <- auxiliary::Registration::new(
                    pdev.as_ref(),
                    c"nova-drm",
                    0, // TODO[XARR]: Once it lands, use XArray; for now we don't use the ID.
                    crate::MODULE_NAME
                ),
            }))
        })
    }

    fn unbind(pdev: &pci::Device<Core>, this: Pin<&Self>) {
        this.gpu.unbind(pdev.as_ref());
    }
}
