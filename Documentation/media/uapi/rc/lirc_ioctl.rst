.. -*- coding: utf-8; mode: rst -*-

.. _lirc_ioctl:

************
LIRC ioctl()
************


Name
====

LIRC ioctl - Sends a I/O control command to a LIRC device

Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_capability *argp )


Arguments
=========

``fd``
    File descriptor returned by ``open()``.

``request``
    The type of I/O control that will be used. See table :ref:`lirc-request`
    for details.

``argp``
    Arguments for the I/O control. They're specific to each request.


The LIRC device's ioctl definition is bound by the ioctl function
definition of struct file_operations, leaving us with an unsigned int
for the ioctl command and an unsigned long for the arg. For the purposes
of ioctl portability across 32-bit and 64-bit, these values are capped
to their 32-bit sizes.

The ioctls can be used to change specific hardware settings.
In general each driver should have a default set of settings. The driver
implementation is expected to re-apply the default settings when the
device is closed by user-space, so that every application opening the
device can rely on working with the default settings initially.

.. _lirc-request:

.. _LIRC_SET_SEND_MODE:
.. _LIRC_SET_REC_MODE:

``LIRC_SET_{SEND,REC}_MODE``

    Set send/receive mode. Largely obsolete for send, as only
    ``LIRC_MODE_PULSE`` is supported.

.. _lirc_dev_errors:

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
