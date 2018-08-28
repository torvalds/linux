.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.1-or-later WITH no-invariant-sections

.. _request-func-close:

***************
request close()
***************

Name
====

request-close - Close a request file descriptor


Synopsis
========

.. code-block:: c

    #include <unistd.h>


.. c:function:: int close( int fd )
    :name: req-close

Arguments
=========

``fd``
    File descriptor returned by :ref:`MEDIA_IOC_REQUEST_ALLOC`.


Description
===========

Closes the request file descriptor. Resources associated with the request
are freed once all file descriptors associated with the request are closed
and the driver has completed the request.
See :ref:`here <media-request-life-time>` for more information.


Return Value
============

:ref:`close() <request-func-close>` returns 0 on success. On error, -1 is
returned, and ``errno`` is set appropriately. Possible error codes are:

EBADF
    ``fd`` is not a valid open file descriptor.
