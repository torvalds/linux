.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _V4L2-TCH-FMT-DELTA-TD08:

********************************
V4L2_TCH_FMT_DELTA_TD08 ('TD08')
********************************

*man V4L2_TCH_FMT_DELTA_TD08(2)*

8-bit signed Touch Delta

Description
===========

This format represents delta data from a touch controller.

Delta values may range from -128 to 127. Typically the values will vary through
a small range depending on whether the sensor is touched or not. The full value
may be seen if one of the touchscreen nodes has a fault or the line is not
connected.

**Byte Order.**
Each cell is one byte.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0
    :widths:       2 1 1 1 1

    * - start + 0:
      - D'\ :sub:`00`
      - D'\ :sub:`01`
      - D'\ :sub:`02`
      - D'\ :sub:`03`
    * - start + 4:
      - D'\ :sub:`10`
      - D'\ :sub:`11`
      - D'\ :sub:`12`
      - D'\ :sub:`13`
    * - start + 8:
      - D'\ :sub:`20`
      - D'\ :sub:`21`
      - D'\ :sub:`22`
      - D'\ :sub:`23`
    * - start + 12:
      - D'\ :sub:`30`
      - D'\ :sub:`31`
      - D'\ :sub:`32`
      - D'\ :sub:`33`
