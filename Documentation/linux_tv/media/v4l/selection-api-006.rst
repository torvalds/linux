.. -*- coding: utf-8; mode: rst -*-

********
Examples
********

(A video capture device is assumed; change
``V4L2_BUF_TYPE_VIDEO_CAPTURE`` for other devices; change target to
``V4L2_SEL_TGT_COMPOSE_*`` family to configure composing area)


.. code-block:: c
	:caption: Example 1.15. Resetting the cropping parameters

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


.. code-block:: c
   :caption: Example 1.16. Simple downscaling

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


.. code-block:: c
   :caption: Example 1.17. Querying for scaling factors

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




.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
