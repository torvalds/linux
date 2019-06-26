.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDEO_STILLPICTURE:

==================
VIDEO_STILLPICTURE
==================

Name
----

VIDEO_STILLPICTURE

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_STILLPICTURE, struct video_still_picture *sp)
    :name: VIDEO_STILLPICTURE


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

       -  Equals VIDEO_STILLPICTURE for this command.

    -  .. row 3

       -  struct video_still_picture \*sp

       -  Pointer to a location where an I-frame and size is stored.


Description
-----------

This ioctl call asks the Video Device to display a still picture
(I-frame). The input data shall contain an I-frame. If the pointer is
NULL, then the current displayed still picture is blanked.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
