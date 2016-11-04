.. -*- coding: utf-8; mode: rst -*-

.. _DMX_REMOVE_PID:

==============
DMX_REMOVE_PID
==============

Name
----

DMX_REMOVE_PID


Synopsis
--------

.. c:function:: int ioctl(fd, DMX_REMOVE_PID, __u16 *pid)
    :name: DMX_REMOVE_PID


Arguments
---------

``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

``pid``
    PID of the PES filter to be removed.


Description
-----------

This ioctl call allows to remove a PID when multiple PIDs are set on a
transport stream filter, e. g. a filter previously set up with output
equal to DMX_OUT_TSDEMUX_TAP, created via either
DMX_SET_PES_FILTER or DMX_ADD_PID.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
