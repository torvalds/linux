.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_OVERLAY:

********************
ioctl VIDIOC_OVERLAY
********************

NAME
====

VIDIOC_OVERLAY - Start or stop video overlay

SYNOPSIS
========

.. cpp:function:: int ioctl( int fd, int request, const int *argp )


ARGUMENTS
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_OVERLAY

``argp``


DESCRIPTION
===========

This ioctl is part of the :ref:`video overlay <overlay>` I/O method.
Applications call :ref:`VIDIOC_OVERLAY` to start or stop the overlay. It
takes a pointer to an integer which must be set to zero by the
application to stop overlay, to one to start.

Drivers do not support :ref:`VIDIOC_STREAMON` or
:ref:`VIDIOC_STREAMOFF <VIDIOC_STREAMON>` with
``V4L2_BUF_TYPE_VIDEO_OVERLAY``.


RETURN VALUE
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The overlay parameters have not been set up. See :ref:`overlay`
    for the necessary steps.
