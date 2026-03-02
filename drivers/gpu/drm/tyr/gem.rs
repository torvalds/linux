// SPDX-License-Identifier: GPL-2.0 or MIT

use kernel::{
    drm::gem,
    prelude::*, //
};

use crate::driver::{
    TyrDrmDevice,
    TyrDrmDriver, //
};

/// GEM Object inner driver data
#[pin_data]
pub(crate) struct TyrObject {}

impl gem::DriverObject for TyrObject {
    type Driver = TyrDrmDriver;

    fn new(_dev: &TyrDrmDevice, _size: usize) -> impl PinInit<Self, Error> {
        try_pin_init!(TyrObject {})
    }
}
