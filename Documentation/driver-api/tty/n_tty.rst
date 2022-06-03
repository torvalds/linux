.. SPDX-License-Identifier: GPL-2.0

=====
N_TTY
=====

.. contents:: :local:

The default (and fallback) :doc:`TTY line discipline <tty_ldisc>`. It tries to
handle characters as per POSIX.

External Functions
==================

.. kernel-doc:: drivers/tty/n_tty.c
   :export:

Internal Functions
==================

.. kernel-doc:: drivers/tty/n_tty.c
   :internal:
