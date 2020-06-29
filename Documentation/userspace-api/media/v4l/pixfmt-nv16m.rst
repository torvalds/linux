.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _V4L2-PIX-FMT-NV16M:
.. _v4l2-pix-fmt-nv61m:

********************************************************
V4L2_PIX_FMT_NV16M ('NM16'), V4L2_PIX_FMT_NV61M ('NM61')
********************************************************

V4L2_PIX_FMT_NV61M
Variation of ``V4L2_PIX_FMT_NV16`` and ``V4L2_PIX_FMT_NV61`` with planes
non contiguous in memory.


Description
===========

This is a multi-planar, two-plane version of the YUV 4:2:2 format. The
three components are separated into two sub-images or planes.
``V4L2_PIX_FMT_NV16M`` differs from ``V4L2_PIX_FMT_NV16`` in that the
two planes are non-contiguous in memory, i.e. the chroma plane does not
necessarily immediately follow the luma plane. The luminance data
occupies the first plane. The Y plane has one byte per pixel. In the
second plane there is chrominance data with alternating chroma samples.
The CbCr plane is the same width and height, in bytes, as the Y plane.
Each CbCr pair belongs to two pixels. For example,
Cb\ :sub:`0`/Cr\ :sub:`0` belongs to Y'\ :sub:`00`, Y'\ :sub:`01`.
``V4L2_PIX_FMT_NV61M`` is the same as ``V4L2_PIX_FMT_NV16M`` except the
Cb and Cr bytes are swapped, the CrCb plane starts with a Cr byte.

``V4L2_PIX_FMT_NV16M`` and ``V4L2_PIX_FMT_NV61M`` are intended to be
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
      - Cr\ :sub:`00`
      - Cb\ :sub:`02`
      - Cr\ :sub:`02`
    * - start1 + 4:
      - Cb\ :sub:`10`
      - Cr\ :sub:`10`
      - Cb\ :sub:`12`
      - Cr\ :sub:`12`
    * - start1 + 8:
      - Cb\ :sub:`20`
      - Cr\ :sub:`20`
      - Cb\ :sub:`22`
      - Cr\ :sub:`22`
    * - start1 + 12:
      - Cb\ :sub:`30`
      - Cr\ :sub:`30`
      - Cb\ :sub:`32`
      - Cr\ :sub:`32`


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
      -
      - C
      -
      -
      - C
      -
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
      - C
      -
    * - 3
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
