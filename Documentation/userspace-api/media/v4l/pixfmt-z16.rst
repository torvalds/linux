.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _V4L2-PIX-FMT-Z16:

*************************
V4L2_PIX_FMT_Z16 ('Z16 ')
*************************


16-bit depth data with distance values at each pixel


Description
===========

This is a 16-bit format, representing depth data. Each pixel is a
distance to the respective point in the image coordinates. Distance unit
can vary and has to be negotiated with the device separately. Each pixel
is stored in a 16-bit word in the little endian byte order.

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Z\ :sub:`00low`
      - Z\ :sub:`00high`
      - Z\ :sub:`01low`
      - Z\ :sub:`01high`
      - Z\ :sub:`02low`
      - Z\ :sub:`02high`
      - Z\ :sub:`03low`
      - Z\ :sub:`03high`
    * - start + 8:
      - Z\ :sub:`10low`
      - Z\ :sub:`10high`
      - Z\ :sub:`11low`
      - Z\ :sub:`11high`
      - Z\ :sub:`12low`
      - Z\ :sub:`12high`
      - Z\ :sub:`13low`
      - Z\ :sub:`13high`
    * - start + 16:
      - Z\ :sub:`20low`
      - Z\ :sub:`20high`
      - Z\ :sub:`21low`
      - Z\ :sub:`21high`
      - Z\ :sub:`22low`
      - Z\ :sub:`22high`
      - Z\ :sub:`23low`
      - Z\ :sub:`23high`
    * - start + 24:
      - Z\ :sub:`30low`
      - Z\ :sub:`30high`
      - Z\ :sub:`31low`
      - Z\ :sub:`31high`
      - Z\ :sub:`32low`
      - Z\ :sub:`32high`
      - Z\ :sub:`33low`
      - Z\ :sub:`33high`
