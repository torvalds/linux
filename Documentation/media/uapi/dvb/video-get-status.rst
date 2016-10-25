.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_GET_STATUS:

================
VIDEO_GET_STATUS
================

Name
----

VIDEO_GET_STATUS

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_GET_STATUS, struct video_status *status)
    :name: VIDEO_GET_STATUS


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

       -  Equals VIDEO_GET_STATUS for this command.

    -  .. row 3

       -  struct video_status \*status

       -  Returns the current status of the Video Device.


Description
-----------

This ioctl call asks the Video Device to return the current status of
the device.

.. c:type:: video_status

.. code-block:: c

	struct video_status {
		int                   video_blank;   /* blank video on freeze? */
		video_play_state_t    play_state;    /* current state of playback */
		video_stream_source_t stream_source; /* current source (demux/memory) */
		video_format_t        video_format;  /* current aspect ratio of stream*/
		video_displayformat_t display_format;/* selected cropping mode */
	};

Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
