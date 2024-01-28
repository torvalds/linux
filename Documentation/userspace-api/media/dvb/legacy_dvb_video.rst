.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later OR GPL-2.0

.. c:namespace:: dtv.legacy.video

.. _dvb_video:

================
DVB Video Device
================

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

The DVB video device controls the MPEG2 video decoder of the DVB
hardware. It can be accessed through ``/dev/dvb/adapter0/video0``. Data
types and ioctl definitions can be accessed by including
``linux/dvb/video.h`` in your application.

Note that the DVB video device only controls decoding of the MPEG video
stream, not its presentation on the TV or computer screen. On PCs this
is typically handled by an associated video4linux device, e.g.
``/dev/video``, which allows scaling and defining output windows.

Most DVB cards don’t have their own MPEG decoder, which results in the
omission of the audio and video device as well as the video4linux
device.

These ioctls were also used by V4L2 to control MPEG decoders implemented
in V4L2. The use of these ioctls for that purpose has been made obsolete
and proper V4L2 ioctls or controls have been created to replace that
functionality. Use :ref:`V4L2 ioctls<video>` for new drivers!


Video Data Types
================



video_format_t
--------------

Synopsis
~~~~~~~~

.. code-block:: c

    typedef enum {
	VIDEO_FORMAT_4_3,
	VIDEO_FORMAT_16_9,
	VIDEO_FORMAT_221_1
    } video_format_t;

Constants
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``VIDEO_FORMAT_4_3``

       -  Select 4:3 format.

    -  ..

       -  ``VIDEO_FORMAT_16_9``

       -  Select 16:9 format.

    -  ..

       -  ``VIDEO_FORMAT_221_1``

       -  Select 2.21:1 format.

Description
~~~~~~~~~~~

The ``video_format_t`` data type
is used in the `VIDEO_SET_FORMAT`_ function to tell the driver which
aspect ratio the output hardware (e.g. TV) has. It is also used in the
data structures `video_status`_ returned by `VIDEO_GET_STATUS`_
and `video_event`_ returned by `VIDEO_GET_EVENT`_ which report
about the display format of the current video stream.


-----


video_displayformat_t
---------------------

Synopsis
~~~~~~~~

.. code-block:: c

    typedef enum {
	VIDEO_PAN_SCAN,
	VIDEO_LETTER_BOX,
	VIDEO_CENTER_CUT_OUT
    } video_displayformat_t;

Constants
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``VIDEO_PAN_SCAN``

       -  Use pan and scan format.

    -  ..

       -  ``VIDEO_LETTER_BOX``

       -  Use letterbox format.

    -  ..

       -  ``VIDEO_CENTER_CUT_OUT``

       -  Use center cut out format.

Description
~~~~~~~~~~~

In case the display format of the video stream and of the display
hardware differ the application has to specify how to handle the
cropping of the picture. This can be done using the
`VIDEO_SET_DISPLAY_FORMAT`_ call which accepts this enum as argument.


-----


video_size_t
------------

Synopsis
~~~~~~~~

.. code-block:: c

    typedef struct {
	int w;
	int h;
	video_format_t aspect_ratio;
    } video_size_t;

Variables
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int w``

       -  Video width in pixels.

    -  ..

       -  ``int h``

       -  Video height in pixels.

    -  ..

       -  `video_format_t`_ ``aspect_ratio``

       -  Aspect ratio.

Description
~~~~~~~~~~~

Used in the struct `video_event`_. It stores the resolution and
aspect ratio of the video.


-----


video_stream_source_t
---------------------

Synopsis
~~~~~~~~

.. code-block:: c

    typedef enum {
	VIDEO_SOURCE_DEMUX,
	VIDEO_SOURCE_MEMORY
    } video_stream_source_t;

Constants
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``VIDEO_SOURCE_DEMUX``

       -  :cspan:`1` Select the demux as the main source.

    -  ..

       -  ``VIDEO_SOURCE_MEMORY``

       -  If this source is selected, the stream
          comes from the user through the write
          system call.

Description
~~~~~~~~~~~

The video stream source is set through the `VIDEO_SELECT_SOURCE`_ call
and can take the following values, depending on whether we are replaying
from an internal (demuxer) or external (user write) source.
VIDEO_SOURCE_DEMUX selects the demultiplexer (fed either by the
frontend or the DVR device) as the source of the video stream. If
VIDEO_SOURCE_MEMORY is selected the stream comes from the application
through the `write()`_ system call.


-----


video_play_state_t
------------------

Synopsis
~~~~~~~~

.. code-block:: c

    typedef enum {
	VIDEO_STOPPED,
	VIDEO_PLAYING,
	VIDEO_FREEZED
    } video_play_state_t;

Constants
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``VIDEO_STOPPED``

       -  Video is stopped.

    -  ..

       -  ``VIDEO_PLAYING``

       -  Video is currently playing.

    -  ..

       -  ``VIDEO_FREEZED``

       -  Video is frozen.

Description
~~~~~~~~~~~

This values can be returned by the `VIDEO_GET_STATUS`_ call
representing the state of video playback.


-----


struct video_command
--------------------

Synopsis
~~~~~~~~

.. code-block:: c

    struct video_command {
	__u32 cmd;
	__u32 flags;
	union {
	    struct {
		__u64 pts;
	    } stop;

	    struct {
		__s32 speed;
		__u32 format;
	    } play;

	    struct {
		__u32 data[16];
	    } raw;
	};
    };


Variables
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``__u32 cmd``

       -  `Decoder command`_

    -  ..

       -  ``__u32 flags``

       -  Flags for the `Decoder command`_.

    -  ..

       -  ``struct stop``

       -  ``__u64 pts``

       -  MPEG PTS

    -  ..

       -  :rspan:`5` ``stuct play``

       -  :rspan:`4` ``__s32 speed``

       -   0 or 1000 specifies normal speed,

    -  ..

       -   1:  specifies forward single stepping,

    -  ..

       -   -1: specifies backward single stepping,

    -  ..

       -   >1: playback at speed / 1000 of the normal speed

    -  ..

       -   <-1: reverse playback at ( -speed / 1000 ) of the normal speed.

    -  ..

       -  ``__u32 format``

       -  `Play input formats`_

    -  ..

       -  ``__u32 data[16]``

       -  Reserved

Description
~~~~~~~~~~~

The structure must be zeroed before use by the application. This ensures
it can be extended safely in the future.


-----


Predefined decoder commands and flags
-------------------------------------

Synopsis
~~~~~~~~

.. code-block:: c

    #define VIDEO_CMD_PLAY                      (0)
    #define VIDEO_CMD_STOP                      (1)
    #define VIDEO_CMD_FREEZE                    (2)
    #define VIDEO_CMD_CONTINUE                  (3)

    #define VIDEO_CMD_FREEZE_TO_BLACK      (1 << 0)

    #define VIDEO_CMD_STOP_TO_BLACK        (1 << 0)
    #define VIDEO_CMD_STOP_IMMEDIATELY     (1 << 1)

    #define VIDEO_PLAY_FMT_NONE                 (0)
    #define VIDEO_PLAY_FMT_GOP                  (1)

    #define VIDEO_VSYNC_FIELD_UNKNOWN           (0)
    #define VIDEO_VSYNC_FIELD_ODD               (1)
    #define VIDEO_VSYNC_FIELD_EVEN              (2)
    #define VIDEO_VSYNC_FIELD_PROGRESSIVE       (3)

Constants
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  :rspan:`3` _`Decoder command`

       -  ``VIDEO_CMD_PLAY``

       -  Start playback.

    -  ..

       -  ``VIDEO_CMD_STOP``

       -  Stop playback.

    -  ..

       -  ``VIDEO_CMD_FREEZE``

       -  Freeze playback.

    -  ..

       -  ``VIDEO_CMD_CONTINUE``

       -  Continue playback after freeze.

    -  ..

       -  Flags for ``VIDEO_CMD_FREEZE``

       -  ``VIDEO_CMD_FREEZE_TO_BLACK``

       -  Show black picture on freeze.

    -  ..

       -  :rspan:`1` Flags for ``VIDEO_CMD_STOP``

       -  ``VIDEO_CMD_STOP_TO_BLACK``

       -  Show black picture on stop.

    -  ..

       -  ``VIDEO_CMD_STOP_IMMEDIATELY``

       -  Stop immediately, without emptying buffers.

    -  ..

       -  :rspan:`1` _`Play input formats`

       -  ``VIDEO_PLAY_FMT_NONE``

       -  The decoder has no special format requirements

    -  ..

       -  ``VIDEO_PLAY_FMT_GOP``

       -  The decoder requires full GOPs

    -  ..

       -  :rspan:`3` Field order

       -  ``VIDEO_VSYNC_FIELD_UNKNOWN``

       -  FIELD_UNKNOWN can be used if the hardware does not know
          whether the Vsync is for an odd, even or progressive
          (i.e. non-interlaced) field.

    -  ..

       -  ``VIDEO_VSYNC_FIELD_ODD``

       -  Vsync is for an odd field.

    -  ..

       -  ``VIDEO_VSYNC_FIELD_EVEN``

       -  Vsync is for an even field.

    -  ..

       -  ``VIDEO_VSYNC_FIELD_PROGRESSIVE``

       -  progressive (i.e. non-interlaced)


-----


video_event
-----------

Synopsis
~~~~~~~~

.. code-block:: c

    struct video_event {
	__s32 type;
    #define VIDEO_EVENT_SIZE_CHANGED        1
    #define VIDEO_EVENT_FRAME_RATE_CHANGED  2
    #define VIDEO_EVENT_DECODER_STOPPED     3
    #define VIDEO_EVENT_VSYNC               4
	long timestamp;
	union {
	    video_size_t size;
	    unsigned int frame_rate;
	    unsigned char vsync_field;
	} u;
    };

Variables
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  :rspan:`4` ``__s32 type``

       -  :cspan:`1` Event type.

    -  ..

       -  ``VIDEO_EVENT_SIZE_CHANGED``

       -  Size changed.

    -  ..

       -  ``VIDEO_EVENT_FRAME_RATE_CHANGED``

       -  Framerate changed.

    -  ..

       -  ``VIDEO_EVENT_DECODER_STOPPED``

       -  Decoder stopped.

    -  ..

       -  ``VIDEO_EVENT_VSYNC``

       -  Vsync occurred.

    -  ..

       -  ``long timestamp``

       -  :cspan:`1` MPEG PTS at occurrence.

    -  ..

       -  :rspan:`2` ``union u``

       -  `video_size_t`_ size

       -  Resolution and aspect ratio of the video.

    -  ..

       -  ``unsigned int frame_rate``

       -  in frames per 1000sec

    -  ..

       -  ``unsigned char vsync_field``

       -  | unknown / odd / even / progressive
          | See: `Predefined decoder commands and flags`_

Description
~~~~~~~~~~~

This is the structure of a video event as it is returned by the
`VIDEO_GET_EVENT`_ call. See there for more details.


-----


video_status
------------

Synopsis
~~~~~~~~

The `VIDEO_GET_STATUS`_ call returns the following structure informing
about various states of the playback operation.

.. code-block:: c

    struct video_status {
	int                    video_blank;
	video_play_state_t     play_state;
	video_stream_source_t  stream_source;
	video_format_t         video_format;
	video_displayformat_t  display_format;
    };

Variables
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  :rspan:`2` ``int video_blank``

       -  :cspan:`1` Show blank video on freeze?

    -  ..

       -  TRUE  ( != 0 )

       -  Blank screen when freeze.

    -  ..

       -  FALSE ( == 0 )

       -  Show last decoded frame.

    -  ..

       -  `video_play_state_t`_ ``play_state``

       -  Current state of playback.

    -  ..

       -  `video_stream_source_t`_ ``stream_source``

       -  Current source (demux/memory).

    -  ..

       -  `video_format_t`_ ``video_format``

       -  Current aspect ratio of stream.

    -  ..

       -  `video_displayformat_t`_ ``display_format``

       -  Applied cropping mode.

Description
~~~~~~~~~~~

If ``video_blank`` is set ``TRUE`` video will be blanked out if the
channel is changed or if playback is stopped. Otherwise, the last picture
will be displayed. ``play_state`` indicates if the video is currently
frozen, stopped, or being played back. The ``stream_source`` corresponds
to the selected source for the video stream. It can come either from the
demultiplexer or from memory. The ``video_format`` indicates the aspect
ratio (one of 4:3 or 16:9) of the currently played video stream.
Finally, ``display_format`` corresponds to the applied cropping mode in
case the source video format is not the same as the format of the output
device.


-----


video_still_picture
-------------------

Synopsis
~~~~~~~~

.. code-block:: c

    struct video_still_picture {
    char *iFrame;
    int32_t size;
    };

Variables
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``char *iFrame``

       -  Pointer to a single iframe in memory.

    -  ..

       -  ``int32_t size``

       -  Size of the iframe.


Description
~~~~~~~~~~~

An I-frame displayed via the `VIDEO_STILLPICTURE`_ call is passed on
within this structure.


-----


video capabilities
------------------

Synopsis
~~~~~~~~

.. code-block:: c

    #define VIDEO_CAP_MPEG1   1
    #define VIDEO_CAP_MPEG2   2
    #define VIDEO_CAP_SYS     4
    #define VIDEO_CAP_PROG    8

Constants
~~~~~~~~~
Bit definitions for capabilities:

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``VIDEO_CAP_MPEG1``

       -  :cspan:`1` The hardware can decode MPEG1.

    -  ..

       -  ``VIDEO_CAP_MPEG2``

       -  The hardware can decode MPEG2.

    -  ..

       -  ``VIDEO_CAP_SYS``

       -  The video device accepts system stream.

          You still have to open the video and the audio device
          but only send the stream to the video device.

    -  ..

       -  ``VIDEO_CAP_PROG``

       -  The video device accepts program stream.

          You still have to open the video and the audio device
          but only send the stream to the video device.

Description
~~~~~~~~~~~

A call to `VIDEO_GET_CAPABILITIES`_ returns an unsigned integer with the
following bits set according to the hardware's capabilities.
