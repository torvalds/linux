.. -*- coding: utf-8; mode: rst -*-

.. _FE_SET_FRONTEND_TUNE_MODE:

*******************************
ioctl FE_SET_FRONTEND_TUNE_MODE
*******************************

Name
====

FE_SET_FRONTEND_TUNE_MODE - Allow setting tuner mode flags to the frontend.


Synopsis
========

.. c:function:: int ioctl( int fd, int request, unsigned int flags )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``request``
    FE_SET_FRONTEND_TUNE_MODE

``flags``
    Valid flags:

    -  0 - normal tune mode

    -  FE_TUNE_MODE_ONESHOT - When set, this flag will disable any
       zigzagging or other "normal" tuning behaviour. Additionally,
       there will be no automatic monitoring of the lock status, and
       hence no frontend events will be generated. If a frontend device
       is closed, this flag will be automatically turned off when the
       device is reopened read-write.


Description
===========

Allow setting tuner mode flags to the frontend, between 0 (normal) or
FE_TUNE_MODE_ONESHOT mode


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
