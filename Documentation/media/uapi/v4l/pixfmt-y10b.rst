.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _V4L2-PIX-FMT-Y10BPACK:

******************************
V4L2_PIX_FMT_Y10BPACK ('Y10B')
******************************

Grey-scale image as a bit-packed array


Description
===========

This is a packed grey-scale image format with a depth of 10 bits per
pixel. Pixels are stored in a bit-packed array of 10bit bits per pixel,
with no padding between them and with the most significant bits coming
first from the left.

**Bit-packed representation.**

pixels cross the byte boundary and have a ratio of 5 bytes for each 4
pixels.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - Y'\ :sub:`00[9:2]`
      - Y'\ :sub:`00[1:0]`\ Y'\ :sub:`01[9:4]`
      - Y'\ :sub:`01[3:0]`\ Y'\ :sub:`02[9:6]`
      - Y'\ :sub:`02[5:0]`\ Y'\ :sub:`03[9:8]`
      - Y'\ :sub:`03[7:0]`
