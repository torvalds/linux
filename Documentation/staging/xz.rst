.. SPDX-License-Identifier: 0BSD

============================
XZ data compression in Linux
============================

Introduction
============

XZ is a general purpose data compression format with high compression
ratio. The XZ decompressor in Linux is called XZ Embedded. It supports
the LZMA2 filter and optionally also Branch/Call/Jump (BCJ) filters
for executable code. CRC32 is supported for integrity checking.

See the `XZ Embedded`_ home page for the latest version which includes
a few optional extra features that aren't required in the Linux kernel
and information about using the code outside the Linux kernel.

For userspace, `XZ Utils`_ provide a zlib-like compression library
and a gzip-like command line tool.

.. _XZ Embedded: https://tukaani.org/xz/embedded.html
.. _XZ Utils: https://tukaani.org/xz/

XZ related components in the kernel
===================================

The xz_dec module provides XZ decompressor with single-call (buffer
to buffer) and multi-call (stateful) APIs in include/linux/xz.h.

For decompressing the kernel image, initramfs, and initrd, there
is a wrapper function in lib/decompress_unxz.c. Its API is the
same as in other decompress_*.c files, which is defined in
include/linux/decompress/generic.h.

For kernel makefiles, three commands are provided for use with
``$(call if_changed)``. They require the xz tool from XZ Utils.

- ``$(call if_changed,xzkern)`` is for compressing the kernel image.
  It runs the script scripts/xz_wrap.sh which uses arch-optimized
  options and a big LZMA2 dictionary.

- ``$(call if_changed,xzkern_with_size)`` is like ``xzkern`` above but
  this also appends a four-byte trailer containing the uncompressed size
  of the file. The trailer is needed by the boot code on some archs.

- Other things can be compressed with ``$(call if_needed,xzmisc)``
  which will use no BCJ filter and 1 MiB LZMA2 dictionary.

Notes on compression options
============================

Since the XZ Embedded supports only streams with CRC32 or no integrity
check, make sure that you don't use some other integrity check type
when encoding files that are supposed to be decoded by the kernel.
With liblzma from XZ Utils, you need to use either ``LZMA_CHECK_CRC32``
or ``LZMA_CHECK_NONE`` when encoding. With the ``xz`` command line tool,
use ``--check=crc32`` or ``--check=none`` to override the default
``--check=crc64``.

Using CRC32 is strongly recommended unless there is some other layer
which will verify the integrity of the uncompressed data anyway.
Double checking the integrity would probably be waste of CPU cycles.
Note that the headers will always have a CRC32 which will be validated
by the decoder; you can only change the integrity check type (or
disable it) for the actual uncompressed data.

In userspace, LZMA2 is typically used with dictionary sizes of several
megabytes. The decoder needs to have the dictionary in RAM:

- In multi-call mode the dictionary is allocated as part of the
  decoder state. The reasonable maximum dictionary size for in-kernel
  use will depend on the target hardware: a few megabytes is fine for
  desktop systems while 64 KiB to 1 MiB might be more appropriate on
  some embedded systems.

- In single-call mode the output buffer is used as the dictionary
  buffer. That is, the size of the dictionary doesn't affect the
  decompressor memory usage at all. Only the base data structures
  are allocated which take a little less than 30 KiB of memory.
  For the best compression, the dictionary should be at least
  as big as the uncompressed data. A notable example of single-call
  mode is decompressing the kernel itself (except on PowerPC).

The compression presets in XZ Utils may not be optimal when creating
files for the kernel, so don't hesitate to use custom settings to,
for example, set the dictionary size. Also, xz may produce a smaller
file in single-threaded mode so setting that explicitly is recommended.
Example::

    xz --threads=1 --check=crc32 --lzma2=dict=512KiB inputfile

xz_dec API
==========

This is available with ``#include <linux/xz.h>``.

``XZ_EXTERN`` is a macro used in the preboot code. Ignore it when
reading this documentation.

.. kernel-doc:: include/linux/xz.h
