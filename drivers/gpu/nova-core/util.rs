// SPDX-License-Identifier: GPL-2.0

use kernel::prelude::*;
use kernel::time::{Delta, Instant, Monotonic};

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
