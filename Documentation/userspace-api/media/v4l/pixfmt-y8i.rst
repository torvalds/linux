.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _V4L2-PIX-FMT-Y8I:

*************************
V4L2_PIX_FMT_Y8I ('Y8I ')
*************************


Interleaved grey-scale image, e.g. from a stereo-pair


Description
===========

This is a grey-scale image with a depth of 8 bits per pixel, but with
pixels from 2 sources interleaved. Each pixel is stored in a 16-bit
word. E.g. the R200 RealSense camera stores pixel from the left sensor
in lower and from the right sensor in the higher 8 bits.

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00left`
      - Y'\ :sub:`00right`
      - Y'\ :sub:`01left`
      - Y'\ :sub:`01right`
      - Y'\ :sub:`02left`
      - Y'\ :sub:`02right`
      - Y'\ :sub:`03left`
      - Y'\ :sub:`03right`
    * - start + 8:
      - Y'\ :sub:`10left`
      - Y'\ :sub:`10right`
      - Y'\ :sub:`11left`
      - Y'\ :sub:`11right`
      - Y'\ :sub:`12left`
      - Y'\ :sub:`12right`
      - Y'\ :sub:`13left`
      - Y'\ :sub:`13right`
    * - start + 16:
      - Y'\ :sub:`20left`
      - Y'\ :sub:`20right`
      - Y'\ :sub:`21left`
      - Y'\ :sub:`21right`
      - Y'\ :sub:`22left`
      - Y'\ :sub:`22right`
      - Y'\ :sub:`23left`
      - Y'\ :sub:`23right`
    * - start + 24:
      - Y'\ :sub:`30left`
      - Y'\ :sub:`30right`
      - Y'\ :sub:`31left`
      - Y'\ :sub:`31right`
      - Y'\ :sub:`32left`
      - Y'\ :sub:`32right`
      - Y'\ :sub:`33left`
      - Y'\ :sub:`33right`
