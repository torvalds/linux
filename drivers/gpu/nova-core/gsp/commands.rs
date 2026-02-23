// SPDX-License-Identifier: GPL-2.0

use core::{
    array,
    convert::Infallible,
    ffi::FromBytesUntilNulError,
    str::Utf8Error, //
};

use kernel::{
    device,
    pci,
    prelude::*,
    time::Delta,
    transmute::{
        AsBytes,
        FromBytes, //
    }, //
};

use crate::{
    driver::Bar0,
    gsp::{
        cmdq::{
            Cmdq,
            CommandToGsp,
            MessageFromGsp, //
        },
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

/// Message type for GSP initialization done notification.
struct GspInitDone;

// SAFETY: `GspInitDone` is a zero-sized type with no bytes, therefore it
// trivially has no uninitialized bytes.
unsafe impl FromBytes for GspInitDone {}

impl MessageFromGsp for GspInitDone {
    const FUNCTION: MsgFunction = MsgFunction::GspInitDone;
    type InitError = Infallible;
    type Message = ();

    fn read(
        _msg: &Self::Message,
        _sbuffer: &mut SBufferIter<array::IntoIter<&[u8], 2>>,
    ) -> Result<Self, Self::InitError> {
        Ok(GspInitDone)
    }
}

/// Waits for GSP initialization to complete.
pub(crate) fn wait_gsp_init_done(cmdq: &mut Cmdq) -> Result {
    loop {
        match cmdq.receive_msg::<GspInitDone>(Delta::from_secs(10)) {
            Ok(_) => break Ok(()),
            Err(ERANGE) => continue,
            Err(e) => break Err(e),
        }
    }
}

/// The `GetGspStaticInfo` command.
struct GetGspStaticInfo;

impl CommandToGsp for GetGspStaticInfo {
    const FUNCTION: MsgFunction = MsgFunction::GetGspStaticInfo;
    type Command = GspStaticConfigInfo;
    type InitError = Infallible;

    fn init(&self) -> impl Init<Self::Command, Self::InitError> {
        GspStaticConfigInfo::init_zeroed()
    }
}

/// The reply from the GSP to the [`GetGspInfo`] command.
pub(crate) struct GetGspStaticInfoReply {
    gpu_name: [u8; 64],
}

impl MessageFromGsp for GetGspStaticInfoReply {
    const FUNCTION: MsgFunction = MsgFunction::GetGspStaticInfo;
    type Message = GspStaticConfigInfo;
    type InitError = Infallible;

    fn read(
        msg: &Self::Message,
        _sbuffer: &mut SBufferIter<array::IntoIter<&[u8], 2>>,
    ) -> Result<Self, Self::InitError> {
        Ok(GetGspStaticInfoReply {
            gpu_name: msg.gpu_name_str(),
        })
    }
}

/// Error type for [`GetGspStaticInfoReply::gpu_name`].
#[derive(Debug)]
pub(crate) enum GpuNameError {
    /// The GPU name string does not contain a null terminator.
    NoNullTerminator(FromBytesUntilNulError),

    /// The GPU name string contains invalid UTF-8.
    #[expect(dead_code)]
    InvalidUtf8(Utf8Error),
}

impl GetGspStaticInfoReply {
    /// Returns the name of the GPU as a string.
    ///
    /// Returns an error if the string given by the GSP does not contain a null terminator or
    /// contains invalid UTF-8.
    pub(crate) fn gpu_name(&self) -> core::result::Result<&str, GpuNameError> {
        CStr::from_bytes_until_nul(&self.gpu_name)
            .map_err(GpuNameError::NoNullTerminator)?
            .to_str()
            .map_err(GpuNameError::InvalidUtf8)
    }
}

/// Send the [`GetGspInfo`] command and awaits for its reply.
pub(crate) fn get_gsp_info(cmdq: &mut Cmdq, bar: &Bar0) -> Result<GetGspStaticInfoReply> {
    cmdq.send_command(bar, GetGspStaticInfo)?;

    loop {
        match cmdq.receive_msg::<GetGspStaticInfoReply>(Delta::from_secs(5)) {
            Ok(info) => return Ok(info),
            Err(ERANGE) => continue,
            Err(e) => return Err(e),
        }
    }
}
