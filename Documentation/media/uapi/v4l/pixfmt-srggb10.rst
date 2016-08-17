.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-SRGGB10:
.. _v4l2-pix-fmt-sbggr10:
.. _v4l2-pix-fmt-sgbrg10:
.. _v4l2-pix-fmt-sgrbg10:

***************************************************************************************************************************
V4L2_PIX_FMT_SRGGB10 ('RG10'), V4L2_PIX_FMT_SGRBG10 ('BA10'), V4L2_PIX_FMT_SGBRG10 ('GB10'), V4L2_PIX_FMT_SBGGR10 ('BG10'),
***************************************************************************************************************************

*man V4L2_PIX_FMT_SRGGB10(2)*

V4L2_PIX_FMT_SGRBG10
V4L2_PIX_FMT_SGBRG10
V4L2_PIX_FMT_SBGGR10
10-bit Bayer formats expanded to 16 bits


Description
===========

These four pixel formats are raw sRGB / Bayer formats with 10 bits per
colour. Each colour component is stored in a 16-bit word, with 6 unused
high bits filled with zeros. Each n-pixel row contains n/2 green samples
and n/2 blue or red samples, with alternating red and blue rows. Bytes
are stored in memory in little endian order. They are conventionally
described as GRGR... BGBG..., RGRG... GBGB..., etc. Below is an example
of one of these formats

**Byte Order.**
Each cell is one byte, high 6 bits in high bytes are 0.



.. tabularcolumns:: |p{3.5cm}|p{1.8cm}|p{1.8cm}|p{1.8cm}|p{1.8cm}|p{1.8cm}|p{1.8cm}|p{1.8cm}|p{1.4cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1 1 1 1 1 1 1


    -  .. row 1

       -  start + 0:

       -  B\ :sub:`00low`

       -  B\ :sub:`00high`

       -  G\ :sub:`01low`

       -  G\ :sub:`01high`

       -  B\ :sub:`02low`

       -  B\ :sub:`02high`

       -  G\ :sub:`03low`

       -  G\ :sub:`03high`

    -  .. row 2

       -  start + 8:

       -  G\ :sub:`10low`

       -  G\ :sub:`10high`

       -  R\ :sub:`11low`

       -  R\ :sub:`11high`

       -  G\ :sub:`12low`

       -  G\ :sub:`12high`

       -  R\ :sub:`13low`

       -  R\ :sub:`13high`

    -  .. row 3

       -  start + 16:

       -  B\ :sub:`20low`

       -  B\ :sub:`20high`

       -  G\ :sub:`21low`

       -  G\ :sub:`21high`

       -  B\ :sub:`22low`

       -  B\ :sub:`22high`

       -  G\ :sub:`23low`

       -  G\ :sub:`23high`

    -  .. row 4

       -  start + 24:

       -  G\ :sub:`30low`

       -  G\ :sub:`30high`

       -  R\ :sub:`31low`

       -  R\ :sub:`31high`

       -  G\ :sub:`32low`

       -  G\ :sub:`32high`

       -  R\ :sub:`33low`

       -  R\ :sub:`33high`
