.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _pixfmt-indexed:

**************
Indexed Format
**************

In this format each pixel is represented by an 8 bit index into a 256
entry ARGB palette. It is intended for
:ref:`Video Output Overlays <osd>` only. There are no ioctls to access
the palette, this must be done with ioctls of the Linux framebuffer API.



.. flat-table:: Indexed Image Format
    :header-rows:  2
    :stub-columns: 0

    * - Identifier
      - Code
      -
      - :cspan:`7` Byte 0
    * -
      -
      - Bit
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * .. _V4L2-PIX-FMT-PAL8:

      - ``V4L2_PIX_FMT_PAL8``
      - 'PAL8'
      -
      - i\ :sub:`7`
      - i\ :sub:`6`
      - i\ :sub:`5`
      - i\ :sub:`4`
      - i\ :sub:`3`
      - i\ :sub:`2`
      - i\ :sub:`1`
      - i\ :sub:`0`
