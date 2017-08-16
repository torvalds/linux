.. -*- coding: utf-8; mode: rst -*-

.. _video_types:

****************
Video Data Types
****************


.. _video-format-t:

video_format_t
==============

The ``video_format_t`` data type defined by


.. code-block:: c

    typedef enum {
	VIDEO_FORMAT_4_3,     /* Select 4:3 format */
	VIDEO_FORMAT_16_9,    /* Select 16:9 format. */
	VIDEO_FORMAT_221_1    /* 2.21:1 */
    } video_format_t;

is used in the VIDEO_SET_FORMAT function (??) to tell the driver which
aspect ratio the output hardware (e.g. TV) has. It is also used in the
data structures video_status (??) returned by VIDEO_GET_STATUS (??)
and video_event (??) returned by VIDEO_GET_EVENT (??) which report
about the display format of the current video stream.


.. _video-displayformat-t:

video_displayformat_t
=====================

In case the display format of the video stream and of the display
hardware differ the application has to specify how to handle the
cropping of the picture. This can be done using the
VIDEO_SET_DISPLAY_FORMAT call (??) which accepts


.. code-block:: c

    typedef enum {
	VIDEO_PAN_SCAN,       /* use pan and scan format */
	VIDEO_LETTER_BOX,     /* use letterbox format */
	VIDEO_CENTER_CUT_OUT  /* use center cut out format */
    } video_displayformat_t;

as argument.


.. _video-stream-source-t:

video_stream_source_t
=====================

The video stream source is set through the VIDEO_SELECT_SOURCE call
and can take the following values, depending on whether we are replaying
from an internal (demuxer) or external (user write) source.


.. code-block:: c

    typedef enum {
	VIDEO_SOURCE_DEMUX, /* Select the demux as the main source */
	VIDEO_SOURCE_MEMORY /* If this source is selected, the stream
		       comes from the user through the write
		       system call */
    } video_stream_source_t;

VIDEO_SOURCE_DEMUX selects the demultiplexer (fed either by the
frontend or the DVR device) as the source of the video stream. If
VIDEO_SOURCE_MEMORY is selected the stream comes from the application
through the **write()** system call.


.. _video-play-state-t:

video_play_state_t
==================

The following values can be returned by the VIDEO_GET_STATUS call
representing the state of video playback.


.. code-block:: c

    typedef enum {
	VIDEO_STOPPED, /* Video is stopped */
	VIDEO_PLAYING, /* Video is currently playing */
	VIDEO_FREEZED  /* Video is freezed */
    } video_play_state_t;


.. c:type:: video_command

struct video_command
====================

The structure must be zeroed before use by the application This ensures
it can be extended safely in the future.


.. code-block:: c

    struct video_command {
	__u32 cmd;
	__u32 flags;
	union {
	    struct {
		__u64 pts;
	    } stop;

	    struct {
		/* 0 or 1000 specifies normal speed,
		   1 specifies forward single stepping,
		   -1 specifies backward single stepping,
		   >>1: playback at speed/1000 of the normal speed,
		   <-1: reverse playback at (-speed/1000) of the normal speed. */
		__s32 speed;
		__u32 format;
	    } play;

	    struct {
		__u32 data[16];
	    } raw;
	};
    };


.. _video-size-t:

video_size_t
============


.. code-block:: c

    typedef struct {
	int w;
	int h;
	video_format_t aspect_ratio;
    } video_size_t;


.. c:type:: video_event

struct video_event
==================

The following is the structure of a video event as it is returned by the
VIDEO_GET_EVENT call.


.. code-block:: c

    struct video_event {
	__s32 type;
    #define VIDEO_EVENT_SIZE_CHANGED    1
    #define VIDEO_EVENT_FRAME_RATE_CHANGED  2
    #define VIDEO_EVENT_DECODER_STOPPED     3
    #define VIDEO_EVENT_VSYNC       4
	__kernel_time_t timestamp;
	union {
	    video_size_t size;
	    unsigned int frame_rate;    /* in frames per 1000sec */
	    unsigned char vsync_field;  /* unknown/odd/even/progressive */
	} u;
    };


.. c:type:: video_status

struct video_status
===================

The VIDEO_GET_STATUS call returns the following structure informing
about various states of the playback operation.


.. code-block:: c

    struct video_status {
	int                   video_blank;   /* blank video on freeze? */
	video_play_state_t    play_state;    /* current state of playback */
	video_stream_source_t stream_source; /* current source (demux/memory) */
	video_format_t        video_format;  /* current aspect ratio of stream */
	video_displayformat_t display_format;/* selected cropping mode */
    };

If video_blank is set video will be blanked out if the channel is
changed or if playback is stopped. Otherwise, the last picture will be
displayed. play_state indicates if the video is currently frozen,
stopped, or being played back. The stream_source corresponds to the
seleted source for the video stream. It can come either from the
demultiplexer or from memory. The video_format indicates the aspect
ratio (one of 4:3 or 16:9) of the currently played video stream.
Finally, display_format corresponds to the selected cropping mode in
case the source video format is not the same as the format of the output
device.


.. c:type:: video_still_picture

struct video_still_picture
==========================

An I-frame displayed via the VIDEO_STILLPICTURE call is passed on
within the following structure.


.. code-block:: c

    /* pointer to and size of a single iframe in memory */
    struct video_still_picture {
	char *iFrame;        /* pointer to a single iframe in memory */
	int32_t size;
    };


.. _video_caps:

video capabilities
==================

A call to VIDEO_GET_CAPABILITIES returns an unsigned integer with the
following bits set according to the hardwares capabilities.


.. code-block:: c

     /* bit definitions for capabilities: */
     /* can the hardware decode MPEG1 and/or MPEG2? */
     #define VIDEO_CAP_MPEG1   1
     #define VIDEO_CAP_MPEG2   2
     /* can you send a system and/or program stream to video device?
	(you still have to open the video and the audio device but only
	 send the stream to the video device) */
     #define VIDEO_CAP_SYS     4
     #define VIDEO_CAP_PROG    8
     /* can the driver also handle SPU, NAVI and CSS encoded data?
	(CSS API is not present yet) */
     #define VIDEO_CAP_SPU    16
     #define VIDEO_CAP_NAVI   32
     #define VIDEO_CAP_CSS    64


.. _video-system:

video_system_t
==============

A call to VIDEO_SET_SYSTEM sets the desired video system for TV
output. The following system types can be set:


.. code-block:: c

    typedef enum {
	 VIDEO_SYSTEM_PAL,
	 VIDEO_SYSTEM_NTSC,
	 VIDEO_SYSTEM_PALN,
	 VIDEO_SYSTEM_PALNc,
	 VIDEO_SYSTEM_PALM,
	 VIDEO_SYSTEM_NTSC60,
	 VIDEO_SYSTEM_PAL60,
	 VIDEO_SYSTEM_PALM60
    } video_system_t;


.. c:type:: video_highlight

struct video_highlight
======================

Calling the ioctl VIDEO_SET_HIGHLIGHTS posts the SPU highlight
information. The call expects the following format for that information:


.. code-block:: c

     typedef
     struct video_highlight {
	 boolean active;      /*    1=show highlight, 0=hide highlight */
	 uint8_t contrast1;   /*    7- 4  Pattern pixel contrast */
		      /*    3- 0  Background pixel contrast */
	 uint8_t contrast2;   /*    7- 4  Emphasis pixel-2 contrast */
		      /*    3- 0  Emphasis pixel-1 contrast */
	 uint8_t color1;      /*    7- 4  Pattern pixel color */
		      /*    3- 0  Background pixel color */
	 uint8_t color2;      /*    7- 4  Emphasis pixel-2 color */
		      /*    3- 0  Emphasis pixel-1 color */
	 uint32_t ypos;       /*   23-22  auto action mode */
		      /*   21-12  start y */
		      /*    9- 0  end y */
	 uint32_t xpos;       /*   23-22  button color number */
		      /*   21-12  start x */
		      /*    9- 0  end x */
     } video_highlight_t;


.. c:type:: video_spu

struct video_spu
================

Calling VIDEO_SET_SPU deactivates or activates SPU decoding, according
to the following format:


.. code-block:: c

     typedef
     struct video_spu {
	 boolean active;
	 int stream_id;
     } video_spu_t;


.. c:type:: video_spu_palette

struct video_spu_palette
========================

The following structure is used to set the SPU palette by calling
VIDEO_SPU_PALETTE:


.. code-block:: c

     typedef
     struct video_spu_palette {
	 int length;
	 uint8_t *palette;
     } video_spu_palette_t;


.. c:type:: video_navi_pack

struct video_navi_pack
======================

In order to get the navigational data the following structure has to be
passed to the ioctl VIDEO_GET_NAVI:


.. code-block:: c

     typedef
     struct video_navi_pack {
	 int length;         /* 0 ... 1024 */
	 uint8_t data[1024];
     } video_navi_pack_t;


.. _video-attributes-t:

video_attributes_t
==================

The following attributes can be set by a call to VIDEO_SET_ATTRIBUTES:


.. code-block:: c

     typedef uint16_t video_attributes_t;
     /*   bits: descr. */
     /*   15-14 Video compression mode (0=MPEG-1, 1=MPEG-2) */
     /*   13-12 TV system (0=525/60, 1=625/50) */
     /*   11-10 Aspect ratio (0=4:3, 3=16:9) */
     /*    9- 8 permitted display mode on 4:3 monitor (0=both, 1=only pan-sca */
     /*    7    line 21-1 data present in GOP (1=yes, 0=no) */
     /*    6    line 21-2 data present in GOP (1=yes, 0=no) */
     /*    5- 3 source resolution (0=720x480/576, 1=704x480/576, 2=352x480/57 */
     /*    2    source letterboxed (1=yes, 0=no) */
     /*    0    film/camera mode (0=camera, 1=film (625/50 only)) */
