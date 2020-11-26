.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.dmx

.. _DMX_ADD_PID:

===========
DMX_ADD_PID
===========

Name
----

DMX_ADD_PID

Synopsis
--------

.. c:macro:: DMX_ADD_PID

``int ioctl(fd, DMX_ADD_PID, __u16 *pid)``

Arguments
---------

``fd``
    File descriptor returned by :c:func:`open()`.

``pid``
   PID number to be filtered.

Description
-----------

This ioctl call allows to add multiple PIDs to a transport stream filter
previously set up with :ref:`DMX_SET_PES_FILTER` and output equal to
:c:type:`DMX_OUT_TSDEMUX_TAP <dmx_output>`.

Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
