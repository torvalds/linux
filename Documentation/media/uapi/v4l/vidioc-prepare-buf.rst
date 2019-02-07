.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDIOC_PREPARE_BUF:

************************
ioctl VIDIOC_PREPARE_BUF
************************

Name
====

VIDIOC_PREPARE_BUF - Prepare a buffer for I/O


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_PREPARE_BUF, struct v4l2_buffer *argp )
    :name: VIDIOC_PREPARE_BUF


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_buffer`.


Description
===========

Applications can optionally call the :ref:`VIDIOC_PREPARE_BUF` ioctl to
pass ownership of the buffer to the driver before actually enqueuing it,
using the :ref:`VIDIOC_QBUF <VIDIOC_QBUF>` ioctl, and to prepare it for future I/O. Such
preparations may include cache invalidation or cleaning. Performing them
in advance saves time during the actual I/O. In case such cache
operations are not required, the application can use one of
``V4L2_BUF_FLAG_NO_CACHE_INVALIDATE`` and
``V4L2_BUF_FLAG_NO_CACHE_CLEAN`` flags to skip the respective step.

The struct :c:type:`v4l2_buffer` structure is specified in
:ref:`buffer`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EBUSY
    File I/O is in progress.

EINVAL
    The buffer ``type`` is not supported, or the ``index`` is out of
    bounds, or no buffers have been allocated yet, or the ``userptr`` or
    ``length`` are invalid.
