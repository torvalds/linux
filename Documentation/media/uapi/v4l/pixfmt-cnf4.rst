.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-CNF4:

******************************
V4L2_PIX_FMT_CNF4 ('CNF4')
******************************

Depth sensor confidence information as a 4 bits per pixel packed array

Description
===========

Proprietary format used by Intel RealSense Depth cameras containing depth
confidence information in range 0-15 with 0 indicating that the sensor was
unable to resolve any signal and 15 indicating maximum level of confidence for
the specific sensor (actual error margins might change from sensor to sensor).

Every two consecutive pixels are packed into a single byte.
Bits 0-3 of byte n refer to confidence value of depth pixel 2*n,
bits 4-7 to confidence value of depth pixel 2*n+1.

**Bit-packed representation.**

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths: 64 64

    * - Y'\ :sub:`01[3:0]`\ (bits 7--4) Y'\ :sub:`00[3:0]`\ (bits 3--0)
      - Y'\ :sub:`03[3:0]`\ (bits 7--4) Y'\ :sub:`02[3:0]`\ (bits 3--0)
