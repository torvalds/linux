// SPDX-License-Identifier: GPL-2.0

use kernel::{
    device,
    pci,
    prelude::*, //
};

use crate::gsp::{
    cmdq::CommandToGsp,
    fw::{
        commands::GspSetSystemInfo,
        MsgFunction, //
    },
};

/// The `GspSetSystemInfo` command.
pub(crate) struct SetSystemInfo<'a> {
    pdev: &'a pci::Device<device::Bound>,
}

impl<'a> SetSystemInfo<'a> {
    /// Creates a new `GspSetSystemInfo` command using the parameters of `pdev`.
    pub(crate) fn new(pdev: &'a pci::Device<device::Bound>) -> Self {
        Self { pdev }
    }
}

impl<'a> CommandToGsp for SetSystemInfo<'a> {
    const FUNCTION: MsgFunction = MsgFunction::GspSetSystemInfo;
    type Command = GspSetSystemInfo;
    type InitError = Error;

    fn init(&self) -> impl Init<Self::Command, Self::InitError> {
        GspSetSystemInfo::init(self.pdev)
    }
}
