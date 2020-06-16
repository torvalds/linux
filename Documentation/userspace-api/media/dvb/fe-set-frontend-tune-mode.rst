.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _FE_SET_FRONTEND_TUNE_MODE:

*******************************
ioctl FE_SET_FRONTEND_TUNE_MODE
*******************************

Name
====

FE_SET_FRONTEND_TUNE_MODE - Allow setting tuner mode flags to the frontend.


Synopsis
========

.. c:function:: int ioctl( int fd, FE_SET_FRONTEND_TUNE_MODE, unsigned int flags )
    :name: FE_SET_FRONTEND_TUNE_MODE


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``flags``
    Valid flags:

    -  0 - normal tune mode

    -  ``FE_TUNE_MODE_ONESHOT`` - When set, this flag will disable any
       zigzagging or other "normal" tuning behaviour. Additionally,
       there will be no automatic monitoring of the lock status, and
       hence no frontend events will be generated. If a frontend device
       is closed, this flag will be automatically turned off when the
       device is reopened read-write.


Description
===========

Allow setting tuner mode flags to the frontend, between 0 (normal) or
``FE_TUNE_MODE_ONESHOT`` mode


Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
