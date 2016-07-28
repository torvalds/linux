OMAP4 ISS Driver
================

Author: Sergio Aguirre <sergio.a.aguirre@gmail.com>

Copyright (C) 2012, Texas Instruments

Introduction
------------

The OMAP44XX family of chips contains the Imaging SubSystem (a.k.a. ISS),
Which contains several components that can be categorized in 3 big groups:

- Interfaces (2 Interfaces: CSI2-A & CSI2-B/CCP2)
- ISP (Image Signal Processor)
- SIMCOP (Still Image Coprocessor)

For more information, please look in [#f1]_ for latest version of:
"OMAP4430 Multimedia Device Silicon Revision 2.x"

As of Revision AB, the ISS is described in detail in section 8.

This driver is supporting **only** the CSI2-A/B interfaces for now.

It makes use of the Media Controller framework [#f2]_, and inherited most of the
code from OMAP3 ISP driver (found under drivers/media/platform/omap3isp/\*),
except that it doesn't need an IOMMU now for ISS buffers memory mapping.

Supports usage of MMAP buffers only (for now).

Tested platforms
----------------

- OMAP4430SDP, w/ ES2.1 GP & SEVM4430-CAM-V1-0 (Contains IMX060 & OV5640, in
  which only the last one is supported, outputting YUV422 frames).

- TI Blaze MDP, w/ OMAP4430 ES2.2 EMU (Contains 1 IMX060 & 2 OV5650 sensors, in
  which only the OV5650 are supported, outputting RAW10 frames).

- PandaBoard, Rev. A2, w/ OMAP4430 ES2.1 GP & OV adapter board, tested with
  following sensors:
  * OV5640
  * OV5650

- Tested on mainline kernel:

	http://git.kernel.org/?p=linux/kernel/git/torvalds/linux.git;a=summary

  Tag: v3.3 (commit c16fa4f2ad19908a47c63d8fa436a1178438c7e7)

File list
---------
drivers/staging/media/omap4iss/
include/linux/platform_data/media/omap4iss.h

References
----------

.. [#f1] http://focus.ti.com/general/docs/wtbu/wtbudocumentcenter.tsp?navigationId=12037&templateId=6123#62
.. [#f2] http://lwn.net/Articles/420485/
