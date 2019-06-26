.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _FE_DISEQC_RESET_OVERLOAD:

******************************
ioctl FE_DISEQC_RESET_OVERLOAD
******************************

Name
====

FE_DISEQC_RESET_OVERLOAD - Restores the power to the antenna subsystem, if it was powered off due - to power overload.


Synopsis
========

.. c:function:: int ioctl( int fd, FE_DISEQC_RESET_OVERLOAD, NULL )
    :name: FE_DISEQC_RESET_OVERLOAD


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

Description
===========

If the bus has been automatically powered off due to power overload,
this ioctl call restores the power to the bus. The call requires
read/write access to the device. This call has no effect if the device
is manually powered off. Not all Digital TV adapters support this ioctl.


Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
