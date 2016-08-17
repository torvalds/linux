.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-SGRBG8:

****************************
V4L2_PIX_FMT_SGRBG8 ('GRBG')
****************************


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




.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  start + 0:

       -  G\ :sub:`00`

       -  R\ :sub:`01`

       -  G\ :sub:`02`

       -  R\ :sub:`03`

    -  .. row 2

       -  start + 4:

       -  B\ :sub:`10`

       -  G\ :sub:`11`

       -  B\ :sub:`12`

       -  G\ :sub:`13`

    -  .. row 3

       -  start + 8:

       -  G\ :sub:`20`

       -  R\ :sub:`21`

       -  G\ :sub:`22`

       -  R\ :sub:`23`

    -  .. row 4

       -  start + 12:

       -  B\ :sub:`30`

       -  G\ :sub:`31`

       -  B\ :sub:`32`

       -  G\ :sub:`33`
