// SPDX-License-Identifier: GPL-2.0

use kernel::{
    drm,
    drm::{gem, gem::BaseObject},
    page,
    prelude::*,
    sync::aref::ARef,
};

use crate::{
    driver::{NovaDevice, NovaDriver},
    file::File,
};

/// GEM Object inner driver data
#[pin_data]
pub(crate) struct NovaObject {}

impl gem::DriverObject for NovaObject {
    type Driver = NovaDriver;

    fn new(_dev: &NovaDevice, _size: usize) -> impl PinInit<Self, Error> {
        try_pin_init!(NovaObject {})
    }
}

impl NovaObject {
    /// Create a new DRM GEM object.
    pub(crate) fn new(dev: &NovaDevice, size: usize) -> Result<ARef<gem::Object<Self>>> {
        if size == 0 {
            return Err(EINVAL);
        }
        let aligned_size = page::page_align(size).ok_or(EINVAL)?;

        gem::Object::new(dev, aligned_size)
    }

    /// Look up a GEM object handle for a `File` and return an `ObjectRef` for it.
    #[inline]
    pub(crate) fn lookup_handle(
        file: &drm::File<File>,
        handle: u32,
    ) -> Result<ARef<gem::Object<Self>>> {
        gem::Object::lookup_handle(file, handle)
    }
}
