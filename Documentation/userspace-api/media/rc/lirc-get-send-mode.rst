.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.1-no-invariants-or-later
.. c:namespace:: RC

.. _lirc_get_send_mode:
.. _lirc_set_send_mode:

************************************************
ioctls LIRC_GET_SEND_MODE and LIRC_SET_SEND_MODE
************************************************

Name
====

LIRC_GET_SEND_MODE/LIRC_SET_SEND_MODE - Get/set current transmit mode.

Synopsis
========

.. c:macro:: LIRC_GET_SEND_MODE

``int ioctl(int fd, LIRC_GET_SEND_MODE, __u32 *mode)``

.. c:macro:: LIRC_SET_SEND_MODE

``int ioctl(int fd, LIRC_SET_SEND_MODE, __u32 *mode)``

Arguments
=========

``fd``
    File descriptor returned by open().

``mode``
    The mode used for transmitting.

Description
===========

Get/set current transmit mode.

Only :ref:`LIRC_MODE_PULSE <lirc-mode-pulse>` and
:ref:`LIRC_MODE_SCANCODE <lirc-mode-scancode>` are supported by for IR send,
depending on the driver. Use :ref:`lirc_get_features` to find out which
modes the driver supports.

Return Value
============

.. tabularcolumns:: |p{2.5cm}|p{15.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  .. row 1

       -  ``ENODEV``

       -  Device not available.

    -  .. row 2

       -  ``ENOTTY``

       -  Device does not support transmitting.

    -  .. row 3

       -  ``EINVAL``

       -  Invalid mode or invalid mode for this device.
