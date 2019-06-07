.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

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
equal to :c:type:`DMX_OUT_TSDEMUX_TAP <dmx_output>`, created via either
:ref:`DMX_SET_PES_FILTER` or :ref:`DMX_ADD_PID`.


Return Value
------------

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
