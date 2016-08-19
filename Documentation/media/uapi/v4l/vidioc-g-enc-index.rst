.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_G_ENC_INDEX:

************************
ioctl VIDIOC_G_ENC_INDEX
************************

Name
====

VIDIOC_G_ENC_INDEX - Get meta data about a compressed video stream


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_G_ENC_INDEX, struct v4l2_enc_idx *argp )
    :name: VIDIOC_G_ENC_INDEX


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``


Description
===========

The :ref:`VIDIOC_G_ENC_INDEX <VIDIOC_G_ENC_INDEX>` ioctl provides meta data about a compressed
video stream the same or another application currently reads from the
driver, which is useful for random access into the stream without
decoding it.

To read the data applications must call :ref:`VIDIOC_G_ENC_INDEX <VIDIOC_G_ENC_INDEX>` with a
pointer to a struct :ref:`v4l2_enc_idx <v4l2-enc-idx>`. On success
the driver fills the ``entry`` array, stores the number of elements
written in the ``entries`` field, and initializes the ``entries_cap``
field.

Each element of the ``entry`` array contains meta data about one
picture. A :ref:`VIDIOC_G_ENC_INDEX <VIDIOC_G_ENC_INDEX>` call reads up to
``V4L2_ENC_IDX_ENTRIES`` entries from a driver buffer, which can hold up
to ``entries_cap`` entries. This number can be lower or higher than
``V4L2_ENC_IDX_ENTRIES``, but not zero. When the application fails to
read the meta data in time the oldest entries will be lost. When the
buffer is empty or no capturing/encoding is in progress, ``entries``
will be zero.

Currently this ioctl is only defined for MPEG-2 program streams and
video elementary streams.


.. tabularcolumns:: |p{3.5cm}|p{5.6cm}|p{8.4cm}|

.. _v4l2-enc-idx:

.. flat-table:: struct v4l2_enc_idx
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 3 8


    -  .. row 1

       -  __u32

       -  ``entries``

       -  The number of entries the driver stored in the ``entry`` array.

    -  .. row 2

       -  __u32

       -  ``entries_cap``

       -  The number of entries the driver can buffer. Must be greater than
	  zero.

    -  .. row 3

       -  __u32

       -  ``reserved``\ [4]

       -  Reserved for future extensions. Drivers must set the
	  array to zero.

    -  .. row 4

       -  struct :ref:`v4l2_enc_idx_entry <v4l2-enc-idx-entry>`

       -  ``entry``\ [``V4L2_ENC_IDX_ENTRIES``]

       -  Meta data about a compressed video stream. Each element of the
	  array corresponds to one picture, sorted in ascending order by
	  their ``offset``.



.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. _v4l2-enc-idx-entry:

.. flat-table:: struct v4l2_enc_idx_entry
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u64

       -  ``offset``

       -  The offset in bytes from the beginning of the compressed video
	  stream to the beginning of this picture, that is a *PES packet
	  header* as defined in :ref:`mpeg2part1` or a *picture header* as
	  defined in :ref:`mpeg2part2`. When the encoder is stopped, the
	  driver resets the offset to zero.

    -  .. row 2

       -  __u64

       -  ``pts``

       -  The 33 bit *Presentation Time Stamp* of this picture as defined in
	  :ref:`mpeg2part1`.

    -  .. row 3

       -  __u32

       -  ``length``

       -  The length of this picture in bytes.

    -  .. row 4

       -  __u32

       -  ``flags``

       -  Flags containing the coding type of this picture, see
	  :ref:`enc-idx-flags`.

    -  .. row 5

       -  __u32

       -  ``reserved``\ [2]

       -  Reserved for future extensions. Drivers must set the array to
	  zero.


.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _enc-idx-flags:

.. flat-table:: Index Entry Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_ENC_IDX_FRAME_I``

       -  0x00

       -  This is an Intra-coded picture.

    -  .. row 2

       -  ``V4L2_ENC_IDX_FRAME_P``

       -  0x01

       -  This is a Predictive-coded picture.

    -  .. row 3

       -  ``V4L2_ENC_IDX_FRAME_B``

       -  0x02

       -  This is a Bidirectionally predictive-coded picture.

    -  .. row 4

       -  ``V4L2_ENC_IDX_FRAME_MASK``

       -  0x0F

       -  *AND* the flags field with this mask to obtain the picture coding
	  type.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
