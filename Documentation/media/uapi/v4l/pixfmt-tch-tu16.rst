.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-TCH-FMT-TU16:

********************************
V4L2_TCH_FMT_TU16 ('TU16')
********************************

*man V4L2_TCH_FMT_TU16(2)*

16-bit unsigned raw touch data


Description
===========

This format represents unsigned 16-bit data from a touch controller.

This may be used for output for raw and reference data. Values may range from
0 to 65535.

**Byte Order.**
Each cell is one byte.


.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1 1 1 1 1 1 1


    -  .. row 1

       -  start + 0:

       -  R'\ :sub:`00high`

       -  R'\ :sub:`00low`

       -  R'\ :sub:`01high`

       -  R'\ :sub:`01low`

       -  R'\ :sub:`02high`

       -  R'\ :sub:`02low`

       -  R'\ :sub:`03high`

       -  R'\ :sub:`03low`

    -  .. row 2

       -  start + 8:

       -  R'\ :sub:`10high`

       -  R'\ :sub:`10low`

       -  R'\ :sub:`11high`

       -  R'\ :sub:`11low`

       -  R'\ :sub:`12high`

       -  R'\ :sub:`12low`

       -  R'\ :sub:`13high`

       -  R'\ :sub:`13low`

    -  .. row 3

       -  start + 16:

       -  R'\ :sub:`20high`

       -  R'\ :sub:`20low`

       -  R'\ :sub:`21high`

       -  R'\ :sub:`21low`

       -  R'\ :sub:`22high`

       -  R'\ :sub:`22low`

       -  R'\ :sub:`23high`

       -  R'\ :sub:`23low`

    -  .. row 4

       -  start + 24:

       -  R'\ :sub:`30high`

       -  R'\ :sub:`30low`

       -  R'\ :sub:`31high`

       -  R'\ :sub:`31low`

       -  R'\ :sub:`32high`

       -  R'\ :sub:`32low`

       -  R'\ :sub:`33high`

       -  R'\ :sub:`33low`
