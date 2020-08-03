.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _lirc_set_measure_carrier_mode:

***********************************
ioctl LIRC_SET_MEASURE_CARRIER_MODE
***********************************

Name
====

LIRC_SET_MEASURE_CARRIER_MODE - enable or disable measure mode

Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_SET_MEASURE_CARRIER_MODE, __u32 *enable )
    :name: LIRC_SET_MEASURE_CARRIER_MODE

Arguments
=========

``fd``
    File descriptor returned by open().

``enable``
    enable = 1 means enable measure mode, enable = 0 means disable measure
    mode.


Description
===========

.. _lirc-mode2-frequency:

Enable or disable measure mode. If enabled, from the next key
press on, the driver will send ``LIRC_MODE2_FREQUENCY`` packets. By
default this should be turned off.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
