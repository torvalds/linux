.. SPDX-License-Identifier: GPL-2.0

===================================
High-speed DMABUF interface for IIO
===================================

1. Overview
===========

The Industrial I/O subsystem supports access to buffers through a
file-based interface, with read() and write() access calls through the
IIO device's dev node.

It additionally supports a DMABUF based interface, where the userspace
can attach DMABUF objects (externally created) to an IIO buffer, and
subsequently use them for data transfers.

A userspace application can then use this interface to share DMABUF
objects between several interfaces, allowing it to transfer data in a
zero-copy fashion, for instance between IIO and the USB stack.

The userspace application can also memory-map the DMABUF objects, and
access the sample data directly. The advantage of doing this vs. the
read() interface is that it avoids an extra copy of the data between the
kernel and userspace. This is particularly useful for high-speed devices
which produce several megabytes or even gigabytes of data per second.
It does however increase the userspace-kernelspace synchronization
overhead, as the DMA_BUF_SYNC_START and DMA_BUF_SYNC_END IOCTLs have to
be used for data integrity.

2. User API
===========

As part of this interface, three new IOCTLs have been added. These three
IOCTLs have to be performed on the IIO buffer's file descriptor, which
can be obtained using the IIO_BUFFER_GET_FD_IOCTL() ioctl.

  ``IIO_BUFFER_DMABUF_ATTACH_IOCTL(int fd)``
    Attach the DMABUF object, identified by its file descriptor, to the
    IIO buffer. Returns zero on success, and a negative errno value on
    error.

  ``IIO_BUFFER_DMABUF_DETACH_IOCTL(int fd)``
    Detach the given DMABUF object, identified by its file descriptor,
    from the IIO buffer. Returns zero on success, and a negative errno
    value on error.

    Note that closing the IIO buffer's file descriptor will
    automatically detach all previously attached DMABUF objects.

  ``IIO_BUFFER_DMABUF_ENQUEUE_IOCTL(struct iio_dmabuf *iio_dmabuf)``
    Enqueue a previously attached DMABUF object to the buffer queue.
    Enqueued DMABUFs will be read from (if output buffer) or written to
    (if input buffer) as long as the buffer is enabled.
