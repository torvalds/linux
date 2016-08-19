.. -*- coding: utf-8; mode: rst -*-

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
    :name lirc-read


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

The lircd userspace daemon reads raw IR data from the LIRC chardev. The
exact format of the data depends on what modes a driver supports, and
what mode has been selected. lircd obtains supported modes and sets the
active mode via the ioctl interface, detailed at :ref:`lirc_func`.
The generally preferred mode for receive is
:ref:`LIRC_MODE_MODE2 <lirc-mode-mode2>`, in which packets containing an
int value describing an IR signal are read from the chardev.

See also
`http://www.lirc.org/html/technical.html <http://www.lirc.org/html/technical.html>`__
for more info.

Return Value
============

On success, the number of bytes read is returned. It is not an error if
this number is smaller than the number of bytes requested, or the amount
of data required for one frame.  On error, -1 is returned, and the ``errno``
variable is set appropriately.
