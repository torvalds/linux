.. SPDX-License-Identifier: GPL-2.0

========
TTY Port
========

.. contents:: :local:

The TTY drivers are advised to use struct tty_port helpers as much as possible.
If the drivers implement :c:member:`tty_port.ops.activate()` and
:c:member:`tty_port.ops.shutdown()`, they can use tty_port_open(),
tty_port_close(), and tty_port_hangup() in respective
:c:member:`tty_struct.ops` hooks.

The reference and details are contained in the `TTY Port Reference`_ and `TTY
Port Operations Reference`_ sections at the bottom.

TTY Port Functions
==================

Init & Destroy
--------------

.. kernel-doc::  drivers/tty/tty_port.c
   :identifiers: tty_port_init tty_port_destroy
        tty_port_get tty_port_put

Open/Close/Hangup Helpers
-------------------------

.. kernel-doc::  drivers/tty/tty_port.c
   :identifiers: tty_port_install tty_port_open tty_port_block_til_ready
        tty_port_close tty_port_close_start tty_port_close_end tty_port_hangup
        tty_port_shutdown

TTY Refcounting
---------------

.. kernel-doc::  drivers/tty/tty_port.c
   :identifiers: tty_port_tty_get tty_port_tty_set

TTY Helpers
-----------

.. kernel-doc::  drivers/tty/tty_port.c
   :identifiers: tty_port_tty_hangup tty_port_tty_wakeup


Modem Signals
-------------

.. kernel-doc::  drivers/tty/tty_port.c
   :identifiers: tty_port_carrier_raised tty_port_raise_dtr_rts
        tty_port_lower_dtr_rts

----

TTY Port Reference
==================

.. kernel-doc:: include/linux/tty_port.h
   :identifiers: tty_port

----

TTY Port Operations Reference
=============================

.. kernel-doc:: include/linux/tty_port.h
   :identifiers: tty_port_operations
