.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _V4L2-PIX-FMT-SRGGB8:
.. _v4l2-pix-fmt-sbggr8:
.. _v4l2-pix-fmt-sgbrg8:
.. _v4l2-pix-fmt-sgrbg8:

***************************************************************************************************************************
V4L2_PIX_FMT_SRGGB8 ('RGGB'), V4L2_PIX_FMT_SGRBG8 ('GRBG'), V4L2_PIX_FMT_SGBRG8 ('GBRG'), V4L2_PIX_FMT_SBGGR8 ('BA81'),
***************************************************************************************************************************


8-bit Bayer formats


Description
===========

These four pixel formats are raw sRGB / Bayer formats with 8 bits per
sample. Each sample is stored in a byte. Each n-pixel row contains n/2
green samples and n/2 blue or red samples, with alternating red and
blue rows. They are conventionally described as GRGR... BGBG...,
RGRG... GBGB..., etc. Below is an example of a small V4L2_PIX_FMT_SBGGR8 image:

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - B\ :sub:`00`
      - G\ :sub:`01`
      - B\ :sub:`02`
      - G\ :sub:`03`
    * - start + 4:
      - G\ :sub:`10`
      - R\ :sub:`11`
      - G\ :sub:`12`
      - R\ :sub:`13`
    * - start + 8:
      - B\ :sub:`20`
      - G\ :sub:`21`
      - B\ :sub:`22`
      - G\ :sub:`23`
    * - start + 12:
      - G\ :sub:`30`
      - R\ :sub:`31`
      - G\ :sub:`32`
      - R\ :sub:`33`
