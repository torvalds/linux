.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _lirc_set_transmitter_mask:

*******************************
ioctl LIRC_SET_TRANSMITTER_MASK
*******************************

Name
====

LIRC_SET_TRANSMITTER_MASK - Enables send codes on a given set of transmitters

Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_SET_TRANSMITTER_MASK, __u32 *mask )
    :name: LIRC_SET_TRANSMITTER_MASK

Arguments
=========

``fd``
    File descriptor returned by open().

``mask``
    Mask with channels to enable tx. Channel 0 is the least significant bit.


Description
===========

Some IR TX devices have multiple output channels, in such case,
:ref:`LIRC_CAN_SET_TRANSMITTER_MASK <LIRC-CAN-SET-TRANSMITTER-MASK>` is
returned via :ref:`LIRC_GET_FEATURES` and this ioctl sets what channels will
send IR codes.

This ioctl enables the given set of transmitters. The first transmitter is
encoded by the least significant bit and so on.

When an invalid bit mask is given, i.e. a bit is set, even though the device
does not have so many transitters, then this ioctl returns the number of
available transitters and does nothing otherwise.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
