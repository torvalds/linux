.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _osd:

******************************
Video Output Overlay Interface
******************************

**Also known as On-Screen Display (OSD)**

Some video output devices can overlay a framebuffer image onto the
outgoing video signal. Applications can set up such an overlay using
this interface, which borrows structures and ioctls of the
:ref:`Video Overlay <overlay>` interface.

The OSD function is accessible through the same character special file
as the :ref:`Video Output <capture>` function.

.. note::

   The default function of such a ``/dev/video`` device is video
   capturing or output. The OSD function is only available after calling
   the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl.


Querying Capabilities
=====================

Devices supporting the *Video Output Overlay* interface set the
``V4L2_CAP_VIDEO_OUTPUT_OVERLAY`` flag in the ``capabilities`` field of
struct :c:type:`v4l2_capability` returned by the
:ref:`VIDIOC_QUERYCAP` ioctl.


Framebuffer
===========

Contrary to the *Video Overlay* interface the framebuffer is normally
implemented on the TV card and not the graphics card. On Linux it is
accessible as a framebuffer device (``/dev/fbN``). Given a V4L2 device,
applications can find the corresponding framebuffer device by calling
the :ref:`VIDIOC_G_FBUF <VIDIOC_G_FBUF>` ioctl. It returns, amongst
other information, the physical address of the framebuffer in the
``base`` field of struct :c:type:`v4l2_framebuffer`.
The framebuffer device ioctl ``FBIOGET_FSCREENINFO`` returns the same
address in the ``smem_start`` field of struct
struct :c:type:`fb_fix_screeninfo`. The ``FBIOGET_FSCREENINFO``
ioctl and struct :c:type:`fb_fix_screeninfo` are defined in
the ``linux/fb.h`` header file.

The width and height of the framebuffer depends on the current video
standard. A V4L2 driver may reject attempts to change the video standard
(or any other ioctl which would imply a framebuffer size change) with an
``EBUSY`` error code until all applications closed the framebuffer device.

Example: Finding a framebuffer device for OSD
---------------------------------------------

.. code-block:: c

    #include <linux/fb.h>

    struct v4l2_framebuffer fbuf;
    unsigned int i;
    int fb_fd;

    if (-1 == ioctl(fd, VIDIOC_G_FBUF, &fbuf)) {
	perror("VIDIOC_G_FBUF");
	exit(EXIT_FAILURE);
    }

    for (i = 0; i < 30; i++) {
	char dev_name[16];
	struct fb_fix_screeninfo si;

	snprintf(dev_name, sizeof(dev_name), "/dev/fb%u", i);

	fb_fd = open(dev_name, O_RDWR);
	if (-1 == fb_fd) {
	    switch (errno) {
	    case ENOENT: /* no such file */
	    case ENXIO:  /* no driver */
		continue;

	    default:
		perror("open");
		exit(EXIT_FAILURE);
	    }
	}

	if (0 == ioctl(fb_fd, FBIOGET_FSCREENINFO, &si)) {
	    if (si.smem_start == (unsigned long)fbuf.base)
		break;
	} else {
	    /* Apparently not a framebuffer device. */
	}

	close(fb_fd);
	fb_fd = -1;
    }

    /* fb_fd is the file descriptor of the framebuffer device
       for the video output overlay, or -1 if no device was found. */


Overlay Window and Scaling
==========================

The overlay is controlled by source and target rectangles. The source
rectangle selects a subsection of the framebuffer image to be overlaid,
the target rectangle an area in the outgoing video signal where the
image will appear. Drivers may or may not support scaling, and arbitrary
sizes and positions of these rectangles. Further drivers may support any
(or none) of the clipping/blending methods defined for the
:ref:`Video Overlay <overlay>` interface.

A struct :c:type:`v4l2_window` defines the size of the
source rectangle, its position in the framebuffer and the
clipping/blending method to be used for the overlay. To get the current
parameters applications set the ``type`` field of a struct
:c:type:`v4l2_format` to
``V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY`` and call the
:ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` ioctl. The driver fills the
struct :c:type:`v4l2_window` substructure named ``win``. It is not
possible to retrieve a previously programmed clipping list or bitmap.

To program the source rectangle applications set the ``type`` field of a
struct :c:type:`v4l2_format` to
``V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY``, initialize the ``win``
substructure and call the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl.
The driver adjusts the parameters against hardware limits and returns
the actual parameters as :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` does. Like :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>`,
the :ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>` ioctl can be used to learn
about driver capabilities without actually changing driver state. Unlike
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` this also works after the overlay has been enabled.

A struct :c:type:`v4l2_crop` defines the size and position
of the target rectangle. The scaling factor of the overlay is implied by
the width and height given in struct :c:type:`v4l2_window`
and struct :c:type:`v4l2_crop`. The cropping API applies to
*Video Output* and *Video Output Overlay* devices in the same way as to
*Video Capture* and *Video Overlay* devices, merely reversing the
direction of the data flow. For more information see :ref:`crop`.


Enabling Overlay
================

There is no V4L2 ioctl to enable or disable the overlay, however the
framebuffer interface of the driver may support the ``FBIOBLANK`` ioctl.
