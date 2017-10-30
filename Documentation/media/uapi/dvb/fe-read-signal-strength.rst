.. -*- coding: utf-8; mode: rst -*-

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
