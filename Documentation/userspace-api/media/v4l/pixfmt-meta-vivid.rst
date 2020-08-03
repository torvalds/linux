.. This file is dual-licensed: you can use it either under the terms
.. of the GPL 2.0 or the GFDL 1.1+ license, at your option. Note that this
.. dual licensing only applies to this file, and not this project as a
.. whole.
..
.. a) This file is free software; you can redistribute it and/or
..    modify it under the terms of the GNU General Public License as
..    published by the Free Software Foundation version 2 of
..    the License.
..
..    This file is distributed in the hope that it will be useful,
..    but WITHOUT ANY WARRANTY; without even the implied warranty of
..    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
..    GNU General Public License for more details.
..
.. Or, alternatively,
..
.. b) Permission is granted to copy, distribute and/or modify this
..    document under the terms of the GNU Free Documentation License,
..    Version 1.1 or any later version published by the Free Software
..    Foundation, with no Invariant Sections, no Front-Cover Texts
..    and no Back-Cover Texts. A copy of the license is included at
..    Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GPL-2.0 OR GFDL-1.1-or-later WITH no-invariant-sections

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
