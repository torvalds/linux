.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later OR GPL-2.0

.. c:namespace:: dtv.legacy.audio

.. _dvb_audio:

================
DVB Audio Device
================

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

The DVB audio device controls the MPEG2 audio decoder of the DVB
hardware. It can be accessed through ``/dev/dvb/adapter?/audio?``. Data
types and ioctl definitions can be accessed by including
``linux/dvb/audio.h`` in your application.

Please note that most DVB cards don’t have their own MPEG decoder, which
results in the omission of the audio and video device.

These ioctls were also used by V4L2 to control MPEG decoders implemented
in V4L2. The use of these ioctls for that purpose has been made obsolete
and proper V4L2 ioctls or controls have been created to replace that
functionality. Use :ref:`V4L2 ioctls<audio>` for new drivers!


Audio Data Types
================

This section describes the structures, data types and defines used when
talking to the audio device.


-----


audio_stream_source_t
---------------------

Synopsis
~~~~~~~~

.. c:enum:: audio_stream_source_t

.. code-block:: c

    typedef enum {
    AUDIO_SOURCE_DEMUX,
    AUDIO_SOURCE_MEMORY
    } audio_stream_source_t;

Constants
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``AUDIO_SOURCE_DEMUX``

       -  :cspan:`1` Selects the demultiplexer (fed either by the frontend
          or the DVR device) as the source of the video stream.

    -  ..

       -  ``AUDIO_SOURCE_MEMORY``

       -  Selects the stream from the application that comes through
          the `write()`_ system call.

Description
~~~~~~~~~~~

The audio stream source is set through the `AUDIO_SELECT_SOURCE`_ call
and can take the following values, depending on whether we are replaying
from an internal (demux) or external (user write) source.

The data fed to the decoder is also controlled by the PID-filter.
Output selection: :c:type:`dmx_output` ``DMX_OUT_DECODER``.


-----


audio_play_state_t
------------------

Synopsis
~~~~~~~~

.. c:enum:: audio_play_state_t

.. code-block:: c

    typedef enum {
	AUDIO_STOPPED,
	AUDIO_PLAYING,
	AUDIO_PAUSED
    } audio_play_state_t;

Constants
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``AUDIO_STOPPED``

       -  Audio is stopped.

    -  ..

       -  ``AUDIO_PLAYING``

       -  Audio is currently playing.

    -  ..

       -  ``AUDIO_PAUSE``

       -  Audio is frozen.

Description
~~~~~~~~~~~

This values can be returned by the `AUDIO_GET_STATUS`_ call
representing the state of audio playback.


-----


audio_channel_select_t
----------------------

Synopsis
~~~~~~~~

.. c:enum:: audio_channel_select_t

.. code-block:: c

    typedef enum {
	AUDIO_STEREO,
	AUDIO_MONO_LEFT,
	AUDIO_MONO_RIGHT,
	AUDIO_MONO,
	AUDIO_STEREO_SWAPPED
    } audio_channel_select_t;

Constants
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``AUDIO_STEREO``

       -  Stereo.

    -  ..

       -  ``AUDIO_MONO_LEFT``

       -  Mono, select left stereo channel as source.

    -  ..

       -  ``AUDIO_MONO_RIGHT``

       -  Mono, select right stereo channel as source.

    -  ..

       -  ``AUDIO_MONO``

       -  Mono source only.

    -  ..

       -  ``AUDIO_STEREO_SWAPPED``

       -  Stereo, swap L & R.

Description
~~~~~~~~~~~

The audio channel selected via `AUDIO_CHANNEL_SELECT`_ is determined by
this values.


-----


audio_mixer_t
-------------

Synopsis
~~~~~~~~

.. c:struct:: audio_mixer

.. code-block:: c

    typedef struct audio_mixer {
	unsigned int volume_left;
	unsigned int volume_right;
    } audio_mixer_t;

Variables
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``unsigned int volume_left``

       -  Volume left channel.
          Valid range: 0 ... 255

    -  ..

       -  ``unsigned int volume_right``

       -  Volume right channel.
          Valid range: 0 ... 255

Description
~~~~~~~~~~~

This structure is used by the `AUDIO_SET_MIXER`_ call to set the
audio volume.


-----


audio_status
------------

Synopsis
~~~~~~~~

.. c:struct:: audio_status

.. code-block:: c

    typedef struct audio_status {
	int AV_sync_state;
	int mute_state;
	audio_play_state_t play_state;
	audio_stream_source_t stream_source;
	audio_channel_select_t channel_select;
	int bypass_mode;
	audio_mixer_t mixer_state;
    } audio_status_t;

Variables
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  :rspan:`2` ``int AV_sync_state``

       -  :cspan:`1` Shows if A/V synchronization is ON or OFF.

    -  ..

       -  TRUE  ( != 0 )

       -  AV-sync ON.

    -  ..

       -  FALSE ( == 0 )

       -  AV-sync OFF.

    -  ..

       -  :rspan:`2` ``int mute_state``

       -  :cspan:`1` Indicates if audio is muted or not.

    -  ..

       -  TRUE  ( != 0 )

       -  mute audio

    -  ..

       -  FALSE ( == 0 )

       -  unmute audio

    -  ..

       -  `audio_play_state_t`_ ``play_state``

       -  Current playback state.

    -  ..

       -  `audio_stream_source_t`_ ``stream_source``

       -  Current source of the data.

    -  ..

       -  :rspan:`2` ``int bypass_mode``

       -  :cspan:`1` Is the decoding of the current Audio stream in
          the DVB subsystem enabled or disabled.

    -  ..

       -  TRUE  ( != 0 )

       -  Bypass disabled.

    -  ..

       -  FALSE ( == 0 )

       -  Bypass enabled.

    -  ..

       -  `audio_mixer_t`_ ``mixer_state``

       -  Current volume settings.

Description
~~~~~~~~~~~

The `AUDIO_GET_STATUS`_ call returns this structure as information
about various states of the playback operation.


-----


audio encodings
---------------

Synopsis
~~~~~~~~

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

Constants
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``AUDIO_CAP_DTS``

       -  :cspan:`1` The hardware accepts DTS audio tracks.

    -  ..

       -  ``AUDIO_CAP_LPCM``

       -   The hardware accepts uncompressed audio with
           Linear Pulse-Code Modulation (LPCM)

    -  ..

       -  ``AUDIO_CAP_MP1``

       -  The hardware accepts MPEG-1 Audio Layer 1.

    -  ..

       -  ``AUDIO_CAP_MP2``

       -  The hardware accepts MPEG-1 Audio Layer 2.
          Also known as MUSICAM.

    -  ..

       -  ``AUDIO_CAP_MP3``

       -  The hardware accepts MPEG-1 Audio Layer III.
          Commomly known as .mp3.

    -  ..

       -  ``AUDIO_CAP_AAC``

       -  The hardware accepts AAC (Advanced Audio Coding).

    -  ..

       -  ``AUDIO_CAP_OGG``

       -  The hardware accepts Vorbis audio tracks.

    -  ..

       -  ``AUDIO_CAP_SDDS``

       -  The hardware accepts Sony Dynamic Digital Sound (SDDS).

    -  ..

       -  ``AUDIO_CAP_AC3``

       -  The hardware accepts Dolby Digital ATSC A/52 audio.
          Also known as AC-3.

Description
~~~~~~~~~~~

A call to `AUDIO_GET_CAPABILITIES`_ returns an unsigned integer with the
following bits set according to the hardwares capabilities.
