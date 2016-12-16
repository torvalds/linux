.. -*- coding: utf-8; mode: rst -*-

.. _audio_data_types:

****************
Audio Data Types
****************

This section describes the structures, data types and defines used when
talking to the audio device.

.. c:type:: audio_stream_source

The audio stream source is set through the AUDIO_SELECT_SOURCE call
and can take the following values, depending on whether we are replaying
from an internal (demux) or external (user write) source.


.. code-block:: c

    typedef enum {
	AUDIO_SOURCE_DEMUX,
	AUDIO_SOURCE_MEMORY
    } audio_stream_source_t;

AUDIO_SOURCE_DEMUX selects the demultiplexer (fed either by the
frontend or the DVR device) as the source of the video stream. If
AUDIO_SOURCE_MEMORY is selected the stream comes from the application
through the ``write()`` system call.


.. c:type:: audio_play_state

The following values can be returned by the AUDIO_GET_STATUS call
representing the state of audio playback.


.. code-block:: c

    typedef enum {
	AUDIO_STOPPED,
	AUDIO_PLAYING,
	AUDIO_PAUSED
    } audio_play_state_t;


.. c:type:: audio_channel_select

The audio channel selected via AUDIO_CHANNEL_SELECT is determined by
the following values.


.. code-block:: c

    typedef enum {
	AUDIO_STEREO,
	AUDIO_MONO_LEFT,
	AUDIO_MONO_RIGHT,
	AUDIO_MONO,
	AUDIO_STEREO_SWAPPED
    } audio_channel_select_t;


.. c:type:: audio_status

The AUDIO_GET_STATUS call returns the following structure informing
about various states of the playback operation.


.. code-block:: c

    typedef struct audio_status {
	boolean AV_sync_state;
	boolean mute_state;
	audio_play_state_t play_state;
	audio_stream_source_t stream_source;
	audio_channel_select_t channel_select;
	boolean bypass_mode;
	audio_mixer_t mixer_state;
    } audio_status_t;


.. c:type:: audio_mixer

The following structure is used by the AUDIO_SET_MIXER call to set the
audio volume.


.. code-block:: c

    typedef struct audio_mixer {
	unsigned int volume_left;
	unsigned int volume_right;
    } audio_mixer_t;


.. _audio_encodings:

audio encodings
===============

A call to AUDIO_GET_CAPABILITIES returns an unsigned integer with the
following bits set according to the hardwares capabilities.


.. code-block:: c

     #define AUDIO_CAP_DTS    1
     #define AUDIO_CAP_LPCM   2
     #define AUDIO_CAP_MP1    4
     #define AUDIO_CAP_MP2    8
     #define AUDIO_CAP_MP3   16
     #define AUDIO_CAP_AAC   32
     #define AUDIO_CAP_OGG   64
     #define AUDIO_CAP_SDDS 128
     #define AUDIO_CAP_AC3  256

.. c:type:: audio_karaoke

The ioctl AUDIO_SET_KARAOKE uses the following format:


.. code-block:: c

    typedef
    struct audio_karaoke {
	int vocal1;
	int vocal2;
	int melody;
    } audio_karaoke_t;

If Vocal1 or Vocal2 are non-zero, they get mixed into left and right t
at 70% each. If both, Vocal1 and Vocal2 are non-zero, Vocal1 gets mixed
into the left channel and Vocal2 into the right channel at 100% each. Ff
Melody is non-zero, the melody channel gets mixed into left and right.


.. c:type:: audio_attributes

The following attributes can be set by a call to AUDIO_SET_ATTRIBUTES:


.. code-block:: c

     typedef uint16_t audio_attributes_t;
     /*   bits: descr. */
     /*   15-13 audio coding mode (0=ac3, 2=mpeg1, 3=mpeg2ext, 4=LPCM, 6=DTS, */
     /*   12    multichannel extension */
     /*   11-10 audio type (0=not spec, 1=language included) */
     /*    9- 8 audio application mode (0=not spec, 1=karaoke, 2=surround) */
     /*    7- 6 Quantization / DRC (mpeg audio: 1=DRC exists)(lpcm: 0=16bit,  */
     /*    5- 4 Sample frequency fs (0=48kHz, 1=96kHz) */
     /*    2- 0 number of audio channels (n+1 channels) */
