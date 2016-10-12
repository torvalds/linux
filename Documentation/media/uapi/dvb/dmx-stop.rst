.. -*- coding: utf-8; mode: rst -*-

.. _DMX_STOP:

========
DMX_STOP
========

Name
----

DMX_STOP


Synopsis
--------

.. c:function:: int ioctl( int fd, DMX_STOP)
    :name: DMX_STOP


Arguments
---------

``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

Description
-----------

This ioctl call is used to stop the actual filtering operation defined
via the ioctl calls DMX_SET_FILTER or DMX_SET_PES_FILTER and
started via the DMX_START command.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
