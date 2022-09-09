.. SPDX-License-Identifier: GPL-2.0

i.MX7 Video Capture Driver
==========================

Introduction
------------

The i.MX7 contrary to the i.MX5/6 family does not contain an Image Processing
Unit (IPU); because of that the capabilities to perform operations or
manipulation of the capture frames are less feature rich.

For image capture the i.MX7 has three units:
- CMOS Sensor Interface (CSI)
- Video Multiplexer
- MIPI CSI-2 Receiver

.. code-block:: none

   MIPI Camera Input ---> MIPI CSI-2 --- > |\
                                           | \
                                           |  \
                                           | M |
                                           | U | ------>  CSI ---> Capture
                                           | X |
                                           |  /
   Parallel Camera Input ----------------> | /
                                           |/

For additional information, please refer to the latest versions of the i.MX7
reference manual [#f1]_.

Entities
--------

imx-mipi-csi2
--------------

This is the MIPI CSI-2 receiver entity. It has one sink pad to receive the pixel
data from MIPI CSI-2 camera sensor. It has one source pad, corresponding to the
virtual channel 0. This module is compliant to previous version of Samsung
D-phy, and supports two D-PHY Rx Data lanes.

csi-mux
-------

This is the video multiplexer. It has two sink pads to select from either camera
sensor with a parallel interface or from MIPI CSI-2 virtual channel 0.  It has
a single source pad that routes to the CSI.

csi
---

The CSI enables the chip to connect directly to external CMOS image sensor. CSI
can interface directly with Parallel and MIPI CSI-2 buses. It has 256 x 64 FIFO
to store received image pixel data and embedded DMA controllers to transfer data
from the FIFO through AHB bus.

This entity has one sink pad that receives from the csi-mux entity and a single
source pad that routes video frames directly to memory buffers. This pad is
routed to a capture device node.

Usage Notes
-----------

To aid in configuration and for backward compatibility with V4L2 applications
that access controls only from video device nodes, the capture device interfaces
inherit controls from the active entities in the current pipeline, so controls
can be accessed either directly from the subdev or from the active capture
device interface. For example, the sensor controls are available either from the
sensor subdevs or from the active capture device.

Warp7 with OV2680
-----------------

On this platform an OV2680 MIPI CSI-2 module is connected to the internal MIPI
CSI-2 receiver. The following example configures a video capture pipeline with
an output of 800x600, and BGGR 10 bit bayer format:

.. code-block:: none

   # Setup links
   media-ctl -l "'ov2680 1-0036':0 -> 'imx7-mipi-csis.0':0[1]"
   media-ctl -l "'imx7-mipi-csis.0':1 -> 'csi-mux':1[1]"
   media-ctl -l "'csi-mux':2 -> 'csi':0[1]"
   media-ctl -l "'csi':1 -> 'csi capture':0[1]"

   # Configure pads for pipeline
   media-ctl -V "'ov2680 1-0036':0 [fmt:SBGGR10_1X10/800x600 field:none]"
   media-ctl -V "'csi-mux':1 [fmt:SBGGR10_1X10/800x600 field:none]"
   media-ctl -V "'csi-mux':2 [fmt:SBGGR10_1X10/800x600 field:none]"
   media-ctl -V "'imx7-mipi-csis.0':0 [fmt:SBGGR10_1X10/800x600 field:none]"
   media-ctl -V "'csi':0 [fmt:SBGGR10_1X10/800x600 field:none]"

After this streaming can start. The v4l2-ctl tool can be used to select any of
the resolutions supported by the sensor.

.. code-block:: none

	# media-ctl -p
	Media controller API version 5.2.0

	Media device information
	------------------------
	driver          imx7-csi
	model           imx-media
	serial
	bus info
	hw revision     0x0
	driver version  5.2.0

	Device topology
	- entity 1: csi (2 pads, 2 links)
	            type V4L2 subdev subtype Unknown flags 0
	            device node name /dev/v4l-subdev0
	        pad0: Sink
	                [fmt:SBGGR10_1X10/800x600 field:none colorspace:srgb xfer:srgb ycbcr:601 quantization:full-range]
	                <- "csi-mux":2 [ENABLED]
	        pad1: Source
	                [fmt:SBGGR10_1X10/800x600 field:none colorspace:srgb xfer:srgb ycbcr:601 quantization:full-range]
	                -> "csi capture":0 [ENABLED]

	- entity 4: csi capture (1 pad, 1 link)
	            type Node subtype V4L flags 0
	            device node name /dev/video0
	        pad0: Sink
	                <- "csi":1 [ENABLED]

	- entity 10: csi-mux (3 pads, 2 links)
	             type V4L2 subdev subtype Unknown flags 0
	             device node name /dev/v4l-subdev1
	        pad0: Sink
	                [fmt:Y8_1X8/1x1 field:none]
	        pad1: Sink
	               [fmt:SBGGR10_1X10/800x600 field:none]
	                <- "imx7-mipi-csis.0":1 [ENABLED]
	        pad2: Source
	                [fmt:SBGGR10_1X10/800x600 field:none]
	                -> "csi":0 [ENABLED]

	- entity 14: imx7-mipi-csis.0 (2 pads, 2 links)
	             type V4L2 subdev subtype Unknown flags 0
	             device node name /dev/v4l-subdev2
	        pad0: Sink
	                [fmt:SBGGR10_1X10/800x600 field:none]
	                <- "ov2680 1-0036":0 [ENABLED]
	        pad1: Source
	                [fmt:SBGGR10_1X10/800x600 field:none]
	                -> "csi-mux":1 [ENABLED]

	- entity 17: ov2680 1-0036 (1 pad, 1 link)
	             type V4L2 subdev subtype Sensor flags 0
	             device node name /dev/v4l-subdev3
	        pad0: Source
	                [fmt:SBGGR10_1X10/800x600@1/30 field:none colorspace:srgb]
	                -> "imx7-mipi-csis.0":0 [ENABLED]

i.MX6ULL-EVK with OV5640
------------------------

On this platform a parallel OV5640 sensor is connected to the CSI port.
The following example configures a video capture pipeline with an output
of 640x480 and UYVY8_2X8 format:

.. code-block:: none

   # Setup links
   media-ctl -l "'ov5640 1-003c':0 -> 'csi':0[1]"
   media-ctl -l "'csi':1 -> 'csi capture':0[1]"

   # Configure pads for pipeline
   media-ctl -v -V "'ov5640 1-003c':0 [fmt:UYVY8_2X8/640x480 field:none]"

After this streaming can start:

.. code-block:: none

   gst-launch-1.0 -v v4l2src device=/dev/video1 ! video/x-raw,format=UYVY,width=640,height=480 ! v4l2convert ! fbdevsink

.. code-block:: none

	# media-ctl -p
	Media controller API version 5.14.0

	Media device information
	------------------------
	driver          imx7-csi
	model           imx-media
	serial
	bus info
	hw revision     0x0
	driver version  5.14.0

	Device topology
	- entity 1: csi (2 pads, 2 links)
	            type V4L2 subdev subtype Unknown flags 0
	            device node name /dev/v4l-subdev0
	        pad0: Sink
	                [fmt:UYVY8_2X8/640x480 field:none colorspace:srgb xfer:srgb ycbcr:601 quantization:full-range]
	                <- "ov5640 1-003c":0 [ENABLED,IMMUTABLE]
	        pad1: Source
	                [fmt:UYVY8_2X8/640x480 field:none colorspace:srgb xfer:srgb ycbcr:601 quantization:full-range]
	                -> "csi capture":0 [ENABLED,IMMUTABLE]

	- entity 4: csi capture (1 pad, 1 link)
	            type Node subtype V4L flags 0
	            device node name /dev/video1
	        pad0: Sink
	                <- "csi":1 [ENABLED,IMMUTABLE]

	- entity 10: ov5640 1-003c (1 pad, 1 link)
	             type V4L2 subdev subtype Sensor flags 0
	             device node name /dev/v4l-subdev1
	        pad0: Source
	                [fmt:UYVY8_2X8/640x480@1/30 field:none colorspace:srgb xfer:srgb ycbcr:601 quantization:full-range]
	                -> "csi":0 [ENABLED,IMMUTABLE]

References
----------

.. [#f1] https://www.nxp.com/docs/en/reference-manual/IMX7SRM.pdf
