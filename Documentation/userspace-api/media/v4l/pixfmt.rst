.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _pixfmt:

#############
Image Formats
#############
The V4L2 API was primarily designed for devices exchanging image data
with applications. The struct :c:type:`v4l2_pix_format` and
struct :c:type:`v4l2_pix_format_mplane` structures define the
format and layout of an image in memory. The former is used with the
single-planar API, while the latter is used with the multi-planar
version (see :ref:`planar-apis`). Image formats are negotiated with
the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl. (The explanations here
focus on video capturing and output, for overlay frame buffer formats
see also :ref:`VIDIOC_G_FBUF <VIDIOC_G_FBUF>`.)


.. toctree::
    :maxdepth: 1

    pixfmt-v4l2
    pixfmt-v4l2-mplane
    pixfmt-intro
    pixfmt-indexed
    pixfmt-rgb
    pixfmt-bayer
    yuv-formats
    hsv-formats
    depth-formats
    pixfmt-compressed
    sdr-formats
    tch-formats
    meta-formats
    pixfmt-reserved
    colorspaces
    colorspaces-defs
    colorspaces-details
