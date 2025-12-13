// SPDX-License-Identifier: GPL-2.0

use kernel::{
    drm,
    drm::{gem, gem::BaseObject},
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
        let aligned_size = size.next_multiple_of(1 << 12);

        if size == 0 || size > aligned_size {
            return Err(EINVAL);
        }

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
