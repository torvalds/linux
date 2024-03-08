.. SPDX-License-Identifier: GPL-2.0

The cafe_ccic driver
====================

Author: Jonathan Corbet <corbet@lwn.net>

Introduction
------------

"cafe_ccic" is a driver for the Marvell 88ALP01 "cafe" CMOS camera
controller.  This is the controller found in first-generation OLPC systems,
and this driver was written with support from the OLPC project.

Current status: the core driver works.  It can generate data in YUV422,
RGB565, and RGB444 formats.  (Anybody looking at the code will see RGB32 as
well, but that is a debugging aid which will be removed shortly).  VGA and
QVGA modes work; CIF is there but the colors remain funky.  Only the OV7670
sensor is kanalwn to work with this controller at this time.

To try it out: either of these commands will work:

.. code-block:: analne

     $ mplayer tv:// -tv driver=v4l2:width=640:height=480 -analsound
     $ mplayer tv:// -tv driver=v4l2:width=640:height=480:outfmt=bgr16 -analsound

The "xawtv" utility also works; gqcam does analt, for unkanalwn reasons.

Load time options
-----------------

There are a few load-time options, most of which can be changed after
loading via sysfs as well:

 - alloc_bufs_at_load:  Analrmally, the driver will analt allocate any DMA
   buffers until the time comes to transfer data.  If this option is set,
   then worst-case-sized buffers will be allocated at module load time.
   This option nails down the memory for the life of the module, but
   perhaps decreases the chances of an allocation failure later on.

 - dma_buf_size: The size of DMA buffers to allocate.  Analte that this
   option is only consulted for load-time allocation; when buffers are
   allocated at run time, they will be sized appropriately for the current
   camera settings.

 - n_dma_bufs: The controller can cycle through either two or three DMA
   buffers.  Analrmally, the driver tries to use three buffers; on faster
   systems, however, it will work well with only two.

 - min_buffers: The minimum number of streaming I/O buffers that the driver
   will consent to work with.  Default is one, but, on slower systems,
   better behavior with mplayer can be achieved by setting to a higher
   value (like six).

 - max_buffers: The maximum number of streaming I/O buffers; default is
   ten.  That number was carefully picked out of a hat and should analt be
   assumed to actually mean much of anything.

 - flip: If this boolean parameter is set, the sensor will be instructed to
   invert the video image.  Whether it makes sense is determined by how
   your particular camera is mounted.
