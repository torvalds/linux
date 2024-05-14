.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _V4L2-TCH-FMT-DELTA-TD16:

********************************
V4L2_TCH_FMT_DELTA_TD16 ('TD16')
********************************

*man V4L2_TCH_FMT_DELTA_TD16(2)*

16-bit signed little endian Touch Delta


Description
===========

This format represents delta data from a touch controller.

Delta values may range from -32768 to 32767. Typically the values will vary
through a small range depending on whether the sensor is touched or not. The
full value may be seen if one of the touchscreen nodes has a fault or the line
is not connected.

**Byte Order.**
Each cell is one byte.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1 1 1 1 1 1 1

    * - start + 0:
      - D'\ :sub:`00low`
      - D'\ :sub:`00high`
      - D'\ :sub:`01low`
      - D'\ :sub:`01high`
      - D'\ :sub:`02low`
      - D'\ :sub:`02high`
      - D'\ :sub:`03low`
      - D'\ :sub:`03high`
    * - start + 8:
      - D'\ :sub:`10low`
      - D'\ :sub:`10high`
      - D'\ :sub:`11low`
      - D'\ :sub:`11high`
      - D'\ :sub:`12low`
      - D'\ :sub:`12high`
      - D'\ :sub:`13low`
      - D'\ :sub:`13high`
    * - start + 16:
      - D'\ :sub:`20low`
      - D'\ :sub:`20high`
      - D'\ :sub:`21low`
      - D'\ :sub:`21high`
      - D'\ :sub:`22low`
      - D'\ :sub:`22high`
      - D'\ :sub:`23low`
      - D'\ :sub:`23high`
    * - start + 24:
      - D'\ :sub:`30low`
      - D'\ :sub:`30high`
      - D'\ :sub:`31low`
      - D'\ :sub:`31high`
      - D'\ :sub:`32low`
      - D'\ :sub:`32high`
      - D'\ :sub:`33low`
      - D'\ :sub:`33high`
