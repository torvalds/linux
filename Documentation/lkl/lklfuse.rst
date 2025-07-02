.. SPDX-License-Identifier: GPL-2.0

=========
 lklfuse
=========

-----------------------------------------
access LKL mounted block devices via FUSE
-----------------------------------------

:Date: 2025-06-23
:Manual section: 8

SYNOPSIS
========

lklfuse block-device mountpoint [options]

DESCRIPTION
===========

lklfuse uses the Linux Kernel Library (LKL) to mount a block-device or
filesystem image, and provides access to the host system via FUSE.
lklfuse can run as an unprivileged user-space process, while reusing entire
Linux kernel filesystem driver implementations.

Udev rules and systemd service files are available for automatically mounting
USB storage devices via an unprivileged lklfuse sandbox; see 61-lklfuse.rules
and lklfuse-mount@.service.

OPTIONS
=======

-o log=<file>           log to <file>.

-o type=fstype          mount with filesystem type <fstype>.

-o mb=memory            allocate <memory> in MB for LKL (default: 64).

-o part=parition        mount <partition>.

-o ro                   open block-device read-only.

-o lock=<file>          only mount after taking an exclusive lock on <file>.

-o opts=options         Linux kernel mount <options> (use \\ to escape , and =).

See `lklfuse --help` for additional FUSE specific options.
