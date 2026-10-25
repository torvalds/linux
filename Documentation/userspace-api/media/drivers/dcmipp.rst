.. SPDX-License-Identifier: GPL-2.0-only

ST DCMIPP driver
================

The ST DCMIPP driver implements a driver specific control as part of its
pixelproc subdev.

``V4L2_CID_DCMIPP_PIXELPROC_GAMMA_CORRECTION_ENABLE (boolean)``
    Enable / disable the gamma correction block.

    The DCMIPP PixelProc stage implements a gamma compression on each R, G, B
    component, using a static gamma exponent 2.2.
    The gamma is implemented as a 7-segment linear curve.
