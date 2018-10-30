.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-Y10P:

******************************
V4L2_PIX_FMT_Y10P ('Y10P')
******************************

Grey-scale image as a MIPI RAW10 packed array


Description
===========

This is a packed grey-scale image format with a depth of 10 bits per
pixel. Every four consecutive pixels are packed into 5 bytes. Each of
the first 4 bytes contain the 8 high order bits of the pixels, and
the 5th byte contains the 2 least significants bits of each pixel,
in the same order.

**Bit-packed representation.**

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths: 8 8 8 8 64

    * - Y'\ :sub:`00[9:2]`
      - Y'\ :sub:`01[9:2]`
      - Y'\ :sub:`02[9:2]`
      - Y'\ :sub:`03[9:2]`
      - Y'\ :sub:`03[1:0]`\ (bits 7--6) Y'\ :sub:`02[1:0]`\ (bits 5--4)
	Y'\ :sub:`01[1:0]`\ (bits 3--2) Y'\ :sub:`00[1:0]`\ (bits 1--0)
