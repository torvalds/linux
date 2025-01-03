.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _yuv-formats:

***********
YUV Formats
***********

YUV is the format native to TV broadcast and composite video signals. It
separates the brightness information (Y) from the color information (U
and V or Cb and Cr). The color information consists of red and blue
*color difference* signals, this way the green component can be
reconstructed by subtracting from the brightness component. See
:ref:`colorspaces` for conversion examples. YUV was chosen because
early television would only transmit brightness information. To add
color in a way compatible with existing receivers a new signal carrier
was added to transmit the color difference signals.


Subsampling
===========

YUV formats commonly encode images with a lower resolution for the chroma
components than for the luma component. This compression technique, taking
advantage of the human eye being more sensitive to luminance than color
differences, is called chroma subsampling.

While many combinations of subsampling factors in the horizontal and vertical
direction are possible, common factors are 1 (no subsampling), 2 and 4, with
horizontal subsampling always larger than or equal to vertical subsampling.
Common combinations are named as follows.

- `4:4:4`: No subsampling
- `4:2:2`: Horizontal subsampling by 2, no vertical subsampling
- `4:2:0`: Horizontal subsampling by 2, vertical subsampling by 2
- `4:1:1`: Horizontal subsampling by 4, no vertical subsampling
- `4:1:0`: Horizontal subsampling by 4, vertical subsampling by 4

Subsampling the chroma component effectively creates chroma values that can be
located in different spatial locations:

- .. _yuv-chroma-centered:

  The subsampled chroma value may be calculated by simply averaging the chroma
  value of two consecutive pixels. It effectively models the chroma of a pixel
  sited between the two original pixels. This is referred to as centered or
  interstitially sited chroma.

- .. _yuv-chroma-cosited:

  The other option is to subsample chroma values in a way that place them in
  the same spatial sites as the pixels. This may be performed by skipping every
  other chroma sample (creating aliasing artifacts), or with filters using an
  odd number of taps. This is referred to as co-sited chroma.

The following examples show different combination of chroma siting in a 4x4
image.

.. flat-table:: 4:2:2 subsampling, interstitially sited
    :header-rows: 1
    :stub-columns: 1

    * -
      - 0
      -
      - 1
      -
      - 2
      -
      - 3
    * - 0
      - Y
      - C
      - Y
      -
      - Y
      - C
      - Y
    * - 1
      - Y
      - C
      - Y
      -
      - Y
      - C
      - Y
    * - 2
      - Y
      - C
      - Y
      -
      - Y
      - C
      - Y
    * - 3
      - Y
      - C
      - Y
      -
      - Y
      - C
      - Y

.. flat-table:: 4:2:2 subsampling, co-sited
    :header-rows: 1
    :stub-columns: 1

    * -
      - 0
      -
      - 1
      -
      - 2
      -
      - 3
    * - 0
      - Y/C
      -
      - Y
      -
      - Y/C
      -
      - Y
    * - 1
      - Y/C
      -
      - Y
      -
      - Y/C
      -
      - Y
    * - 2
      - Y/C
      -
      - Y
      -
      - Y/C
      -
      - Y
    * - 3
      - Y/C
      -
      - Y
      -
      - Y/C
      -
      - Y

.. flat-table:: 4:2:0 subsampling, horizontally interstitially sited, vertically co-sited
    :header-rows: 1
    :stub-columns: 1

    * -
      - 0
      -
      - 1
      -
      - 2
      -
      - 3
    * - 0
      - Y
      - C
      - Y
      -
      - Y
      - C
      - Y
    * - 1
      - Y
      -
      - Y
      -
      - Y
      -
      - Y
    * - 2
      - Y
      - C
      - Y
      -
      - Y
      - C
      - Y
    * - 3
      - Y
      -
      - Y
      -
      - Y
      -
      - Y

.. flat-table:: 4:1:0 subsampling, horizontally and vertically interstitially sited
    :header-rows: 1
    :stub-columns: 1

    * -
      - 0
      -
      - 1
      -
      - 2
      -
      - 3
    * - 0
      - Y
      -
      - Y
      -
      - Y
      -
      - Y
    * -
      -
      -
      -
      -
      -
      -
      -
    * - 1
      - Y
      -
      - Y
      -
      - Y
      -
      - Y
    * -
      -
      -
      -
      - C
      -
      -
      -
    * - 2
      - Y
      -
      - Y
      -
      - Y
      -
      - Y
    * -
      -
      -
      -
      -
      -
      -
      -
    * - 3
      - Y
      -
      - Y
      -
      - Y
      -
      - Y


.. toctree::
    :maxdepth: 1

    pixfmt-packed-yuv
    pixfmt-yuv-planar
    pixfmt-yuv-luma
    pixfmt-y8i
    pixfmt-y12i
    pixfmt-y16i
    pixfmt-uv8
    pixfmt-m420
