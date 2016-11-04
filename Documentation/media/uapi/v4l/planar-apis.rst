.. -*- coding: utf-8; mode: rst -*-

.. _planar-apis:

*****************************
Single- and multi-planar APIs
*****************************

Some devices require data for each input or output video frame to be
placed in discontiguous memory buffers. In such cases, one video frame
has to be addressed using more than one memory address, i.e. one pointer
per "plane". A plane is a sub-buffer of the current frame. For examples
of such formats see :ref:`pixfmt`.

Initially, V4L2 API did not support multi-planar buffers and a set of
extensions has been introduced to handle them. Those extensions
constitute what is being referred to as the "multi-planar API".

Some of the V4L2 API calls and structures are interpreted differently,
depending on whether single- or multi-planar API is being used. An
application can choose whether to use one or the other by passing a
corresponding buffer type to its ioctl calls. Multi-planar versions of
buffer types are suffixed with an ``_MPLANE`` string. For a list of
available multi-planar buffer types see enum
:c:type:`v4l2_buf_type`.


Multi-planar formats
====================

Multi-planar API introduces new multi-planar formats. Those formats use
a separate set of FourCC codes. It is important to distinguish between
the multi-planar API and a multi-planar format. Multi-planar API calls
can handle all single-planar formats as well (as long as they are passed
in multi-planar API structures), while the single-planar API cannot
handle multi-planar formats.


Calls that distinguish between single and multi-planar APIs
===========================================================

:ref:`VIDIOC_QUERYCAP <VIDIOC_QUERYCAP>`
    Two additional multi-planar capabilities are added. They can be set
    together with non-multi-planar ones for devices that handle both
    single- and multi-planar formats.

:ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>`, :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>`, :ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>`
    New structures for describing multi-planar formats are added: struct
    :c:type:`v4l2_pix_format_mplane` and
    struct :c:type:`v4l2_plane_pix_format`.
    Drivers may define new multi-planar formats, which have distinct
    FourCC codes from the existing single-planar ones.

:ref:`VIDIOC_QBUF <VIDIOC_QBUF>`, :ref:`VIDIOC_DQBUF <VIDIOC_QBUF>`, :ref:`VIDIOC_QUERYBUF <VIDIOC_QUERYBUF>`
    A new struct :c:type:`v4l2_plane` structure for
    describing planes is added. Arrays of this structure are passed in
    the new ``m.planes`` field of struct
    :c:type:`v4l2_buffer`.

:ref:`VIDIOC_REQBUFS <VIDIOC_REQBUFS>`
    Will allocate multi-planar buffers as requested.
