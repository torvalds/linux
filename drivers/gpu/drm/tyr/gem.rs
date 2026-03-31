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
    type Args = ();

    fn new(_dev: &TyrDrmDevice, _size: usize, _args: ()) -> impl PinInit<Self, Error> {
        try_pin_init!(TyrObject {})
    }
}
