// SPDX-License-Identifier: GPL-2.0

//! Nova DRM Driver

mod driver;
mod file;
mod gem;

use crate::driver::NovaDriver;

kernel::module_auxiliary_driver! {
    type: NovaDriver,
    name: "Nova",
    authors: ["Danilo Krummrich"],
    description: "Nova GPU driver",
    license: "GPL v2",
}
