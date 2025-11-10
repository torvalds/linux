// SPDX-License-Identifier: GPL-2.0

use core::convert::Infallible;

use kernel::{
    device,
    pci,
    prelude::*,
    transmute::AsBytes, //
};

use crate::{
    gsp::{
        cmdq::CommandToGsp,
        fw::{
            commands::*,
            MsgFunction, //
        },
    },
    sbuffer::SBufferIter,
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

struct RegistryEntry {
    key: &'static str,
    value: u32,
}

/// The `SetRegistry` command.
pub(crate) struct SetRegistry {
    entries: [RegistryEntry; Self::NUM_ENTRIES],
}

impl SetRegistry {
    // For now we hard-code the registry entries. Future work will allow others to
    // be added as module parameters.
    const NUM_ENTRIES: usize = 3;

    /// Creates a new `SetRegistry` command, using a set of hardcoded entries.
    pub(crate) fn new() -> Self {
        Self {
            entries: [
                // RMSecBusResetEnable - enables PCI secondary bus reset
                RegistryEntry {
                    key: "RMSecBusResetEnable",
                    value: 1,
                },
                // RMForcePcieConfigSave - forces GSP-RM to preserve PCI configuration registers on
                // any PCI reset.
                RegistryEntry {
                    key: "RMForcePcieConfigSave",
                    value: 1,
                },
                // RMDevidCheckIgnore - allows GSP-RM to boot even if the PCI dev ID is not found
                // in the internal product name database.
                RegistryEntry {
                    key: "RMDevidCheckIgnore",
                    value: 1,
                },
            ],
        }
    }
}

impl CommandToGsp for SetRegistry {
    const FUNCTION: MsgFunction = MsgFunction::SetRegistry;
    type Command = PackedRegistryTable;
    type InitError = Infallible;

    fn init(&self) -> impl Init<Self::Command, Self::InitError> {
        PackedRegistryTable::init(Self::NUM_ENTRIES as u32, self.variable_payload_len() as u32)
    }

    fn variable_payload_len(&self) -> usize {
        let mut key_size = 0;
        for i in 0..Self::NUM_ENTRIES {
            key_size += self.entries[i].key.len() + 1; // +1 for NULL terminator
        }
        Self::NUM_ENTRIES * size_of::<PackedRegistryEntry>() + key_size
    }

    fn init_variable_payload(
        &self,
        dst: &mut SBufferIter<core::array::IntoIter<&mut [u8], 2>>,
    ) -> Result {
        let string_data_start_offset =
            size_of::<PackedRegistryTable>() + Self::NUM_ENTRIES * size_of::<PackedRegistryEntry>();

        // Array for string data.
        let mut string_data = KVec::new();

        for entry in self.entries.iter().take(Self::NUM_ENTRIES) {
            dst.write_all(
                PackedRegistryEntry::new(
                    (string_data_start_offset + string_data.len()) as u32,
                    entry.value,
                )
                .as_bytes(),
            )?;

            let key_bytes = entry.key.as_bytes();
            string_data.extend_from_slice(key_bytes, GFP_KERNEL)?;
            string_data.push(0, GFP_KERNEL)?;
        }

        dst.write_all(string_data.as_slice())
    }
}
