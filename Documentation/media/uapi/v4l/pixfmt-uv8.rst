.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-UV8:

************************
V4L2_PIX_FMT_UV8 ('UV8')
************************

*man V4L2_PIX_FMT_UV8(2)*

UV plane interleaved


Description
===========

In this format there is no Y plane, Only CbCr plane. ie (UV interleaved)

**Byte Order.**
Each cell is one byte.



.. tabularcolumns:: |p{5.8cm}|p{2.9cm}|p{2.9cm}|p{2.9cm}|p{3.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1 1 1


    -  .. row 1

       -  start + 0:

       -  Cb\ :sub:`00`

       -  Cr\ :sub:`00`

       -  Cb\ :sub:`01`

       -  Cr\ :sub:`01`

    -  .. row 2

       -  start + 4:

       -  Cb\ :sub:`10`

       -  Cr\ :sub:`10`

       -  Cb\ :sub:`11`

       -  Cr\ :sub:`11`

    -  .. row 3

       -  start + 8:

       -  Cb\ :sub:`20`

       -  Cr\ :sub:`20`

       -  Cb\ :sub:`21`

       -  Cr\ :sub:`21`

    -  .. row 4

       -  start + 12:

       -  Cb\ :sub:`30`

       -  Cr\ :sub:`30`

       -  Cb\ :sub:`31`

       -  Cr\ :sub:`31`
