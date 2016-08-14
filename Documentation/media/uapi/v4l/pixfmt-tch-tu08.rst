.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-TCH-FMT-TU08:

**************************
V4L2_TCH_FMT_TU08 ('TU08')
**************************

*man V4L2_TCH_FMT_TU08(2)*

8-bit unsigned raw touch data

Description
===========

This format represents unsigned 8-bit data from a touch controller.

This may be used for output for raw and reference data. Values may range from
0 to 255.

**Byte Order.**
Each cell is one byte.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1 1 1


    -  .. row 1

       -  start + 0:

       -  R'\ :sub:`00`

       -  R'\ :sub:`01`

       -  R'\ :sub:`02`

       -  R'\ :sub:`03`

    -  .. row 2

       -  start + 4:

       -  R'\ :sub:`10`

       -  R'\ :sub:`11`

       -  R'\ :sub:`12`

       -  R'\ :sub:`13`

    -  .. row 3

       -  start + 8:

       -  R'\ :sub:`20`

       -  R'\ :sub:`21`

       -  R'\ :sub:`22`

       -  R'\ :sub:`23`

    -  .. row 4

       -  start + 12:

       -  R'\ :sub:`30`

       -  R'\ :sub:`31`

       -  R'\ :sub:`32`

       -  R'\ :sub:`33`
