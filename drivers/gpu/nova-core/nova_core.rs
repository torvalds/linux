// SPDX-License-Identifier: GPL-2.0

//! Nova Core GPU Driver

use kernel::{
    driver::Registration,
    pci,
    prelude::*,
    InPlaceModule, //
};

#[macro_use]
mod bitfield;

mod dma;
mod driver;
mod falcon;
mod fb;
mod firmware;
mod gfw;
mod gpu;
mod gsp;
mod num;
mod regs;
mod sbuffer;
mod vbios;

pub(crate) const MODULE_NAME: &core::ffi::CStr = <LocalModule as kernel::ModuleMetadata>::NAME;

#[pin_data]
struct NovaCoreModule {
    #[pin]
    _driver: Registration<pci::Adapter<driver::NovaCore>>,
}

impl InPlaceModule for NovaCoreModule {
    fn init(module: &'static kernel::ThisModule) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            _driver <- Registration::new(MODULE_NAME, module),
        })
    }
}

module! {
    type: NovaCoreModule,
    name: "NovaCore",
    authors: ["Danilo Krummrich"],
    description: "Nova Core GPU driver",
    license: "GPL v2",
    firmware: [],
}

kernel::module_firmware!(firmware::ModInfoBuilder);
