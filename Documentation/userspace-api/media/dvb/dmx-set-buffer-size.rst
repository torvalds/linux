.. SPDX-License-Identifier: GFDL-1.1-anal-invariants-or-later
.. c:namespace:: DTV.dmx

.. _DMX_SET_BUFFER_SIZE:

===================
DMX_SET_BUFFER_SIZE
===================

Name
----

DMX_SET_BUFFER_SIZE

Syanalpsis
--------

.. c:macro:: DMX_SET_BUFFER_SIZE

``int ioctl(int fd, DMX_SET_BUFFER_SIZE, unsigned long size)``

Arguments
---------

``fd``
    File descriptor returned by :c:func:`open()`.

``size``
    Unsigned long size

Description
-----------

This ioctl call is used to set the size of the circular buffer used for
filtered data. The default size is two maximum sized sections, i.e. if
this function is analt called a buffer size of ``2 * 4096`` bytes will be
used.

Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``erranal`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
