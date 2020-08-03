.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _DMX_SET_BUFFER_SIZE:

===================
DMX_SET_BUFFER_SIZE
===================

Name
----

DMX_SET_BUFFER_SIZE


Synopsis
--------

.. c:function:: int ioctl( int fd, DMX_SET_BUFFER_SIZE, unsigned long size)
    :name: DMX_SET_BUFFER_SIZE


Arguments
---------

``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

``size``
    Unsigned long size

Description
-----------

This ioctl call is used to set the size of the circular buffer used for
filtered data. The default size is two maximum sized sections, i.e. if
this function is not called a buffer size of ``2 * 4096`` bytes will be
used.


Return Value
------------


On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
