// SPDX-License-Identifier: GPL-2.0

//! Nova Core GPU Driver

mod driver;
mod firmware;
mod gpu;
mod regs;
mod util;

kernel::module_pci_driver! {
    type: driver::NovaCore,
    name: "NovaCore",
    author: "Danilo Krummrich",
    description: "Nova Core GPU driver",
    license: "GPL v2",
    firmware: [],
}

kernel::module_firmware!(firmware::ModInfoBuilder);
