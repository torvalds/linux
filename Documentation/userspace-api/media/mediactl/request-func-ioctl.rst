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
..    Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GPL-2.0 OR GFDL-1.1-or-later WITH no-invariant-sections

.. _request-func-ioctl:

***************
request ioctl()
***************

Name
====

request-ioctl - Control a request file descriptor


Synopsis
========

.. code-block:: c

    #include <sys/ioctl.h>


.. c:function:: int ioctl( int fd, int cmd, void *argp )
    :name: req-ioctl

Arguments
=========

``fd``
    File descriptor returned by :ref:`MEDIA_IOC_REQUEST_ALLOC`.

``cmd``
    The request ioctl command code as defined in the media.h header file, for
    example :ref:`MEDIA_REQUEST_IOC_QUEUE`.

``argp``
    Pointer to a request-specific structure.


Description
===========

The :ref:`ioctl() <request-func-ioctl>` function manipulates request
parameters. The argument ``fd`` must be an open file descriptor.

The ioctl ``cmd`` code specifies the request function to be called. It
has encoded in it whether the argument is an input, output or read/write
parameter, and the size of the argument ``argp`` in bytes.

Macros and structures definitions specifying request ioctl commands and
their parameters are located in the media.h header file. All request ioctl
commands, their respective function and parameters are specified in
:ref:`media-user-func`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

Command-specific error codes are listed in the individual command
descriptions.

When an ioctl that takes an output or read/write parameter fails, the
parameter remains unmodified.
