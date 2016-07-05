.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_QUERYBUF:

*********************
ioctl VIDIOC_QUERYBUF
*********************

Name
====

VIDIOC_QUERYBUF - Query the status of a buffer


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_buffer *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_QUERYBUF

``argp``


Description
===========

This ioctl is part of the :ref:`streaming <mmap>` I/O method. It can
be used to query the status of a buffer at any time after buffers have
been allocated with the :ref:`VIDIOC_REQBUFS` ioctl.

Applications set the ``type`` field of a struct
:ref:`v4l2_buffer <v4l2-buffer>` to the same buffer type as was
previously used with struct :ref:`v4l2_format <v4l2-format>` ``type``
and struct :ref:`v4l2_requestbuffers <v4l2-requestbuffers>` ``type``,
and the ``index`` field. Valid index numbers range from zero to the
number of buffers allocated with
:ref:`VIDIOC_REQBUFS` (struct
:ref:`v4l2_requestbuffers <v4l2-requestbuffers>` ``count``) minus
one. The ``reserved`` and ``reserved2`` fields must be set to 0. When
using the :ref:`multi-planar API <planar-apis>`, the ``m.planes``
field must contain a userspace pointer to an array of struct
:ref:`v4l2_plane <v4l2-plane>` and the ``length`` field has to be set
to the number of elements in that array. After calling
:ref:`VIDIOC_QUERYBUF` with a pointer to this structure drivers return an
error code or fill the rest of the structure.

In the ``flags`` field the ``V4L2_BUF_FLAG_MAPPED``,
``V4L2_BUF_FLAG_PREPARED``, ``V4L2_BUF_FLAG_QUEUED`` and
``V4L2_BUF_FLAG_DONE`` flags will be valid. The ``memory`` field will be
set to the current I/O method. For the single-planar API, the
``m.offset`` contains the offset of the buffer from the start of the
device memory, the ``length`` field its size. For the multi-planar API,
fields ``m.mem_offset`` and ``length`` in the ``m.planes`` array
elements will be used instead and the ``length`` field of struct
:ref:`v4l2_buffer <v4l2-buffer>` is set to the number of filled-in
array elements. The driver may or may not set the remaining fields and
flags, they are meaningless in this context.

The :ref:`struct v4l2_buffer <v4l2-buffer>` structure is specified in
:ref:`buffer`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The buffer ``type`` is not supported, or the ``index`` is out of
    bounds.
