.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDEO_GET_SIZE:

==============
VIDEO_GET_SIZE
==============

Name
----

VIDEO_GET_SIZE

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(int fd, VIDEO_GET_SIZE, video_size_t *size)
    :name: VIDEO_GET_SIZE


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

       -  Equals VIDEO_GET_SIZE for this command.

    -  .. row 3

       -  video_size_t \*size

       -  Returns the size and aspect ratio.


Description
-----------

This ioctl returns the size and aspect ratio.

.. c:type:: video_size_t

.. code-block::c

	typedef struct {
		int w;
		int h;
		video_format_t aspect_ratio;
	} video_size_t;


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
