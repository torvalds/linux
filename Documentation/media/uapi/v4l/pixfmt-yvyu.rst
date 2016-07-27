.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-YVYU:

**************************
V4L2_PIX_FMT_YVYU ('YVYU')
**************************

*man V4L2_PIX_FMT_YVYU(2)*

Variation of ``V4L2_PIX_FMT_YUYV`` with different order of samples in
memory


Description
===========

In this format each four bytes is two pixels. Each four bytes is two
Y's, a Cb and a Cr. Each Y goes to one of the pixels, and the Cb and Cr
belong to both pixels. As you can see, the Cr and Cb components have
half the horizontal resolution of the Y component.

**Byte Order.**
Each cell is one byte.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1 1 1 1 1 1 1


    -  .. row 1

       -  start + 0:

       -  Y'\ :sub:`00`

       -  Cr\ :sub:`00`

       -  Y'\ :sub:`01`

       -  Cb\ :sub:`00`

       -  Y'\ :sub:`02`

       -  Cr\ :sub:`01`

       -  Y'\ :sub:`03`

       -  Cb\ :sub:`01`

    -  .. row 2

       -  start + 8:

       -  Y'\ :sub:`10`

       -  Cr\ :sub:`10`

       -  Y'\ :sub:`11`

       -  Cb\ :sub:`10`

       -  Y'\ :sub:`12`

       -  Cr\ :sub:`11`

       -  Y'\ :sub:`13`

       -  Cb\ :sub:`11`

    -  .. row 3

       -  start + 16:

       -  Y'\ :sub:`20`

       -  Cr\ :sub:`20`

       -  Y'\ :sub:`21`

       -  Cb\ :sub:`20`

       -  Y'\ :sub:`22`

       -  Cr\ :sub:`21`

       -  Y'\ :sub:`23`

       -  Cb\ :sub:`21`

    -  .. row 4

       -  start + 24:

       -  Y'\ :sub:`30`

       -  Cr\ :sub:`30`

       -  Y'\ :sub:`31`

       -  Cb\ :sub:`30`

       -  Y'\ :sub:`32`

       -  Cr\ :sub:`31`

       -  Y'\ :sub:`33`

       -  Cb\ :sub:`31`


**Color Sample Location..**

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -
       -  0

       -
       -  1

       -  2

       -
       -  3

    -  .. row 2

       -  0

       -  Y

       -  C

       -  Y

       -  Y

       -  C

       -  Y

    -  .. row 3

       -  1

       -  Y

       -  C

       -  Y

       -  Y

       -  C

       -  Y

    -  .. row 4

       -  2

       -  Y

       -  C

       -  Y

       -  Y

       -  C

       -  Y

    -  .. row 5

       -  3

       -  Y

       -  C

       -  Y

       -  Y

       -  C

       -  Y
