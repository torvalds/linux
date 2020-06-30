.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

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
previously set up with :ref:`DMX_SET_PES_FILTER` and output equal to
:c:type:`DMX_OUT_TSDEMUX_TAP <dmx_output>`.


Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
