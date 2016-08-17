.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-Y16:

*************************
V4L2_PIX_FMT_Y16 ('Y16 ')
*************************


Grey-scale image


Description
===========

This is a grey-scale image with a depth of 16 bits per pixel. The least
significant byte is stored at lower memory addresses (little-endian).

.. note::

   The actual sampling precision may be lower than 16 bits, for
   example 10 bits per pixel with values in range 0 to 1023.

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  start + 0:

       -  Y'\ :sub:`00low`

       -  Y'\ :sub:`00high`

       -  Y'\ :sub:`01low`

       -  Y'\ :sub:`01high`

       -  Y'\ :sub:`02low`

       -  Y'\ :sub:`02high`

       -  Y'\ :sub:`03low`

       -  Y'\ :sub:`03high`

    -  .. row 2

       -  start + 8:

       -  Y'\ :sub:`10low`

       -  Y'\ :sub:`10high`

       -  Y'\ :sub:`11low`

       -  Y'\ :sub:`11high`

       -  Y'\ :sub:`12low`

       -  Y'\ :sub:`12high`

       -  Y'\ :sub:`13low`

       -  Y'\ :sub:`13high`

    -  .. row 3

       -  start + 16:

       -  Y'\ :sub:`20low`

       -  Y'\ :sub:`20high`

       -  Y'\ :sub:`21low`

       -  Y'\ :sub:`21high`

       -  Y'\ :sub:`22low`

       -  Y'\ :sub:`22high`

       -  Y'\ :sub:`23low`

       -  Y'\ :sub:`23high`

    -  .. row 4

       -  start + 24:

       -  Y'\ :sub:`30low`

       -  Y'\ :sub:`30high`

       -  Y'\ :sub:`31low`

       -  Y'\ :sub:`31high`

       -  Y'\ :sub:`32low`

       -  Y'\ :sub:`32high`

       -  Y'\ :sub:`33low`

       -  Y'\ :sub:`33high`
