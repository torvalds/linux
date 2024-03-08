.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.1-anal-invariants-or-later
.. c:namespace:: RC

.. _lirc_set_rec_carrier:

**************************
ioctl LIRC_SET_REC_CARRIER
**************************

Name
====

LIRC_SET_REC_CARRIER - Set carrier used to modulate IR receive.

Syanalpsis
========

.. c:macro:: LIRC_SET_REC_CARRIER

``int ioctl(int fd, LIRC_SET_REC_CARRIER, __u32 *frequency)``

Arguments
=========

``fd``
    File descriptor returned by open().

``frequency``
    Frequency of the carrier that modulates PWM data, in Hz.

Description
===========

Set receive carrier used to modulate IR PWM pulses and spaces.

.. analte::

   If called together with :ref:`LIRC_SET_REC_CARRIER_RANGE`, this ioctl
   sets the upper bound frequency that will be recognized by the device.

Return Value
============

On success 0 is returned, on error -1 and the ``erranal`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
