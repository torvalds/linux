.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _V4L2-PIX-FMT-Y16I:

**************************
V4L2_PIX_FMT_Y16I ('Y16I')
**************************

Interleaved grey-scale image, e.g. from a stereo-pair


Description
===========

This is a grey-scale image with a depth of 16 bits per pixel, but with pixels
from 2 sources interleaved and unpacked. Each pixel is stored in a 16-bit word
in the little-endian order. The first pixel is from the left source.

**Pixel unpacked representation.**
Left/Right pixels 16-bit unpacked - 16-bit for each interleaved pixel.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - Y'\ :sub:`0L[7:0]`
      - Y'\ :sub:`0L[15:8]`
      - Y'\ :sub:`0R[7:0]`
      - Y'\ :sub:`0R[15:8]`

**Byte Order.**
Each cell is one byte.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00Llow`
      - Y'\ :sub:`00Lhigh`
      - Y'\ :sub:`00Rlow`
      - Y'\ :sub:`00Rhigh`
      - Y'\ :sub:`01Llow`
      - Y'\ :sub:`01Lhigh`
      - Y'\ :sub:`01Rlow`
      - Y'\ :sub:`01Rhigh`
    * - start + 8:
      - Y'\ :sub:`10Llow`
      - Y'\ :sub:`10Lhigh`
      - Y'\ :sub:`10Rlow`
      - Y'\ :sub:`10Rhigh`
      - Y'\ :sub:`11Llow`
      - Y'\ :sub:`11Lhigh`
      - Y'\ :sub:`11Rlow`
      - Y'\ :sub:`11Rhigh`
    * - start + 16:
      - Y'\ :sub:`20Llow`
      - Y'\ :sub:`20Lhigh`
      - Y'\ :sub:`20Rlow`
      - Y'\ :sub:`20Rhigh`
      - Y'\ :sub:`21Llow`
      - Y'\ :sub:`21Lhigh`
      - Y'\ :sub:`21Rlow`
      - Y'\ :sub:`21Rhigh`
    * - start + 24:
      - Y'\ :sub:`30Llow`
      - Y'\ :sub:`30Lhigh`
      - Y'\ :sub:`30Rlow`
      - Y'\ :sub:`30Rhigh`
      - Y'\ :sub:`31Llow`
      - Y'\ :sub:`31Lhigh`
      - Y'\ :sub:`31Rlow`
      - Y'\ :sub:`31Rhigh`
