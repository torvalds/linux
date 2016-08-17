.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-NV24:
.. _V4L2-PIX-FMT-NV42:

******************************************************
V4L2_PIX_FMT_NV24 ('NV24'), V4L2_PIX_FMT_NV42 ('NV42')
******************************************************

*man V4L2_PIX_FMT_NV24(2)*

V4L2_PIX_FMT_NV42
Formats with full horizontal and vertical chroma resolutions, also known
as YUV 4:4:4. One luminance and one chrominance plane with alternating
chroma samples as opposed to ``V4L2_PIX_FMT_YVU420``


Description
===========

These are two-plane versions of the YUV 4:4:4 format. The three
components are separated into two sub-images or planes. The Y plane is
first, with each Y sample stored in one byte per pixel. For
``V4L2_PIX_FMT_NV24``, a combined CbCr plane immediately follows the Y
plane in memory. The CbCr plane has the same width and height, in
pixels, as the Y plane (and the image). Each line contains one CbCr pair
per pixel, with each Cb and Cr sample stored in one byte.
``V4L2_PIX_FMT_NV42`` is the same except that the Cb and Cr samples are
swapped, the CrCb plane starts with a Cr sample.

If the Y plane has pad bytes after each row, then the CbCr plane has
twice as many pad bytes after its rows.

**Byte Order.**
Each cell is one byte.



.. tabularcolumns:: |p{3.5cm}|p{1.8cm}|p{1.8cm}|p{1.8cm}|p{1.8cm}|p{1.8cm}|p{1.8cm}|p{1.8cm}|p{1.4cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1 1 1 1 1 1 1


    -  .. row 1

       -  start + 0:

       -  Y'\ :sub:`00`

       -  Y'\ :sub:`01`

       -  Y'\ :sub:`02`

       -  Y'\ :sub:`03`

    -  .. row 2

       -  start + 4:

       -  Y'\ :sub:`10`

       -  Y'\ :sub:`11`

       -  Y'\ :sub:`12`

       -  Y'\ :sub:`13`

    -  .. row 3

       -  start + 8:

       -  Y'\ :sub:`20`

       -  Y'\ :sub:`21`

       -  Y'\ :sub:`22`

       -  Y'\ :sub:`23`

    -  .. row 4

       -  start + 12:

       -  Y'\ :sub:`30`

       -  Y'\ :sub:`31`

       -  Y'\ :sub:`32`

       -  Y'\ :sub:`33`

    -  .. row 5

       -  start + 16:

       -  Cb\ :sub:`00`

       -  Cr\ :sub:`00`

       -  Cb\ :sub:`01`

       -  Cr\ :sub:`01`

       -  Cb\ :sub:`02`

       -  Cr\ :sub:`02`

       -  Cb\ :sub:`03`

       -  Cr\ :sub:`03`

    -  .. row 6

       -  start + 24:

       -  Cb\ :sub:`10`

       -  Cr\ :sub:`10`

       -  Cb\ :sub:`11`

       -  Cr\ :sub:`11`

       -  Cb\ :sub:`12`

       -  Cr\ :sub:`12`

       -  Cb\ :sub:`13`

       -  Cr\ :sub:`13`

    -  .. row 7

       -  start + 32:

       -  Cb\ :sub:`20`

       -  Cr\ :sub:`20`

       -  Cb\ :sub:`21`

       -  Cr\ :sub:`21`

       -  Cb\ :sub:`22`

       -  Cr\ :sub:`22`

       -  Cb\ :sub:`23`

       -  Cr\ :sub:`23`

    -  .. row 8

       -  start + 40:

       -  Cb\ :sub:`30`

       -  Cr\ :sub:`30`

       -  Cb\ :sub:`31`

       -  Cr\ :sub:`31`

       -  Cb\ :sub:`32`

       -  Cr\ :sub:`32`

       -  Cb\ :sub:`33`

       -  Cr\ :sub:`33`
