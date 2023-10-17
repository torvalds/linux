.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _v4l2-meta-fmt-d4xx:

*******************************
V4L2_META_FMT_D4XX ('D4XX')
*******************************

Intel D4xx UVC Cameras Metadata


Description
===========

Intel D4xx (D435, D455 and others) cameras include per-frame metadata in their UVC
payload headers, following the Microsoft(R) UVC extension proposal [1_]. That
means, that the private D4XX metadata, following the standard UVC header, is
organised in blocks. D4XX cameras implement several standard block types,
proposed by Microsoft, and several proprietary ones. Supported standard metadata
types are MetadataId_CaptureStats (ID 3), MetadataId_CameraExtrinsics (ID 4),
and MetadataId_CameraIntrinsics (ID 5). For their description see [1_]. This
document describes proprietary metadata types, used by D4xx cameras.

V4L2_META_FMT_D4XX buffers follow the metadata buffer layout of
V4L2_META_FMT_UVC with the only difference, that it also includes proprietary
payload header data. D4xx cameras use bulk transfers and only send one payload
per frame, therefore their headers cannot be larger than 255 bytes.

This document implements Intel Configuration version 3 [9_].

Below are proprietary Microsoft style metadata types, used by D4xx cameras,
where all fields are in little endian order:

.. tabularcolumns:: |p{5.0cm}|p{12.5cm}|


.. flat-table:: D4xx metadata
    :widths: 1 2
    :header-rows:  1
    :stub-columns: 0

    * - **Field**
      - **Description**
    * - :cspan:`1` *Depth Control*
    * - __u32 ID
      - 0x80000000
    * - __u32 Size
      - Size in bytes, include ID (all protocol versions: 60)
    * - __u32 Version
      - Version of this structure. The documentation herein covers versions 1,
        2 and 3. The version number will be incremented when new fields are
        added.
    * - __u32 Flags
      - A bitmask of flags: see [2_] below
    * - __u32 Gain
      - Gain value in internal units, same as the V4L2_CID_GAIN control, used to
	capture the frame
    * - __u32 Exposure
      - Exposure time (in microseconds) used to capture the frame
    * - __u32 Laser power
      - Power of the laser LED 0-360, used for depth measurement
    * - __u32 AE mode
      - 0: manual; 1: automatic exposure
    * - __u32 Exposure priority
      - Exposure priority value: 0 - constant frame rate
    * - __u32 AE ROI left
      - Left border of the AE Region of Interest (all ROI values are in pixels
	and lie between 0 and maximum width or height respectively)
    * - __u32 AE ROI right
      - Right border of the AE Region of Interest
    * - __u32 AE ROI top
      - Top border of the AE Region of Interest
    * - __u32 AE ROI bottom
      - Bottom border of the AE Region of Interest
    * - __u32 Preset
      - Preset selector value, default: 0, unless changed by the user
    * - __u8 Emitter mode (v3 only) (__u32 Laser mode for v1) [8_]
      - 0: off, 1: on, same as __u32 Laser mode for v1
    * - __u8 RFU byte (v3 only)
      - Spare byte for future use
    * - __u16 LED Power (v3 only)
      - Led power value 0-360 (F416 SKU)
    * - :cspan:`1` *Capture Timing*
    * - __u32 ID
      - 0x80000001
    * - __u32 Size
      - Size in bytes, include ID (all protocol versions: 40)
    * - __u32 Version
      - Version of this structure. The documentation herein corresponds to
        version xxx. The version number will be incremented when new fields are
        added.
    * - __u32 Flags
      - A bitmask of flags: see [3_] below
    * - __u32 Frame counter
      - Monotonically increasing counter
    * - __u32 Optical time
      - Time in microseconds from the beginning of a frame till its middle
    * - __u32 Readout time
      - Time, used to read out a frame in microseconds
    * - __u32 Exposure time
      - Frame exposure time in microseconds
    * - __u32 Frame interval
      - In microseconds = 1000000 / framerate
    * - __u32 Pipe latency
      - Time in microseconds from start of frame to data in USB buffer
    * - :cspan:`1` *Configuration*
    * - __u32 ID
      - 0x80000002
    * - __u32 Size
      - Size in bytes, include ID (v1:36, v3:40)
    * - __u32 Version
      - Version of this structure. The documentation herein corresponds to
        version xxx. The version number will be incremented when new fields are
        added.
    * - __u32 Flags
      - A bitmask of flags: see [4_] below
    * - __u8 Hardware type
      - Camera hardware version [5_]
    * - __u8 SKU ID
      - Camera hardware configuration [6_]
    * - __u32 Cookie
      - Internal synchronisation
    * - __u16 Format
      - Image format code [7_]
    * - __u16 Width
      - Width in pixels
    * - __u16 Height
      - Height in pixels
    * - __u16 Framerate
      - Requested frame rate per second
    * - __u16 Trigger
      - Byte 0: bit 0: depth and RGB are synchronised, bit 1: external trigger
    * - __u16 Calibration count (v3 only)
      - Calibration counter, see [4_] below
    * - __u8 GPIO input data (v3 only)
      - GPIO readout, see [4_] below (Supported from FW 5.12.7.0)
    * - __u32 Sub-preset info (v3 only)
      - Sub-preset choice information, see [4_] below
    * - __u8 reserved (v3 only)
      - RFU byte.

.. _1:

[1] https://docs.microsoft.com/en-us/windows-hardware/drivers/stream/uvc-extensions-1-5

.. _2:

[2] Depth Control flags specify which fields are valid: ::

  0x00000001 Gain
  0x00000002 Exposure
  0x00000004 Laser power
  0x00000008 AE mode
  0x00000010 Exposure priority
  0x00000020 AE ROI
  0x00000040 Preset
  0x00000080 Emitter mode
  0x00000100 LED Power

.. _3:

[3] Capture Timing flags specify which fields are valid: ::

  0x00000001 Frame counter
  0x00000002 Optical time
  0x00000004 Readout time
  0x00000008 Exposure time
  0x00000010 Frame interval
  0x00000020 Pipe latency

.. _4:

[4] Configuration flags specify which fields are valid: ::

  0x00000001 Hardware type
  0x00000002 SKU ID
  0x00000004 Cookie
  0x00000008 Format
  0x00000010 Width
  0x00000020 Height
  0x00000040 Framerate
  0x00000080 Trigger
  0x00000100 Cal count
  0x00000200 GPIO Input Data
  0x00000400 Sub-preset Info

.. _5:

[5] Camera model: ::

  0 DS5
  1 IVCAM2

.. _6:

[6] 8-bit camera hardware configuration bitfield: ::

  [1:0] depthCamera
	00: no depth
	01: standard depth
	10: wide depth
	11: reserved
  [2]   depthIsActive - has a laser projector
  [3]   RGB presence
  [4]   Inertial Measurement Unit (IMU) presence
  [5]   projectorType
	0: HPTG
	1: Princeton
  [6]   0: a projector, 1: an LED
  [7]   reserved

.. _7:

[7] Image format codes per video streaming interface:

Depth: ::

  1 Z16
  2 Z

Left sensor: ::

  1 Y8
  2 UYVY
  3 R8L8
  4 Calibration
  5 W10

Fish Eye sensor: ::

  1 RAW8

.. _8:

[8] The "Laser mode" has been replaced in version 3 by three different fields.
"Laser" has been renamed to "Emitter" as there are multiple technologies for
camera projectors. As we have another field for "Laser Power" we introduced
"LED Power" for extra emitter.

The "Laser mode" __u32 fields has been split into: ::
   1 __u8 Emitter mode
   2 __u8 RFU byte
   3 __u16 LED Power

This is a change between versions 1 and 3. All versions 1, 2 and 3 are backward
compatible with the same data format and they are supported. See [2_] for which
attributes are valid.

.. _9:

[9] LibRealSense SDK metadata source:
https://github.com/IntelRealSense/librealsense/blob/master/src/metadata.h
