.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_G_OUTPUT:

**************************************
ioctl VIDIOC_G_OUTPUT, VIDIOC_S_OUTPUT
**************************************

Name
====

VIDIOC_G_OUTPUT - VIDIOC_S_OUTPUT - Query or select the current video output

Synopsis
========

.. c:macro:: VIDIOC_G_OUTPUT

``int ioctl(int fd, VIDIOC_G_OUTPUT, int *argp)``

.. c:macro:: VIDIOC_S_OUTPUT

``int ioctl(int fd, VIDIOC_S_OUTPUT, int *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to an integer with output index.

Description
===========

To query the current video output applications call the
:ref:`VIDIOC_G_OUTPUT <VIDIOC_G_OUTPUT>` ioctl with a pointer to an integer where the driver
stores the number of the output, as in the struct
:c:type:`v4l2_output` ``index`` field. This ioctl will
fail only when there are no video outputs, returning the ``EINVAL`` error
code.

To select a video output applications store the number of the desired
output in an integer and call the :ref:`VIDIOC_S_OUTPUT <VIDIOC_G_OUTPUT>` ioctl with a
pointer to this integer. Side effects are possible. For example outputs
may support different video standards, so the driver may implicitly
switch the current standard. standard. Because of these possible side
effects applications must select an output before querying or
negotiating any other parameters.

Information about video outputs is available using the
:ref:`VIDIOC_ENUMOUTPUT` ioctl.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The number of the video output is out of bounds, or there are no
    video outputs at all.
