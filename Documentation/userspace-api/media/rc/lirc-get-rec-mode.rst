.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: RC

.. _lirc_get_rec_mode:
.. _lirc_set_rec_mode:

**********************************************
ioctls LIRC_GET_REC_MODE and LIRC_SET_REC_MODE
**********************************************

Name
====

LIRC_GET_REC_MODE/LIRC_SET_REC_MODE - Get/set current receive mode.

Synopsis
========

.. c:macro:: LIRC_GET_REC_MODE

``int ioctl(int fd, LIRC_GET_REC_MODE, __u32 *mode)``

.. c:macro:: LIRC_SET_REC_MODE

``int ioctl(int fd, LIRC_SET_REC_MODE, __u32 *mode)``

Arguments
=========

``fd``
    File descriptor returned by open().

``mode``
    Mode used for receive.

Description
===========

Get and set the current receive mode. Only
:ref:`LIRC_MODE_MODE2 <lirc-mode-mode2>` and
:ref:`LIRC_MODE_SCANCODE <lirc-mode-scancode>` are supported.
Use :ref:`lirc_get_features` to find out which modes the driver supports.

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

       -  Device does not support receiving.

    -  .. row 3

       -  ``EINVAL``

       -  Invalid mode or invalid mode for this device.
