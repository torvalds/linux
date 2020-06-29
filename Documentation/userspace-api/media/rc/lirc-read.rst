.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

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
    :name: lirc-read


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

:ref:`read() <lirc-read>` attempts to read up to ``count`` bytes from file
descriptor ``fd`` into the buffer starting at ``buf``.  If ``count`` is zero,
:ref:`read() <lirc-read>` returns zero and has no other results. If ``count``
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
