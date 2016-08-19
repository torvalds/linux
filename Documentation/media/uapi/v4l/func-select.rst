.. -*- coding: utf-8; mode: rst -*-

.. _func-select:

*************
V4L2 select()
*************

Name
====

v4l2-select - Synchronous I/O multiplexing


Synopsis
========

.. code-block:: c

    #include <sys/time.h>
    #include <sys/types.h>
    #include <unistd.h>


.. c:function:: int select( int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout )


Arguments
=========



Description
===========

With the :ref:`select() <func-select>` function applications can suspend
execution until the driver has captured data or is ready to accept data
for output.

When streaming I/O has been negotiated this function waits until a
buffer has been filled or displayed and can be dequeued with the
:ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctl. When buffers are already in
the outgoing queue of the driver the function returns immediately.

On success :ref:`select() <func-select>` returns the total number of bits set in
:c:func:`struct fd_set`. When the function timed out it returns
a value of zero. On failure it returns -1 and the ``errno`` variable is
set appropriately. When the application did not call
:ref:`VIDIOC_QBUF` or
:ref:`VIDIOC_STREAMON` yet the :ref:`select() <func-select>`
function succeeds, setting the bit of the file descriptor in ``readfds``
or ``writefds``, but subsequent :ref:`VIDIOC_DQBUF <VIDIOC_QBUF>`
calls will fail. [#f1]_

When use of the :ref:`read() <func-read>` function has been negotiated and the
driver does not capture yet, the :ref:`select() <func-select>` function starts
capturing. When that fails, :ref:`select() <func-select>` returns successful and
a subsequent :ref:`read() <func-read>` call, which also attempts to start
capturing, will return an appropriate error code. When the driver
captures continuously (as opposed to, for example, still images) and
data is already available the :ref:`select() <func-select>` function returns
immediately.

When use of the :ref:`write() <func-write>` function has been negotiated the
:ref:`select() <func-select>` function just waits until the driver is ready for a
non-blocking :ref:`write() <func-write>` call.

All drivers implementing the :ref:`read() <func-read>` or :ref:`write() <func-write>`
function or streaming I/O must also support the :ref:`select() <func-select>`
function.

For more details see the :ref:`select() <func-select>` manual page.


Return Value
============

On success, :ref:`select() <func-select>` returns the number of descriptors
contained in the three returned descriptor sets, which will be zero if
the timeout expired. On error -1 is returned, and the ``errno`` variable
is set appropriately; the sets and ``timeout`` are undefined. Possible
error codes are:

EBADF
    One or more of the file descriptor sets specified a file descriptor
    that is not open.

EBUSY
    The driver does not support multiple read or write streams and the
    device is already in use.

EFAULT
    The ``readfds``, ``writefds``, ``exceptfds`` or ``timeout`` pointer
    references an inaccessible memory area.

EINTR
    The call was interrupted by a signal.

EINVAL
    The ``nfds`` argument is less than zero or greater than
    ``FD_SETSIZE``.

.. [#f1]
   The Linux kernel implements :ref:`select() <func-select>` like the
   :ref:`poll() <func-poll>` function, but :ref:`select() <func-select>` cannot
   return a ``POLLERR``.
