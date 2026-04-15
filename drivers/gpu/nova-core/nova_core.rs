// SPDX-License-Identifier: GPL-2.0

//! Nova Core GPU Driver

use kernel::{
    debugfs,
    driver::Registration,
    pci,
    prelude::*,
    InPlaceModule, //
};

#[macro_use]
mod bitfield;

mod driver;
mod falcon;
mod fb;
mod firmware;
mod gfw;
mod gpu;
mod gsp;
#[macro_use]
mod num;
mod regs;
mod sbuffer;
mod vbios;

pub(crate) const MODULE_NAME: &core::ffi::CStr = <LocalModule as kernel::ModuleMetadata>::NAME;

// TODO: Move this into per-module data once that exists.
static mut DEBUGFS_ROOT: Option<debugfs::Dir> = None;

/// Guard that clears `DEBUGFS_ROOT` when dropped.
struct DebugfsRootGuard;

impl Drop for DebugfsRootGuard {
    fn drop(&mut self) {
        // SAFETY: This guard is dropped after `_driver` (due to field order),
        // so the driver is unregistered and no probe() can be running.
        unsafe { DEBUGFS_ROOT = None };
    }
}

#[pin_data]
struct NovaCoreModule {
    // Fields are dropped in declaration order, so `_driver` is dropped first,
    // then `_debugfs_guard` clears `DEBUGFS_ROOT`.
    #[pin]
    _driver: Registration<pci::Adapter<driver::NovaCore>>,
    _debugfs_guard: DebugfsRootGuard,
}

impl InPlaceModule for NovaCoreModule {
    fn init(module: &'static kernel::ThisModule) -> impl PinInit<Self, Error> {
        let dir = debugfs::Dir::new(kernel::c_str!("nova_core"));

        // SAFETY: We are the only driver code running during init, so there
        // cannot be any concurrent access to `DEBUGFS_ROOT`.
        unsafe { DEBUGFS_ROOT = Some(dir) };

        try_pin_init!(Self {
            _driver <- Registration::new(MODULE_NAME, module),
            _debugfs_guard: DebugfsRootGuard,
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
