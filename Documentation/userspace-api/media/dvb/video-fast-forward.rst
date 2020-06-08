.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDEO_FAST_FORWARD:

==================
VIDEO_FAST_FORWARD
==================

Name
----

VIDEO_FAST_FORWARD

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_FAST_FORWARD, int nFrames)
    :name: VIDEO_FAST_FORWARD


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

       -  Equals VIDEO_FAST_FORWARD for this command.

    -  .. row 3

       -  int nFrames

       -  The number of frames to skip.


Description
-----------

This ioctl call asks the Video Device to skip decoding of N number of
I-frames. This call can only be used if VIDEO_SOURCE_MEMORY is
selected.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EPERM``

       -  Mode VIDEO_SOURCE_MEMORY not selected.
