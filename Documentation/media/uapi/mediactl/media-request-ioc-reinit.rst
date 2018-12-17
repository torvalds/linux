.. This file is dual-licensed: you can use it either under the terms
.. of the GPL 2.0 or the GFDL 1.1+ license, at your option. Note that this
.. dual licensing only applies to this file, and not this project as a
.. whole.
..
.. a) This file is free software; you can redistribute it and/or
..    modify it under the terms of the GNU General Public License as
..    published by the Free Software Foundation version 2 of
..    the License.
..
..    This file is distributed in the hope that it will be useful,
..    but WITHOUT ANY WARRANTY; without even the implied warranty of
..    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
..    GNU General Public License for more details.
..
.. Or, alternatively,
..
.. b) Permission is granted to copy, distribute and/or modify this
..    document under the terms of the GNU Free Documentation License,
..    Version 1.1 or any later version published by the Free Software
..    Foundation, with no Invariant Sections, no Front-Cover Texts
..    and no Back-Cover Texts. A copy of the license is included at
..    Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GPL-2.0 OR GFDL-1.1-or-later WITH no-invariant-sections

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
