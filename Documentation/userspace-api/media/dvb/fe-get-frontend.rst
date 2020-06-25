.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _FE_GET_FRONTEND:

***************
FE_GET_FRONTEND
***************

Name
====

FE_GET_FRONTEND

.. attention:: This ioctl is deprecated.


Synopsis
========

.. c:function:: int ioctl(int fd, FE_GET_FRONTEND, struct dvb_frontend_parameters *p)
    :name: FE_GET_FRONTEND


Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <dvb-fe-open>`.


``p``
    Points to parameters for tuning operation.


Description
===========

This ioctl call queries the currently effective frontend parameters. For
this command, read-only access to the device is sufficient.


Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  Maximum supported symbol rate reached.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
