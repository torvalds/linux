// SPDX-License-Identifier: GPL-2.0

mod boot;

use kernel::prelude::*;

mod fw;

pub(crate) const GSP_PAGE_SHIFT: usize = 12;
pub(crate) const GSP_PAGE_SIZE: usize = 1 << GSP_PAGE_SHIFT;

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
