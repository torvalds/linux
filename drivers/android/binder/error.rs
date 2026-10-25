// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

use core::panic::Location;
use kernel::fmt;
use kernel::prelude::*;

use crate::defs::*;

pub(crate) type BinderResult<T = ()> = core::result::Result<T, BinderError>;

/// Wraps a `&'static Location` to format it as `filename.rs:line` without the directory prefix.
#[derive(Copy, Clone)]
#[repr(transparent)]
pub(crate) struct ErrorLocation(&'static Location<'static>);

// SAFETY: `ErrorLocation` is `repr(transparent)` over a shared reference, which is part of the
// option layout optimization guarantee, so all zeroes is a valid representation for `None`.
unsafe impl pin_init::ZeroableOption for ErrorLocation {}

impl ErrorLocation {
    #[track_caller]
    pub(crate) const fn caller() -> Self {
        Self(Location::caller())
    }
}

impl fmt::Display for ErrorLocation {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let file = match self.0.file().rsplit_once('/') {
            Some((_, file)) => file,
            None => self.0.file(),
        };
        write!(f, "{}:{}", file, self.0.line())
    }
}

/// An error that will be returned to userspace via the `BINDER_WRITE_READ` ioctl rather than via
/// errno.
pub(crate) struct BinderError {
    pub(crate) reply: u32,
    pub(crate) source: Option<Error>,
    pub(crate) line: ErrorLocation,
}

impl BinderError {
    #[track_caller]
    pub(crate) fn new_dead() -> Self {
        Self {
            reply: BR_DEAD_REPLY,
            source: None,
            line: ErrorLocation::caller(),
        }
    }

    #[track_caller]
    pub(crate) fn new_frozen() -> Self {
        Self {
            reply: BR_FROZEN_REPLY,
            source: None,
            line: ErrorLocation::caller(),
        }
    }

    #[track_caller]
    pub(crate) fn new_frozen_oneway() -> Self {
        Self {
            reply: BR_TRANSACTION_PENDING_FROZEN,
            source: None,
            line: ErrorLocation::caller(),
        }
    }
}

/// Convert an errno into a `BinderError` and store the errno used to construct it. The errno
/// should be stored as the thread's extended error when given to userspace.
impl From<Error> for BinderError {
    #[track_caller]
    fn from(source: Error) -> Self {
        Self {
            reply: BR_FAILED_REPLY,
            source: Some(source),
            line: ErrorLocation::caller(),
        }
    }
}

impl From<kernel::fs::file::BadFdError> for BinderError {
    #[track_caller]
    fn from(source: kernel::fs::file::BadFdError) -> Self {
        BinderError::from(Error::from(source))
    }
}

impl From<kernel::alloc::AllocError> for BinderError {
    #[track_caller]
    fn from(_: kernel::alloc::AllocError) -> Self {
        Self {
            reply: BR_FAILED_REPLY,
            source: Some(ENOMEM),
            line: ErrorLocation::caller(),
        }
    }
}

impl fmt::Debug for BinderError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.reply {
            BR_FAILED_REPLY => match self.source.as_ref() {
                Some(source) => source.fmt(f),
                None => f.pad("BR_FAILED_REPLY"),
            },
            BR_DEAD_REPLY => f.pad("BR_DEAD_REPLY"),
            BR_FROZEN_REPLY => f.pad("BR_FROZEN_REPLY"),
            BR_TRANSACTION_PENDING_FROZEN => f.pad("BR_TRANSACTION_PENDING_FROZEN"),
            BR_TRANSACTION_COMPLETE => f.pad("BR_TRANSACTION_COMPLETE"),
            _ => match self.source.as_ref() {
                Some(source) => source.fmt(f),
                None => self.reply.fmt(f),
            },
        }
    }
}
