// SPDX-License-Identifier: GPL-2.0 or MIT
//! GEM buffer object management for the Tyr driver.
//!
//! This module provides buffer object (BO) management functionality using
//! DRM's GEM subsystem with shmem backing.

use core::ops::Range;

use kernel::{
    drm::gem::{
        self,
        shmem, //
    },
    prelude::*,
    sync::{
        aref::ARef,
        Arc, //
    }, //
};

use crate::{
    driver::{
        TyrDrmDevice,
        TyrDrmDriver, //
    },
    vm::{
        Vm,
        VmMapFlags, //
    },
};

/// Tyr's DriverObject type for GEM objects.
#[pin_data]
pub(crate) struct BoData {
    flags: u32,
}

/// Provides a way to pass arguments when creating BoData
/// as required by the gem::DriverObject trait.
pub(crate) struct BoCreateArgs {
    flags: u32,
}

impl gem::DriverObject for BoData {
    type Driver = TyrDrmDriver;
    type Args = BoCreateArgs;

    fn new(_dev: &TyrDrmDevice, _size: usize, args: BoCreateArgs) -> impl PinInit<Self, Error> {
        try_pin_init!(Self { flags: args.flags })
    }
}

/// Type alias for Tyr GEM buffer objects.
pub(crate) type Bo = gem::shmem::Object<BoData>;

/// Creates a dummy GEM object to serve as the root of a GPUVM.
pub(crate) fn new_dummy_object(ddev: &TyrDrmDevice) -> Result<ARef<Bo>> {
    let bo = Bo::new(
        ddev,
        4096,
        shmem::ObjectConfig {
            map_wc: true,
            parent_resv_obj: None,
        },
        BoCreateArgs { flags: 0 },
    )?;

    Ok(bo)
}

/// Specifies how to choose a GPU virtual address for a [`KernelBo`].
/// An automatic VA allocation strategy will be added in the future.
pub(crate) enum KernelBoVaAlloc {
    /// Explicit VA address specified by the caller.
    Explicit(u64),
}

/// A kernel-owned buffer object with automatic GPU virtual address mapping.
///
/// This structure represents a buffer object that is created and managed entirely
/// by the kernel driver, as opposed to userspace-created GEM objects. It combines
/// a GEM object with automatic GPU virtual address (VA) space mapping and cleanup.
///
/// When dropped, the buffer is automatically unmapped from the GPU VA space.
pub(crate) struct KernelBo<'drm> {
    /// The underlying GEM buffer object.
    bo: ARef<Bo>,
    /// The GPU VM this buffer is mapped into.
    vm: Arc<Vm<'drm>>,
    /// The GPU VA range occupied by this buffer.
    va_range: Range<u64>,
}

impl<'drm> KernelBo<'drm> {
    /// Creates a new kernel-owned buffer object and maps it into GPU VA space.
    ///
    /// This function allocates a new shmem-backed GEM object and immediately maps
    /// it into the specified GPU virtual memory space. The mapping is automatically
    /// cleaned up when the [`KernelBo`] is dropped.
    pub(crate) fn new(
        ddev: &TyrDrmDevice,
        vm: Arc<Vm<'drm>>,
        size: u64,
        va_alloc: KernelBoVaAlloc,
        flags: VmMapFlags,
    ) -> Result<Self> {
        if size == 0 {
            dev_err!(vm.dev(), "Cannot create KernelBo with size 0");
            return Err(EINVAL);
        }

        let KernelBoVaAlloc::Explicit(va) = va_alloc;

        let bo_size = usize::try_from(size).map_err(|_| EOVERFLOW)?;
        let va_end = va.checked_add(size).ok_or(EINVAL)?;

        let bo = Bo::new(
            ddev,
            bo_size,
            shmem::ObjectConfig {
                map_wc: true,
                parent_resv_obj: None,
            },
            BoCreateArgs { flags: 0 },
        )?;

        vm.map_bo_range(&bo, 0, size, va, flags)?;

        Ok(KernelBo {
            bo,
            vm,
            va_range: va..va_end,
        })
    }

    pub(crate) fn bo(&self) -> &Bo {
        &self.bo
    }
}

impl Drop for KernelBo<'_> {
    fn drop(&mut self) {
        let va = self.va_range.start;
        let size = self.va_range.end - self.va_range.start;

        if let Err(e) = self.vm.unmap_range(va, size) {
            // If unmap_range fails, it is still safe to drop the
            // KernelBo and its ARef to the GEM buffer object because
            // GPUVM also holds a reference to the GEM buffer object.
            // The physical pages won't be freed or reallocated.
            dev_err!(
                self.vm.dev(),
                "Failed to unmap KernelBo range {:#x}..{:#x}: {:?}",
                self.va_range.start,
                self.va_range.end,
                e
            );
        }
    }
}
