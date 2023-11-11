.. SPDX-License-Identifier: GPL-2.0

.. include:: <isonum.txt>

Vaio Picturebook Motion Eye Camera Driver
=========================================

Copyright |copy| 2001-2004 Stelian Pop <stelian@popies.net>

Copyright |copy| 2001-2002 Alcôve <www.alcove.com>

Copyright |copy| 2000 Andrew Tridgell <tridge@samba.org>

This driver enable the use of video4linux compatible applications with the
Motion Eye camera. This driver requires the "Sony Laptop Extras" driver (which
can be found in the "Misc devices" section of the kernel configuration utility)
to be compiled and installed (using its "camera=1" parameter).

It can do at maximum 30 fps @ 320x240 or 15 fps @ 640x480.

Grabbing is supported in packed YUV colorspace only.

MJPEG hardware grabbing is supported via a private API (see below).

Hardware supported
------------------

This driver supports the 'second' version of the MotionEye camera :)

The first version was connected directly on the video bus of the Neomagic
video card and is unsupported.

The second one, made by Kawasaki Steel is fully supported by this
driver (PCI vendor/device is 0x136b/0xff01)

The third one, present in recent (more or less last year) Picturebooks
(C1M* models), is not supported. The manufacturer has given the specs
to the developers under a NDA (which allows the development of a GPL
driver however), but things are not moving very fast (see
http://r-engine.sourceforge.net/) (PCI vendor/device is 0x10cf/0x2011).

There is a forth model connected on the USB bus in TR1* Vaio laptops.
This camera is not supported at all by the current driver, in fact
little information if any is available for this camera
(USB vendor/device is 0x054c/0x0107).

Driver options
--------------

Several options can be passed to the meye driver using the standard
module argument syntax (<param>=<value> when passing the option to the
module or meye.<param>=<value> on the kernel boot line when meye is
statically linked into the kernel). Those options are:

.. code-block:: none

	gbuffers:	number of capture buffers, default is 2 (32 max)

	gbufsize:	size of each capture buffer, default is 614400

	video_nr:	video device to register (0 = /dev/video0, etc)

Module use
----------

In order to automatically load the meye module on use, you can put those lines
in your /etc/modprobe.d/meye.conf file:

.. code-block:: none

	alias char-major-81 videodev
	alias char-major-81-0 meye
	options meye gbuffers=32

Usage:
------

.. code-block:: none

	xawtv >= 3.49 (<http://bytesex.org/xawtv/>)
		for display and uncompressed video capture:

			xawtv -c /dev/video0 -geometry 640x480
				or
			xawtv -c /dev/video0 -geometry 320x240

	motioneye (<http://popies.net/meye/>)
		for getting ppm or jpg snapshots, mjpeg video

Bugs / Todo
-----------

- 'motioneye' still uses the meye private v4l1 API extensions.
