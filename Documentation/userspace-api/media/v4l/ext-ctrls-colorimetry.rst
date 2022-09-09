.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _colorimetry-controls:

*****************************
Colorimetry Control Reference
*****************************

The Colorimetry class includes controls for High Dynamic Range
imaging for representing colors in digital images and video. The
controls should be used for video and image encoding and decoding
as well as in HDMI receivers and transmitters.

Colorimetry Control IDs
-----------------------

.. _colorimetry-control-id:

``V4L2_CID_COLORIMETRY_CLASS (class)``
    The Colorimetry class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

``V4L2_CID_COLORIMETRY_HDR10_CLL_INFO (struct)``
    The Content Light Level defines upper bounds for the nominal target
    brightness light level of the pictures.

.. c:type:: v4l2_ctrl_hdr10_cll_info

.. cssclass:: longtable

.. flat-table:: struct v4l2_ctrl_hdr10_cll_info
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u16
      - ``max_content_light_level``
      - The upper bound for the maximum light level among all individual
        samples for the pictures of a video sequence, cd/m\ :sup:`2`.
        When equal to 0 no such upper bound is present.
    * - __u16
      - ``max_pic_average_light_level``
      - The upper bound for the maximum average light level among the
        samples for any individual picture of a video sequence,
        cd/m\ :sup:`2`. When equal to 0 no such upper bound is present.

``V4L2_CID_COLORIMETRY_HDR10_MASTERING_DISPLAY (struct)``
    The mastering display defines the color volume (the color primaries,
    white point and luminance range) of a display considered to be the
    mastering display for the current video content.

.. c:type:: v4l2_ctrl_hdr10_mastering_display

.. cssclass:: longtable

.. flat-table:: struct v4l2_ctrl_hdr10_mastering_display
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u16
      - ``display_primaries_x[3]``
      - Specifies the normalized x chromaticity coordinate of the color
        primary component c of the mastering display in increments of 0.00002.
        For describing the mastering display that uses Red, Green and Blue
        color primaries, index value c equal to 0 corresponds to the Green
        primary, c equal to 1 corresponds to Blue primary and c equal to 2
        corresponds to the Red color primary.
    * - __u16
      - ``display_primaries_y[3]``
      - Specifies the normalized y chromaticity coordinate of the color
        primary component c of the mastering display in increments of 0.00002.
        For describing the mastering display that uses Red, Green and Blue
        color primaries, index value c equal to 0 corresponds to the Green
        primary, c equal to 1 corresponds to Blue primary and c equal to 2
        corresponds to Red color primary.
    * - __u16
      - ``white_point_x``
      - Specifies the normalized x chromaticity coordinate of the white
        point of the mastering display in increments of 0.00002.
    * - __u16
      - ``white_point_y``
      - Specifies the normalized y chromaticity coordinate of the white
        point of the mastering display in increments of 0.00002.
    * - __u32
      - ``max_luminance``
      - Specifies the nominal maximum display luminance of the mastering
        display in units of 0.0001 cd/m\ :sup:`2`.
    * - __u32
      - ``min_luminance``
      - specifies the nominal minimum display luminance of the mastering
        display in units of 0.0001 cd/m\ :sup:`2`.
