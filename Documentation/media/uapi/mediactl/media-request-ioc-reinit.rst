.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.1-or-later WITH no-invariant-sections

.. _media_request_ioc_reinit:

******************************
ioctl MEDIA_REQUEST_IOC_REINIT
******************************

Name
====

MEDIA_REQUEST_IOC_REINIT - Re-initialize a request


Synopsis
========

.. c:function:: int ioctl( int request_fd, MEDIA_REQUEST_IOC_REINIT )
    :name: MEDIA_REQUEST_IOC_REINIT


Arguments
=========

``request_fd``
    File descriptor returned by :ref:`MEDIA_IOC_REQUEST_ALLOC`.

Description
===========

If the media device supports :ref:`requests <media-request-api>`, then
this request ioctl can be used to re-initialize a previously allocated
request.

Re-initializing a request will clear any existing data from the request.
This avoids having to :ref:`close() <request-func-close>` a completed
request and allocate a new request. Instead the completed request can just
be re-initialized and it is ready to be used again.

A request can only be re-initialized if it either has not been queued
yet, or if it was queued and completed. Otherwise it will set ``errno``
to ``EBUSY``. No other error codes can be returned.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately.

EBUSY
    The request is queued but not yet completed.
