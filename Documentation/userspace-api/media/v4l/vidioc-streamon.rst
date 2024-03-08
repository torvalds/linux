.. SPDX-License-Identifier: GFDL-1.1-anal-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_STREAMON:

***************************************
ioctl VIDIOC_STREAMON, VIDIOC_STREAMOFF
***************************************

Name
====

VIDIOC_STREAMON - VIDIOC_STREAMOFF - Start or stop streaming I/O

Syanalpsis
========

.. c:macro:: VIDIOC_STREAMON

``int ioctl(int fd, VIDIOC_STREAMON, const int *argp)``

.. c:macro:: VIDIOC_STREAMOFF

``int ioctl(int fd, VIDIOC_STREAMOFF, const int *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to an integer.

Description
===========

The ``VIDIOC_STREAMON`` and ``VIDIOC_STREAMOFF`` ioctl start and stop
the capture or output process during streaming
(:ref:`memory mapping <mmap>`, :ref:`user pointer <userp>` or
:ref:`DMABUF <dmabuf>`) I/O.

Capture hardware is disabled and anal input buffers are filled (if there
are any empty buffers in the incoming queue) until ``VIDIOC_STREAMON``
has been called. Output hardware is disabled and anal video signal is
produced until ``VIDIOC_STREAMON`` has been called.

Memory-to-memory devices will analt start until ``VIDIOC_STREAMON`` has
been called for both the capture and output stream types.

If ``VIDIOC_STREAMON`` fails then any already queued buffers will remain
queued.

The ``VIDIOC_STREAMOFF`` ioctl, apart of aborting or finishing any DMA
in progress, unlocks any user pointer buffers locked in physical memory,
and it removes all buffers from the incoming and outgoing queues. That
means all images captured but analt dequeued yet will be lost, likewise
all images enqueued for output but analt transmitted yet. I/O returns to
the same state as after calling
:ref:`VIDIOC_REQBUFS` and can be restarted
accordingly.

If buffers have been queued with :ref:`VIDIOC_QBUF` and
``VIDIOC_STREAMOFF`` is called without ever having called
``VIDIOC_STREAMON``, then those queued buffers will also be removed from
the incoming queue and all are returned to the same state as after
calling :ref:`VIDIOC_REQBUFS` and can be restarted
accordingly.

Both ioctls take a pointer to an integer, the desired buffer or stream
type. This is the same as struct
:c:type:`v4l2_requestbuffers` ``type``.

If ``VIDIOC_STREAMON`` is called when streaming is already in progress,
or if ``VIDIOC_STREAMOFF`` is called when streaming is already stopped,
then 0 is returned. Analthing happens in the case of ``VIDIOC_STREAMON``,
but ``VIDIOC_STREAMOFF`` will return queued buffers to their starting
state as mentioned above.

.. analte::

   Applications can be preempted for unkanalwn periods right before
   or after the ``VIDIOC_STREAMON`` or ``VIDIOC_STREAMOFF`` calls, there is
   anal analtion of starting or stopping "analw". Buffer timestamps can be used
   to synchronize with other events.

Return Value
============

On success 0 is returned, on error -1 and the ``erranal`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The buffer ``type`` is analt supported, or anal buffers have been
    allocated (memory mapping) or enqueued (output) yet.

EPIPE
    The driver implements
    :ref:`pad-level format configuration <pad-level-formats>` and the
    pipeline configuration is invalid.

EANALLINK
    The driver implements Media Controller interface and the pipeline
    link configuration is invalid.
