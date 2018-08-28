.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.1-or-later WITH no-invariant-sections

.. _media_ioc_request_alloc:

*****************************
ioctl MEDIA_IOC_REQUEST_ALLOC
*****************************

Name
====

MEDIA_IOC_REQUEST_ALLOC - Allocate a request


Synopsis
========

.. c:function:: int ioctl( int fd, MEDIA_IOC_REQUEST_ALLOC, int *argp )
    :name: MEDIA_IOC_REQUEST_ALLOC


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <media-func-open>`.

``argp``
    Pointer to an integer.


Description
===========

If the media device supports :ref:`requests <media-request-api>`, then
this ioctl can be used to allocate a request. If it is not supported, then
``errno`` is set to ``ENOTTY``. A request is accessed through a file descriptor
that is returned in ``*argp``.

If the request was successfully allocated, then the request file descriptor
can be passed to the :ref:`VIDIOC_QBUF <VIDIOC_QBUF>`,
:ref:`VIDIOC_G_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>`,
:ref:`VIDIOC_S_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` and
:ref:`VIDIOC_TRY_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` ioctls.

In addition, the request can be queued by calling
:ref:`MEDIA_REQUEST_IOC_QUEUE` and re-initialized by calling
:ref:`MEDIA_REQUEST_IOC_REINIT`.

Finally, the file descriptor can be :ref:`polled <request-func-poll>` to wait
for the request to complete.

The request will remain allocated until all the file descriptors associated
with it are closed by :ref:`close() <request-func-close>` and the driver no
longer uses the request internally. See also
:ref:`here <media-request-life-time>` for more information.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

ENOTTY
    The driver has no support for requests.
