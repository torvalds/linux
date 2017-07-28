.. -*- coding: utf-8; mode: rst -*-

***************************************
Detailed Transfer Function Descriptions
***************************************


.. _xf-smpte-2084:

Transfer Function SMPTE 2084 (V4L2_XFER_FUNC_SMPTE2084)
=======================================================

The :ref:`smpte2084` standard defines the transfer function used by
High Dynamic Range content.

Constants:
    m1 = (2610 / 4096) / 4

    m2 = (2523 / 4096) * 128

    c1 = 3424 / 4096

    c2 = (2413 / 4096) * 32

    c3 = (2392 / 4096) * 32

Transfer function:
    L' = ((c1 + c2 * L\ :sup:`m1`) / (1 + c3 * L\ :sup:`m1`))\ :sup:`m2`

Inverse Transfer function:
    L = (max(L':sup:`1/m2` - c1, 0) / (c2 - c3 *
    L'\ :sup:`1/m2`))\ :sup:`1/m1`
