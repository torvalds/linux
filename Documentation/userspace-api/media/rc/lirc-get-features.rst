.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: RC

.. _lirc_get_features:

***********************
ioctl LIRC_GET_FEATURES
***********************

Name
====

LIRC_GET_FEATURES - Get the underlying hardware device's features

Synopsis
========

.. c:macro:: LIRC_GET_FEATURES

``int ioctl(int fd, LIRC_GET_FEATURES, __u32 *features)``

Arguments
=========

``fd``
    File descriptor returned by open().

``features``
    Bitmask with the LIRC features.

Description
===========

Get the underlying hardware device's features. If a driver does not
announce support of certain features, calling of the corresponding ioctls
is undefined.

LIRC features
=============

.. _LIRC-CAN-REC-RAW:

``LIRC_CAN_REC_RAW``

    Unused. Kept just to avoid breaking uAPI.

.. _LIRC-CAN-REC-PULSE:

``LIRC_CAN_REC_PULSE``

    Unused. Kept just to avoid breaking uAPI.
    :ref:`LIRC_MODE_PULSE <lirc-mode-pulse>` can only be used for transmitting.

.. _LIRC-CAN-REC-MODE2:

``LIRC_CAN_REC_MODE2``

    This is raw IR driver for receiving. This means that
    :ref:`LIRC_MODE_MODE2 <lirc-mode-MODE2>` is used. This also implies
    that :ref:`LIRC_MODE_SCANCODE <lirc-mode-SCANCODE>` is also supported,
    as long as the kernel is recent enough. Use the
    :ref:`lirc_set_rec_mode` to switch modes.

.. _LIRC-CAN-REC-LIRCCODE:

``LIRC_CAN_REC_LIRCCODE``

    Unused. Kept just to avoid breaking uAPI.

.. _LIRC-CAN-REC-SCANCODE:

``LIRC_CAN_REC_SCANCODE``

    This is a scancode driver for receiving. This means that
    :ref:`LIRC_MODE_SCANCODE <lirc-mode-SCANCODE>` is used.

.. _LIRC-CAN-SET-SEND-CARRIER:

``LIRC_CAN_SET_SEND_CARRIER``

    The driver supports changing the modulation frequency via
    :ref:`ioctl LIRC_SET_SEND_CARRIER <LIRC_SET_SEND_CARRIER>`.

.. _LIRC-CAN-SET-SEND-DUTY-CYCLE:

``LIRC_CAN_SET_SEND_DUTY_CYCLE``

    The driver supports changing the duty cycle using
    :ref:`ioctl LIRC_SET_SEND_DUTY_CYCLE <LIRC_SET_SEND_DUTY_CYCLE>`.

.. _LIRC-CAN-SET-TRANSMITTER-MASK:

``LIRC_CAN_SET_TRANSMITTER_MASK``

    The driver supports changing the active transmitter(s) using
    :ref:`ioctl LIRC_SET_TRANSMITTER_MASK <LIRC_SET_TRANSMITTER_MASK>`.

.. _LIRC-CAN-SET-REC-CARRIER:

``LIRC_CAN_SET_REC_CARRIER``

    The driver supports setting the receive carrier frequency using
    :ref:`ioctl LIRC_SET_REC_CARRIER <LIRC_SET_REC_CARRIER>`.

.. _LIRC-CAN-SET-REC-DUTY-CYCLE-RANGE:

``LIRC_CAN_SET_REC_DUTY_CYCLE_RANGE``

    Unused. Kept just to avoid breaking uAPI.

.. _LIRC-CAN-SET-REC-CARRIER-RANGE:

``LIRC_CAN_SET_REC_CARRIER_RANGE``

    The driver supports
    :ref:`ioctl LIRC_SET_REC_CARRIER_RANGE <LIRC_SET_REC_CARRIER_RANGE>`.

.. _LIRC-CAN-GET-REC-RESOLUTION:

``LIRC_CAN_GET_REC_RESOLUTION``

    The driver supports
    :ref:`ioctl LIRC_GET_REC_RESOLUTION <LIRC_GET_REC_RESOLUTION>`.

.. _LIRC-CAN-SET-REC-TIMEOUT:

``LIRC_CAN_SET_REC_TIMEOUT``

    The driver supports
    :ref:`ioctl LIRC_SET_REC_TIMEOUT <LIRC_SET_REC_TIMEOUT>`.

.. _LIRC-CAN-SET-REC-FILTER:

``LIRC_CAN_SET_REC_FILTER``

    Unused. Kept just to avoid breaking uAPI.

.. _LIRC-CAN-MEASURE-CARRIER:

``LIRC_CAN_MEASURE_CARRIER``

    The driver supports measuring of the modulation frequency using
    :ref:`ioctl LIRC_SET_MEASURE_CARRIER_MODE <LIRC_SET_MEASURE_CARRIER_MODE>`.

.. _LIRC-CAN-USE-WIDEBAND-RECEIVER:

``LIRC_CAN_USE_WIDEBAND_RECEIVER``

    The driver supports learning mode using
    :ref:`ioctl LIRC_SET_WIDEBAND_RECEIVER <LIRC_SET_WIDEBAND_RECEIVER>`.

.. _LIRC-CAN-NOTIFY-DECODE:

``LIRC_CAN_NOTIFY_DECODE``

    Unused. Kept just to avoid breaking uAPI.

.. _LIRC-CAN-SEND-RAW:

``LIRC_CAN_SEND_RAW``

    Unused. Kept just to avoid breaking uAPI.

.. _LIRC-CAN-SEND-PULSE:

``LIRC_CAN_SEND_PULSE``

    The driver supports sending (also called as IR blasting or IR TX) using
    :ref:`LIRC_MODE_PULSE <lirc-mode-pulse>`. This implies that
    :ref:`LIRC_MODE_SCANCODE <lirc-mode-SCANCODE>` is also supported for
    transmit, as long as the kernel is recent enough. Use the
    :ref:`lirc_set_send_mode` to switch modes.

.. _LIRC-CAN-SEND-MODE2:

``LIRC_CAN_SEND_MODE2``

    Unused. Kept just to avoid breaking uAPI.
    :ref:`LIRC_MODE_MODE2 <lirc-mode-mode2>` can only be used for receiving.

.. _LIRC-CAN-SEND-LIRCCODE:

``LIRC_CAN_SEND_LIRCCODE``

    Unused. Kept just to avoid breaking uAPI.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
