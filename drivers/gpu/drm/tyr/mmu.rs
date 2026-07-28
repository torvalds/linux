// SPDX-License-Identifier: GPL-2.0 or MIT

//! Memory Management Unit (MMU) module.
//!
//! The GPU MMU provides a limited number of memory address spaces for use by command streams.
//! The MMU translates virtual addresses to physical addresses and manages memory configuration
//! and access permissions.
//!
//! This MMU module is essentially a locked wrapper around a [`SlotManager`] instance.
//! The [`SlotManager`] manages the assignment of virtual address spaces to hardware address-space
//! (AS) slots. MMU commands such as updates and flushes are carried out by the
//! [`AddressSpaceManager`] which actually writes to the MMU registers.

use core::ops::Range;

use kernel::{
    device::{
        Bound,
        Device, //
    },
    new_mutex,
    prelude::*,
    sync::{
        Arc,
        ArcBorrow,
        Mutex, //
    }, //
};

use crate::{
    driver::IoMem,
    gpu::GpuInfo,
    mmu::address_space::{
        AddressSpaceManager,
        VmAsData, //
    },
    regs::{
        gpu_control::AS_PRESENT,
        MAX_AS, //
    },
    slot::SlotManager, //
};

pub(crate) mod address_space;

pub(crate) type AsSlotManager<'drm> = SlotManager<AddressSpaceManager<'drm>, MAX_AS>;

/// Locked wrapper for carrying out virtual memory (VM) operations on the MMU.
#[pin_data]
pub(crate) struct Mmu<'drm> {
    /// Slot Manager instance used to allocate hardware slots and write to MMU registers.
    #[pin]
    pub(crate) as_manager: Mutex<AsSlotManager<'drm>>,
}

impl<'drm> Mmu<'drm> {
    /// Create an MMU component for this device.
    pub(crate) fn new(
        dev: &'drm Device<Bound>,
        iomem: ArcBorrow<'_, IoMem<'drm>>,
        gpu_info: &GpuInfo,
    ) -> Result<Arc<Mmu<'drm>>> {
        let present = AS_PRESENT::from_raw(gpu_info.as_present).present().get();
        let slot_count = present.count_ones().try_into()?;

        let address_space_manager = AddressSpaceManager::new(dev, iomem.into(), present)?;
        let as_slot_manager =
            SlotManager::new(address_space_manager, slot_count).inspect_err(|e| {
                dev_err!(
                    dev,
                    "Failed to initialize MMU slot manager with {} slots: {:?}",
                    slot_count,
                    e
                );
            })?;
        let mmu_init = try_pin_init!(Self{
            as_manager <- new_mutex!(as_slot_manager),
        });
        Arc::pin_init(mmu_init, GFP_KERNEL)
    }

    /// Assign a VM to an AS slot, provide a translation table,
    /// and update the MMU to make the VM resident.
    pub(crate) fn activate_vm(&self, vm_as_data: ArcBorrow<'_, VmAsData<'drm>>) -> Result {
        self.as_manager.lock().activate_vm(vm_as_data)
    }

    /// Evict a VM from its AS slot and flush the MMU.
    pub(crate) fn deactivate_vm(&self, vm_as_data: &VmAsData<'drm>) -> Result {
        self.as_manager.lock().deactivate_vm(vm_as_data)
    }

    /// Flush MMU translation caches after a VM update.
    pub(crate) fn flush_vm(&self, vm_as_data: &VmAsData<'drm>) -> Result {
        self.as_manager.lock().flush_vm(vm_as_data)
    }

    /// Flags the start of a VM update.
    ///
    /// If the VM is resident, any GPU access on the memory range being
    /// updated will be blocked until `Mmu::end_vm_update()` is called.
    /// This guarantees the atomicity of a VM update.
    /// If the VM is not resident, this is a NOP.
    pub(crate) fn start_vm_update(
        &self,
        vm_as_data: &VmAsData<'drm>,
        region: &Range<u64>,
    ) -> Result {
        self.as_manager.lock().start_vm_update(vm_as_data, region)
    }

    /// Flags the end of a VM update.
    ///
    /// If the VM is resident, this will let GPU accesses on the updated
    /// range go through, in case any of them were blocked.
    /// If the VM is not resident, this is a NOP.
    pub(crate) fn end_vm_update(&self, vm_as_data: &VmAsData<'drm>) -> Result {
        self.as_manager.lock().end_vm_update(vm_as_data)
    }
}
