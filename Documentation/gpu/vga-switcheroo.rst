==============
VGA Switcheroo
==============

.. kernel-doc:: drivers/gpu/vga/vga_switcheroo.c
   :doc: Overview

Modes of Use
============

Manual switching and manual power control
-----------------------------------------

.. kernel-doc:: drivers/gpu/vga/vga_switcheroo.c
   :doc: Manual switching and manual power control

Driver power control
--------------------

.. kernel-doc:: drivers/gpu/vga/vga_switcheroo.c
   :doc: Driver power control

API
===

Public functions
----------------

.. kernel-doc:: drivers/gpu/vga/vga_switcheroo.c
   :export:

Public structures
-----------------

.. kernel-doc:: include/linux/vga_switcheroo.h
   :functions: vga_switcheroo_handler

.. kernel-doc:: include/linux/vga_switcheroo.h
   :functions: vga_switcheroo_client_ops

Public constants
----------------

.. kernel-doc:: include/linux/vga_switcheroo.h
   :functions: vga_switcheroo_handler_flags_t

.. kernel-doc:: include/linux/vga_switcheroo.h
   :functions: vga_switcheroo_client_id

.. kernel-doc:: include/linux/vga_switcheroo.h
   :functions: vga_switcheroo_state

Private structures
------------------

.. kernel-doc:: drivers/gpu/vga/vga_switcheroo.c
   :functions: vgasr_priv

.. kernel-doc:: drivers/gpu/vga/vga_switcheroo.c
   :functions: vga_switcheroo_client

Handlers
========

apple-gmux Handler
------------------

.. kernel-doc:: drivers/platform/x86/apple-gmux.c
   :doc: Overview

.. kernel-doc:: drivers/platform/x86/apple-gmux.c
   :doc: Interrupt

Graphics mux
~~~~~~~~~~~~

.. kernel-doc:: drivers/platform/x86/apple-gmux.c
   :doc: Graphics mux

Power control
~~~~~~~~~~~~~

.. kernel-doc:: drivers/platform/x86/apple-gmux.c
   :doc: Power control

Backlight control
~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/platform/x86/apple-gmux.c
   :doc: Backlight control

Public functions
~~~~~~~~~~~~~~~~

.. kernel-doc:: include/linux/apple-gmux.h
   :internal:
