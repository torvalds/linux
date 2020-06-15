.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _V4L2-SDR-FMT-RU12LE:

****************************
V4L2_SDR_FMT_RU12LE ('RU12')
****************************


Real unsigned 12-bit little endian sample


Description
===========

This format contains sequence of real number samples. Each sample is
represented as a 12 bit unsigned little endian number. Sample is stored
in 16 bit space with unused high bits padded with 0.

**Byte Order.**
Each cell is one byte.




.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - I'\ :sub:`0[7:0]`
      - I'\ :sub:`0[11:8]`
