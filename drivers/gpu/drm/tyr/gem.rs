// SPDX-License-Identifier: GPL-2.0 or MIT

use crate::driver::TyrDevice;
use crate::driver::TyrDriver;
use kernel::drm::gem;
use kernel::prelude::*;

/// GEM Object inner driver data
#[pin_data]
pub(crate) struct TyrObject {}

impl gem::DriverObject for TyrObject {
    type Driver = TyrDriver;

    fn new(_dev: &TyrDevice, _size: usize) -> impl PinInit<Self, Error> {
        try_pin_init!(TyrObject {})
    }
}
