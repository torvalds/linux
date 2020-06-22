.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _V4L2-PIX-FMT-YUYV:

**************************
V4L2_PIX_FMT_YUYV ('YUYV')
**************************


Packed format with ½ horizontal chroma resolution, also known as YUV
4:2:2


Description
===========

In this format each four bytes is two pixels. Each four bytes is two
Y's, a Cb and a Cr. Each Y goes to one of the pixels, and the Cb and Cr
belong to both pixels. As you can see, the Cr and Cb components have
half the horizontal resolution of the Y component. ``V4L2_PIX_FMT_YUYV``
is known in the Windows environment as YUY2.

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - Y'\ :sub:`00`
      - Cb\ :sub:`00`
      - Y'\ :sub:`01`
      - Cr\ :sub:`00`
      - Y'\ :sub:`02`
      - Cb\ :sub:`01`
      - Y'\ :sub:`03`
      - Cr\ :sub:`01`
    * - start + 8:
      - Y'\ :sub:`10`
      - Cb\ :sub:`10`
      - Y'\ :sub:`11`
      - Cr\ :sub:`10`
      - Y'\ :sub:`12`
      - Cb\ :sub:`11`
      - Y'\ :sub:`13`
      - Cr\ :sub:`11`
    * - start + 16:
      - Y'\ :sub:`20`
      - Cb\ :sub:`20`
      - Y'\ :sub:`21`
      - Cr\ :sub:`20`
      - Y'\ :sub:`22`
      - Cb\ :sub:`21`
      - Y'\ :sub:`23`
      - Cr\ :sub:`21`
    * - start + 24:
      - Y'\ :sub:`30`
      - Cb\ :sub:`30`
      - Y'\ :sub:`31`
      - Cr\ :sub:`30`
      - Y'\ :sub:`32`
      - Cb\ :sub:`31`
      - Y'\ :sub:`33`
      - Cr\ :sub:`31`


**Color Sample Location:**



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

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
