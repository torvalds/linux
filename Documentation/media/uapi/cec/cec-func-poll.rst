.. -*- coding: utf-8; mode: rst -*-

.. _cec-func-poll:

**********
cec poll()
**********

Name
====

cec-poll - Wait for some event on a file descriptor


Synopsis
========

.. code-block:: c

    #include <sys/poll.h>


.. c:function:: int poll( struct pollfd *ufds, unsigned int nfds, int timeout )
   :name: cec-poll

Arguments
=========

``ufds``
   List of FD events to be watched

``nfds``
   Number of FD efents at the \*ufds array

``timeout``
   Timeout to wait for events


Description
===========

.. note::

   This documents the proposed CEC API. This API is not yet finalized
   and is currently only available as a staging kernel module.

With the :c:func:`poll()` function applications can wait for CEC
events.

On success :c:func:`poll()` returns the number of file descriptors
that have been selected (that is, file descriptors for which the
``revents`` field of the respective struct :c:type:`pollfd`
is non-zero). CEC devices set the ``POLLIN`` and ``POLLRDNORM`` flags in
the ``revents`` field if there are messages in the receive queue. If the
transmit queue has room for new messages, the ``POLLOUT`` and
``POLLWRNORM`` flags are set. If there are events in the event queue,
then the ``POLLPRI`` flag is set. When the function timed out it returns
a value of zero, on failure it returns -1 and the ``errno`` variable is
set appropriately.

For more details see the :c:func:`poll()` manual page.


Return Value
============

On success, :c:func:`poll()` returns the number structures which have
non-zero ``revents`` fields, or zero if the call timed out. On error -1
is returned, and the ``errno`` variable is set appropriately:

``EBADF``
    One or more of the ``ufds`` members specify an invalid file
    descriptor.

``EFAULT``
    ``ufds`` references an inaccessible memory area.

``EINTR``
    The call was interrupted by a signal.

``EINVAL``
    The ``nfds`` argument is greater than ``OPEN_MAX``.
