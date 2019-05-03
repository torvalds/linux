.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _FE_READ_UNCORRECTED_BLOCKS:

**************************
FE_READ_UNCORRECTED_BLOCKS
**************************

Name
====

FE_READ_UNCORRECTED_BLOCKS

.. attention:: This ioctl is deprecated.

Synopsis
========

.. c:function:: int ioctl( int fd, FE_READ_UNCORRECTED_BLOCKS, uint32_t *ublocks)
    :name: FE_READ_UNCORRECTED_BLOCKS


Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <dvb-fe-open>`.

``ublocks``
    The total number of uncorrected blocks seen by the driver so far.


Description
===========

This ioctl call returns the number of uncorrected blocks detected by the
device driver during its lifetime. For meaningful measurements, the
increment in block count during a specific time interval should be
calculated. For this command, read-only access to the device is
sufficient.


Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
