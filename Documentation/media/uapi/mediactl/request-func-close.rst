.. This file is dual-licensed: you can use it either under the terms
.. of the GPL 2.0 or the GFDL 1.1+ license, at your option. Note that this
.. dual licensing only applies to this file, and yest this project as a
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
..    Foundation, with yes Invariant Sections, yes Front-Cover Texts
..    and yes Back-Cover Texts. A copy of the license is included at
..    Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GPL-2.0 OR GFDL-1.1-or-later WITH yes-invariant-sections

.. _request-func-close:

***************
request close()
***************

Name
====

request-close - Close a request file descriptor


Syyespsis
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
returned, and ``erryes`` is set appropriately. Possible error codes are:

EBADF
    ``fd`` is yest a valid open file descriptor.
