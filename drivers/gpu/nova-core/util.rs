// SPDX-License-Identifier: GPL-2.0

use kernel::prelude::*;
use kernel::time::{Delta, Instant, Monotonic};

pub(crate) const fn to_lowercase_bytes<const N: usize>(s: &str) -> [u8; N] {
    let src = s.as_bytes();
    let mut dst = [0; N];
    let mut i = 0;

    while i < src.len() && i < N {
        dst[i] = (src[i] as char).to_ascii_lowercase() as u8;
        i += 1;
    }

    dst
}

pub(crate) const fn const_bytes_to_str(bytes: &[u8]) -> &str {
    match core::str::from_utf8(bytes) {
        Ok(string) => string,
        Err(_) => kernel::build_error!("Bytes are not valid UTF-8."),
    }
}

/// Wait until `cond` is true or `timeout` elapsed.
///
/// When `cond` evaluates to `Some`, its return value is returned.
///
/// `Err(ETIMEDOUT)` is returned if `timeout` has been reached without `cond` evaluating to
/// `Some`.
///
/// TODO[DLAY]: replace with `read_poll_timeout` once it is available.
/// (https://lore.kernel.org/lkml/20250220070611.214262-8-fujita.tomonori@gmail.com/)
pub(crate) fn wait_on<R, F: Fn() -> Option<R>>(timeout: Delta, cond: F) -> Result<R> {
    let start_time = Instant::<Monotonic>::now();

    loop {
        if let Some(ret) = cond() {
            return Ok(ret);
        }

        if start_time.elapsed().as_nanos() > timeout.as_nanos() {
            return Err(ETIMEDOUT);
        }
    }
}
