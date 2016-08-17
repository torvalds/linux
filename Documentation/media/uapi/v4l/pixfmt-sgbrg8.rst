.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-SGBRG8:

****************************
V4L2_PIX_FMT_SGBRG8 ('GBRG')
****************************

*man V4L2_PIX_FMT_SGBRG8(2)*

Bayer RGB format


Description
===========

This is commonly the native format of digital cameras, reflecting the
arrangement of sensors on the CCD device. Only one red, green or blue
value is given for each pixel. Missing components must be interpolated
from neighbouring pixels. From left to right the first row consists of a
green and blue value, the second row of a red and green value. This
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

       -  G\ :sub:`00`

       -  B\ :sub:`01`

       -  G\ :sub:`02`

       -  B\ :sub:`03`

    -  .. row 2

       -  start + 4:

       -  R\ :sub:`10`

       -  G\ :sub:`11`

       -  R\ :sub:`12`

       -  G\ :sub:`13`

    -  .. row 3

       -  start + 8:

       -  G\ :sub:`20`

       -  B\ :sub:`21`

       -  G\ :sub:`22`

       -  B\ :sub:`23`

    -  .. row 4

       -  start + 12:

       -  R\ :sub:`30`

       -  G\ :sub:`31`

       -  R\ :sub:`32`

       -  G\ :sub:`33`
