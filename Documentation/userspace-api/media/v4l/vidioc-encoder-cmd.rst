.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_ENCODER_CMD:

************************************************
ioctl VIDIOC_ENCODER_CMD, VIDIOC_TRY_ENCODER_CMD
************************************************

Name
====

VIDIOC_ENCODER_CMD - VIDIOC_TRY_ENCODER_CMD - Execute an encoder command

Synopsis
========

.. c:macro:: VIDIOC_ENCODER_CMD

``int ioctl(int fd, VIDIOC_ENCODER_CMD, struct v4l2_encoder_cmd *argp)``

.. c:macro:: VIDIOC_TRY_ENCODER_CMD

``int ioctl(int fd, VIDIOC_TRY_ENCODER_CMD, struct v4l2_encoder_cmd *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

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

The ``cmd`` field must contain the command code. Some commands use the
``flags`` field for additional information.

After a STOP command, :c:func:`read()` calls will read
the remaining data buffered by the driver. When the buffer is empty,
:c:func:`read()` will return zero and the next :c:func:`read()`
call will restart the encoder.

A :c:func:`read()` or :ref:`VIDIOC_STREAMON <VIDIOC_STREAMON>`
call sends an implicit START command to the encoder if it has not been
started yet. Applies to both queues of mem2mem encoders.

A :c:func:`close()` or :ref:`VIDIOC_STREAMOFF <VIDIOC_STREAMON>`
call of a streaming file descriptor sends an implicit immediate STOP to
the encoder, and all buffered data is discarded. Applies to both queues of
mem2mem encoders.

These ioctls are optional, not all drivers may support them. They were
introduced in Linux 2.6.21. They are, however, mandatory for stateful mem2mem
encoders (as further documented in :ref:`encoder`).

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.5cm}|

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


.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.5cm}|

.. _encoder-cmds:

.. flat-table:: Encoder Commands
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_ENC_CMD_START``
      - 0
      - Start the encoder. When the encoder is already running or paused,
	this command does nothing. No flags are defined for this command.

	For a device implementing the :ref:`encoder`, once the drain sequence
	is initiated with the ``V4L2_ENC_CMD_STOP`` command, it must be driven
	to completion before this command can be invoked.  Any attempt to
	invoke the command while the drain sequence is in progress will trigger
	an ``EBUSY`` error code. See :ref:`encoder` for more details.
    * - ``V4L2_ENC_CMD_STOP``
      - 1
      - Stop the encoder. When the ``V4L2_ENC_CMD_STOP_AT_GOP_END`` flag
	is set, encoding will continue until the end of the current *Group
	Of Pictures*, otherwise encoding will stop immediately. When the
	encoder is already stopped, this command does nothing.

	For a device implementing the :ref:`encoder`, the command will initiate
	the drain sequence as documented in :ref:`encoder`. No flags or other
	arguments are accepted in this case. Any attempt to invoke the command
	again before the sequence completes will trigger an ``EBUSY`` error
	code.
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

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.5cm}|

.. _encoder-flags:

.. flat-table:: Encoder Command Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_ENC_CMD_STOP_AT_GOP_END``
      - 0x0001
      - Stop encoding at the end of the current *Group Of Pictures*,
	rather than immediately.

        Does not apply to :ref:`encoder`.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EBUSY
    A drain sequence of a device implementing the :ref:`encoder` is still in
    progress. It is not allowed to issue another encoder command until it
    completes.

EINVAL
    The ``cmd`` field is invalid.

EPERM
    The application sent a PAUSE or RESUME command when the encoder was
    not running.
