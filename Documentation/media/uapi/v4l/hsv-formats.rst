.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _hsv-formats:

***********
HSV Formats
***********

These formats store the color information of the image
in a geometrical representation. The colors are mapped into a
cylinder, where the angle is the HUE, the height is the VALUE
and the distance to the center is the SATURATION. This is a very
useful format for image segmentation algorithms.


.. toctree::
    :maxdepth: 1

    pixfmt-packed-hsv
