.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _v4l2-sdr-fmt-cs8:

*************************
V4L2_SDR_FMT_CS8 ('CS08')
*************************

Complex signed 8-bit IQ sample


Description
===========

This format contains sequence of complex number samples. Each complex
number consist two parts, called In-phase and Quadrature (IQ). Both I
and Q are represented as a 8 bit signed number. I value comes first and
Q value after that.

**Byte Order.**
Each cell is one byte.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - I'\ :sub:`0`
    * - start + 1:
      - Q'\ :sub:`0`
