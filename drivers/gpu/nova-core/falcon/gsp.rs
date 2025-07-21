// SPDX-License-Identifier: GPL-2.0

use crate::{
    driver::Bar0,
    falcon::{Falcon, FalconEngine},
    regs,
};

/// Type specifying the `Gsp` falcon engine. Cannot be instantiated.
pub(crate) struct Gsp(());

impl FalconEngine for Gsp {
    const BASE: usize = 0x00110000;
}

impl Falcon<Gsp> {
    /// Clears the SWGEN0 bit in the Falcon's IRQ status clear register to
    /// allow GSP to signal CPU for processing new messages in message queue.
    pub(crate) fn clear_swgen0_intr(&self, bar: &Bar0) {
        regs::NV_PFALCON_FALCON_IRQSCLR::default()
            .set_swgen0(true)
            .write(bar, Gsp::BASE);
    }
}
