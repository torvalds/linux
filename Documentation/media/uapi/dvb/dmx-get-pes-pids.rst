.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _DMX_GET_PES_PIDS:

================
DMX_GET_PES_PIDS
================

Name
----

DMX_GET_PES_PIDS


Synopsis
--------

.. c:function:: int ioctl(fd, DMX_GET_PES_PIDS, __u16 pids[5])
    :name: DMX_GET_PES_PIDS

Arguments
---------

``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

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
