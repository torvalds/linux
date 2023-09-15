.. SPDX-License-Identifier: GPL-2.0

==========
TTY Buffer
==========

.. contents:: :local:

Here, we document functions for taking care of tty buffer and their flipping.
Drivers are supposed to fill the buffer by one of those functions below and
then flip the buffer, so that the data are passed to :doc:`line discipline
<tty_ldisc>` for further processing.

Flip Buffer Management
======================

.. kernel-doc:: drivers/tty/tty_buffer.c
   :identifiers: tty_prepare_flip_string
           tty_flip_buffer_push tty_ldisc_receive_buf

.. kernel-doc:: include/linux/tty_flip.h
   :identifiers: tty_insert_flip_string_fixed_flag tty_insert_flip_string_flags
           tty_insert_flip_char

----

Other Functions
===============

.. kernel-doc:: drivers/tty/tty_buffer.c
   :identifiers: tty_buffer_space_avail tty_buffer_set_limit

----

Buffer Locking
==============

These are used only in special circumstances. Avoid them.

.. kernel-doc:: drivers/tty/tty_buffer.c
   :identifiers: tty_buffer_lock_exclusive tty_buffer_unlock_exclusive

----

Internal Functions
==================

.. kernel-doc:: drivers/tty/tty_buffer.c
   :internal:
