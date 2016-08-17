.. -*- coding: utf-8; mode: rst -*-

.. _v4l2-sdr-fmt-cu8:

*************************
V4L2_SDR_FMT_CU8 ('CU08')
*************************

Complex unsigned 8-bit IQ sample


Description
===========

This format contains sequence of complex number samples. Each complex
number consist two parts, called In-phase and Quadrature (IQ). Both I
and Q are represented as a 8 bit unsigned number. I value comes first
and Q value after that.

**Byte Order.**
Each cell is one byte.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  .. row 1

       -  start + 0:

       -  I'\ :sub:`0`

    -  .. row 2

       -  start + 1:

       -  Q'\ :sub:`0`
