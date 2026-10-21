// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Based on: Documentation/netlink/specs/binder.yaml */

#![allow(unreachable_pub, clippy::wrong_self_convention)]
use kernel::{
    net::netlink::{
        Family,
        GenlMsg,
        MulticastGroup,
        NetlinkSkBuff, //
    },
    prelude::*, //
};

pub static BINDER_NL_FAMILY: Family = Family::const_new(
    kernel::module::this_module::<crate::LocalModule>(),
    kernel::uapi::BINDER_FAMILY_NAME,
    kernel::uapi::BINDER_FAMILY_VERSION,
    &BINDER_NL_FAMILY_MCGRPS,
);

static BINDER_NL_FAMILY_MCGRPS: [MulticastGroup; 1] = [MulticastGroup::const_new(c"report")];

/// A multicast event sent to userspace subscribers to notify them about
/// binder transaction failures. The generated report provides the full
/// details of the specific transaction that failed. The intention is for
/// programs to monitor these events and react to the failures as needed.
pub struct Report {
    skb: GenlMsg,
}

impl Report {
    /// Create a new multicast message.
    pub fn new(
        size: usize,
        portid: u32,
        seq: u32,
        flags: kernel::alloc::Flags,
    ) -> Result<Self, kernel::alloc::AllocError> {
        const BINDER_CMD_REPORT: u8 = kernel::uapi::BINDER_CMD_REPORT as u8;
        let skb = NetlinkSkBuff::new(size, flags)?;
        let skb = skb.genlmsg_put(portid, seq, &BINDER_NL_FAMILY, BINDER_CMD_REPORT)?;
        Ok(Self { skb })
    }

    /// Broadcast this message.
    pub fn multicast(self, portid: u32, flags: kernel::alloc::Flags) -> Result {
        self.skb.multicast(&BINDER_NL_FAMILY, portid, 0, flags)
    }

    /// Check if this message type has listeners.
    pub fn has_listeners() -> bool {
        BINDER_NL_FAMILY.has_listeners(0)
    }

    /// The enum binder_driver_return_protocol returned to the sender.
    pub fn error(&mut self, val: u32) -> Result {
        const BINDER_A_REPORT_ERROR: c_int = kernel::uapi::BINDER_A_REPORT_ERROR as c_int;
        self.skb.put_u32(BINDER_A_REPORT_ERROR, val)
    }

    /// The binder context where the transaction occurred.
    pub fn context(&mut self, val: &CStr) -> Result {
        const BINDER_A_REPORT_CONTEXT: c_int = kernel::uapi::BINDER_A_REPORT_CONTEXT as c_int;
        self.skb.put_string(BINDER_A_REPORT_CONTEXT, val)
    }

    /// The PID of the sender process.
    pub fn from_pid(&mut self, val: u32) -> Result {
        const BINDER_A_REPORT_FROM_PID: c_int = kernel::uapi::BINDER_A_REPORT_FROM_PID as c_int;
        self.skb.put_u32(BINDER_A_REPORT_FROM_PID, val)
    }

    /// The TID of the sender thread.
    pub fn from_tid(&mut self, val: u32) -> Result {
        const BINDER_A_REPORT_FROM_TID: c_int = kernel::uapi::BINDER_A_REPORT_FROM_TID as c_int;
        self.skb.put_u32(BINDER_A_REPORT_FROM_TID, val)
    }

    /// The PID of the recipient process. This attribute may not be present
    /// if the target could not be determined.
    pub fn to_pid(&mut self, val: u32) -> Result {
        const BINDER_A_REPORT_TO_PID: c_int = kernel::uapi::BINDER_A_REPORT_TO_PID as c_int;
        self.skb.put_u32(BINDER_A_REPORT_TO_PID, val)
    }

    /// The TID of the recipient thread. This attribute may not be present
    /// if the target could not be determined.
    pub fn to_tid(&mut self, val: u32) -> Result {
        const BINDER_A_REPORT_TO_TID: c_int = kernel::uapi::BINDER_A_REPORT_TO_TID as c_int;
        self.skb.put_u32(BINDER_A_REPORT_TO_TID, val)
    }

    /// When present, indicates the failed transaction is a reply.
    pub fn is_reply(&mut self) -> Result {
        const BINDER_A_REPORT_IS_REPLY: c_int = kernel::uapi::BINDER_A_REPORT_IS_REPLY as c_int;
        self.skb.put_flag(BINDER_A_REPORT_IS_REPLY)
    }

    /// The bitmask of enum transaction_flags from the transaction.
    pub fn flags(&mut self, val: u32) -> Result {
        const BINDER_A_REPORT_FLAGS: c_int = kernel::uapi::BINDER_A_REPORT_FLAGS as c_int;
        self.skb.put_u32(BINDER_A_REPORT_FLAGS, val)
    }

    /// The application-defined code from the transaction.
    pub fn code(&mut self, val: u32) -> Result {
        const BINDER_A_REPORT_CODE: c_int = kernel::uapi::BINDER_A_REPORT_CODE as c_int;
        self.skb.put_u32(BINDER_A_REPORT_CODE, val)
    }

    /// The transaction payload size in bytes.
    pub fn data_size(&mut self, val: u32) -> Result {
        const BINDER_A_REPORT_DATA_SIZE: c_int = kernel::uapi::BINDER_A_REPORT_DATA_SIZE as c_int;
        self.skb.put_u32(BINDER_A_REPORT_DATA_SIZE, val)
    }
}
