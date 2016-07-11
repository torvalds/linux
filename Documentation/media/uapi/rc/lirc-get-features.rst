.. -*- coding: utf-8; mode: rst -*-

.. _lirc_get_features:

***********************
ioctl LIRC_GET_FEATURES
***********************

Name
====

LIRC_GET_FEATURES - Get the underlying hardware device's features

Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, __u32 *features)

Arguments
=========

``fd``
    File descriptor returned by open().

``request``
    LIRC_GET_FEATURES

``features``
    Bitmask with the LIRC features.


Description
===========


Get the underlying hardware device's features. If a driver does not
announce support of certain features, calling of the corresponding ioctls
is undefined.

LIRC features
=============

.. _LIRC_CAN_REC_RAW:

``LIRC_CAN_REC_RAW``
    The driver is capable of receiving using
    :ref:`LIRC_MODE_RAW.`

.. _LIRC_CAN_REC_PULSE:

``LIRC_CAN_REC_PULSE``
    The driver is capable of receiving using
    :ref:`LIRC_MODE_PULSE.`

.. _LIRC_CAN_REC_MODE2:

``LIRC_CAN_REC_MODE2``
    The driver is capable of receiving using
    :ref:`LIRC_MODE_MODE2.`

.. _LIRC_CAN_REC_LIRCCODE:

``LIRC_CAN_REC_LIRCCODE``
    The driver is capable of receiving using
    :ref:`LIRC_MODE_LIRCCODE.`

.. _LIRC_CAN_SET_SEND_CARRIER:

``LIRC_CAN_SET_SEND_CARRIER``
    The driver supports changing the modulation frequency via
    :ref:`LIRC_SET_SEND_CARRIER.`

.. _LIRC_CAN_SET_SEND_DUTY_CYCLE:

``LIRC_CAN_SET_SEND_DUTY_CYCLE``
    The driver supports changing the duty cycle using
    :ref:`LIRC_SET_SEND_DUTY_CYCLE`.

.. _LIRC_CAN_SET_TRANSMITTER_MASK:

``LIRC_CAN_SET_TRANSMITTER_MASK``
    The driver supports changing the active transmitter(s) using
    :ref:`LIRC_SET_TRANSMITTER_MASK.`

.. _LIRC_CAN_SET_REC_CARRIER:

``LIRC_CAN_SET_REC_CARRIER``
    The driver supports setting the receive carrier frequency using
    :ref:`LIRC_SET_REC_CARRIER.`

.. _LIRC_CAN_SET_REC_DUTY_CYCLE_RANGE:

``LIRC_CAN_SET_REC_DUTY_CYCLE_RANGE``
    The driver supports
    :ref:`LIRC_SET_REC_DUTY_CYCLE_RANGE.`

.. _LIRC_CAN_SET_REC_CARRIER_RANGE:

``LIRC_CAN_SET_REC_CARRIER_RANGE``
    The driver supports
    :ref:`LIRC_SET_REC_CARRIER_RANGE.`

.. _LIRC_CAN_GET_REC_RESOLUTION:

``LIRC_CAN_GET_REC_RESOLUTION``
    The driver supports
    :ref:`LIRC_GET_REC_RESOLUTION.`

.. _LIRC_CAN_SET_REC_TIMEOUT:

``LIRC_CAN_SET_REC_TIMEOUT``
    The driver supports
    :ref:`LIRC_SET_REC_TIMEOUT.`

.. _LIRC_CAN_SET_REC_FILTER:

``LIRC_CAN_SET_REC_FILTER``
    The driver supports
    :ref:`LIRC_SET_REC_FILTER.`

.. _LIRC_CAN_MEASURE_CARRIER:

``LIRC_CAN_MEASURE_CARRIER``
    The driver supports measuring of the modulation frequency using
    :ref:`LIRC_SET_MEASURE_CARRIER_MODE`.

.. _LIRC_CAN_USE_WIDEBAND_RECEIVER:

``LIRC_CAN_USE_WIDEBAND_RECEIVER``
    The driver supports learning mode using
    :ref:`LIRC_SET_WIDEBAND_RECEIVER.`

.. _LIRC_CAN_NOTIFY_DECODE:

``LIRC_CAN_NOTIFY_DECODE``
    The driver supports
    :ref:`LIRC_NOTIFY_DECODE.`

.. _LIRC_CAN_SEND_RAW:

``LIRC_CAN_SEND_RAW``
    The driver supports sending using
    :ref:`LIRC_MODE_RAW.`

.. _LIRC_CAN_SEND_PULSE:

``LIRC_CAN_SEND_PULSE``
    The driver supports sending using
    :ref:`LIRC_MODE_PULSE.`

.. _LIRC_CAN_SEND_MODE2:

``LIRC_CAN_SEND_MODE2``
    The driver supports sending using
    :ref:`LIRC_MODE_MODE2.`

.. _LIRC_CAN_SEND_LIRCCODE:

``LIRC_CAN_SEND_LIRCCODE``
    The driver supports sending codes (also called as IR blasting or IR TX).


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
