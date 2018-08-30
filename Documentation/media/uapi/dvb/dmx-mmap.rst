.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _dmx-mmap:

*****************
Digital TV mmap()
*****************

Name
====

dmx-mmap - Map device memory into application address space

.. warning:: this API is still experimental

Synopsis
========

.. code-block:: c

    #include <unistd.h>
    #include <sys/mman.h>


.. c:function:: void *mmap( void *start, size_t length, int prot, int flags, int fd, off_t offset )
    :name: dmx-mmap

Arguments
=========

``start``
    Map the buffer to this address in the application's address space.
    When the ``MAP_FIXED`` flag is specified, ``start`` must be a
    multiple of the pagesize and mmap will fail when the specified
    address cannot be used. Use of this option is discouraged;
    applications should just specify a ``NULL`` pointer here.

``length``
    Length of the memory area to map. This must be a multiple of the
    DVB packet length (188, on most drivers).

``prot``
    The ``prot`` argument describes the desired memory protection.
    Regardless of the device type and the direction of data exchange it
    should be set to ``PROT_READ`` | ``PROT_WRITE``, permitting read
    and write access to image buffers. Drivers should support at least
    this combination of flags.

``flags``
    The ``flags`` parameter specifies the type of the mapped object,
    mapping options and whether modifications made to the mapped copy of
    the page are private to the process or are to be shared with other
    references.

    ``MAP_FIXED`` requests that the driver selects no other address than
    the one specified. If the specified address cannot be used,
    :ref:`mmap() <dmx-mmap>` will fail. If ``MAP_FIXED`` is specified,
    ``start`` must be a multiple of the pagesize. Use of this option is
    discouraged.

    One of the ``MAP_SHARED`` or ``MAP_PRIVATE`` flags must be set.
    ``MAP_SHARED`` allows applications to share the mapped memory with
    other (e. g. child-) processes.

    .. note::

       The Linux Digital TV applications should not set the
       ``MAP_PRIVATE``, ``MAP_DENYWRITE``, ``MAP_EXECUTABLE`` or ``MAP_ANON``
       flags.

``fd``
    File descriptor returned by :ref:`open() <dmx_fopen>`.

``offset``
    Offset of the buffer in device memory, as returned by
    :ref:`DMX_QUERYBUF` ioctl.


Description
===========

The :ref:`mmap() <dmx-mmap>` function asks to map ``length`` bytes starting at
``offset`` in the memory of the device specified by ``fd`` into the
application address space, preferably at address ``start``. This latter
address is a hint only, and is usually specified as 0.

Suitable length and offset parameters are queried with the
:ref:`DMX_QUERYBUF` ioctl. Buffers must be allocated with the
:ref:`DMX_REQBUFS` ioctl before they can be queried.

To unmap buffers the :ref:`munmap() <dmx-munmap>` function is used.


Return Value
============

On success :ref:`mmap() <dmx-mmap>` returns a pointer to the mapped buffer. On
error ``MAP_FAILED`` (-1) is returned, and the ``errno`` variable is set
appropriately. Possible error codes are:

EBADF
    ``fd`` is not a valid file descriptor.

EACCES
    ``fd`` is not open for reading and writing.

EINVAL
    The ``start`` or ``length`` or ``offset`` are not suitable. (E. g.
    they are too large, or not aligned on a ``PAGESIZE`` boundary.)

    The ``flags`` or ``prot`` value is not supported.

    No buffers have been allocated with the
    :ref:`DMX_REQBUFS` ioctl.

ENOMEM
    Not enough physical or virtual memory was available to complete the
    request.
