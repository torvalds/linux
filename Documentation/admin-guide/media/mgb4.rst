.. SPDX-License-Identifier: GPL-2.0

The mgb4 driver
===============

sysfs interface
---------------

The mgb4 driver provides a sysfs interface, that is used to configure video
stream related parameters (some of them must be set properly before the v4l2
device can be opened) and obtain the video device/stream status.

There are two types of parameters - global / PCI card related, found under
``/sys/class/video4linux/videoX/device`` and module specific found under
``/sys/class/video4linux/videoX``.

Global (PCI card) parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**module_type** (R):
    Module type.

    | 0 - No module present
    | 1 - FPDL3
    | 2 - GMSL (one serializer, two daisy chained deserializers)
    | 3 - GMSL (one serializer, two deserializers)
    | 4 - GMSL (two deserializers with two daisy chain outputs)

**module_version** (R):
    Module version number. Zero in case of a missing module.

**fw_type** (R):
    Firmware type.

    | 1 - FPDL3
    | 2 - GMSL

**fw_version** (R):
    Firmware version number.

**serial_number** (R):
    Card serial number. The format is::

        PRODUCT-REVISION-SERIES-SERIAL

    where each component is a 8b number.

Common FPDL3/GMSL input parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**input_id** (R):
    Input number ID, zero based.

**oldi_lane_width** (RW):
    Number of deserializer output lanes.

    | 0 - single
    | 1 - dual (default)

**color_mapping** (RW):
    Mapping of the incoming bits in the signal to the colour bits of the pixels.

    | 0 - OLDI/JEIDA
    | 1 - SPWG/VESA (default)

**link_status** (R):
    Video link status. If the link is locked, chips are properly connected and
    communicating at the same speed and protocol. The link can be locked without
    an active video stream.

    A value of 0 is equivalent to the V4L2_IN_ST_NO_SYNC flag of the V4L2
    VIDIOC_ENUMINPUT status bits.

    | 0 - unlocked
    | 1 - locked

**stream_status** (R):
    Video stream status. A stream is detected if the link is locked, the input
    pixel clock is running and the DE signal is moving.

    A value of 0 is equivalent to the V4L2_IN_ST_NO_SIGNAL flag of the V4L2
    VIDIOC_ENUMINPUT status bits.

    | 0 - not detected
    | 1 - detected

**video_width** (R):
    Video stream width. This is the actual width as detected by the HW.

    The value is identical to what VIDIOC_QUERY_DV_TIMINGS returns in the width
    field of the v4l2_bt_timings struct.

**video_height** (R):
    Video stream height. This is the actual height as detected by the HW.

    The value is identical to what VIDIOC_QUERY_DV_TIMINGS returns in the height
    field of the v4l2_bt_timings struct.

**vsync_status** (R):
    The type of VSYNC pulses as detected by the video format detector.

    The value is equivalent to the flags returned by VIDIOC_QUERY_DV_TIMINGS in
    the polarities field of the v4l2_bt_timings struct.

    | 0 - active low
    | 1 - active high
    | 2 - not available

**hsync_status** (R):
    The type of HSYNC pulses as detected by the video format detector.

    The value is equivalent to the flags returned by VIDIOC_QUERY_DV_TIMINGS in
    the polarities field of the v4l2_bt_timings struct.

    | 0 - active low
    | 1 - active high
    | 2 - not available

**vsync_gap_length** (RW):
    If the incoming video signal does not contain synchronization VSYNC and
    HSYNC pulses, these must be generated internally in the FPGA to achieve
    the correct frame ordering. This value indicates, how many "empty" pixels
    (pixels with deasserted Data Enable signal) are necessary to generate the
    internal VSYNC pulse.

**hsync_gap_length** (RW):
    If the incoming video signal does not contain synchronization VSYNC and
    HSYNC pulses, these must be generated internally in the FPGA to achieve
    the correct frame ordering. This value indicates, how many "empty" pixels
    (pixels with deasserted Data Enable signal) are necessary to generate the
    internal HSYNC pulse. The value must be greater than 1 and smaller than
    vsync_gap_length.

**pclk_frequency** (R):
    Input pixel clock frequency in kHz.

    The value is identical to what VIDIOC_QUERY_DV_TIMINGS returns in
    the pixelclock field of the v4l2_bt_timings struct.

    *Note: The frequency_range parameter must be set properly first to get
    a valid frequency here.*

**hsync_width** (R):
    Width of the HSYNC signal in PCLK clock ticks.

    The value is identical to what VIDIOC_QUERY_DV_TIMINGS returns in
    the hsync field of the v4l2_bt_timings struct.

**vsync_width** (R):
    Width of the VSYNC signal in PCLK clock ticks.

    The value is identical to what VIDIOC_QUERY_DV_TIMINGS returns in
    the vsync field of the v4l2_bt_timings struct.

**hback_porch** (R):
    Number of PCLK pulses between deassertion of the HSYNC signal and the first
    valid pixel in the video line (marked by DE=1).

    The value is identical to what VIDIOC_QUERY_DV_TIMINGS returns in
    the hbackporch field of the v4l2_bt_timings struct.

**hfront_porch** (R):
    Number of PCLK pulses between the end of the last valid pixel in the video
    line (marked by DE=1) and assertion of the HSYNC signal.

    The value is identical to what VIDIOC_QUERY_DV_TIMINGS returns in
    the hfrontporch field of the v4l2_bt_timings struct.

**vback_porch** (R):
    Number of video lines between deassertion of the VSYNC signal and the video
    line with the first valid pixel (marked by DE=1).

    The value is identical to what VIDIOC_QUERY_DV_TIMINGS returns in
    the vbackporch field of the v4l2_bt_timings struct.

**vfront_porch** (R):
    Number of video lines between the end of the last valid pixel line (marked
    by DE=1) and assertion of the VSYNC signal.

    The value is identical to what VIDIOC_QUERY_DV_TIMINGS returns in
    the vfrontporch field of the v4l2_bt_timings struct.

**frequency_range** (RW)
    PLL frequency range of the OLDI input clock generator. The PLL frequency is
    derived from the Pixel Clock Frequency (PCLK) and is equal to PCLK if
    oldi_lane_width is set to "single" and PCLK/2 if oldi_lane_width is set to
    "dual".

    | 0 - PLL < 50MHz (default)
    | 1 - PLL >= 50MHz

    *Note: This parameter can not be changed while the input v4l2 device is
    open.*

Common FPDL3/GMSL output parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**output_id** (R):
    Output number ID, zero based.

**video_source** (RW):
    Output video source. If set to 0 or 1, the source is the corresponding card
    input and the v4l2 output devices are disabled. If set to 2 or 3, the source
    is the corresponding v4l2 video output device. The default is
    the corresponding v4l2 output, i.e. 2 for OUT1 and 3 for OUT2.

    | 0 - input 0
    | 1 - input 1
    | 2 - v4l2 output 0
    | 3 - v4l2 output 1

    *Note: This parameter can not be changed while ANY of the input/output v4l2
    devices is open.*

**display_width** (RW):
    Display width. There is no autodetection of the connected display, so the
    proper value must be set before the start of streaming. The default width
    is 1280.

    *Note: This parameter can not be changed while the output v4l2 device is
    open.*

**display_height** (RW):
    Display height. There is no autodetection of the connected display, so the
    proper value must be set before the start of streaming. The default height
    is 640.

    *Note: This parameter can not be changed while the output v4l2 device is
    open.*

**frame_rate** (RW):
    Output video signal frame rate limit in frames per second. Due to
    the limited output pixel clock steps, the card can not always generate
    a frame rate perfectly matching the value required by the connected display.
    Using this parameter one can limit the frame rate by "crippling" the signal
    so that the lines are not equal (the porches of the last line differ) but
    the signal appears like having the exact frame rate to the connected display.
    The default frame rate limit is 60Hz.

**hsync_polarity** (RW):
    HSYNC signal polarity.

    | 0 - active low (default)
    | 1 - active high

**vsync_polarity** (RW):
    VSYNC signal polarity.

    | 0 - active low (default)
    | 1 - active high

**de_polarity** (RW):
    DE signal polarity.

    | 0 - active low
    | 1 - active high (default)

**pclk_frequency** (RW):
    Output pixel clock frequency. Allowed values are between 25000-190000(kHz)
    and there is a non-linear stepping between two consecutive allowed
    frequencies. The driver finds the nearest allowed frequency to the given
    value and sets it. When reading this property, you get the exact
    frequency set by the driver. The default frequency is 61150kHz.

    *Note: This parameter can not be changed while the output v4l2 device is
    open.*

**hsync_width** (RW):
    Width of the HSYNC signal in pixels. The default value is 40.

**vsync_width** (RW):
    Width of the VSYNC signal in video lines. The default value is 20.

**hback_porch** (RW):
    Number of PCLK pulses between deassertion of the HSYNC signal and the first
    valid pixel in the video line (marked by DE=1). The default value is 50.

**hfront_porch** (RW):
    Number of PCLK pulses between the end of the last valid pixel in the video
    line (marked by DE=1) and assertion of the HSYNC signal. The default value
    is 50.

**vback_porch** (RW):
    Number of video lines between deassertion of the VSYNC signal and the video
    line with the first valid pixel (marked by DE=1). The default value is 31.

**vfront_porch** (RW):
    Number of video lines between the end of the last valid pixel line (marked
    by DE=1) and assertion of the VSYNC signal. The default value is 30.

FPDL3 specific input parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**fpdl3_input_width** (RW):
    Number of deserializer input lines.

    | 0 - auto (default)
    | 1 - single
    | 2 - dual

FPDL3 specific output parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**fpdl3_output_width** (RW):
    Number of serializer output lines.

    | 0 - auto (default)
    | 1 - single
    | 2 - dual

GMSL specific input parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**gmsl_mode** (RW):
    GMSL speed mode.

    | 0 - 12Gb/s (default)
    | 1 - 6Gb/s
    | 2 - 3Gb/s
    | 3 - 1.5Gb/s

**gmsl_stream_id** (RW):
    The GMSL multi-stream contains up to four video streams. This parameter
    selects which stream is captured by the video input. The value is the
    zero-based index of the stream. The default stream id is 0.

    *Note: This parameter can not be changed while the input v4l2 device is
    open.*

**gmsl_fec** (RW):
    GMSL Forward Error Correction (FEC).

    | 0 - disabled
    | 1 - enabled (default)

MTD partitions
--------------

The mgb4 driver creates a MTD device with two partitions:
 - mgb4-fw.X - FPGA firmware.
 - mgb4-data.X - Factory settings, e.g. card serial number.

The *mgb4-fw* partition is writable and is used for FW updates, *mgb4-data* is
read-only. The *X* attached to the partition name represents the card number.
Depending on the CONFIG_MTD_PARTITIONED_MASTER kernel configuration, you may
also have a third partition named *mgb4-flash* available in the system. This
partition represents the whole, unpartitioned, card's FLASH memory and one should
not fiddle with it...

IIO (triggers)
--------------

The mgb4 driver creates an Industrial I/O (IIO) device that provides trigger and
signal level status capability. The following scan elements are available:

**activity**:
	The trigger levels and pending status.

	| bit 1 - trigger 1 pending
	| bit 2 - trigger 2 pending
	| bit 5 - trigger 1 level
	| bit 6 - trigger 2 level

**timestamp**:
	The trigger event timestamp.

The iio device can operate either in "raw" mode where you can fetch the signal
levels (activity bits 5 and 6) using sysfs access or in triggered buffer mode.
In the triggered buffer mode you can follow the signal level changes (activity
bits 1 and 2) using the iio device in /dev. If you enable the timestamps, you
will also get the exact trigger event time that can be matched to a video frame
(every mgb4 video frame has a timestamp with the same clock source).

*Note: although the activity sample always contains all the status bits, it makes
no sense to get the pending bits in raw mode or the level bits in the triggered
buffer mode - the values do not represent valid data in such case.*
