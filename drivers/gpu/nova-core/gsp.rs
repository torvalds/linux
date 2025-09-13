// SPDX-License-Identifier: GPL-2.0

mod boot;

use kernel::prelude::*;

/// GSP runtime data.
///
/// This is an empty pinned placeholder for now.
#[pin_data]
pub(crate) struct Gsp {}

impl Gsp {
    pub(crate) fn new() -> impl PinInit<Self> {
        pin_init!(Self {})
    }
}
