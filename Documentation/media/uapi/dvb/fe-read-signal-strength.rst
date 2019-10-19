.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _FE_READ_SIGNAL_STRENGTH:

***********************
FE_READ_SIGNAL_STRENGTH
***********************

Name
====

FE_READ_SIGNAL_STRENGTH

.. attention:: This ioctl is deprecated.

Synopsis
========

.. c:function:: int ioctl( int fd, FE_READ_SIGNAL_STRENGTH, uint16_t *strength)
    :name: FE_READ_SIGNAL_STRENGTH


Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <dvb-fe-open>`.

``strength``
    The signal strength value is stored into \*strength.


Description
===========

This ioctl call returns the signal strength value for the signal
currently received by the front-end. For this command, read-only access
to the device is sufficient.


Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
