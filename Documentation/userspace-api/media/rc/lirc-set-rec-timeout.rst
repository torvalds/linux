.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _lirc_set_rec_timeout:
.. _lirc_get_rec_timeout:

***************************************************
ioctl LIRC_GET_REC_TIMEOUT and LIRC_SET_REC_TIMEOUT
***************************************************

Name
====

LIRC_GET_REC_TIMEOUT/LIRC_SET_REC_TIMEOUT - Get/set the integer value for IR inactivity timeout.

Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_GET_REC_TIMEOUT, __u32 *timeout )
    :name: LIRC_GET_REC_TIMEOUT

.. c:function:: int ioctl( int fd, LIRC_SET_REC_TIMEOUT, __u32 *timeout )
    :name: LIRC_SET_REC_TIMEOUT

Arguments
=========

``fd``
    File descriptor returned by open().

``timeout``
    Timeout, in microseconds.


Description
===========

Get and set the integer value for IR inactivity timeout.

If supported by the hardware, setting it to 0  disables all hardware timeouts
and data should be reported as soon as possible. If the exact value
cannot be set, then the next possible value _greater_ than the
given value should be set.

.. note::

   The range of supported timeout is given by :ref:`LIRC_GET_MIN_TIMEOUT`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
