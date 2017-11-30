.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-SDR-FMT-PCU18BE:

******************************
V4L2_SDR_FMT_PCU18BE ('PC18')
******************************

Planar complex unsigned 18-bit big endian IQ sample

Description
===========

This format contains a sequence of complex number samples. Each complex
number consist of two parts called In-phase and Quadrature (IQ). Both I
and Q are represented as a 18 bit unsigned big endian number stored in
32 bit space. The remaining unused bits within the 32 bit space will be
padded with 0. I value starts first and Q value starts at an offset
equalling half of the buffer size (i.e.) offset = buffersize/2. Out of
the 18 bits, bit 17:2 (16 bit) is data and bit 1:0 (2 bit) can be any
value.

**Byte Order.**
Each cell is one byte.

.. flat-table::
    :header-rows:  1
    :stub-columns: 0

    * -  Offset:
      -  Byte B0
      -  Byte B1
      -  Byte B2
      -  Byte B3
    * -  start + 0:
      -  I'\ :sub:`0[17:10]`
      -  I'\ :sub:`0[9:2]`
      -  I'\ :sub:`0[1:0]; B2[5:0]=pad`
      -  pad
    * -  start + 4:
      -  I'\ :sub:`1[17:10]`
      -  I'\ :sub:`1[9:2]`
      -  I'\ :sub:`1[1:0]; B2[5:0]=pad`
      -  pad
    * -  ...
    * - start + offset:
      -  Q'\ :sub:`0[17:10]`
      -  Q'\ :sub:`0[9:2]`
      -  Q'\ :sub:`0[1:0]; B2[5:0]=pad`
      -  pad
    * - start + offset + 4:
      -  Q'\ :sub:`1[17:10]`
      -  Q'\ :sub:`1[9:2]`
      -  Q'\ :sub:`1[1:0]; B2[5:0]=pad`
      -  pad
