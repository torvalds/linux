.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _DMX_SET_FILTER:

==============
DMX_SET_FILTER
==============

Name
----

DMX_SET_FILTER


Synopsis
--------

.. c:function:: int ioctl( int fd, DMX_SET_FILTER, struct dmx_sct_filter_params *params)
    :name: DMX_SET_FILTER

Arguments
---------

``fd``
    File descriptor returned by :c:func:`open() <dvb-dmx-open>`.

``params``

    Pointer to structure containing filter parameters.


Description
-----------

This ioctl call sets up a filter according to the filter and mask
parameters provided. A timeout may be defined stating number of seconds
to wait for a section to be loaded. A value of 0 means that no timeout
should be applied. Finally there is a flag field where it is possible to
state whether a section should be CRC-checked, whether the filter should
be a ”one-shot” filter, i.e. if the filtering operation should be
stopped after the first section is received, and whether the filtering
operation should be started immediately (without waiting for a
:ref:`DMX_START` ioctl call). If a filter was previously set-up, this
filter will be canceled, and the receive buffer will be flushed.


Return Value
------------


On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
