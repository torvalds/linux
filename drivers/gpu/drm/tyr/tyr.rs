// SPDX-License-Identifier: GPL-2.0 or MIT

//! Arm Mali Tyr DRM driver.
//!
//! The name "Tyr" is inspired by Norse mythology, reflecting Arm's tradition of
//! naming their GPUs after Nordic mythological figures and places.

use crate::driver::TyrDriver;

mod driver;
mod file;
mod gem;
mod gpu;
mod regs;

kernel::module_platform_driver! {
    type: TyrDriver,
    name: "tyr",
    authors: ["The Tyr driver authors"],
    description: "Arm Mali Tyr DRM driver",
    license: "Dual MIT/GPL",
}
