.. SPDX-License-Identifier: GPL-2.0

.. include:: <isonum.txt>

Vaio Picturebook Motion Eye Camera Driver
=========================================

Copyright |copy| 2001-2004 Stelian Pop <stelian@popies.net>

Copyright |copy| 2001-2002 Alcôve <www.alcove.com>

Copyright |copy| 2000 Andrew Tridgell <tridge@samba.org>

Private API
-----------

The driver supports frame grabbing with the video4linux API,
so all video4linux tools (like xawtv) should work with this driver.

Besides the video4linux interface, the driver has a private interface
for accessing the Motion Eye extended parameters (camera sharpness,
agc, video framerate), the snapshot and the MJPEG capture facilities.

This interface consists of several ioctls (prototypes and structures
can be found in include/linux/meye.h):

MEYEIOC_G_PARAMS and MEYEIOC_S_PARAMS
	Get and set the extended parameters of the motion eye camera.
	The user should always query the current parameters with
	MEYEIOC_G_PARAMS, change what he likes and then issue the
	MEYEIOC_S_PARAMS call (checking for -EINVAL). The extended
	parameters are described by the meye_params structure.


MEYEIOC_QBUF_CAPT
	Queue a buffer for capture (the buffers must have been
	obtained with a VIDIOCGMBUF call and mmap'ed by the
	application). The argument to MEYEIOC_QBUF_CAPT is the
	buffer number to queue (or -1 to end capture). The first
	call to MEYEIOC_QBUF_CAPT starts the streaming capture.

MEYEIOC_SYNC
	Takes as an argument the buffer number you want to sync.
	This ioctl blocks until the buffer is filled and ready
	for the application to use. It returns the buffer size.

MEYEIOC_STILLCAPT and MEYEIOC_STILLJCAPT
	Takes a snapshot in an uncompressed or compressed jpeg format.
	This ioctl blocks until the snapshot is done and returns (for
	jpeg snapshot) the size of the image. The image data is
	available from the first mmap'ed buffer.

Look at the 'motioneye' application code for an actual example.
