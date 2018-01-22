.. -*- coding: utf-8; mode: rst -*-

.. _extended-controls:

*****************
Extended Controls
*****************


Introduction
============

The control mechanism as originally designed was meant to be used for
user settings (brightness, saturation, etc). However, it turned out to
be a very useful model for implementing more complicated driver APIs
where each driver implements only a subset of a larger API.

The MPEG encoding API was the driving force behind designing and
implementing this extended control mechanism: the MPEG standard is quite
large and the currently supported hardware MPEG encoders each only
implement a subset of this standard. Further more, many parameters
relating to how the video is encoded into an MPEG stream are specific to
the MPEG encoding chip since the MPEG standard only defines the format
of the resulting MPEG stream, not how the video is actually encoded into
that format.

Unfortunately, the original control API lacked some features needed for
these new uses and so it was extended into the (not terribly originally
named) extended control API.

Even though the MPEG encoding API was the first effort to use the
Extended Control API, nowadays there are also other classes of Extended
Controls, such as Camera Controls and FM Transmitter Controls. The
Extended Controls API as well as all Extended Controls classes are
described in the following text.


The Extended Control API
========================

Three new ioctls are available:
:ref:`VIDIOC_G_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>`,
:ref:`VIDIOC_S_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` and
:ref:`VIDIOC_TRY_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>`. These ioctls act
on arrays of controls (as opposed to the
:ref:`VIDIOC_G_CTRL <VIDIOC_G_CTRL>` and
:ref:`VIDIOC_S_CTRL <VIDIOC_G_CTRL>` ioctls that act on a single
control). This is needed since it is often required to atomically change
several controls at once.

Each of the new ioctls expects a pointer to a struct
:c:type:`v4l2_ext_controls`. This structure
contains a pointer to the control array, a count of the number of
controls in that array and a control class. Control classes are used to
group similar controls into a single class. For example, control class
``V4L2_CTRL_CLASS_USER`` contains all user controls (i. e. all controls
that can also be set using the old :ref:`VIDIOC_S_CTRL <VIDIOC_G_CTRL>`
ioctl). Control class ``V4L2_CTRL_CLASS_MPEG`` contains all controls
relating to MPEG encoding, etc.

All controls in the control array must belong to the specified control
class. An error is returned if this is not the case.

It is also possible to use an empty control array (``count`` == 0) to check
whether the specified control class is supported.

The control array is a struct
:c:type:`v4l2_ext_control` array. The
struct :c:type:`v4l2_ext_control` is very similar to
struct :c:type:`v4l2_control`, except for the fact that
it also allows for 64-bit values and pointers to be passed.

Since the struct :c:type:`v4l2_ext_control` supports
pointers it is now also possible to have controls with compound types
such as N-dimensional arrays and/or structures. You need to specify the
``V4L2_CTRL_FLAG_NEXT_COMPOUND`` when enumerating controls to actually
be able to see such compound controls. In other words, these controls
with compound types should only be used programmatically.

Since such compound controls need to expose more information about
themselves than is possible with
:ref:`VIDIOC_QUERYCTRL` the
:ref:`VIDIOC_QUERY_EXT_CTRL <VIDIOC_QUERYCTRL>` ioctl was added. In
particular, this ioctl gives the dimensions of the N-dimensional array
if this control consists of more than one element.

.. note::

   #. It is important to realize that due to the flexibility of controls it is
      necessary to check whether the control you want to set actually is
      supported in the driver and what the valid range of values is. So use
      the :ref:`VIDIOC_QUERYCTRL` (or :ref:`VIDIOC_QUERY_EXT_CTRL
      <VIDIOC_QUERYCTRL>`) and :ref:`VIDIOC_QUERYMENU <VIDIOC_QUERYCTRL>`
      ioctls to check this.

   #. It is possible that some of the menu indices in a control of
      type ``V4L2_CTRL_TYPE_MENU`` may not be supported (``VIDIOC_QUERYMENU``
      will return an error). A good example is the list of supported MPEG
      audio bitrates. Some drivers only support one or two bitrates, others
      support a wider range.

All controls use machine endianness.


Enumerating Extended Controls
=============================

The recommended way to enumerate over the extended controls is by using
:ref:`VIDIOC_QUERYCTRL` in combination with the
``V4L2_CTRL_FLAG_NEXT_CTRL`` flag:


.. code-block:: c

    struct v4l2_queryctrl qctrl;

    qctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
    while (0 == ioctl (fd, VIDIOC_QUERYCTRL, &qctrl)) {
	/* ... */
	qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

The initial control ID is set to 0 ORed with the
``V4L2_CTRL_FLAG_NEXT_CTRL`` flag. The ``VIDIOC_QUERYCTRL`` ioctl will
return the first control with a higher ID than the specified one. When
no such controls are found an error is returned.

If you want to get all controls within a specific control class, then
you can set the initial ``qctrl.id`` value to the control class and add
an extra check to break out of the loop when a control of another
control class is found:


.. code-block:: c

    qctrl.id = V4L2_CTRL_CLASS_MPEG | V4L2_CTRL_FLAG_NEXT_CTRL;
    while (0 == ioctl(fd, VIDIOC_QUERYCTRL, &qctrl)) {
	if (V4L2_CTRL_ID2CLASS(qctrl.id) != V4L2_CTRL_CLASS_MPEG)
	    break;
	    /* ... */
	qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

The 32-bit ``qctrl.id`` value is subdivided into three bit ranges: the
top 4 bits are reserved for flags (e. g. ``V4L2_CTRL_FLAG_NEXT_CTRL``)
and are not actually part of the ID. The remaining 28 bits form the
control ID, of which the most significant 12 bits define the control
class and the least significant 16 bits identify the control within the
control class. It is guaranteed that these last 16 bits are always
non-zero for controls. The range of 0x1000 and up are reserved for
driver-specific controls. The macro ``V4L2_CTRL_ID2CLASS(id)`` returns
the control class ID based on a control ID.

If the driver does not support extended controls, then
``VIDIOC_QUERYCTRL`` will fail when used in combination with
``V4L2_CTRL_FLAG_NEXT_CTRL``. In that case the old method of enumerating
control should be used (see :ref:`enum_all_controls`). But if it is
supported, then it is guaranteed to enumerate over all controls,
including driver-private controls.


Creating Control Panels
=======================

It is possible to create control panels for a graphical user interface
where the user can select the various controls. Basically you will have
to iterate over all controls using the method described above. Each
control class starts with a control of type
``V4L2_CTRL_TYPE_CTRL_CLASS``. ``VIDIOC_QUERYCTRL`` will return the name
of this control class which can be used as the title of a tab page
within a control panel.

The flags field of struct :ref:`v4l2_queryctrl <v4l2-queryctrl>` also
contains hints on the behavior of the control. See the
:ref:`VIDIOC_QUERYCTRL` documentation for more
details.


.. _mpeg-controls:

Codec Control Reference
=======================

Below all controls within the Codec control class are described. First
the generic controls, then controls specific for certain hardware.

.. note::

   These controls are applicable to all codecs and not just MPEG. The
   defines are prefixed with V4L2_CID_MPEG/V4L2_MPEG as the controls
   were originally made for MPEG codecs and later extended to cover all
   encoding formats.


Generic Codec Controls
----------------------


.. _mpeg-control-id:

Codec Control IDs
^^^^^^^^^^^^^^^^^

``V4L2_CID_MPEG_CLASS (class)``
    The Codec class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class. This description can be
    used as the caption of a Tab page in a GUI, for example.

.. _v4l2-mpeg-stream-type:

``V4L2_CID_MPEG_STREAM_TYPE``
    (enum)

enum v4l2_mpeg_stream_type -
    The MPEG-1, -2 or -4 output stream type. One cannot assume anything
    here. Each hardware MPEG encoder tends to support different subsets
    of the available MPEG stream types. This control is specific to
    multiplexed MPEG streams. The currently defined stream types are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_STREAM_TYPE_MPEG2_PS``
      - MPEG-2 program stream
    * - ``V4L2_MPEG_STREAM_TYPE_MPEG2_TS``
      - MPEG-2 transport stream
    * - ``V4L2_MPEG_STREAM_TYPE_MPEG1_SS``
      - MPEG-1 system stream
    * - ``V4L2_MPEG_STREAM_TYPE_MPEG2_DVD``
      - MPEG-2 DVD-compatible stream
    * - ``V4L2_MPEG_STREAM_TYPE_MPEG1_VCD``
      - MPEG-1 VCD-compatible stream
    * - ``V4L2_MPEG_STREAM_TYPE_MPEG2_SVCD``
      - MPEG-2 SVCD-compatible stream



``V4L2_CID_MPEG_STREAM_PID_PMT (integer)``
    Program Map Table Packet ID for the MPEG transport stream (default
    16)

``V4L2_CID_MPEG_STREAM_PID_AUDIO (integer)``
    Audio Packet ID for the MPEG transport stream (default 256)

``V4L2_CID_MPEG_STREAM_PID_VIDEO (integer)``
    Video Packet ID for the MPEG transport stream (default 260)

``V4L2_CID_MPEG_STREAM_PID_PCR (integer)``
    Packet ID for the MPEG transport stream carrying PCR fields (default
    259)

``V4L2_CID_MPEG_STREAM_PES_ID_AUDIO (integer)``
    Audio ID for MPEG PES

``V4L2_CID_MPEG_STREAM_PES_ID_VIDEO (integer)``
    Video ID for MPEG PES

.. _v4l2-mpeg-stream-vbi-fmt:

``V4L2_CID_MPEG_STREAM_VBI_FMT``
    (enum)

enum v4l2_mpeg_stream_vbi_fmt -
    Some cards can embed VBI data (e. g. Closed Caption, Teletext) into
    the MPEG stream. This control selects whether VBI data should be
    embedded, and if so, what embedding method should be used. The list
    of possible VBI formats depends on the driver. The currently defined
    VBI format types are:



.. tabularcolumns:: |p{6 cm}|p{11.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_STREAM_VBI_FMT_NONE``
      - No VBI in the MPEG stream
    * - ``V4L2_MPEG_STREAM_VBI_FMT_IVTV``
      - VBI in private packets, IVTV format (documented in the kernel
	sources in the file
	``Documentation/media/v4l-drivers/cx2341x.rst``)



.. _v4l2-mpeg-audio-sampling-freq:

``V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ``
    (enum)

enum v4l2_mpeg_audio_sampling_freq -
    MPEG Audio sampling frequency. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_AUDIO_SAMPLING_FREQ_44100``
      - 44.1 kHz
    * - ``V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000``
      - 48 kHz
    * - ``V4L2_MPEG_AUDIO_SAMPLING_FREQ_32000``
      - 32 kHz



.. _v4l2-mpeg-audio-encoding:

``V4L2_CID_MPEG_AUDIO_ENCODING``
    (enum)

enum v4l2_mpeg_audio_encoding -
    MPEG Audio encoding. This control is specific to multiplexed MPEG
    streams. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_AUDIO_ENCODING_LAYER_1``
      - MPEG-1/2 Layer I encoding
    * - ``V4L2_MPEG_AUDIO_ENCODING_LAYER_2``
      - MPEG-1/2 Layer II encoding
    * - ``V4L2_MPEG_AUDIO_ENCODING_LAYER_3``
      - MPEG-1/2 Layer III encoding
    * - ``V4L2_MPEG_AUDIO_ENCODING_AAC``
      - MPEG-2/4 AAC (Advanced Audio Coding)
    * - ``V4L2_MPEG_AUDIO_ENCODING_AC3``
      - AC-3 aka ATSC A/52 encoding



.. _v4l2-mpeg-audio-l1-bitrate:

``V4L2_CID_MPEG_AUDIO_L1_BITRATE``
    (enum)

enum v4l2_mpeg_audio_l1_bitrate -
    MPEG-1/2 Layer I bitrate. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_32K``
      - 32 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_64K``
      - 64 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_96K``
      - 96 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_128K``
      - 128 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_160K``
      - 160 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_192K``
      - 192 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_224K``
      - 224 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_256K``
      - 256 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_288K``
      - 288 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_320K``
      - 320 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_352K``
      - 352 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_384K``
      - 384 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_416K``
      - 416 kbit/s
    * - ``V4L2_MPEG_AUDIO_L1_BITRATE_448K``
      - 448 kbit/s



.. _v4l2-mpeg-audio-l2-bitrate:

``V4L2_CID_MPEG_AUDIO_L2_BITRATE``
    (enum)

enum v4l2_mpeg_audio_l2_bitrate -
    MPEG-1/2 Layer II bitrate. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_32K``
      - 32 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_48K``
      - 48 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_56K``
      - 56 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_64K``
      - 64 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_80K``
      - 80 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_96K``
      - 96 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_112K``
      - 112 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_128K``
      - 128 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_160K``
      - 160 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_192K``
      - 192 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_224K``
      - 224 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_256K``
      - 256 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_320K``
      - 320 kbit/s
    * - ``V4L2_MPEG_AUDIO_L2_BITRATE_384K``
      - 384 kbit/s



.. _v4l2-mpeg-audio-l3-bitrate:

``V4L2_CID_MPEG_AUDIO_L3_BITRATE``
    (enum)

enum v4l2_mpeg_audio_l3_bitrate -
    MPEG-1/2 Layer III bitrate. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_32K``
      - 32 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_40K``
      - 40 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_48K``
      - 48 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_56K``
      - 56 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_64K``
      - 64 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_80K``
      - 80 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_96K``
      - 96 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_112K``
      - 112 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_128K``
      - 128 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_160K``
      - 160 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_192K``
      - 192 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_224K``
      - 224 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_256K``
      - 256 kbit/s
    * - ``V4L2_MPEG_AUDIO_L3_BITRATE_320K``
      - 320 kbit/s



``V4L2_CID_MPEG_AUDIO_AAC_BITRATE (integer)``
    AAC bitrate in bits per second.

.. _v4l2-mpeg-audio-ac3-bitrate:

``V4L2_CID_MPEG_AUDIO_AC3_BITRATE``
    (enum)

enum v4l2_mpeg_audio_ac3_bitrate -
    AC-3 bitrate. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_32K``
      - 32 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_40K``
      - 40 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_48K``
      - 48 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_56K``
      - 56 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_64K``
      - 64 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_80K``
      - 80 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_96K``
      - 96 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_112K``
      - 112 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_128K``
      - 128 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_160K``
      - 160 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_192K``
      - 192 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_224K``
      - 224 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_256K``
      - 256 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_320K``
      - 320 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_384K``
      - 384 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_448K``
      - 448 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_512K``
      - 512 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_576K``
      - 576 kbit/s
    * - ``V4L2_MPEG_AUDIO_AC3_BITRATE_640K``
      - 640 kbit/s



.. _v4l2-mpeg-audio-mode:

``V4L2_CID_MPEG_AUDIO_MODE``
    (enum)

enum v4l2_mpeg_audio_mode -
    MPEG Audio mode. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_AUDIO_MODE_STEREO``
      - Stereo
    * - ``V4L2_MPEG_AUDIO_MODE_JOINT_STEREO``
      - Joint Stereo
    * - ``V4L2_MPEG_AUDIO_MODE_DUAL``
      - Bilingual
    * - ``V4L2_MPEG_AUDIO_MODE_MONO``
      - Mono



.. _v4l2-mpeg-audio-mode-extension:

``V4L2_CID_MPEG_AUDIO_MODE_EXTENSION``
    (enum)

enum v4l2_mpeg_audio_mode_extension -
    Joint Stereo audio mode extension. In Layer I and II they indicate
    which subbands are in intensity stereo. All other subbands are coded
    in stereo. Layer III is not (yet) supported. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_4``
      - Subbands 4-31 in intensity stereo
    * - ``V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_8``
      - Subbands 8-31 in intensity stereo
    * - ``V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_12``
      - Subbands 12-31 in intensity stereo
    * - ``V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_16``
      - Subbands 16-31 in intensity stereo



.. _v4l2-mpeg-audio-emphasis:

``V4L2_CID_MPEG_AUDIO_EMPHASIS``
    (enum)

enum v4l2_mpeg_audio_emphasis -
    Audio Emphasis. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_AUDIO_EMPHASIS_NONE``
      - None
    * - ``V4L2_MPEG_AUDIO_EMPHASIS_50_DIV_15_uS``
      - 50/15 microsecond emphasis
    * - ``V4L2_MPEG_AUDIO_EMPHASIS_CCITT_J17``
      - CCITT J.17



.. _v4l2-mpeg-audio-crc:

``V4L2_CID_MPEG_AUDIO_CRC``
    (enum)

enum v4l2_mpeg_audio_crc -
    CRC method. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_AUDIO_CRC_NONE``
      - None
    * - ``V4L2_MPEG_AUDIO_CRC_CRC16``
      - 16 bit parity check



``V4L2_CID_MPEG_AUDIO_MUTE (boolean)``
    Mutes the audio when capturing. This is not done by muting audio
    hardware, which can still produce a slight hiss, but in the encoder
    itself, guaranteeing a fixed and reproducible audio bitstream. 0 =
    unmuted, 1 = muted.

.. _v4l2-mpeg-audio-dec-playback:

``V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK``
    (enum)

enum v4l2_mpeg_audio_dec_playback -
    Determines how monolingual audio should be played back. Possible
    values are:



.. tabularcolumns:: |p{9.0cm}|p{8.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_AUDIO_DEC_PLAYBACK_AUTO``
      - Automatically determines the best playback mode.
    * - ``V4L2_MPEG_AUDIO_DEC_PLAYBACK_STEREO``
      - Stereo playback.
    * - ``V4L2_MPEG_AUDIO_DEC_PLAYBACK_LEFT``
      - Left channel playback.
    * - ``V4L2_MPEG_AUDIO_DEC_PLAYBACK_RIGHT``
      - Right channel playback.
    * - ``V4L2_MPEG_AUDIO_DEC_PLAYBACK_MONO``
      - Mono playback.
    * - ``V4L2_MPEG_AUDIO_DEC_PLAYBACK_SWAPPED_STEREO``
      - Stereo playback with swapped left and right channels.



.. _v4l2-mpeg-audio-dec-multilingual-playback:

``V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK``
    (enum)

enum v4l2_mpeg_audio_dec_playback -
    Determines how multilingual audio should be played back.

.. _v4l2-mpeg-video-encoding:

``V4L2_CID_MPEG_VIDEO_ENCODING``
    (enum)

enum v4l2_mpeg_video_encoding -
    MPEG Video encoding method. This control is specific to multiplexed
    MPEG streams. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_ENCODING_MPEG_1``
      - MPEG-1 Video encoding
    * - ``V4L2_MPEG_VIDEO_ENCODING_MPEG_2``
      - MPEG-2 Video encoding
    * - ``V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC``
      - MPEG-4 AVC (H.264) Video encoding



.. _v4l2-mpeg-video-aspect:

``V4L2_CID_MPEG_VIDEO_ASPECT``
    (enum)

enum v4l2_mpeg_video_aspect -
    Video aspect. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_ASPECT_1x1``
    * - ``V4L2_MPEG_VIDEO_ASPECT_4x3``
    * - ``V4L2_MPEG_VIDEO_ASPECT_16x9``
    * - ``V4L2_MPEG_VIDEO_ASPECT_221x100``



``V4L2_CID_MPEG_VIDEO_B_FRAMES (integer)``
    Number of B-Frames (default 2)

``V4L2_CID_MPEG_VIDEO_GOP_SIZE (integer)``
    GOP size (default 12)

``V4L2_CID_MPEG_VIDEO_GOP_CLOSURE (boolean)``
    GOP closure (default 1)

``V4L2_CID_MPEG_VIDEO_PULLDOWN (boolean)``
    Enable 3:2 pulldown (default 0)

.. _v4l2-mpeg-video-bitrate-mode:

``V4L2_CID_MPEG_VIDEO_BITRATE_MODE``
    (enum)

enum v4l2_mpeg_video_bitrate_mode -
    Video bitrate mode. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_BITRATE_MODE_VBR``
      - Variable bitrate
    * - ``V4L2_MPEG_VIDEO_BITRATE_MODE_CBR``
      - Constant bitrate



``V4L2_CID_MPEG_VIDEO_BITRATE (integer)``
    Video bitrate in bits per second.

``V4L2_CID_MPEG_VIDEO_BITRATE_PEAK (integer)``
    Peak video bitrate in bits per second. Must be larger or equal to
    the average video bitrate. It is ignored if the video bitrate mode
    is set to constant bitrate.

``V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION (integer)``
    For every captured frame, skip this many subsequent frames (default
    0).

``V4L2_CID_MPEG_VIDEO_MUTE (boolean)``
    "Mutes" the video to a fixed color when capturing. This is useful
    for testing, to produce a fixed video bitstream. 0 = unmuted, 1 =
    muted.

``V4L2_CID_MPEG_VIDEO_MUTE_YUV (integer)``
    Sets the "mute" color of the video. The supplied 32-bit integer is
    interpreted as follows (bit 0 = least significant bit):



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - Bit 0:7
      - V chrominance information
    * - Bit 8:15
      - U chrominance information
    * - Bit 16:23
      - Y luminance information
    * - Bit 24:31
      - Must be zero.



.. _v4l2-mpeg-video-dec-pts:

``V4L2_CID_MPEG_VIDEO_DEC_PTS (integer64)``
    This read-only control returns the 33-bit video Presentation Time
    Stamp as defined in ITU T-REC-H.222.0 and ISO/IEC 13818-1 of the
    currently displayed frame. This is the same PTS as is used in
    :ref:`VIDIOC_DECODER_CMD`.

.. _v4l2-mpeg-video-dec-frame:

``V4L2_CID_MPEG_VIDEO_DEC_FRAME (integer64)``
    This read-only control returns the frame counter of the frame that
    is currently displayed (decoded). This value is reset to 0 whenever
    the decoder is started.

``V4L2_CID_MPEG_VIDEO_DECODER_SLICE_INTERFACE (boolean)``
    If enabled the decoder expects to receive a single slice per buffer,
    otherwise the decoder expects a single frame in per buffer.
    Applicable to the decoder, all codecs.

``V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE (boolean)``
    Enable writing sample aspect ratio in the Video Usability
    Information. Applicable to the H264 encoder.

.. _v4l2-mpeg-video-h264-vui-sar-idc:

``V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC``
    (enum)

enum v4l2_mpeg_video_h264_vui_sar_idc -
    VUI sample aspect ratio indicator for H.264 encoding. The value is
    defined in the table E-1 in the standard. Applicable to the H264
    encoder.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_UNSPECIFIED``
      - Unspecified
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_1x1``
      - 1x1
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_12x11``
      - 12x11
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_10x11``
      - 10x11
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_16x11``
      - 16x11
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_40x33``
      - 40x33
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_24x11``
      - 24x11
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_20x11``
      - 20x11
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_32x11``
      - 32x11
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_80x33``
      - 80x33
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_18x11``
      - 18x11
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_15x11``
      - 15x11
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_64x33``
      - 64x33
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_160x99``
      - 160x99
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_4x3``
      - 4x3
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_3x2``
      - 3x2
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_2x1``
      - 2x1
    * - ``V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_EXTENDED``
      - Extended SAR



``V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH (integer)``
    Extended sample aspect ratio width for H.264 VUI encoding.
    Applicable to the H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT (integer)``
    Extended sample aspect ratio height for H.264 VUI encoding.
    Applicable to the H264 encoder.

.. _v4l2-mpeg-video-h264-level:

``V4L2_CID_MPEG_VIDEO_H264_LEVEL``
    (enum)

enum v4l2_mpeg_video_h264_level -
    The level information for the H264 video elementary stream.
    Applicable to the H264 encoder. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_1_0``
      - Level 1.0
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_1B``
      - Level 1B
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_1_1``
      - Level 1.1
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_1_2``
      - Level 1.2
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_1_3``
      - Level 1.3
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_2_0``
      - Level 2.0
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_2_1``
      - Level 2.1
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_2_2``
      - Level 2.2
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_3_0``
      - Level 3.0
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_3_1``
      - Level 3.1
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_3_2``
      - Level 3.2
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_4_0``
      - Level 4.0
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_4_1``
      - Level 4.1
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_4_2``
      - Level 4.2
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_5_0``
      - Level 5.0
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_5_1``
      - Level 5.1



.. _v4l2-mpeg-video-mpeg4-level:

``V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL``
    (enum)

enum v4l2_mpeg_video_mpeg4_level -
    The level information for the MPEG4 elementary stream. Applicable to
    the MPEG4 encoder. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_MPEG4_LEVEL_0``
      - Level 0
    * - ``V4L2_MPEG_VIDEO_MPEG4_LEVEL_0B``
      - Level 0b
    * - ``V4L2_MPEG_VIDEO_MPEG4_LEVEL_1``
      - Level 1
    * - ``V4L2_MPEG_VIDEO_MPEG4_LEVEL_2``
      - Level 2
    * - ``V4L2_MPEG_VIDEO_MPEG4_LEVEL_3``
      - Level 3
    * - ``V4L2_MPEG_VIDEO_MPEG4_LEVEL_3B``
      - Level 3b
    * - ``V4L2_MPEG_VIDEO_MPEG4_LEVEL_4``
      - Level 4
    * - ``V4L2_MPEG_VIDEO_MPEG4_LEVEL_5``
      - Level 5



.. _v4l2-mpeg-video-h264-profile:

``V4L2_CID_MPEG_VIDEO_H264_PROFILE``
    (enum)

enum v4l2_mpeg_video_h264_profile -
    The profile information for H264. Applicable to the H264 encoder.
    Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE``
      - Baseline profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE``
      - Constrained Baseline profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_MAIN``
      - Main profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED``
      - Extended profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_HIGH``
      - High profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10``
      - High 10 profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422``
      - High 422 profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE``
      - High 444 Predictive profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10_INTRA``
      - High 10 Intra profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422_INTRA``
      - High 422 Intra profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_INTRA``
      - High 444 Intra profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_CAVLC_444_INTRA``
      - CAVLC 444 Intra profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_BASELINE``
      - Scalable Baseline profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_HIGH``
      - Scalable High profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_SCALABLE_HIGH_INTRA``
      - Scalable High Intra profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_STEREO_HIGH``
      - Stereo High profile
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH``
      - Multiview High profile



.. _v4l2-mpeg-video-mpeg4-profile:

``V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE``
    (enum)

enum v4l2_mpeg_video_mpeg4_profile -
    The profile information for MPEG4. Applicable to the MPEG4 encoder.
    Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE``
      - Simple profile
    * - ``V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE``
      - Advanced Simple profile
    * - ``V4L2_MPEG_VIDEO_MPEG4_PROFILE_CORE``
      - Core profile
    * - ``V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE_SCALABLE``
      - Simple Scalable profile
    * - ``V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_CODING_EFFICIENCY``
      -



``V4L2_CID_MPEG_VIDEO_MAX_REF_PIC (integer)``
    The maximum number of reference pictures used for encoding.
    Applicable to the encoder.

.. _v4l2-mpeg-video-multi-slice-mode:

``V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE``
    (enum)

enum v4l2_mpeg_video_multi_slice_mode -
    Determines how the encoder should handle division of frame into
    slices. Applicable to the encoder. Possible values are:



.. tabularcolumns:: |p{8.7cm}|p{8.8cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE``
      - Single slice per frame.
    * - ``V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_MB``
      - Multiple slices with set maximum number of macroblocks per slice.
    * - ``V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_BYTES``
      - Multiple slice with set maximum size in bytes per slice.



``V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB (integer)``
    The maximum number of macroblocks in a slice. Used when
    ``V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE`` is set to
    ``V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_MB``. Applicable to the
    encoder.

``V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES (integer)``
    The maximum size of a slice in bytes. Used when
    ``V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE`` is set to
    ``V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_BYTES``. Applicable to the
    encoder.

.. _v4l2-mpeg-video-h264-loop-filter-mode:

``V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE``
    (enum)

enum v4l2_mpeg_video_h264_loop_filter_mode -
    Loop filter mode for H264 encoder. Possible values are:



.. tabularcolumns:: |p{14.0cm}|p{3.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED``
      - Loop filter is enabled.
    * - ``V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED``
      - Loop filter is disabled.
    * - ``V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY``
      - Loop filter is disabled at the slice boundary.



``V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA (integer)``
    Loop filter alpha coefficient, defined in the H264 standard.
    Applicable to the H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA (integer)``
    Loop filter beta coefficient, defined in the H264 standard.
    Applicable to the H264 encoder.

.. _v4l2-mpeg-video-h264-entropy-mode:

``V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE``
    (enum)

enum v4l2_mpeg_video_h264_entropy_mode -
    Entropy coding mode for H264 - CABAC/CAVALC. Applicable to the H264
    encoder. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC``
      - Use CAVLC entropy coding.
    * - ``V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC``
      - Use CABAC entropy coding.



``V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM (boolean)``
    Enable 8X8 transform for H264. Applicable to the H264 encoder.

``V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB (integer)``
    Cyclic intra macroblock refresh. This is the number of continuous
    macroblocks refreshed every frame. Each frame a successive set of
    macroblocks is refreshed until the cycle completes and starts from
    the top of the frame. Applicable to H264, H263 and MPEG4 encoder.

``V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE (boolean)``
    Frame level rate control enable. If this control is disabled then
    the quantization parameter for each frame type is constant and set
    with appropriate controls (e.g.
    ``V4L2_CID_MPEG_VIDEO_H263_I_FRAME_QP``). If frame rate control is
    enabled then quantization parameter is adjusted to meet the chosen
    bitrate. Minimum and maximum value for the quantization parameter
    can be set with appropriate controls (e.g.
    ``V4L2_CID_MPEG_VIDEO_H263_MIN_QP``). Applicable to encoders.

``V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE (boolean)``
    Macroblock level rate control enable. Applicable to the MPEG4 and
    H264 encoders.

``V4L2_CID_MPEG_VIDEO_MPEG4_QPEL (boolean)``
    Quarter pixel motion estimation for MPEG4. Applicable to the MPEG4
    encoder.

``V4L2_CID_MPEG_VIDEO_H263_I_FRAME_QP (integer)``
    Quantization parameter for an I frame for H263. Valid range: from 1
    to 31.

``V4L2_CID_MPEG_VIDEO_H263_MIN_QP (integer)``
    Minimum quantization parameter for H263. Valid range: from 1 to 31.

``V4L2_CID_MPEG_VIDEO_H263_MAX_QP (integer)``
    Maximum quantization parameter for H263. Valid range: from 1 to 31.

``V4L2_CID_MPEG_VIDEO_H263_P_FRAME_QP (integer)``
    Quantization parameter for an P frame for H263. Valid range: from 1
    to 31.

``V4L2_CID_MPEG_VIDEO_H263_B_FRAME_QP (integer)``
    Quantization parameter for an B frame for H263. Valid range: from 1
    to 31.

``V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP (integer)``
    Quantization parameter for an I frame for H264. Valid range: from 0
    to 51.

``V4L2_CID_MPEG_VIDEO_H264_MIN_QP (integer)``
    Minimum quantization parameter for H264. Valid range: from 0 to 51.

``V4L2_CID_MPEG_VIDEO_H264_MAX_QP (integer)``
    Maximum quantization parameter for H264. Valid range: from 0 to 51.

``V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP (integer)``
    Quantization parameter for an P frame for H264. Valid range: from 0
    to 51.

``V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP (integer)``
    Quantization parameter for an B frame for H264. Valid range: from 0
    to 51.

``V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP (integer)``
    Quantization parameter for an I frame for MPEG4. Valid range: from 1
    to 31.

``V4L2_CID_MPEG_VIDEO_MPEG4_MIN_QP (integer)``
    Minimum quantization parameter for MPEG4. Valid range: from 1 to 31.

``V4L2_CID_MPEG_VIDEO_MPEG4_MAX_QP (integer)``
    Maximum quantization parameter for MPEG4. Valid range: from 1 to 31.

``V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP (integer)``
    Quantization parameter for an P frame for MPEG4. Valid range: from 1
    to 31.

``V4L2_CID_MPEG_VIDEO_MPEG4_B_FRAME_QP (integer)``
    Quantization parameter for an B frame for MPEG4. Valid range: from 1
    to 31.

``V4L2_CID_MPEG_VIDEO_VBV_SIZE (integer)``
    The Video Buffer Verifier size in kilobytes, it is used as a
    limitation of frame skip. The VBV is defined in the standard as a
    mean to verify that the produced stream will be successfully
    decoded. The standard describes it as "Part of a hypothetical
    decoder that is conceptually connected to the output of the encoder.
    Its purpose is to provide a constraint on the variability of the
    data rate that an encoder or editing process may produce.".
    Applicable to the MPEG1, MPEG2, MPEG4 encoders.

.. _v4l2-mpeg-video-vbv-delay:

``V4L2_CID_MPEG_VIDEO_VBV_DELAY (integer)``
    Sets the initial delay in milliseconds for VBV buffer control.

.. _v4l2-mpeg-video-hor-search-range:

``V4L2_CID_MPEG_VIDEO_MV_H_SEARCH_RANGE (integer)``
    Horizontal search range defines maximum horizontal search area in
    pixels to search and match for the present Macroblock (MB) in the
    reference picture. This V4L2 control macro is used to set horizontal
    search range for motion estimation module in video encoder.

.. _v4l2-mpeg-video-vert-search-range:

``V4L2_CID_MPEG_VIDEO_MV_V_SEARCH_RANGE (integer)``
    Vertical search range defines maximum vertical search area in pixels
    to search and match for the present Macroblock (MB) in the reference
    picture. This V4L2 control macro is used to set vertical search
    range for motion estimation module in video encoder.

.. _v4l2-mpeg-video-force-key-frame:

``V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME (button)``
    Force a key frame for the next queued buffer. Applicable to
    encoders. This is a general, codec-agnostic keyframe control.

``V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE (integer)``
    The Coded Picture Buffer size in kilobytes, it is used as a
    limitation of frame skip. The CPB is defined in the H264 standard as
    a mean to verify that the produced stream will be successfully
    decoded. Applicable to the H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_I_PERIOD (integer)``
    Period between I-frames in the open GOP for H264. In case of an open
    GOP this is the period between two I-frames. The period between IDR
    (Instantaneous Decoding Refresh) frames is taken from the GOP_SIZE
    control. An IDR frame, which stands for Instantaneous Decoding
    Refresh is an I-frame after which no prior frames are referenced.
    This means that a stream can be restarted from an IDR frame without
    the need to store or decode any previous frames. Applicable to the
    H264 encoder.

.. _v4l2-mpeg-video-header-mode:

``V4L2_CID_MPEG_VIDEO_HEADER_MODE``
    (enum)

enum v4l2_mpeg_video_header_mode -
    Determines whether the header is returned as the first buffer or is
    it returned together with the first frame. Applicable to encoders.
    Possible values are:



.. tabularcolumns:: |p{10.3cm}|p{7.2cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE``
      - The stream header is returned separately in the first buffer.
    * - ``V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME``
      - The stream header is returned together with the first encoded
	frame.



``V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER (boolean)``
    Repeat the video sequence headers. Repeating these headers makes
    random access to the video stream easier. Applicable to the MPEG1, 2
    and 4 encoder.

``V4L2_CID_MPEG_VIDEO_DECODER_MPEG4_DEBLOCK_FILTER (boolean)``
    Enabled the deblocking post processing filter for MPEG4 decoder.
    Applicable to the MPEG4 decoder.

``V4L2_CID_MPEG_VIDEO_MPEG4_VOP_TIME_RES (integer)``
    vop_time_increment_resolution value for MPEG4. Applicable to the
    MPEG4 encoder.

``V4L2_CID_MPEG_VIDEO_MPEG4_VOP_TIME_INC (integer)``
    vop_time_increment value for MPEG4. Applicable to the MPEG4
    encoder.

``V4L2_CID_MPEG_VIDEO_H264_SEI_FRAME_PACKING (boolean)``
    Enable generation of frame packing supplemental enhancement
    information in the encoded bitstream. The frame packing SEI message
    contains the arrangement of L and R planes for 3D viewing.
    Applicable to the H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_SEI_FP_CURRENT_FRAME_0 (boolean)``
    Sets current frame as frame0 in frame packing SEI. Applicable to the
    H264 encoder.

.. _v4l2-mpeg-video-h264-sei-fp-arrangement-type:

``V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE``
    (enum)

enum v4l2_mpeg_video_h264_sei_fp_arrangement_type -
    Frame packing arrangement type for H264 SEI. Applicable to the H264
    encoder. Possible values are:

.. tabularcolumns:: |p{12cm}|p{5.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE_CHEKERBOARD``
      - Pixels are alternatively from L and R.
    * - ``V4L2_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE_COLUMN``
      - L and R are interlaced by column.
    * - ``V4L2_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE_ROW``
      - L and R are interlaced by row.
    * - ``V4L2_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE_SIDE_BY_SIDE``
      - L is on the left, R on the right.
    * - ``V4L2_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE_TOP_BOTTOM``
      - L is on top, R on bottom.
    * - ``V4L2_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE_TEMPORAL``
      - One view per frame.



``V4L2_CID_MPEG_VIDEO_H264_FMO (boolean)``
    Enables flexible macroblock ordering in the encoded bitstream. It is
    a technique used for restructuring the ordering of macroblocks in
    pictures. Applicable to the H264 encoder.

.. _v4l2-mpeg-video-h264-fmo-map-type:

``V4L2_CID_MPEG_VIDEO_H264_FMO_MAP_TYPE``
   (enum)

enum v4l2_mpeg_video_h264_fmo_map_type -
    When using FMO, the map type divides the image in different scan
    patterns of macroblocks. Applicable to the H264 encoder. Possible
    values are:

.. tabularcolumns:: |p{12.5cm}|p{5.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_H264_FMO_MAP_TYPE_INTERLEAVED_SLICES``
      - Slices are interleaved one after other with macroblocks in run
	length order.
    * - ``V4L2_MPEG_VIDEO_H264_FMO_MAP_TYPE_SCATTERED_SLICES``
      - Scatters the macroblocks based on a mathematical function known to
	both encoder and decoder.
    * - ``V4L2_MPEG_VIDEO_H264_FMO_MAP_TYPE_FOREGROUND_WITH_LEFT_OVER``
      - Macroblocks arranged in rectangular areas or regions of interest.
    * - ``V4L2_MPEG_VIDEO_H264_FMO_MAP_TYPE_BOX_OUT``
      - Slice groups grow in a cyclic way from centre to outwards.
    * - ``V4L2_MPEG_VIDEO_H264_FMO_MAP_TYPE_RASTER_SCAN``
      - Slice groups grow in raster scan pattern from left to right.
    * - ``V4L2_MPEG_VIDEO_H264_FMO_MAP_TYPE_WIPE_SCAN``
      - Slice groups grow in wipe scan pattern from top to bottom.
    * - ``V4L2_MPEG_VIDEO_H264_FMO_MAP_TYPE_EXPLICIT``
      - User defined map type.



``V4L2_CID_MPEG_VIDEO_H264_FMO_SLICE_GROUP (integer)``
    Number of slice groups in FMO. Applicable to the H264 encoder.

.. _v4l2-mpeg-video-h264-fmo-change-direction:

``V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_DIRECTION``
    (enum)

enum v4l2_mpeg_video_h264_fmo_change_dir -
    Specifies a direction of the slice group change for raster and wipe
    maps. Applicable to the H264 encoder. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_H264_FMO_CHANGE_DIR_RIGHT``
      - Raster scan or wipe right.
    * - ``V4L2_MPEG_VIDEO_H264_FMO_CHANGE_DIR_LEFT``
      - Reverse raster scan or wipe left.



``V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_RATE (integer)``
    Specifies the size of the first slice group for raster and wipe map.
    Applicable to the H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_FMO_RUN_LENGTH (integer)``
    Specifies the number of consecutive macroblocks for the interleaved
    map. Applicable to the H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_ASO (boolean)``
    Enables arbitrary slice ordering in encoded bitstream. Applicable to
    the H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_ASO_SLICE_ORDER (integer)``
    Specifies the slice order in ASO. Applicable to the H264 encoder.
    The supplied 32-bit integer is interpreted as follows (bit 0 = least
    significant bit):



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - Bit 0:15
      - Slice ID
    * - Bit 16:32
      - Slice position or order



``V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING (boolean)``
    Enables H264 hierarchical coding. Applicable to the H264 encoder.

.. _v4l2-mpeg-video-h264-hierarchical-coding-type:

``V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_TYPE``
    (enum)

enum v4l2_mpeg_video_h264_hierarchical_coding_type -
    Specifies the hierarchical coding type. Applicable to the H264
    encoder. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_H264_HIERARCHICAL_CODING_B``
      - Hierarchical B coding.
    * - ``V4L2_MPEG_VIDEO_H264_HIERARCHICAL_CODING_P``
      - Hierarchical P coding.



``V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER (integer)``
    Specifies the number of hierarchical coding layers. Applicable to
    the H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER_QP (integer)``
    Specifies a user defined QP for each layer. Applicable to the H264
    encoder. The supplied 32-bit integer is interpreted as follows (bit
    0 = least significant bit):



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - Bit 0:15
      - QP value
    * - Bit 16:32
      - Layer number




MFC 5.1 MPEG Controls
---------------------

The following MPEG class controls deal with MPEG decoding and encoding
settings that are specific to the Multi Format Codec 5.1 device present
in the S5P family of SoCs by Samsung.


.. _mfc51-control-id:

MFC 5.1 Control IDs
^^^^^^^^^^^^^^^^^^^

``V4L2_CID_MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY_ENABLE (boolean)``
    If the display delay is enabled then the decoder is forced to return
    a CAPTURE buffer (decoded frame) after processing a certain number
    of OUTPUT buffers. The delay can be set through
    ``V4L2_CID_MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY``. This
    feature can be used for example for generating thumbnails of videos.
    Applicable to the H264 decoder.

``V4L2_CID_MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY (integer)``
    Display delay value for H264 decoder. The decoder is forced to
    return a decoded frame after the set 'display delay' number of
    frames. If this number is low it may result in frames returned out
    of dispaly order, in addition the hardware may still be using the
    returned buffer as a reference picture for subsequent frames.

``V4L2_CID_MPEG_MFC51_VIDEO_H264_NUM_REF_PIC_FOR_P (integer)``
    The number of reference pictures used for encoding a P picture.
    Applicable to the H264 encoder.

``V4L2_CID_MPEG_MFC51_VIDEO_PADDING (boolean)``
    Padding enable in the encoder - use a color instead of repeating
    border pixels. Applicable to encoders.

``V4L2_CID_MPEG_MFC51_VIDEO_PADDING_YUV (integer)``
    Padding color in the encoder. Applicable to encoders. The supplied
    32-bit integer is interpreted as follows (bit 0 = least significant
    bit):



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - Bit 0:7
      - V chrominance information
    * - Bit 8:15
      - U chrominance information
    * - Bit 16:23
      - Y luminance information
    * - Bit 24:31
      - Must be zero.



``V4L2_CID_MPEG_MFC51_VIDEO_RC_REACTION_COEFF (integer)``
    Reaction coefficient for MFC rate control. Applicable to encoders.

    .. note::

       #. Valid only when the frame level RC is enabled.

       #. For tight CBR, this field must be small (ex. 2 ~ 10). For
	  VBR, this field must be large (ex. 100 ~ 1000).

       #. It is not recommended to use the greater number than
	  FRAME_RATE * (10^9 / BIT_RATE).

``V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_DARK (boolean)``
    Adaptive rate control for dark region. Valid only when H.264 and
    macroblock level RC is enabled
    (``V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE``). Applicable to the H264
    encoder.

``V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_SMOOTH (boolean)``
    Adaptive rate control for smooth region. Valid only when H.264 and
    macroblock level RC is enabled
    (``V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE``). Applicable to the H264
    encoder.

``V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_STATIC (boolean)``
    Adaptive rate control for static region. Valid only when H.264 and
    macroblock level RC is enabled
    (``V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE``). Applicable to the H264
    encoder.

``V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_ACTIVITY (boolean)``
    Adaptive rate control for activity region. Valid only when H.264 and
    macroblock level RC is enabled
    (``V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE``). Applicable to the H264
    encoder.

.. _v4l2-mpeg-mfc51-video-frame-skip-mode:

``V4L2_CID_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE``
    (enum)

enum v4l2_mpeg_mfc51_video_frame_skip_mode -
    Indicates in what conditions the encoder should skip frames. If
    encoding a frame would cause the encoded stream to be larger then a
    chosen data limit then the frame will be skipped. Possible values
    are:


.. tabularcolumns:: |p{9.0cm}|p{8.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_MFC51_FRAME_SKIP_MODE_DISABLED``
      - Frame skip mode is disabled.
    * - ``V4L2_MPEG_MFC51_FRAME_SKIP_MODE_LEVEL_LIMIT``
      - Frame skip mode enabled and buffer limit is set by the chosen
	level and is defined by the standard.
    * - ``V4L2_MPEG_MFC51_FRAME_SKIP_MODE_BUF_LIMIT``
      - Frame skip mode enabled and buffer limit is set by the VBV
	(MPEG1/2/4) or CPB (H264) buffer size control.



``V4L2_CID_MPEG_MFC51_VIDEO_RC_FIXED_TARGET_BIT (integer)``
    Enable rate-control with fixed target bit. If this setting is
    enabled, then the rate control logic of the encoder will calculate
    the average bitrate for a GOP and keep it below or equal the set
    bitrate target. Otherwise the rate control logic calculates the
    overall average bitrate for the stream and keeps it below or equal
    to the set bitrate. In the first case the average bitrate for the
    whole stream will be smaller then the set bitrate. This is caused
    because the average is calculated for smaller number of frames, on
    the other hand enabling this setting will ensure that the stream
    will meet tight bandwidth constraints. Applicable to encoders.

.. _v4l2-mpeg-mfc51-video-force-frame-type:

``V4L2_CID_MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE``
    (enum)

enum v4l2_mpeg_mfc51_video_force_frame_type -
    Force a frame type for the next queued buffer. Applicable to
    encoders. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_MFC51_FORCE_FRAME_TYPE_DISABLED``
      - Forcing a specific frame type disabled.
    * - ``V4L2_MPEG_MFC51_FORCE_FRAME_TYPE_I_FRAME``
      - Force an I-frame.
    * - ``V4L2_MPEG_MFC51_FORCE_FRAME_TYPE_NOT_CODED``
      - Force a non-coded frame.




CX2341x MPEG Controls
---------------------

The following MPEG class controls deal with MPEG encoding settings that
are specific to the Conexant CX23415 and CX23416 MPEG encoding chips.


.. _cx2341x-control-id:

CX2341x Control IDs
^^^^^^^^^^^^^^^^^^^

.. _v4l2-mpeg-cx2341x-video-spatial-filter-mode:

``V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE``
    (enum)

enum v4l2_mpeg_cx2341x_video_spatial_filter_mode -
    Sets the Spatial Filter mode (default ``MANUAL``). Possible values
    are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_MANUAL``
      - Choose the filter manually
    * - ``V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_AUTO``
      - Choose the filter automatically



``V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER (integer (0-15))``
    The setting for the Spatial Filter. 0 = off, 15 = maximum. (Default
    is 0.)

.. _luma-spatial-filter-type:

``V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE``
    (enum)

enum v4l2_mpeg_cx2341x_video_luma_spatial_filter_type -
    Select the algorithm to use for the Luma Spatial Filter (default
    ``1D_HOR``). Possible values:



.. tabularcolumns:: |p{14.5cm}|p{3.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_OFF``
      - No filter
    * - ``V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_1D_HOR``
      - One-dimensional horizontal
    * - ``V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_1D_VERT``
      - One-dimensional vertical
    * - ``V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_2D_HV_SEPARABLE``
      - Two-dimensional separable
    * - ``V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_2D_SYM_NON_SEPARABLE``
      - Two-dimensional symmetrical non-separable



.. _chroma-spatial-filter-type:

``V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE``
    (enum)

enum v4l2_mpeg_cx2341x_video_chroma_spatial_filter_type -
    Select the algorithm for the Chroma Spatial Filter (default
    ``1D_HOR``). Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_OFF``
      - No filter
    * - ``V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_1D_HOR``
      - One-dimensional horizontal



.. _v4l2-mpeg-cx2341x-video-temporal-filter-mode:

``V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE``
    (enum)

enum v4l2_mpeg_cx2341x_video_temporal_filter_mode -
    Sets the Temporal Filter mode (default ``MANUAL``). Possible values
    are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_MANUAL``
      - Choose the filter manually
    * - ``V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_AUTO``
      - Choose the filter automatically



``V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER (integer (0-31))``
    The setting for the Temporal Filter. 0 = off, 31 = maximum. (Default
    is 8 for full-scale capturing and 0 for scaled capturing.)

.. _v4l2-mpeg-cx2341x-video-median-filter-type:

``V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE``
    (enum)

enum v4l2_mpeg_cx2341x_video_median_filter_type -
    Median Filter Type (default ``OFF``). Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF``
      - No filter
    * - ``V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_HOR``
      - Horizontal filter
    * - ``V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_VERT``
      - Vertical filter
    * - ``V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_HOR_VERT``
      - Horizontal and vertical filter
    * - ``V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_DIAG``
      - Diagonal filter



``V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM (integer (0-255))``
    Threshold above which the luminance median filter is enabled
    (default 0)

``V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP (integer (0-255))``
    Threshold below which the luminance median filter is enabled
    (default 255)

``V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM (integer (0-255))``
    Threshold above which the chroma median filter is enabled (default
    0)

``V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP (integer (0-255))``
    Threshold below which the chroma median filter is enabled (default
    255)

``V4L2_CID_MPEG_CX2341X_STREAM_INSERT_NAV_PACKETS (boolean)``
    The CX2341X MPEG encoder can insert one empty MPEG-2 PES packet into
    the stream between every four video frames. The packet size is 2048
    bytes, including the packet_start_code_prefix and stream_id
    fields. The stream_id is 0xBF (private stream 2). The payload
    consists of 0x00 bytes, to be filled in by the application. 0 = do
    not insert, 1 = insert packets.


VPX Control Reference
---------------------

The VPX controls include controls for encoding parameters of VPx video
codec.


.. _vpx-control-id:

VPX Control IDs
^^^^^^^^^^^^^^^

.. _v4l2-vpx-num-partitions:

``V4L2_CID_MPEG_VIDEO_VPX_NUM_PARTITIONS``
    (enum)

enum v4l2_vp8_num_partitions -
    The number of token partitions to use in VP8 encoder. Possible
    values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_CID_MPEG_VIDEO_VPX_1_PARTITION``
      - 1 coefficient partition
    * - ``V4L2_CID_MPEG_VIDEO_VPX_2_PARTITIONS``
      - 2 coefficient partitions
    * - ``V4L2_CID_MPEG_VIDEO_VPX_4_PARTITIONS``
      - 4 coefficient partitions
    * - ``V4L2_CID_MPEG_VIDEO_VPX_8_PARTITIONS``
      - 8 coefficient partitions



``V4L2_CID_MPEG_VIDEO_VPX_IMD_DISABLE_4X4 (boolean)``
    Setting this prevents intra 4x4 mode in the intra mode decision.

.. _v4l2-vpx-num-ref-frames:

``V4L2_CID_MPEG_VIDEO_VPX_NUM_REF_FRAMES``
    (enum)

enum v4l2_vp8_num_ref_frames -
    The number of reference pictures for encoding P frames. Possible
    values are:

.. tabularcolumns:: |p{7.9cm}|p{9.6cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_CID_MPEG_VIDEO_VPX_1_REF_FRAME``
      - Last encoded frame will be searched
    * - ``V4L2_CID_MPEG_VIDEO_VPX_2_REF_FRAME``
      - Two frames will be searched among the last encoded frame, the
	golden frame and the alternate reference (altref) frame. The
	encoder implementation will decide which two are chosen.
    * - ``V4L2_CID_MPEG_VIDEO_VPX_3_REF_FRAME``
      - The last encoded frame, the golden frame and the altref frame will
	be searched.



``V4L2_CID_MPEG_VIDEO_VPX_FILTER_LEVEL (integer)``
    Indicates the loop filter level. The adjustment of the loop filter
    level is done via a delta value against a baseline loop filter
    value.

``V4L2_CID_MPEG_VIDEO_VPX_FILTER_SHARPNESS (integer)``
    This parameter affects the loop filter. Anything above zero weakens
    the deblocking effect on the loop filter.

``V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_REF_PERIOD (integer)``
    Sets the refresh period for the golden frame. The period is defined
    in number of frames. For a value of 'n', every nth frame starting
    from the first key frame will be taken as a golden frame. For eg.
    for encoding sequence of 0, 1, 2, 3, 4, 5, 6, 7 where the golden
    frame refresh period is set as 4, the frames 0, 4, 8 etc will be
    taken as the golden frames as frame 0 is always a key frame.

.. _v4l2-vpx-golden-frame-sel:

``V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_SEL``
    (enum)

enum v4l2_vp8_golden_frame_sel -
    Selects the golden frame for encoding. Possible values are:

.. raw:: latex

    \footnotesize

.. tabularcolumns:: |p{9.0cm}|p{8.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_USE_PREV``
      - Use the (n-2)th frame as a golden frame, current frame index being
	'n'.
    * - ``V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_USE_REF_PERIOD``
      - Use the previous specific frame indicated by
	``V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_REF_PERIOD`` as a
	golden frame.

.. raw:: latex

    \normalsize


``V4L2_CID_MPEG_VIDEO_VPX_MIN_QP (integer)``
    Minimum quantization parameter for VP8.

``V4L2_CID_MPEG_VIDEO_VPX_MAX_QP (integer)``
    Maximum quantization parameter for VP8.

``V4L2_CID_MPEG_VIDEO_VPX_I_FRAME_QP (integer)``
    Quantization parameter for an I frame for VP8.

``V4L2_CID_MPEG_VIDEO_VPX_P_FRAME_QP (integer)``
    Quantization parameter for a P frame for VP8.

``V4L2_CID_MPEG_VIDEO_VPX_PROFILE (integer)``
    Select the desired profile for VPx encoder. Acceptable values are 0,
    1, 2 and 3 corresponding to encoder profiles 0, 1, 2 and 3.


.. _camera-controls:

Camera Control Reference
========================

The Camera class includes controls for mechanical (or equivalent
digital) features of a device such as controllable lenses or sensors.


.. _camera-control-id:

Camera Control IDs
------------------

``V4L2_CID_CAMERA_CLASS (class)``
    The Camera class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

.. _v4l2-exposure-auto-type:

``V4L2_CID_EXPOSURE_AUTO``
    (enum)

enum v4l2_exposure_auto_type -
    Enables automatic adjustments of the exposure time and/or iris
    aperture. The effect of manual changes of the exposure time or iris
    aperture while these features are enabled is undefined, drivers
    should ignore such requests. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_EXPOSURE_AUTO``
      - Automatic exposure time, automatic iris aperture.
    * - ``V4L2_EXPOSURE_MANUAL``
      - Manual exposure time, manual iris.
    * - ``V4L2_EXPOSURE_SHUTTER_PRIORITY``
      - Manual exposure time, auto iris.
    * - ``V4L2_EXPOSURE_APERTURE_PRIORITY``
      - Auto exposure time, manual iris.



``V4L2_CID_EXPOSURE_ABSOLUTE (integer)``
    Determines the exposure time of the camera sensor. The exposure time
    is limited by the frame interval. Drivers should interpret the
    values as 100 µs units, where the value 1 stands for 1/10000th of a
    second, 10000 for 1 second and 100000 for 10 seconds.

``V4L2_CID_EXPOSURE_AUTO_PRIORITY (boolean)``
    When ``V4L2_CID_EXPOSURE_AUTO`` is set to ``AUTO`` or
    ``APERTURE_PRIORITY``, this control determines if the device may
    dynamically vary the frame rate. By default this feature is disabled
    (0) and the frame rate must remain constant.

``V4L2_CID_AUTO_EXPOSURE_BIAS (integer menu)``
    Determines the automatic exposure compensation, it is effective only
    when ``V4L2_CID_EXPOSURE_AUTO`` control is set to ``AUTO``,
    ``SHUTTER_PRIORITY`` or ``APERTURE_PRIORITY``. It is expressed in
    terms of EV, drivers should interpret the values as 0.001 EV units,
    where the value 1000 stands for +1 EV.

    Increasing the exposure compensation value is equivalent to
    decreasing the exposure value (EV) and will increase the amount of
    light at the image sensor. The camera performs the exposure
    compensation by adjusting absolute exposure time and/or aperture.

.. _v4l2-exposure-metering:

``V4L2_CID_EXPOSURE_METERING``
    (enum)

enum v4l2_exposure_metering -
    Determines how the camera measures the amount of light available for
    the frame exposure. Possible values are:

.. tabularcolumns:: |p{8.5cm}|p{9.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_EXPOSURE_METERING_AVERAGE``
      - Use the light information coming from the entire frame and average
	giving no weighting to any particular portion of the metered area.
    * - ``V4L2_EXPOSURE_METERING_CENTER_WEIGHTED``
      - Average the light information coming from the entire frame giving
	priority to the center of the metered area.
    * - ``V4L2_EXPOSURE_METERING_SPOT``
      - Measure only very small area at the center of the frame.
    * - ``V4L2_EXPOSURE_METERING_MATRIX``
      - A multi-zone metering. The light intensity is measured in several
	points of the frame and the results are combined. The algorithm of
	the zones selection and their significance in calculating the
	final value is device dependent.



``V4L2_CID_PAN_RELATIVE (integer)``
    This control turns the camera horizontally by the specified amount.
    The unit is undefined. A positive value moves the camera to the
    right (clockwise when viewed from above), a negative value to the
    left. A value of zero does not cause motion. This is a write-only
    control.

``V4L2_CID_TILT_RELATIVE (integer)``
    This control turns the camera vertically by the specified amount.
    The unit is undefined. A positive value moves the camera up, a
    negative value down. A value of zero does not cause motion. This is
    a write-only control.

``V4L2_CID_PAN_RESET (button)``
    When this control is set, the camera moves horizontally to the
    default position.

``V4L2_CID_TILT_RESET (button)``
    When this control is set, the camera moves vertically to the default
    position.

``V4L2_CID_PAN_ABSOLUTE (integer)``
    This control turns the camera horizontally to the specified
    position. Positive values move the camera to the right (clockwise
    when viewed from above), negative values to the left. Drivers should
    interpret the values as arc seconds, with valid values between -180
    * 3600 and +180 * 3600 inclusive.

``V4L2_CID_TILT_ABSOLUTE (integer)``
    This control turns the camera vertically to the specified position.
    Positive values move the camera up, negative values down. Drivers
    should interpret the values as arc seconds, with valid values
    between -180 * 3600 and +180 * 3600 inclusive.

``V4L2_CID_FOCUS_ABSOLUTE (integer)``
    This control sets the focal point of the camera to the specified
    position. The unit is undefined. Positive values set the focus
    closer to the camera, negative values towards infinity.

``V4L2_CID_FOCUS_RELATIVE (integer)``
    This control moves the focal point of the camera by the specified
    amount. The unit is undefined. Positive values move the focus closer
    to the camera, negative values towards infinity. This is a
    write-only control.

``V4L2_CID_FOCUS_AUTO (boolean)``
    Enables continuous automatic focus adjustments. The effect of manual
    focus adjustments while this feature is enabled is undefined,
    drivers should ignore such requests.

``V4L2_CID_AUTO_FOCUS_START (button)``
    Starts single auto focus process. The effect of setting this control
    when ``V4L2_CID_FOCUS_AUTO`` is set to ``TRUE`` (1) is undefined,
    drivers should ignore such requests.

``V4L2_CID_AUTO_FOCUS_STOP (button)``
    Aborts automatic focusing started with ``V4L2_CID_AUTO_FOCUS_START``
    control. It is effective only when the continuous autofocus is
    disabled, that is when ``V4L2_CID_FOCUS_AUTO`` control is set to
    ``FALSE`` (0).

.. _v4l2-auto-focus-status:

``V4L2_CID_AUTO_FOCUS_STATUS (bitmask)``
    The automatic focus status. This is a read-only control.

    Setting ``V4L2_LOCK_FOCUS`` lock bit of the ``V4L2_CID_3A_LOCK``
    control may stop updates of the ``V4L2_CID_AUTO_FOCUS_STATUS``
    control value.

.. tabularcolumns:: |p{6.5cm}|p{11.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_AUTO_FOCUS_STATUS_IDLE``
      - Automatic focus is not active.
    * - ``V4L2_AUTO_FOCUS_STATUS_BUSY``
      - Automatic focusing is in progress.
    * - ``V4L2_AUTO_FOCUS_STATUS_REACHED``
      - Focus has been reached.
    * - ``V4L2_AUTO_FOCUS_STATUS_FAILED``
      - Automatic focus has failed, the driver will not transition from
	this state until another action is performed by an application.



.. _v4l2-auto-focus-range:

``V4L2_CID_AUTO_FOCUS_RANGE``
    (enum)

enum v4l2_auto_focus_range -
    Determines auto focus distance range for which lens may be adjusted.

.. tabularcolumns:: |p{6.5cm}|p{11.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_AUTO_FOCUS_RANGE_AUTO``
      - The camera automatically selects the focus range.
    * - ``V4L2_AUTO_FOCUS_RANGE_NORMAL``
      - Normal distance range, limited for best automatic focus
	performance.
    * - ``V4L2_AUTO_FOCUS_RANGE_MACRO``
      - Macro (close-up) auto focus. The camera will use its minimum
	possible distance for auto focus.
    * - ``V4L2_AUTO_FOCUS_RANGE_INFINITY``
      - The lens is set to focus on an object at infinite distance.



``V4L2_CID_ZOOM_ABSOLUTE (integer)``
    Specify the objective lens focal length as an absolute value. The
    zoom unit is driver-specific and its value should be a positive
    integer.

``V4L2_CID_ZOOM_RELATIVE (integer)``
    Specify the objective lens focal length relatively to the current
    value. Positive values move the zoom lens group towards the
    telephoto direction, negative values towards the wide-angle
    direction. The zoom unit is driver-specific. This is a write-only
    control.

``V4L2_CID_ZOOM_CONTINUOUS (integer)``
    Move the objective lens group at the specified speed until it
    reaches physical device limits or until an explicit request to stop
    the movement. A positive value moves the zoom lens group towards the
    telephoto direction. A value of zero stops the zoom lens group
    movement. A negative value moves the zoom lens group towards the
    wide-angle direction. The zoom speed unit is driver-specific.

``V4L2_CID_IRIS_ABSOLUTE (integer)``
    This control sets the camera's aperture to the specified value. The
    unit is undefined. Larger values open the iris wider, smaller values
    close it.

``V4L2_CID_IRIS_RELATIVE (integer)``
    This control modifies the camera's aperture by the specified amount.
    The unit is undefined. Positive values open the iris one step
    further, negative values close it one step further. This is a
    write-only control.

``V4L2_CID_PRIVACY (boolean)``
    Prevent video from being acquired by the camera. When this control
    is set to ``TRUE`` (1), no image can be captured by the camera.
    Common means to enforce privacy are mechanical obturation of the
    sensor and firmware image processing, but the device is not
    restricted to these methods. Devices that implement the privacy
    control must support read access and may support write access.

``V4L2_CID_BAND_STOP_FILTER (integer)``
    Switch the band-stop filter of a camera sensor on or off, or specify
    its strength. Such band-stop filters can be used, for example, to
    filter out the fluorescent light component.

.. _v4l2-auto-n-preset-white-balance:

``V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE``
    (enum)

enum v4l2_auto_n_preset_white_balance -
    Sets white balance to automatic, manual or a preset. The presets
    determine color temperature of the light as a hint to the camera for
    white balance adjustments resulting in most accurate color
    representation. The following white balance presets are listed in
    order of increasing color temperature.

.. tabularcolumns:: |p{7.0 cm}|p{10.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_WHITE_BALANCE_MANUAL``
      - Manual white balance.
    * - ``V4L2_WHITE_BALANCE_AUTO``
      - Automatic white balance adjustments.
    * - ``V4L2_WHITE_BALANCE_INCANDESCENT``
      - White balance setting for incandescent (tungsten) lighting. It
	generally cools down the colors and corresponds approximately to
	2500...3500 K color temperature range.
    * - ``V4L2_WHITE_BALANCE_FLUORESCENT``
      - White balance preset for fluorescent lighting. It corresponds
	approximately to 4000...5000 K color temperature.
    * - ``V4L2_WHITE_BALANCE_FLUORESCENT_H``
      - With this setting the camera will compensate for fluorescent H
	lighting.
    * - ``V4L2_WHITE_BALANCE_HORIZON``
      - White balance setting for horizon daylight. It corresponds
	approximately to 5000 K color temperature.
    * - ``V4L2_WHITE_BALANCE_DAYLIGHT``
      - White balance preset for daylight (with clear sky). It corresponds
	approximately to 5000...6500 K color temperature.
    * - ``V4L2_WHITE_BALANCE_FLASH``
      - With this setting the camera will compensate for the flash light.
	It slightly warms up the colors and corresponds roughly to
	5000...5500 K color temperature.
    * - ``V4L2_WHITE_BALANCE_CLOUDY``
      - White balance preset for moderately overcast sky. This option
	corresponds approximately to 6500...8000 K color temperature
	range.
    * - ``V4L2_WHITE_BALANCE_SHADE``
      - White balance preset for shade or heavily overcast sky. It
	corresponds approximately to 9000...10000 K color temperature.



.. _v4l2-wide-dynamic-range:

``V4L2_CID_WIDE_DYNAMIC_RANGE (boolean)``
    Enables or disables the camera's wide dynamic range feature. This
    feature allows to obtain clear images in situations where intensity
    of the illumination varies significantly throughout the scene, i.e.
    there are simultaneously very dark and very bright areas. It is most
    commonly realized in cameras by combining two subsequent frames with
    different exposure times.  [#f1]_

.. _v4l2-image-stabilization:

``V4L2_CID_IMAGE_STABILIZATION (boolean)``
    Enables or disables image stabilization.

``V4L2_CID_ISO_SENSITIVITY (integer menu)``
    Determines ISO equivalent of an image sensor indicating the sensor's
    sensitivity to light. The numbers are expressed in arithmetic scale,
    as per :ref:`iso12232` standard, where doubling the sensor
    sensitivity is represented by doubling the numerical ISO value.
    Applications should interpret the values as standard ISO values
    multiplied by 1000, e.g. control value 800 stands for ISO 0.8.
    Drivers will usually support only a subset of standard ISO values.
    The effect of setting this control while the
    ``V4L2_CID_ISO_SENSITIVITY_AUTO`` control is set to a value other
    than ``V4L2_CID_ISO_SENSITIVITY_MANUAL`` is undefined, drivers
    should ignore such requests.

.. _v4l2-iso-sensitivity-auto-type:

``V4L2_CID_ISO_SENSITIVITY_AUTO``
    (enum)

enum v4l2_iso_sensitivity_type -
    Enables or disables automatic ISO sensitivity adjustments.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_CID_ISO_SENSITIVITY_MANUAL``
      - Manual ISO sensitivity.
    * - ``V4L2_CID_ISO_SENSITIVITY_AUTO``
      - Automatic ISO sensitivity adjustments.



.. _v4l2-scene-mode:

``V4L2_CID_SCENE_MODE``
    (enum)

enum v4l2_scene_mode -
    This control allows to select scene programs as the camera automatic
    modes optimized for common shooting scenes. Within these modes the
    camera determines best exposure, aperture, focusing, light metering,
    white balance and equivalent sensitivity. The controls of those
    parameters are influenced by the scene mode control. An exact
    behavior in each mode is subject to the camera specification.

    When the scene mode feature is not used, this control should be set
    to ``V4L2_SCENE_MODE_NONE`` to make sure the other possibly related
    controls are accessible. The following scene programs are defined:

.. tabularcolumns:: |p{6.0cm}|p{11.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_SCENE_MODE_NONE``
      - The scene mode feature is disabled.
    * - ``V4L2_SCENE_MODE_BACKLIGHT``
      - Backlight. Compensates for dark shadows when light is coming from
	behind a subject, also by automatically turning on the flash.
    * - ``V4L2_SCENE_MODE_BEACH_SNOW``
      - Beach and snow. This mode compensates for all-white or bright
	scenes, which tend to look gray and low contrast, when camera's
	automatic exposure is based on an average scene brightness. To
	compensate, this mode automatically slightly overexposes the
	frames. The white balance may also be adjusted to compensate for
	the fact that reflected snow looks bluish rather than white.
    * - ``V4L2_SCENE_MODE_CANDLELIGHT``
      - Candle light. The camera generally raises the ISO sensitivity and
	lowers the shutter speed. This mode compensates for relatively
	close subject in the scene. The flash is disabled in order to
	preserve the ambiance of the light.
    * - ``V4L2_SCENE_MODE_DAWN_DUSK``
      - Dawn and dusk. Preserves the colors seen in low natural light
	before dusk and after down. The camera may turn off the flash, and
	automatically focus at infinity. It will usually boost saturation
	and lower the shutter speed.
    * - ``V4L2_SCENE_MODE_FALL_COLORS``
      - Fall colors. Increases saturation and adjusts white balance for
	color enhancement. Pictures of autumn leaves get saturated reds
	and yellows.
    * - ``V4L2_SCENE_MODE_FIREWORKS``
      - Fireworks. Long exposure times are used to capture the expanding
	burst of light from a firework. The camera may invoke image
	stabilization.
    * - ``V4L2_SCENE_MODE_LANDSCAPE``
      - Landscape. The camera may choose a small aperture to provide deep
	depth of field and long exposure duration to help capture detail
	in dim light conditions. The focus is fixed at infinity. Suitable
	for distant and wide scenery.
    * - ``V4L2_SCENE_MODE_NIGHT``
      - Night, also known as Night Landscape. Designed for low light
	conditions, it preserves detail in the dark areas without blowing
	out bright objects. The camera generally sets itself to a
	medium-to-high ISO sensitivity, with a relatively long exposure
	time, and turns flash off. As such, there will be increased image
	noise and the possibility of blurred image.
    * - ``V4L2_SCENE_MODE_PARTY_INDOOR``
      - Party and indoor. Designed to capture indoor scenes that are lit
	by indoor background lighting as well as the flash. The camera
	usually increases ISO sensitivity, and adjusts exposure for the
	low light conditions.
    * - ``V4L2_SCENE_MODE_PORTRAIT``
      - Portrait. The camera adjusts the aperture so that the depth of
	field is reduced, which helps to isolate the subject against a
	smooth background. Most cameras recognize the presence of faces in
	the scene and focus on them. The color hue is adjusted to enhance
	skin tones. The intensity of the flash is often reduced.
    * - ``V4L2_SCENE_MODE_SPORTS``
      - Sports. Significantly increases ISO and uses a fast shutter speed
	to freeze motion of rapidly-moving subjects. Increased image noise
	may be seen in this mode.
    * - ``V4L2_SCENE_MODE_SUNSET``
      - Sunset. Preserves deep hues seen in sunsets and sunrises. It bumps
	up the saturation.
    * - ``V4L2_SCENE_MODE_TEXT``
      - Text. It applies extra contrast and sharpness, it is typically a
	black-and-white mode optimized for readability. Automatic focus
	may be switched to close-up mode and this setting may also involve
	some lens-distortion correction.



``V4L2_CID_3A_LOCK (bitmask)``
    This control locks or unlocks the automatic focus, exposure and
    white balance. The automatic adjustments can be paused independently
    by setting the corresponding lock bit to 1. The camera then retains
    the settings until the lock bit is cleared. The following lock bits
    are defined:

    When a given algorithm is not enabled, drivers should ignore
    requests to lock it and should return no error. An example might be
    an application setting bit ``V4L2_LOCK_WHITE_BALANCE`` when the
    ``V4L2_CID_AUTO_WHITE_BALANCE`` control is set to ``FALSE``. The
    value of this control may be changed by exposure, white balance or
    focus controls.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_LOCK_EXPOSURE``
      - Automatic exposure adjustments lock.
    * - ``V4L2_LOCK_WHITE_BALANCE``
      - Automatic white balance adjustments lock.
    * - ``V4L2_LOCK_FOCUS``
      - Automatic focus lock.



``V4L2_CID_PAN_SPEED (integer)``
    This control turns the camera horizontally at the specific speed.
    The unit is undefined. A positive value moves the camera to the
    right (clockwise when viewed from above), a negative value to the
    left. A value of zero stops the motion if one is in progress and has
    no effect otherwise.

``V4L2_CID_TILT_SPEED (integer)``
    This control turns the camera vertically at the specified speed. The
    unit is undefined. A positive value moves the camera up, a negative
    value down. A value of zero stops the motion if one is in progress
    and has no effect otherwise.


.. _fm-tx-controls:

FM Transmitter Control Reference
================================

The FM Transmitter (FM_TX) class includes controls for common features
of FM transmissions capable devices. Currently this class includes
parameters for audio compression, pilot tone generation, audio deviation
limiter, RDS transmission and tuning power features.


.. _fm-tx-control-id:

FM_TX Control IDs
-----------------

``V4L2_CID_FM_TX_CLASS (class)``
    The FM_TX class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

``V4L2_CID_RDS_TX_DEVIATION (integer)``
    Configures RDS signal frequency deviation level in Hz. The range and
    step are driver-specific.

``V4L2_CID_RDS_TX_PI (integer)``
    Sets the RDS Programme Identification field for transmission.

``V4L2_CID_RDS_TX_PTY (integer)``
    Sets the RDS Programme Type field for transmission. This encodes up
    to 31 pre-defined programme types.

``V4L2_CID_RDS_TX_PS_NAME (string)``
    Sets the Programme Service name (PS_NAME) for transmission. It is
    intended for static display on a receiver. It is the primary aid to
    listeners in programme service identification and selection. In
    Annex E of :ref:`iec62106`, the RDS specification, there is a full
    description of the correct character encoding for Programme Service
    name strings. Also from RDS specification, PS is usually a single
    eight character text. However, it is also possible to find receivers
    which can scroll strings sized as 8 x N characters. So, this control
    must be configured with steps of 8 characters. The result is it must
    always contain a string with size multiple of 8.

``V4L2_CID_RDS_TX_RADIO_TEXT (string)``
    Sets the Radio Text info for transmission. It is a textual
    description of what is being broadcasted. RDS Radio Text can be
    applied when broadcaster wishes to transmit longer PS names,
    programme-related information or any other text. In these cases,
    RadioText should be used in addition to ``V4L2_CID_RDS_TX_PS_NAME``.
    The encoding for Radio Text strings is also fully described in Annex
    E of :ref:`iec62106`. The length of Radio Text strings depends on
    which RDS Block is being used to transmit it, either 32 (2A block)
    or 64 (2B block). However, it is also possible to find receivers
    which can scroll strings sized as 32 x N or 64 x N characters. So,
    this control must be configured with steps of 32 or 64 characters.
    The result is it must always contain a string with size multiple of
    32 or 64.

``V4L2_CID_RDS_TX_MONO_STEREO (boolean)``
    Sets the Mono/Stereo bit of the Decoder Identification code. If set,
    then the audio was recorded as stereo.

``V4L2_CID_RDS_TX_ARTIFICIAL_HEAD (boolean)``
    Sets the
    `Artificial Head <http://en.wikipedia.org/wiki/Artificial_head>`__
    bit of the Decoder Identification code. If set, then the audio was
    recorded using an artificial head.

``V4L2_CID_RDS_TX_COMPRESSED (boolean)``
    Sets the Compressed bit of the Decoder Identification code. If set,
    then the audio is compressed.

``V4L2_CID_RDS_TX_DYNAMIC_PTY (boolean)``
    Sets the Dynamic PTY bit of the Decoder Identification code. If set,
    then the PTY code is dynamically switched.

``V4L2_CID_RDS_TX_TRAFFIC_ANNOUNCEMENT (boolean)``
    If set, then a traffic announcement is in progress.

``V4L2_CID_RDS_TX_TRAFFIC_PROGRAM (boolean)``
    If set, then the tuned programme carries traffic announcements.

``V4L2_CID_RDS_TX_MUSIC_SPEECH (boolean)``
    If set, then this channel broadcasts music. If cleared, then it
    broadcasts speech. If the transmitter doesn't make this distinction,
    then it should be set.

``V4L2_CID_RDS_TX_ALT_FREQS_ENABLE (boolean)``
    If set, then transmit alternate frequencies.

``V4L2_CID_RDS_TX_ALT_FREQS (__u32 array)``
    The alternate frequencies in kHz units. The RDS standard allows for
    up to 25 frequencies to be defined. Drivers may support fewer
    frequencies so check the array size.

``V4L2_CID_AUDIO_LIMITER_ENABLED (boolean)``
    Enables or disables the audio deviation limiter feature. The limiter
    is useful when trying to maximize the audio volume, minimize
    receiver-generated distortion and prevent overmodulation.

``V4L2_CID_AUDIO_LIMITER_RELEASE_TIME (integer)``
    Sets the audio deviation limiter feature release time. Unit is in
    useconds. Step and range are driver-specific.

``V4L2_CID_AUDIO_LIMITER_DEVIATION (integer)``
    Configures audio frequency deviation level in Hz. The range and step
    are driver-specific.

``V4L2_CID_AUDIO_COMPRESSION_ENABLED (boolean)``
    Enables or disables the audio compression feature. This feature
    amplifies signals below the threshold by a fixed gain and compresses
    audio signals above the threshold by the ratio of Threshold/(Gain +
    Threshold).

``V4L2_CID_AUDIO_COMPRESSION_GAIN (integer)``
    Sets the gain for audio compression feature. It is a dB value. The
    range and step are driver-specific.

``V4L2_CID_AUDIO_COMPRESSION_THRESHOLD (integer)``
    Sets the threshold level for audio compression freature. It is a dB
    value. The range and step are driver-specific.

``V4L2_CID_AUDIO_COMPRESSION_ATTACK_TIME (integer)``
    Sets the attack time for audio compression feature. It is a useconds
    value. The range and step are driver-specific.

``V4L2_CID_AUDIO_COMPRESSION_RELEASE_TIME (integer)``
    Sets the release time for audio compression feature. It is a
    useconds value. The range and step are driver-specific.

``V4L2_CID_PILOT_TONE_ENABLED (boolean)``
    Enables or disables the pilot tone generation feature.

``V4L2_CID_PILOT_TONE_DEVIATION (integer)``
    Configures pilot tone frequency deviation level. Unit is in Hz. The
    range and step are driver-specific.

``V4L2_CID_PILOT_TONE_FREQUENCY (integer)``
    Configures pilot tone frequency value. Unit is in Hz. The range and
    step are driver-specific.

``V4L2_CID_TUNE_PREEMPHASIS``
    (enum)

enum v4l2_preemphasis -
    Configures the pre-emphasis value for broadcasting. A pre-emphasis
    filter is applied to the broadcast to accentuate the high audio
    frequencies. Depending on the region, a time constant of either 50
    or 75 useconds is used. The enum v4l2_preemphasis defines possible
    values for pre-emphasis. Here they are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_PREEMPHASIS_DISABLED``
      - No pre-emphasis is applied.
    * - ``V4L2_PREEMPHASIS_50_uS``
      - A pre-emphasis of 50 uS is used.
    * - ``V4L2_PREEMPHASIS_75_uS``
      - A pre-emphasis of 75 uS is used.



``V4L2_CID_TUNE_POWER_LEVEL (integer)``
    Sets the output power level for signal transmission. Unit is in
    dBuV. Range and step are driver-specific.

``V4L2_CID_TUNE_ANTENNA_CAPACITOR (integer)``
    This selects the value of antenna tuning capacitor manually or
    automatically if set to zero. Unit, range and step are
    driver-specific.

For more details about RDS specification, refer to :ref:`iec62106`
document, from CENELEC.


.. _flash-controls:

Flash Control Reference
=======================

The V4L2 flash controls are intended to provide generic access to flash
controller devices. Flash controller devices are typically used in
digital cameras.

The interface can support both LED and xenon flash devices. As of
writing this, there is no xenon flash driver using this interface.


.. _flash-controls-use-cases:

Supported use cases
-------------------


Unsynchronised LED flash (software strobe)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Unsynchronised LED flash is controlled directly by the host as the
sensor. The flash must be enabled by the host before the exposure of the
image starts and disabled once it ends. The host is fully responsible
for the timing of the flash.

Example of such device: Nokia N900.


Synchronised LED flash (hardware strobe)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The synchronised LED flash is pre-programmed by the host (power and
timeout) but controlled by the sensor through a strobe signal from the
sensor to the flash.

The sensor controls the flash duration and timing. This information
typically must be made available to the sensor.


LED flash as torch
^^^^^^^^^^^^^^^^^^

LED flash may be used as torch in conjunction with another use case
involving camera or individually.


.. _flash-control-id:

Flash Control IDs
"""""""""""""""""

``V4L2_CID_FLASH_CLASS (class)``
    The FLASH class descriptor.

``V4L2_CID_FLASH_LED_MODE (menu)``
    Defines the mode of the flash LED, the high-power white LED attached
    to the flash controller. Setting this control may not be possible in
    presence of some faults. See V4L2_CID_FLASH_FAULT.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_FLASH_LED_MODE_NONE``
      - Off.
    * - ``V4L2_FLASH_LED_MODE_FLASH``
      - Flash mode.
    * - ``V4L2_FLASH_LED_MODE_TORCH``
      - Torch mode. See V4L2_CID_FLASH_TORCH_INTENSITY.



``V4L2_CID_FLASH_STROBE_SOURCE (menu)``
    Defines the source of the flash LED strobe.

.. tabularcolumns:: |p{7.0cm}|p{10.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_FLASH_STROBE_SOURCE_SOFTWARE``
      - The flash strobe is triggered by using the
	V4L2_CID_FLASH_STROBE control.
    * - ``V4L2_FLASH_STROBE_SOURCE_EXTERNAL``
      - The flash strobe is triggered by an external source. Typically
	this is a sensor, which makes it possible to synchronises the
	flash strobe start to exposure start.



``V4L2_CID_FLASH_STROBE (button)``
    Strobe flash. Valid when V4L2_CID_FLASH_LED_MODE is set to
    V4L2_FLASH_LED_MODE_FLASH and V4L2_CID_FLASH_STROBE_SOURCE
    is set to V4L2_FLASH_STROBE_SOURCE_SOFTWARE. Setting this
    control may not be possible in presence of some faults. See
    V4L2_CID_FLASH_FAULT.

``V4L2_CID_FLASH_STROBE_STOP (button)``
    Stop flash strobe immediately.

``V4L2_CID_FLASH_STROBE_STATUS (boolean)``
    Strobe status: whether the flash is strobing at the moment or not.
    This is a read-only control.

``V4L2_CID_FLASH_TIMEOUT (integer)``
    Hardware timeout for flash. The flash strobe is stopped after this
    period of time has passed from the start of the strobe.

``V4L2_CID_FLASH_INTENSITY (integer)``
    Intensity of the flash strobe when the flash LED is in flash mode
    (V4L2_FLASH_LED_MODE_FLASH). The unit should be milliamps (mA)
    if possible.

``V4L2_CID_FLASH_TORCH_INTENSITY (integer)``
    Intensity of the flash LED in torch mode
    (V4L2_FLASH_LED_MODE_TORCH). The unit should be milliamps (mA)
    if possible. Setting this control may not be possible in presence of
    some faults. See V4L2_CID_FLASH_FAULT.

``V4L2_CID_FLASH_INDICATOR_INTENSITY (integer)``
    Intensity of the indicator LED. The indicator LED may be fully
    independent of the flash LED. The unit should be microamps (uA) if
    possible.

``V4L2_CID_FLASH_FAULT (bitmask)``
    Faults related to the flash. The faults tell about specific problems
    in the flash chip itself or the LEDs attached to it. Faults may
    prevent further use of some of the flash controls. In particular,
    V4L2_CID_FLASH_LED_MODE is set to V4L2_FLASH_LED_MODE_NONE
    if the fault affects the flash LED. Exactly which faults have such
    an effect is chip dependent. Reading the faults resets the control
    and returns the chip to a usable state if possible.

.. tabularcolumns:: |p{8.0cm}|p{9.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_FLASH_FAULT_OVER_VOLTAGE``
      - Flash controller voltage to the flash LED has exceeded the limit
	specific to the flash controller.
    * - ``V4L2_FLASH_FAULT_TIMEOUT``
      - The flash strobe was still on when the timeout set by the user ---
	V4L2_CID_FLASH_TIMEOUT control --- has expired. Not all flash
	controllers may set this in all such conditions.
    * - ``V4L2_FLASH_FAULT_OVER_TEMPERATURE``
      - The flash controller has overheated.
    * - ``V4L2_FLASH_FAULT_SHORT_CIRCUIT``
      - The short circuit protection of the flash controller has been
	triggered.
    * - ``V4L2_FLASH_FAULT_OVER_CURRENT``
      - Current in the LED power supply has exceeded the limit specific to
	the flash controller.
    * - ``V4L2_FLASH_FAULT_INDICATOR``
      - The flash controller has detected a short or open circuit
	condition on the indicator LED.
    * - ``V4L2_FLASH_FAULT_UNDER_VOLTAGE``
      - Flash controller voltage to the flash LED has been below the
	minimum limit specific to the flash controller.
    * - ``V4L2_FLASH_FAULT_INPUT_VOLTAGE``
      - The input voltage of the flash controller is below the limit under
	which strobing the flash at full current will not be possible.The
	condition persists until this flag is no longer set.
    * - ``V4L2_FLASH_FAULT_LED_OVER_TEMPERATURE``
      - The temperature of the LED has exceeded its allowed upper limit.



``V4L2_CID_FLASH_CHARGE (boolean)``
    Enable or disable charging of the xenon flash capacitor.

``V4L2_CID_FLASH_READY (boolean)``
    Is the flash ready to strobe? Xenon flashes require their capacitors
    charged before strobing. LED flashes often require a cooldown period
    after strobe during which another strobe will not be possible. This
    is a read-only control.


.. _jpeg-controls:

JPEG Control Reference
======================

The JPEG class includes controls for common features of JPEG encoders
and decoders. Currently it includes features for codecs implementing
progressive baseline DCT compression process with Huffman entrophy
coding.


.. _jpeg-control-id:

JPEG Control IDs
----------------

``V4L2_CID_JPEG_CLASS (class)``
    The JPEG class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

``V4L2_CID_JPEG_CHROMA_SUBSAMPLING (menu)``
    The chroma subsampling factors describe how each component of an
    input image is sampled, in respect to maximum sample rate in each
    spatial dimension. See :ref:`itu-t81`, clause A.1.1. for more
    details. The ``V4L2_CID_JPEG_CHROMA_SUBSAMPLING`` control determines
    how Cb and Cr components are downsampled after converting an input
    image from RGB to Y'CbCr color space.

.. tabularcolumns:: |p{7.0cm}|p{10.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_JPEG_CHROMA_SUBSAMPLING_444``
      - No chroma subsampling, each pixel has Y, Cr and Cb values.
    * - ``V4L2_JPEG_CHROMA_SUBSAMPLING_422``
      - Horizontally subsample Cr, Cb components by a factor of 2.
    * - ``V4L2_JPEG_CHROMA_SUBSAMPLING_420``
      - Subsample Cr, Cb components horizontally and vertically by 2.
    * - ``V4L2_JPEG_CHROMA_SUBSAMPLING_411``
      - Horizontally subsample Cr, Cb components by a factor of 4.
    * - ``V4L2_JPEG_CHROMA_SUBSAMPLING_410``
      - Subsample Cr, Cb components horizontally by 4 and vertically by 2.
    * - ``V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY``
      - Use only luminance component.



``V4L2_CID_JPEG_RESTART_INTERVAL (integer)``
    The restart interval determines an interval of inserting RSTm
    markers (m = 0..7). The purpose of these markers is to additionally
    reinitialize the encoder process, in order to process blocks of an
    image independently. For the lossy compression processes the restart
    interval unit is MCU (Minimum Coded Unit) and its value is contained
    in DRI (Define Restart Interval) marker. If
    ``V4L2_CID_JPEG_RESTART_INTERVAL`` control is set to 0, DRI and RSTm
    markers will not be inserted.

.. _jpeg-quality-control:

``V4L2_CID_JPEG_COMPRESSION_QUALITY (integer)``
    ``V4L2_CID_JPEG_COMPRESSION_QUALITY`` control determines trade-off
    between image quality and size. It provides simpler method for
    applications to control image quality, without a need for direct
    reconfiguration of luminance and chrominance quantization tables. In
    cases where a driver uses quantization tables configured directly by
    an application, using interfaces defined elsewhere,
    ``V4L2_CID_JPEG_COMPRESSION_QUALITY`` control should be set by
    driver to 0.

    The value range of this control is driver-specific. Only positive,
    non-zero values are meaningful. The recommended range is 1 - 100,
    where larger values correspond to better image quality.

.. _jpeg-active-marker-control:

``V4L2_CID_JPEG_ACTIVE_MARKER (bitmask)``
    Specify which JPEG markers are included in compressed stream. This
    control is valid only for encoders.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_JPEG_ACTIVE_MARKER_APP0``
      - Application data segment APP\ :sub:`0`.
    * - ``V4L2_JPEG_ACTIVE_MARKER_APP1``
      - Application data segment APP\ :sub:`1`.
    * - ``V4L2_JPEG_ACTIVE_MARKER_COM``
      - Comment segment.
    * - ``V4L2_JPEG_ACTIVE_MARKER_DQT``
      - Quantization tables segment.
    * - ``V4L2_JPEG_ACTIVE_MARKER_DHT``
      - Huffman tables segment.



For more details about JPEG specification, refer to :ref:`itu-t81`,
:ref:`jfif`, :ref:`w3c-jpeg-jfif`.


.. _image-source-controls:

Image Source Control Reference
==============================

The Image Source control class is intended for low-level control of
image source devices such as image sensors. The devices feature an
analogue to digital converter and a bus transmitter to transmit the
image data out of the device.


.. _image-source-control-id:

Image Source Control IDs
------------------------

``V4L2_CID_IMAGE_SOURCE_CLASS (class)``
    The IMAGE_SOURCE class descriptor.

``V4L2_CID_VBLANK (integer)``
    Vertical blanking. The idle period after every frame during which no
    image data is produced. The unit of vertical blanking is a line.
    Every line has length of the image width plus horizontal blanking at
    the pixel rate defined by ``V4L2_CID_PIXEL_RATE`` control in the
    same sub-device.

``V4L2_CID_HBLANK (integer)``
    Horizontal blanking. The idle period after every line of image data
    during which no image data is produced. The unit of horizontal
    blanking is pixels.

``V4L2_CID_ANALOGUE_GAIN (integer)``
    Analogue gain is gain affecting all colour components in the pixel
    matrix. The gain operation is performed in the analogue domain
    before A/D conversion.

``V4L2_CID_TEST_PATTERN_RED (integer)``
    Test pattern red colour component.

``V4L2_CID_TEST_PATTERN_GREENR (integer)``
    Test pattern green (next to red) colour component.

``V4L2_CID_TEST_PATTERN_BLUE (integer)``
    Test pattern blue colour component.

``V4L2_CID_TEST_PATTERN_GREENB (integer)``
    Test pattern green (next to blue) colour component.


.. _image-process-controls:

Image Process Control Reference
===============================

The Image Process control class is intended for low-level control of
image processing functions. Unlike ``V4L2_CID_IMAGE_SOURCE_CLASS``, the
controls in this class affect processing the image, and do not control
capturing of it.


.. _image-process-control-id:

Image Process Control IDs
-------------------------

``V4L2_CID_IMAGE_PROC_CLASS (class)``
    The IMAGE_PROC class descriptor.

``V4L2_CID_LINK_FREQ (integer menu)``
    Data bus frequency. Together with the media bus pixel code, bus type
    (clock cycles per sample), the data bus frequency defines the pixel
    rate (``V4L2_CID_PIXEL_RATE``) in the pixel array (or possibly
    elsewhere, if the device is not an image sensor). The frame rate can
    be calculated from the pixel clock, image width and height and
    horizontal and vertical blanking. While the pixel rate control may
    be defined elsewhere than in the subdev containing the pixel array,
    the frame rate cannot be obtained from that information. This is
    because only on the pixel array it can be assumed that the vertical
    and horizontal blanking information is exact: no other blanking is
    allowed in the pixel array. The selection of frame rate is performed
    by selecting the desired horizontal and vertical blanking. The unit
    of this control is Hz.

``V4L2_CID_PIXEL_RATE (64-bit integer)``
    Pixel rate in the source pads of the subdev. This control is
    read-only and its unit is pixels / second.

``V4L2_CID_TEST_PATTERN (menu)``
    Some capture/display/sensor devices have the capability to generate
    test pattern images. These hardware specific test patterns can be
    used to test if a device is working properly.

``V4L2_CID_DEINTERLACING_MODE (menu)``
    The video deinterlacing mode (such as Bob, Weave, ...). The menu items are
    driver specific and are documented in :ref:`v4l-drivers`.

``V4L2_CID_DIGITAL_GAIN (integer)``
    Digital gain is the value by which all colour components
    are multiplied by. Typically the digital gain applied is the
    control value divided by e.g. 0x100, meaning that to get no
    digital gain the control value needs to be 0x100. The no-gain
    configuration is also typically the default.


.. _dv-controls:

Digital Video Control Reference
===============================

The Digital Video control class is intended to control receivers and
transmitters for `VGA <http://en.wikipedia.org/wiki/Vga>`__,
`DVI <http://en.wikipedia.org/wiki/Digital_Visual_Interface>`__
(Digital Visual Interface), HDMI (:ref:`hdmi`) and DisplayPort
(:ref:`dp`). These controls are generally expected to be private to
the receiver or transmitter subdevice that implements them, so they are
only exposed on the ``/dev/v4l-subdev*`` device node.

.. note::

   Note that these devices can have multiple input or output pads which are
   hooked up to e.g. HDMI connectors. Even though the subdevice will
   receive or transmit video from/to only one of those pads, the other pads
   can still be active when it comes to EDID (Extended Display
   Identification Data, :ref:`vesaedid`) and HDCP (High-bandwidth Digital
   Content Protection System, :ref:`hdcp`) processing, allowing the
   device to do the fairly slow EDID/HDCP handling in advance. This allows
   for quick switching between connectors.

These pads appear in several of the controls in this section as
bitmasks, one bit for each pad. Bit 0 corresponds to pad 0, bit 1 to pad
1, etc. The maximum value of the control is the set of valid pads.


.. _dv-control-id:

Digital Video Control IDs
-------------------------

``V4L2_CID_DV_CLASS (class)``
    The Digital Video class descriptor.

``V4L2_CID_DV_TX_HOTPLUG (bitmask)``
    Many connectors have a hotplug pin which is high if EDID information
    is available from the source. This control shows the state of the
    hotplug pin as seen by the transmitter. Each bit corresponds to an
    output pad on the transmitter. If an output pad does not have an
    associated hotplug pin, then the bit for that pad will be 0. This
    read-only control is applicable to DVI-D, HDMI and DisplayPort
    connectors.

``V4L2_CID_DV_TX_RXSENSE (bitmask)``
    Rx Sense is the detection of pull-ups on the TMDS clock lines. This
    normally means that the sink has left/entered standby (i.e. the
    transmitter can sense that the receiver is ready to receive video).
    Each bit corresponds to an output pad on the transmitter. If an
    output pad does not have an associated Rx Sense, then the bit for
    that pad will be 0. This read-only control is applicable to DVI-D
    and HDMI devices.

``V4L2_CID_DV_TX_EDID_PRESENT (bitmask)``
    When the transmitter sees the hotplug signal from the receiver it
    will attempt to read the EDID. If set, then the transmitter has read
    at least the first block (= 128 bytes). Each bit corresponds to an
    output pad on the transmitter. If an output pad does not support
    EDIDs, then the bit for that pad will be 0. This read-only control
    is applicable to VGA, DVI-A/D, HDMI and DisplayPort connectors.

``V4L2_CID_DV_TX_MODE``
    (enum)

enum v4l2_dv_tx_mode -
    HDMI transmitters can transmit in DVI-D mode (just video) or in HDMI
    mode (video + audio + auxiliary data). This control selects which
    mode to use: V4L2_DV_TX_MODE_DVI_D or V4L2_DV_TX_MODE_HDMI.
    This control is applicable to HDMI connectors.

``V4L2_CID_DV_TX_RGB_RANGE``
    (enum)

enum v4l2_dv_rgb_range -
    Select the quantization range for RGB output. V4L2_DV_RANGE_AUTO
    follows the RGB quantization range specified in the standard for the
    video interface (ie. :ref:`cea861` for HDMI).
    V4L2_DV_RANGE_LIMITED and V4L2_DV_RANGE_FULL override the
    standard to be compatible with sinks that have not implemented the
    standard correctly (unfortunately quite common for HDMI and DVI-D).
    Full range allows all possible values to be used whereas limited
    range sets the range to (16 << (N-8)) - (235 << (N-8)) where N is
    the number of bits per component. This control is applicable to VGA,
    DVI-A/D, HDMI and DisplayPort connectors.

``V4L2_CID_DV_TX_IT_CONTENT_TYPE``
    (enum)

enum v4l2_dv_it_content_type -
    Configures the IT Content Type of the transmitted video. This
    information is sent over HDMI and DisplayPort connectors as part of
    the AVI InfoFrame. The term 'IT Content' is used for content that
    originates from a computer as opposed to content from a TV broadcast
    or an analog source. The enum v4l2_dv_it_content_type defines
    the possible content types:

.. tabularcolumns:: |p{7.0cm}|p{10.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_DV_IT_CONTENT_TYPE_GRAPHICS``
      - Graphics content. Pixel data should be passed unfiltered and
	without analog reconstruction.
    * - ``V4L2_DV_IT_CONTENT_TYPE_PHOTO``
      - Photo content. The content is derived from digital still pictures.
	The content should be passed through with minimal scaling and
	picture enhancements.
    * - ``V4L2_DV_IT_CONTENT_TYPE_CINEMA``
      - Cinema content.
    * - ``V4L2_DV_IT_CONTENT_TYPE_GAME``
      - Game content. Audio and video latency should be minimized.
    * - ``V4L2_DV_IT_CONTENT_TYPE_NO_ITC``
      - No IT Content information is available and the ITC bit in the AVI
	InfoFrame is set to 0.



``V4L2_CID_DV_RX_POWER_PRESENT (bitmask)``
    Detects whether the receiver receives power from the source (e.g.
    HDMI carries 5V on one of the pins). This is often used to power an
    eeprom which contains EDID information, such that the source can
    read the EDID even if the sink is in standby/power off. Each bit
    corresponds to an input pad on the transmitter. If an input pad
    cannot detect whether power is present, then the bit for that pad
    will be 0. This read-only control is applicable to DVI-D, HDMI and
    DisplayPort connectors.

``V4L2_CID_DV_RX_RGB_RANGE``
    (enum)

enum v4l2_dv_rgb_range -
    Select the quantization range for RGB input. V4L2_DV_RANGE_AUTO
    follows the RGB quantization range specified in the standard for the
    video interface (ie. :ref:`cea861` for HDMI).
    V4L2_DV_RANGE_LIMITED and V4L2_DV_RANGE_FULL override the
    standard to be compatible with sources that have not implemented the
    standard correctly (unfortunately quite common for HDMI and DVI-D).
    Full range allows all possible values to be used whereas limited
    range sets the range to (16 << (N-8)) - (235 << (N-8)) where N is
    the number of bits per component. This control is applicable to VGA,
    DVI-A/D, HDMI and DisplayPort connectors.

``V4L2_CID_DV_RX_IT_CONTENT_TYPE``
    (enum)

enum v4l2_dv_it_content_type -
    Reads the IT Content Type of the received video. This information is
    sent over HDMI and DisplayPort connectors as part of the AVI
    InfoFrame. The term 'IT Content' is used for content that originates
    from a computer as opposed to content from a TV broadcast or an
    analog source. See ``V4L2_CID_DV_TX_IT_CONTENT_TYPE`` for the
    available content types.


.. _fm-rx-controls:

FM Receiver Control Reference
=============================

The FM Receiver (FM_RX) class includes controls for common features of
FM Reception capable devices.


.. _fm-rx-control-id:

FM_RX Control IDs
-----------------

``V4L2_CID_FM_RX_CLASS (class)``
    The FM_RX class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

``V4L2_CID_RDS_RECEPTION (boolean)``
    Enables/disables RDS reception by the radio tuner

``V4L2_CID_RDS_RX_PTY (integer)``
    Gets RDS Programme Type field. This encodes up to 31 pre-defined
    programme types.

``V4L2_CID_RDS_RX_PS_NAME (string)``
    Gets the Programme Service name (PS_NAME). It is intended for
    static display on a receiver. It is the primary aid to listeners in
    programme service identification and selection. In Annex E of
    :ref:`iec62106`, the RDS specification, there is a full
    description of the correct character encoding for Programme Service
    name strings. Also from RDS specification, PS is usually a single
    eight character text. However, it is also possible to find receivers
    which can scroll strings sized as 8 x N characters. So, this control
    must be configured with steps of 8 characters. The result is it must
    always contain a string with size multiple of 8.

``V4L2_CID_RDS_RX_RADIO_TEXT (string)``
    Gets the Radio Text info. It is a textual description of what is
    being broadcasted. RDS Radio Text can be applied when broadcaster
    wishes to transmit longer PS names, programme-related information or
    any other text. In these cases, RadioText can be used in addition to
    ``V4L2_CID_RDS_RX_PS_NAME``. The encoding for Radio Text strings is
    also fully described in Annex E of :ref:`iec62106`. The length of
    Radio Text strings depends on which RDS Block is being used to
    transmit it, either 32 (2A block) or 64 (2B block). However, it is
    also possible to find receivers which can scroll strings sized as 32
    x N or 64 x N characters. So, this control must be configured with
    steps of 32 or 64 characters. The result is it must always contain a
    string with size multiple of 32 or 64.

``V4L2_CID_RDS_RX_TRAFFIC_ANNOUNCEMENT (boolean)``
    If set, then a traffic announcement is in progress.

``V4L2_CID_RDS_RX_TRAFFIC_PROGRAM (boolean)``
    If set, then the tuned programme carries traffic announcements.

``V4L2_CID_RDS_RX_MUSIC_SPEECH (boolean)``
    If set, then this channel broadcasts music. If cleared, then it
    broadcasts speech. If the transmitter doesn't make this distinction,
    then it will be set.

``V4L2_CID_TUNE_DEEMPHASIS``
    (enum)

enum v4l2_deemphasis -
    Configures the de-emphasis value for reception. A de-emphasis filter
    is applied to the broadcast to accentuate the high audio
    frequencies. Depending on the region, a time constant of either 50
    or 75 useconds is used. The enum v4l2_deemphasis defines possible
    values for de-emphasis. Here they are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_DEEMPHASIS_DISABLED``
      - No de-emphasis is applied.
    * - ``V4L2_DEEMPHASIS_50_uS``
      - A de-emphasis of 50 uS is used.
    * - ``V4L2_DEEMPHASIS_75_uS``
      - A de-emphasis of 75 uS is used.




.. _detect-controls:

Detect Control Reference
========================

The Detect class includes controls for common features of various motion
or object detection capable devices.


.. _detect-control-id:

Detect Control IDs
------------------

``V4L2_CID_DETECT_CLASS (class)``
    The Detect class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

``V4L2_CID_DETECT_MD_MODE (menu)``
    Sets the motion detection mode.

.. tabularcolumns:: |p{7.5cm}|p{10.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_DETECT_MD_MODE_DISABLED``
      - Disable motion detection.
    * - ``V4L2_DETECT_MD_MODE_GLOBAL``
      - Use a single motion detection threshold.
    * - ``V4L2_DETECT_MD_MODE_THRESHOLD_GRID``
      - The image is divided into a grid, each cell with its own motion
	detection threshold. These thresholds are set through the
	``V4L2_CID_DETECT_MD_THRESHOLD_GRID`` matrix control.
    * - ``V4L2_DETECT_MD_MODE_REGION_GRID``
      - The image is divided into a grid, each cell with its own region
	value that specifies which per-region motion detection thresholds
	should be used. Each region has its own thresholds. How these
	per-region thresholds are set up is driver-specific. The region
	values for the grid are set through the
	``V4L2_CID_DETECT_MD_REGION_GRID`` matrix control.



``V4L2_CID_DETECT_MD_GLOBAL_THRESHOLD (integer)``
    Sets the global motion detection threshold to be used with the
    ``V4L2_DETECT_MD_MODE_GLOBAL`` motion detection mode.

``V4L2_CID_DETECT_MD_THRESHOLD_GRID (__u16 matrix)``
    Sets the motion detection thresholds for each cell in the grid. To
    be used with the ``V4L2_DETECT_MD_MODE_THRESHOLD_GRID`` motion
    detection mode. Matrix element (0, 0) represents the cell at the
    top-left of the grid.

``V4L2_CID_DETECT_MD_REGION_GRID (__u8 matrix)``
    Sets the motion detection region value for each cell in the grid. To
    be used with the ``V4L2_DETECT_MD_MODE_REGION_GRID`` motion
    detection mode. Matrix element (0, 0) represents the cell at the
    top-left of the grid.


.. _rf-tuner-controls:

RF Tuner Control Reference
==========================

The RF Tuner (RF_TUNER) class includes controls for common features of
devices having RF tuner.

In this context, RF tuner is radio receiver circuit between antenna and
demodulator. It receives radio frequency (RF) from the antenna and
converts that received signal to lower intermediate frequency (IF) or
baseband frequency (BB). Tuners that could do baseband output are often
called Zero-IF tuners. Older tuners were typically simple PLL tuners
inside a metal box, whilst newer ones are highly integrated chips
without a metal box "silicon tuners". These controls are mostly
applicable for new feature rich silicon tuners, just because older
tuners does not have much adjustable features.

For more information about RF tuners see
`Tuner (radio) <http://en.wikipedia.org/wiki/Tuner_%28radio%29>`__
and `RF front end <http://en.wikipedia.org/wiki/RF_front_end>`__
from Wikipedia.


.. _rf-tuner-control-id:

RF_TUNER Control IDs
--------------------

``V4L2_CID_RF_TUNER_CLASS (class)``
    The RF_TUNER class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

``V4L2_CID_RF_TUNER_BANDWIDTH_AUTO (boolean)``
    Enables/disables tuner radio channel bandwidth configuration. In
    automatic mode bandwidth configuration is performed by the driver.

``V4L2_CID_RF_TUNER_BANDWIDTH (integer)``
    Filter(s) on tuner signal path are used to filter signal according
    to receiving party needs. Driver configures filters to fulfill
    desired bandwidth requirement. Used when
    V4L2_CID_RF_TUNER_BANDWIDTH_AUTO is not set. Unit is in Hz. The
    range and step are driver-specific.

``V4L2_CID_RF_TUNER_LNA_GAIN_AUTO (boolean)``
    Enables/disables LNA automatic gain control (AGC)

``V4L2_CID_RF_TUNER_MIXER_GAIN_AUTO (boolean)``
    Enables/disables mixer automatic gain control (AGC)

``V4L2_CID_RF_TUNER_IF_GAIN_AUTO (boolean)``
    Enables/disables IF automatic gain control (AGC)

``V4L2_CID_RF_TUNER_RF_GAIN (integer)``
    The RF amplifier is the very first amplifier on the receiver signal
    path, just right after the antenna input. The difference between the
    LNA gain and the RF gain in this document is that the LNA gain is
    integrated in the tuner chip while the RF gain is a separate chip.
    There may be both RF and LNA gain controls in the same device. The
    range and step are driver-specific.

``V4L2_CID_RF_TUNER_LNA_GAIN (integer)``
    LNA (low noise amplifier) gain is first gain stage on the RF tuner
    signal path. It is located very close to tuner antenna input. Used
    when ``V4L2_CID_RF_TUNER_LNA_GAIN_AUTO`` is not set. See
    ``V4L2_CID_RF_TUNER_RF_GAIN`` to understand how RF gain and LNA gain
    differs from the each others. The range and step are
    driver-specific.

``V4L2_CID_RF_TUNER_MIXER_GAIN (integer)``
    Mixer gain is second gain stage on the RF tuner signal path. It is
    located inside mixer block, where RF signal is down-converted by the
    mixer. Used when ``V4L2_CID_RF_TUNER_MIXER_GAIN_AUTO`` is not set.
    The range and step are driver-specific.

``V4L2_CID_RF_TUNER_IF_GAIN (integer)``
    IF gain is last gain stage on the RF tuner signal path. It is
    located on output of RF tuner. It controls signal level of
    intermediate frequency output or baseband output. Used when
    ``V4L2_CID_RF_TUNER_IF_GAIN_AUTO`` is not set. The range and step
    are driver-specific.

``V4L2_CID_RF_TUNER_PLL_LOCK (boolean)``
    Is synthesizer PLL locked? RF tuner is receiving given frequency
    when that control is set. This is a read-only control.

.. [#f1]
   This control may be changed to a menu control in the future, if more
   options are required.
