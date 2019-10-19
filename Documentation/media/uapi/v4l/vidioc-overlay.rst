.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDIOC_OVERLAY:

********************
ioctl VIDIOC_OVERLAY
********************

Name
====

VIDIOC_OVERLAY - Start or stop video overlay


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_OVERLAY, const int *argp )
    :name: VIDIOC_OVERLAY


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

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
