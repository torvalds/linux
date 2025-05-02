.. SPDX-License-Identifier: GPL-2.0

==========
TTY Struct
==========

.. contents:: :local:

struct tty_struct is allocated by the TTY layer upon the first open of the TTY
device and released after the last close. The TTY layer passes this structure
to most of struct tty_operation's hooks. Members of tty_struct are documented
in `TTY Struct Reference`_ at the bottom.

Initialization
==============

.. kernel-doc:: drivers/tty/tty_io.c
   :identifiers: tty_init_termios

Name
====

.. kernel-doc:: drivers/tty/tty_io.c
   :identifiers: tty_name

Reference counting
==================

.. kernel-doc:: include/linux/tty.h
   :identifiers: tty_kref_get

.. kernel-doc:: drivers/tty/tty_io.c
   :identifiers: tty_kref_put

Install
=======

.. kernel-doc:: drivers/tty/tty_io.c
   :identifiers: tty_standard_install

Read & Write
============

.. kernel-doc:: drivers/tty/tty_io.c
   :identifiers: tty_put_char

Start & Stop
============

.. kernel-doc:: drivers/tty/tty_io.c
   :identifiers: start_tty stop_tty

Wakeup
======

.. kernel-doc:: drivers/tty/tty_io.c
   :identifiers: tty_wakeup

Hangup
======

.. kernel-doc:: drivers/tty/tty_io.c
   :identifiers: tty_hangup tty_vhangup tty_hung_up_p

Misc
====

.. kernel-doc:: drivers/tty/tty_io.c
   :identifiers: tty_do_resize

TTY Struct Flags
================

.. kernel-doc:: include/linux/tty.h
   :identifiers: tty_struct_flags

TTY Struct Reference
====================

.. kernel-doc:: include/linux/tty.h
   :identifiers: tty_struct
