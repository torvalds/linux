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

#[kunit_tests(nova_core_gsp_continuation)]
mod tests {
    use super::*;

    use kernel::transmute::{
        AsBytes,
        FromBytes, //
    };

    /// Non-zero-sized command header for testing.
    #[repr(C)]
    #[derive(Clone, Copy, Zeroable)]
    struct TestHeader([u8; 64]);

    // SAFETY: `TestHeader` is a plain array of bytes for which all bit patterns are valid.
    unsafe impl FromBytes for TestHeader {}

    // SAFETY: `TestHeader` is a plain array of bytes for which all bit patterns are valid.
    unsafe impl AsBytes for TestHeader {}

    struct TestPayload {
        data: KVVec<u8>,
    }

    impl TestPayload {
        fn generate_pattern(len: usize) -> Result<KVVec<u8>> {
            let mut data = KVVec::with_capacity(len, GFP_KERNEL)?;
            for i in 0..len {
                // Mix in higher bits so the pattern does not repeat every 256 bytes.
                data.push((i ^ (i >> 8)) as u8, GFP_KERNEL)?;
            }
            Ok(data)
        }

        fn new(len: usize) -> Result<Self> {
            Ok(Self {
                data: Self::generate_pattern(len)?,
            })
        }
    }

    impl CommandToGsp for TestPayload {
        const FUNCTION: MsgFunction = MsgFunction::Nop;
        type Command = TestHeader;
        type InitError = Infallible;

        fn init(&self) -> impl Init<Self::Command, Self::InitError> {
            TestHeader::init_zeroed()
        }

        fn variable_payload_len(&self) -> usize {
            self.data.len()
        }

        fn init_variable_payload(
            &self,
            dst: &mut SBufferIter<core::array::IntoIter<&mut [u8], 2>>,
        ) -> Result {
            dst.write_all(self.data.as_slice())
        }
    }

    /// Maximum variable payload size that fits in the first command alongside the header.
    const MAX_FIRST_PAYLOAD: usize = SplitState::<TestPayload>::MAX_FIRST_PAYLOAD;

    fn read_payload(cmd: impl CommandToGsp) -> Result<KVVec<u8>> {
        let len = cmd.variable_payload_len();
        let mut buf = KVVec::from_elem(0u8, len, GFP_KERNEL)?;
        let mut sbuf = SBufferIter::new_writer([buf.as_mut_slice(), &mut []]);
        cmd.init_variable_payload(&mut sbuf)?;
        drop(sbuf);
        Ok(buf)
    }

    struct SplitTest {
        payload_size: usize,
        num_continuations: usize,
    }

    fn check_split(t: SplitTest) -> Result {
        let payload = TestPayload::new(t.payload_size)?;
        let mut num_continuations = 0;

        let buf = match SplitState::new(payload)? {
            SplitState::Single(cmd) => read_payload(cmd)?,
            SplitState::Split(cmd, mut continuations) => {
                let mut buf = read_payload(cmd)?;
                assert!(size_of::<TestHeader>() + buf.len() <= MAX_CMD_SIZE);

                while let Some(cont) = continuations.next() {
                    let payload = read_payload(cont)?;
                    assert!(payload.len() <= MAX_CMD_SIZE);
                    buf.extend_from_slice(&payload, GFP_KERNEL)?;
                    num_continuations += 1;
                }

                buf
            }
        };

        assert_eq!(num_continuations, t.num_continuations);
        assert_eq!(
            buf.as_slice(),
            TestPayload::generate_pattern(t.payload_size)?.as_slice()
        );
        Ok(())
    }

    #[test]
    fn split_command() -> Result {
        check_split(SplitTest {
            payload_size: 0,
            num_continuations: 0,
        })?;
        check_split(SplitTest {
            payload_size: MAX_FIRST_PAYLOAD,
            num_continuations: 0,
        })?;
        check_split(SplitTest {
            payload_size: MAX_FIRST_PAYLOAD + 1,
            num_continuations: 1,
        })?;
        check_split(SplitTest {
            payload_size: MAX_FIRST_PAYLOAD + MAX_CMD_SIZE,
            num_continuations: 1,
        })?;
        check_split(SplitTest {
            payload_size: MAX_FIRST_PAYLOAD + MAX_CMD_SIZE + 1,
            num_continuations: 2,
        })?;
        check_split(SplitTest {
            payload_size: MAX_FIRST_PAYLOAD + MAX_CMD_SIZE * 3 + MAX_CMD_SIZE / 2,
            num_continuations: 4,
        })?;
        Ok(())
    }
}
