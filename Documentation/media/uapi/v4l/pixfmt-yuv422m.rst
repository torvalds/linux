.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _V4L2-PIX-FMT-YUV422M:
.. _v4l2-pix-fmt-yvu422m:

************************************************************
V4L2_PIX_FMT_YUV422M ('YM16'), V4L2_PIX_FMT_YVU422M ('YM61')
************************************************************


V4L2_PIX_FMT_YVU422M
Planar formats with ½ horizontal resolution, also known as YUV and YVU
4:2:2


Description
===========

This is a multi-planar format, as opposed to a packed format. The three
components are separated into three sub-images or planes.

The Y plane is first. The Y plane has one byte per pixel. For
``V4L2_PIX_FMT_YUV422M`` the Cb data constitutes the second plane which
is half the width of the Y plane (and of the image). Each Cb belongs to
two pixels. For example, Cb\ :sub:`0` belongs to Y'\ :sub:`00`,
Y'\ :sub:`01`. The Cr data, just like the Cb plane, is in the third
plane.

``V4L2_PIX_FMT_YVU422M`` is the same except the Cr data is stored in the
second plane and the Cb data in the third plane.

If the Y plane has pad bytes after each row, then the Cb and Cr planes
have half as many pad bytes after their rows. In other words, two Cx
rows (including padding) is exactly as long as one Y row (including
padding).

``V4L2_PIX_FMT_YUV422M`` and ``V4L2_PIX_FMT_YVU422M`` are intended to be
used only in drivers and applications that support the multi-planar API,
described in :ref:`planar-apis`.

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start0 + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start0 + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start0 + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * -
    * - start1 + 0:
      - Cb\ :sub:`00`
      - Cb\ :sub:`01`
    * - start1 + 2:
      - Cb\ :sub:`10`
      - Cb\ :sub:`11`
    * - start1 + 4:
      - Cb\ :sub:`20`
      - Cb\ :sub:`21`
    * - start1 + 6:
      - Cb\ :sub:`30`
      - Cb\ :sub:`31`
    * -
    * - start2 + 0:
      - Cr\ :sub:`00`
      - Cr\ :sub:`01`
    * - start2 + 2:
      - Cr\ :sub:`10`
      - Cr\ :sub:`11`
    * - start2 + 4:
      - Cr\ :sub:`20`
      - Cr\ :sub:`21`
    * - start2 + 6:
      - Cr\ :sub:`30`
      - Cr\ :sub:`31`


**Color Sample Location:**



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * -
      - 0
      -
      - 1
      - 2
      -
      - 3
    * - 0
      - Y
      - C
      - Y
      - Y
      - C
      - Y
    * - 1
      - Y
      - C
      - Y
      - Y
      - C
      - Y
    * - 2
      - Y
      - C
      - Y
      - Y
      - C
      - Y
    * - 3
      - Y
      - C
      - Y
      - Y
      - C
      - Y
