.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _V4L2-PIX-FMT-NV12M:
.. _v4l2-pix-fmt-nv12mt-16x16:
.. _V4L2-PIX-FMT-NV21M:

***********************************************************************************
V4L2_PIX_FMT_NV12M ('NM12'), V4L2_PIX_FMT_NV21M ('NM21'), V4L2_PIX_FMT_NV12MT_16X16
***********************************************************************************


V4L2_PIX_FMT_NV21M
V4L2_PIX_FMT_NV12MT_16X16
Variation of ``V4L2_PIX_FMT_NV12`` and ``V4L2_PIX_FMT_NV21`` with planes
non contiguous in memory.


Description
===========

This is a multi-planar, two-plane version of the YUV 4:2:0 format. The
three components are separated into two sub-images or planes.
``V4L2_PIX_FMT_NV12M`` differs from ``V4L2_PIX_FMT_NV12`` in that the
two planes are non-contiguous in memory, i.e. the chroma plane do not
necessarily immediately follows the luma plane. The luminance data
occupies the first plane. The Y plane has one byte per pixel. In the
second plane there is a chrominance data with alternating chroma
samples. The CbCr plane is the same width, in bytes, as the Y plane (and
of the image), but is half as tall in pixels. Each CbCr pair belongs to
four pixels. For example, Cb\ :sub:`0`/Cr\ :sub:`0` belongs to
Y'\ :sub:`00`, Y'\ :sub:`01`, Y'\ :sub:`10`, Y'\ :sub:`11`.
``V4L2_PIX_FMT_NV12MT_16X16`` is the tiled version of
``V4L2_PIX_FMT_NV12M`` with 16x16 macroblock tiles. Here pixels are
arranged in 16x16 2D tiles and tiles are arranged in linear order in
memory. ``V4L2_PIX_FMT_NV21M`` is the same as ``V4L2_PIX_FMT_NV12M``
except the Cb and Cr bytes are swapped, the CrCb plane starts with a Cr
byte.

``V4L2_PIX_FMT_NV12M`` is intended to be used only in drivers and
applications that support the multi-planar API, described in
:ref:`planar-apis`.

If the Y plane has pad bytes after each row, then the CbCr plane has as
many pad bytes after its rows.

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
      - Cr\ :sub:`00`
      - Cb\ :sub:`01`
      - Cr\ :sub:`01`
    * - start1 + 4:
      - Cb\ :sub:`10`
      - Cr\ :sub:`10`
      - Cb\ :sub:`11`
      - Cr\ :sub:`11`


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
      -
      - Y
      - Y
      -
      - Y
    * -
      -
      - C
      -
      -
      - C
      -
    * - 1
      - Y
      -
      - Y
      - Y
      -
      - Y
    * -
    * - 2
      - Y
      -
      - Y
      - Y
      -
      - Y
    * -
      -
      - C
      -
      -
      -
      - C
      -
    * - 3
      - Y
      -
      - Y
      - Y
      -
      - Y
