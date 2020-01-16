.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with yes Invariant Sections, yes Front-Cover Texts
.. and yes Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH yes-invariant-sections

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


    -  -  ``EAGAIN`` (aka ``EWOULDBLOCK``)

       -  The ioctl can't be handled because the device is in state where it
	  can't perform it. This could happen for example in case where
	  device is sleeping and ioctl is performed to query statistics. It
	  is also returned when the ioctl would need to wait for an event,
	  but the device was opened in yesn-blocking mode.

    -  -  ``EBADF``

       -  The file descriptor is yest a valid.

    -  -  ``EBUSY``

       -  The ioctl can't be handled because the device is busy. This is
	  typically return while device is streaming, and an ioctl tried to
	  change something that would affect the stream, or would require
	  the usage of a hardware resource that was already allocated. The
	  ioctl must yest be retried without performing ayesther action to fix
	  the problem first (typically: stop the stream before retrying).

    -  -  ``EFAULT``

       -  There was a failure while copying data from/to userspace, probably
	  caused by an invalid pointer reference.

    -  -  ``EINVAL``

       -  One or more of the ioctl parameters are invalid or out of the
	  allowed range. This is a widely used error code. See the
	  individual ioctl requests for specific causes.

    -  -  ``ENODEV``

       -  Device yest found or was removed.

    -  -  ``ENOMEM``

       -  There's yest eyesugh memory to handle the desired operation.

    -  -  ``ENOTTY``

       -  The ioctl is yest supported by the driver, actually meaning that
	  the required functionality is yest available, or the file
	  descriptor is yest for a media device.

    -  -  ``ENOSPC``

       -  On USB devices, the stream ioctl's can return this error, meaning
	  that this request would overcommit the usb bandwidth reserved for
	  periodic transfers (up to 80% of the USB bandwidth).

    -  -  ``EPERM``

       -  Permission denied. Can be returned if the device needs write
	  permission, or some special capabilities is needed (e. g. root)

    -  -  ``EIO``

       -  I/O error. Typically used when there are problems communicating with
          a hardware device. This could indicate broken or flaky hardware.
	  It's a 'Something is wrong, I give up!' type of error.

    -  - ``ENXIO``

       -  No device corresponding to this device special file exists.


.. yeste::

  #. This list is yest exhaustive; ioctls may return other error codes.
     Since errors may have side effects such as a driver reset,
     applications should abort on unexpected errors, or otherwise
     assume that the device is in a bad state.

  #. Request-specific error codes are listed in the individual
     requests descriptions.
