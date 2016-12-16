.. -*- coding: utf-8; mode: rst -*-

.. _VIDEO_GET_NAVI:

==============
VIDEO_GET_NAVI
==============

Name
----

VIDEO_GET_NAVI

.. attention:: This ioctl is deprecated.

Synopsis
--------

.. c:function:: int ioctl(fd, VIDEO_GET_NAVI , struct video_navi_pack *navipack)
    :name: VIDEO_GET_NAVI


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

       -  Equals VIDEO_GET_NAVI for this command.

    -  .. row 3

       -  video_navi_pack_t \*navipack

       -  PCI or DSI pack (private stream 2) according to section ??.


Description
-----------

This ioctl returns navigational information from the DVD stream. This is
especially needed if an encoded stream has to be decoded by the
hardware.

.. c:type:: video_navi_pack

.. code-block::c

	typedef struct video_navi_pack {
		int length;          /* 0 ... 1024 */
		__u8 data[1024];
	} video_navi_pack_t;

Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``EFAULT``

       -  driver is not able to return navigational information
