.. -*- coding: utf-8; mode: rst -*-

.. _media-controller-types:

Types and flags used to represent the media graph elements
==========================================================

..  tabularcolumns:: |p{8.0cm}|p{10.5cm}|

.. _media-entity-type:

.. cssclass:: longtable

.. flat-table:: Media entity types
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       .. _MEDIA-ENT-F-UNKNOWN:
       .. _MEDIA-ENT-F-V4L2-SUBDEV-UNKNOWN:

       -  ``MEDIA_ENT_F_UNKNOWN`` and

	  ``MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN``

       -  Unknown entity. That generally indicates that a driver didn't
	  initialize properly the entity, with is a Kernel bug

    -  .. row 2

       ..  _MEDIA-ENT-F-IO-V4L:

       -  ``MEDIA_ENT_F_IO_V4L``

       -  Data streaming input and/or output entity.

    -  .. row 3

       ..  _MEDIA-ENT-F-IO-VBI:

       -  ``MEDIA_ENT_F_IO_VBI``

       -  V4L VBI streaming input or output entity

    -  .. row 4

       ..  _MEDIA-ENT-F-IO-SWRADIO:

       -  ``MEDIA_ENT_F_IO_SWRADIO``

       -  V4L Software Digital Radio (SDR) streaming input or output entity

    -  .. row 5

       ..  _MEDIA-ENT-F-IO-DTV:

       -  ``MEDIA_ENT_F_IO_DTV``

       -  DVB Digital TV streaming input or output entity

    -  .. row 6

       ..  _MEDIA-ENT-F-DTV-DEMOD:

       -  ``MEDIA_ENT_F_DTV_DEMOD``

       -  Digital TV demodulator entity.

    -  .. row 7

       ..  _MEDIA-ENT-F-TS-DEMUX:

       -  ``MEDIA_ENT_F_TS_DEMUX``

       -  MPEG Transport stream demux entity. Could be implemented on
	  hardware or in Kernelspace by the Linux DVB subsystem.

    -  .. row 8

       ..  _MEDIA-ENT-F-DTV-CA:

       -  ``MEDIA_ENT_F_DTV_CA``

       -  Digital TV Conditional Access module (CAM) entity

    -  .. row 9

       ..  _MEDIA-ENT-F-DTV-NET-DECAP:

       -  ``MEDIA_ENT_F_DTV_NET_DECAP``

       -  Digital TV network ULE/MLE desencapsulation entity. Could be
	  implemented on hardware or in Kernelspace

    -  .. row 10

       ..  _MEDIA-ENT-F-CONN-RF:

       -  ``MEDIA_ENT_F_CONN_RF``

       -  Connector for a Radio Frequency (RF) signal.

    -  .. row 11

       ..  _MEDIA-ENT-F-CONN-SVIDEO:

       -  ``MEDIA_ENT_F_CONN_SVIDEO``

       -  Connector for a S-Video signal.

    -  .. row 12

       ..  _MEDIA-ENT-F-CONN-COMPOSITE:

       -  ``MEDIA_ENT_F_CONN_COMPOSITE``

       -  Connector for a RGB composite signal.

    -  .. row 13

       ..  _MEDIA-ENT-F-CAM-SENSOR:

       -  ``MEDIA_ENT_F_CAM_SENSOR``

       -  Camera video sensor entity.

    -  .. row 14

       ..  _MEDIA-ENT-F-FLASH:

       -  ``MEDIA_ENT_F_FLASH``

       -  Flash controller entity.

    -  .. row 15

       ..  _MEDIA-ENT-F-LENS:

       -  ``MEDIA_ENT_F_LENS``

       -  Lens controller entity.

    -  .. row 16

       ..  _MEDIA-ENT-F-ATV-DECODER:

       -  ``MEDIA_ENT_F_ATV_DECODER``

       -  Analog video decoder, the basic function of the video decoder is
	  to accept analogue video from a wide variety of sources such as
	  broadcast, DVD players, cameras and video cassette recorders, in
	  either NTSC, PAL, SECAM or HD format, separating the stream into
	  its component parts, luminance and chrominance, and output it in
	  some digital video standard, with appropriate timing signals.

    -  .. row 17

       ..  _MEDIA-ENT-F-TUNER:

       -  ``MEDIA_ENT_F_TUNER``

       -  Digital TV, analog TV, radio and/or software radio tuner, with
	  consists on a PLL tuning stage that converts radio frequency (RF)
	  signal into an Intermediate Frequency (IF). Modern tuners have
	  internally IF-PLL decoders for audio and video, but older models
	  have those stages implemented on separate entities.

    -  .. row 18

       ..  _MEDIA-ENT-F-IF-VID-DECODER:

       -  ``MEDIA_ENT_F_IF_VID_DECODER``

       -  IF-PLL video decoder. It receives the IF from a PLL and decodes
	  the analog TV video signal. This is commonly found on some very
	  old analog tuners, like Philips MK3 designs. They all contain a
	  tda9887 (or some software compatible similar chip, like tda9885).
	  Those devices use a different I2C address than the tuner PLL.

    -  .. row 19

       ..  _MEDIA-ENT-F-IF-AUD-DECODER:

       -  ``MEDIA_ENT_F_IF_AUD_DECODER``

       -  IF-PLL sound decoder. It receives the IF from a PLL and decodes
	  the analog TV audio signal. This is commonly found on some very
	  old analog hardware, like Micronas msp3400, Philips tda9840,
	  tda985x, etc. Those devices use a different I2C address than the
	  tuner PLL and should be controlled together with the IF-PLL video
	  decoder.

    -  .. row 20

       ..  _MEDIA-ENT-F-AUDIO-CAPTURE:

       -  ``MEDIA_ENT_F_AUDIO_CAPTURE``

       -  Audio Capture Function Entity.

    -  .. row 21

       ..  _MEDIA-ENT-F-AUDIO-PLAYBACK:

       -  ``MEDIA_ENT_F_AUDIO_PLAYBACK``

       -  Audio Playback Function Entity.

    -  .. row 22

       ..  _MEDIA-ENT-F-AUDIO-MIXER:

       -  ``MEDIA_ENT_F_AUDIO_MIXER``

       -  Audio Mixer Function Entity.

    -  .. row 23

       ..  _MEDIA-ENT-F-PROC-VIDEO-COMPOSER:

       -  ``MEDIA_ENT_F_PROC_VIDEO_COMPOSER``

       -  Video composer (blender). An entity capable of video
	  composing must have at least two sink pads and one source
	  pad, and composes input video frames onto output video
	  frames. Composition can be performed using alpha blending,
	  color keying, raster operations (ROP), stitching or any other
	  means.

    -  ..  row 24

       ..  _MEDIA-ENT-F-PROC-VIDEO-PIXEL-FORMATTER:

       -  ``MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER``

       -  Video pixel formatter. An entity capable of pixel formatting
	  must have at least one sink pad and one source pad. Read
	  pixel formatters read pixels from memory and perform a subset
	  of unpacking, cropping, color keying, alpha multiplication
	  and pixel encoding conversion. Write pixel formatters perform
	  a subset of dithering, pixel encoding conversion and packing
	  and write pixels to memory.

    -  ..  row 25

       ..  _MEDIA-ENT-F-PROC-VIDEO-PIXEL-ENC-CONV:

       -  ``MEDIA_ENT_F_PROC_VIDEO_PIXEL_ENC_CONV``

       -  Video pixel encoding converter. An entity capable of pixel
	  enconding conversion must have at least one sink pad and one
	  source pad, and convert the encoding of pixels received on
	  its sink pad(s) to a different encoding output on its source
	  pad(s). Pixel encoding conversion includes but isn't limited
	  to RGB to/from HSV, RGB to/from YUV and CFA (Bayer) to RGB
	  conversions.

    -  ..  row 26

       ..  _MEDIA-ENT-F-PROC-VIDEO-LUT:

       -  ``MEDIA_ENT_F_PROC_VIDEO_LUT``

       -  Video look-up table. An entity capable of video lookup table
	  processing must have one sink pad and one source pad. It uses
	  the values of the pixels received on its sink pad to look up
	  entries in internal tables and output them on its source pad.
	  The lookup processing can be performed on all components
	  separately or combine them for multi-dimensional table
	  lookups.

    -  ..  row 27

       ..  _MEDIA-ENT-F-PROC-VIDEO-SCALER:

       -  ``MEDIA_ENT_F_PROC_VIDEO_SCALER``

       -  Video scaler. An entity capable of video scaling must have
	  at least one sink pad and one source pad, and scale the
	  video frame(s) received on its sink pad(s) to a different
	  resolution output on its source pad(s). The range of
	  supported scaling ratios is entity-specific and can differ
	  between the horizontal and vertical directions (in particular
	  scaling can be supported in one direction only). Binning and
	  skipping are considered as scaling.

    -  ..  row 28

       ..  _MEDIA-ENT-F-PROC-VIDEO-STATISTICS:

       -  ``MEDIA_ENT_F_PROC_VIDEO_STATISTICS``

       -  Video statistics computation (histogram, 3A, ...). An entity
	  capable of statistics computation must have one sink pad and
	  one source pad. It computes statistics over the frames
	  received on its sink pad and outputs the statistics data on
	  its source pad.


..  tabularcolumns:: |p{5.5cm}|p{12.0cm}|

.. _media-entity-flag:

.. flat-table:: Media entity flags
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       ..  _MEDIA-ENT-FL-DEFAULT:

       -  ``MEDIA_ENT_FL_DEFAULT``

       -  Default entity for its type. Used to discover the default audio,
	  VBI and video devices, the default camera sensor, ...

    -  .. row 2

       ..  _MEDIA-ENT-FL-CONNECTOR:

       -  ``MEDIA_ENT_FL_CONNECTOR``

       -  The entity represents a data conector


..  tabularcolumns:: |p{6.5cm}|p{6.0cm}|p{5.0cm}|

.. _media-intf-type:

.. flat-table:: Media interface types
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       ..  _MEDIA-INTF-T-DVB-FE:

       -  ``MEDIA_INTF_T_DVB_FE``

       -  Device node interface for the Digital TV frontend

       -  typically, /dev/dvb/adapter?/frontend?

    -  .. row 2

       ..  _MEDIA-INTF-T-DVB-DEMUX:

       -  ``MEDIA_INTF_T_DVB_DEMUX``

       -  Device node interface for the Digital TV demux

       -  typically, /dev/dvb/adapter?/demux?

    -  .. row 3

       ..  _MEDIA-INTF-T-DVB-DVR:

       -  ``MEDIA_INTF_T_DVB_DVR``

       -  Device node interface for the Digital TV DVR

       -  typically, /dev/dvb/adapter?/dvr?

    -  .. row 4

       ..  _MEDIA-INTF-T-DVB-CA:

       -  ``MEDIA_INTF_T_DVB_CA``

       -  Device node interface for the Digital TV Conditional Access

       -  typically, /dev/dvb/adapter?/ca?

    -  .. row 5

       ..  _MEDIA-INTF-T-DVB-NET:

       -  ``MEDIA_INTF_T_DVB_NET``

       -  Device node interface for the Digital TV network control

       -  typically, /dev/dvb/adapter?/net?

    -  .. row 6

       ..  _MEDIA-INTF-T-V4L-VIDEO:

       -  ``MEDIA_INTF_T_V4L_VIDEO``

       -  Device node interface for video (V4L)

       -  typically, /dev/video?

    -  .. row 7

       ..  _MEDIA-INTF-T-V4L-VBI:

       -  ``MEDIA_INTF_T_V4L_VBI``

       -  Device node interface for VBI (V4L)

       -  typically, /dev/vbi?

    -  .. row 8

       ..  _MEDIA-INTF-T-V4L-RADIO:

       -  ``MEDIA_INTF_T_V4L_RADIO``

       -  Device node interface for radio (V4L)

       -  typically, /dev/vbi?

    -  .. row 9

       ..  _MEDIA-INTF-T-V4L-SUBDEV:

       -  ``MEDIA_INTF_T_V4L_SUBDEV``

       -  Device node interface for a V4L subdevice

       -  typically, /dev/v4l-subdev?

    -  .. row 10

       ..  _MEDIA-INTF-T-V4L-SWRADIO:

       -  ``MEDIA_INTF_T_V4L_SWRADIO``

       -  Device node interface for Software Defined Radio (V4L)

       -  typically, /dev/swradio?

    -  .. row 11

       ..  _MEDIA-INTF-T-V4L-TOUCH:

       -  ``MEDIA_INTF_T_V4L_TOUCH``

       -  Device node interface for Touch device (V4L)

       -  typically, /dev/v4l-touch?

    -  .. row 12

       ..  _MEDIA-INTF-T-ALSA-PCM-CAPTURE:

       -  ``MEDIA_INTF_T_ALSA_PCM_CAPTURE``

       -  Device node interface for ALSA PCM Capture

       -  typically, /dev/snd/pcmC?D?c

    -  .. row 13

       ..  _MEDIA-INTF-T-ALSA-PCM-PLAYBACK:

       -  ``MEDIA_INTF_T_ALSA_PCM_PLAYBACK``

       -  Device node interface for ALSA PCM Playback

       -  typically, /dev/snd/pcmC?D?p

    -  .. row 14

       ..  _MEDIA-INTF-T-ALSA-CONTROL:

       -  ``MEDIA_INTF_T_ALSA_CONTROL``

       -  Device node interface for ALSA Control

       -  typically, /dev/snd/controlC?

    -  .. row 15

       ..  _MEDIA-INTF-T-ALSA-COMPRESS:

       -  ``MEDIA_INTF_T_ALSA_COMPRESS``

       -  Device node interface for ALSA Compress

       -  typically, /dev/snd/compr?

    -  .. row 16

       ..  _MEDIA-INTF-T-ALSA-RAWMIDI:

       -  ``MEDIA_INTF_T_ALSA_RAWMIDI``

       -  Device node interface for ALSA Raw MIDI

       -  typically, /dev/snd/midi?

    -  .. row 17

       ..  _MEDIA-INTF-T-ALSA-HWDEP:

       -  ``MEDIA_INTF_T_ALSA_HWDEP``

       -  Device node interface for ALSA Hardware Dependent

       -  typically, /dev/snd/hwC?D?

    -  .. row 18

       ..  _MEDIA-INTF-T-ALSA-SEQUENCER:

       -  ``MEDIA_INTF_T_ALSA_SEQUENCER``

       -  Device node interface for ALSA Sequencer

       -  typically, /dev/snd/seq

    -  .. row 19

       ..  _MEDIA-INTF-T-ALSA-TIMER:

       -  ``MEDIA_INTF_T_ALSA_TIMER``

       -  Device node interface for ALSA Timer

       -  typically, /dev/snd/timer


.. tabularcolumns:: |p{5.5cm}|p{12.0cm}|

.. _media-pad-flag:

.. flat-table:: Media pad flags
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       ..  _MEDIA-PAD-FL-SINK:

       -  ``MEDIA_PAD_FL_SINK``

       -  Input pad, relative to the entity. Input pads sink data and are
	  targets of links.

    -  .. row 2

       ..  _MEDIA-PAD-FL-SOURCE:

       -  ``MEDIA_PAD_FL_SOURCE``

       -  Output pad, relative to the entity. Output pads source data and
	  are origins of links.

    -  .. row 3

       ..  _MEDIA-PAD-FL-MUST-CONNECT:

       -  ``MEDIA_PAD_FL_MUST_CONNECT``

       -  If this flag is set and the pad is linked to any other pad, then
	  at least one of those links must be enabled for the entity to be
	  able to stream. There could be temporary reasons (e.g. device
	  configuration dependent) for the pad to need enabled links even
	  when this flag isn't set; the absence of the flag doesn't imply
	  there is none.


One and only one of ``MEDIA_PAD_FL_SINK`` and ``MEDIA_PAD_FL_SOURCE``
must be set for every pad.

.. tabularcolumns:: |p{5.5cm}|p{12.0cm}|

.. _media-link-flag:

.. flat-table:: Media link flags
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       ..  _MEDIA-LNK-FL-ENABLED:

       -  ``MEDIA_LNK_FL_ENABLED``

       -  The link is enabled and can be used to transfer media data. When
	  two or more links target a sink pad, only one of them can be
	  enabled at a time.

    -  .. row 2

       ..  _MEDIA-LNK-FL-IMMUTABLE:

       -  ``MEDIA_LNK_FL_IMMUTABLE``

       -  The link enabled state can't be modified at runtime. An immutable
	  link is always enabled.

    -  .. row 3

       ..  _MEDIA-LNK-FL-DYNAMIC:

       -  ``MEDIA_LNK_FL_DYNAMIC``

       -  The link enabled state can be modified during streaming. This flag
	  is set by drivers and is read-only for applications.

    -  .. row 4

       ..  _MEDIA-LNK-FL-LINK-TYPE:

       -  ``MEDIA_LNK_FL_LINK_TYPE``

       -  This is a bitmask that defines the type of the link. Currently,
	  two types of links are supported:

	  .. _MEDIA-LNK-FL-DATA-LINK:

	  ``MEDIA_LNK_FL_DATA_LINK`` if the link is between two pads

	  .. _MEDIA-LNK-FL-INTERFACE-LINK:

	  ``MEDIA_LNK_FL_INTERFACE_LINK`` if the link is between an
	  interface and an entity
