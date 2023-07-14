.. SPDX-License-Identifier: GPL-2.0

=================================
Linux-specific ELF idiosyncrasies
=================================

Definitions
===========

"First" program header is the one with the smallest offset in the file:
e_phoff.

"Last" program header is the one with the biggest offset in the file:
e_phoff + (e_phnum - 1) * sizeof(Elf_Phdr).

PT_INTERP
=========

First PT_INTERP program header is used to locate the filename of ELF
interpreter. Other PT_INTERP headers are ignored (since Linux 2.4.11).

PT_GNU_STACK
============

Last PT_GNU_STACK program header defines userspace stack executability
(since Linux 2.6.6). Other PT_GNU_STACK headers are ignored.

PT_GNU_PROPERTY
===============

ELF interpreter's last PT_GNU_PROPERTY program header is used (since
Linux 5.8). If interpreter doesn't have one, then the last PT_GNU_PROPERTY
program header of an executable is used. Other PT_GNU_PROPERTY headers
are ignored.
