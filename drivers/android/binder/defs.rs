// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

use core::mem::MaybeUninit;
use core::ops::{Deref, DerefMut};
use kernel::{
    transmute::{AsBytes, FromBytes},
    uapi::{self, *},
};

macro_rules! pub_no_prefix {
    ($prefix:ident, $($newname:ident),+ $(,)?) => {
        $(pub(crate) const $newname: u32 = kernel::macros::concat_idents!($prefix, $newname);)+
    };
}

pub_no_prefix!(
    binder_driver_return_protocol_,
    BR_TRANSACTION,
    BR_TRANSACTION_SEC_CTX,
    BR_REPLY,
    BR_DEAD_REPLY,
    BR_FAILED_REPLY,
    BR_FROZEN_REPLY,
    BR_NOOP,
    BR_SPAWN_LOOPER,
    BR_TRANSACTION_COMPLETE,
    BR_TRANSACTION_PENDING_FROZEN,
    BR_ONEWAY_SPAM_SUSPECT,
    BR_OK,
    BR_ERROR,
    BR_INCREFS,
    BR_ACQUIRE,
    BR_RELEASE,
    BR_DECREFS,
    BR_DEAD_BINDER,
    BR_CLEAR_DEATH_NOTIFICATION_DONE,
    BR_FROZEN_BINDER,
    BR_CLEAR_FREEZE_NOTIFICATION_DONE,
);

pub_no_prefix!(
    binder_driver_command_protocol_,
    BC_TRANSACTION,
    BC_TRANSACTION_SG,
    BC_REPLY,
    BC_REPLY_SG,
    BC_FREE_BUFFER,
    BC_ENTER_LOOPER,
    BC_EXIT_LOOPER,
    BC_REGISTER_LOOPER,
    BC_INCREFS,
    BC_ACQUIRE,
    BC_RELEASE,
    BC_DECREFS,
    BC_INCREFS_DONE,
    BC_ACQUIRE_DONE,
    BC_REQUEST_DEATH_NOTIFICATION,
    BC_CLEAR_DEATH_NOTIFICATION,
    BC_DEAD_BINDER_DONE,
    BC_REQUEST_FREEZE_NOTIFICATION,
    BC_CLEAR_FREEZE_NOTIFICATION,
    BC_FREEZE_NOTIFICATION_DONE,
);

pub_no_prefix!(
    flat_binder_object_flags_,
    FLAT_BINDER_FLAG_ACCEPTS_FDS,
    FLAT_BINDER_FLAG_TXN_SECURITY_CTX
);

pub_no_prefix!(
    transaction_flags_,
    TF_ONE_WAY,
    TF_ACCEPT_FDS,
    TF_CLEAR_BUF,
    TF_UPDATE_TXN
);

pub(crate) use uapi::{
    BINDER_TYPE_BINDER, BINDER_TYPE_FD, BINDER_TYPE_FDA, BINDER_TYPE_HANDLE, BINDER_TYPE_PTR,
    BINDER_TYPE_WEAK_BINDER, BINDER_TYPE_WEAK_HANDLE,
};

macro_rules! decl_wrapper {
    ($newname:ident, $wrapped:ty) => {
        // Define a wrapper around the C type. Use `MaybeUninit` to enforce that the value of
        // padding bytes must be preserved.
        #[derive(Copy, Clone)]
        #[repr(transparent)]
        pub(crate) struct $newname(MaybeUninit<$wrapped>);

        // SAFETY: This macro is only used with types where this is ok.
        unsafe impl FromBytes for $newname {}
        // SAFETY: This macro is only used with types where this is ok.
        unsafe impl AsBytes for $newname {}

        impl Deref for $newname {
            type Target = $wrapped;
            fn deref(&self) -> &Self::Target {
                // SAFETY: We use `MaybeUninit` only to preserve padding. The value must still
                // always be valid.
                unsafe { self.0.assume_init_ref() }
            }
        }

        impl DerefMut for $newname {
            fn deref_mut(&mut self) -> &mut Self::Target {
                // SAFETY: We use `MaybeUninit` only to preserve padding. The value must still
                // always be valid.
                unsafe { self.0.assume_init_mut() }
            }
        }

        impl Default for $newname {
            fn default() -> Self {
                // Create a new value of this type where all bytes (including padding) are zeroed.
                Self(MaybeUninit::zeroed())
            }
        }
    };
}

decl_wrapper!(BinderNodeDebugInfo, uapi::binder_node_debug_info);
decl_wrapper!(BinderNodeInfoForRef, uapi::binder_node_info_for_ref);
decl_wrapper!(FlatBinderObject, uapi::flat_binder_object);
decl_wrapper!(BinderFdObject, uapi::binder_fd_object);
decl_wrapper!(BinderFdArrayObject, uapi::binder_fd_array_object);
decl_wrapper!(BinderObjectHeader, uapi::binder_object_header);
decl_wrapper!(BinderBufferObject, uapi::binder_buffer_object);
decl_wrapper!(BinderTransactionData, uapi::binder_transaction_data);
decl_wrapper!(
    BinderTransactionDataSecctx,
    uapi::binder_transaction_data_secctx
);
decl_wrapper!(BinderTransactionDataSg, uapi::binder_transaction_data_sg);
decl_wrapper!(BinderWriteRead, uapi::binder_write_read);
decl_wrapper!(BinderVersion, uapi::binder_version);
decl_wrapper!(BinderFrozenStatusInfo, uapi::binder_frozen_status_info);
decl_wrapper!(BinderFreezeInfo, uapi::binder_freeze_info);
decl_wrapper!(BinderFrozenStateInfo, uapi::binder_frozen_state_info);
decl_wrapper!(BinderHandleCookie, uapi::binder_handle_cookie);
decl_wrapper!(ExtendedError, uapi::binder_extended_error);

impl BinderVersion {
    pub(crate) fn current() -> Self {
        Self(MaybeUninit::new(uapi::binder_version {
            protocol_version: BINDER_CURRENT_PROTOCOL_VERSION as _,
        }))
    }
}

impl BinderTransactionData {
    pub(crate) fn with_buffers_size(self, buffers_size: u64) -> BinderTransactionDataSg {
        BinderTransactionDataSg(MaybeUninit::new(uapi::binder_transaction_data_sg {
            transaction_data: *self,
            buffers_size,
        }))
    }
}

impl BinderTransactionDataSecctx {
    /// View the inner data as wrapped in `BinderTransactionData`.
    pub(crate) fn tr_data(&mut self) -> &mut BinderTransactionData {
        // SAFETY: Transparent wrapper is safe to transmute.
        unsafe {
            &mut *(&mut self.transaction_data as *mut uapi::binder_transaction_data
                as *mut BinderTransactionData)
        }
    }
}

impl ExtendedError {
    pub(crate) fn new(id: u32, command: u32, param: i32) -> Self {
        Self(MaybeUninit::new(uapi::binder_extended_error {
            id,
            command,
            param,
        }))
    }
}
