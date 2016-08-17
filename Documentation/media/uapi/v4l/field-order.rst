.. -*- coding: utf-8; mode: rst -*-

.. _field-order:

***********
Field Order
***********

We have to distinguish between progressive and interlaced video.
Progressive video transmits all lines of a video image sequentially.
Interlaced video divides an image into two fields, containing only the
odd and even lines of the image, respectively. Alternating the so called
odd and even field are transmitted, and due to a small delay between
fields a cathode ray TV displays the lines interleaved, yielding the
original frame. This curious technique was invented because at refresh
rates similar to film the image would fade out too quickly. Transmitting
fields reduces the flicker without the necessity of doubling the frame
rate and with it the bandwidth required for each channel.

It is important to understand a video camera does not expose one frame
at a time, merely transmitting the frames separated into fields. The
fields are in fact captured at two different instances in time. An
object on screen may well move between one field and the next. For
applications analysing motion it is of paramount importance to recognize
which field of a frame is older, the *temporal order*.

When the driver provides or accepts images field by field rather than
interleaved, it is also important applications understand how the fields
combine to frames. We distinguish between top (aka odd) and bottom (aka
even) fields, the *spatial order*: The first line of the top field is
the first line of an interlaced frame, the first line of the bottom
field is the second line of that frame.

However because fields were captured one after the other, arguing
whether a frame commences with the top or bottom field is pointless. Any
two successive top and bottom, or bottom and top fields yield a valid
frame. Only when the source was progressive to begin with, e. g. when
transferring film to video, two fields may come from the same frame,
creating a natural order.

Counter to intuition the top field is not necessarily the older field.
Whether the older field contains the top or bottom lines is a convention
determined by the video standard. Hence the distinction between temporal
and spatial order of fields. The diagrams below should make this
clearer.

All video capture and output devices must report the current field
order. Some drivers may permit the selection of a different order, to
this end applications initialize the ``field`` field of struct
:ref:`v4l2_pix_format <v4l2-pix-format>` before calling the
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl. If this is not desired it
should have the value ``V4L2_FIELD_ANY`` (0).


.. _v4l2-field:

enum v4l2_field
===============

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_FIELD_ANY``

       -  0

       -  Applications request this field order when any one of the
	  ``V4L2_FIELD_NONE``, ``V4L2_FIELD_TOP``, ``V4L2_FIELD_BOTTOM``, or
	  ``V4L2_FIELD_INTERLACED`` formats is acceptable. Drivers choose
	  depending on hardware capabilities or e. g. the requested image
	  size, and return the actual field order. Drivers must never return
	  ``V4L2_FIELD_ANY``. If multiple field orders are possible the
	  driver must choose one of the possible field orders during
	  :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` or
	  :ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>`. struct
	  :ref:`v4l2_buffer <v4l2-buffer>` ``field`` can never be
	  ``V4L2_FIELD_ANY``.

    -  .. row 2

       -  ``V4L2_FIELD_NONE``

       -  1

       -  Images are in progressive format, not interlaced. The driver may
	  also indicate this order when it cannot distinguish between
	  ``V4L2_FIELD_TOP`` and ``V4L2_FIELD_BOTTOM``.

    -  .. row 3

       -  ``V4L2_FIELD_TOP``

       -  2

       -  Images consist of the top (aka odd) field only.

    -  .. row 4

       -  ``V4L2_FIELD_BOTTOM``

       -  3

       -  Images consist of the bottom (aka even) field only. Applications
	  may wish to prevent a device from capturing interlaced images
	  because they will have "comb" or "feathering" artefacts around
	  moving objects.

    -  .. row 5

       -  ``V4L2_FIELD_INTERLACED``

       -  4

       -  Images contain both fields, interleaved line by line. The temporal
	  order of the fields (whether the top or bottom field is first
	  transmitted) depends on the current video standard. M/NTSC
	  transmits the bottom field first, all other standards the top
	  field first.

    -  .. row 6

       -  ``V4L2_FIELD_SEQ_TB``

       -  5

       -  Images contain both fields, the top field lines are stored first
	  in memory, immediately followed by the bottom field lines. Fields
	  are always stored in temporal order, the older one first in
	  memory. Image sizes refer to the frame, not fields.

    -  .. row 7

       -  ``V4L2_FIELD_SEQ_BT``

       -  6

       -  Images contain both fields, the bottom field lines are stored
	  first in memory, immediately followed by the top field lines.
	  Fields are always stored in temporal order, the older one first in
	  memory. Image sizes refer to the frame, not fields.

    -  .. row 8

       -  ``V4L2_FIELD_ALTERNATE``

       -  7

       -  The two fields of a frame are passed in separate buffers, in
	  temporal order, i. e. the older one first. To indicate the field
	  parity (whether the current field is a top or bottom field) the
	  driver or application, depending on data direction, must set
	  struct :ref:`v4l2_buffer <v4l2-buffer>` ``field`` to
	  ``V4L2_FIELD_TOP`` or ``V4L2_FIELD_BOTTOM``. Any two successive
	  fields pair to build a frame. If fields are successive, without
	  any dropped fields between them (fields can drop individually),
	  can be determined from the struct
	  :ref:`v4l2_buffer <v4l2-buffer>` ``sequence`` field. This
	  format cannot be selected when using the read/write I/O method
	  since there is no way to communicate if a field was a top or
	  bottom field.

    -  .. row 9

       -  ``V4L2_FIELD_INTERLACED_TB``

       -  8

       -  Images contain both fields, interleaved line by line, top field
	  first. The top field is transmitted first.

    -  .. row 10

       -  ``V4L2_FIELD_INTERLACED_BT``

       -  9

       -  Images contain both fields, interleaved line by line, top field
	  first. The bottom field is transmitted first.



.. _fieldseq-tb:

Field Order, Top Field First Transmitted
========================================

.. figure::  field-order_files/fieldseq_tb.*
    :alt:    fieldseq_tb.pdf / fieldseq_tb.gif
    :align:  center


.. _fieldseq-bt:

Field Order, Bottom Field First Transmitted
===========================================

.. figure::  field-order_files/fieldseq_bt.*
    :alt:    fieldseq_bt.pdf / fieldseq_bt.gif
    :align:  center

