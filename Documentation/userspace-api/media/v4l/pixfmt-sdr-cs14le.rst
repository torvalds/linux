.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _V4L2-SDR-FMT-CS14LE:

****************************
V4L2_SDR_FMT_CS14LE ('CS14')
****************************

Complex signed 14-bit little endian IQ sample


Description
===========

This format contains sequence of complex number samples. Each complex
number consist two parts, called In-phase and Quadrature (IQ). Both I
and Q are represented as a 14 bit signed little endian number. I value
comes first and Q value after that. 14 bit value is stored in 16 bit
space with unused high bits padded with 0.

**Byte Order.**
Each cell is one byte.


.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - start + 0:
      - I'\ :sub:`0[7:0]`
      - I'\ :sub:`0[13:8]`
    * - start + 2:
      - Q'\ :sub:`0[7:0]`
      - Q'\ :sub:`0[13:8]`
