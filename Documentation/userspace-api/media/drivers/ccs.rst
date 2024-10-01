.. SPDX-License-Identifier: GPL-2.0-only

.. include:: <isonum.txt>

MIPI CCS camera sensor driver
=============================

The MIPI CCS camera sensor driver is a generic driver for `MIPI CCS
<https://www.mipi.org/specifications/camera-command-set>`_ compliant
camera sensors. It exposes three sub-devices representing the pixel array,
the binner and the scaler.

As the capabilities of individual devices vary, the driver exposes
interfaces based on the capabilities that exist in hardware.

Pixel Array sub-device
----------------------

The pixel array sub-device represents the camera sensor's pixel matrix, as well
as analogue crop functionality present in many compliant devices. The analogue
crop is configured using the ``V4L2_SEL_TGT_CROP`` on the source pad (0) of the
entity. The size of the pixel matrix can be obtained by getting the
``V4L2_SEL_TGT_NATIVE_SIZE`` target.

Binner
------

The binner sub-device represents the binning functionality on the sensor. For
that purpose, selection target ``V4L2_SEL_TGT_COMPOSE`` is supported on the
sink pad (0).

Additionally, if a device has no scaler or digital crop functionality, the
source pad (1) expses another digital crop selection rectangle that can only
crop at the end of the lines and frames.

Scaler
------

The scaler sub-device represents the digital crop and scaling functionality of
the sensor. The V4L2 selection target ``V4L2_SEL_TGT_CROP`` is used to
configure the digital crop on the sink pad (0) when digital crop is supported.
Scaling is configured using selection target ``V4L2_SEL_TGT_COMPOSE`` on the
sink pad (0) as well.

Additionally, if the scaler sub-device exists, its source pad (1) exposes
another digital crop selection rectangle that can only crop at the end of the
lines and frames.

Digital and analogue crop
-------------------------

Digital crop functionality is referred to as cropping that effectively works by
dropping some data on the floor. Analogue crop, on the other hand, means that
the cropped information is never retrieved. In case of camera sensors, the
analogue data is never read from the pixel matrix that are outside the
configured selection rectangle that designates crop. The difference has an
effect in device timing and likely also in power consumption.

Private controls
----------------

The MIPI CCS driver implements a number of private controls under
``V4L2_CID_USER_BASE_CCS`` to control the MIPI CCS compliant camera sensors.

Analogue gain model
~~~~~~~~~~~~~~~~~~~

The CCS defines an analogue gain model where the gain can be calculated using
the following formula:

	gain = m0 * x + c0 / (m1 * x + c1)

Either m0 or c0 will be zero. The constants that are device specific, can be
obtained from the following controls:

	V4L2_CID_CCS_ANALOGUE_GAIN_M0
	V4L2_CID_CCS_ANALOGUE_GAIN_M1
	V4L2_CID_CCS_ANALOGUE_GAIN_C0
	V4L2_CID_CCS_ANALOGUE_GAIN_C1

The analogue gain (``x`` in the formula) is controlled through
``V4L2_CID_ANALOGUE_GAIN`` in this case.

Alternate analogue gain model
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The CCS defines another analogue gain model called alternate analogue gain. In
this case, the formula to calculate actual gain consists of linear and
exponential parts:

	gain = linear * 2 ^ exponent

The ``linear`` and ``exponent`` factors can be set using the
``V4L2_CID_CCS_ANALOGUE_LINEAR_GAIN`` and
``V4L2_CID_CCS_ANALOGUE_EXPONENTIAL_GAIN`` controls, respectively

Shading correction
~~~~~~~~~~~~~~~~~~

The CCS standard supports lens shading correction. The feature can be controlled
using ``V4L2_CID_CCS_SHADING_CORRECTION``. Additionally, the luminance
correction level may be changed using
``V4L2_CID_CCS_LUMINANCE_CORRECTION_LEVEL``, where value 0 indicates no
correction and 128 indicates correcting the luminance in corners to 10 % less
than in the centre.

Shading correction needs to be enabled for luminance correction level to have an
effect.

**Copyright** |copy| 2020 Intel Corporation
