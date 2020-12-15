.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDEO_SET_BLANK:

===============
VIDEO_SET_BLANK
===============

Name
----

VIDEO_SET_BLANK

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_SET_BLANK, boolean mode)
    :name: VIDEO_SET_BLANK


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

       -  Equals VIDEO_SET_BLANK for this command.

    -  .. row 3

       -  boolean mode

       -  TRUE: Blank screen when stop.

    -  .. row 4

       -
       -  FALSE: Show last decoded frame.


Description
-----------

This ioctl call asks the Video Device to blank out the picture.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
