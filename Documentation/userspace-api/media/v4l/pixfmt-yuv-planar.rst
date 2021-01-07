.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. planar-yuv:

******************
Planar YUV formats
******************

Planar formats split luma and chroma data in separate memory regions. They
exist in two variants:

- Semi-planar formats use two planes. The first plane is the luma plane and
  stores the Y components. The second plane is the chroma plane and stores the
  Cb and Cr components interleaved.

- Fully planar formats use three planes to store the Y, Cb and Cr components
  separately.

Within a plane, components are stored in pixel order, which may be linear or
tiled. Padding may be supported at the end of the lines, and the line stride of
the chroma planes may be constrained by the line stride of the luma plane.

Some planar formats allow planes to be placed in independent memory locations.
They are identified by an 'M' suffix in their name (such as in
``V4L2_PIX_FMT_NV12M``). Those formats are intended to be used only in drivers
and applications that support the multi-planar API, described in
:ref:`planar-apis`. Unless explicitly documented as supporting non-contiguous
planes, formats require the planes to follow each other immediately in memory.


Semi-Planar YUV Formats
=======================

These formats are commonly referred to as NV formats (NV12, NV16, ...). They
use two planes, and store the luma components in the first plane and the chroma
components in the second plane. The Cb and Cr components are interleaved in the
chroma plane, with Cb and Cr always stored in pairs. The chroma order is
exposed as different formats.

For memory contiguous formats, the number of padding pixels at the end of the
chroma lines is identical to the padding of the luma lines. Without horizontal
subsampling, the chroma line stride (in bytes) is thus equal to twice the luma
line stride. With horizontal subsampling by 2, the chroma line stride is equal
to the luma line stride. Vertical subsampling doesn't affect the line stride.

For non-contiguous formats, no constraints are enforced by the format on the
relationship between the luma and chroma line padding and stride.

All components are stored with the same number of bits per component.

.. flat-table:: Overview of Semi-Planar YUV Formats
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Code
      - Bits per component
      - Subsampling
      - Chroma order [1]_
      - Contiguous [2]_
      - Tiling [3]_
    * - V4L2_PIX_FMT_NV12
      - 'NV12'
      - 8
      - 4:2:0
      - Cb, Cr
      - Yes
      - Linear
    * - V4L2_PIX_FMT_NV21
      - 'NV21'
      - 8
      - 4:2:0
      - Cr, Cr
      - Yes
      - Linear
    * - V4L2_PIX_FMT_NV12M
      - 'NM12'
      - 8
      - 4:2:0
      - Cb, Cr
      - No
      - Linear
    * - V4L2_PIX_FMT_NV21M
      - 'NM21'
      - 8
      - 4:2:0
      - Cr, Cr
      - No
      - Linear
    * - V4L2_PIX_FMT_NV12MT
      - 'TM12'
      - 8
      - 4:2:0
      - Cb, Cr
      - No
      - 64x32 macroblocks

        Horizontal Z order
    * - V4L2_PIX_FMT_NV12MT_16X16
      - 'VM12'
      - 8
      - 4:2:2
      - Cb, Cr
      - No
      - 16x16 macroblocks
    * - V4L2_PIX_FMT_NV16
      - 'NV16'
      - 8
      - 4:2:2
      - Cb, Cr
      - Yes
      - Linear
    * - V4L2_PIX_FMT_NV61
      - 'NV61'
      - 8
      - 4:2:2
      - Cr, Cr
      - Yes
      - Linear
    * - V4L2_PIX_FMT_NV16M
      - 'NM16'
      - 8
      - 4:2:2
      - Cb, Cr
      - No
      - Linear
    * - V4L2_PIX_FMT_NV61M
      - 'NM61'
      - 8
      - 4:2:2
      - Cr, Cr
      - No
      - Linear
    * - V4L2_PIX_FMT_NV24
      - 'NV24'
      - 8
      - 4:4:4
      - Cb, Cr
      - Yes
      - Linear
    * - V4L2_PIX_FMT_NV42
      - 'NV42'
      - 8
      - 4:4:4
      - Cr, Cr
      - Yes
      - Linear

.. note::

   .. [1] Order of chroma samples in the second plane
   .. [2] Indicates if planes have to be contiguous in memory or can be
      disjoint
   .. [3] Macroblock size in pixels


**Color Sample Location:**
Chroma samples are :ref:`interstitially sited<yuv-chroma-centered>`
horizontally.


.. _V4L2-PIX-FMT-NV12:
.. _V4L2-PIX-FMT-NV21:
.. _V4L2-PIX-FMT-NV12M:
.. _V4L2-PIX-FMT-NV21M:

NV12, NV21, NV12M and NV21M
---------------------------

Semi-planar YUV 4:2:0 formats. The chroma plane is subsampled by 2 in each
direction. Chroma lines contain half the number of pixels and the same number
of bytes as luma lines, and the chroma plane contains half the number of lines
of the luma plane.

.. flat-table:: Sample 4x4 NV12 Image
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * - start + 16:
      - Cb\ :sub:`00`
      - Cr\ :sub:`00`
      - Cb\ :sub:`01`
      - Cr\ :sub:`01`
    * - start + 20:
      - Cb\ :sub:`10`
      - Cr\ :sub:`10`
      - Cb\ :sub:`11`
      - Cr\ :sub:`11`

.. flat-table:: Sample 4x4 NV12M Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start0 + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start0 + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start0 + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * -
    * - start1 + 0:
      - Cb\ :sub:`00`
      - Cr\ :sub:`00`
      - Cb\ :sub:`01`
      - Cr\ :sub:`01`
    * - start1 + 4:
      - Cb\ :sub:`10`
      - Cr\ :sub:`10`
      - Cb\ :sub:`11`
      - Cr\ :sub:`11`


.. _V4L2-PIX-FMT-NV12MT:
.. _V4L2-PIX-FMT-NV12MT-16X16:

NV12MT and MV12MT_16X16
-----------------------

Semi-planar YUV 4:2:0 formats, using macroblock tiling. The chroma plane is
subsampled by 2 in each direction. Chroma lines contain half the number of
pixels and the same number of bytes as luma lines, and the chroma plane
contains half the number of lines of the luma plane.

``V4L2_PIX_FMT_NV12MT_16X16`` stores pixel in 2D 16x16 macroblocks, and stores
macroblocks linearly in memory. The line stride and image height must be
aligned to a multiple of 16. The layouts of the luma and chroma planes are
identical.

``V4L2_PIX_FMT_NV12MT`` stores pixels in 2D 64x32 macroblocks, and stores 2x2
groups of macroblocks in Z-order in memory, alternating Z and mirrored Z shapes
horizontally.  The line stride must be a multiple of 128 pixels to ensure an
integer number of Z shapes. The image height must be a multiple of 32 pixels.
If the vertical resolution is an odd number of macroblocks, the last row of
macroblocks is stored in linear order. The layouts of the luma and chroma
planes are identical.

.. _nv12mt:

.. kernel-figure:: nv12mt.svg
    :alt:    nv12mt.svg
    :align:  center

    V4L2_PIX_FMT_NV12MT macroblock Z shape memory layout

.. _nv12mt_ex:

.. kernel-figure:: nv12mt_example.svg
    :alt:    nv12mt_example.svg
    :align:  center

    Example V4L2_PIX_FMT_NV12MT memory layout of macroblocks


.. _V4L2-PIX-FMT-NV16:
.. _V4L2-PIX-FMT-NV61:
.. _V4L2-PIX-FMT-NV16M:
.. _V4L2-PIX-FMT-NV61M:

NV16, NV61, NV16M and NV61M
---------------------------

Semi-planar YUV 4:2:2 formats. The chroma plane is subsampled by 2 in the
horizontal direction. Chroma lines contain half the number of pixels and the
same number of bytes as luma lines, and the chroma plane contains the same
number of lines as the luma plane.

.. flat-table:: Sample 4x4 NV16 Image
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * - start + 16:
      - Cb\ :sub:`00`
      - Cr\ :sub:`00`
      - Cb\ :sub:`01`
      - Cr\ :sub:`01`
    * - start + 20:
      - Cb\ :sub:`10`
      - Cr\ :sub:`10`
      - Cb\ :sub:`11`
      - Cr\ :sub:`11`
    * - start + 24:
      - Cb\ :sub:`20`
      - Cr\ :sub:`20`
      - Cb\ :sub:`21`
      - Cr\ :sub:`21`
    * - start + 28:
      - Cb\ :sub:`30`
      - Cr\ :sub:`30`
      - Cb\ :sub:`31`
      - Cr\ :sub:`31`

.. flat-table:: Sample 4x4 NV16M Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start0 + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start0 + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start0 + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * -
    * - start1 + 0:
      - Cb\ :sub:`00`
      - Cr\ :sub:`00`
      - Cb\ :sub:`02`
      - Cr\ :sub:`02`
    * - start1 + 4:
      - Cb\ :sub:`10`
      - Cr\ :sub:`10`
      - Cb\ :sub:`12`
      - Cr\ :sub:`12`
    * - start1 + 8:
      - Cb\ :sub:`20`
      - Cr\ :sub:`20`
      - Cb\ :sub:`22`
      - Cr\ :sub:`22`
    * - start1 + 12:
      - Cb\ :sub:`30`
      - Cr\ :sub:`30`
      - Cb\ :sub:`32`
      - Cr\ :sub:`32`


.. _V4L2-PIX-FMT-NV24:
.. _V4L2-PIX-FMT-NV42:

NV24 and NV42
-------------

Semi-planar YUV 4:4:4 formats. The chroma plane is subsampled by 2 in the
horizontal direction. Chroma lines contain half the number of pixels and the
same number of bytes as luma lines, and the chroma plane contains the same
number of lines as the luma plane.

.. flat-table:: Sample 4x4 NV24 Image
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * - start + 16:
      - Cb\ :sub:`00`
      - Cr\ :sub:`00`
      - Cb\ :sub:`01`
      - Cr\ :sub:`01`
      - Cb\ :sub:`02`
      - Cr\ :sub:`02`
      - Cb\ :sub:`03`
      - Cr\ :sub:`03`
    * - start + 24:
      - Cb\ :sub:`10`
      - Cr\ :sub:`10`
      - Cb\ :sub:`11`
      - Cr\ :sub:`11`
      - Cb\ :sub:`12`
      - Cr\ :sub:`12`
      - Cb\ :sub:`13`
      - Cr\ :sub:`13`
    * - start + 32:
      - Cb\ :sub:`20`
      - Cr\ :sub:`20`
      - Cb\ :sub:`21`
      - Cr\ :sub:`21`
      - Cb\ :sub:`22`
      - Cr\ :sub:`22`
      - Cb\ :sub:`23`
      - Cr\ :sub:`23`
    * - start + 40:
      - Cb\ :sub:`30`
      - Cr\ :sub:`30`
      - Cb\ :sub:`31`
      - Cr\ :sub:`31`
      - Cb\ :sub:`32`
      - Cr\ :sub:`32`
      - Cb\ :sub:`33`
      - Cr\ :sub:`33`


Fully Planar YUV Formats
========================

These formats store the Y, Cb and Cr components in three separate planes. The
luma plane comes first, and the order of the two chroma planes varies between
formats. The two chroma planes always use the same subsampling.

For memory contiguous formats, the number of padding pixels at the end of the
chroma lines is identical to the padding of the luma lines. The chroma line
stride (in bytes) is thus equal to the luma line stride divided by the
horizontal subsampling factor. Vertical subsampling doesn't affect the line
stride.

For non-contiguous formats, no constraints are enforced by the format on the
relationship between the luma and chroma line padding and stride.

All components are stored with the same number of bits per component.

.. flat-table:: Overview of Fully Planar YUV Formats
    :header-rows:  1
    :stub-columns: 0

    * - Identifier
      - Code
      - Bits per component
      - Subsampling
      - Planes order [4]_
      - Contiguous [5]_

    * - V4L2_PIX_FMT_YUV410
      - 'YUV9'
      - 8
      - 4:1:0
      - Y, Cb, Cr
      - Yes
    * - V4L2_PIX_FMT_YVU410
      - 'YVU9'
      - 8
      - 4:1:0
      - Y, Cr, Cb
      - Yes
    * - V4L2_PIX_FMT_YUV411P
      - '411P'
      - 8
      - 4:1:1
      - Y, Cb, Cr
      - Yes
    * - V4L2_PIX_FMT_YUV420M
      - 'YM12'
      - 8
      - 4:2:0
      - Y, Cb, Cr
      - No
    * - V4L2_PIX_FMT_YVU420M
      - 'YM21'
      - 8
      - 4:2:0
      - Y, Cr, Cb
      - No
    * - V4L2_PIX_FMT_YUV420
      - 'YU12'
      - 8
      - 4:2:0
      - Y, Cb, Cr
      - Yes
    * - V4L2_PIX_FMT_YVU420
      - 'YV12'
      - 8
      - 4:2:0
      - Y, Cr, Cb
      - Yes
    * - V4L2_PIX_FMT_YUV422P
      - '422P'
      - 8
      - 4:2:2
      - Y, Cb, Cr
      - Yes
    * - V4L2_PIX_FMT_YUV422M
      - 'YM16'
      - 8
      - 4:2:2
      - Y, Cb, Cr
      - No
    * - V4L2_PIX_FMT_YVU422M
      - 'YM61'
      - 8
      - 4:2:2
      - Y, Cr, Cb
      - No
    * - V4L2_PIX_FMT_YUV444M
      - 'YM24'
      - 8
      - 4:4:4
      - Y, Cb, Cr
      - No
    * - V4L2_PIX_FMT_YVU444M
      - 'YM42'
      - 8
      - 4:4:4
      - Y, Cr, Cb
      - No

.. note::

   .. [4] Order of luma and chroma planes
   .. [5] Indicates if planes have to be contiguous in memory or can be
      disjoint


**Color Sample Location:**
Chroma samples are :ref:`interstitially sited<yuv-chroma-centered>`
horizontally.

.. _V4L2-PIX-FMT-YUV410:
.. _V4L2-PIX-FMT-YVU410:

YUV410 and YVU410
-----------------

Planar YUV 4:1:0 formats. The chroma planes are subsampled by 4 in each
direction. Chroma lines contain a quarter of the number of pixels and bytes of
the luma lines, and the chroma planes contain a quarter of the number of lines
of the luma plane.

.. flat-table:: Sample 4x4 YUV410 Image
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * - start + 16:
      - Cr\ :sub:`00`
    * - start + 17:
      - Cb\ :sub:`00`


.. _V4L2-PIX-FMT-YUV411P:

YUV411P
-------

Planar YUV 4:1:1 formats. The chroma planes are subsampled by 4 in the
horizontal direction. Chroma lines contain a quarter of the number of pixels
and bytes of the luma lines, and the chroma planes contain the same number of
lines as the luma plane.

.. flat-table:: Sample 4x4 YUV411P Image
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * - start + 16:
      - Cb\ :sub:`00`
    * - start + 17:
      - Cb\ :sub:`10`
    * - start + 18:
      - Cb\ :sub:`20`
    * - start + 19:
      - Cb\ :sub:`30`
    * - start + 20:
      - Cr\ :sub:`00`
    * - start + 21:
      - Cr\ :sub:`10`
    * - start + 22:
      - Cr\ :sub:`20`
    * - start + 23:
      - Cr\ :sub:`30`


.. _V4L2-PIX-FMT-YUV420:
.. _V4L2-PIX-FMT-YVU420:
.. _V4L2-PIX-FMT-YUV420M:
.. _V4L2-PIX-FMT-YVU420M:

YUV420, YVU420, YUV420M and YVU420M
-----------------------------------

Planar YUV 4:2:0 formats. The chroma planes are subsampled by 2 in each
direction. Chroma lines contain half of the number of pixels and bytes of the
luma lines, and the chroma planes contain half of the number of lines of the
luma plane.

.. flat-table:: Sample 4x4 YUV420 Image
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * - start + 16:
      - Cr\ :sub:`00`
      - Cr\ :sub:`01`
    * - start + 18:
      - Cr\ :sub:`10`
      - Cr\ :sub:`11`
    * - start + 20:
      - Cb\ :sub:`00`
      - Cb\ :sub:`01`
    * - start + 22:
      - Cb\ :sub:`10`
      - Cb\ :sub:`11`

.. flat-table:: Sample 4x4 YUV420M Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start0 + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start0 + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start0 + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * -
    * - start1 + 0:
      - Cb\ :sub:`00`
      - Cb\ :sub:`01`
    * - start1 + 2:
      - Cb\ :sub:`10`
      - Cb\ :sub:`11`
    * -
    * - start2 + 0:
      - Cr\ :sub:`00`
      - Cr\ :sub:`01`
    * - start2 + 2:
      - Cr\ :sub:`10`
      - Cr\ :sub:`11`


.. _V4L2-PIX-FMT-YUV422P:
.. _V4L2-PIX-FMT-YUV422M:
.. _V4L2-PIX-FMT-YVU422M:

YUV422P, YUV422M and YVU422M
----------------------------

Planar YUV 4:2:2 formats. The chroma planes are subsampled by 2 in the
horizontal direction. Chroma lines contain half of the number of pixels and
bytes of the luma lines, and the chroma planes contain the same number of lines
as the luma plane.

.. flat-table:: Sample 4x4 YUV422P Image
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * - start + 16:
      - Cb\ :sub:`00`
      - Cb\ :sub:`01`
    * - start + 18:
      - Cb\ :sub:`10`
      - Cb\ :sub:`11`
    * - start + 20:
      - Cb\ :sub:`20`
      - Cb\ :sub:`21`
    * - start + 22:
      - Cb\ :sub:`30`
      - Cb\ :sub:`31`
    * - start + 24:
      - Cr\ :sub:`00`
      - Cr\ :sub:`01`
    * - start + 26:
      - Cr\ :sub:`10`
      - Cr\ :sub:`11`
    * - start + 28:
      - Cr\ :sub:`20`
      - Cr\ :sub:`21`
    * - start + 30:
      - Cr\ :sub:`30`
      - Cr\ :sub:`31`

.. flat-table:: Sample 4x4 YUV422M Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start0 + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start0 + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start0 + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * -
    * - start1 + 0:
      - Cb\ :sub:`00`
      - Cb\ :sub:`01`
    * - start1 + 2:
      - Cb\ :sub:`10`
      - Cb\ :sub:`11`
    * - start1 + 4:
      - Cb\ :sub:`20`
      - Cb\ :sub:`21`
    * - start1 + 6:
      - Cb\ :sub:`30`
      - Cb\ :sub:`31`
    * -
    * - start2 + 0:
      - Cr\ :sub:`00`
      - Cr\ :sub:`01`
    * - start2 + 2:
      - Cr\ :sub:`10`
      - Cr\ :sub:`11`
    * - start2 + 4:
      - Cr\ :sub:`20`
      - Cr\ :sub:`21`
    * - start2 + 6:
      - Cr\ :sub:`30`
      - Cr\ :sub:`31`


.. _V4L2-PIX-FMT-YUV444M:
.. _V4L2-PIX-FMT-YVU444M:

YUV444M and YVU444M
-------------------

Planar YUV 4:4:4 formats. The chroma planes are no subsampled. Chroma lines
contain the same number of pixels and bytes of the luma lines, and the chroma
planes contain the same number of lines as the luma plane.

.. flat-table:: Sample 4x4 YUV444M Image
    :header-rows:  0
    :stub-columns: 0

    * - start0 + 0:
      - Y'\ :sub:`00`
      - Y'\ :sub:`01`
      - Y'\ :sub:`02`
      - Y'\ :sub:`03`
    * - start0 + 4:
      - Y'\ :sub:`10`
      - Y'\ :sub:`11`
      - Y'\ :sub:`12`
      - Y'\ :sub:`13`
    * - start0 + 8:
      - Y'\ :sub:`20`
      - Y'\ :sub:`21`
      - Y'\ :sub:`22`
      - Y'\ :sub:`23`
    * - start0 + 12:
      - Y'\ :sub:`30`
      - Y'\ :sub:`31`
      - Y'\ :sub:`32`
      - Y'\ :sub:`33`
    * -
    * - start1 + 0:
      - Cb\ :sub:`00`
      - Cb\ :sub:`01`
      - Cb\ :sub:`02`
      - Cb\ :sub:`03`
    * - start1 + 4:
      - Cb\ :sub:`10`
      - Cb\ :sub:`11`
      - Cb\ :sub:`12`
      - Cb\ :sub:`13`
    * - start1 + 8:
      - Cb\ :sub:`20`
      - Cb\ :sub:`21`
      - Cb\ :sub:`22`
      - Cb\ :sub:`23`
    * - start1 + 12:
      - Cb\ :sub:`20`
      - Cb\ :sub:`21`
      - Cb\ :sub:`32`
      - Cb\ :sub:`33`
    * -
    * - start2 + 0:
      - Cr\ :sub:`00`
      - Cr\ :sub:`01`
      - Cr\ :sub:`02`
      - Cr\ :sub:`03`
    * - start2 + 4:
      - Cr\ :sub:`10`
      - Cr\ :sub:`11`
      - Cr\ :sub:`12`
      - Cr\ :sub:`13`
    * - start2 + 8:
      - Cr\ :sub:`20`
      - Cr\ :sub:`21`
      - Cr\ :sub:`22`
      - Cr\ :sub:`23`
    * - start2 + 12:
      - Cr\ :sub:`30`
      - Cr\ :sub:`31`
      - Cr\ :sub:`32`
      - Cr\ :sub:`33`
