.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.dmx

.. _DMX_REMOVE_PID:

==============
DMX_REMOVE_PID
==============

Name
----

DMX_REMOVE_PID

Synopsis
--------

.. c:macro:: DMX_REMOVE_PID

``int ioctl(fd, DMX_REMOVE_PID, __u16 *pid)``

Arguments
---------

``fd``
    File descriptor returned by :c:func:`open()`.

``pid``
    PID of the PES filter to be removed.

Description
-----------

This ioctl call allows to remove a PID when multiple PIDs are set on a
transport stream filter, e. g. a filter previously set up with output
equal to :c:type:`DMX_OUT_TSDEMUX_TAP <dmx_output>`, created via either
:ref:`DMX_SET_PES_FILTER` or :ref:`DMX_ADD_PID`.

Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
