.. SPDX-License-Identifier: GPL-2.0

The VPBE V4L2 driver design
===========================

File partitioning
-----------------

 V4L2 display device driver
         drivers/media/platform/davinci/vpbe_display.c
         drivers/media/platform/davinci/vpbe_display.h

 VPBE display controller
         drivers/media/platform/davinci/vpbe.c
         drivers/media/platform/davinci/vpbe.h

 VPBE venc sub device driver
         drivers/media/platform/davinci/vpbe_venc.c
         drivers/media/platform/davinci/vpbe_venc.h
         drivers/media/platform/davinci/vpbe_venc_regs.h

 VPBE osd driver
         drivers/media/platform/davinci/vpbe_osd.c
         drivers/media/platform/davinci/vpbe_osd.h
         drivers/media/platform/davinci/vpbe_osd_regs.h

To be done
----------

vpbe display controller
    - Add support for external encoders.
    - add support for selecting external encoder as default at probe time.

vpbe venc sub device
    - add timings for supporting ths8200
    - add support for LogicPD LCD.

FB drivers
    - Add support for fbdev drivers.- Ready and part of subsequent patches.
