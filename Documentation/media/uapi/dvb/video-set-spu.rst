.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_SET_SPU:

=============
VIDEO_SET_SPU
=============

Name
----

VIDEO_SET_SPU

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_SET_SPU , video_spu_t *spu)
    :name: VIDEO_SET_SPU


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

       -  Equals VIDEO_SET_SPU for this command.

    -  .. row 3

       -  video_spu_t \*spu

       -  SPU decoding (de)activation and subid setting according to section
	  ??.


Description
-----------

This ioctl activates or deactivates SPU decoding in a DVD input stream.
It can only be used, if the driver is able to handle a DVD stream.


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

       -  input is not a valid spu setting or driver cannot handle SPU.
