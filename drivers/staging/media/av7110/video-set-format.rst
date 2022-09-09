.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.video

.. _VIDEO_SET_FORMAT:

================
VIDEO_SET_FORMAT
================

Name
----

VIDEO_SET_FORMAT

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:macro:: VIDEO_SET_FORMAT

``int ioctl(fd, VIDEO_SET_FORMAT, video_format_t format)``

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

       -  Equals VIDEO_SET_FORMAT for this command.

    -  .. row 3

       -  video_format_t format

       -  video format of TV as defined in section ??.

Description
-----------

This ioctl sets the screen format (aspect ratio) of the connected output
device (TV) so that the output of the decoder can be adjusted
accordingly.

.. c:type:: video_format_t

.. code-block:: c

	typedef enum {
		VIDEO_FORMAT_4_3,     /* Select 4:3 format */
		VIDEO_FORMAT_16_9,    /* Select 16:9 format. */
		VIDEO_FORMAT_221_1    /* 2.21:1 */
	} video_format_t;

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
