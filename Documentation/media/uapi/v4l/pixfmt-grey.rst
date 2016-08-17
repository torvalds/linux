.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-GREY:

**************************
V4L2_PIX_FMT_GREY ('GREY')
**************************

*man V4L2_PIX_FMT_GREY(2)*

Grey-scale image


Description
===========

This is a grey-scale image. It is really a degenerate Y'CbCr format
which simply contains no Cb or Cr data.

**Byte Order.**
Each cell is one byte.



.. tabularcolumns:: |p{5.8cm}|p{2.9cm}|p{2.9cm}|p{2.9cm}|p{3.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1 1 1


    -  .. row 1

       -  start + 0:

       -  Y'\ :sub:`00`

       -  Y'\ :sub:`01`

       -  Y'\ :sub:`02`

       -  Y'\ :sub:`03`

    -  .. row 2

       -  start + 4:

       -  Y'\ :sub:`10`

       -  Y'\ :sub:`11`

       -  Y'\ :sub:`12`

       -  Y'\ :sub:`13`

    -  .. row 3

       -  start + 8:

       -  Y'\ :sub:`20`

       -  Y'\ :sub:`21`

       -  Y'\ :sub:`22`

       -  Y'\ :sub:`23`

    -  .. row 4

       -  start + 12:

       -  Y'\ :sub:`30`

       -  Y'\ :sub:`31`

       -  Y'\ :sub:`32`

       -  Y'\ :sub:`33`
