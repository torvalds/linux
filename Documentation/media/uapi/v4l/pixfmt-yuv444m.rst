.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-YUV444M:
.. _v4l2-pix-fmt-yvu444m:

************************************************************
V4L2_PIX_FMT_YUV444M ('YM24'), V4L2_PIX_FMT_YVU444M ('YM42')
************************************************************


V4L2_PIX_FMT_YVU444M
Planar formats with full horizontal resolution, also known as YUV and
YVU 4:4:4


Description
===========

This is a multi-planar format, as opposed to a packed format. The three
components are separated into three sub-images or planes.

The Y plane is first. The Y plane has one byte per pixel. For
``V4L2_PIX_FMT_YUV444M`` the Cb data constitutes the second plane which
is the same width and height as the Y plane (and as the image). The Cr
data, just like the Cb plane, is in the third plane.

``V4L2_PIX_FMT_YVU444M`` is the same except the Cr data is stored in the
second plane and the Cb data in the third plane.

If the Y plane has pad bytes after each row, then the Cb and Cr planes
have the same number of pad bytes after their rows.

``V4L2_PIX_FMT_YUV444M`` and ``V4L2_PIX_FMT_YUV444M`` are intended to be
used only in drivers and applications that support the multi-planar API,
described in :ref:`planar-apis`.

**Byte Order.**
Each cell is one byte.


.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  start0 + 0:

       -  Y'\ :sub:`00`

       -  Y'\ :sub:`01`

       -  Y'\ :sub:`02`

       -  Y'\ :sub:`03`

    -  .. row 2

       -  start0 + 4:

       -  Y'\ :sub:`10`

       -  Y'\ :sub:`11`

       -  Y'\ :sub:`12`

       -  Y'\ :sub:`13`

    -  .. row 3

       -  start0 + 8:

       -  Y'\ :sub:`20`

       -  Y'\ :sub:`21`

       -  Y'\ :sub:`22`

       -  Y'\ :sub:`23`

    -  .. row 4

       -  start0 + 12:

       -  Y'\ :sub:`30`

       -  Y'\ :sub:`31`

       -  Y'\ :sub:`32`

       -  Y'\ :sub:`33`

    -  .. row 5

       -

    -  .. row 6

       -  start1 + 0:

       -  Cb\ :sub:`00`

       -  Cb\ :sub:`01`

       -  Cb\ :sub:`02`

       -  Cb\ :sub:`03`

    -  .. row 7

       -  start1 + 4:

       -  Cb\ :sub:`10`

       -  Cb\ :sub:`11`

       -  Cb\ :sub:`12`

       -  Cb\ :sub:`13`

    -  .. row 8

       -  start1 + 8:

       -  Cb\ :sub:`20`

       -  Cb\ :sub:`21`

       -  Cb\ :sub:`22`

       -  Cb\ :sub:`23`

    -  .. row 9

       -  start1 + 12:

       -  Cb\ :sub:`20`

       -  Cb\ :sub:`21`

       -  Cb\ :sub:`32`

       -  Cb\ :sub:`33`

    -  .. row 10

       -

    -  .. row 11

       -  start2 + 0:

       -  Cr\ :sub:`00`

       -  Cr\ :sub:`01`

       -  Cr\ :sub:`02`

       -  Cr\ :sub:`03`

    -  .. row 12

       -  start2 + 4:

       -  Cr\ :sub:`10`

       -  Cr\ :sub:`11`

       -  Cr\ :sub:`12`

       -  Cr\ :sub:`13`

    -  .. row 13

       -  start2 + 8:

       -  Cr\ :sub:`20`

       -  Cr\ :sub:`21`

       -  Cr\ :sub:`22`

       -  Cr\ :sub:`23`

    -  .. row 14

       -  start2 + 12:

       -  Cr\ :sub:`30`

       -  Cr\ :sub:`31`

       -  Cr\ :sub:`32`

       -  Cr\ :sub:`33`


**Color Sample Location..**



.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -
       -  0

       -  1

       -  2

       -  3

    -  .. row 2

       -  0

       -  YC

       -  YC

       -  YC

       -  YC

    -  .. row 3

       -  1

       -  YC

       -  YC

       -  YC

       -  YC

    -  .. row 4

       -  2

       -  YC

       -  YC

       -  YC

       -  YC

    -  .. row 5

       -  3

       -  YC

       -  YC

       -  YC

       -  YC
