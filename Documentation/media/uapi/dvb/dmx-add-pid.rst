.. -*- coding: utf-8; mode: rst -*-

.. _DMX_ADD_PID:

===========
DMX_ADD_PID
===========

Name
----

DMX_ADD_PID


Synopsis
--------

.. c:function:: int ioctl(fd, DMX_ADD_PID, __u16 *pid)
    :name: DMX_ADD_PID


Arguments
---------

``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

``pid``
   PID number to be filtered.


Description
-----------

This ioctl call allows to add multiple PIDs to a transport stream filter
previously set up with DMX_SET_PES_FILTER and output equal to
DMX_OUT_TSDEMUX_TAP.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
