.. contents::
.. sectnum::

===================================================
BPF ABI Recommended Conventions and Guidelines v1.0
===================================================

This is version 1.0 of an informational document containing recommended
conventions and guidelines for producing portable BPF program binaries.

Registers and calling convention
================================

BPF has 10 general purpose registers and a read-only frame pointer register,
all of which are 64-bits wide.

The BPF calling convention is defined as:

* R0: return value from function calls, and exit value for BPF programs
* R1 - R5: arguments for function calls
* R6 - R9: callee saved registers that function calls will preserve
* R10: read-only frame pointer to access stack

R0 - R5 are scratch registers and BPF programs needs to spill/fill them if
necessary across calls.
