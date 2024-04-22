.. SPDX-License-Identifier: GPL-2.0

=======
Console
=======

.. contents:: :local:

Struct Console
==============

.. kernel-doc:: include/linux/console.h
   :identifiers: console cons_flags

Internals
---------

.. kernel-doc:: include/linux/console.h
   :identifiers: nbcon_state nbcon_prio nbcon_context nbcon_write_context

Struct Consw
============

.. kernel-doc:: include/linux/console.h
   :identifiers: consw

Console functions
=================

.. kernel-doc:: include/linux/console.h
   :identifiers: console_srcu_read_flags console_srcu_write_flags
        console_is_registered for_each_console_srcu for_each_console

.. kernel-doc:: drivers/tty/vt/selection.c
   :export:
.. kernel-doc:: drivers/tty/vt/vt.c
   :export:

Internals
---------

.. kernel-doc:: drivers/tty/vt/selection.c
   :internal:
.. kernel-doc:: drivers/tty/vt/vt.c
   :internal:
