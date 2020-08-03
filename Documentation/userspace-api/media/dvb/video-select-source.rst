.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDEO_SELECT_SOURCE:

===================
VIDEO_SELECT_SOURCE
===================

Name
----

VIDEO_SELECT_SOURCE

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_SELECT_SOURCE, video_stream_source_t source)
    :name: VIDEO_SELECT_SOURCE


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

       -  Equals VIDEO_SELECT_SOURCE for this command.

    -  .. row 3

       -  video_stream_source_t source

       -  Indicates which source shall be used for the Video stream.


Description
-----------

This ioctl is for Digital TV devices only. This ioctl was also supported by the
V4L2 ivtv driver, but that has been replaced by the ivtv-specific
``IVTV_IOC_PASSTHROUGH_MODE`` ioctl.

This ioctl call informs the video device which source shall be used for
the input data. The possible sources are demux or memory. If memory is
selected, the data is fed to the video device through the write command.

.. c:type:: video_stream_source_t

.. code-block:: c

	typedef enum {
		VIDEO_SOURCE_DEMUX, /* Select the demux as the main source */
		VIDEO_SOURCE_MEMORY /* If this source is selected, the stream
				comes from the user through the write
				system call */
	} video_stream_source_t;

Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
