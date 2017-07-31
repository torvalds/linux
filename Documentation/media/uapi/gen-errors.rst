.. -*- coding: utf-8; mode: rst -*-

.. _gen_errors:

*******************
Generic Error Codes
*******************


.. _gen-errors:

.. tabularcolumns:: |p{2.5cm}|p{15.0cm}|

.. flat-table:: Generic error codes
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 16


    -  .. row 1

       -  ``EAGAIN`` (aka ``EWOULDBLOCK``)

       -  The ioctl can't be handled because the device is in state where it
	  can't perform it. This could happen for example in case where
	  device is sleeping and ioctl is performed to query statistics. It
	  is also returned when the ioctl would need to wait for an event,
	  but the device was opened in non-blocking mode.

    -  .. row 2

       -  ``EBADF``

       -  The file descriptor is not a valid.

    -  .. row 3

       -  ``EBUSY``

       -  The ioctl can't be handled because the device is busy. This is
	  typically return while device is streaming, and an ioctl tried to
	  change something that would affect the stream, or would require
	  the usage of a hardware resource that was already allocated. The
	  ioctl must not be retried without performing another action to fix
	  the problem first (typically: stop the stream before retrying).

    -  .. row 4

       -  ``EFAULT``

       -  There was a failure while copying data from/to userspace, probably
	  caused by an invalid pointer reference.

    -  .. row 5

       -  ``EINVAL``

       -  One or more of the ioctl parameters are invalid or out of the
	  allowed range. This is a widely used error code. See the
	  individual ioctl requests for specific causes.

    -  .. row 6

       -  ``ENODEV``

       -  Device not found or was removed.

    -  .. row 7

       -  ``ENOMEM``

       -  There's not enough memory to handle the desired operation.

    -  .. row 8

       -  ``ENOTTY``

       -  The ioctl is not supported by the driver, actually meaning that
	  the required functionality is not available, or the file
	  descriptor is not for a media device.

    -  .. row 9

       -  ``ENOSPC``

       -  On USB devices, the stream ioctl's can return this error, meaning
	  that this request would overcommit the usb bandwidth reserved for
	  periodic transfers (up to 80% of the USB bandwidth).

    -  .. row 10

       -  ``EPERM``

       -  Permission denied. Can be returned if the device needs write
	  permission, or some special capabilities is needed (e. g. root)

    -  .. row 11

       -  ``EIO``

       -  I/O error. Typically used when there are problems communicating with
          a hardware device. This could indicate broken or flaky hardware.
	  It's a 'Something is wrong, I give up!' type of error.

.. note::

  #. This list is not exhaustive; ioctls may return other error codes.
     Since errors may have side effects such as a driver reset,
     applications should abort on unexpected errors, or otherwise
     assume that the device is in a bad state.

  #. Request-specific error codes are listed in the individual
     requests descriptions.
