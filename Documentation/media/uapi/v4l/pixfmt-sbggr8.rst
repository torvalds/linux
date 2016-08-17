.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-SBGGR8:

****************************
V4L2_PIX_FMT_SBGGR8 ('BA81')
****************************

*man V4L2_PIX_FMT_SBGGR8(2)*

Bayer RGB format


Description
===========

This is commonly the native format of digital cameras, reflecting the
arrangement of sensors on the CCD device. Only one red, green or blue
value is given for each pixel. Missing components must be interpolated
from neighbouring pixels. From left to right the first row consists of a
blue and green value, the second row of a green and red value. This
scheme repeats to the right and down for every two columns and rows.

**Byte Order.**
Each cell is one byte.



.. tabularcolumns:: |p{5.8cm}|p{2.9cm}|p{2.9cm}|p{2.9cm}|p{3.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1 1 1


    -  .. row 1

       -  start + 0:

       -  B\ :sub:`00`

       -  G\ :sub:`01`

       -  B\ :sub:`02`

       -  G\ :sub:`03`

    -  .. row 2

       -  start + 4:

       -  G\ :sub:`10`

       -  R\ :sub:`11`

       -  G\ :sub:`12`

       -  R\ :sub:`13`

    -  .. row 3

       -  start + 8:

       -  B\ :sub:`20`

       -  G\ :sub:`21`

       -  B\ :sub:`22`

       -  G\ :sub:`23`

    -  .. row 4

       -  start + 12:

       -  G\ :sub:`30`

       -  R\ :sub:`31`

       -  G\ :sub:`32`

       -  R\ :sub:`33`
