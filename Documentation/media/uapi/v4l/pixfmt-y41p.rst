.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-Y41P:

**************************
V4L2_PIX_FMT_Y41P ('Y41P')
**************************


Format with ¼ horizontal chroma resolution, also known as YUV 4:1:1


Description
===========

In this format each 12 bytes is eight pixels. In the twelve bytes are
two CbCr pairs and eight Y's. The first CbCr pair goes with the first
four Y's, and the second CbCr pair goes with the other four Y's. The Cb
and Cr components have one fourth the horizontal resolution of the Y
component.

Do not confuse this format with
:ref:`V4L2_PIX_FMT_YUV411P <V4L2-PIX-FMT-YUV411P>`. Y41P is derived
from "YUV 4:1:1 *packed*", while YUV411P stands for "YUV 4:1:1
*planar*".

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Cb\ :sub:`00`
      - Y'\ :sub:`00`
      - Cr\ :sub:`00`
      - Y'\ :sub:`01`
      - Cb\ :sub:`01`
      - Y'\ :sub:`02`
      - Cr\ :sub:`01`
      - Y'\ :sub:`03`
      - Y'\ :sub:`04`
      - Y'\ :sub:`05`
      - Y'\ :sub:`06`
      - Y'\ :sub:`07`
    * - start + 12:
      - Cb\ :sub:`10`
      - Y'\ :sub:`10`
      - Cr\ :sub:`10`
      - Y'\ :sub:`11`
      - Cb\ :sub:`11`
      - Y'\ :sub:`12`
      - Cr\ :sub:`11`
      - Y'\ :sub:`13`
      - Y'\ :sub:`14`
      - Y'\ :sub:`15`
      - Y'\ :sub:`16`
      - Y'\ :sub:`17`
    * - start + 24:
      - Cb\ :sub:`20`
      - Y'\ :sub:`20`
      - Cr\ :sub:`20`
      - Y'\ :sub:`21`
      - Cb\ :sub:`21`
      - Y'\ :sub:`22`
      - Cr\ :sub:`21`
      - Y'\ :sub:`23`
      - Y'\ :sub:`24`
      - Y'\ :sub:`25`
      - Y'\ :sub:`26`
      - Y'\ :sub:`27`
    * - start + 36:
      - Cb\ :sub:`30`
      - Y'\ :sub:`30`
      - Cr\ :sub:`30`
      - Y'\ :sub:`31`
      - Cb\ :sub:`31`
      - Y'\ :sub:`32`
      - Cr\ :sub:`31`
      - Y'\ :sub:`33`
      - Y'\ :sub:`34`
      - Y'\ :sub:`35`
      - Y'\ :sub:`36`
      - Y'\ :sub:`37`


**Color Sample Location..**

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * -
      - 0
      - 1
      -
      - 2
      - 3
      - 4
      - 5
      -
      - 6
      - 7
    * - 0
      - Y
      - Y
      - C
      - Y
      - Y
      - Y
      - Y
      - C
      - Y
      - Y
    * - 1
      - Y
      - Y
      - C
      - Y
      - Y
      - Y
      - Y
      - C
      - Y
      - Y
    * - 2
      - Y
      - Y
      - C
      - Y
      - Y
      - Y
      - Y
      - C
      - Y
      - Y
    * - 3
      - Y
      - Y
      - C
      - Y
      - Y
      - Y
      - Y
      - C
      - Y
      - Y
