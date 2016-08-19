.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_G_OUTPUT:

**************************************
ioctl VIDIOC_G_OUTPUT, VIDIOC_S_OUTPUT
**************************************

Name
====

VIDIOC_G_OUTPUT - VIDIOC_S_OUTPUT - Query or select the current video output


Synopsis
========

.. c:function:: int ioctl( int fd, int request, int *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_G_OUTPUT, VIDIOC_S_OUTPUT

``argp``


Description
===========

To query the current video output applications call the
:ref:`VIDIOC_G_OUTPUT <VIDIOC_G_OUTPUT>` ioctl with a pointer to an integer where the driver
stores the number of the output, as in the struct
:ref:`v4l2_output <v4l2-output>` ``index`` field. This ioctl will
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
