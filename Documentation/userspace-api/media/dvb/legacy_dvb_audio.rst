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


-----


Audio Function Calls
====================


AUDIO_STOP
----------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_STOP

.. code-block:: c

	 int ioctl(int fd, int request = AUDIO_STOP)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  File descriptor returned by a previous call to `open()`_.

    -  ..

       -  ``int request``

       -  :cspan:`1` Equals ``AUDIO_STOP`` for this command.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the Audio Device to stop playing the current
stream.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_PLAY
----------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_PLAY

.. code-block:: c

	 int  ioctl(int fd, int request = AUDIO_PLAY)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  File descriptor returned by a previous call to `open()`_.

    -  ..

       -  ``int request``

       -  :cspan:`1` Equals ``AUDIO_PLAY`` for this command.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the Audio Device to start playing an audio stream
from the selected source.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_PAUSE
-----------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_PAUSE

.. code-block:: c

	 int  ioctl(int fd, int request = AUDIO_PAUSE)

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

       -  Equals ``AUDIO_PAUSE`` for this command.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call suspends the audio stream being played. Decoding and
playing are paused. It is then possible to restart again decoding and
playing process of the audio stream using `AUDIO_CONTINUE`_ command.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_CONTINUE
--------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_CONTINUE

.. code-block:: c

	 int  ioctl(int fd, int request = AUDIO_CONTINUE)

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

       -  Equals ``AUDIO_CONTINUE`` for this command.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl restarts the decoding and playing process previously paused
with `AUDIO_PAUSE`_ command.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_SELECT_SOURCE
-------------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_SELECT_SOURCE

.. code-block:: c

	 int ioctl(int fd, int request = AUDIO_SELECT_SOURCE,
	 audio_stream_source_t source)

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

       -  Equals ``AUDIO_SELECT_SOURCE`` for this command.

    -  ..

       -  `audio_stream_source_t`_ ``source``

       -  Indicates the source that shall be used for the Audio stream.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call informs the audio device which source shall be used for
the input data. The possible sources are demux or memory. If
``AUDIO_SOURCE_MEMORY`` is selected, the data is fed to the Audio Device
through the write command. If ``AUDIO_SOURCE_DEMUX`` is selected, the data
is directly transferred from the onboard demux-device to the decoder.
Note: This only supports DVB-devices with one demux and one decoder so far.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_SET_MUTE
--------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_SET_MUTE

.. code-block:: c

	 int  ioctl(int fd, int request = AUDIO_SET_MUTE, int state)

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

       -  :cspan:`1` Equals ``AUDIO_SET_MUTE`` for this command.

    -  ..

       -  :rspan:`2` ``int state``

       -  :cspan:`1` Indicates if audio device shall mute or not.

    -  ..

       -  TRUE  ( != 0 )

       -  mute audio

    -  ..

       -  FALSE ( == 0 )

       -  unmute audio

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl is for DVB devices only. To control a V4L2 decoder use the
V4L2 :ref:`VIDIOC_DECODER_CMD` with the
``V4L2_DEC_CMD_START_MUTE_AUDIO`` flag instead.

This ioctl call asks the audio device to mute the stream that is
currently being played.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_SET_AV_SYNC
-----------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_SET_AV_SYNC

.. code-block:: c

	 int  ioctl(int fd, int request = AUDIO_SET_AV_SYNC, int state)

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

       -  :cspan:`1` Equals ``AUDIO_AV_SYNC`` for this command.

    -  ..

       -  :rspan:`2` ``int state``

       -  :cspan:`1` Tells the DVB subsystem if A/V synchronization
          shall be ON or OFF.

    -  ..

       -  TRUE  ( != 0 )

       -  AV-sync ON.

    -  ..

       -  FALSE ( == 0 )

       -  AV-sync OFF.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the Audio Device to turn ON or OFF A/V
synchronization.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_SET_BYPASS_MODE
---------------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_SET_BYPASS_MODE

.. code-block:: c

	 int ioctl(int fd, int request = AUDIO_SET_BYPASS_MODE, int mode)

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

       -  :cspan:`1` Equals ``AUDIO_SET_BYPASS_MODE`` for this command.

    -  ..

       -  :rspan:`2` ``int mode``

       -  :cspan:`1` Enables or disables the decoding of the current
          Audio stream in the DVB subsystem.
    -  ..

       -  TRUE  ( != 0 )

       -  Disable bypass

    -  ..

       -  FALSE ( == 0 )

       -  Enable bypass

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the Audio Device to bypass the Audio decoder and
forward the stream without decoding. This mode shall be used if streams
that can’t be handled by the DVB system shall be decoded. Dolby
DigitalTM streams are automatically forwarded by the DVB subsystem if
the hardware can handle it.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_CHANNEL_SELECT
--------------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_CHANNEL_SELECT

.. code-block:: c

	 int ioctl(int fd, int request = AUDIO_CHANNEL_SELECT,
	 audio_channel_select_t)

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

       -  Equals ``AUDIO_CHANNEL_SELECT`` for this command.

    -  ..

       -  `audio_channel_select_t`_ ``ch``

       -  Select the output format of the audio (mono left/right, stereo).

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl is for DVB devices only. To control a V4L2 decoder use the
V4L2 ``V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK`` control instead.

This ioctl call asks the Audio Device to select the requested channel if
possible.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_GET_STATUS
----------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_GET_STATUS

.. code-block:: c

	 int ioctl(int fd, int request = AUDIO_GET_STATUS,
	 struct audio_status *status)

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

       -  Equals AUDIO_GET_STATUS for this command.

    -  ..

       -  ``struct`` `audio_status`_ ``*status``

       -  Returns the current state of Audio Device.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the Audio Device to return the current state of the
Audio Device.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_GET_CAPABILITIES
----------------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_GET_CAPABILITIES

.. code-block:: c

	 int ioctl(int fd, int request = AUDIO_GET_CAPABILITIES,
	 unsigned int *cap)

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

       -  Equals ``AUDIO_GET_CAPABILITIES`` for this command.

    -  ..

       -  ``unsigned int *cap``

       -  Returns a bit array of supported sound formats.
          Bits are defined in `audio encodings`_.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the Audio Device to tell us about the decoding
capabilities of the audio hardware.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_CLEAR_BUFFER
------------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_CLEAR_BUFFER

.. code-block:: c

	 int  ioctl(int fd, int request = AUDIO_CLEAR_BUFFER)

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

       -  Equals ``AUDIO_CLEAR_BUFFER`` for this command.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl call asks the Audio Device to clear all software and hardware
buffers of the audio decoder device.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_SET_ID
------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_SET_ID

.. code-block:: c

	 int  ioctl(int fd, int request = AUDIO_SET_ID, int id)

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

       -  Equals ``AUDIO_SET_ID`` for this command.

    -  ..

       -  ``int id``

       -  Audio sub-stream id.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl selects which sub-stream is to be decoded if a program or
system stream is sent to the video device.

If no audio stream type is set the id has to be in range [0xC0,0xDF]
for MPEG sound, in [0x80,0x87] for AC3 and in [0xA0,0xA7] for LPCM.
See ITU-T H.222.0 | ISO/IEC 13818-1 for further description.

If the stream type is set with `AUDIO_SET_STREAMTYPE`_, specifies the
id just the sub-stream id of the audio stream and only the first 5 bits
(& 0x1F) are recognized.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_SET_MIXER
---------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_SET_MIXER

.. code-block:: c

	 int ioctl(int fd, int request = AUDIO_SET_MIXER, audio_mixer_t *mix)

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

       -  Equals ``AUDIO_SET_MIXER`` for this command.

    -  ..

       -  ``audio_mixer_t *mix``

       -  Mixer settings.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl lets you adjust the mixer settings of the audio decoder.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


-----


AUDIO_SET_STREAMTYPE
--------------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_SET_STREAMTYPE

.. code-block:: c

	 int  ioctl(fd, int request = AUDIO_SET_STREAMTYPE, int type)

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

       -  Equals ``AUDIO_SET_STREAMTYPE`` for this command.

    -  ..

       -  ``int type``

       -  Stream type.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl tells the driver which kind of audio stream to expect. This
is useful if the stream offers several audio sub-streams like LPCM and
AC3.

Stream types defined in ITU-T H.222.0 | ISO/IEC 13818-1 are used.


Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``EINVAL``

       -  Type is not a valid or supported stream type.


-----


AUDIO_BILINGUAL_CHANNEL_SELECT
------------------------------

Synopsis
~~~~~~~~

.. c:macro:: AUDIO_BILINGUAL_CHANNEL_SELECT

.. code-block:: c

	 int ioctl(int fd, int request = AUDIO_BILINGUAL_CHANNEL_SELECT,
	 audio_channel_select_t)

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

       -  Equals ``AUDIO_BILINGUAL_CHANNEL_SELECT`` for this command.

    -  ..

       -  ``audio_channel_select_t ch``

       -  Select the output format of the audio (mono left/right, stereo).

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl has been replaced by the V4L2
``V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK`` control
for MPEG decoders controlled through V4L2.

This ioctl call asks the Audio Device to select the requested channel
for bilingual streams if possible.

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

.. c:function:: int  open(const char *deviceName, int flags)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``const char *deviceName``

       -  Name of specific audio device.

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

This system call opens a named audio device (e.g.
``/dev/dvb/adapter0/audio0``) for subsequent use. When an open() call has
succeeded, the device will be ready for use. The significance of
blocking or non-blocking mode is described in the documentation for
functions where there is a difference. It does not affect the semantics
of the open() call itself. A device opened in blocking mode can later be
put into non-blocking mode (and vice versa) using the F_SETFL command
of the fcntl system call. This is a standard system call, documented in
the Linux manual page for fcntl. Only one user can open the Audio Device
in O_RDWR mode. All other attempts to open the device in this mode will
fail, and an error code will be returned. If the Audio Device is opened
in O_RDONLY mode, the only ioctl call that can be used is
`AUDIO_GET_STATUS`_. All other call will return with an error code.

Return Value
~~~~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``ENODEV``

       -  Device driver not loaded/available.

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

This system call closes a previously opened audio device.

Return Value
~~~~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``EBADF``

       -  Fd is not a valid open file descriptor.

-----


write()
-------

Synopsis
~~~~~~~~

.. code-block:: c

	 size_t write(int fd, const void *buf, size_t count)

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

This system call can only be used if ``AUDIO_SOURCE_MEMORY`` is selected
in the ioctl call `AUDIO_SELECT_SOURCE`_. The data provided shall be in
PES format. If ``O_NONBLOCK`` is not specified the function will block
until buffer space is available. The amount of data to be transferred is
implied by count.

Return Value
~~~~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``EPERM``

       -  :cspan:`1` Mode ``AUDIO_SOURCE_MEMORY`` not selected.

    -  ..

       -  ``ENOMEM``

       -  Attempted to write more data than the internal buffer can hold.

    -  ..

       -  ``EBADF``

       -  Fd is not a valid open file descriptor.
