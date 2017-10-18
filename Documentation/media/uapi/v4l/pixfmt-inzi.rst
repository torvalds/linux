.. -*- coding: utf-8; mode: rst -*-

.. _V4L2-PIX-FMT-INZI:

**************************
V4L2_PIX_FMT_INZI ('INZI')
**************************

Infrared 10-bit linked with Depth 16-bit images


Description
===========

Proprietary multi-planar format used by Intel SR300 Depth cameras, comprise of
Infrared image followed by Depth data. The pixel definition is 32-bpp,
with the Depth and Infrared Data split into separate continuous planes of
identical dimensions.



The first plane - Infrared data - is stored according to
:ref:`V4L2_PIX_FMT_Y10 <V4L2-PIX-FMT-Y10>` greyscale format.
Each pixel is 16-bit cell, with actual data stored in the 10 LSBs
with values in range 0 to 1023.
The six remaining MSBs are padded with zeros.


The second plane provides 16-bit per-pixel Depth data arranged in
:ref:`V4L2-PIX-FMT-Z16 <V4L2-PIX-FMT-Z16>` format.


**Frame Structure.**
Each cell is a 16-bit word with more significant data stored at higher
memory address (byte order is little-endian).

.. raw:: latex

    \newline\newline\begin{adjustbox}{width=\columnwidth}

.. tabularcolumns:: |p{4.0cm}|p{4.0cm}|p{4.0cm}|p{4.0cm}|p{4.0cm}|p{4.0cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 1
    :widths:    1 1 1 1 1 1

    * - Ir\ :sub:`0,0`
      - Ir\ :sub:`0,1`
      - Ir\ :sub:`0,2`
      - ...
      - ...
      - ...
    * - :cspan:`5` ...
    * - :cspan:`5` Infrared Data
    * - :cspan:`5` ...
    * - ...
      - ...
      - ...
      - Ir\ :sub:`n-1,n-3`
      - Ir\ :sub:`n-1,n-2`
      - Ir\ :sub:`n-1,n-1`
    * - Depth\ :sub:`0,0`
      - Depth\ :sub:`0,1`
      - Depth\ :sub:`0,2`
      - ...
      - ...
      - ...
    * - :cspan:`5` ...
    * - :cspan:`5` Depth Data
    * - :cspan:`5` ...
    * - ...
      - ...
      - ...
      - Depth\ :sub:`n-1,n-3`
      - Depth\ :sub:`n-1,n-2`
      - Depth\ :sub:`n-1,n-1`

.. raw:: latex

    \end{adjustbox}\newline\newline
