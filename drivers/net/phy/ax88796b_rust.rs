// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2023 FUJITA Tomonori <fujita.tomonori@gmail.com>

//! Rust Asix PHYs driver
//!
//! C version of this driver: [`drivers/net/phy/ax88796b.c`](./ax88796b.c)
use kernel::{
    c_str,
    net::phy::{self, reg::C22, DeviceId, Driver},
    prelude::*,
    uapi,
};

kernel::module_phy_driver! {
    drivers: [PhyAX88772A, PhyAX88772C, PhyAX88796B],
    device_table: [
        DeviceId::new_with_driver::<PhyAX88772A>(),
        DeviceId::new_with_driver::<PhyAX88772C>(),
        DeviceId::new_with_driver::<PhyAX88796B>()
    ],
    name: "rust_asix_phy",
    author: "FUJITA Tomonori <fujita.tomonori@gmail.com>",
    description: "Rust Asix PHYs driver",
    license: "GPL",
}

const BMCR_SPEED100: u16 = uapi::BMCR_SPEED100 as u16;
const BMCR_FULLDPLX: u16 = uapi::BMCR_FULLDPLX as u16;

// Performs a software PHY reset using the standard
// BMCR_RESET bit and poll for the reset bit to be cleared.
// Toggle BMCR_RESET bit off to accommodate broken AX8796B PHY implementation
// such as used on the Individual Computers' X-Surf 100 Zorro card.
fn asix_soft_reset(dev: &mut phy::Device) -> Result {
    dev.write(C22::BMCR, 0)?;
    dev.genphy_soft_reset()
}

struct PhyAX88772A;

#[vtable]
impl Driver for PhyAX88772A {
    const FLAGS: u32 = phy::flags::IS_INTERNAL;
    const NAME: &'static CStr = c_str!("Asix Electronics AX88772A");
    const PHY_DEVICE_ID: DeviceId = DeviceId::new_with_exact_mask(0x003b1861);

    // AX88772A is not working properly with some old switches (NETGEAR EN 108TP):
    // after autoneg is done and the link status is reported as active, the MII_LPA
    // register is 0. This issue is not reproducible on AX88772C.
    fn read_status(dev: &mut phy::Device) -> Result<u16> {
        dev.genphy_update_link()?;
        if !dev.is_link_up() {
            return Ok(0);
        }
        // If MII_LPA is 0, phy_resolve_aneg_linkmode() will fail to resolve
        // linkmode so use MII_BMCR as default values.
        let ret = dev.read(C22::BMCR)?;

        if ret & BMCR_SPEED100 != 0 {
            dev.set_speed(uapi::SPEED_100);
        } else {
            dev.set_speed(uapi::SPEED_10);
        }

        let duplex = if ret & BMCR_FULLDPLX != 0 {
            phy::DuplexMode::Full
        } else {
            phy::DuplexMode::Half
        };
        dev.set_duplex(duplex);

        dev.genphy_read_lpa()?;

        if dev.is_autoneg_enabled() && dev.is_autoneg_completed() {
            dev.resolve_aneg_linkmode();
        }

        Ok(0)
    }

    fn suspend(dev: &mut phy::Device) -> Result {
        dev.genphy_suspend()
    }

    fn resume(dev: &mut phy::Device) -> Result {
        dev.genphy_resume()
    }

    fn soft_reset(dev: &mut phy::Device) -> Result {
        asix_soft_reset(dev)
    }

    fn link_change_notify(dev: &mut phy::Device) {
        // Reset PHY, otherwise MII_LPA will provide outdated information.
        // This issue is reproducible only with some link partner PHYs.
        if dev.state() == phy::DeviceState::NoLink {
            let _ = dev.init_hw();
            let _ = dev.start_aneg();
        }
    }
}

struct PhyAX88772C;

#[vtable]
impl Driver for PhyAX88772C {
    const FLAGS: u32 = phy::flags::IS_INTERNAL;
    const NAME: &'static CStr = c_str!("Asix Electronics AX88772C");
    const PHY_DEVICE_ID: DeviceId = DeviceId::new_with_exact_mask(0x003b1881);

    fn suspend(dev: &mut phy::Device) -> Result {
        dev.genphy_suspend()
    }

    fn resume(dev: &mut phy::Device) -> Result {
        dev.genphy_resume()
    }

    fn soft_reset(dev: &mut phy::Device) -> Result {
        asix_soft_reset(dev)
    }
}

struct PhyAX88796B;

#[vtable]
impl Driver for PhyAX88796B {
    const NAME: &'static CStr = c_str!("Asix Electronics AX88796B");
    const PHY_DEVICE_ID: DeviceId = DeviceId::new_with_model_mask(0x003b1841);

    fn soft_reset(dev: &mut phy::Device) -> Result {
        asix_soft_reset(dev)
    }
}
