.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _V4L2-PIX-FMT-NV12MT:

****************************
V4L2_PIX_FMT_NV12MT ('TM12')
****************************

Formats with ½ horizontal and vertical chroma resolution. This format
has two planes - one for luminance and one for chrominance. Chroma
samples are interleaved. The difference to ``V4L2_PIX_FMT_NV12`` is the
memory layout. Pixels are grouped in macroblocks of 64x32 size. The
order of macroblocks in memory is also not standard.


Description
===========

This is the two-plane versions of the YUV 4:2:0 format where data is
grouped into 64x32 macroblocks. The three components are separated into
two sub-images or planes. The Y plane has one byte per pixel and pixels
are grouped into 64x32 macroblocks. The CbCr plane has the same width,
in bytes, as the Y plane (and the image), but is half as tall in pixels.
The chroma plane is also grouped into 64x32 macroblocks.

Width of the buffer has to be aligned to the multiple of 128, and height
alignment is 32. Every four adjacent buffers - two horizontally and two
vertically are grouped together and are located in memory in Z or
flipped Z order.

Layout of macroblocks in memory is presented in the following figure.


.. _nv12mt:

.. kernel-figure:: nv12mt.svg
    :alt:    nv12mt.svg
    :align:  center

    V4L2_PIX_FMT_NV12MT macroblock Z shape memory layout

The requirement that width is multiple of 128 is implemented because,
the Z shape cannot be cut in half horizontally. In case the vertical
resolution of macroblocks is odd then the last row of macroblocks is
arranged in a linear order.

In case of chroma the layout is identical. Cb and Cr samples are
interleaved. Height of the buffer is aligned to 32.


.. _nv12mt_ex:

.. kernel-figure:: nv12mt_example.svg
    :alt:    nv12mt_example.svg
    :align:  center

    Example V4L2_PIX_FMT_NV12MT memory layout of macroblocks

Memory layout of macroblocks of ``V4L2_PIX_FMT_NV12MT`` format in most
extreme case.
