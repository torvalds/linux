.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_ATTRIBUTES:

====================
VIDEO_SET_ATTRIBUTES
====================

Name
----

VIDEO_SET_ATTRIBUTES

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_SET_ATTRIBUTE ,video_attributes_t vattr)
    :name: VIDEO_SET_ATTRIBUTE


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

       -  Equals VIDEO_SET_ATTRIBUTE for this command.

    -  .. row 3

       -  video_attributes_t vattr

       -  video attributes according to section ??.


Description
-----------

This ioctl is intended for DVD playback and allows you to set certain
information about the stream. Some hardware may not need this
information, but the call also tells the hardware to prepare for DVD
playback.

.. c:type:: video_attributes_t

.. code-block::c

	typedef __u16 video_attributes_t;
	/*   bits: descr. */
	/*   15-14 Video compression mode (0=MPEG-1, 1=MPEG-2) */
	/*   13-12 TV system (0=525/60, 1=625/50) */
	/*   11-10 Aspect ratio (0=4:3, 3=16:9) */
	/*    9- 8 permitted display mode on 4:3 monitor (0=both, 1=only pan-sca */
	/*    7    line 21-1 data present in GOP (1=yes, 0=no) */
	/*    6    line 21-2 data present in GOP (1=yes, 0=no) */
	/*    5- 3 source resolution (0=720x480/576, 1=704x480/576, 2=352x480/57 */
	/*    2    source letterboxed (1=yes, 0=no) */
	/*    0    film/camera mode (0=camera, 1=film (625/50 only)) */


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
