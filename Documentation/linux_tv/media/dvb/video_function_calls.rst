.. -*- coding: utf-8; mode: rst -*-

.. _video_function_calls:

********************
Video Function Calls
********************


.. _video_fopen:

dvb video open()
================

Description
-----------

This system call opens a named video device (e.g.
/dev/dvb/adapter0/video0) for subsequent use.

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
ioctl call that can be used is VIDEO_GET_STATUS. All other call will
return an error code.

Synopsis
--------

.. c:function:: int open(const char *deviceName, int flags)

Arguments
----------

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  const char \*deviceName

       -  Name of specific video device.

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


Return Value
------------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``ENODEV``

       -  Device driver not loaded/available.

    -  .. row 2

       -  ``EINTERNAL``

       -  Internal error.

    -  .. row 3

       -  ``EBUSY``

       -  Device or resource busy.

    -  .. row 4

       -  ``EINVAL``

       -  Invalid argument.



.. _video_fclose:

dvb video close()
=================

Description
-----------

This system call closes a previously opened video device.

Synopsis
--------

.. c:function:: int close(int fd)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().


Return Value
------------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EBADF``

       -  fd is not a valid open file descriptor.



.. _video_fwrite:

dvb video write()
=================

Description
-----------

This system call can only be used if VIDEO_SOURCE_MEMORY is selected
in the ioctl call VIDEO_SELECT_SOURCE. The data provided shall be in
PES format, unless the capability allows other formats. If O_NONBLOCK
is not specified the function will block until buffer space is
available. The amount of data to be transferred is implied by count.

Synopsis
--------

.. c:function:: size_t write(int fd, const void *buf, size_t count)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  void \*buf

       -  Pointer to the buffer containing the PES data.

    -  .. row 3

       -  size_t count

       -  Size of buf.


Return Value
------------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EPERM``

       -  Mode VIDEO_SOURCE_MEMORY not selected.

    -  .. row 2

       -  ``ENOMEM``

       -  Attempted to write more data than the internal buffer can hold.

    -  .. row 3

       -  ``EBADF``

       -  fd is not a valid open file descriptor.



.. _VIDEO_STOP:

VIDEO_STOP
==========

Description
-----------

This ioctl is for DVB devices only. To control a V4L2 decoder use the
V4L2 :ref:`VIDIOC_DECODER_CMD` instead.

This ioctl call asks the Video Device to stop playing the current
stream. Depending on the input parameter, the screen can be blanked out
or displaying the last decoded frame.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_STOP, boolean mode)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_STOP for this command.

    -  .. row 3

       -  Boolean mode

       -  Indicates how the screen shall be handled.

    -  .. row 4

       -
       -  TRUE: Blank screen when stop.

    -  .. row 5

       -
       -  FALSE: Show last decoded frame.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_PLAY:

VIDEO_PLAY
==========

Description
-----------

This ioctl is for DVB devices only. To control a V4L2 decoder use the
V4L2 :ref:`VIDIOC_DECODER_CMD` instead.

This ioctl call asks the Video Device to start playing a video stream
from the selected source.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_PLAY)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_PLAY for this command.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_FREEZE:

VIDEO_FREEZE
============

Description
-----------

This ioctl is for DVB devices only. To control a V4L2 decoder use the
V4L2 :ref:`VIDIOC_DECODER_CMD` instead.

This ioctl call suspends the live video stream being played. Decoding
and playing are frozen. It is then possible to restart the decoding and
playing process of the video stream using the VIDEO_CONTINUE command.
If VIDEO_SOURCE_MEMORY is selected in the ioctl call
VIDEO_SELECT_SOURCE, the DVB subsystem will not decode any more data
until the ioctl call VIDEO_CONTINUE or VIDEO_PLAY is performed.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_FREEZE)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_FREEZE for this command.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_CONTINUE:

VIDEO_CONTINUE
==============

Description
-----------

This ioctl is for DVB devices only. To control a V4L2 decoder use the
V4L2 :ref:`VIDIOC_DECODER_CMD` instead.

This ioctl call restarts decoding and playing processes of the video
stream which was played before a call to VIDEO_FREEZE was made.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_CONTINUE)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_CONTINUE for this command.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_SELECT_SOURCE:

VIDEO_SELECT_SOURCE
===================

Description
-----------

This ioctl is for DVB devices only. This ioctl was also supported by the
V4L2 ivtv driver, but that has been replaced by the ivtv-specific
``IVTV_IOC_PASSTHROUGH_MODE`` ioctl.

This ioctl call informs the video device which source shall be used for
the input data. The possible sources are demux or memory. If memory is
selected, the data is fed to the video device through the write command.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SELECT_SOURCE, video_stream_source_t source)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SELECT_SOURCE for this command.

    -  .. row 3

       -  video_stream_source_t source

       -  Indicates which source shall be used for the Video stream.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_SET_BLANK:

VIDEO_SET_BLANK
===============

Description
-----------

This ioctl call asks the Video Device to blank out the picture.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_BLANK, boolean mode)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SET_BLANK for this command.

    -  .. row 3

       -  boolean mode

       -  TRUE: Blank screen when stop.

    -  .. row 4

       -
       -  FALSE: Show last decoded frame.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_GET_STATUS:

VIDEO_GET_STATUS
================

Description
-----------

This ioctl call asks the Video Device to return the current status of
the device.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_GET_STATUS, struct video_status *status)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_GET_STATUS for this command.

    -  .. row 3

       -  struct video_status \*status

       -  Returns the current status of the Video Device.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_GET_FRAME_COUNT:

VIDEO_GET_FRAME_COUNT
=====================

Description
-----------

This ioctl is obsolete. Do not use in new drivers. For V4L2 decoders
this ioctl has been replaced by the ``V4L2_CID_MPEG_VIDEO_DEC_FRAME``
control.

This ioctl call asks the Video Device to return the number of displayed
frames since the decoder was started.

Synopsis
--------

.. c:function:: int ioctl(int fd, int request = VIDEO_GET_FRAME_COUNT, __u64 *pts)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_GET_FRAME_COUNT for this command.

    -  .. row 3

       -  __u64 \*pts

       -  Returns the number of frames displayed since the decoder was
	  started.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_GET_PTS:

VIDEO_GET_PTS
=============

Description
-----------

This ioctl is obsolete. Do not use in new drivers. For V4L2 decoders
this ioctl has been replaced by the ``V4L2_CID_MPEG_VIDEO_DEC_PTS``
control.

This ioctl call asks the Video Device to return the current PTS
timestamp.

Synopsis
--------

.. c:function:: int ioctl(int fd, int request = VIDEO_GET_PTS, __u64 *pts)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_GET_PTS for this command.

    -  .. row 3

       -  __u64 \*pts

       -  Returns the 33-bit timestamp as defined in ITU T-REC-H.222.0 /
	  ISO/IEC 13818-1.

	  The PTS should belong to the currently played frame if possible,
	  but may also be a value close to it like the PTS of the last
	  decoded frame or the last PTS extracted by the PES parser.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_GET_FRAME_RATE:

VIDEO_GET_FRAME_RATE
====================

Description
-----------

This ioctl call asks the Video Device to return the current framerate.

Synopsis
--------

.. c:function:: int ioctl(int fd, int request = VIDEO_GET_FRAME_RATE, unsigned int *rate)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_GET_FRAME_RATE for this command.

    -  .. row 3

       -  unsigned int \*rate

       -  Returns the framerate in number of frames per 1000 seconds.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_GET_EVENT:

VIDEO_GET_EVENT
===============

Description
-----------

This ioctl is for DVB devices only. To get events from a V4L2 decoder
use the V4L2 :ref:`VIDIOC_DQEVENT` ioctl instead.

This ioctl call returns an event of type video_event if available. If
an event is not available, the behavior depends on whether the device is
in blocking or non-blocking mode. In the latter case, the call fails
immediately with errno set to ``EWOULDBLOCK``. In the former case, the call
blocks until an event becomes available. The standard Linux poll()
and/or select() system calls can be used with the device file descriptor
to watch for new events. For select(), the file descriptor should be
included in the exceptfds argument, and for poll(), POLLPRI should be
specified as the wake-up condition. Read-only permissions are sufficient
for this ioctl call.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_GET_EVENT, struct video_event *ev)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_GET_EVENT for this command.

    -  .. row 3

       -  struct video_event \*ev

       -  Points to the location where the event, if any, is to be stored.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EWOULDBLOCK``

       -  There is no event pending, and the device is in non-blocking mode.

    -  .. row 2

       -  ``EOVERFLOW``

       -  Overflow in event queue - one or more events were lost.



.. _VIDEO_COMMAND:

VIDEO_COMMAND
=============

Description
-----------

This ioctl is obsolete. Do not use in new drivers. For V4L2 decoders
this ioctl has been replaced by the
:ref:`VIDIOC_DECODER_CMD` ioctl.

This ioctl commands the decoder. The ``video_command`` struct is a
subset of the ``v4l2_decoder_cmd`` struct, so refer to the
:ref:`VIDIOC_DECODER_CMD` documentation for
more information.

Synopsis
--------

.. c:function:: int ioctl(int fd, int request = VIDEO_COMMAND, struct video_command *cmd)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_COMMAND for this command.

    -  .. row 3

       -  struct video_command \*cmd

       -  Commands the decoder.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_TRY_COMMAND:

VIDEO_TRY_COMMAND
=================

Description
-----------

This ioctl is obsolete. Do not use in new drivers. For V4L2 decoders
this ioctl has been replaced by the
:ref:`VIDIOC_TRY_DECODER_CMD <VIDIOC_DECODER_CMD>` ioctl.

This ioctl tries a decoder command. The ``video_command`` struct is a
subset of the ``v4l2_decoder_cmd`` struct, so refer to the
:ref:`VIDIOC_TRY_DECODER_CMD <VIDIOC_DECODER_CMD>` documentation
for more information.

Synopsis
--------

.. c:function:: int ioctl(int fd, int request = VIDEO_TRY_COMMAND, struct video_command *cmd)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_TRY_COMMAND for this command.

    -  .. row 3

       -  struct video_command \*cmd

       -  Try a decoder command.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_GET_SIZE:

VIDEO_GET_SIZE
==============

Description
-----------

This ioctl returns the size and aspect ratio.

Synopsis
--------

.. c:function:: int ioctl(int fd, int request = VIDEO_GET_SIZE, video_size_t *size)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_GET_SIZE for this command.

    -  .. row 3

       -  video_size_t \*size

       -  Returns the size and aspect ratio.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_SET_DISPLAY_FORMAT:

VIDEO_SET_DISPLAY_FORMAT
========================

Description
-----------

This ioctl call asks the Video Device to select the video format to be
applied by the MPEG chip on the video.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_DISPLAY_FORMAT, video_display_format_t format)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SET_DISPLAY_FORMAT for this command.

    -  .. row 3

       -  video_display_format_t format

       -  Selects the video format to be used.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_STILLPICTURE:

VIDEO_STILLPICTURE
==================

Description
-----------

This ioctl call asks the Video Device to display a still picture
(I-frame). The input data shall contain an I-frame. If the pointer is
NULL, then the current displayed still picture is blanked.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_STILLPICTURE, struct video_still_picture *sp)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_STILLPICTURE for this command.

    -  .. row 3

       -  struct video_still_picture \*sp

       -  Pointer to a location where an I-frame and size is stored.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_FAST_FORWARD:

VIDEO_FAST_FORWARD
==================

Description
-----------

This ioctl call asks the Video Device to skip decoding of N number of
I-frames. This call can only be used if VIDEO_SOURCE_MEMORY is
selected.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_FAST_FORWARD, int nFrames)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_FAST_FORWARD for this command.

    -  .. row 3

       -  int nFrames

       -  The number of frames to skip.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EPERM``

       -  Mode VIDEO_SOURCE_MEMORY not selected.



.. _VIDEO_SLOWMOTION:

VIDEO_SLOWMOTION
================

Description
-----------

This ioctl call asks the video device to repeat decoding frames N number
of times. This call can only be used if VIDEO_SOURCE_MEMORY is
selected.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SLOWMOTION, int nFrames)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SLOWMOTION for this command.

    -  .. row 3

       -  int nFrames

       -  The number of times to repeat each frame.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EPERM``

       -  Mode VIDEO_SOURCE_MEMORY not selected.



.. _VIDEO_GET_CAPABILITIES:

VIDEO_GET_CAPABILITIES
======================

Description
-----------

This ioctl call asks the video device about its decoding capabilities.
On success it returns and integer which has bits set according to the
defines in section ??.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_GET_CAPABILITIES, unsigned int *cap)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_GET_CAPABILITIES for this command.

    -  .. row 3

       -  unsigned int \*cap

       -  Pointer to a location where to store the capability information.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_SET_ID:

VIDEO_SET_ID
============

Description
-----------

This ioctl selects which sub-stream is to be decoded if a program or
system stream is sent to the video device.

Synopsis
--------

.. c:function:: int ioctl(int fd, int request = VIDEO_SET_ID, int id)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SET_ID for this command.

    -  .. row 3

       -  int id

       -  video sub-stream id


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  Invalid sub-stream id.



.. _VIDEO_CLEAR_BUFFER:

VIDEO_CLEAR_BUFFER
==================

Description
-----------

This ioctl call clears all video buffers in the driver and in the
decoder hardware.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_CLEAR_BUFFER)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_CLEAR_BUFFER for this command.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_SET_STREAMTYPE:

VIDEO_SET_STREAMTYPE
====================

Description
-----------

This ioctl tells the driver which kind of stream to expect being written
to it. If this call is not used the default of video PES is used. Some
drivers might not support this call and always expect PES.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_STREAMTYPE, int type)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SET_STREAMTYPE for this command.

    -  .. row 3

       -  int type

       -  stream type


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_SET_FORMAT:

VIDEO_SET_FORMAT
================

Description
-----------

This ioctl sets the screen format (aspect ratio) of the connected output
device (TV) so that the output of the decoder can be adjusted
accordingly.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_FORMAT, video_format_t format)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SET_FORMAT for this command.

    -  .. row 3

       -  video_format_t format

       -  video format of TV as defined in section ??.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  format is not a valid video format.



.. _VIDEO_SET_SYSTEM:

VIDEO_SET_SYSTEM
================

Description
-----------

This ioctl sets the television output format. The format (see section
??) may vary from the color format of the displayed MPEG stream. If the
hardware is not able to display the requested format the call will
return an error.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_SYSTEM , video_system_t system)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SET_FORMAT for this command.

    -  .. row 3

       -  video_system_t system

       -  video system of TV output.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  system is not a valid or supported video system.



.. _VIDEO_SET_HIGHLIGHT:

VIDEO_SET_HIGHLIGHT
===================

Description
-----------

This ioctl sets the SPU highlight information for the menu access of a
DVD.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_HIGHLIGHT ,video_highlight_t *vhilite)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SET_HIGHLIGHT for this command.

    -  .. row 3

       -  video_highlight_t \*vhilite

       -  SPU Highlight information according to section ??.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _VIDEO_SET_SPU:

VIDEO_SET_SPU
=============

Description
-----------

This ioctl activates or deactivates SPU decoding in a DVD input stream.
It can only be used, if the driver is able to handle a DVD stream.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_SPU , video_spu_t *spu)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SET_SPU for this command.

    -  .. row 3

       -  video_spu_t \*spu

       -  SPU decoding (de)activation and subid setting according to section
	  ??.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  input is not a valid spu setting or driver cannot handle SPU.



.. _VIDEO_SET_SPU_PALETTE:

VIDEO_SET_SPU_PALETTE
=====================

Description
-----------

This ioctl sets the SPU color palette.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_SPU_PALETTE, video_spu_palette_t *palette )

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SET_SPU_PALETTE for this command.

    -  .. row 3

       -  video_spu_palette_t \*palette

       -  SPU palette according to section ??.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  input is not a valid palette or driver doesn’t handle SPU.



.. _VIDEO_GET_NAVI:

VIDEO_GET_NAVI
==============

Description
-----------

This ioctl returns navigational information from the DVD stream. This is
especially needed if an encoded stream has to be decoded by the
hardware.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_GET_NAVI , video_navi_pack_t *navipack)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_GET_NAVI for this command.

    -  .. row 3

       -  video_navi_pack_t \*navipack

       -  PCI or DSI pack (private stream 2) according to section ??.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EFAULT``

       -  driver is not able to return navigational information



.. _VIDEO_SET_ATTRIBUTES:

VIDEO_SET_ATTRIBUTES
====================

Description
-----------

This ioctl is intended for DVD playback and allows you to set certain
information about the stream. Some hardware may not need this
information, but the call also tells the hardware to prepare for DVD
playback.

Synopsis
--------

.. c:function:: int ioctl(fd, int request = VIDEO_SET_ATTRIBUTE ,video_attributes_t vattr)

Arguments
----------



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  int fd

       -  File descriptor returned by a previous call to open().

    -  .. row 2

       -  int request

       -  Equals VIDEO_SET_ATTRIBUTE for this command.

    -  .. row 3

       -  video_attributes_t vattr

       -  video attributes according to section ??.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EINVAL``

       -  input is not a valid attribute setting.
