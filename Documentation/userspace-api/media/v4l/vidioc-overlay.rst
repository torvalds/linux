.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_OVERLAY:

********************
ioctl VIDIOC_OVERLAY
********************

Name
====

VIDIOC_OVERLAY - Start or stop video overlay

Synopsis
========

.. c:macro:: VIDIOC_OVERLAY

``int ioctl(int fd, VIDIOC_OVERLAY, const int *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to an integer.

Description
===========

This ioctl is part of the :ref:`video overlay <overlay>` I/O method.
Applications call :ref:`VIDIOC_OVERLAY` to start or stop the overlay. It
takes a pointer to an integer which must be set to zero by the
application to stop overlay, to one to start.

Drivers do not support :ref:`VIDIOC_STREAMON` or
:ref:`VIDIOC_STREAMOFF <VIDIOC_STREAMON>` with
``V4L2_BUF_TYPE_VIDEO_OVERLAY``.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The overlay parameters have not been set up. See :ref:`overlay`
    for the necessary steps.
