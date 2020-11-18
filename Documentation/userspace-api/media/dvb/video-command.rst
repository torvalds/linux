.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDEO_COMMAND:

=============
VIDEO_COMMAND
=============

Name
----

VIDEO_COMMAND

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(int fd, VIDEO_COMMAND, struct video_command *cmd)
    :name: VIDEO_COMMAND


Arguments
---------

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


Description
-----------

This ioctl is obsolete. Do not use in new drivers. For V4L2 decoders
this ioctl has been replaced by the
:ref:`VIDIOC_DECODER_CMD` ioctl.

This ioctl commands the decoder. The ``video_command`` struct is a
subset of the ``v4l2_decoder_cmd`` struct, so refer to the
:ref:`VIDIOC_DECODER_CMD` documentation for
more information.

.. c:type:: struct video_command

.. code-block:: c

	/* The structure must be zeroed before use by the application
	This ensures it can be extended safely in the future. */
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
				>1: playback at speed/1000 of the normal speed,
				<-1: reverse playback at (-speed/1000) of the normal speed. */
				__s32 speed;
				__u32 format;
			} play;

			struct {
				__u32 data[16];
			} raw;
		};
	};


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
