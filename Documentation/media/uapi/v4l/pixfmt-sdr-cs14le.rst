.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-SDR-FMT-CS14LE:

****************************
V4L2_SDR_FMT_CS14LE ('CS14')
****************************

*man V4L2_SDR_FMT_CS14LE(2)*

Complex signed 14-bit little endian IQ sample


Description
===========

This format contains sequence of complex number samples. Each complex
number consist two parts, called In-phase and Quadrature (IQ). Both I
and Q are represented as a 14 bit signed little endian number. I value
comes first and Q value after that. 14 bit value is stored in 16 bit
space with unused high bits padded with 0.

**Byte Order.**
Each cell is one byte.



.. tabularcolumns:: |p{8.8cm}|p{4.4cm}|p{4.3cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1


    -  .. row 1

       -  start + 0:

       -  I'\ :sub:`0[7:0]`

       -  I'\ :sub:`0[13:8]`

    -  .. row 2

       -  start + 2:

       -  Q'\ :sub:`0[7:0]`

       -  Q'\ :sub:`0[13:8]`
