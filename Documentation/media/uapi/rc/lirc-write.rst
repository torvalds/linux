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

The exact format of the data depends on what mode a driver uses, use
:ref:`lirc_get_features` to get the supported mode.

When in :ref:`LIRC_MODE_PULSE <lirc-mode-PULSE>` mode, the data written to
the chardev is a pulse/space sequence of integer values. Pulses and spaces
are only marked implicitly by their position. The data must start and end
with a pulse, therefore, the data must always include an uneven number of
samples. The write function must block until the data has been transmitted
by the hardware. If more data is provided than the hardware can send, the
driver returns ``EINVAL``.

Return Value
============

On success, the number of bytes read is returned. It is not an error if
this number is smaller than the number of bytes requested, or the amount
of data required for one frame.  On error, -1 is returned, and the ``errno``
variable is set appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
