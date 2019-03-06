.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

********
Examples
********

(A video capture device is assumed; change
``V4L2_BUF_TYPE_VIDEO_CAPTURE`` for other devices; change target to
``V4L2_SEL_TGT_COMPOSE_*`` family to configure composing area)

Example: Resetting the cropping parameters
==========================================

.. code-block:: c

	struct v4l2_selection sel = {
	    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	    .target = V4L2_SEL_TGT_CROP_DEFAULT,
	};
	ret = ioctl(fd, VIDIOC_G_SELECTION, &sel);
	if (ret)
	    exit(-1);
	sel.target = V4L2_SEL_TGT_CROP;
	ret = ioctl(fd, VIDIOC_S_SELECTION, &sel);
	if (ret)
	    exit(-1);

Setting a composing area on output of size of *at most* half of limit
placed at a center of a display.

Example: Simple downscaling
===========================

.. code-block:: c

	struct v4l2_selection sel = {
	    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
	    .target = V4L2_SEL_TGT_COMPOSE_BOUNDS,
	};
	struct v4l2_rect r;

	ret = ioctl(fd, VIDIOC_G_SELECTION, &sel);
	if (ret)
	    exit(-1);
	/* setting smaller compose rectangle */
	r.width = sel.r.width / 2;
	r.height = sel.r.height / 2;
	r.left = sel.r.width / 4;
	r.top = sel.r.height / 4;
	sel.r = r;
	sel.target = V4L2_SEL_TGT_COMPOSE;
	sel.flags = V4L2_SEL_FLAG_LE;
	ret = ioctl(fd, VIDIOC_S_SELECTION, &sel);
	if (ret)
	    exit(-1);

A video output device is assumed; change ``V4L2_BUF_TYPE_VIDEO_OUTPUT``
for other devices

Example: Querying for scaling factors
=====================================

.. code-block:: c

	struct v4l2_selection compose = {
	    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
	    .target = V4L2_SEL_TGT_COMPOSE,
	};
	struct v4l2_selection crop = {
	    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
	    .target = V4L2_SEL_TGT_CROP,
	};
	double hscale, vscale;

	ret = ioctl(fd, VIDIOC_G_SELECTION, &compose);
	if (ret)
	    exit(-1);
	ret = ioctl(fd, VIDIOC_G_SELECTION, &crop);
	if (ret)
	    exit(-1);

	/* computing scaling factors */
	hscale = (double)compose.r.width / crop.r.width;
	vscale = (double)compose.r.height / crop.r.height;
