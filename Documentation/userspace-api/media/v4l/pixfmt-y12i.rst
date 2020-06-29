.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _V4L2-PIX-FMT-Y12I:

**************************
V4L2_PIX_FMT_Y12I ('Y12I')
**************************

Interleaved grey-scale image, e.g. from a stereo-pair


Description
===========

This is a grey-scale image with a depth of 12 bits per pixel, but with
pixels from 2 sources interleaved and bit-packed. Each pixel is stored
in a 24-bit word in the little-endian order. On a little-endian machine
these pixels can be deinterlaced using

.. code-block:: c

    __u8 *buf;
    left0 = 0xfff & *(__u16 *)buf;
    right0 = *(__u16 *)(buf + 1) >> 4;

**Bit-packed representation.**
pixels cross the byte boundary and have a ratio of 3 bytes for each
interleaved pixel.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - Y'\ :sub:`0left[7:0]`
      - Y'\ :sub:`0right[3:0]`\ Y'\ :sub:`0left[11:8]`
      - Y'\ :sub:`0right[11:4]`
