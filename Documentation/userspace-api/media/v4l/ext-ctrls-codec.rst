.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _codec-controls:

***********************
Codec Control Reference
***********************

Below all controls within the Codec control class are described. First
the generic controls, then controls specific for certain hardware.

.. note::

   These controls are applicable to all codecs and not just MPEG. The
   defines are prefixed with V4L2_CID_MPEG/V4L2_MPEG as the controls
   were originally made for MPEG codecs and later extended to cover all
   encoding formats.


Generic Codec Controls
======================


.. _mpeg-control-id:

Codec Control IDs
-----------------

``V4L2_CID_CODEC_CLASS (class)``
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



.. tabularcolumns:: |p{6.6 cm}|p{10.9cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_STREAM_VBI_FMT_NONE``
      - No VBI in the MPEG stream
    * - ``V4L2_MPEG_STREAM_VBI_FMT_IVTV``
      - VBI in private packets, IVTV format (documented in the kernel
	sources in the file
	``Documentation/userspace-api/media/drivers/cx2341x-uapi.rst``)



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

.. tabularcolumns:: |p{9.1cm}|p{8.4cm}|

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



.. tabularcolumns:: |p{9.8cm}|p{7.7cm}|

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
    * - ``V4L2_MPEG_VIDEO_BITRATE_MODE_CQ``
      - Constant quality



``V4L2_CID_MPEG_VIDEO_BITRATE (integer)``
    Average video bitrate in bits per second.

``V4L2_CID_MPEG_VIDEO_BITRATE_PEAK (integer)``
    Peak video bitrate in bits per second. Must be larger or equal to
    the average video bitrate. It is ignored if the video bitrate mode
    is set to constant bitrate.

``V4L2_CID_MPEG_VIDEO_CONSTANT_QUALITY (integer)``
    Constant quality level control. This control is applicable when
    ``V4L2_CID_MPEG_VIDEO_BITRATE_MODE`` value is
    ``V4L2_MPEG_VIDEO_BITRATE_MODE_CQ``. Valid range is 1 to 100
    where 1 indicates lowest quality and 100 indicates highest quality.
    Encoder will decide the appropriate quantization parameter and
    bitrate to produce requested frame quality.


``V4L2_CID_MPEG_VIDEO_FRAME_SKIP_MODE (enum)``

enum v4l2_mpeg_video_frame_skip_mode -
    Indicates in what conditions the encoder should skip frames. If
    encoding a frame would cause the encoded stream to be larger then a
    chosen data limit then the frame will be skipped. Possible values
    are:


.. tabularcolumns:: |p{8.2cm}|p{9.3cm}|

.. raw:: latex

    \small

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED``
      - Frame skip mode is disabled.
    * - ``V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_LEVEL_LIMIT``
      - Frame skip mode enabled and buffer limit is set by the chosen
        level and is defined by the standard.
    * - ``V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT``
      - Frame skip mode enabled and buffer limit is set by the
        :ref:`VBV (MPEG1/2/4) <v4l2-mpeg-video-vbv-size>` or
        :ref:`CPB (H264) buffer size <v4l2-mpeg-video-h264-cpb-size>` control.

.. raw:: latex

    \normalsize

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

``V4L2_CID_MPEG_VIDEO_DEC_CONCEAL_COLOR (integer64)``
    This control sets the conceal color in YUV color space. It describes
    the client preference of the error conceal color in case of an error
    where the reference frame is missing. The decoder should fill the
    reference buffer with the preferred color and use it for future
    decoding. The control is using 16 bits per channel.
    Applicable to decoders.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * -
      - 8bit  format
      - 10bit format
      - 12bit format
    * - Y luminance
      - Bit 0:7
      - Bit 0:9
      - Bit 0:11
    * - Cb chrominance
      - Bit 16:23
      - Bit 16:25
      - Bit 16:27
    * - Cr chrominance
      - Bit 32:39
      - Bit 32:41
      - Bit 32:43
    * - Must be zero
      - Bit 48:63
      - Bit 48:63
      - Bit 48:63

``V4L2_CID_MPEG_VIDEO_DECODER_SLICE_INTERFACE (boolean)``
    If enabled the decoder expects to receive a single slice per buffer,
    otherwise the decoder expects a single frame in per buffer.
    Applicable to the decoder, all codecs.

``V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY_ENABLE (boolean)``
    If the display delay is enabled then the decoder is forced to return
    a CAPTURE buffer (decoded frame) after processing a certain number
    of OUTPUT buffers. The delay can be set through
    ``V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY``. This
    feature can be used for example for generating thumbnails of videos.
    Applicable to the decoder.

``V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY (integer)``
    Display delay value for decoder. The decoder is forced to
    return a decoded frame after the set 'display delay' number of
    frames. If this number is low it may result in frames returned out
    of display order, in addition the hardware may still be using the
    returned buffer as a reference picture for subsequent frames.

``V4L2_CID_MPEG_VIDEO_AU_DELIMITER (boolean)``
    If enabled then, AUD (Access Unit Delimiter) NALUs will be generated.
    That could be useful to find the start of a frame without having to
    fully parse each NALU. Applicable to the H264 and HEVC encoders.

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
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_5_2``
      - Level 5.2
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_6_0``
      - Level 6.0
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_6_1``
      - Level 6.1
    * - ``V4L2_MPEG_VIDEO_H264_LEVEL_6_2``
      - Level 6.2



.. _v4l2-mpeg-video-mpeg2-level:

``V4L2_CID_MPEG_VIDEO_MPEG2_LEVEL``
    (enum)

enum v4l2_mpeg_video_mpeg2_level -
    The level information for the MPEG2 elementary stream. Applicable to
    MPEG2 codecs. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_MPEG2_LEVEL_LOW``
      - Low Level (LL)
    * - ``V4L2_MPEG_VIDEO_MPEG2_LEVEL_MAIN``
      - Main Level (ML)
    * - ``V4L2_MPEG_VIDEO_MPEG2_LEVEL_HIGH_1440``
      - High-1440 Level (H-14)
    * - ``V4L2_MPEG_VIDEO_MPEG2_LEVEL_HIGH``
      - High Level (HL)



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

.. raw:: latex

    \small

.. tabularcolumns:: |p{10.2cm}|p{7.3cm}|

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
    * - ``V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH``
      - Constrained High profile

.. raw:: latex

    \normalsize

.. _v4l2-mpeg-video-mpeg2-profile:

``V4L2_CID_MPEG_VIDEO_MPEG2_PROFILE``
    (enum)

enum v4l2_mpeg_video_mpeg2_profile -
    The profile information for MPEG2. Applicable to MPEG2 codecs.
    Possible values are:

.. raw:: latex

    \small

.. tabularcolumns:: |p{10.2cm}|p{7.3cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_MPEG2_PROFILE_SIMPLE``
      - Simple profile (SP)
    * - ``V4L2_MPEG_VIDEO_MPEG2_PROFILE_MAIN``
      - Main profile (MP)
    * - ``V4L2_MPEG_VIDEO_MPEG2_PROFILE_SNR_SCALABLE``
      - SNR Scalable profile (SNR)
    * - ``V4L2_MPEG_VIDEO_MPEG2_PROFILE_SPATIALLY_SCALABLE``
      - Spatially Scalable profile (Spt)
    * - ``V4L2_MPEG_VIDEO_MPEG2_PROFILE_HIGH``
      - High profile (HP)
    * - ``V4L2_MPEG_VIDEO_MPEG2_PROFILE_MULTIVIEW``
      - Multi-view profile (MVP)


.. raw:: latex

    \normalsize

.. _v4l2-mpeg-video-mpeg4-profile:

``V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE``
    (enum)

enum v4l2_mpeg_video_mpeg4_profile -
    The profile information for MPEG4. Applicable to the MPEG4 encoder.
    Possible values are:

.. raw:: latex

    \small

.. tabularcolumns:: |p{11.8cm}|p{5.7cm}|

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
      - Advanced Coding Efficiency profile

.. raw:: latex

    \normalsize

``V4L2_CID_MPEG_VIDEO_MAX_REF_PIC (integer)``
    The maximum number of reference pictures used for encoding.
    Applicable to the encoder.

.. _v4l2-mpeg-video-multi-slice-mode:

``V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE``
    (enum)

enum v4l2_mpeg_video_multi_slice_mode -
    Determines how the encoder should handle division of frame into
    slices. Applicable to the encoder. Possible values are:



.. tabularcolumns:: |p{9.6cm}|p{7.9cm}|

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

.. raw:: latex

    \small

.. tabularcolumns:: |p{13.5cm}|p{4.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED``
      - Loop filter is enabled.
    * - ``V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED``
      - Loop filter is disabled.
    * - ``V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY``
      - Loop filter is disabled at the slice boundary.

.. raw:: latex

    \normalsize


``V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA (integer)``
    Loop filter alpha coefficient, defined in the H264 standard.
    This value corresponds to the slice_alpha_c0_offset_div2 slice header
    field, and should be in the range of -6 to +6, inclusive. The actual alpha
    offset FilterOffsetA is twice this value.
    Applicable to the H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA (integer)``
    Loop filter beta coefficient, defined in the H264 standard.
    This corresponds to the slice_beta_offset_div2 slice header field, and
    should be in the range of -6 to +6, inclusive. The actual beta offset
    FilterOffsetB is twice this value.
    Applicable to the H264 encoder.

.. _v4l2-mpeg-video-h264-entropy-mode:

``V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE``
    (enum)

enum v4l2_mpeg_video_h264_entropy_mode -
    Entropy coding mode for H264 - CABAC/CAVALC. Applicable to the H264
    encoder. Possible values are:


.. tabularcolumns:: |p{9.0cm}|p{8.5cm}|


.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC``
      - Use CAVLC entropy coding.
    * - ``V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC``
      - Use CABAC entropy coding.



``V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM (boolean)``
    Enable 8X8 transform for H264. Applicable to the H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_CONSTRAINED_INTRA_PREDICTION (boolean)``
    Enable constrained intra prediction for H264. Applicable to the H264
    encoder.

``V4L2_CID_MPEG_VIDEO_H264_CHROMA_QP_INDEX_OFFSET (integer)``
    Specify the offset that should be added to the luma quantization
    parameter to determine the chroma quantization parameter. Applicable
    to the H264 encoder.

``V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB (integer)``
    Cyclic intra macroblock refresh. This is the number of continuous
    macroblocks refreshed every frame. Each frame a successive set of
    macroblocks is refreshed until the cycle completes and starts from
    the top of the frame. Setting this control to zero means that
    macroblocks will not be refreshed.  Note that this control will not
    take effect when ``V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD`` control
    is set to non zero value.
    Applicable to H264, H263 and MPEG4 encoder.

``V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD (integer)``
    Intra macroblock refresh period. This sets the period to refresh
    the whole frame. In other words, this defines the number of frames
    for which the whole frame will be intra-refreshed.  An example:
    setting period to 1 means that the whole frame will be refreshed,
    setting period to 2 means that the half of macroblocks will be
    intra-refreshed on frameX and the other half of macroblocks
    will be refreshed in frameX + 1 and so on. Setting the period to
    zero means no period is specified.
    Note that if the client sets this control to non zero value the
    ``V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB`` control shall be
    ignored. Applicable to H264 and HEVC encoders.

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

``V4L2_CID_MPEG_VIDEO_H264_I_FRAME_MIN_QP (integer)``
    Minimum quantization parameter for the H264 I frame to limit I frame
    quality to a range. Valid range: from 0 to 51. If
    V4L2_CID_MPEG_VIDEO_H264_MIN_QP is also set, the quantization parameter
    should be chosen to meet both requirements.

``V4L2_CID_MPEG_VIDEO_H264_I_FRAME_MAX_QP (integer)``
    Maximum quantization parameter for the H264 I frame to limit I frame
    quality to a range. Valid range: from 0 to 51. If
    V4L2_CID_MPEG_VIDEO_H264_MAX_QP is also set, the quantization parameter
    should be chosen to meet both requirements.

``V4L2_CID_MPEG_VIDEO_H264_P_FRAME_MIN_QP (integer)``
    Minimum quantization parameter for the H264 P frame to limit P frame
    quality to a range. Valid range: from 0 to 51. If
    V4L2_CID_MPEG_VIDEO_H264_MIN_QP is also set, the quantization parameter
    should be chosen to meet both requirements.

``V4L2_CID_MPEG_VIDEO_H264_P_FRAME_MAX_QP (integer)``
    Maximum quantization parameter for the H264 P frame to limit P frame
    quality to a range. Valid range: from 0 to 51. If
    V4L2_CID_MPEG_VIDEO_H264_MAX_QP is also set, the quantization parameter
    should be chosen to meet both requirements.

``V4L2_CID_MPEG_VIDEO_H264_B_FRAME_MIN_QP (integer)``
    Minimum quantization parameter for the H264 B frame to limit B frame
    quality to a range. Valid range: from 0 to 51. If
    V4L2_CID_MPEG_VIDEO_H264_MIN_QP is also set, the quantization parameter
    should be chosen to meet both requirements.

``V4L2_CID_MPEG_VIDEO_H264_B_FRAME_MAX_QP (integer)``
    Maximum quantization parameter for the H264 B frame to limit B frame
    quality to a range. Valid range: from 0 to 51. If
    V4L2_CID_MPEG_VIDEO_H264_MAX_QP is also set, the quantization parameter
    should be chosen to meet both requirements.

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

.. _v4l2-mpeg-video-vbv-size:

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

.. _v4l2-mpeg-video-h264-cpb-size:

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

.. raw:: latex

    \small

.. tabularcolumns:: |p{10.3cm}|p{7.2cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE``
      - The stream header is returned separately in the first buffer.
    * - ``V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME``
      - The stream header is returned together with the first encoded
	frame.

.. raw:: latex

    \normalsize


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

.. raw:: latex

    \small

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

.. raw:: latex

    \normalsize



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

.. raw:: latex

    \small

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

.. raw:: latex

    \normalsize



``V4L2_CID_MPEG_VIDEO_H264_FMO_SLICE_GROUP (integer)``
    Number of slice groups in FMO. Applicable to the H264 encoder.

.. _v4l2-mpeg-video-h264-fmo-change-direction:

``V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_DIRECTION``
    (enum)

enum v4l2_mpeg_video_h264_fmo_change_dir -
    Specifies a direction of the slice group change for raster and wipe
    maps. Applicable to the H264 encoder. Possible values are:

.. tabularcolumns:: |p{9.6cm}|p{7.9cm}|

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

``V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L0_BR (integer)``
    Indicates bit rate (bps) for hierarchical coding layer 0 for H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L1_BR (integer)``
    Indicates bit rate (bps) for hierarchical coding layer 1 for H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L2_BR (integer)``
    Indicates bit rate (bps) for hierarchical coding layer 2 for H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L3_BR (integer)``
    Indicates bit rate (bps) for hierarchical coding layer 3 for H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L4_BR (integer)``
    Indicates bit rate (bps) for hierarchical coding layer 4 for H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L5_BR (integer)``
    Indicates bit rate (bps) for hierarchical coding layer 5 for H264 encoder.

``V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L6_BR (integer)``
    Indicates bit rate (bps) for hierarchical coding layer 6 for H264 encoder.

``V4L2_CID_FWHT_I_FRAME_QP (integer)``
    Quantization parameter for an I frame for FWHT. Valid range: from 1
    to 31.

``V4L2_CID_FWHT_P_FRAME_QP (integer)``
    Quantization parameter for a P frame for FWHT. Valid range: from 1
    to 31.

.. raw:: latex

    \normalsize


MFC 5.1 MPEG Controls
=====================

The following MPEG class controls deal with MPEG decoding and encoding
settings that are specific to the Multi Format Codec 5.1 device present
in the S5P family of SoCs by Samsung.


.. _mfc51-control-id:

MFC 5.1 Control IDs
-------------------

``V4L2_CID_MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY_ENABLE (boolean)``
    If the display delay is enabled then the decoder is forced to return
    a CAPTURE buffer (decoded frame) after processing a certain number
    of OUTPUT buffers. The delay can be set through
    ``V4L2_CID_MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY``. This
    feature can be used for example for generating thumbnails of videos.
    Applicable to the H264 decoder.

    .. note::

       This control is deprecated. Use the standard
       ``V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY_ENABLE`` control instead.

``V4L2_CID_MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY (integer)``
    Display delay value for H264 decoder. The decoder is forced to
    return a decoded frame after the set 'display delay' number of
    frames. If this number is low it may result in frames returned out
    of display order, in addition the hardware may still be using the
    returned buffer as a reference picture for subsequent frames.

    .. note::

       This control is deprecated. Use the standard
       ``V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY`` control instead.

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

    .. note::

       This control is deprecated. Use the standard
       ``V4L2_CID_MPEG_VIDEO_FRAME_SKIP_MODE`` control instead.

enum v4l2_mpeg_mfc51_video_frame_skip_mode -
    Indicates in what conditions the encoder should skip frames. If
    encoding a frame would cause the encoded stream to be larger then a
    chosen data limit then the frame will be skipped. Possible values
    are:


.. tabularcolumns:: |p{9.4cm}|p{8.1cm}|

.. raw:: latex

    \small

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_DISABLED``
      - Frame skip mode is disabled.
    * - ``V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_LEVEL_LIMIT``
      - Frame skip mode enabled and buffer limit is set by the chosen
	level and is defined by the standard.
    * - ``V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT``
      - Frame skip mode enabled and buffer limit is set by the VBV
	(MPEG1/2/4) or CPB (H264) buffer size control.

.. raw:: latex

    \normalsize

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

.. tabularcolumns:: |p{9.9cm}|p{7.6cm}|

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
=====================

The following MPEG class controls deal with MPEG encoding settings that
are specific to the Conexant CX23415 and CX23416 MPEG encoding chips.


.. _cx2341x-control-id:

CX2341x Control IDs
-------------------

.. _v4l2-mpeg-cx2341x-video-spatial-filter-mode:

``V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE``
    (enum)

enum v4l2_mpeg_cx2341x_video_spatial_filter_mode -
    Sets the Spatial Filter mode (default ``MANUAL``). Possible values
    are:


.. tabularcolumns:: |p{11.5cm}|p{6.0cm}|

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

.. tabularcolumns:: |p{13.1cm}|p{4.4cm}|

.. raw:: latex

    \footnotesize

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

.. raw:: latex

    \normalsize

.. _chroma-spatial-filter-type:

``V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE``
    (enum)

enum v4l2_mpeg_cx2341x_video_chroma_spatial_filter_type -
    Select the algorithm for the Chroma Spatial Filter (default
    ``1D_HOR``). Possible values are:

.. raw:: latex

    \footnotesize

.. tabularcolumns:: |p{11.0cm}|p{6.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_OFF``
      - No filter
    * - ``V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_1D_HOR``
      - One-dimensional horizontal

.. raw:: latex

    \normalsize

.. _v4l2-mpeg-cx2341x-video-temporal-filter-mode:

``V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE``
    (enum)

enum v4l2_mpeg_cx2341x_video_temporal_filter_mode -
    Sets the Temporal Filter mode (default ``MANUAL``). Possible values
    are:

.. raw:: latex

    \footnotesize

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_MANUAL``
      - Choose the filter manually
    * - ``V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_AUTO``
      - Choose the filter automatically

.. raw:: latex

    \normalsize

``V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER (integer (0-31))``
    The setting for the Temporal Filter. 0 = off, 31 = maximum. (Default
    is 8 for full-scale capturing and 0 for scaled capturing.)

.. _v4l2-mpeg-cx2341x-video-median-filter-type:

``V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE``
    (enum)

enum v4l2_mpeg_cx2341x_video_median_filter_type -
    Median Filter Type (default ``OFF``). Possible values are:


.. raw:: latex

    \small

.. tabularcolumns:: |p{11.0cm}|p{6.5cm}|

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

.. raw:: latex

    \normalsize

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
=====================

The VPX controls include controls for encoding parameters of VPx video
codec.


.. _vpx-control-id:

VPX Control IDs
---------------

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

.. tabularcolumns:: |p{7.5cm}|p{7.5cm}|

.. raw:: latex

    \small

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

.. raw:: latex

    \normalsize



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

    \scriptsize

.. tabularcolumns:: |p{8.6cm}|p{8.9cm}|

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

.. _v4l2-mpeg-video-vp8-profile:

``V4L2_CID_MPEG_VIDEO_VP8_PROFILE``
    (enum)

enum v4l2_mpeg_video_vp8_profile -
    This control allows selecting the profile for VP8 encoder.
    This is also used to enumerate supported profiles by VP8 encoder or decoder.
    Possible values are:

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_VP8_PROFILE_0``
      - Profile 0
    * - ``V4L2_MPEG_VIDEO_VP8_PROFILE_1``
      - Profile 1
    * - ``V4L2_MPEG_VIDEO_VP8_PROFILE_2``
      - Profile 2
    * - ``V4L2_MPEG_VIDEO_VP8_PROFILE_3``
      - Profile 3

.. _v4l2-mpeg-video-vp9-profile:

``V4L2_CID_MPEG_VIDEO_VP9_PROFILE``
    (enum)

enum v4l2_mpeg_video_vp9_profile -
    This control allows selecting the profile for VP9 encoder.
    This is also used to enumerate supported profiles by VP9 encoder or decoder.
    Possible values are:

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_VP9_PROFILE_0``
      - Profile 0
    * - ``V4L2_MPEG_VIDEO_VP9_PROFILE_1``
      - Profile 1
    * - ``V4L2_MPEG_VIDEO_VP9_PROFILE_2``
      - Profile 2
    * - ``V4L2_MPEG_VIDEO_VP9_PROFILE_3``
      - Profile 3

.. _v4l2-mpeg-video-vp9-level:

``V4L2_CID_MPEG_VIDEO_VP9_LEVEL (enum)``

enum v4l2_mpeg_video_vp9_level -
    This control allows selecting the level for VP9 encoder.
    This is also used to enumerate supported levels by VP9 encoder or decoder.
    More information can be found at
    `webmproject <https://www.webmproject.org/vp9/levels/>`__. Possible values are:

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_1_0``
      - Level 1
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_1_1``
      - Level 1.1
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_2_0``
      - Level 2
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_2_1``
      - Level 2.1
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_3_0``
      - Level 3
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_3_1``
      - Level 3.1
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_4_0``
      - Level 4
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_4_1``
      - Level 4.1
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_5_0``
      - Level 5
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_5_1``
      - Level 5.1
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_5_2``
      - Level 5.2
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_6_0``
      - Level 6
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_6_1``
      - Level 6.1
    * - ``V4L2_MPEG_VIDEO_VP9_LEVEL_6_2``
      - Level 6.2


High Efficiency Video Coding (HEVC/H.265) Control Reference
===========================================================

The HEVC/H.265 controls include controls for encoding parameters of HEVC/H.265
video codec.


.. _hevc-control-id:

HEVC/H.265 Control IDs
----------------------

``V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP (integer)``
    Minimum quantization parameter for HEVC.
    Valid range: from 0 to 51 for 8 bit and from 0 to 63 for 10 bit.

``V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP (integer)``
    Maximum quantization parameter for HEVC.
    Valid range: from 0 to 51 for 8 bit and from 0 to 63 for 10 bit.

``V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_QP (integer)``
    Quantization parameter for an I frame for HEVC.
    Valid range: [V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP,
    V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP].

``V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_QP (integer)``
    Quantization parameter for a P frame for HEVC.
    Valid range: [V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP,
    V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP].

``V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_QP (integer)``
    Quantization parameter for a B frame for HEVC.
    Valid range: [V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP,
    V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP].

``V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_MIN_QP (integer)``
    Minimum quantization parameter for the HEVC I frame to limit I frame
    quality to a range. Valid range: from 0 to 51 for 8 bit and from 0 to 63 for 10 bit.
    If V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP is also set, the quantization parameter
    should be chosen to meet both requirements.

``V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_MAX_QP (integer)``
    Maximum quantization parameter for the HEVC I frame to limit I frame
    quality to a range. Valid range: from 0 to 51 for 8 bit and from 0 to 63 for 10 bit.
    If V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP is also set, the quantization parameter
    should be chosen to meet both requirements.

``V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_MIN_QP (integer)``
    Minimum quantization parameter for the HEVC P frame to limit P frame
    quality to a range. Valid range: from 0 to 51 for 8 bit and from 0 to 63 for 10 bit.
    If V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP is also set, the quantization parameter
    should be chosen to meet both requirements.

``V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_MAX_QP (integer)``
    Maximum quantization parameter for the HEVC P frame to limit P frame
    quality to a range. Valid range: from 0 to 51 for 8 bit and from 0 to 63 for 10 bit.
    If V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP is also set, the quantization parameter
    should be chosen to meet both requirements.

``V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_MIN_QP (integer)``
    Minimum quantization parameter for the HEVC B frame to limit B frame
    quality to a range. Valid range: from 0 to 51 for 8 bit and from 0 to 63 for 10 bit.
    If V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP is also set, the quantization parameter
    should be chosen to meet both requirements.

``V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_MAX_QP (integer)``
    Maximum quantization parameter for the HEVC B frame to limit B frame
    quality to a range. Valid range: from 0 to 51 for 8 bit and from 0 to 63 for 10 bit.
    If V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP is also set, the quantization parameter
    should be chosen to meet both requirements.

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_QP (boolean)``
    HIERARCHICAL_QP allows the host to specify the quantization parameter
    values for each temporal layer through HIERARCHICAL_QP_LAYER. This is
    valid only if HIERARCHICAL_CODING_LAYER is greater than 1. Setting the
    control value to 1 enables setting of the QP values for the layers.

.. _v4l2-hevc-hier-coding-type:

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_TYPE``
    (enum)

enum v4l2_mpeg_video_hevc_hier_coding_type -
    Selects the hierarchical coding type for encoding. Possible values are:

.. raw:: latex

    \footnotesize

.. tabularcolumns:: |p{8.2cm}|p{9.3cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_HEVC_HIERARCHICAL_CODING_B``
      - Use the B frame for hierarchical coding.
    * - ``V4L2_MPEG_VIDEO_HEVC_HIERARCHICAL_CODING_P``
      - Use the P frame for hierarchical coding.

.. raw:: latex

    \normalsize


``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_LAYER (integer)``
    Selects the hierarchical coding layer. In normal encoding
    (non-hierarchial coding), it should be zero. Possible values are [0, 6].
    0 indicates HIERARCHICAL CODING LAYER 0, 1 indicates HIERARCHICAL CODING
    LAYER 1 and so on.

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L0_QP (integer)``
    Indicates quantization parameter for hierarchical coding layer 0.
    Valid range: [V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP,
    V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP].

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L1_QP (integer)``
    Indicates quantization parameter for hierarchical coding layer 1.
    Valid range: [V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP,
    V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP].

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L2_QP (integer)``
    Indicates quantization parameter for hierarchical coding layer 2.
    Valid range: [V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP,
    V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP].

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L3_QP (integer)``
    Indicates quantization parameter for hierarchical coding layer 3.
    Valid range: [V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP,
    V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP].

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L4_QP (integer)``
    Indicates quantization parameter for hierarchical coding layer 4.
    Valid range: [V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP,
    V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP].

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L5_QP (integer)``
    Indicates quantization parameter for hierarchical coding layer 5.
    Valid range: [V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP,
    V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP].

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L6_QP (integer)``
    Indicates quantization parameter for hierarchical coding layer 6.
    Valid range: [V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP,
    V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP].

.. _v4l2-hevc-profile:

``V4L2_CID_MPEG_VIDEO_HEVC_PROFILE``
    (enum)

enum v4l2_mpeg_video_hevc_profile -
    Select the desired profile for HEVC encoder.

.. raw:: latex

    \footnotesize

.. tabularcolumns:: |p{9.0cm}|p{8.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN``
      - Main profile.
    * - ``V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE``
      - Main still picture profile.
    * - ``V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10``
      - Main 10 profile.

.. raw:: latex

    \normalsize


.. _v4l2-hevc-level:

``V4L2_CID_MPEG_VIDEO_HEVC_LEVEL``
    (enum)

enum v4l2_mpeg_video_hevc_level -
    Selects the desired level for HEVC encoder.

==================================	=========
``V4L2_MPEG_VIDEO_HEVC_LEVEL_1``	Level 1.0
``V4L2_MPEG_VIDEO_HEVC_LEVEL_2``	Level 2.0
``V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1``	Level 2.1
``V4L2_MPEG_VIDEO_HEVC_LEVEL_3``	Level 3.0
``V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1``	Level 3.1
``V4L2_MPEG_VIDEO_HEVC_LEVEL_4``	Level 4.0
``V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1``	Level 4.1
``V4L2_MPEG_VIDEO_HEVC_LEVEL_5``	Level 5.0
``V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1``	Level 5.1
``V4L2_MPEG_VIDEO_HEVC_LEVEL_5_2``	Level 5.2
``V4L2_MPEG_VIDEO_HEVC_LEVEL_6``	Level 6.0
``V4L2_MPEG_VIDEO_HEVC_LEVEL_6_1``	Level 6.1
``V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2``	Level 6.2
==================================	=========

``V4L2_CID_MPEG_VIDEO_HEVC_FRAME_RATE_RESOLUTION (integer)``
    Indicates the number of evenly spaced subintervals, called ticks, within
    one second. This is a 16 bit unsigned integer and has a maximum value up to
    0xffff and a minimum value of 1.

.. _v4l2-hevc-tier:

``V4L2_CID_MPEG_VIDEO_HEVC_TIER``
    (enum)

enum v4l2_mpeg_video_hevc_tier -
    TIER_FLAG specifies tiers information of the HEVC encoded picture. Tier
    were made to deal with applications that differ in terms of maximum bit
    rate. Setting the flag to 0 selects HEVC tier as Main tier and setting
    this flag to 1 indicates High tier. High tier is for applications requiring
    high bit rates.

==================================	==========
``V4L2_MPEG_VIDEO_HEVC_TIER_MAIN``	Main tier.
``V4L2_MPEG_VIDEO_HEVC_TIER_HIGH``	High tier.
==================================	==========


``V4L2_CID_MPEG_VIDEO_HEVC_MAX_PARTITION_DEPTH (integer)``
    Selects HEVC maximum coding unit depth.

.. _v4l2-hevc-loop-filter-mode:

``V4L2_CID_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE``
    (enum)

enum v4l2_mpeg_video_hevc_loop_filter_mode -
    Loop filter mode for HEVC encoder. Possible values are:

.. raw:: latex

    \footnotesize

.. tabularcolumns:: |p{12.1cm}|p{5.4cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE_DISABLED``
      - Loop filter is disabled.
    * - ``V4L2_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE_ENABLED``
      - Loop filter is enabled.
    * - ``V4L2_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY``
      - Loop filter is disabled at the slice boundary.

.. raw:: latex

    \normalsize


``V4L2_CID_MPEG_VIDEO_HEVC_LF_BETA_OFFSET_DIV2 (integer)``
    Selects HEVC loop filter beta offset. The valid range is [-6, +6].

``V4L2_CID_MPEG_VIDEO_HEVC_LF_TC_OFFSET_DIV2 (integer)``
    Selects HEVC loop filter tc offset. The valid range is [-6, +6].

.. _v4l2-hevc-refresh-type:

``V4L2_CID_MPEG_VIDEO_HEVC_REFRESH_TYPE``
    (enum)

enum v4l2_mpeg_video_hevc_hier_refresh_type -
    Selects refresh type for HEVC encoder.
    Host has to specify the period into
    V4L2_CID_MPEG_VIDEO_HEVC_REFRESH_PERIOD.

.. raw:: latex

    \footnotesize

.. tabularcolumns:: |p{6.2cm}|p{11.3cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_HEVC_REFRESH_NONE``
      - Use the B frame for hierarchical coding.
    * - ``V4L2_MPEG_VIDEO_HEVC_REFRESH_CRA``
      - Use CRA (Clean Random Access Unit) picture encoding.
    * - ``V4L2_MPEG_VIDEO_HEVC_REFRESH_IDR``
      - Use IDR (Instantaneous Decoding Refresh) picture encoding.

.. raw:: latex

    \normalsize


``V4L2_CID_MPEG_VIDEO_HEVC_REFRESH_PERIOD (integer)``
    Selects the refresh period for HEVC encoder.
    This specifies the number of I pictures between two CRA/IDR pictures.
    This is valid only if REFRESH_TYPE is not 0.

``V4L2_CID_MPEG_VIDEO_HEVC_LOSSLESS_CU (boolean)``
    Indicates HEVC lossless encoding. Setting it to 0 disables lossless
    encoding. Setting it to 1 enables lossless encoding.

``V4L2_CID_MPEG_VIDEO_HEVC_CONST_INTRA_PRED (boolean)``
    Indicates constant intra prediction for HEVC encoder. Specifies the
    constrained intra prediction in which intra largest coding unit (LCU)
    prediction is performed by using residual data and decoded samples of
    neighboring intra LCU only. Setting the value to 1 enables constant intra
    prediction and setting the value to 0 disables constant intra prediction.

``V4L2_CID_MPEG_VIDEO_HEVC_WAVEFRONT (boolean)``
    Indicates wavefront parallel processing for HEVC encoder. Setting it to 0
    disables the feature and setting it to 1 enables the wavefront parallel
    processing.

``V4L2_CID_MPEG_VIDEO_HEVC_GENERAL_PB (boolean)``
    Setting the value to 1 enables combination of P and B frame for HEVC
    encoder.

``V4L2_CID_MPEG_VIDEO_HEVC_TEMPORAL_ID (boolean)``
    Indicates temporal identifier for HEVC encoder which is enabled by
    setting the value to 1.

``V4L2_CID_MPEG_VIDEO_HEVC_STRONG_SMOOTHING (boolean)``
    Indicates bi-linear interpolation is conditionally used in the intra
    prediction filtering process in the CVS when set to 1. Indicates bi-linear
    interpolation is not used in the CVS when set to 0.

``V4L2_CID_MPEG_VIDEO_HEVC_MAX_NUM_MERGE_MV_MINUS1 (integer)``
    Indicates maximum number of merge candidate motion vectors.
    Values are from 0 to 4.

``V4L2_CID_MPEG_VIDEO_HEVC_TMV_PREDICTION (boolean)``
    Indicates temporal motion vector prediction for HEVC encoder. Setting it to
    1 enables the prediction. Setting it to 0 disables the prediction.

``V4L2_CID_MPEG_VIDEO_HEVC_WITHOUT_STARTCODE (boolean)``
    Specifies if HEVC generates a stream with a size of the length field
    instead of start code pattern. The size of the length field is configurable
    through the V4L2_CID_MPEG_VIDEO_HEVC_SIZE_OF_LENGTH_FIELD control. Setting
    the value to 0 disables encoding without startcode pattern. Setting the
    value to 1 will enables encoding without startcode pattern.

.. _v4l2-hevc-size-of-length-field:

``V4L2_CID_MPEG_VIDEO_HEVC_SIZE_OF_LENGTH_FIELD``
(enum)

enum v4l2_mpeg_video_hevc_size_of_length_field -
    Indicates the size of length field.
    This is valid when encoding WITHOUT_STARTCODE_ENABLE is enabled.

.. raw:: latex

    \footnotesize

.. tabularcolumns:: |p{5.5cm}|p{12.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_MPEG_VIDEO_HEVC_SIZE_0``
      - Generate start code pattern (Normal).
    * - ``V4L2_MPEG_VIDEO_HEVC_SIZE_1``
      - Generate size of length field instead of start code pattern and length is 1.
    * - ``V4L2_MPEG_VIDEO_HEVC_SIZE_2``
      - Generate size of length field instead of start code pattern and length is 2.
    * - ``V4L2_MPEG_VIDEO_HEVC_SIZE_4``
      - Generate size of length field instead of start code pattern and length is 4.

.. raw:: latex

    \normalsize

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L0_BR (integer)``
    Indicates bit rate for hierarchical coding layer 0 for HEVC encoder.

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L1_BR (integer)``
    Indicates bit rate for hierarchical coding layer 1 for HEVC encoder.

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L2_BR (integer)``
    Indicates bit rate for hierarchical coding layer 2 for HEVC encoder.

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L3_BR (integer)``
    Indicates bit rate for hierarchical coding layer 3 for HEVC encoder.

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L4_BR (integer)``
    Indicates bit rate for hierarchical coding layer 4 for HEVC encoder.

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L5_BR (integer)``
    Indicates bit rate for hierarchical coding layer 5 for HEVC encoder.

``V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L6_BR (integer)``
    Indicates bit rate for hierarchical coding layer 6 for HEVC encoder.

``V4L2_CID_MPEG_VIDEO_REF_NUMBER_FOR_PFRAMES (integer)``
    Selects number of P reference pictures required for HEVC encoder.
    P-Frame can use 1 or 2 frames for reference.

``V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR (integer)``
    Indicates whether to generate SPS and PPS at every IDR. Setting it to 0
    disables generating SPS and PPS at every IDR. Setting it to one enables
    generating SPS and PPS at every IDR.

.. _v4l2-mpeg-hevc:

``V4L2_CID_MPEG_VIDEO_HEVC_SPS (struct)``
    Specifies the Sequence Parameter Set fields (as extracted from the
    bitstream) for the associated HEVC slice data.
    These bitstream parameters are defined according to :ref:`hevc`.
    They are described in section 7.4.3.2 "Sequence parameter set RBSP
    semantics" of the specification.

.. c:type:: v4l2_ctrl_hevc_sps

.. raw:: latex

    \small

.. tabularcolumns:: |p{1.2cm}|p{9.2cm}|p{6.9cm}|

.. cssclass:: longtable

.. flat-table:: struct v4l2_ctrl_hevc_sps
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u16
      - ``pic_width_in_luma_samples``
      -
    * - __u16
      - ``pic_height_in_luma_samples``
      -
    * - __u8
      - ``bit_depth_luma_minus8``
      -
    * - __u8
      - ``bit_depth_chroma_minus8``
      -
    * - __u8
      - ``log2_max_pic_order_cnt_lsb_minus4``
      -
    * - __u8
      - ``sps_max_dec_pic_buffering_minus1``
      -
    * - __u8
      - ``sps_max_num_reorder_pics``
      -
    * - __u8
      - ``sps_max_latency_increase_plus1``
      -
    * - __u8
      - ``log2_min_luma_coding_block_size_minus3``
      -
    * - __u8
      - ``log2_diff_max_min_luma_coding_block_size``
      -
    * - __u8
      - ``log2_min_luma_transform_block_size_minus2``
      -
    * - __u8
      - ``log2_diff_max_min_luma_transform_block_size``
      -
    * - __u8
      - ``max_transform_hierarchy_depth_inter``
      -
    * - __u8
      - ``max_transform_hierarchy_depth_intra``
      -
    * - __u8
      - ``pcm_sample_bit_depth_luma_minus1``
      -
    * - __u8
      - ``pcm_sample_bit_depth_chroma_minus1``
      -
    * - __u8
      - ``log2_min_pcm_luma_coding_block_size_minus3``
      -
    * - __u8
      - ``log2_diff_max_min_pcm_luma_coding_block_size``
      -
    * - __u8
      - ``num_short_term_ref_pic_sets``
      -
    * - __u8
      - ``num_long_term_ref_pics_sps``
      -
    * - __u8
      - ``chroma_format_idc``
      -
    * - __u8
      - ``sps_max_sub_layers_minus1``
      -
    * - __u64
      - ``flags``
      - See :ref:`Sequence Parameter Set Flags <hevc_sps_flags>`

.. raw:: latex

    \normalsize

.. _hevc_sps_flags:

``Sequence Parameter Set Flags``

.. raw:: latex

    \small

.. cssclass:: longtable

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_HEVC_SPS_FLAG_SEPARATE_COLOUR_PLANE``
      - 0x00000001
      -
    * - ``V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED``
      - 0x00000002
      -
    * - ``V4L2_HEVC_SPS_FLAG_AMP_ENABLED``
      - 0x00000004
      -
    * - ``V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET``
      - 0x00000008
      -
    * - ``V4L2_HEVC_SPS_FLAG_PCM_ENABLED``
      - 0x00000010
      -
    * - ``V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED``
      - 0x00000020
      -
    * - ``V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT``
      - 0x00000040
      -
    * - ``V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED``
      - 0x00000080
      -
    * - ``V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED``
      - 0x00000100
      -

.. raw:: latex

    \normalsize

``V4L2_CID_MPEG_VIDEO_HEVC_PPS (struct)``
    Specifies the Picture Parameter Set fields (as extracted from the
    bitstream) for the associated HEVC slice data.
    These bitstream parameters are defined according to :ref:`hevc`.
    They are described in section 7.4.3.3 "Picture parameter set RBSP
    semantics" of the specification.

.. c:type:: v4l2_ctrl_hevc_pps

.. tabularcolumns:: |p{1.2cm}|p{8.6cm}|p{7.5cm}|

.. cssclass:: longtable

.. flat-table:: struct v4l2_ctrl_hevc_pps
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``num_extra_slice_header_bits``
      -
    * - __u8
      - ``num_ref_idx_l0_default_active_minus1``
      - Specifies the inferred value of num_ref_idx_l0_active_minus1
    * - __u8
      - ``num_ref_idx_l1_default_active_minus1``
      - Specifies the inferred value of num_ref_idx_l1_active_minus1
    * - __s8
      - ``init_qp_minus26``
      -
    * - __u8
      - ``diff_cu_qp_delta_depth``
      -
    * - __s8
      - ``pps_cb_qp_offset``
      -
    * - __s8
      - ``pps_cr_qp_offset``
      -
    * - __u8
      - ``num_tile_columns_minus1``
      -
    * - __u8
      - ``num_tile_rows_minus1``
      -
    * - __u8
      - ``column_width_minus1[20]``
      -
    * - __u8
      - ``row_height_minus1[22]``
      -
    * - __s8
      - ``pps_beta_offset_div2``
      -
    * - __s8
      - ``pps_tc_offset_div2``
      -
    * - __u8
      - ``log2_parallel_merge_level_minus2``
      -
    * - __u8
      - ``padding[4]``
      - Applications and drivers must set this to zero.
    * - __u64
      - ``flags``
      - See :ref:`Picture Parameter Set Flags <hevc_pps_flags>`

.. _hevc_pps_flags:

``Picture Parameter Set Flags``

.. raw:: latex

    \small

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED``
      - 0x00000001
      -
    * - ``V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT``
      - 0x00000002
      -
    * - ``V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED``
      - 0x00000004
      -
    * - ``V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT``
      - 0x00000008
      -
    * - ``V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED``
      - 0x00000010
      -
    * - ``V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED``
      - 0x00000020
      -
    * - ``V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED``
      - 0x00000040
      -
    * - ``V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT``
      - 0x00000080
      -
    * - ``V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED``
      - 0x00000100
      -
    * - ``V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED``
      - 0x00000200
      -
    * - ``V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED``
      - 0x00000400
      -
    * - ``V4L2_HEVC_PPS_FLAG_TILES_ENABLED``
      - 0x00000800
      -
    * - ``V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED``
      - 0x00001000
      -
    * - ``V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED``
      - 0x00002000
      -
    * - ``V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED``
      - 0x00004000
      -
    * - ``V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED``
      - 0x00008000
      -
    * - ``V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER``
      - 0x00010000
      -
    * - ``V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT``
      - 0x00020000
      -
    * - ``V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT``
      - 0x00040000
      -
    * - ``V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT``
      - 0x00080000
      - Specifies the presence of deblocking filter control syntax elements in
        the PPS
    * - ``V4L2_HEVC_PPS_FLAG_UNIFORM_SPACING``
      - 0x00100000
      - Specifies that tile column boundaries and likewise tile row boundaries
        are distributed uniformly across the picture

.. raw:: latex

    \normalsize

``V4L2_CID_MPEG_VIDEO_HEVC_SLICE_PARAMS (struct)``
    Specifies various slice-specific parameters, especially from the NAL unit
    header, general slice segment header and weighted prediction parameter
    parts of the bitstream.
    These bitstream parameters are defined according to :ref:`hevc`.
    They are described in section 7.4.7 "General slice segment header
    semantics" of the specification.

.. c:type:: v4l2_ctrl_hevc_slice_params

.. raw:: latex

    \scriptsize

.. tabularcolumns:: |p{5.4cm}|p{6.8cm}|p{5.1cm}|

.. cssclass:: longtable

.. flat-table:: struct v4l2_ctrl_hevc_slice_params
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``bit_size``
      - Size (in bits) of the current slice data.
    * - __u32
      - ``data_bit_offset``
      - Offset (in bits) to the video data in the current slice data.
    * - __u8
      - ``nal_unit_type``
      -
    * - __u8
      - ``nuh_temporal_id_plus1``
      -
    * - __u8
      - ``slice_type``
      -
	(V4L2_HEVC_SLICE_TYPE_I, V4L2_HEVC_SLICE_TYPE_P or
	V4L2_HEVC_SLICE_TYPE_B).
    * - __u8
      - ``colour_plane_id``
      -
    * - __u16
      - ``slice_pic_order_cnt``
      -
    * - __u8
      - ``num_ref_idx_l0_active_minus1``
      -
    * - __u8
      - ``num_ref_idx_l1_active_minus1``
      -
    * - __u8
      - ``collocated_ref_idx``
      -
    * - __u8
      - ``five_minus_max_num_merge_cand``
      -
    * - __s8
      - ``slice_qp_delta``
      -
    * - __s8
      - ``slice_cb_qp_offset``
      -
    * - __s8
      - ``slice_cr_qp_offset``
      -
    * - __s8
      - ``slice_act_y_qp_offset``
      -
    * - __s8
      - ``slice_act_cb_qp_offset``
      -
    * - __s8
      - ``slice_act_cr_qp_offset``
      -
    * - __s8
      - ``slice_beta_offset_div2``
      -
    * - __s8
      - ``slice_tc_offset_div2``
      -
    * - __u8
      - ``pic_struct``
      -
    * - __u32
      - ``slice_segment_addr``
      -
    * - __u8
      - ``ref_idx_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX]``
      - The list of L0 reference elements as indices in the DPB.
    * - __u8
      - ``ref_idx_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX]``
      - The list of L1 reference elements as indices in the DPB.
    * - __u8
      - ``padding``
      - Applications and drivers must set this to zero.
    * - struct :c:type:`v4l2_hevc_pred_weight_table`
      - ``pred_weight_table``
      - The prediction weight coefficients for inter-picture prediction.
    * - __u64
      - ``flags``
      - See :ref:`Slice Parameters Flags <hevc_slice_params_flags>`

.. raw:: latex

    \normalsize

.. _hevc_slice_params_flags:

``Slice Parameters Flags``

.. raw:: latex

    \scriptsize

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_LUMA``
      - 0x00000001
      -
    * - ``V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_CHROMA``
      - 0x00000002
      -
    * - ``V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_TEMPORAL_MVP_ENABLED``
      - 0x00000004
      -
    * - ``V4L2_HEVC_SLICE_PARAMS_FLAG_MVD_L1_ZERO``
      - 0x00000008
      -
    * - ``V4L2_HEVC_SLICE_PARAMS_FLAG_CABAC_INIT``
      - 0x00000010
      -
    * - ``V4L2_HEVC_SLICE_PARAMS_FLAG_COLLOCATED_FROM_L0``
      - 0x00000020
      -
    * - ``V4L2_HEVC_SLICE_PARAMS_FLAG_USE_INTEGER_MV``
      - 0x00000040
      -
    * - ``V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_DEBLOCKING_FILTER_DISABLED``
      - 0x00000080
      -
    * - ``V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED``
      - 0x00000100
      -
    * - ``V4L2_HEVC_SLICE_PARAMS_FLAG_DEPENDENT_SLICE_SEGMENT``
      - 0x00000200
      -

.. raw:: latex

    \normalsize

``V4L2_CID_MPEG_VIDEO_HEVC_SCALING_MATRIX (struct)``
    Specifies the HEVC scaling matrix parameters used for the scaling process
    for transform coefficients.
    These matrix and parameters are defined according to :ref:`hevc`.
    They are described in section 7.4.5 "Scaling list data semantics" of
    the specification.

.. c:type:: v4l2_ctrl_hevc_scaling_matrix

.. raw:: latex

    \scriptsize

.. tabularcolumns:: |p{5.4cm}|p{6.8cm}|p{5.1cm}|

.. cssclass:: longtable

.. flat-table:: struct v4l2_ctrl_hevc_scaling_matrix
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``scaling_list_4x4[6][16]``
      - Scaling list is used for the scaling process for transform
        coefficients. The values on each scaling list are expected
        in raster scan order.
    * - __u8
      - ``scaling_list_8x8[6][64]``
      - Scaling list is used for the scaling process for transform
        coefficients. The values on each scaling list are expected
        in raster scan order.
    * - __u8
      - ``scaling_list_16x16[6][64]``
      - Scaling list is used for the scaling process for transform
        coefficients. The values on each scaling list are expected
        in raster scan order.
    * - __u8
      - ``scaling_list_32x32[2][64]``
      - Scaling list is used for the scaling process for transform
        coefficients. The values on each scaling list are expected
        in raster scan order.
    * - __u8
      - ``scaling_list_dc_coef_16x16[6]``
      - Scaling list is used for the scaling process for transform
        coefficients. The values on each scaling list are expected
        in raster scan order.
    * - __u8
      - ``scaling_list_dc_coef_32x32[2]``
      - Scaling list is used for the scaling process for transform
        coefficients. The values on each scaling list are expected
        in raster scan order.

.. raw:: latex

    \normalsize

.. c:type:: v4l2_hevc_dpb_entry

.. raw:: latex

    \small

.. tabularcolumns:: |p{1.0cm}|p{4.2cm}|p{12.1cm}|

.. flat-table:: struct v4l2_hevc_dpb_entry
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u64
      - ``timestamp``
      - Timestamp of the V4L2 capture buffer to use as reference, used
        with B-coded and P-coded frames. The timestamp refers to the
	``timestamp`` field in struct :c:type:`v4l2_buffer`. Use the
	:c:func:`v4l2_timeval_to_ns()` function to convert the struct
	:c:type:`timeval` in struct :c:type:`v4l2_buffer` to a __u64.
    * - __u8
      - ``rps``
      - The reference set for the reference frame
        (V4L2_HEVC_DPB_ENTRY_RPS_ST_CURR_BEFORE,
        V4L2_HEVC_DPB_ENTRY_RPS_ST_CURR_AFTER or
        V4L2_HEVC_DPB_ENTRY_RPS_LT_CURR)
    * - __u8
      - ``field_pic``
      - Whether the reference is a field picture or a frame.
    * - __u16
      - ``pic_order_cnt[2]``
      - The picture order count of the reference. Only the first element of the
        array is used for frame pictures, while the first element identifies the
        top field and the second the bottom field in field-coded pictures.
    * - __u8
      - ``padding[2]``
      - Applications and drivers must set this to zero.

.. raw:: latex

    \normalsize

.. c:type:: v4l2_hevc_pred_weight_table

.. raw:: latex

    \footnotesize

.. tabularcolumns:: |p{0.8cm}|p{10.6cm}|p{5.9cm}|

.. flat-table:: struct v4l2_hevc_pred_weight_table
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u8
      - ``luma_log2_weight_denom``
      -
    * - __s8
      - ``delta_chroma_log2_weight_denom``
      -
    * - __s8
      - ``delta_luma_weight_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX]``
      -
    * - __s8
      - ``luma_offset_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX]``
      -
    * - __s8
      - ``delta_chroma_weight_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX][2]``
      -
    * - __s8
      - ``chroma_offset_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX][2]``
      -
    * - __s8
      - ``delta_luma_weight_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX]``
      -
    * - __s8
      - ``luma_offset_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX]``
      -
    * - __s8
      - ``delta_chroma_weight_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX][2]``
      -
    * - __s8
      - ``chroma_offset_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX][2]``
      -
    * - __u8
      - ``padding[6]``
      - Applications and drivers must set this to zero.

.. raw:: latex

    \normalsize

``V4L2_CID_MPEG_VIDEO_HEVC_DECODE_MODE (enum)``
    Specifies the decoding mode to use. Currently exposes slice-based and
    frame-based decoding but new modes might be added later on.
    This control is used as a modifier for V4L2_PIX_FMT_HEVC_SLICE
    pixel format. Applications that support V4L2_PIX_FMT_HEVC_SLICE
    are required to set this control in order to specify the decoding mode
    that is expected for the buffer.
    Drivers may expose a single or multiple decoding modes, depending
    on what they can support.

    .. note::

       This menu control is not yet part of the public kernel API and
       it is expected to change.

.. c:type:: v4l2_mpeg_video_hevc_decode_mode

.. raw:: latex

    \small

.. tabularcolumns:: |p{9.4cm}|p{0.6cm}|p{7.3cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_MPEG_VIDEO_HEVC_DECODE_MODE_SLICE_BASED``
      - 0
      - Decoding is done at the slice granularity.
        The OUTPUT buffer must contain a single slice.
    * - ``V4L2_MPEG_VIDEO_HEVC_DECODE_MODE_FRAME_BASED``
      - 1
      - Decoding is done at the frame granularity.
        The OUTPUT buffer must contain all slices needed to decode the
        frame. The OUTPUT buffer must also contain both fields.

.. raw:: latex

    \normalsize

``V4L2_CID_MPEG_VIDEO_HEVC_START_CODE (enum)``
    Specifies the HEVC slice start code expected for each slice.
    This control is used as a modifier for V4L2_PIX_FMT_HEVC_SLICE
    pixel format. Applications that support V4L2_PIX_FMT_HEVC_SLICE
    are required to set this control in order to specify the start code
    that is expected for the buffer.
    Drivers may expose a single or multiple start codes, depending
    on what they can support.

    .. note::

       This menu control is not yet part of the public kernel API and
       it is expected to change.

.. c:type:: v4l2_mpeg_video_hevc_start_code

.. tabularcolumns:: |p{9.2cm}|p{0.6cm}|p{7.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_MPEG_VIDEO_HEVC_START_CODE_NONE``
      - 0
      - Selecting this value specifies that HEVC slices are passed
        to the driver without any start code. The bitstream data should be
        according to :ref:`hevc` 7.3.1.1 General NAL unit syntax, hence
        contains emulation prevention bytes when required.
    * - ``V4L2_MPEG_VIDEO_HEVC_START_CODE_ANNEX_B``
      - 1
      - Selecting this value specifies that HEVC slices are expected
        to be prefixed by Annex B start codes. According to :ref:`hevc`
        valid start codes can be 3-bytes 0x000001 or 4-bytes 0x00000001.

``V4L2_CID_MPEG_VIDEO_BASELAYER_PRIORITY_ID (integer)``
    Specifies a priority identifier for the NAL unit, which will be applied to
    the base layer. By default this value is set to 0 for the base layer,
    and the next layer will have the priority ID assigned as 1, 2, 3 and so on.
    The video encoder can't decide the priority id to be applied to a layer,
    so this has to come from client.
    This is applicable to H264 and valid Range is from 0 to 63.
    Source Rec. ITU-T H.264 (06/2019); G.7.4.1.1, G.8.8.1.

``V4L2_CID_MPEG_VIDEO_LTR_COUNT (integer)``
    Specifies the maximum number of Long Term Reference (LTR) frames at any
    given time that the encoder can keep.
    This is applicable to the H264 and HEVC encoders.

``V4L2_CID_MPEG_VIDEO_FRAME_LTR_INDEX (integer)``
    After setting this control the frame that will be queued next
    will be marked as a Long Term Reference (LTR) frame
    and given this LTR index which ranges from 0 to LTR_COUNT-1.
    This is applicable to the H264 and HEVC encoders.
    Source Rec. ITU-T H.264 (06/2019); Table 7.9

``V4L2_CID_MPEG_VIDEO_USE_LTR_FRAMES (bitmask)``
    Specifies the Long Term Reference (LTR) frame(s) to be used for
    encoding the next frame queued after setting this control.
    This provides a bitmask which consists of bits [0, LTR_COUNT-1].
    This is applicable to the H264 and HEVC encoders.

``V4L2_CID_MPEG_VIDEO_HEVC_DECODE_PARAMS (struct)``
    Specifies various decode parameters, especially the references picture order
    count (POC) for all the lists (short, long, before, current, after) and the
    number of entries for each of them.
    These parameters are defined according to :ref:`hevc`.
    They are described in section 8.3 "Slice decoding process" of the
    specification.

.. c:type:: v4l2_ctrl_hevc_decode_params

.. cssclass:: longtable

.. flat-table:: struct v4l2_ctrl_hevc_decode_params
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __s32
      - ``pic_order_cnt_val``
      - PicOrderCntVal as described in section 8.3.1 "Decoding process
        for picture order count" of the specification.
    * - __u8
      - ``num_active_dpb_entries``
      - The number of entries in ``dpb``.
    * - struct :c:type:`v4l2_hevc_dpb_entry`
      - ``dpb[V4L2_HEVC_DPB_ENTRIES_NUM_MAX]``
      - The decoded picture buffer, for meta-data about reference frames.
    * - __u8
      - ``num_poc_st_curr_before``
      - The number of reference pictures in the short-term set that come before
        the current frame.
    * - __u8
      - ``num_poc_st_curr_after``
      - The number of reference pictures in the short-term set that come after
        the current frame.
    * - __u8
      - ``num_poc_lt_curr``
      - The number of reference pictures in the long-term set.
    * - __u8
      - ``poc_st_curr_before[V4L2_HEVC_DPB_ENTRIES_NUM_MAX]``
      - PocStCurrBefore as described in section 8.3.2 "Decoding process for reference
        picture set.
    * - __u8
      - ``poc_st_curr_after[V4L2_HEVC_DPB_ENTRIES_NUM_MAX]``
      - PocStCurrAfter as described in section 8.3.2 "Decoding process for reference
        picture set.
    * - __u8
      - ``poc_lt_curr[V4L2_HEVC_DPB_ENTRIES_NUM_MAX]``
      - PocLtCurr as described in section 8.3.2 "Decoding process for reference
        picture set.
    * - __u64
      - ``flags``
      - See :ref:`Decode Parameters Flags <hevc_decode_params_flags>`

.. _hevc_decode_params_flags:

``Decode Parameters Flags``

.. cssclass:: longtable

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - ``V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC``
      - 0x00000001
      -
    * - ``V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC``
      - 0x00000002
      -
    * - ``V4L2_HEVC_DECODE_PARAM_FLAG_NO_OUTPUT_OF_PRIOR``
      - 0x00000004
      -
