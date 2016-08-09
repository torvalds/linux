.. -*- coding: utf-8; mode: rst -*-

.. _pixfmt:

#############
Image Formats
#############
The V4L2 API was primarily designed for devices exchanging image data
with applications. The :ref:`struct v4l2_pix_format <v4l2-pix-format>` and
:ref:`struct v4l2_pix_format_mplane <v4l2-pix-format-mplane>` structures define the
format and layout of an image in memory. The former is used with the
single-planar API, while the latter is used with the multi-planar
version (see :ref:`planar-apis`). Image formats are negotiated with
the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl. (The explanations here
focus on video capturing and output, for overlay frame buffer formats
see also :ref:`VIDIOC_G_FBUF <VIDIOC_G_FBUF>`.)


.. toctree::
    :maxdepth: 1

    pixfmt-002
    pixfmt-003
    pixfmt-004
    colorspaces
    pixfmt-006
    pixfmt-007
    pixfmt-008
    pixfmt-indexed
    pixfmt-rgb
    yuv-formats
    depth-formats
    pixfmt-013
    sdr-formats
    pixfmt-reserved
