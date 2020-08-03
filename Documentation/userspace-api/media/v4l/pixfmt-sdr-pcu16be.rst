.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _V4L2-SDR-FMT-PCU16BE:

******************************
V4L2_SDR_FMT_PCU16BE ('PC16')
******************************

Planar complex unsigned 16-bit big endian IQ sample

Description
===========

This format contains a sequence of complex number samples. Each complex
number consist of two parts called In-phase and Quadrature (IQ). Both I
and Q are represented as a 16 bit unsigned big endian number stored in
32 bit space. The remaining unused bits within the 32 bit space will be
padded with 0. I value starts first and Q value starts at an offset
equalling half of the buffer size (i.e.) offset = buffersize/2. Out of
the 16 bits, bit 15:2 (14 bit) is data and bit 1:0 (2 bit) can be any
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
      -  I'\ :sub:`0[13:6]`
      -  I'\ :sub:`0[5:0]; B1[1:0]=pad`
      -  pad
      -  pad
    * -  start + 4:
      -  I'\ :sub:`1[13:6]`
      -  I'\ :sub:`1[5:0]; B1[1:0]=pad`
      -  pad
      -  pad
    * -  ...
    * - start + offset:
      -  Q'\ :sub:`0[13:6]`
      -  Q'\ :sub:`0[5:0]; B1[1:0]=pad`
      -  pad
      -  pad
    * - start + offset + 4:
      -  Q'\ :sub:`1[13:6]`
      -  Q'\ :sub:`1[5:0]; B1[1:0]=pad`
      -  pad
      -  pad
