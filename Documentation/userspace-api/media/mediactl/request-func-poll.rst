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

.. _request-func-poll:

**************
request poll()
**************

Name
====

request-poll - Wait for some event on a file descriptor


Synopsis
========

.. code-block:: c

    #include <sys/poll.h>


.. c:function:: int poll( struct pollfd *ufds, unsigned int nfds, int timeout )
   :name: request-poll

Arguments
=========

``ufds``
   List of file descriptor events to be watched

``nfds``
   Number of file descriptor events at the \*ufds array

``timeout``
   Timeout to wait for events


Description
===========

With the :c:func:`poll() <request-func-poll>` function applications can wait
for a request to complete.

On success :c:func:`poll() <request-func-poll>` returns the number of file
descriptors that have been selected (that is, file descriptors for which the
``revents`` field of the respective struct :c:type:`pollfd`
is non-zero). Request file descriptor set the ``POLLPRI`` flag in ``revents``
when the request was completed.  When the function times out it returns
a value of zero, on failure it returns -1 and the ``errno`` variable is
set appropriately.

Attempting to poll for a request that is not yet queued will
set the ``POLLERR`` flag in ``revents``.


Return Value
============

On success, :c:func:`poll() <request-func-poll>` returns the number of
structures which have non-zero ``revents`` fields, or zero if the call
timed out. On error -1 is returned, and the ``errno`` variable is set
appropriately:

``EBADF``
    One or more of the ``ufds`` members specify an invalid file
    descriptor.

``EFAULT``
    ``ufds`` references an inaccessible memory area.

``EINTR``
    The call was interrupted by a signal.

``EINVAL``
    The ``nfds`` value exceeds the ``RLIMIT_NOFILE`` value. Use
    ``getrlimit()`` to obtain this value.
