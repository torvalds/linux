.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-SDR-FMT-RU12LE:

****************************
V4L2_SDR_FMT_RU12LE ('RU12')
****************************


Real unsigned 12-bit little endian sample


Description
===========

This format contains sequence of real number samples. Each sample is
represented as a 12 bit unsigned little endian number. Sample is stored
in 16 bit space with unused high bits padded with 0.

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - I'\ :sub:`0[7:0]`
      - I'\ :sub:`0[11:8]`
