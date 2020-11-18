.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

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

.. c:function:: int ioctl( int fd, LIRC_GET_SEND_MODE, __u32 *mode )
    :name: LIRC_GET_SEND_MODE

.. c:function:: int ioctl( int fd, LIRC_SET_SEND_MODE, __u32 *mode )
    :name: LIRC_SET_SEND_MODE

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
