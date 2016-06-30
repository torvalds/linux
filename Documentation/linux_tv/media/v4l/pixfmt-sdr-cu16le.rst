.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-SDR-FMT-CU16LE:

****************************
V4L2_SDR_FMT_CU16LE ('CU16')
****************************

*man V4L2_SDR_FMT_CU16LE(2)*

Complex unsigned 16-bit little endian IQ sample


Description
===========

This format contains sequence of complex number samples. Each complex
number consist two parts, called In-phase and Quadrature (IQ). Both I
and Q are represented as a 16 bit unsigned little endian number. I value
comes first and Q value after that.

**Byte Order..**

Each cell is one byte.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1


    -  .. row 1

       -  start + 0:

       -  I'\ :sub:`0[7:0]`

       -  I'\ :sub:`0[15:8]`

    -  .. row 2

       -  start + 2:

       -  Q'\ :sub:`0[7:0]`

       -  Q'\ :sub:`0[15:8]`




.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
