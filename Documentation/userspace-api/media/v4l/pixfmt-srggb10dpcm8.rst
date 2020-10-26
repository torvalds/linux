.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _V4L2-PIX-FMT-SBGGR10DPCM8:
.. _v4l2-pix-fmt-sgbrg10dpcm8:
.. _v4l2-pix-fmt-sgrbg10dpcm8:
.. _v4l2-pix-fmt-srggb10dpcm8:


***********************************************************************************************************************************************
V4L2_PIX_FMT_SBGGR10DPCM8 ('bBA8'), V4L2_PIX_FMT_SGBRG10DPCM8 ('bGA8'), V4L2_PIX_FMT_SGRBG10DPCM8 ('BD10'), V4L2_PIX_FMT_SRGGB10DPCM8 ('bRA8'),
***********************************************************************************************************************************************

*man V4L2_PIX_FMT_SBGGR10DPCM8(2)*

V4L2_PIX_FMT_SGBRG10DPCM8
V4L2_PIX_FMT_SGRBG10DPCM8
V4L2_PIX_FMT_SRGGB10DPCM8
10-bit Bayer formats compressed to 8 bits


Description
===========

These four pixel formats are raw sRGB / Bayer formats with 10 bits per
colour compressed to 8 bits each, using DPCM compression. DPCM,
differential pulse-code modulation, is lossy. Each colour component
consumes 8 bits of memory. In other respects this format is similar to
:ref:`V4L2-PIX-FMT-SRGGB10`.
