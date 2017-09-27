.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_ENCODER_CMD:

************************************************
ioctl VIDIOC_ENCODER_CMD, VIDIOC_TRY_ENCODER_CMD
************************************************

Name
====

VIDIOC_ENCODER_CMD - VIDIOC_TRY_ENCODER_CMD - Execute an encoder command


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_ENCODER_CMD, struct v4l2_encoder_cmd *argp )
    :name: VIDIOC_ENCODER_CMD

.. c:function:: int ioctl( int fd, VIDIOC_TRY_ENCODER_CMD, struct v4l2_encoder_cmd *argp )
    :name: VIDIOC_TRY_ENCODER_CMD


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_encoder_cmd`.

Description
===========

These ioctls control an audio/video (usually MPEG-) encoder.
``VIDIOC_ENCODER_CMD`` sends a command to the encoder,
``VIDIOC_TRY_ENCODER_CMD`` can be used to try a command without actually
executing it.

To send a command applications must initialize all fields of a struct
:c:type:`v4l2_encoder_cmd` and call
``VIDIOC_ENCODER_CMD`` or ``VIDIOC_TRY_ENCODER_CMD`` with a pointer to
this structure.

The ``cmd`` field must contain the command code. The ``flags`` field is
currently only used by the STOP command and contains one bit: If the
``V4L2_ENC_CMD_STOP_AT_GOP_END`` flag is set, encoding will continue
until the end of the current *Group Of Pictures*, otherwise it will stop
immediately.

A :ref:`read() <func-read>` or :ref:`VIDIOC_STREAMON <VIDIOC_STREAMON>`
call sends an implicit START command to the encoder if it has not been
started yet. After a STOP command, :ref:`read() <func-read>` calls will read
the remaining data buffered by the driver. When the buffer is empty,
:ref:`read() <func-read>` will return zero and the next :ref:`read() <func-read>`
call will restart the encoder.

A :ref:`close() <func-close>` or :ref:`VIDIOC_STREAMOFF <VIDIOC_STREAMON>`
call of a streaming file descriptor sends an implicit immediate STOP to
the encoder, and all buffered data is discarded.

These ioctls are optional, not all drivers may support them. They were
introduced in Linux 2.6.21.


.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. c:type:: v4l2_encoder_cmd

.. flat-table:: struct v4l2_encoder_cmd
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``cmd``
      - The encoder command, see :ref:`encoder-cmds`.
    * - __u32
      - ``flags``
      - Flags to go with the command, see :ref:`encoder-flags`. If no
	flags are defined for this command, drivers and applications must
	set this field to zero.
    * - __u32
      - ``data``\ [8]
      - Reserved for future extensions. Drivers and applications must set
	the array to zero.



.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _encoder-cmds:

.. flat-table:: Encoder Commands
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_ENC_CMD_START``
      - 0
      - Start the encoder. When the encoder is already running or paused,
	this command does nothing. No flags are defined for this command.
    * - ``V4L2_ENC_CMD_STOP``
      - 1
      - Stop the encoder. When the ``V4L2_ENC_CMD_STOP_AT_GOP_END`` flag
	is set, encoding will continue until the end of the current *Group
	Of Pictures*, otherwise encoding will stop immediately. When the
	encoder is already stopped, this command does nothing. mem2mem
	encoders will send a ``V4L2_EVENT_EOS`` event when the last frame
	has been encoded and all frames are ready to be dequeued and will
	set the ``V4L2_BUF_FLAG_LAST`` buffer flag on the last buffer of
	the capture queue to indicate there will be no new buffers
	produced to dequeue. This buffer may be empty, indicated by the
	driver setting the ``bytesused`` field to 0. Once the
	``V4L2_BUF_FLAG_LAST`` flag was set, the
	:ref:`VIDIOC_DQBUF <VIDIOC_QBUF>` ioctl will not block anymore,
	but return an ``EPIPE`` error code.
    * - ``V4L2_ENC_CMD_PAUSE``
      - 2
      - Pause the encoder. When the encoder has not been started yet, the
	driver will return an ``EPERM`` error code. When the encoder is
	already paused, this command does nothing. No flags are defined
	for this command.
    * - ``V4L2_ENC_CMD_RESUME``
      - 3
      - Resume encoding after a PAUSE command. When the encoder has not
	been started yet, the driver will return an ``EPERM`` error code. When
	the encoder is already running, this command does nothing. No
	flags are defined for this command.


.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. _encoder-flags:

.. flat-table:: Encoder Command Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_ENC_CMD_STOP_AT_GOP_END``
      - 0x0001
      - Stop encoding at the end of the current *Group Of Pictures*,
	rather than immediately.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The ``cmd`` field is invalid.

EPERM
    The application sent a PAUSE or RESUME command when the encoder was
    not running.
