.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _pixfmt-bayer:

*****************
Raw Bayer Formats
*****************

Description
===========

The raw Bayer formats are used by image sensors before much if any processing is
performed on the image. The formats contain green, red and blue components, with
alternating lines of red and green, and blue and green pixels in different
orders. See also `the Wikipedia article on Bayer filter
<https://en.wikipedia.org/wiki/Bayer_filter>`__.


.. toctree::
    :maxdepth: 1

    pixfmt-srggb8
    pixfmt-srggb10
    pixfmt-srggb10p
    pixfmt-srggb10alaw8
    pixfmt-srggb10dpcm8
    pixfmt-srggb10-ipu3
    pixfmt-srggb12
    pixfmt-srggb12p
    pixfmt-srggb14
    pixfmt-srggb14p
    pixfmt-srggb16
