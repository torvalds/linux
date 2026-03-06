// SPDX-License-Identifier: GPL-2.0

//! Support for splitting large GSP commands across continuation records.

use core::convert::Infallible;

use kernel::prelude::*;

use super::CommandToGsp;

use crate::{
    gsp::fw::{
        GspMsgElement,
        MsgFunction,
        GSP_MSG_QUEUE_ELEMENT_SIZE_MAX, //
    },
    sbuffer::SBufferIter,
};

/// Maximum command size that fits in a single queue element.
const MAX_CMD_SIZE: usize = GSP_MSG_QUEUE_ELEMENT_SIZE_MAX - size_of::<GspMsgElement>();

/// Acts as an iterator over the continuation records for a split command.
pub(super) struct ContinuationRecords {
    payload: KVVec<u8>,
    offset: usize,
}

impl ContinuationRecords {
    /// Creates a new iterator over continuation records for the given payload.
    fn new(payload: KVVec<u8>) -> Self {
        Self { payload, offset: 0 }
    }

    /// Returns the next continuation record, or [`None`] if there are no more.
    pub(super) fn next(&mut self) -> Option<ContinuationRecord<'_>> {
        let remaining = self.payload.len() - self.offset;

        if remaining > 0 {
            let chunk_size = remaining.min(MAX_CMD_SIZE);
            let record =
                ContinuationRecord::new(&self.payload[self.offset..(self.offset + chunk_size)]);
            self.offset += chunk_size;
            Some(record)
        } else {
            None
        }
    }
}

/// The [`ContinuationRecord`] command.
pub(super) struct ContinuationRecord<'a> {
    data: &'a [u8],
}

impl<'a> ContinuationRecord<'a> {
    /// Creates a new [`ContinuationRecord`] command with the given data.
    fn new(data: &'a [u8]) -> Self {
        Self { data }
    }
}

impl<'a> CommandToGsp for ContinuationRecord<'a> {
    const FUNCTION: MsgFunction = MsgFunction::ContinuationRecord;
    type Command = ();
    type InitError = Infallible;

    fn init(&self) -> impl Init<Self::Command, Self::InitError> {
        <()>::init_zeroed()
    }

    fn variable_payload_len(&self) -> usize {
        self.data.len()
    }

    fn init_variable_payload(
        &self,
        dst: &mut SBufferIter<core::array::IntoIter<&mut [u8], 2>>,
    ) -> Result {
        dst.write_all(self.data)
    }
}

/// Whether a command needs to be split across continuation records or not.
pub(super) enum SplitState<C: CommandToGsp> {
    /// A command that fits in a single queue element.
    Single(C),
    /// A command split across continuation records.
    Split(SplitCommand<C>, ContinuationRecords),
}

impl<C: CommandToGsp> SplitState<C> {
    /// Maximum variable payload size that fits in the first command alongside the command header.
    const MAX_FIRST_PAYLOAD: usize = MAX_CMD_SIZE - size_of::<C::Command>();

    /// Creates a new [`SplitState`] for the given command.
    ///
    /// If the command is too large, it will be split into a main command and some number of
    /// continuation records.
    pub(super) fn new(command: C) -> Result<Self> {
        let payload_len = command.variable_payload_len();

        if command.size() > MAX_CMD_SIZE {
            let mut command_payload =
                KVVec::<u8>::from_elem(0u8, payload_len.min(Self::MAX_FIRST_PAYLOAD), GFP_KERNEL)?;
            let mut continuation_payload =
                KVVec::<u8>::from_elem(0u8, payload_len - command_payload.len(), GFP_KERNEL)?;
            let mut sbuffer = SBufferIter::new_writer([
                command_payload.as_mut_slice(),
                continuation_payload.as_mut_slice(),
            ]);

            command.init_variable_payload(&mut sbuffer)?;
            if !sbuffer.is_empty() {
                return Err(EIO);
            }
            drop(sbuffer);

            Ok(Self::Split(
                SplitCommand::new(command, command_payload),
                ContinuationRecords::new(continuation_payload),
            ))
        } else {
            Ok(Self::Single(command))
        }
    }
}

/// A command that has been truncated to maximum accepted length of the command queue.
///
/// The remainder of its payload is expected to be sent using [`ContinuationRecords`].
pub(super) struct SplitCommand<C: CommandToGsp> {
    command: C,
    payload: KVVec<u8>,
}

impl<C: CommandToGsp> SplitCommand<C> {
    /// Creates a new [`SplitCommand`] wrapping `command` with the given truncated payload.
    fn new(command: C, payload: KVVec<u8>) -> Self {
        Self { command, payload }
    }
}

impl<C: CommandToGsp> CommandToGsp for SplitCommand<C> {
    const FUNCTION: MsgFunction = C::FUNCTION;
    type Command = C::Command;
    type InitError = C::InitError;

    fn init(&self) -> impl Init<Self::Command, Self::InitError> {
        self.command.init()
    }

    fn variable_payload_len(&self) -> usize {
        self.payload.len()
    }

    fn init_variable_payload(
        &self,
        dst: &mut SBufferIter<core::array::IntoIter<&mut [u8], 2>>,
    ) -> Result {
        dst.write_all(&self.payload)
    }
}
