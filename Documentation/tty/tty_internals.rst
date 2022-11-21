.. SPDX-License-Identifier: GPL-2.0

=============
TTY Internals
=============

.. contents:: :local:

Kopen
=====

These functions serve for opening a TTY from the kernelspace:

.. kernel-doc:: drivers/tty/tty_io.c
      :identifiers: tty_kopen_exclusive tty_kopen_shared tty_kclose

----

Exported Internal Functions
===========================

.. kernel-doc:: drivers/tty/tty_io.c
   :identifiers: tty_release_struct tty_dev_name_to_number tty_get_icount

----

Internal Functions
==================

.. kernel-doc:: drivers/tty/tty_io.c
   :internal:
