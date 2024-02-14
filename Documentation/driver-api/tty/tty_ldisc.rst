.. SPDX-License-Identifier: GPL-2.0

===================
TTY Line Discipline
===================

.. contents:: :local:

TTY line discipline process all incoming and outgoing character from/to a tty
device. The default line discipline is :doc:`N_TTY <n_tty>`. It is also a
fallback if establishing any other discipline for a tty fails. If even N_TTY
fails, N_NULL takes over. That never fails, but also does not process any
characters -- it throws them away.

Registration
============

Line disciplines are registered with tty_register_ldisc() passing the ldisc
structure. At the point of registration the discipline must be ready to use and
it is possible it will get used before the call returns success. If the call
returns an error then it won’t get called. Do not re-use ldisc numbers as they
are part of the userspace ABI and writing over an existing ldisc will cause
demons to eat your computer. You must not re-register over the top of the line
discipline even with the same data or your computer again will be eaten by
demons. In order to remove a line discipline call tty_unregister_ldisc().

Heed this warning: the reference count field of the registered copies of the
tty_ldisc structure in the ldisc table counts the number of lines using this
discipline. The reference count of the tty_ldisc structure within a tty counts
the number of active users of the ldisc at this instant. In effect it counts
the number of threads of execution within an ldisc method (plus those about to
enter and exit although this detail matters not).

.. kernel-doc:: drivers/tty/tty_ldisc.c
   :identifiers: tty_register_ldisc tty_unregister_ldisc

Other Functions
===============

.. kernel-doc:: drivers/tty/tty_ldisc.c
   :identifiers: tty_set_ldisc tty_ldisc_flush

Line Discipline Operations Reference
====================================

.. kernel-doc:: include/linux/tty_ldisc.h
   :identifiers: tty_ldisc_ops

Driver Access
=============

Line discipline methods can call the methods of the underlying hardware driver.
These are documented as a part of struct tty_operations.

TTY Flags
=========

Line discipline methods have access to :c:member:`tty_struct.flags` field. See
:doc:`tty_struct`.

Locking
=======

Callers to the line discipline functions from the tty layer are required to
take line discipline locks. The same is true of calls from the driver side
but not yet enforced.

.. kernel-doc:: drivers/tty/tty_ldisc.c
   :identifiers: tty_ldisc_ref_wait tty_ldisc_ref tty_ldisc_deref

While these functions are slightly slower than the old code they should have
minimal impact as most receive logic uses the flip buffers and they only
need to take a reference when they push bits up through the driver.

A caution: The :c:member:`tty_ldisc_ops.open()`,
:c:member:`tty_ldisc_ops.close()` and :c:member:`tty_driver.set_ldisc()`
functions are called with the ldisc unavailable. Thus tty_ldisc_ref() will fail
in this situation if used within these functions.  Ldisc and driver code
calling its own functions must be careful in this case.

Internal Functions
==================

.. kernel-doc:: drivers/tty/tty_ldisc.c
   :internal:
