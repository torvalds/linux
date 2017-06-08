.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_HIGHLIGHT:

===================
VIDEO_SET_HIGHLIGHT
===================

Name
----

VIDEO_SET_HIGHLIGHT

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_SET_HIGHLIGHT, struct video_highlight *vhilite)
    :name: VIDEO_SET_HIGHLIGHT


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

       -  Equals VIDEO_SET_HIGHLIGHT for this command.

    -  .. row 3

       -  video_highlight_t \*vhilite

       -  SPU Highlight information according to section ??.


Description
-----------

This ioctl sets the SPU highlight information for the menu access of a
DVD.

.. c:type:: video_highlight

.. code-block:: c

	typedef
	struct video_highlight {
		int     active;      /*    1=show highlight, 0=hide highlight */
		__u8    contrast1;   /*    7- 4  Pattern pixel contrast */
				/*    3- 0  Background pixel contrast */
		__u8    contrast2;   /*    7- 4  Emphasis pixel-2 contrast */
				/*    3- 0  Emphasis pixel-1 contrast */
		__u8    color1;      /*    7- 4  Pattern pixel color */
				/*    3- 0  Background pixel color */
		__u8    color2;      /*    7- 4  Emphasis pixel-2 color */
				/*    3- 0  Emphasis pixel-1 color */
		__u32    ypos;       /*   23-22  auto action mode */
				/*   21-12  start y */
				/*    9- 0  end y */
		__u32    xpos;       /*   23-22  button color number */
				/*   21-12  start x */
				/*    9- 0  end x */
	} video_highlight_t;



Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
