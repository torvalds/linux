.. SPDX-License-Identifier: GPL-2.0

bcachefs private error codes
----------------------------

In bcachefs, as a hard rule we do not throw or directly use standard error
codes (-EINVAL, -EBUSY, etc.). Instead, we define private error codes as needed
in fs/bcachefs/errcode.h.

This gives us much better error messages and makes debugging much easier. Any
direct uses of standard error codes you see in the source code are simply old
code that has yet to be converted - feel free to clean it up!

Private error codes may subtype another error code, this allows for grouping of
related errors that should be handled similarly (e.g. transaction restart
errors), as well as specifying which standard error code should be returned at
the bcachefs module boundary.

At the module boundary, we use bch2_err_class() to convert to a standard error
code; this also emits a trace event so that the original error code be
recovered even if it wasn't logged.

Do not reuse error codes! Generally speaking, a private error code should only
be thrown in one place. That means that when we see it in a log message we can
see, unambiguously, exactly which file and line number it was returned from.

Try to give error codes names that are as reasonably descriptive of the error
as possible. Frequently, the error will be logged at a place far removed from
where the error was generated; good names for error codes mean much more
descriptive and useful error messages.
