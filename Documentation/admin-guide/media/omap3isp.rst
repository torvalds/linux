.. SPDX-License-Identifier: GPL-2.0

.. include:: <isonum.txt>

OMAP 3 Image Signal Processor (ISP) driver
==========================================

Copyright |copy| 2010 Nokia Corporation

Copyright |copy| 2009 Texas Instruments, Inc.

Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>,
Sakari Ailus <sakari.ailus@iki.fi>, David Cohen <dacohen@gmail.com>


Introduction
------------

This file documents the Texas Instruments OMAP 3 Image Signal Processor (ISP)
driver located under drivers/media/platform/omap3isp. The original driver was
written by Texas Instruments but since that it has been rewritten (twice) at
Nokia.

The driver has been successfully used on the following versions of OMAP 3:

- 3430
- 3530
- 3630

The driver implements V4L2, Media controller and v4l2_subdev interfaces.
Sensor, lens and flash drivers using the v4l2_subdev interface in the kernel
are supported.


Split to subdevs
----------------

The OMAP 3 ISP is split into V4L2 subdevs, each of the blocks inside the ISP
having one subdev to represent it. Each of the subdevs provide a V4L2 subdev
interface to userspace.

- OMAP3 ISP CCP2
- OMAP3 ISP CSI2a
- OMAP3 ISP CCDC
- OMAP3 ISP preview
- OMAP3 ISP resizer
- OMAP3 ISP AEWB
- OMAP3 ISP AF
- OMAP3 ISP histogram

Each possible link in the ISP is modelled by a link in the Media controller
interface. For an example program see [#]_.


Controlling the OMAP 3 ISP
--------------------------

In general, the settings given to the OMAP 3 ISP take effect at the beginning
of the following frame. This is done when the module becomes idle during the
vertical blanking period on the sensor. In memory-to-memory operation the pipe
is run one frame at a time. Applying the settings is done between the frames.

All the blocks in the ISP, excluding the CSI-2 and possibly the CCP2 receiver,
insist on receiving complete frames. Sensors must thus never send the ISP
partial frames.

Autoidle does have issues with some ISP blocks on the 3430, at least.
Autoidle is only enabled on 3630 when the omap3isp module parameter autoidle
is non-zero.

Technical reference manuals (TRMs) and other documentation
----------------------------------------------------------

OMAP 3430 TRM:
<URL:http://focus.ti.com/pdfs/wtbu/OMAP34xx_ES3.1.x_PUBLIC_TRM_vZM.zip>
Referenced 2011-03-05.

OMAP 35xx TRM:
<URL:http://www.ti.com/litv/pdf/spruf98o> Referenced 2011-03-05.

OMAP 3630 TRM:
<URL:http://focus.ti.com/pdfs/wtbu/OMAP36xx_ES1.x_PUBLIC_TRM_vQ.zip>
Referenced 2011-03-05.

DM 3730 TRM:
<URL:http://www.ti.com/litv/pdf/sprugn4h> Referenced 2011-03-06.


References
----------

.. [#] http://git.ideasonboard.org/?p=media-ctl.git;a=summary
