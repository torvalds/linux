.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_G_INPUT:

************************************
ioctl VIDIOC_G_INPUT, VIDIOC_S_INPUT
************************************

Name
====

VIDIOC_G_INPUT - VIDIOC_S_INPUT - Query or select the current video input


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_G_INPUT, int *argp )
    :name: VIDIOC_G_INPUT

.. c:function:: int ioctl( int fd, VIDIOC_S_INPUT, int *argp )
    :name: VIDIOC_S_INPUT


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer an integer with input index.


Description
===========

To query the current video input applications call the
:ref:`VIDIOC_G_INPUT <VIDIOC_G_INPUT>` ioctl with a pointer to an integer where the driver
stores the number of the input, as in the struct
:c:type:`v4l2_input` ``index`` field. This ioctl will fail
only when there are no video inputs, returning ``EINVAL``.

To select a video input applications store the number of the desired
input in an integer and call the :ref:`VIDIOC_S_INPUT <VIDIOC_G_INPUT>` ioctl with a pointer
to this integer. Side effects are possible. For example inputs may
support different video standards, so the driver may implicitly switch
the current standard. Because of these possible side effects
applications must select an input before querying or negotiating any
other parameters.

Information about video inputs is available using the
:ref:`VIDIOC_ENUMINPUT` ioctl.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The number of the video input is out of bounds.
