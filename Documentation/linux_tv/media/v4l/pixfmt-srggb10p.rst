.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-SRGGB10P:
.. _v4l2-pix-fmt-sbggr10p:
.. _v4l2-pix-fmt-sgbrg10p:
.. _v4l2-pix-fmt-sgrbg10p:

*******************************************************************************************************************************
V4L2_PIX_FMT_SRGGB10P ('pRAA'), V4L2_PIX_FMT_SGRBG10P ('pgAA'), V4L2_PIX_FMT_SGBRG10P ('pGAA'), V4L2_PIX_FMT_SBGGR10P ('pBAA'),
*******************************************************************************************************************************

*man V4L2_PIX_FMT_SRGGB10P(2)*

V4L2_PIX_FMT_SGRBG10P
V4L2_PIX_FMT_SGBRG10P
V4L2_PIX_FMT_SBGGR10P
10-bit packed Bayer formats


Description
===========

These four pixel formats are packed raw sRGB / Bayer formats with 10
bits per colour. Every four consecutive colour components are packed
into 5 bytes. Each of the first 4 bytes contain the 8 high order bits of
the pixels, and the fifth byte contains the two least significants bits
of each pixel, in the same order.

Each n-pixel row contains n/2 green samples and n/2 blue or red samples,
with alternating green-red and green-blue rows. They are conventionally
described as GRGR... BGBG..., RGRG... GBGB..., etc. Below is an example
of one of these formats:

**Byte Order.**
Each cell is one byte.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1 1 1 1


    -  .. row 1

       -  start + 0:

       -  B\ :sub:`00high`

       -  G\ :sub:`01high`

       -  B\ :sub:`02high`

       -  G\ :sub:`03high`

       -  B\ :sub:`00low`\ (bits 7--6) G\ :sub:`01low`\ (bits 5--4)
	  B\ :sub:`02low`\ (bits 3--2) G\ :sub:`03low`\ (bits 1--0)

    -  .. row 2

       -  start + 5:

       -  G\ :sub:`10high`

       -  R\ :sub:`11high`

       -  G\ :sub:`12high`

       -  R\ :sub:`13high`

       -  G\ :sub:`10low`\ (bits 7--6) R\ :sub:`11low`\ (bits 5--4)
	  G\ :sub:`12low`\ (bits 3--2) R\ :sub:`13low`\ (bits 1--0)

    -  .. row 3

       -  start + 10:

       -  B\ :sub:`20high`

       -  G\ :sub:`21high`

       -  B\ :sub:`22high`

       -  G\ :sub:`23high`

       -  B\ :sub:`20low`\ (bits 7--6) G\ :sub:`21low`\ (bits 5--4)
	  B\ :sub:`22low`\ (bits 3--2) G\ :sub:`23low`\ (bits 1--0)

    -  .. row 4

       -  start + 15:

       -  G\ :sub:`30high`

       -  R\ :sub:`31high`

       -  G\ :sub:`32high`

       -  R\ :sub:`33high`

       -  G\ :sub:`30low`\ (bits 7--6) R\ :sub:`31low`\ (bits 5--4)
	  G\ :sub:`32low`\ (bits 3--2) R\ :sub:`33low`\ (bits 1--0)
