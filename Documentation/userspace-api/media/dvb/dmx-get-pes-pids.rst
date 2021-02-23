.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.dmx

.. _DMX_GET_PES_PIDS:

================
DMX_GET_PES_PIDS
================

Name
----

DMX_GET_PES_PIDS

Synopsis
--------

.. c:macro:: DMX_GET_PES_PIDS

``int ioctl(fd, DMX_GET_PES_PIDS, __u16 pids[5])``

Arguments
---------

``fd``
    File descriptor returned by :c:func:`open()`.

``pids``
    Array used to store 5 Program IDs.

Description
-----------

This ioctl allows to query a DVB device to return the first PID used
by audio, video, textext, subtitle and PCR programs on a given service.
They're stored as:

=======================	========	=======================================
PID  element		position	content
=======================	========	=======================================
pids[DMX_PES_AUDIO]	0		first audio PID
pids[DMX_PES_VIDEO]	1		first video PID
pids[DMX_PES_TELETEXT]	2		first teletext PID
pids[DMX_PES_SUBTITLE]	3		first subtitle PID
pids[DMX_PES_PCR]	4		first Program Clock Reference PID
=======================	========	=======================================

.. note::

	A value equal to 0xffff means that the PID was not filled by the
	Kernel.

Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
