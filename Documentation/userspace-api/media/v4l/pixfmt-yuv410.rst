.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _V4L2-PIX-FMT-YVU410:
.. _v4l2-pix-fmt-yuv410:

**********************************************************
V4L2_PIX_FMT_YVU410 ('YVU9'), V4L2_PIX_FMT_YUV410 ('YUV9')
**********************************************************


V4L2_PIX_FMT_YUV410
Planar formats with ¼ horizontal and vertical chroma resolution, also
known as YUV 4:1:0


Description
===========

These are planar formats, as opposed to a packed format. The three
components are separated into three sub-images or planes. The Y plane is
first. The Y plane has one byte per pixel. For ``V4L2_PIX_FMT_YVU410``,
the Cr plane immediately follows the Y plane in memory. The Cr plane is
¼ the width and ¼ the height of the Y plane (and of the image). Each Cr
belongs to 16 pixels, a four-by-four square of the image. Following the
Cr plane is the Cb plane, just like the Cr plane.
``V4L2_PIX_FMT_YUV410`` is the same, except the Cb plane comes first,
then the Cr plane.

If the Y plane has pad bytes after each row, then the Cr and Cb planes
have ¼ as many pad bytes after their rows. In other words, four Cx rows
(including padding) are exactly as long as one Y row (including
padding).

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * - start + 16:
      - Cr\ :sub:`00`
    * - start + 17:
      - Cb\ :sub:`00`


**Color Sample Location:**



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * -
      - 0
      -
      - 1
      -
      - 2
      -
      - 3
    * - 0
      - Y
      -
      - Y
      -
      - Y
      -
      - Y
    * -
    * - 1
      - Y
      -
      - Y
      -
      - Y
      -
      - Y
    * -
      -
      -
      -
      - C
      -
      -
      -
    * - 2
      - Y
      -
      - Y
      -
      - Y
      -
      - Y
    * -
    * - 3
      - Y
      -
      - Y
      -
      - Y
      -
      - Y
