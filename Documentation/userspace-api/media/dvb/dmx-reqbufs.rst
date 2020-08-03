.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _DMX_REQBUFS:

*****************
ioctl DMX_REQBUFS
*****************

Name
====

DMX_REQBUFS - Initiate Memory Mapping and/or DMA buffer I/O

.. warning:: this API is still experimental


Synopsis
========

.. c:function:: int ioctl( int fd, DMX_REQBUFS, struct dmx_requestbuffers *argp )
    :name: DMX_REQBUFS


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <dmx_fopen>`.

``argp``
    Pointer to struct :c:type:`dmx_requestbuffers`.

Description
===========

This ioctl is used to initiate a memory mapped or DMABUF based demux I/O.

Memory mapped buffers are located in device memory and must be allocated
with this ioctl before they can be mapped into the application's address
space. User buffers are allocated by applications themselves, and this
ioctl is merely used to switch the driver into user pointer I/O mode and
to setup some internal structures. Similarly, DMABUF buffers are
allocated by applications through a device driver, and this ioctl only
configures the driver into DMABUF I/O mode without performing any direct
allocation.

To allocate device buffers applications initialize all fields of the
struct :c:type:`dmx_requestbuffers` structure. They set the  ``count`` field
to the desired number of buffers,  and ``size`` to the size of each
buffer.

When the ioctl is called with a pointer to this structure, the driver will
attempt to allocate the requested number of buffers and it stores the actual
number allocated in the ``count`` field. The ``count`` can be smaller than the number requested, even zero, when the driver runs out of free memory. A larger
number is also possible when the driver requires more buffers to
function correctly. The actual allocated buffer size can is returned
at ``size``, and can be smaller than what's requested.

When this I/O method is not supported, the ioctl returns an ``EOPNOTSUPP``
error code.

Applications can call :ref:`DMX_REQBUFS` again to change the number of
buffers, however this cannot succeed when any buffers are still mapped.
A ``count`` value of zero frees all buffers, after aborting or finishing
any DMA in progress.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EOPNOTSUPP
    The  the requested I/O method is not supported.
