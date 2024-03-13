.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.1-no-invariants-or-later
.. c:namespace:: RC

.. _lirc-read:

***********
LIRC read()
***********

Name
====

lirc-read - Read from a LIRC device

Synopsis
========

.. code-block:: c

    #include <unistd.h>

.. c:function:: ssize_t read( int fd, void *buf, size_t count )

Arguments
=========

``fd``
    File descriptor returned by ``open()``.

``buf``
   Buffer to be filled

``count``
   Max number of bytes to read

Description
===========

:c:func:`read()` attempts to read up to ``count`` bytes from file
descriptor ``fd`` into the buffer starting at ``buf``.  If ``count`` is zero,
:c:func:`read()` returns zero and has no other results. If ``count``
is greater than ``SSIZE_MAX``, the result is unspecified.

The exact format of the data depends on what :ref:`lirc_modes` a driver
uses. Use :ref:`lirc_get_features` to get the supported mode, and use
:ref:`lirc_set_rec_mode` set the current active mode.

The mode :ref:`LIRC_MODE_MODE2 <lirc-mode-mode2>` is for raw IR,
in which packets containing an unsigned int value describing an IR signal are
read from the chardev.

Alternatively, :ref:`LIRC_MODE_SCANCODE <lirc-mode-scancode>` can be available,
in this mode scancodes which are either decoded by software decoders, or
by hardware decoders. The :c:type:`rc_proto` member is set to the
:ref:`IR protocol <Remote_controllers_Protocols>`
used for transmission, and ``scancode`` to the decoded scancode,
and the ``keycode`` set to the keycode or ``KEY_RESERVED``.

Return Value
============

On success, the number of bytes read is returned. It is not an error if
this number is smaller than the number of bytes requested, or the amount
of data required for one frame.  On error, -1 is returned, and the ``errno``
variable is set appropriately.
