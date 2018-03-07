.. -*- coding: utf-8; mode: rst -*-

.. _lirc-write:

************
LIRC write()
************

Name
====

lirc-write - Write to a LIRC device


Synopsis
========

.. code-block:: c

    #include <unistd.h>


.. c:function:: ssize_t write( int fd, void *buf, size_t count )
    :name: lirc-write

Arguments
=========

``fd``
    File descriptor returned by ``open()``.

``buf``
    Buffer with data to be written

``count``
    Number of bytes at the buffer

Description
===========

:ref:`write() <lirc-write>` writes up to ``count`` bytes to the device
referenced by the file descriptor ``fd`` from the buffer starting at
``buf``.

The exact format of the data depends on what mode a driver is in, use
:ref:`lirc_get_features` to get the supported modes and use
:ref:`lirc_set_send_mode` set the mode.

When in :ref:`LIRC_MODE_PULSE <lirc-mode-PULSE>` mode, the data written to
the chardev is a pulse/space sequence of integer values. Pulses and spaces
are only marked implicitly by their position. The data must start and end
with a pulse, therefore, the data must always include an uneven number of
samples. The write function blocks until the data has been transmitted
by the hardware. If more data is provided than the hardware can send, the
driver returns ``EINVAL``.

When in :ref:`LIRC_MODE_SCANCODE <lirc-mode-scancode>` mode, one
``struct lirc_scancode`` must be written to the chardev at a time, else
``EINVAL`` is returned. Set the desired scancode in the ``scancode`` member,
and the protocol in the :c:type:`rc_proto`: member. All other members must be
set to 0, else ``EINVAL`` is returned. If there is no protocol encoder
for the protocol or the scancode is not valid for the specified protocol,
``EINVAL`` is returned. The write function blocks until the scancode
is transmitted by the hardware.


Return Value
============

On success, the number of bytes written is returned. It is not an error if
this number is smaller than the number of bytes requested, or the amount
of data required for one frame.  On error, -1 is returned, and the ``errno``
variable is set appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
