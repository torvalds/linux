.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDIOC_DECODER_CMD:

************************************************
ioctl VIDIOC_DECODER_CMD, VIDIOC_TRY_DECODER_CMD
************************************************

Name
====

VIDIOC_DECODER_CMD - VIDIOC_TRY_DECODER_CMD - Execute an decoder command


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_DECODER_CMD, struct v4l2_decoder_cmd *argp )
    :name: VIDIOC_DECODER_CMD


.. c:function:: int ioctl( int fd, VIDIOC_TRY_DECODER_CMD, struct v4l2_decoder_cmd *argp )
    :name: VIDIOC_TRY_DECODER_CMD


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    pointer to struct :c:type:`v4l2_decoder_cmd`.


Description
===========

These ioctls control an audio/video (usually MPEG-) decoder.
``VIDIOC_DECODER_CMD`` sends a command to the decoder,
``VIDIOC_TRY_DECODER_CMD`` can be used to try a command without actually
executing it. To send a command applications must initialize all fields
of a struct :c:type:`v4l2_decoder_cmd` and call
``VIDIOC_DECODER_CMD`` or ``VIDIOC_TRY_DECODER_CMD`` with a pointer to
this structure.

The ``cmd`` field must contain the command code. Some commands use the
``flags`` field for additional information.

A :ref:`write() <func-write>` or :ref:`VIDIOC_STREAMON`
call sends an implicit START command to the decoder if it has not been
started yet.

A :ref:`close() <func-close>` or :ref:`VIDIOC_STREAMOFF <VIDIOC_STREAMON>`
call of a streaming file descriptor sends an implicit immediate STOP
command to the decoder, and all buffered data is discarded.

These ioctls are optional, not all drivers may support them. They were
introduced in Linux 3.3.


.. tabularcolumns:: |p{1.1cm}|p{2.4cm}|p{1.2cm}|p{1.6cm}|p{10.6cm}|

.. c:type:: v4l2_decoder_cmd

.. cssclass:: longtable

.. flat-table:: struct v4l2_decoder_cmd
    :header-rows:  0
    :stub-columns: 0
    :widths: 11 24 12 16 106

    * - __u32
      - ``cmd``
      -
      -
      - The decoder command, see :ref:`decoder-cmds`.
    * - __u32
      - ``flags``
      -
      -
      - Flags to go with the command. If no flags are defined for this
	command, drivers and applications must set this field to zero.
    * - union
      - (anonymous)
      -
      -
      -
    * -
      - struct
      - ``start``
      -
      - Structure containing additional data for the
	``V4L2_DEC_CMD_START`` command.
    * -
      -
      - __s32
      - ``speed``
      - Playback speed and direction. The playback speed is defined as
	``speed``/1000 of the normal speed. So 1000 is normal playback.
	Negative numbers denote reverse playback, so -1000 does reverse
	playback at normal speed. Speeds -1, 0 and 1 have special
	meanings: speed 0 is shorthand for 1000 (normal playback). A speed
	of 1 steps just one frame forward, a speed of -1 steps just one
	frame back.
    * -
      -
      - __u32
      - ``format``
      - Format restrictions. This field is set by the driver, not the
	application. Possible values are ``V4L2_DEC_START_FMT_NONE`` if
	there are no format restrictions or ``V4L2_DEC_START_FMT_GOP`` if
	the decoder operates on full GOPs (*Group Of Pictures*). This is
	usually the case for reverse playback: the decoder needs full
	GOPs, which it can then play in reverse order. So to implement
	reverse playback the application must feed the decoder the last
	GOP in the video file, then the GOP before that, etc. etc.
    * -
      - struct
      - ``stop``
      -
      - Structure containing additional data for the ``V4L2_DEC_CMD_STOP``
	command.
    * -
      -
      - __u64
      - ``pts``
      - Stop playback at this ``pts`` or immediately if the playback is
	already past that timestamp. Leave to 0 if you want to stop after
	the last frame was decoded.
    * -
      - struct
      - ``raw``
      -
      -
    * -
      -
      - __u32
      - ``data``\ [16]
      - Reserved for future extensions. Drivers and applications must set
	the array to zero.



.. tabularcolumns:: |p{5.6cm}|p{0.6cm}|p{11.3cm}|

.. _decoder-cmds:

.. flat-table:: Decoder Commands
    :header-rows:  0
    :stub-columns: 0
    :widths: 56 6 113

    * - ``V4L2_DEC_CMD_START``
      - 0
      - Start the decoder. When the decoder is already running or paused,
	this command will just change the playback speed. That means that
	calling ``V4L2_DEC_CMD_START`` when the decoder was paused will
	*not* resume the decoder. You have to explicitly call
	``V4L2_DEC_CMD_RESUME`` for that. This command has one flag:
	``V4L2_DEC_CMD_START_MUTE_AUDIO``. If set, then audio will be
	muted when playing back at a non-standard speed.
    * - ``V4L2_DEC_CMD_STOP``
      - 1
      - Stop the decoder. When the decoder is already stopped, this
	command does nothing. This command has two flags: if
	``V4L2_DEC_CMD_STOP_TO_BLACK`` is set, then the decoder will set
	the picture to black after it stopped decoding. Otherwise the last
	image will repeat. mem2mem decoders will stop producing new frames
	altogether. They will send a ``V4L2_EVENT_EOS`` event when the
	last frame has been decoded and all frames are ready to be
	dequeued and will set the ``V4L2_BUF_FLAG_LAST`` buffer flag on
	the last buffer of the capture queue to indicate there will be no
	new buffers produced to dequeue. This buffer may be empty,
	indicated by the driver setting the ``bytesused`` field to 0. Once
	the ``V4L2_BUF_FLAG_LAST`` flag was set, the
	:ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctl will not block anymore,
	but return an ``EPIPE`` error code. If
	``V4L2_DEC_CMD_STOP_IMMEDIATELY`` is set, then the decoder stops
	immediately (ignoring the ``pts`` value), otherwise it will keep
	decoding until timestamp >= pts or until the last of the pending
	data from its internal buffers was decoded.
    * - ``V4L2_DEC_CMD_PAUSE``
      - 2
      - Pause the decoder. When the decoder has not been started yet, the
	driver will return an ``EPERM`` error code. When the decoder is
	already paused, this command does nothing. This command has one
	flag: if ``V4L2_DEC_CMD_PAUSE_TO_BLACK`` is set, then set the
	decoder output to black when paused.
    * - ``V4L2_DEC_CMD_RESUME``
      - 3
      - Resume decoding after a PAUSE command. When the decoder has not
	been started yet, the driver will return an ``EPERM`` error code. When
	the decoder is already running, this command does nothing. No
	flags are defined for this command.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The ``cmd`` field is invalid.

EPERM
    The application sent a PAUSE or RESUME command when the decoder was
    not running.
