.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _v4l2-pix-fmt-raw-cru10:
.. _v4l2-pix-fmt-raw-cru12:
.. _v4l2-pix-fmt-raw-cru14:
.. _v4l2-pix-fmt-raw-cru20:

**********************************************************************************************************************************
V4L2_PIX_FMT_RAW_CRU10 ('CR10'), V4L2_PIX_FMT_RAW_CRU12 ('CR12'), V4L2_PIX_FMT_RAW_CRU14 ('CR14'), V4L2_PIX_FMT_RAW_CRU20 ('CR20')
**********************************************************************************************************************************

===============================================================
Renesas RZ/V2H Camera Receiver Unit 64-bit packed pixel formats
===============================================================

| V4L2_PIX_FMT_RAW_CRU10 (CR10)
| V4L2_PIX_FMT_RAW_CRU12 (CR12)
| V4L2_PIX_FMT_RAW_CRU14 (CR14)
| V4L2_PIX_FMT_RAW_CRU20 (CR20)

Description
===========

These pixel formats are some of the RAW outputs for the Camera Receiver Unit in
the Renesas RZ/V2H SoC. They are raw formats which pack pixels contiguously into
64-bit units, with the 4 or 8 most significant bits padded.

**Byte Order**

.. flat-table:: RAW formats
    :header-rows:  2
    :stub-columns: 0
    :widths: 36 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2
    :fill-cells:

    * - :rspan:`1` Pixel Format Code
      - :cspan:`63` Data organization
    * - 63
      - 62
      - 61
      - 60
      - 59
      - 58
      - 57
      - 56
      - 55
      - 54
      - 53
      - 52
      - 51
      - 50
      - 49
      - 48
      - 47
      - 46
      - 45
      - 44
      - 43
      - 42
      - 41
      - 40
      - 39
      - 38
      - 37
      - 36
      - 35
      - 34
      - 33
      - 32
      - 31
      - 30
      - 29
      - 28
      - 27
      - 26
      - 25
      - 24
      - 23
      - 22
      - 21
      - 20
      - 19
      - 18
      - 17
      - 16
      - 15
      - 14
      - 13
      - 12
      - 11
      - 10
      - 9
      - 8
      - 7
      - 6
      - 5
      - 4
      - 3
      - 2
      - 1
      - 0
    * - V4L2_PIX_FMT_RAW_CRU10
      - 0
      - 0
      - 0
      - 0
      - :cspan:`9` P5
      - :cspan:`9` P4
      - :cspan:`9` P3
      - :cspan:`9` P2
      - :cspan:`9` P1
      - :cspan:`9` P0
    * - V4L2_PIX_FMT_RAW_CRU12
      - 0
      - 0
      - 0
      - 0
      - :cspan:`11` P4
      - :cspan:`11` P3
      - :cspan:`11` P2
      - :cspan:`11` P1
      - :cspan:`11` P0
    * - V4L2_PIX_FMT_RAW_CRU14
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
      - 0
      - :cspan:`13` P3
      - :cspan:`13` P2
      - :cspan:`13` P1
      - :cspan:`13` P0
    * - V4L2_PIX_FMT_RAW_CRU20
      - 0
      - 0
      - 0
      - 0
      - :cspan:`19` P2
      - :cspan:`19` P1
      - :cspan:`19` P0
