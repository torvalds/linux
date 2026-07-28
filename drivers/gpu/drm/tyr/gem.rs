// SPDX-License-Identifier: GPL-2.0 or MIT
//! GEM buffer object management for the Tyr driver.
//!
//! This module provides buffer object (BO) management functionality using
//! DRM's GEM subsystem with shmem backing.

use kernel::{
    drm::gem::{
        self,
        shmem, //
    },
    prelude::*,
    sync::aref::ARef, //
};

use crate::driver::{
    TyrDrmDevice,
    TyrDrmDriver, //
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
