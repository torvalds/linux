.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _lirc_set_send_carrier:

***************************
ioctl LIRC_SET_SEND_CARRIER
***************************

Name
====

LIRC_SET_SEND_CARRIER - Set send carrier used to modulate IR TX.


Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_SET_SEND_CARRIER, __u32 *frequency )
    :name: LIRC_SET_SEND_CARRIER

Arguments
=========

``fd``
    File descriptor returned by open().

``frequency``
    Frequency of the carrier to be modulated, in Hz.

Description
===========

Set send carrier used to modulate IR PWM pulses and spaces.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
