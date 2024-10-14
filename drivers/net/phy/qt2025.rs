// SPDX-License-Identifier: GPL-2.0
// Copyright (C) Tehuti Networks Ltd.
// Copyright (C) 2024 FUJITA Tomonori <fujita.tomonori@gmail.com>

//! Applied Micro Circuits Corporation QT2025 PHY driver
//!
//! This driver is based on the vendor driver `QT2025_phy.c`. This source
//! and firmware can be downloaded on the EN-9320SFP+ support site.
//!
//! The QT2025 PHY integrates an Intel 8051 micro-controller.

use kernel::c_str;
use kernel::error::code;
use kernel::firmware::Firmware;
use kernel::net::phy::{
    self,
    reg::{Mmd, C45},
    Driver,
};
use kernel::prelude::*;
use kernel::sizes::{SZ_16K, SZ_8K};

kernel::module_phy_driver! {
    drivers: [PhyQT2025],
    device_table: [
        phy::DeviceId::new_with_driver::<PhyQT2025>(),
    ],
    name: "qt2025_phy",
    author: "FUJITA Tomonori <fujita.tomonori@gmail.com>",
    description: "AMCC QT2025 PHY driver",
    license: "GPL",
    firmware: ["qt2025-2.0.3.3.fw"],
}

struct PhyQT2025;

#[vtable]
impl Driver for PhyQT2025 {
    const NAME: &'static CStr = c_str!("QT2025 10Gpbs SFP+");
    const PHY_DEVICE_ID: phy::DeviceId = phy::DeviceId::new_with_exact_mask(0x0043a400);

    fn probe(dev: &mut phy::Device) -> Result<()> {
        // Check the hardware revision code.
        // Only 0x3b works with this driver and firmware.
        let hw_rev = dev.read(C45::new(Mmd::PMAPMD, 0xd001))?;
        if (hw_rev >> 8) != 0xb3 {
            return Err(code::ENODEV);
        }

        // `MICRO_RESETN`: hold the micro-controller in reset while configuring.
        dev.write(C45::new(Mmd::PMAPMD, 0xc300), 0x0000)?;
        // `SREFCLK_FREQ`: configure clock frequency of the micro-controller.
        dev.write(C45::new(Mmd::PMAPMD, 0xc302), 0x0004)?;
        // Non loopback mode.
        dev.write(C45::new(Mmd::PMAPMD, 0xc319), 0x0038)?;
        // `CUS_LAN_WAN_CONFIG`: select between LAN and WAN (WIS) mode.
        dev.write(C45::new(Mmd::PMAPMD, 0xc31a), 0x0098)?;
        // The following writes use standardized registers (3.38 through
        // 3.41 5/10/25GBASE-R PCS test pattern seed B) for something else.
        // We don't know what.
        dev.write(C45::new(Mmd::PCS, 0x0026), 0x0e00)?;
        dev.write(C45::new(Mmd::PCS, 0x0027), 0x0893)?;
        dev.write(C45::new(Mmd::PCS, 0x0028), 0xa528)?;
        dev.write(C45::new(Mmd::PCS, 0x0029), 0x0003)?;
        // Configure transmit and recovered clock.
        dev.write(C45::new(Mmd::PMAPMD, 0xa30a), 0x06e1)?;
        // `MICRO_RESETN`: release the micro-controller from the reset state.
        dev.write(C45::new(Mmd::PMAPMD, 0xc300), 0x0002)?;
        // The micro-controller will start running from the boot ROM.
        dev.write(C45::new(Mmd::PCS, 0xe854), 0x00c0)?;

        let fw = Firmware::request(c_str!("qt2025-2.0.3.3.fw"), dev.as_ref())?;
        if fw.data().len() > SZ_16K + SZ_8K {
            return Err(code::EFBIG);
        }

        // The 24kB of program memory space is accessible by MDIO.
        // The first 16kB of memory is located in the address range 3.8000h - 3.BFFFh.
        // The next 8kB of memory is located at 4.8000h - 4.9FFFh.
        let mut dst_offset = 0;
        let mut dst_mmd = Mmd::PCS;
        for (src_idx, val) in fw.data().iter().enumerate() {
            if src_idx == SZ_16K {
                // Start writing to the next register with no offset
                dst_offset = 0;
                dst_mmd = Mmd::PHYXS;
            }

            dev.write(C45::new(dst_mmd, 0x8000 + dst_offset), (*val).into())?;

            dst_offset += 1;
        }
        // The micro-controller will start running from SRAM.
        dev.write(C45::new(Mmd::PCS, 0xe854), 0x0040)?;

        // TODO: sleep here until the hw becomes ready.
        Ok(())
    }

    fn read_status(dev: &mut phy::Device) -> Result<u16> {
        dev.genphy_read_status::<C45>()
    }
}
