.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _V4L2-PIX-FMT-Y16-BE:

****************************************
V4L2_PIX_FMT_Y16_BE ('Y16 ' | (1 << 31))
****************************************


Grey-scale image


Description
===========

This is a grey-scale image with a depth of 16 bits per pixel. The most
significant byte is stored at lower memory addresses (big-endian).

.. note::

   The actual sampling precision may be lower than 16 bits, for
   example 10 bits per pixel with values in range 0 to 1023.

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00high`
      - Y'\ :sub:`00low`
      - Y'\ :sub:`01high`
      - Y'\ :sub:`01low`
      - Y'\ :sub:`02high`
      - Y'\ :sub:`02low`
      - Y'\ :sub:`03high`
      - Y'\ :sub:`03low`
    * - start + 8:
      - Y'\ :sub:`10high`
      - Y'\ :sub:`10low`
      - Y'\ :sub:`11high`
      - Y'\ :sub:`11low`
      - Y'\ :sub:`12high`
      - Y'\ :sub:`12low`
      - Y'\ :sub:`13high`
      - Y'\ :sub:`13low`
    * - start + 16:
      - Y'\ :sub:`20high`
      - Y'\ :sub:`20low`
      - Y'\ :sub:`21high`
      - Y'\ :sub:`21low`
      - Y'\ :sub:`22high`
      - Y'\ :sub:`22low`
      - Y'\ :sub:`23high`
      - Y'\ :sub:`23low`
    * - start + 24:
      - Y'\ :sub:`30high`
      - Y'\ :sub:`30low`
      - Y'\ :sub:`31high`
      - Y'\ :sub:`31low`
      - Y'\ :sub:`32high`
      - Y'\ :sub:`32low`
      - Y'\ :sub:`33high`
      - Y'\ :sub:`33low`
