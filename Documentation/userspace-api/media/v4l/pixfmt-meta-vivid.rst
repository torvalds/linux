.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.1-no-invariants-or-later

.. _v4l2-meta-fmt-vivid:

*******************************
V4L2_META_FMT_VIVID ('VIVD')
*******************************

VIVID Metadata Format


Description
===========

This describes metadata format used by the vivid driver.

It sets Brightness, Saturation, Contrast and Hue, each of which maps to
corresponding controls of the vivid driver with respect to the range and default values.

It contains the following fields:

.. flat-table:: VIVID Metadata
    :widths: 1 4
    :header-rows:  1
    :stub-columns: 0

    * - Field
      - Description
    * - u16 brightness;
      - Image brightness, the value is in the range 0 to 255, with the default value as 128.
    * - u16 contrast;
      - Image contrast, the value is in the range 0 to 255, with the default value as 128.
    * - u16 saturation;
      - Image color saturation, the value is in the range 0 to 255, with the default value as 128.
    * - s16 hue;
      - Image color balance, the value is in the range -128 to 128, with the default value as 0.
