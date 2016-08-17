.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-SBGGR16:

*****************************
V4L2_PIX_FMT_SBGGR16 ('BYR2')
*****************************

*man V4L2_PIX_FMT_SBGGR16(2)*

Bayer RGB format


Description
===========

This format is similar to
:ref:`V4L2_PIX_FMT_SBGGR8 <V4L2-PIX-FMT-SBGGR8>`, except each pixel
has a depth of 16 bits. The least significant byte is stored at lower
memory addresses (little-endian).

..note::

    The actual sampling precision may be lower than 16 bits,
    for example 10 bits per pixel with values in tange 0 to 1023.

**Byte Order.**
Each cell is one byte.



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
