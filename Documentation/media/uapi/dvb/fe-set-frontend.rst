.. -*- coding: utf-8; mode: rst -*-

.. _FE_SET_FRONTEND:

***************
FE_SET_FRONTEND
***************

.. attention:: This ioctl is deprecated.

Name
====

FE_SET_FRONTEND


Synopsis
========

.. c:function:: int ioctl(int fd, FE_SET_FRONTEND, struct dvb_frontend_parameters *p)
    :name: FE_SET_FRONTEND


Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <dvb-fe-open>`.

``p``
    Points to parameters for tuning operation.


Description
===========

This ioctl call starts a tuning operation using specified parameters.
The result of this call will be successful if the parameters were valid
and the tuning could be initiated. The result of the tuning operation in
itself, however, will arrive asynchronously as an event (see
documentation for :ref:`FE_GET_EVENT` and
FrontendEvent.) If a new :ref:`FE_SET_FRONTEND`
operation is initiated before the previous one was completed, the
previous operation will be aborted in favor of the new one. This command
requires read/write access to the device.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  Maximum supported symbol rate reached.
