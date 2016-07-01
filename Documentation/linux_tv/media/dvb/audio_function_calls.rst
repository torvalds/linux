.. -*- coding: utf-8; mode: rst -*-

.. _audio_function_calls:

********************
Audio Function Calls
********************


.. _audio_fopen:

open()
======

DESCRIPTION

This system call opens a named audio device (e.g.
/dev/dvb/adapter0/audio0) for subsequent use. When an open() call has
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
AUDIO_GET_STATUS. All other call will return with an error code.

SYNOPSIS

int open(const char *deviceName, int flags);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  const char *deviceName

       -  Name of specific audio device.

    -  .. row 2

       -  int flags

       -  A bit-wise OR of the following flags:

    -  .. row 3

       -  
       -  O_RDONLY read-only access

    -  .. row 4

       -  
       -  O_RDWR read/write access

    -  .. row 5

       -  
       -  O_NONBLOCK open in non-blocking mode

    -  .. row 6

       -  
       -  (blocking mode is the default)


RETURN VALUE



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ENODEV

       -  Device driver not loaded/available.

    -  .. row 2

       -  EBUSY

       -  Device or resource busy.

    -  .. row 3

       -  EINVAL

       -  Invalid argument.



.. _audio_fclose:

close()
=======

DESCRIPTION

This system call closes a previously opened audio device.

SYNOPSIS

int close(int fd);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().


RETURN VALUE



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  EBADF

       -  fd is not a valid open file descriptor.



.. _audio_fwrite:

write()
=======

DESCRIPTION

This system call can only be used if AUDIO_SOURCE_MEMORY is selected
in the ioctl call AUDIO_SELECT_SOURCE. The data provided shall be in
PES format. If O_NONBLOCK is not specified the function will block
until buffer space is available. The amount of data to be transferred is
implied by count.

SYNOPSIS

size_t write(int fd, const void *buf, size_t count);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  void *buf

       -  Pointer to the buffer containing the PES data.

    -  .. row 3

       -  size_t count

       -  Size of buf.


RETURN VALUE



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  EPERM

       -  Mode AUDIO_SOURCE_MEMORY not selected.

    -  .. row 2

       -  ENOMEM

       -  Attempted to write more data than the internal buffer can hold.

    -  .. row 3

       -  EBADF

       -  fd is not a valid open file descriptor.



.. _AUDIO_STOP:

AUDIO_STOP
==========

DESCRIPTION

This ioctl call asks the Audio Device to stop playing the current
stream.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_STOP);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_STOP for this command.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_PLAY:

AUDIO_PLAY
==========

DESCRIPTION

This ioctl call asks the Audio Device to start playing an audio stream
from the selected source.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_PLAY);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_PLAY for this command.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_PAUSE:

AUDIO_PAUSE
===========

DESCRIPTION

This ioctl call suspends the audio stream being played. Decoding and
playing are paused. It is then possible to restart again decoding and
playing process of the audio stream using AUDIO_CONTINUE command.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_PAUSE);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_PAUSE for this command.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_CONTINUE:

AUDIO_CONTINUE
==============

DESCRIPTION

This ioctl restarts the decoding and playing process previously paused
with AUDIO_PAUSE command.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_CONTINUE);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_CONTINUE for this command.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_SELECT_SOURCE:

AUDIO_SELECT_SOURCE
===================

DESCRIPTION

This ioctl call informs the audio device which source shall be used for
the input data. The possible sources are demux or memory. If
AUDIO_SOURCE_MEMORY is selected, the data is fed to the Audio Device
through the write command.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_SELECT_SOURCE,
audio_stream_source_t source);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_SELECT_SOURCE for this command.

    -  .. row 3

       -  audio_stream_source_t source

       -  Indicates the source that shall be used for the Audio stream.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_SET_MUTE:

AUDIO_SET_MUTE
==============

DESCRIPTION

This ioctl is for DVB devices only. To control a V4L2 decoder use the
V4L2 :ref:`VIDIOC_DECODER_CMD` with the
``V4L2_DEC_CMD_START_MUTE_AUDIO`` flag instead.

This ioctl call asks the audio device to mute the stream that is
currently being played.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_SET_MUTE, boolean state);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_SET_MUTE for this command.

    -  .. row 3

       -  boolean state

       -  Indicates if audio device shall mute or not.

    -  .. row 4

       -  
       -  TRUE Audio Mute

    -  .. row 5

       -  
       -  FALSE Audio Un-mute


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_SET_AV_SYNC:

AUDIO_SET_AV_SYNC
=================

DESCRIPTION

This ioctl call asks the Audio Device to turn ON or OFF A/V
synchronization.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_SET_AV_SYNC, boolean state);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_AV_SYNC for this command.

    -  .. row 3

       -  boolean state

       -  Tells the DVB subsystem if A/V synchronization shall be ON or OFF.

    -  .. row 4

       -  
       -  TRUE AV-sync ON

    -  .. row 5

       -  
       -  FALSE AV-sync OFF


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_SET_BYPASS_MODE:

AUDIO_SET_BYPASS_MODE
=====================

DESCRIPTION

This ioctl call asks the Audio Device to bypass the Audio decoder and
forward the stream without decoding. This mode shall be used if streams
that can’t be handled by the DVB system shall be decoded. Dolby
DigitalTM streams are automatically forwarded by the DVB subsystem if
the hardware can handle it.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_SET_BYPASS_MODE, boolean mode);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_SET_BYPASS_MODE for this command.

    -  .. row 3

       -  boolean mode

       -  Enables or disables the decoding of the current Audio stream in
          the DVB subsystem.

    -  .. row 4

       -  
       -  TRUE Bypass is disabled

    -  .. row 5

       -  
       -  FALSE Bypass is enabled


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_CHANNEL_SELECT:

AUDIO_CHANNEL_SELECT
====================

DESCRIPTION

This ioctl is for DVB devices only. To control a V4L2 decoder use the
V4L2 ``V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK`` control instead.

This ioctl call asks the Audio Device to select the requested channel if
possible.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_CHANNEL_SELECT,
audio_channel_select_t);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_CHANNEL_SELECT for this command.

    -  .. row 3

       -  audio_channel_select_t ch

       -  Select the output format of the audio (mono left/right, stereo).


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_BILINGUAL_CHANNEL_SELECT:

AUDIO_BILINGUAL_CHANNEL_SELECT
==============================

DESCRIPTION

This ioctl is obsolete. Do not use in new drivers. It has been replaced
by the V4L2 ``V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK`` control
for MPEG decoders controlled through V4L2.

This ioctl call asks the Audio Device to select the requested channel
for bilingual streams if possible.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_BILINGUAL_CHANNEL_SELECT,
audio_channel_select_t);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_BILINGUAL_CHANNEL_SELECT for this command.

    -  .. row 3

       -  audio_channel_select_t ch

       -  Select the output format of the audio (mono left/right, stereo).


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_GET_PTS:

AUDIO_GET_PTS
=============

DESCRIPTION

This ioctl is obsolete. Do not use in new drivers. If you need this
functionality, then please contact the linux-media mailing list
(`https://linuxtv.org/lists.php <https://linuxtv.org/lists.php>`__).

This ioctl call asks the Audio Device to return the current PTS
timestamp.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_GET_PTS, __u64 *pts);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_GET_PTS for this command.

    -  .. row 3

       -  __u64 *pts

       -  Returns the 33-bit timestamp as defined in ITU T-REC-H.222.0 /
          ISO/IEC 13818-1.

          The PTS should belong to the currently played frame if possible,
          but may also be a value close to it like the PTS of the last
          decoded frame or the last PTS extracted by the PES parser.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_GET_STATUS:

AUDIO_GET_STATUS
================

DESCRIPTION

This ioctl call asks the Audio Device to return the current state of the
Audio Device.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_GET_STATUS, struct audio_status
*status);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_GET_STATUS for this command.

    -  .. row 3

       -  struct audio_status *status

       -  Returns the current state of Audio Device.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_GET_CAPABILITIES:

AUDIO_GET_CAPABILITIES
======================

DESCRIPTION

This ioctl call asks the Audio Device to tell us about the decoding
capabilities of the audio hardware.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_GET_CAPABILITIES, unsigned int
*cap);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_GET_CAPABILITIES for this command.

    -  .. row 3

       -  unsigned int *cap

       -  Returns a bit array of supported sound formats.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_CLEAR_BUFFER:

AUDIO_CLEAR_BUFFER
==================

DESCRIPTION

This ioctl call asks the Audio Device to clear all software and hardware
buffers of the audio decoder device.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_CLEAR_BUFFER);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_CLEAR_BUFFER for this command.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_SET_ID:

AUDIO_SET_ID
============

DESCRIPTION

This ioctl selects which sub-stream is to be decoded if a program or
system stream is sent to the video device. If no audio stream type is
set the id has to be in [0xC0,0xDF] for MPEG sound, in [0x80,0x87] for
AC3 and in [0xA0,0xA7] for LPCM. More specifications may follow for
other stream types. If the stream type is set the id just specifies the
substream id of the audio stream and only the first 5 bits are
recognized.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_SET_ID, int id);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_SET_ID for this command.

    -  .. row 3

       -  int id

       -  audio sub-stream id


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_SET_MIXER:

AUDIO_SET_MIXER
===============

DESCRIPTION

This ioctl lets you adjust the mixer settings of the audio decoder.

SYNOPSIS

int ioctl(int fd, int request = AUDIO_SET_MIXER, audio_mixer_t
*mix);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_SET_ID for this command.

    -  .. row 3

       -  audio_mixer_t *mix

       -  mixer settings.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _AUDIO_SET_STREAMTYPE:

AUDIO_SET_STREAMTYPE
====================

DESCRIPTION

This ioctl tells the driver which kind of audio stream to expect. This
is useful if the stream offers several audio sub-streams like LPCM and
AC3.

SYNOPSIS

int ioctl(fd, int request = AUDIO_SET_STREAMTYPE, int type);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_SET_STREAMTYPE for this command.

    -  .. row 3

       -  int type

       -  stream type


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  EINVAL

       -  type is not a valid or supported stream type.



.. _AUDIO_SET_EXT_ID:

AUDIO_SET_EXT_ID
================

DESCRIPTION

This ioctl can be used to set the extension id for MPEG streams in DVD
playback. Only the first 3 bits are recognized.

SYNOPSIS

int ioctl(fd, int request = AUDIO_SET_EXT_ID, int id);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_SET_EXT_ID for this command.

    -  .. row 3

       -  int id

       -  audio sub_stream_id


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  EINVAL

       -  id is not a valid id.



.. _AUDIO_SET_ATTRIBUTES:

AUDIO_SET_ATTRIBUTES
====================

DESCRIPTION

This ioctl is intended for DVD playback and allows you to set certain
information about the audio stream.

SYNOPSIS

int ioctl(fd, int request = AUDIO_SET_ATTRIBUTES, audio_attributes_t
attr );

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_SET_ATTRIBUTES for this command.

    -  .. row 3

       -  audio_attributes_t attr

       -  audio attributes according to section ??


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  EINVAL

       -  attr is not a valid or supported attribute setting.



.. _AUDIO_SET_KARAOKE:

AUDIO_SET_KARAOKE
=================

DESCRIPTION

This ioctl allows one to set the mixer settings for a karaoke DVD.

SYNOPSIS

int ioctl(fd, int request = AUDIO_SET_KARAOKE, audio_karaoke_t
*karaoke);

PARAMETERS



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals AUDIO_SET_KARAOKE for this command.

    -  .. row 3

       -  audio_karaoke_t *karaoke

       -  karaoke settings according to section ??.


RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  EINVAL

       -  karaoke is not a valid or supported karaoke setting.




.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
