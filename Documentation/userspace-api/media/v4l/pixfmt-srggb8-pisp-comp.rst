.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _v4l2-pix-fmt-pisp-comp1-rggb:
.. _v4l2-pix-fmt-pisp-comp1-grbg:
.. _v4l2-pix-fmt-pisp-comp1-gbrg:
.. _v4l2-pix-fmt-pisp-comp1-bggr:
.. _v4l2-pix-fmt-pisp-comp1-mono:
.. _v4l2-pix-fmt-pisp-comp2-rggb:
.. _v4l2-pix-fmt-pisp-comp2-grbg:
.. _v4l2-pix-fmt-pisp-comp2-gbrg:
.. _v4l2-pix-fmt-pisp-comp2-bggr:
.. _v4l2-pix-fmt-pisp-comp2-mono:

**************************************************************************************************************************************************************************************************************************************************************************************************************************************************************************************************
V4L2_PIX_FMT_PISP_COMP1_RGGB ('PC1R'), V4L2_PIX_FMT_PISP_COMP1_GRBG ('PC1G'), V4L2_PIX_FMT_PISP_COMP1_GBRG ('PC1g'), V4L2_PIX_FMT_PISP_COMP1_BGGR ('PC1B), V4L2_PIX_FMT_PISP_COMP1_MONO ('PC1M'), V4L2_PIX_FMT_PISP_COMP2_RGGB ('PC2R'), V4L2_PIX_FMT_PISP_COMP2_GRBG ('PC2G'), V4L2_PIX_FMT_PISP_COMP2_GBRG ('PC2g'), V4L2_PIX_FMT_PISP_COMP2_BGGR ('PC2B), V4L2_PIX_FMT_PISP_COMP2_MONO ('PC2M')
**************************************************************************************************************************************************************************************************************************************************************************************************************************************************************************************************

================================================
Raspberry Pi PiSP compressed 8-bit Bayer formats
================================================

Description
===========

The Raspberry Pi ISP (PiSP) uses a family of three fixed-rate compressed Bayer
formats. A black-level offset may be subtracted to improve compression
efficiency; the nominal black level and amount of offset must be signalled out
of band. Each scanline is padded to a multiple of 8 pixels wide, and each block
of 8 horizontally-contiguous pixels is coded using 8 bytes.

Mode 1 uses a quantization and delta-based coding scheme which preserves up to
12 significant bits. Mode 2 is a simple sqrt-like companding scheme with 6 PWL
chords, preserving up to 12 significant bits. Mode 3 combines both companding
(with 4 chords) and the delta scheme, preserving up to 14 significant bits.

The remainder of this description applies to Modes 1 and 3.

Each block of 8 pixels is separated into even and odd phases of 4 pixels,
coded independently by 32-bit words at successive locations in memory.
The two LS bits of each 32-bit word give its "quantization mode".

In quantization mode 0, the lowest 321 quantization levels are multiples of
FSD/4096 and the remaining levels are successive multiples of FSD/2048.
Quantization modes 1 and 2 use linear quantization with step sizes of
FSD/1024 and FSD/512 respectively. Each of the four pixels is quantized
independently, with rounding to the nearest level.
In quantization mode 2 where the middle two samples have quantized values
(q1,q2) both in the range [384..511], they are coded using 9 bits for q1
followed by 7 bits for (q2 & 127). Otherwise, for quantization modes
0, 1 and 2: a 9-bit field encodes MIN(q1,q2) which must be in the range
[0..511] and a 7-bit field encodes (q2-q1+64) which must be in [0..127].

Each of the outer samples (q0,q3) is encoded using a 7-bit field based
on its inner neighbour q1 or q2. In quantization mode 2 where the inner
sample has a quantized value in the range [448..511], the field value is
(q0-384). Otherwise for quantization modes 0, 1 and 2: The outer sample
is encoded as (q0-MAX(0,q1-64)). q3 is likewise coded based on q2.
Each of these values must be in the range [0..127]. All these fields
of 2, 9, 7, 7, 7 bits respectively are packed in little-endian order
to give a 32-bit word with LE byte order.

Quantization mode 3 has a "7.5-bit" escape, used when none of the above
encodings will fit. Each pixel value is quantized to the nearest of 176
levels, where the lowest 95 levels are multiples of FSD/256 and the
remaining levels are multiples of FSD/128 (level 175 represents values
very close to FSD and may require saturating arithmetic to decode).

Each pair of quantized pixels (q0,q1) or (q2,q3) is jointly coded
by a 15-bit field: 2816*(q0>>4) + 16*q1 + (q0&15).
Three fields of 2, 15, 15 bits are packed in LE order {15,15,2}.

An implementation of a software decoder of compressed formats is available
in `Raspberry Pi camera applications code base
<https://github.com/raspberrypi/rpicam-apps/blob/main/image/dng.cpp>`_.
