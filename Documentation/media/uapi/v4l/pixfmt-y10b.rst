.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-Y10BPACK:

******************************
V4L2_PIX_FMT_Y10BPACK ('Y10B')
******************************

Grey-scale image as a bit-packed array


Description
===========

This is a packed grey-scale image format with a depth of 10 bits per
pixel. Pixels are stored in a bit-packed array of 10bit bits per pixel,
with no padding between them and with the most significant bits coming
first from the left.

**Bit-packed representation.**

pixels cross the byte boundary and have a ratio of 5 bytes for each 4
pixels.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  .. row 1

       -  Y'\ :sub:`00[9:2]`

       -  Y'\ :sub:`00[1:0]`\ Y'\ :sub:`01[9:4]`

       -  Y'\ :sub:`01[3:0]`\ Y'\ :sub:`02[9:6]`

       -  Y'\ :sub:`02[5:0]`\ Y'\ :sub:`03[9:8]`

       -  Y'\ :sub:`03[7:0]`
