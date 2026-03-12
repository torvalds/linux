// SPDX-License-Identifier: GPL-2.0

//! GSP Sequencer implementation for Pre-hopper GSP boot sequence.

use core::array;

use kernel::{
    device,
    io::{
        poll::read_poll_timeout,
        Io, //
    },
    prelude::*,
    sync::aref::ARef,
    time::{
        delay::fsleep,
        Delta, //
    },
    transmute::FromBytes, //
};

use crate::{
    driver::Bar0,
    falcon::{
        gsp::Gsp,
        sec2::Sec2,
        Falcon, //
    },
    gsp::{
        cmdq::{
            Cmdq,
            MessageFromGsp, //
        },
        fw,
    },
    num::FromSafeCast,
    sbuffer::SBufferIter,
};

/// GSP Sequencer information containing the command sequence and data.
struct GspSequence {
    /// Current command index for error reporting.
    cmd_index: u32,
    /// Command data buffer containing the sequence of commands.
    cmd_data: KVec<u8>,
}

impl MessageFromGsp for GspSequence {
    const FUNCTION: fw::MsgFunction = fw::MsgFunction::GspRunCpuSequencer;
    type InitError = Error;
    type Message = fw::RunCpuSequencer;

    fn read(
        msg: &Self::Message,
        sbuffer: &mut SBufferIter<array::IntoIter<&[u8], 2>>,
    ) -> Result<Self, Self::InitError> {
        let cmd_data = sbuffer.flush_into_kvec(GFP_KERNEL)?;
        Ok(GspSequence {
            cmd_index: msg.cmd_index(),
            cmd_data,
        })
    }
}

const CMD_SIZE: usize = size_of::<fw::SequencerBufferCmd>();

/// GSP Sequencer Command types with payload data.
/// Commands have an opcode and an opcode-dependent struct.
#[allow(clippy::enum_variant_names)]
pub(crate) enum GspSeqCmd {
    RegWrite(fw::RegWritePayload),
    RegModify(fw::RegModifyPayload),
    RegPoll(fw::RegPollPayload),
    DelayUs(fw::DelayUsPayload),
    RegStore(fw::RegStorePayload),
    CoreReset,
    CoreStart,
    CoreWaitForHalt,
    CoreResume,
}

impl GspSeqCmd {
    /// Creates a new `GspSeqCmd` from raw data returning the command and its size in bytes.
    pub(crate) fn new(data: &[u8], dev: &device::Device) -> Result<(Self, usize)> {
        let fw_cmd = fw::SequencerBufferCmd::from_bytes(data).ok_or(EINVAL)?;
        let opcode_size = core::mem::size_of::<u32>();

        let (cmd, size) = match fw_cmd.opcode()? {
            fw::SeqBufOpcode::RegWrite => {
                let payload = fw_cmd.reg_write_payload()?;
                let size = opcode_size + size_of_val(&payload);
                (GspSeqCmd::RegWrite(payload), size)
            }
            fw::SeqBufOpcode::RegModify => {
                let payload = fw_cmd.reg_modify_payload()?;
                let size = opcode_size + size_of_val(&payload);
                (GspSeqCmd::RegModify(payload), size)
            }
            fw::SeqBufOpcode::RegPoll => {
                let payload = fw_cmd.reg_poll_payload()?;
                let size = opcode_size + size_of_val(&payload);
                (GspSeqCmd::RegPoll(payload), size)
            }
            fw::SeqBufOpcode::DelayUs => {
                let payload = fw_cmd.delay_us_payload()?;
                let size = opcode_size + size_of_val(&payload);
                (GspSeqCmd::DelayUs(payload), size)
            }
            fw::SeqBufOpcode::RegStore => {
                let payload = fw_cmd.reg_store_payload()?;
                let size = opcode_size + size_of_val(&payload);
                (GspSeqCmd::RegStore(payload), size)
            }
            fw::SeqBufOpcode::CoreReset => (GspSeqCmd::CoreReset, opcode_size),
            fw::SeqBufOpcode::CoreStart => (GspSeqCmd::CoreStart, opcode_size),
            fw::SeqBufOpcode::CoreWaitForHalt => (GspSeqCmd::CoreWaitForHalt, opcode_size),
            fw::SeqBufOpcode::CoreResume => (GspSeqCmd::CoreResume, opcode_size),
        };

        if data.len() < size {
            dev_err!(dev, "Data is not enough for command\n");
            return Err(EINVAL);
        }

        Ok((cmd, size))
    }
}

/// GSP Sequencer for executing firmware commands during boot.
pub(crate) struct GspSequencer<'a> {
    /// Sequencer information with command data.
    seq_info: GspSequence,
    /// `Bar0` for register access.
    bar: &'a Bar0,
    /// SEC2 falcon for core operations.
    sec2_falcon: &'a Falcon<Sec2>,
    /// GSP falcon for core operations.
    gsp_falcon: &'a Falcon<Gsp>,
    /// LibOS DMA handle address.
    libos_dma_handle: u64,
    /// Bootloader application version.
    bootloader_app_version: u32,
    /// Device for logging.
    dev: ARef<device::Device>,
}

/// Trait for running sequencer commands.
pub(crate) trait GspSeqCmdRunner {
    fn run(&self, sequencer: &GspSequencer<'_>) -> Result;
}

impl GspSeqCmdRunner for fw::RegWritePayload {
    fn run(&self, sequencer: &GspSequencer<'_>) -> Result {
        let addr = usize::from_safe_cast(self.addr());

        sequencer.bar.try_write32(self.val(), addr)
    }
}

impl GspSeqCmdRunner for fw::RegModifyPayload {
    fn run(&self, sequencer: &GspSequencer<'_>) -> Result {
        let addr = usize::from_safe_cast(self.addr());

        sequencer.bar.try_read32(addr).and_then(|val| {
            sequencer
                .bar
                .try_write32((val & !self.mask()) | self.val(), addr)
        })
    }
}

impl GspSeqCmdRunner for fw::RegPollPayload {
    fn run(&self, sequencer: &GspSequencer<'_>) -> Result {
        let addr = usize::from_safe_cast(self.addr());

        // Default timeout to 4 seconds.
        let timeout_us = if self.timeout() == 0 {
            4_000_000
        } else {
            i64::from(self.timeout())
        };

        // First read.
        sequencer.bar.try_read32(addr)?;

        // Poll the requested register with requested timeout.
        read_poll_timeout(
            || sequencer.bar.try_read32(addr),
            |current| (current & self.mask()) == self.val(),
            Delta::ZERO,
            Delta::from_micros(timeout_us),
        )
        .map(|_| ())
    }
}

impl GspSeqCmdRunner for fw::DelayUsPayload {
    fn run(&self, _sequencer: &GspSequencer<'_>) -> Result {
        fsleep(Delta::from_micros(i64::from(self.val())));
        Ok(())
    }
}

impl GspSeqCmdRunner for fw::RegStorePayload {
    fn run(&self, sequencer: &GspSequencer<'_>) -> Result {
        let addr = usize::from_safe_cast(self.addr());

        sequencer.bar.try_read32(addr).map(|_| ())
    }
}

impl GspSeqCmdRunner for GspSeqCmd {
    fn run(&self, seq: &GspSequencer<'_>) -> Result {
        match self {
            GspSeqCmd::RegWrite(cmd) => cmd.run(seq),
            GspSeqCmd::RegModify(cmd) => cmd.run(seq),
            GspSeqCmd::RegPoll(cmd) => cmd.run(seq),
            GspSeqCmd::DelayUs(cmd) => cmd.run(seq),
            GspSeqCmd::RegStore(cmd) => cmd.run(seq),
            GspSeqCmd::CoreReset => {
                seq.gsp_falcon.reset(seq.bar)?;
                seq.gsp_falcon.dma_reset(seq.bar);
                Ok(())
            }
            GspSeqCmd::CoreStart => {
                seq.gsp_falcon.start(seq.bar)?;
                Ok(())
            }
            GspSeqCmd::CoreWaitForHalt => {
                seq.gsp_falcon.wait_till_halted(seq.bar)?;
                Ok(())
            }
            GspSeqCmd::CoreResume => {
                // At this point, 'SEC2-RTOS' has been loaded into SEC2 by the sequencer
                // but neither SEC2-RTOS nor GSP-RM is running yet. This part of the
                // sequencer will start both.

                // Reset the GSP to prepare it for resuming.
                seq.gsp_falcon.reset(seq.bar)?;

                // Write the libOS DMA handle to GSP mailboxes.
                seq.gsp_falcon.write_mailboxes(
                    seq.bar,
                    Some(seq.libos_dma_handle as u32),
                    Some((seq.libos_dma_handle >> 32) as u32),
                );

                // Start the SEC2 falcon which will trigger GSP-RM to resume on the GSP.
                seq.sec2_falcon.start(seq.bar)?;

                // Poll until GSP-RM reload/resume has completed (up to 2 seconds).
                seq.gsp_falcon
                    .check_reload_completed(seq.bar, Delta::from_secs(2))?;

                // Verify SEC2 completed successfully by checking its mailbox for errors.
                let mbox0 = seq.sec2_falcon.read_mailbox0(seq.bar);
                if mbox0 != 0 {
                    dev_err!(seq.dev, "Sequencer: sec2 errors: {:?}\n", mbox0);
                    return Err(EIO);
                }

                // Configure GSP with the bootloader version.
                seq.gsp_falcon
                    .write_os_version(seq.bar, seq.bootloader_app_version);

                // Verify the GSP's RISC-V core is active indicating successful GSP boot.
                if !seq.gsp_falcon.is_riscv_active(seq.bar) {
                    dev_err!(seq.dev, "Sequencer: RISC-V core is not active\n");
                    return Err(EIO);
                }
                Ok(())
            }
        }
    }
}

/// Iterator over GSP sequencer commands.
pub(crate) struct GspSeqIter<'a> {
    /// Command data buffer.
    cmd_data: &'a [u8],
    /// Current position in the buffer.
    current_offset: usize,
    /// Total number of commands to process.
    total_cmds: u32,
    /// Number of commands processed so far.
    cmds_processed: u32,
    /// Device for logging.
    dev: ARef<device::Device>,
}

impl<'a> Iterator for GspSeqIter<'a> {
    type Item = Result<GspSeqCmd>;

    fn next(&mut self) -> Option<Self::Item> {
        // Stop if we've processed all commands or reached the end of data.
        if self.cmds_processed >= self.total_cmds || self.current_offset >= self.cmd_data.len() {
            return None;
        }

        // Check if we have enough data for opcode.
        if self.current_offset + core::mem::size_of::<u32>() > self.cmd_data.len() {
            return Some(Err(EIO));
        }

        let offset = self.current_offset;

        // Handle command creation based on available data,
        // zero-pad if necessary (since last command may not be full size).
        let mut buffer = [0u8; CMD_SIZE];
        let copy_len = if offset + CMD_SIZE <= self.cmd_data.len() {
            CMD_SIZE
        } else {
            self.cmd_data.len() - offset
        };
        buffer[..copy_len].copy_from_slice(&self.cmd_data[offset..offset + copy_len]);
        let cmd_result = GspSeqCmd::new(&buffer, &self.dev);

        cmd_result.map_or_else(
            |_err| {
                dev_err!(self.dev, "Error parsing command at offset {}\n", offset);
                None
            },
            |(cmd, size)| {
                self.current_offset += size;
                self.cmds_processed += 1;
                Some(Ok(cmd))
            },
        )
    }
}

impl<'a> GspSequencer<'a> {
    fn iter(&self) -> GspSeqIter<'_> {
        let cmd_data = &self.seq_info.cmd_data[..];

        GspSeqIter {
            cmd_data,
            current_offset: 0,
            total_cmds: self.seq_info.cmd_index,
            cmds_processed: 0,
            dev: self.dev.clone(),
        }
    }
}

/// Parameters for running the GSP sequencer.
pub(crate) struct GspSequencerParams<'a> {
    /// Bootloader application version.
    pub(crate) bootloader_app_version: u32,
    /// LibOS DMA handle address.
    pub(crate) libos_dma_handle: u64,
    /// GSP falcon for core operations.
    pub(crate) gsp_falcon: &'a Falcon<Gsp>,
    /// SEC2 falcon for core operations.
    pub(crate) sec2_falcon: &'a Falcon<Sec2>,
    /// Device for logging.
    pub(crate) dev: ARef<device::Device>,
    /// BAR0 for register access.
    pub(crate) bar: &'a Bar0,
}

impl<'a> GspSequencer<'a> {
    pub(crate) fn run(cmdq: &mut Cmdq, params: GspSequencerParams<'a>) -> Result {
        let seq_info = loop {
            match cmdq.receive_msg::<GspSequence>(Delta::from_secs(10)) {
                Ok(seq_info) => break seq_info,
                Err(ERANGE) => continue,
                Err(e) => return Err(e),
            }
        };

        let sequencer = GspSequencer {
            seq_info,
            bar: params.bar,
            sec2_falcon: params.sec2_falcon,
            gsp_falcon: params.gsp_falcon,
            libos_dma_handle: params.libos_dma_handle,
            bootloader_app_version: params.bootloader_app_version,
            dev: params.dev,
        };

        dev_dbg!(sequencer.dev, "Running CPU Sequencer commands\n");

        for cmd_result in sequencer.iter() {
            match cmd_result {
                Ok(cmd) => cmd.run(&sequencer)?,
                Err(e) => {
                    dev_err!(
                        sequencer.dev,
                        "Error running command at index {}\n",
                        sequencer.seq_info.cmd_index
                    );
                    return Err(e);
                }
            }
        }

        dev_dbg!(
            sequencer.dev,
            "CPU Sequencer commands completed successfully\n"
        );
        Ok(())
    }
}
