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


-----


Video Function Calls
====================


VIDEO_STOP
----------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_STOP

.. code-block:: c

	int ioctl(fd, VIDEO_STOP, int mode)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  :cspan:`1` Equals ``VIDEO_STOP`` for this command.

    -  ..

       -  :rspan:`2` ``int mode``

       -  :cspan:`1` Indicates how the screen shall be handled.

    -  ..

       -  TRUE  ( != 0 )

       -  Blank screen when stop.

    -  ..

       -  FALSE ( == 0 )

       -  Show last decoded frame.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl is for Digital TV devices only. To control a V4L2 decoder use
the V4L2 :ref:`VIDIOC_DECODER_CMD` instead.

This ioctl call asks the Video Device to stop playing the current
stream. Depending on the input parameter, the screen can be blanked out
or displaying the last decoded frame.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_PLAY
----------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_PLAY

.. code-block:: c

	int ioctl(fd, VIDEO_PLAY)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_PLAY`` for this command.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl is for Digital TV devices only. To control a V4L2 decoder use
the V4L2 :ref:`VIDIOC_DECODER_CMD` instead.

This ioctl call asks the Video Device to start playing a video stream
from the selected source.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_FREEZE
------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_FREEZE

.. code-block:: c

	int ioctl(fd, VIDEO_FREEZE)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_FREEZE`` for this command.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl is for Digital TV devices only. To control a V4L2 decoder use
the V4L2 :ref:`VIDIOC_DECODER_CMD` instead.

This ioctl call suspends the live video stream being played, if
VIDEO_SOURCE_DEMUX is selected. Decoding and playing are frozen.
It is then possible to restart the decoding and playing process of the
video stream using the `VIDEO_CONTINUE`_ command.
If VIDEO_SOURCE_MEMORY is selected in the ioctl call
`VIDEO_SELECT_SOURCE`_, the Digital TV subsystem will not decode any more
data until the ioctl call `VIDEO_CONTINUE`_ or `VIDEO_PLAY`_ is performed.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_CONTINUE
--------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_CONTINUE

.. code-block:: c

	int ioctl(fd, VIDEO_CONTINUE)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_CONTINUE`` for this command.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl is for Digital TV devices only. To control a V4L2 decoder use
the V4L2 :ref:`VIDIOC_DECODER_CMD` instead.

This ioctl call restarts decoding and playing processes of the video
stream which was played before a call to `VIDEO_FREEZE`_ was made.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_SELECT_SOURCE
-------------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_SELECT_SOURCE

.. code-block:: c

	int ioctl(fd, VIDEO_SELECT_SOURCE, video_stream_source_t source)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_SELECT_SOURCE`` for this command.

    -  ..

       -  `video_stream_source_t`_ ``source``

       -  Indicates which source shall be used for the Video stream.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl is for Digital TV devices only. This ioctl was also supported
by the V4L2 ivtv driver, but that has been replaced by the ivtv-specific
``IVTV_IOC_PASSTHROUGH_MODE`` ioctl.

This ioctl call informs the video device which source shall be used for
the input data. The possible sources are demux or memory. If memory is
selected, the data is fed to the video device through the write command
using the struct `video_stream_source_t`_. If demux is selected, the data
is directly transferred from the onboard demux-device to the decoder.

The data fed to the decoder is also controlled by the PID-filter.
Output selection: :c:type:`dmx_output` ``DMX_OUT_DECODER``.


Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_SET_BLANK
---------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_SET_BLANK

.. code-block:: c

	int ioctl(fd, VIDEO_SET_BLANK, int mode)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  :cspan:`1` Equals ``VIDEO_SET_BLANK`` for this command.

    -  ..

       -  :rspan:`2` ``int mode``

       -  :cspan:`1` Indicates if the screen shall be blanked.

    -  ..

       -  TRUE  ( != 0 )

       -  Blank screen when stop.

    -  ..

       -  FALSE ( == 0 )

       -  Show last decoded frame.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the Video Device to blank out the picture.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_GET_STATUS
----------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_GET_STATUS

.. code-block:: c

	int ioctl(fd, int request = VIDEO_GET_STATUS,
	struct video_status *status)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_GET_STATUS`` for this command.

    -  ..

       -  ``struct`` `video_status`_ ``*status``

       -  Returns the current status of the Video Device.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the Video Device to return the current status of
the device.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_GET_EVENT
---------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_GET_EVENT

.. code-block:: c

	int ioctl(fd, int request = VIDEO_GET_EVENT,
	struct video_event *ev)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_GET_EVENT`` for this command.

    -  ..

       -  ``struct`` `video_event`_ ``*ev``

       -  Points to the location where the event, if any, is to be stored.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl is for DVB devices only. To get events from a V4L2 decoder
use the V4L2 :ref:`VIDIOC_DQEVENT` ioctl instead.

This ioctl call returns an event of type `video_event`_ if available. A
certain number of the latest events will be cued and returned in order of
occurrence. Older events may be discarded if not fetched in time. If
an event is not available, the behavior depends on whether the device is
in blocking or non-blocking mode. In the latter case, the call fails
immediately with errno set to ``EWOULDBLOCK``. In the former case, the
call blocks until an event becomes available. The standard Linux poll()
and/or select() system calls can be used with the device file descriptor
to watch for new events. For select(), the file descriptor should be
included in the exceptfds argument, and for poll(), POLLPRI should be
specified as the wake-up condition. Read-only permissions are sufficient
for this ioctl call.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``EWOULDBLOCK``

       -  :cspan:`1` There is no event pending, and the device is in
          non-blocking mode.

    -  ..

       -  ``EOVERFLOW``

       -  Overflow in event queue - one or more events were lost.


-----


VIDEO_SET_DISPLAY_FORMAT
------------------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_SET_DISPLAY_FORMAT

.. code-block:: c

	int ioctl(fd, int request = VIDEO_SET_DISPLAY_FORMAT,
	video_display_format_t format)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_SET_DISPLAY_FORMAT`` for this command.

    -  ..

       -  `video_displayformat_t`_ ``format``

       -  Selects the video format to be used.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the Video Device to select the video format to be
applied by the MPEG chip on the video.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_STILLPICTURE
------------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_STILLPICTURE

.. code-block:: c

	int ioctl(fd, int request = VIDEO_STILLPICTURE,
	struct video_still_picture *sp)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_STILLPICTURE`` for this command.

    -  ..

       -  ``struct`` `video_still_picture`_ ``*sp``

       -  Pointer to the location where the struct with the I-frame
          and size is stored.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the Video Device to display a still picture
(I-frame). The input data shall be the section of an elementary video
stream containing an I-frame. Typically this section is extracted from a
TS or PES recording. Resolution and codec (see `video capabilities`_) must
be supported by the device. If the pointer is NULL, then the current
displayed still picture is blanked.

e.g. The AV7110 supports MPEG1 and MPEG2 with the common PAL-SD
resolutions.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_FAST_FORWARD
------------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_FAST_FORWARD

.. code-block:: c

	int ioctl(fd, int request = VIDEO_FAST_FORWARD, int nFrames)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_FAST_FORWARD`` for this command.

    -  ..

       -  ``int nFrames``

       -  The number of frames to skip.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the Video Device to skip decoding of N number of
I-frames. This call can only be used if ``VIDEO_SOURCE_MEMORY`` is
selected.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``EPERM``

       -  Mode ``VIDEO_SOURCE_MEMORY`` not selected.


-----


VIDEO_SLOWMOTION
----------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_SLOWMOTION

.. code-block:: c

	int ioctl(fd, int request = VIDEO_SLOWMOTION, int nFrames)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_SLOWMOTION`` for this command.

    -  ..

       -  ``int nFrames``

       -  The number of times to repeat each frame.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the video device to repeat decoding frames N number
of times. This call can only be used if ``VIDEO_SOURCE_MEMORY`` is
selected.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``EPERM``

       -  Mode ``VIDEO_SOURCE_MEMORY`` not selected.


-----


VIDEO_GET_CAPABILITIES
----------------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_GET_CAPABILITIES

.. code-block:: c

	int ioctl(fd, int request = VIDEO_GET_CAPABILITIES, unsigned int *cap)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_GET_CAPABILITIES`` for this command.

    -  ..

       -  ``unsigned int *cap``

       -  Pointer to a location where to store the capability information.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the video device about its decoding capabilities.
On success it returns an integer which has bits set according to the
defines in `video capabilities`_.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_CLEAR_BUFFER
------------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_CLEAR_BUFFER

.. code-block:: c

	int ioctl(fd, int request = VIDEO_CLEAR_BUFFER)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_CLEAR_BUFFER`` for this command.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call clears all video buffers in the driver and in the
decoder hardware.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_SET_STREAMTYPE
--------------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_SET_STREAMTYPE

.. code-block:: c

	int ioctl(fd, int request = VIDEO_SET_STREAMTYPE, int type)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_SET_STREAMTYPE`` for this command.

    -  ..

       -  ``int type``

       -  Stream type.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl tells the driver which kind of stream to expect being written
to it.
Intelligent decoder might also not support or ignore (like the AV7110)
this call and determine the stream type themselves.

Currently used stream types:

.. flat-table::
    :header-rows:  1
    :stub-columns: 0

    -  ..

       -  Codec

       -  Stream type

    -  ..

       -  MPEG2

       -  0

    -  ..

       -  MPEG4 h.264

       -  1

    -  ..

       -  VC1

       -  3

    -  ..

       -  MPEG4 Part2

       -  4

    -  ..

       -  VC1 SM

       -  5

    -  ..

       -  MPEG1

       -  6

    -  ..

       -  HEVC h.265

       -  | 7
          | DREAMBOX: 22

    -  ..

       -  AVS

       -  16

    -  ..

       -  AVS2

       -  40

Not every decoder supports all stream types.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_SET_FORMAT
----------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_SET_FORMAT

.. code-block:: c

	int ioctl(fd, int request = VIDEO_SET_FORMAT, video_format_t format)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_SET_FORMAT`` for this command.

    -  ..

       -  `video_format_t`_ ``format``

       -  Video format of TV as defined in section `video_format_t`_.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl sets the screen format (aspect ratio) of the connected output
device (TV) so that the output of the decoder can be adjusted
accordingly.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_GET_SIZE
--------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_GET_SIZE

.. code-block:: c

	int ioctl(int fd, int request = VIDEO_GET_SIZE, video_size_t *size)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call,
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_GET_SIZE`` for this command.

    -  ..

       -  `video_size_t`_ ``*size``

       -  Returns the size and aspect ratio.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl returns the size and aspect ratio.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_GET_PTS
-------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_GET_PTS

.. code-block:: c

	int ioctl(int fd, int request = VIDEO_GET_PTS, __u64 *pts)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_GET_PTS`` for this command.

    -  ..

       -  ``__u64 *pts``

       -  Returns the 33-bit timestamp as defined in ITU T-REC-H.222.0 /
          ISO/IEC 13818-1.

          The PTS should belong to the currently played frame if possible,
          but may also be a value close to it like the PTS of the last
          decoded frame or the last PTS extracted by the PES parser.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

For V4L2 decoders this ioctl has been replaced by the
``V4L2_CID_MPEG_VIDEO_DEC_PTS`` control.

This ioctl call asks the Video Device to return the current PTS
timestamp.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_GET_FRAME_COUNT
---------------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_GET_FRAME_COUNT

.. code-block:: c

	int ioctl(int fd, VIDEO_GET_FRAME_COUNT, __u64 *pts)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_GET_FRAME_COUNT`` for this command.

    -  ..

       -  ``__u64 *pts``

       -  Returns the number of frames displayed since the decoder was
          started.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

For V4L2 decoders this ioctl has been replaced by the
``V4L2_CID_MPEG_VIDEO_DEC_FRAME`` control.

This ioctl call asks the Video Device to return the number of displayed
frames since the decoder was started.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_COMMAND
-------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_COMMAND

.. code-block:: c

	int ioctl(int fd, int request = VIDEO_COMMAND,
	struct video_command *cmd)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_COMMAND`` for this command.

    -  ..

       -  `struct video_command`_ ``*cmd``

       -  Commands the decoder.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

For V4L2 decoders this ioctl has been replaced by the
:ref:`VIDIOC_DECODER_CMD` ioctl.

This ioctl commands the decoder. The `struct video_command`_ is a
subset of the ``v4l2_decoder_cmd`` struct, so refer to the
:ref:`VIDIOC_DECODER_CMD` documentation for
more information.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


VIDEO_TRY_COMMAND
-----------------

Synopsis
~~~~~~~~

.. c:macro:: VIDEO_TRY_COMMAND

.. code-block:: c

	int ioctl(int fd, int request = VIDEO_TRY_COMMAND,
	struct video_command *cmd)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``VIDEO_TRY_COMMAND`` for this command.

    -  ..

       -  `struct video_command`_ ``*cmd``

       -  Try a decoder command.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

For V4L2 decoders this ioctl has been replaced by the
:ref:`VIDIOC_TRY_DECODER_CMD <VIDIOC_DECODER_CMD>` ioctl.

This ioctl tries a decoder command. The `struct video_command`_ is a
subset of the ``v4l2_decoder_cmd`` struct, so refer to the
:ref:`VIDIOC_TRY_DECODER_CMD <VIDIOC_DECODER_CMD>` documentation
for more information.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


open()
------

Synopsis
~~~~~~~~

.. code-block:: c

    #include <fcntl.h>

.. c:function:: 	int open(const char *deviceName, int flags)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``const char *deviceName``

       -  Name of specific video device.

    -  ..

       -  :rspan:`3` ``int flags``

       -  :cspan:`1` A bit-wise OR of the following flags:

    -  ..

       -  ``O_RDONLY``

       -  read-only access

    -  ..

       -  ``O_RDWR``

       -  read/write access

    -  ..

       -  ``O_NONBLOCK``
       -  | Open in non-blocking mode
          | (blocking mode is the default)

Description
~~~~~~~~~~~

This system call opens a named video device (e.g.
/dev/dvb/adapter?/video?) for subsequent use.

When an open() call has succeeded, the device will be ready for use. The
significance of blocking or non-blocking mode is described in the
documentation for functions where there is a difference. It does not
affect the semantics of the open() call itself. A device opened in
blocking mode can later be put into non-blocking mode (and vice versa)
using the F_SETFL command of the fcntl system call. This is a standard
system call, documented in the Linux manual page for fcntl. Only one
user can open the Video Device in O_RDWR mode. All other attempts to
open the device in this mode will fail, and an error-code will be
returned. If the Video Device is opened in O_RDONLY mode, the only
ioctl call that can be used is `VIDEO_GET_STATUS`_. All other call will
return an error code.

Return Value
~~~~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``ENODEV``

       -  :cspan:`1` Device driver not loaded/available.

    -  ..

       -  ``EINTERNAL``

       -  Internal error.

    -  ..

       -  ``EBUSY``

       -  Device or resource busy.

    -  ..

       -  ``EINVAL``

       -  Invalid argument.


-----


close()
-------

Synopsis
~~~~~~~~

.. c:function:: 	int close(int fd)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

Description
~~~~~~~~~~~

This system call closes a previously opened video device.

Return Value
~~~~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``EBADF``

       -  fd is not a valid open file descriptor.


-----


write()
-------

Synopsis
~~~~~~~~

.. c:function:: size_t write(int fd, const void *buf, size_t count)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``void *buf``

       -  Pointer to the buffer containing the PES data.

    -  ..

       -  ``size_t count``

       -  Size of buf.

Description
~~~~~~~~~~~

This system call can only be used if VIDEO_SOURCE_MEMORY is selected
in the ioctl call `VIDEO_SELECT_SOURCE`_. The data provided shall be in
PES format, unless the capability allows other formats. TS is the
most common format for storing DVB-data, it is usually supported too.
If O_NONBLOCK is not specified the function will block until buffer space
is available. The amount of data to be transferred is implied by count.

.. note:: See: :ref:`DVB Data Formats <legacy_dvb_decoder_formats>`

Return Value
~~~~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``EPERM``

       -  :cspan:`1` Mode ``VIDEO_SOURCE_MEMORY`` not selected.

    -  ..

       -  ``ENOMEM``

       -  Attempted to write more data than the internal buffer can hold.

    -  ..

       -  ``EBADF``

       -  fd is not a valid open file descriptor.
