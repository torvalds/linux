.. -*- coding: utf-8; mode: rst -*-

.. _diff-v4l:

********************************
Differences between V4L and V4L2
********************************

The Video For Linux API was first introduced in Linux 2.1 to unify and
replace various TV and radio device related interfaces, developed
independently by driver writers in prior years. Starting with Linux 2.5
the much improved V4L2 API replaces the V4L API. The support for the old
V4L calls were removed from Kernel, but the library :ref:`libv4l`
supports the conversion of a V4L API system call into a V4L2 one.


Opening and Closing Devices
===========================

For compatibility reasons the character device file names recommended
for V4L2 video capture, overlay, radio and raw vbi capture devices did
not change from those used by V4L. They are listed in :ref:`devices`
and below in :ref:`v4l-dev`.

The teletext devices (minor range 192-223) have been removed in V4L2 and
no longer exist. There is no hardware available anymore for handling
pure teletext. Instead raw or sliced VBI is used.

The V4L ``videodev`` module automatically assigns minor numbers to
drivers in load order, depending on the registered device type. We
recommend that V4L2 drivers by default register devices with the same
numbers, but the system administrator can assign arbitrary minor numbers
using driver module options. The major device number remains 81.


.. _v4l-dev:

.. flat-table:: V4L Device Types, Names and Numbers
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  Device Type

       -  File Name

       -  Minor Numbers

    -  .. row 2

       -  Video capture and overlay

       -  ``/dev/video`` and ``/dev/bttv0``\  [#f1]_, ``/dev/video0`` to
	  ``/dev/video63``

       -  0-63

    -  .. row 3

       -  Radio receiver

       -  ``/dev/radio``\  [#f2]_, ``/dev/radio0`` to ``/dev/radio63``

       -  64-127

    -  .. row 4

       -  Raw VBI capture

       -  ``/dev/vbi``, ``/dev/vbi0`` to ``/dev/vbi31``

       -  224-255


V4L prohibits (or used to prohibit) multiple opens of a device file.
V4L2 drivers *may* support multiple opens, see :ref:`open` for details
and consequences.

V4L drivers respond to V4L2 ioctls with an ``EINVAL`` error code.


Querying Capabilities
=====================

The V4L ``VIDIOCGCAP`` ioctl is equivalent to V4L2's
:ref:`VIDIOC_QUERYCAP`.

The ``name`` field in struct :c:type:`struct video_capability` became
``card`` in struct :ref:`v4l2_capability <v4l2-capability>`, ``type``
was replaced by ``capabilities``. Note V4L2 does not distinguish between
device types like this, better think of basic video input, video output
and radio devices supporting a set of related functions like video
capturing, video overlay and VBI capturing. See :ref:`open` for an
introduction.



.. flat-table::
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  struct :c:type:`struct video_capability` ``type``

       -  struct :ref:`v4l2_capability <v4l2-capability>`
	  ``capabilities`` flags

       -  Purpose

    -  .. row 2

       -  ``VID_TYPE_CAPTURE``

       -  ``V4L2_CAP_VIDEO_CAPTURE``

       -  The :ref:`video capture <capture>` interface is supported.

    -  .. row 3

       -  ``VID_TYPE_TUNER``

       -  ``V4L2_CAP_TUNER``

       -  The device has a :ref:`tuner or modulator <tuner>`.

    -  .. row 4

       -  ``VID_TYPE_TELETEXT``

       -  ``V4L2_CAP_VBI_CAPTURE``

       -  The :ref:`raw VBI capture <raw-vbi>` interface is supported.

    -  .. row 5

       -  ``VID_TYPE_OVERLAY``

       -  ``V4L2_CAP_VIDEO_OVERLAY``

       -  The :ref:`video overlay <overlay>` interface is supported.

    -  .. row 6

       -  ``VID_TYPE_CHROMAKEY``

       -  ``V4L2_FBUF_CAP_CHROMAKEY`` in field ``capability`` of struct
	  :ref:`v4l2_framebuffer <v4l2-framebuffer>`

       -  Whether chromakey overlay is supported. For more information on
	  overlay see :ref:`overlay`.

    -  .. row 7

       -  ``VID_TYPE_CLIPPING``

       -  ``V4L2_FBUF_CAP_LIST_CLIPPING`` and
	  ``V4L2_FBUF_CAP_BITMAP_CLIPPING`` in field ``capability`` of
	  struct :ref:`v4l2_framebuffer <v4l2-framebuffer>`

       -  Whether clipping the overlaid image is supported, see
	  :ref:`overlay`.

    -  .. row 8

       -  ``VID_TYPE_FRAMERAM``

       -  ``V4L2_FBUF_CAP_EXTERNOVERLAY`` *not set* in field ``capability``
	  of struct :ref:`v4l2_framebuffer <v4l2-framebuffer>`

       -  Whether overlay overwrites frame buffer memory, see
	  :ref:`overlay`.

    -  .. row 9

       -  ``VID_TYPE_SCALES``

       -  ``-``

       -  This flag indicates if the hardware can scale images. The V4L2 API
	  implies the scale factor by setting the cropping dimensions and
	  image size with the :ref:`VIDIOC_S_CROP <VIDIOC_G_CROP>` and
	  :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl, respectively. The
	  driver returns the closest sizes possible. For more information on
	  cropping and scaling see :ref:`crop`.

    -  .. row 10

       -  ``VID_TYPE_MONOCHROME``

       -  ``-``

       -  Applications can enumerate the supported image formats with the
	  :ref:`VIDIOC_ENUM_FMT` ioctl to determine if
	  the device supports grey scale capturing only. For more
	  information on image formats see :ref:`pixfmt`.

    -  .. row 11

       -  ``VID_TYPE_SUBCAPTURE``

       -  ``-``

       -  Applications can call the :ref:`VIDIOC_G_CROP <VIDIOC_G_CROP>`
	  ioctl to determine if the device supports capturing a subsection
	  of the full picture ("cropping" in V4L2). If not, the ioctl
	  returns the ``EINVAL`` error code. For more information on cropping
	  and scaling see :ref:`crop`.

    -  .. row 12

       -  ``VID_TYPE_MPEG_DECODER``

       -  ``-``

       -  Applications can enumerate the supported image formats with the
	  :ref:`VIDIOC_ENUM_FMT` ioctl to determine if
	  the device supports MPEG streams.

    -  .. row 13

       -  ``VID_TYPE_MPEG_ENCODER``

       -  ``-``

       -  See above.

    -  .. row 14

       -  ``VID_TYPE_MJPEG_DECODER``

       -  ``-``

       -  See above.

    -  .. row 15

       -  ``VID_TYPE_MJPEG_ENCODER``

       -  ``-``

       -  See above.


The ``audios`` field was replaced by ``capabilities`` flag
``V4L2_CAP_AUDIO``, indicating *if* the device has any audio inputs or
outputs. To determine their number applications can enumerate audio
inputs with the :ref:`VIDIOC_G_AUDIO <VIDIOC_G_AUDIO>` ioctl. The
audio ioctls are described in :ref:`audio`.

The ``maxwidth``, ``maxheight``, ``minwidth`` and ``minheight`` fields
were removed. Calling the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` or
:ref:`VIDIOC_TRY_FMT <VIDIOC_G_FMT>` ioctl with the desired
dimensions returns the closest size possible, taking into account the
current video standard, cropping and scaling limitations.


Video Sources
=============

V4L provides the ``VIDIOCGCHAN`` and ``VIDIOCSCHAN`` ioctl using struct
:c:type:`struct video_channel` to enumerate the video inputs of a V4L
device. The equivalent V4L2 ioctls are
:ref:`VIDIOC_ENUMINPUT`,
:ref:`VIDIOC_G_INPUT <VIDIOC_G_INPUT>` and
:ref:`VIDIOC_S_INPUT <VIDIOC_G_INPUT>` using struct
:ref:`v4l2_input <v4l2-input>` as discussed in :ref:`video`.

The ``channel`` field counting inputs was renamed to ``index``, the
video input types were renamed as follows:



.. flat-table::
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  struct :c:type:`struct video_channel` ``type``

       -  struct :ref:`v4l2_input <v4l2-input>` ``type``

    -  .. row 2

       -  ``VIDEO_TYPE_TV``

       -  ``V4L2_INPUT_TYPE_TUNER``

    -  .. row 3

       -  ``VIDEO_TYPE_CAMERA``

       -  ``V4L2_INPUT_TYPE_CAMERA``


Unlike the ``tuners`` field expressing the number of tuners of this
input, V4L2 assumes each video input is connected to at most one tuner.
However a tuner can have more than one input, i. e. RF connectors, and a
device can have multiple tuners. The index number of the tuner
associated with the input, if any, is stored in field ``tuner`` of
struct :ref:`v4l2_input <v4l2-input>`. Enumeration of tuners is
discussed in :ref:`tuner`.

The redundant ``VIDEO_VC_TUNER`` flag was dropped. Video inputs
associated with a tuner are of type ``V4L2_INPUT_TYPE_TUNER``. The
``VIDEO_VC_AUDIO`` flag was replaced by the ``audioset`` field. V4L2
considers devices with up to 32 audio inputs. Each set bit in the
``audioset`` field represents one audio input this video input combines
with. For information about audio inputs and how to switch between them
see :ref:`audio`.

The ``norm`` field describing the supported video standards was replaced
by ``std``. The V4L specification mentions a flag ``VIDEO_VC_NORM``
indicating whether the standard can be changed. This flag was a later
addition together with the ``norm`` field and has been removed in the
meantime. V4L2 has a similar, albeit more comprehensive approach to
video standards, see :ref:`standard` for more information.


Tuning
======

The V4L ``VIDIOCGTUNER`` and ``VIDIOCSTUNER`` ioctl and struct
:c:type:`struct video_tuner` can be used to enumerate the tuners of a
V4L TV or radio device. The equivalent V4L2 ioctls are
:ref:`VIDIOC_G_TUNER <VIDIOC_G_TUNER>` and
:ref:`VIDIOC_S_TUNER <VIDIOC_G_TUNER>` using struct
:ref:`v4l2_tuner <v4l2-tuner>`. Tuners are covered in :ref:`tuner`.

The ``tuner`` field counting tuners was renamed to ``index``. The fields
``name``, ``rangelow`` and ``rangehigh`` remained unchanged.

The ``VIDEO_TUNER_PAL``, ``VIDEO_TUNER_NTSC`` and ``VIDEO_TUNER_SECAM``
flags indicating the supported video standards were dropped. This
information is now contained in the associated struct
:ref:`v4l2_input <v4l2-input>`. No replacement exists for the
``VIDEO_TUNER_NORM`` flag indicating whether the video standard can be
switched. The ``mode`` field to select a different video standard was
replaced by a whole new set of ioctls and structures described in
:ref:`standard`. Due to its ubiquity it should be mentioned the BTTV
driver supports several standards in addition to the regular
``VIDEO_MODE_PAL`` (0), ``VIDEO_MODE_NTSC``, ``VIDEO_MODE_SECAM`` and
``VIDEO_MODE_AUTO`` (3). Namely N/PAL Argentina, M/PAL, N/PAL, and NTSC
Japan with numbers 3-6 (sic).

The ``VIDEO_TUNER_STEREO_ON`` flag indicating stereo reception became
``V4L2_TUNER_SUB_STEREO`` in field ``rxsubchans``. This field also
permits the detection of monaural and bilingual audio, see the
definition of struct :ref:`v4l2_tuner <v4l2-tuner>` for details.
Presently no replacement exists for the ``VIDEO_TUNER_RDS_ON`` and
``VIDEO_TUNER_MBS_ON`` flags.

The ``VIDEO_TUNER_LOW`` flag was renamed to ``V4L2_TUNER_CAP_LOW`` in
the struct :ref:`v4l2_tuner <v4l2-tuner>` ``capability`` field.

The ``VIDIOCGFREQ`` and ``VIDIOCSFREQ`` ioctl to change the tuner
frequency where renamed to
:ref:`VIDIOC_G_FREQUENCY <VIDIOC_G_FREQUENCY>` and
:ref:`VIDIOC_S_FREQUENCY <VIDIOC_G_FREQUENCY>`. They take a pointer
to a struct :ref:`v4l2_frequency <v4l2-frequency>` instead of an
unsigned long integer.


.. _v4l-image-properties:

Image Properties
================

V4L2 has no equivalent of the ``VIDIOCGPICT`` and ``VIDIOCSPICT`` ioctl
and struct :c:type:`struct video_picture`. The following fields where
replaced by V4L2 controls accessible with the
:ref:`VIDIOC_QUERYCTRL`,
:ref:`VIDIOC_G_CTRL <VIDIOC_G_CTRL>` and
:ref:`VIDIOC_S_CTRL <VIDIOC_G_CTRL>` ioctls:



.. flat-table::
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  struct :c:type:`struct video_picture`

       -  V4L2 Control ID

    -  .. row 2

       -  ``brightness``

       -  ``V4L2_CID_BRIGHTNESS``

    -  .. row 3

       -  ``hue``

       -  ``V4L2_CID_HUE``

    -  .. row 4

       -  ``colour``

       -  ``V4L2_CID_SATURATION``

    -  .. row 5

       -  ``contrast``

       -  ``V4L2_CID_CONTRAST``

    -  .. row 6

       -  ``whiteness``

       -  ``V4L2_CID_WHITENESS``


The V4L picture controls are assumed to range from 0 to 65535 with no
particular reset value. The V4L2 API permits arbitrary limits and
defaults which can be queried with the
:ref:`VIDIOC_QUERYCTRL` ioctl. For general
information about controls see :ref:`control`.

The ``depth`` (average number of bits per pixel) of a video image is
implied by the selected image format. V4L2 does not explicitly provide
such information assuming applications recognizing the format are aware
of the image depth and others need not know. The ``palette`` field moved
into the struct :ref:`v4l2_pix_format <v4l2-pix-format>`:



.. flat-table::
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  struct :c:type:`struct video_picture` ``palette``

       -  struct :ref:`v4l2_pix_format <v4l2-pix-format>` ``pixfmt``

    -  .. row 2

       -  ``VIDEO_PALETTE_GREY``

       -  :ref:`V4L2_PIX_FMT_GREY <V4L2-PIX-FMT-GREY>`

    -  .. row 3

       -  ``VIDEO_PALETTE_HI240``

       -  :ref:`V4L2_PIX_FMT_HI240 <pixfmt-reserved>` [#f3]_

    -  .. row 4

       -  ``VIDEO_PALETTE_RGB565``

       -  :ref:`V4L2_PIX_FMT_RGB565 <pixfmt-rgb>`

    -  .. row 5

       -  ``VIDEO_PALETTE_RGB555``

       -  :ref:`V4L2_PIX_FMT_RGB555 <pixfmt-rgb>`

    -  .. row 6

       -  ``VIDEO_PALETTE_RGB24``

       -  :ref:`V4L2_PIX_FMT_BGR24 <pixfmt-rgb>`

    -  .. row 7

       -  ``VIDEO_PALETTE_RGB32``

       -  :ref:`V4L2_PIX_FMT_BGR32 <pixfmt-rgb>` [#f4]_

    -  .. row 8

       -  ``VIDEO_PALETTE_YUV422``

       -  :ref:`V4L2_PIX_FMT_YUYV <V4L2-PIX-FMT-YUYV>`

    -  .. row 9

       -  ``VIDEO_PALETTE_YUYV``\  [#f5]_

       -  :ref:`V4L2_PIX_FMT_YUYV <V4L2-PIX-FMT-YUYV>`

    -  .. row 10

       -  ``VIDEO_PALETTE_UYVY``

       -  :ref:`V4L2_PIX_FMT_UYVY <V4L2-PIX-FMT-UYVY>`

    -  .. row 11

       -  ``VIDEO_PALETTE_YUV420``

       -  None

    -  .. row 12

       -  ``VIDEO_PALETTE_YUV411``

       -  :ref:`V4L2_PIX_FMT_Y41P <V4L2-PIX-FMT-Y41P>` [#f6]_

    -  .. row 13

       -  ``VIDEO_PALETTE_RAW``

       -  None [#f7]_

    -  .. row 14

       -  ``VIDEO_PALETTE_YUV422P``

       -  :ref:`V4L2_PIX_FMT_YUV422P <V4L2-PIX-FMT-YUV422P>`

    -  .. row 15

       -  ``VIDEO_PALETTE_YUV411P``

       -  :ref:`V4L2_PIX_FMT_YUV411P <V4L2-PIX-FMT-YUV411P>` [#f8]_

    -  .. row 16

       -  ``VIDEO_PALETTE_YUV420P``

       -  :ref:`V4L2_PIX_FMT_YVU420 <V4L2-PIX-FMT-YVU420>`

    -  .. row 17

       -  ``VIDEO_PALETTE_YUV410P``

       -  :ref:`V4L2_PIX_FMT_YVU410 <V4L2-PIX-FMT-YVU410>`


V4L2 image formats are defined in :ref:`pixfmt`. The image format can
be selected with the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl.


Audio
=====

The ``VIDIOCGAUDIO`` and ``VIDIOCSAUDIO`` ioctl and struct
:c:type:`struct video_audio` are used to enumerate the audio inputs
of a V4L device. The equivalent V4L2 ioctls are
:ref:`VIDIOC_G_AUDIO <VIDIOC_G_AUDIO>` and
:ref:`VIDIOC_S_AUDIO <VIDIOC_G_AUDIO>` using struct
:ref:`v4l2_audio <v4l2-audio>` as discussed in :ref:`audio`.

The ``audio`` "channel number" field counting audio inputs was renamed
to ``index``.

On ``VIDIOCSAUDIO`` the ``mode`` field selects *one* of the
``VIDEO_SOUND_MONO``, ``VIDEO_SOUND_STEREO``, ``VIDEO_SOUND_LANG1`` or
``VIDEO_SOUND_LANG2`` audio demodulation modes. When the current audio
standard is BTSC ``VIDEO_SOUND_LANG2`` refers to SAP and
``VIDEO_SOUND_LANG1`` is meaningless. Also undocumented in the V4L
specification, there is no way to query the selected mode. On
``VIDIOCGAUDIO`` the driver returns the *actually received* audio
programmes in this field. In the V4L2 API this information is stored in
the struct :ref:`v4l2_tuner <v4l2-tuner>` ``rxsubchans`` and
``audmode`` fields, respectively. See :ref:`tuner` for more
information on tuners. Related to audio modes struct
:ref:`v4l2_audio <v4l2-audio>` also reports if this is a mono or
stereo input, regardless if the source is a tuner.

The following fields where replaced by V4L2 controls accessible with the
:ref:`VIDIOC_QUERYCTRL`,
:ref:`VIDIOC_G_CTRL <VIDIOC_G_CTRL>` and
:ref:`VIDIOC_S_CTRL <VIDIOC_G_CTRL>` ioctls:



.. flat-table::
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  struct :c:type:`struct video_audio`

       -  V4L2 Control ID

    -  .. row 2

       -  ``volume``

       -  ``V4L2_CID_AUDIO_VOLUME``

    -  .. row 3

       -  ``bass``

       -  ``V4L2_CID_AUDIO_BASS``

    -  .. row 4

       -  ``treble``

       -  ``V4L2_CID_AUDIO_TREBLE``

    -  .. row 5

       -  ``balance``

       -  ``V4L2_CID_AUDIO_BALANCE``


To determine which of these controls are supported by a driver V4L
provides the ``flags`` ``VIDEO_AUDIO_VOLUME``, ``VIDEO_AUDIO_BASS``,
``VIDEO_AUDIO_TREBLE`` and ``VIDEO_AUDIO_BALANCE``. In the V4L2 API the
:ref:`VIDIOC_QUERYCTRL` ioctl reports if the
respective control is supported. Accordingly the ``VIDEO_AUDIO_MUTABLE``
and ``VIDEO_AUDIO_MUTE`` flags where replaced by the boolean
``V4L2_CID_AUDIO_MUTE`` control.

All V4L2 controls have a ``step`` attribute replacing the struct
:c:type:`struct video_audio` ``step`` field. The V4L audio controls
are assumed to range from 0 to 65535 with no particular reset value. The
V4L2 API permits arbitrary limits and defaults which can be queried with
the :ref:`VIDIOC_QUERYCTRL` ioctl. For general
information about controls see :ref:`control`.


Frame Buffer Overlay
====================

The V4L2 ioctls equivalent to ``VIDIOCGFBUF`` and ``VIDIOCSFBUF`` are
:ref:`VIDIOC_G_FBUF <VIDIOC_G_FBUF>` and
:ref:`VIDIOC_S_FBUF <VIDIOC_G_FBUF>`. The ``base`` field of struct
:c:type:`struct video_buffer` remained unchanged, except V4L2 defines
a flag to indicate non-destructive overlays instead of a ``NULL``
pointer. All other fields moved into the struct
:ref:`v4l2_pix_format <v4l2-pix-format>` ``fmt`` substructure of
struct :ref:`v4l2_framebuffer <v4l2-framebuffer>`. The ``depth``
field was replaced by ``pixelformat``. See :ref:`pixfmt-rgb` for a
list of RGB formats and their respective color depths.

Instead of the special ioctls ``VIDIOCGWIN`` and ``VIDIOCSWIN`` V4L2
uses the general-purpose data format negotiation ioctls
:ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` and
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>`. They take a pointer to a struct
:ref:`v4l2_format <v4l2-format>` as argument. Here the ``win`` member
of the ``fmt`` union is used, a struct
:ref:`v4l2_window <v4l2-window>`.

The ``x``, ``y``, ``width`` and ``height`` fields of struct
:c:type:`struct video_window` moved into struct
:ref:`v4l2_rect <v4l2-rect>` substructure ``w`` of struct
:c:type:`struct v4l2_window`. The ``chromakey``, ``clips``, and
``clipcount`` fields remained unchanged. Struct
:c:type:`struct video_clip` was renamed to struct
:ref:`v4l2_clip <v4l2-clip>`, also containing a struct
:c:type:`struct v4l2_rect`, but the semantics are still the same.

The ``VIDEO_WINDOW_INTERLACE`` flag was dropped. Instead applications
must set the ``field`` field to ``V4L2_FIELD_ANY`` or
``V4L2_FIELD_INTERLACED``. The ``VIDEO_WINDOW_CHROMAKEY`` flag moved
into struct :ref:`v4l2_framebuffer <v4l2-framebuffer>`, under the new
name ``V4L2_FBUF_FLAG_CHROMAKEY``.

In V4L, storing a bitmap pointer in ``clips`` and setting ``clipcount``
to ``VIDEO_CLIP_BITMAP`` (-1) requests bitmap clipping, using a fixed
size bitmap of 1024 × 625 bits. Struct :c:type:`struct v4l2_window`
has a separate ``bitmap`` pointer field for this purpose and the bitmap
size is determined by ``w.width`` and ``w.height``.

The ``VIDIOCCAPTURE`` ioctl to enable or disable overlay was renamed to
:ref:`VIDIOC_OVERLAY`.


Cropping
========

To capture only a subsection of the full picture V4L defines the
``VIDIOCGCAPTURE`` and ``VIDIOCSCAPTURE`` ioctls using struct
:c:type:`struct video_capture`. The equivalent V4L2 ioctls are
:ref:`VIDIOC_G_CROP <VIDIOC_G_CROP>` and
:ref:`VIDIOC_S_CROP <VIDIOC_G_CROP>` using struct
:ref:`v4l2_crop <v4l2-crop>`, and the related
:ref:`VIDIOC_CROPCAP` ioctl. This is a rather
complex matter, see :ref:`crop` for details.

The ``x``, ``y``, ``width`` and ``height`` fields moved into struct
:ref:`v4l2_rect <v4l2-rect>` substructure ``c`` of struct
:c:type:`struct v4l2_crop`. The ``decimation`` field was dropped. In
the V4L2 API the scaling factor is implied by the size of the cropping
rectangle and the size of the captured or overlaid image.

The ``VIDEO_CAPTURE_ODD`` and ``VIDEO_CAPTURE_EVEN`` flags to capture
only the odd or even field, respectively, were replaced by
``V4L2_FIELD_TOP`` and ``V4L2_FIELD_BOTTOM`` in the field named
``field`` of struct :ref:`v4l2_pix_format <v4l2-pix-format>` and
struct :ref:`v4l2_window <v4l2-window>`. These structures are used to
select a capture or overlay format with the
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl.


Reading Images, Memory Mapping
==============================


Capturing using the read method
-------------------------------

There is no essential difference between reading images from a V4L or
V4L2 device using the :ref:`read() <func-read>` function, however V4L2
drivers are not required to support this I/O method. Applications can
determine if the function is available with the
:ref:`VIDIOC_QUERYCAP` ioctl. All V4L2 devices
exchanging data with applications must support the
:ref:`select() <func-select>` and :ref:`poll() <func-poll>`
functions.

To select an image format and size, V4L provides the ``VIDIOCSPICT`` and
``VIDIOCSWIN`` ioctls. V4L2 uses the general-purpose data format
negotiation ioctls :ref:`VIDIOC_G_FMT <VIDIOC_G_FMT>` and
:ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>`. They take a pointer to a struct
:ref:`v4l2_format <v4l2-format>` as argument, here the struct
:ref:`v4l2_pix_format <v4l2-pix-format>` named ``pix`` of its
``fmt`` union is used.

For more information about the V4L2 read interface see :ref:`rw`.


Capturing using memory mapping
------------------------------

Applications can read from V4L devices by mapping buffers in device
memory, or more often just buffers allocated in DMA-able system memory,
into their address space. This avoids the data copying overhead of the
read method. V4L2 supports memory mapping as well, with a few
differences.



.. flat-table::
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  V4L

       -  V4L2

    -  .. row 2

       -
       -  The image format must be selected before buffers are allocated,
	  with the :ref:`VIDIOC_S_FMT <VIDIOC_G_FMT>` ioctl. When no
	  format is selected the driver may use the last, possibly by
	  another application requested format.

    -  .. row 3

       -  Applications cannot change the number of buffers. The it is built
	  into the driver, unless it has a module option to change the
	  number when the driver module is loaded.

       -  The :ref:`VIDIOC_REQBUFS` ioctl allocates the
	  desired number of buffers, this is a required step in the
	  initialization sequence.

    -  .. row 4

       -  Drivers map all buffers as one contiguous range of memory. The
	  ``VIDIOCGMBUF`` ioctl is available to query the number of buffers,
	  the offset of each buffer from the start of the virtual file, and
	  the overall amount of memory used, which can be used as arguments
	  for the :ref:`mmap() <func-mmap>` function.

       -  Buffers are individually mapped. The offset and size of each
	  buffer can be determined with the
	  :ref:`VIDIOC_QUERYBUF` ioctl.

    -  .. row 5

       -  The ``VIDIOCMCAPTURE`` ioctl prepares a buffer for capturing. It
	  also determines the image format for this buffer. The ioctl
	  returns immediately, eventually with an ``EAGAIN`` error code if no
	  video signal had been detected. When the driver supports more than
	  one buffer applications can call the ioctl multiple times and thus
	  have multiple outstanding capture requests.

	  The ``VIDIOCSYNC`` ioctl suspends execution until a particular
	  buffer has been filled.

       -  Drivers maintain an incoming and outgoing queue.
	  :ref:`VIDIOC_QBUF` enqueues any empty buffer into
	  the incoming queue. Filled buffers are dequeued from the outgoing
	  queue with the :ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctl. To wait
	  until filled buffers become available this function,
	  :ref:`select() <func-select>` or :ref:`poll() <func-poll>` can
	  be used. The :ref:`VIDIOC_STREAMON` ioctl
	  must be called once after enqueuing one or more buffers to start
	  capturing. Its counterpart
	  :ref:`VIDIOC_STREAMOFF <VIDIOC_STREAMON>` stops capturing and
	  dequeues all buffers from both queues. Applications can query the
	  signal status, if known, with the
	  :ref:`VIDIOC_ENUMINPUT` ioctl.


For a more in-depth discussion of memory mapping and examples, see
:ref:`mmap`.


Reading Raw VBI Data
====================

Originally the V4L API did not specify a raw VBI capture interface, only
the device file ``/dev/vbi`` was reserved for this purpose. The only
driver supporting this interface was the BTTV driver, de-facto defining
the V4L VBI interface. Reading from the device yields a raw VBI image
with the following parameters:



.. flat-table::
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  struct :ref:`v4l2_vbi_format <v4l2-vbi-format>`

       -  V4L, BTTV driver

    -  .. row 2

       -  sampling_rate

       -  28636363 Hz NTSC (or any other 525-line standard); 35468950 Hz PAL
	  and SECAM (625-line standards)

    -  .. row 3

       -  offset

       -  ?

    -  .. row 4

       -  samples_per_line

       -  2048

    -  .. row 5

       -  sample_format

       -  V4L2_PIX_FMT_GREY. The last four bytes (a machine endianness
	  integer) contain a frame counter.

    -  .. row 6

       -  start[]

       -  10, 273 NTSC; 22, 335 PAL and SECAM

    -  .. row 7

       -  count[]

       -  16, 16 [#f9]_

    -  .. row 8

       -  flags

       -  0


Undocumented in the V4L specification, in Linux 2.3 the
``VIDIOCGVBIFMT`` and ``VIDIOCSVBIFMT`` ioctls using struct
:c:type:`struct vbi_format` were added to determine the VBI image
parameters. These ioctls are only partially compatible with the V4L2 VBI
interface specified in :ref:`raw-vbi`.

An ``offset`` field does not exist, ``sample_format`` is supposed to be
``VIDEO_PALETTE_RAW``, equivalent to ``V4L2_PIX_FMT_GREY``. The
remaining fields are probably equivalent to struct
:ref:`v4l2_vbi_format <v4l2-vbi-format>`.

Apparently only the Zoran (ZR 36120) driver implements these ioctls. The
semantics differ from those specified for V4L2 in two ways. The
parameters are reset on :ref:`open() <func-open>` and
``VIDIOCSVBIFMT`` always returns an ``EINVAL`` error code if the parameters
are invalid.


Miscellaneous
=============

V4L2 has no equivalent of the ``VIDIOCGUNIT`` ioctl. Applications can
find the VBI device associated with a video capture device (or vice
versa) by reopening the device and requesting VBI data. For details see
:ref:`open`.

No replacement exists for ``VIDIOCKEY``, and the V4L functions for
microcode programming. A new interface for MPEG compression and playback
devices is documented in :ref:`extended-controls`.

.. [#f1]
   According to Documentation/devices.txt these should be symbolic links
   to ``/dev/video0``. Note the original bttv interface is not
   compatible with V4L or V4L2.

.. [#f2]
   According to ``Documentation/devices.txt`` a symbolic link to
   ``/dev/radio0``.

.. [#f3]
   This is a custom format used by the BTTV driver, not one of the V4L2
   standard formats.

.. [#f4]
   Presumably all V4L RGB formats are little-endian, although some
   drivers might interpret them according to machine endianness. V4L2
   defines little-endian, big-endian and red/blue swapped variants. For
   details see :ref:`pixfmt-rgb`.

.. [#f5]
   ``VIDEO_PALETTE_YUV422`` and ``VIDEO_PALETTE_YUYV`` are the same
   formats. Some V4L drivers respond to one, some to the other.

.. [#f6]
   Not to be confused with ``V4L2_PIX_FMT_YUV411P``, which is a planar
   format.

.. [#f7]
   V4L explains this as: "RAW capture (BT848)"

.. [#f8]
   Not to be confused with ``V4L2_PIX_FMT_Y41P``, which is a packed
   format.

.. [#f9]
   Old driver versions used different values, eventually the custom
   ``BTTV_VBISIZE`` ioctl was added to query the correct values.
