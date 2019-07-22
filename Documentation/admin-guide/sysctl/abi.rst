================================
Documentation for /proc/sys/abi/
================================

kernel version 2.6.0.test2

Copyright (c) 2003,  Fabian Frederick <ffrederick@users.sourceforge.net>

For general info: index.rst.

------------------------------------------------------------------------------

This path is binary emulation relevant aka personality types aka abi.
When a process is executed, it's linked to an exec_domain whose
personality is defined using values available from /proc/sys/abi.
You can find further details about abi in include/linux/personality.h.

Here are the files featuring in 2.6 kernel:

- defhandler_coff
- defhandler_elf
- defhandler_lcall7
- defhandler_libcso
- fake_utsname
- trace

defhandler_coff
---------------

defined value:
	PER_SCOSVR3::

		0x0003 | STICKY_TIMEOUTS | WHOLE_SECONDS | SHORT_INODE

defhandler_elf
--------------

defined value:
	PER_LINUX::

		0

defhandler_lcall7
-----------------

defined value :
	PER_SVR4::

		0x0001 | STICKY_TIMEOUTS | MMAP_PAGE_ZERO,

defhandler_libsco
-----------------

defined value:
	PER_SVR4::

		0x0001 | STICKY_TIMEOUTS | MMAP_PAGE_ZERO,

fake_utsname
------------

Unused

trace
-----

Unused
