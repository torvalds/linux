.. -*- coding: utf-8; mode: rst -*-

.. _v4l2-mbus-format:

Media Bus Formats
=================

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. _v4l2-mbus-framefmt:

.. flat-table:: struct v4l2_mbus_framefmt
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``width``

       -  Image width, in pixels.

    -  .. row 2

       -  __u32

       -  ``height``

       -  Image height, in pixels.

    -  .. row 3

       -  __u32

       -  ``code``

       -  Format code, from enum
	  :ref:`v4l2_mbus_pixelcode <v4l2-mbus-pixelcode>`.

    -  .. row 4

       -  __u32

       -  ``field``

       -  Field order, from enum :ref:`v4l2_field <v4l2-field>`. See
	  :ref:`field-order` for details.

    -  .. row 5

       -  __u32

       -  ``colorspace``

       -  Image colorspace, from enum
	  :ref:`v4l2_colorspace <v4l2-colorspace>`. See
	  :ref:`colorspaces` for details.

    -  .. row 6

       -  enum :ref:`v4l2_ycbcr_encoding <v4l2-ycbcr-encoding>`

       -  ``ycbcr_enc``

       -  This information supplements the ``colorspace`` and must be set by
	  the driver for capture streams and by the application for output
	  streams, see :ref:`colorspaces`.

    -  .. row 7

       -  enum :ref:`v4l2_quantization <v4l2-quantization>`

       -  ``quantization``

       -  This information supplements the ``colorspace`` and must be set by
	  the driver for capture streams and by the application for output
	  streams, see :ref:`colorspaces`.

    -  .. row 8

       -  enum :ref:`v4l2_xfer_func <v4l2-xfer-func>`

       -  ``xfer_func``

       -  This information supplements the ``colorspace`` and must be set by
	  the driver for capture streams and by the application for output
	  streams, see :ref:`colorspaces`.

    -  .. row 9

       -  __u16

       -  ``reserved``\ [11]

       -  Reserved for future extensions. Applications and drivers must set
	  the array to zero.



.. _v4l2-mbus-pixelcode:

Media Bus Pixel Codes
---------------------

The media bus pixel codes describe image formats as flowing over
physical busses (both between separate physical components and inside
SoC devices). This should not be confused with the V4L2 pixel formats
that describe, using four character codes, image formats as stored in
memory.

While there is a relationship between image formats on busses and image
formats in memory (a raw Bayer image won't be magically converted to
JPEG just by storing it to memory), there is no one-to-one
correspondance between them.


Packed RGB Formats
^^^^^^^^^^^^^^^^^^

Those formats transfer pixel data as red, green and blue components. The
format code is made of the following information.

-  The red, green and blue components order code, as encoded in a pixel
   sample. Possible values are RGB and BGR.

-  The number of bits per component, for each component. The values can
   be different for all components. Common values are 555 and 565.

-  The number of bus samples per pixel. Pixels that are wider than the
   bus width must be transferred in multiple samples. Common values are
   1 and 2.

-  The bus width.

-  For formats where the total number of bits per pixel is smaller than
   the number of bus samples per pixel times the bus width, a padding
   value stating if the bytes are padded in their most high order bits
   (PADHI) or low order bits (PADLO). A "C" prefix is used for
   component-wise padding in the most high order bits (CPADHI) or low
   order bits (CPADLO) of each separate component.

-  For formats where the number of bus samples per pixel is larger than
   1, an endianness value stating if the pixel is transferred MSB first
   (BE) or LSB first (LE).

For instance, a format where pixels are encoded as 5-bits red, 5-bits
green and 5-bit blue values padded on the high bit, transferred as 2
8-bit samples per pixel with the most significant bits (padding, red and
half of the green value) transferred first will be named
``MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE``.

The following tables list existing packed RGB formats.

.. FIXME: I was unable to find a way to use adjustbox or landscape for this table!

.. tabularcolumns:: |p{7.6cm}|p{1.6cm}|p{0.7cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{-1.0cm}|

.. _v4l2-mbus-pixelcode-rgb:

.. flat-table:: RGB formats
    :header-rows:  2
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2


    -  .. row 1

       -  Identifier

       -  Code

       -
       -  :cspan:`31` Data organization

    -  .. row 2

       -
       -
       -  Bit

       -  31

       -  30

       -  29

       -  28

       -  27

       -  26

       -  25

       -  24

       -  23

       -  22

       -  21

       -  20

       -  19

       -  18

       -  17

       -  16

       -  15

       -  14

       -  13

       -  12

       -  11

       -  10

       -  9

       -  8

       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

    -  .. _MEDIA-BUS-FMT-RGB444-1X12:

       -  MEDIA_BUS_FMT_RGB444_1X12

       -  0x1016

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RGB444-2X8-PADHI-BE:

       -  MEDIA_BUS_FMT_RGB444_2X8_PADHI_BE

       -  0x1001

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  0

       -  0

       -  0

       -  0

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. row 5

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RGB444-2X8-PADHI-LE:

       -  MEDIA_BUS_FMT_RGB444_2X8_PADHI_LE

       -  0x1002

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. row 7

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  0

       -  0

       -  0

       -  0

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RGB555-2X8-PADHI-BE:

       -  MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE

       -  0x1003

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  0

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

    -  .. row 9

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RGB555-2X8-PADHI-LE:

       -  MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE

       -  0x1004

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. row 11

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  0

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

    -  .. _MEDIA-BUS-FMT-RGB565-1X16:

       -  MEDIA_BUS_FMT_RGB565_1X16

       -  0x1017

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-BGR565-2X8-BE:

       -  MEDIA_BUS_FMT_BGR565_2X8_BE

       -  0x1005

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

    -  .. row 14

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-BGR565-2X8-LE:

       -  MEDIA_BUS_FMT_BGR565_2X8_LE

       -  0x1006

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. row 16

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

    -  .. _MEDIA-BUS-FMT-RGB565-2X8-BE:

       -  MEDIA_BUS_FMT_RGB565_2X8_BE

       -  0x1007

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

    -  .. row 18

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RGB565-2X8-LE:

       -  MEDIA_BUS_FMT_RGB565_2X8_LE

       -  0x1008

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. row 20

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

    -  .. _MEDIA-BUS-FMT-RGB666-1X18:

       -  MEDIA_BUS_FMT_RGB666_1X18

       -  0x1009

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RBG888-1X24:

       -  MEDIA_BUS_FMT_RBG888_1X24

       -  0x100e

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RGB666-1X24_CPADHI:

       -  MEDIA_BUS_FMT_RGB666_1X24_CPADHI

       -  0x1015

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  0

       -  0

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  0

       -  0

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  0

       -  0

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-BGR888-1X24:

       -  MEDIA_BUS_FMT_BGR888_1X24

       -  0x1013

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-GBR888-1X24:

       -  MEDIA_BUS_FMT_GBR888_1X24

       -  0x1014

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RGB888-1X24:

       -  MEDIA_BUS_FMT_RGB888_1X24

       -  0x100a

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RGB888-2X12-BE:

       -  MEDIA_BUS_FMT_RGB888_2X12_BE

       -  0x100b

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

    -  .. row 28

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RGB888-2X12-LE:

       -  MEDIA_BUS_FMT_RGB888_2X12_LE

       -  0x100c

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. row 30

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

    -  .. _MEDIA-BUS-FMT-ARGB888-1X32:

       -  MEDIA_BUS_FMT_ARGB888_1X32

       -  0x100d

       -
       -  a\ :sub:`7`

       -  a\ :sub:`6`

       -  a\ :sub:`5`

       -  a\ :sub:`4`

       -  a\ :sub:`3`

       -  a\ :sub:`2`

       -  a\ :sub:`1`

       -  a\ :sub:`0`

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RGB888-1X32-PADHI:

       -  MEDIA_BUS_FMT_RGB888_1X32_PADHI

       -  0x100f

       -
       -  0

       -  0

       -  0

       -  0

       -  0

       -  0

       -  0

       -  0

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

On LVDS buses, usually each sample is transferred serialized in seven
time slots per pixel clock, on three (18-bit) or four (24-bit)
differential data pairs at the same time. The remaining bits are used
for control signals as defined by SPWG/PSWG/VESA or JEIDA standards. The
24-bit RGB format serialized in seven time slots on four lanes using
JEIDA defined bit mapping will be named
``MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA``, for example.

.. raw:: latex

    \newline\newline\begin{adjustbox}{width=\columnwidth}

.. _v4l2-mbus-pixelcode-rgb-lvds:

.. flat-table:: LVDS RGB formats
    :header-rows:  2
    :stub-columns: 0


    -  .. row 1

       -  Identifier

       -  Code

       -
       -
       -  :cspan:`3` Data organization

    -  .. row 2

       -
       -
       -  Timeslot

       -  Lane

       -  3

       -  2

       -  1

       -  0

    -  .. _MEDIA-BUS-FMT-RGB666-1X7X3-SPWG:

       -  MEDIA_BUS_FMT_RGB666_1X7X3_SPWG

       -  0x1010

       -  0

       -
       -

       -  d

       -  b\ :sub:`1`

       -  g\ :sub:`0`

    -  .. row 4

       -
       -
       -  1

       -
       -

       -  d

       -  b\ :sub:`0`

       -  r\ :sub:`5`

    -  .. row 5

       -
       -
       -  2

       -
       -

       -  d

       -  g\ :sub:`5`

       -  r\ :sub:`4`

    -  .. row 6

       -
       -
       -  3

       -
       -

       -  b\ :sub:`5`

       -  g\ :sub:`4`

       -  r\ :sub:`3`

    -  .. row 7

       -
       -
       -  4

       -
       -

       -  b\ :sub:`4`

       -  g\ :sub:`3`

       -  r\ :sub:`2`

    -  .. row 8

       -
       -
       -  5

       -
       -

       -  b\ :sub:`3`

       -  g\ :sub:`2`

       -  r\ :sub:`1`

    -  .. row 9

       -
       -
       -  6

       -
       -

       -  b\ :sub:`2`

       -  g\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RGB888-1X7X4-SPWG:

       -  MEDIA_BUS_FMT_RGB888_1X7X4_SPWG

       -  0x1011

       -  0

       -
       -  d

       -  d

       -  b\ :sub:`1`

       -  g\ :sub:`0`

    -  .. row 11

       -
       -
       -  1

       -
       -  b\ :sub:`7`

       -  d

       -  b\ :sub:`0`

       -  r\ :sub:`5`

    -  .. row 12

       -
       -
       -  2

       -
       -  b\ :sub:`6`

       -  d

       -  g\ :sub:`5`

       -  r\ :sub:`4`

    -  .. row 13

       -
       -
       -  3

       -
       -  g\ :sub:`7`

       -  b\ :sub:`5`

       -  g\ :sub:`4`

       -  r\ :sub:`3`

    -  .. row 14

       -
       -
       -  4

       -
       -  g\ :sub:`6`

       -  b\ :sub:`4`

       -  g\ :sub:`3`

       -  r\ :sub:`2`

    -  .. row 15

       -
       -
       -  5

       -
       -  r\ :sub:`7`

       -  b\ :sub:`3`

       -  g\ :sub:`2`

       -  r\ :sub:`1`

    -  .. row 16

       -
       -
       -  6

       -
       -  r\ :sub:`6`

       -  b\ :sub:`2`

       -  g\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-RGB888-1X7X4-JEIDA:

       -  MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA

       -  0x1012

       -  0

       -
       -  d

       -  d

       -  b\ :sub:`3`

       -  g\ :sub:`2`

    -  .. row 18

       -
       -
       -  1

       -
       -  b\ :sub:`1`

       -  d

       -  b\ :sub:`2`

       -  r\ :sub:`7`

    -  .. row 19

       -
       -
       -  2

       -
       -  b\ :sub:`0`

       -  d

       -  g\ :sub:`7`

       -  r\ :sub:`6`

    -  .. row 20

       -
       -
       -  3

       -
       -  g\ :sub:`1`

       -  b\ :sub:`7`

       -  g\ :sub:`6`

       -  r\ :sub:`5`

    -  .. row 21

       -
       -
       -  4

       -
       -  g\ :sub:`0`

       -  b\ :sub:`6`

       -  g\ :sub:`5`

       -  r\ :sub:`4`

    -  .. row 22

       -
       -
       -  5

       -
       -  r\ :sub:`1`

       -  b\ :sub:`5`

       -  g\ :sub:`4`

       -  r\ :sub:`3`

    -  .. row 23

       -
       -
       -  6

       -
       -  r\ :sub:`0`

       -  b\ :sub:`4`

       -  g\ :sub:`3`

       -  r\ :sub:`2`

.. raw:: latex

    \end{adjustbox}\newline\newline


Bayer Formats
^^^^^^^^^^^^^

Those formats transfer pixel data as red, green and blue components. The
format code is made of the following information.

-  The red, green and blue components order code, as encoded in a pixel
   sample. The possible values are shown in :ref:`bayer-patterns`.

-  The number of bits per pixel component. All components are
   transferred on the same number of bits. Common values are 8, 10 and
   12.

-  The compression (optional). If the pixel components are ALAW- or
   DPCM-compressed, a mention of the compression scheme and the number
   of bits per compressed pixel component.

-  The number of bus samples per pixel. Pixels that are wider than the
   bus width must be transferred in multiple samples. Common values are
   1 and 2.

-  The bus width.

-  For formats where the total number of bits per pixel is smaller than
   the number of bus samples per pixel times the bus width, a padding
   value stating if the bytes are padded in their most high order bits
   (PADHI) or low order bits (PADLO).

-  For formats where the number of bus samples per pixel is larger than
   1, an endianness value stating if the pixel is transferred MSB first
   (BE) or LSB first (LE).

For instance, a format with uncompressed 10-bit Bayer components
arranged in a red, green, green, blue pattern transferred as 2 8-bit
samples per pixel with the least significant bits transferred first will
be named ``MEDIA_BUS_FMT_SRGGB10_2X8_PADHI_LE``.


.. _bayer-patterns:

.. figure::  subdev-formats_files/bayer.*
    :alt:    bayer.png
    :align:  center

    **Figure 4.8 Bayer Patterns**



The following table lists existing packed Bayer formats. The data
organization is given as an example for the first pixel only.


.. raw:: latex

    \newline\newline\begin{adjustbox}{width=\columnwidth}

.. tabularcolumns:: |p{7.6cm}|p{1.6cm}|p{0.7cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|

.. _v4l2-mbus-pixelcode-bayer:

.. cssclass: longtable

.. flat-table:: Bayer Formats
    :header-rows:  2
    :stub-columns: 0


    -  .. row 1

       -  Identifier

       -  Code

       -
       -  :cspan:`11` Data organization

    -  .. row 2

       -
       -
       -  Bit

       -  11

       -  10

       -  9

       -  8

       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

    -  .. _MEDIA-BUS-FMT-SBGGR8-1X8:

       -  MEDIA_BUS_FMT_SBGGR8_1X8

       -  0x3001

       -
       -

       -

       -

       -

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SGBRG8-1X8:

       -  MEDIA_BUS_FMT_SGBRG8_1X8

       -  0x3013

       -
       -

       -

       -

       -

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SGRBG8-1X8:

       -  MEDIA_BUS_FMT_SGRBG8_1X8

       -  0x3002

       -
       -

       -

       -

       -

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SRGGB8-1X8:

       -  MEDIA_BUS_FMT_SRGGB8_1X8

       -  0x3014

       -
       -

       -

       -

       -

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SBGGR10-ALAW8-1X8:

       -  MEDIA_BUS_FMT_SBGGR10_ALAW8_1X8

       -  0x3015

       -
       -

       -

       -

       -

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SGBRG10-ALAW8-1X8:

       -  MEDIA_BUS_FMT_SGBRG10_ALAW8_1X8

       -  0x3016

       -
       -

       -

       -

       -

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SGRBG10-ALAW8-1X8:

       -  MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8

       -  0x3017

       -
       -

       -

       -

       -

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SRGGB10-ALAW8-1X8:

       -  MEDIA_BUS_FMT_SRGGB10_ALAW8_1X8

       -  0x3018

       -
       -

       -

       -

       -

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SBGGR10-DPCM8-1X8:

       -  MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8

       -  0x300b

       -
       -

       -

       -

       -

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SGBRG10-DPCM8-1X8:

       -  MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8

       -  0x300c

       -
       -

       -

       -

       -

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SGRBG10-DPCM8-1X8:

       -  MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8

       -  0x3009

       -
       -

       -

       -

       -

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SRGGB10-DPCM8-1X8:

       -  MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8

       -  0x300d

       -
       -

       -

       -

       -

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SBGGR10-2X8-PADHI-BE:

       -  MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_BE

       -  0x3003

       -
       -

       -

       -

       -

       -  0

       -  0

       -  0

       -  0

       -  0

       -  0

       -  b\ :sub:`9`

       -  b\ :sub:`8`

    -  .. row 16

       -
       -
       -
       -

       -

       -

       -

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SBGGR10-2X8-PADHI-LE:

       -  MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_LE

       -  0x3004

       -
       -

       -

       -

       -

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. row 18

       -
       -
       -
       -

       -

       -

       -

       -  0

       -  0

       -  0

       -  0

       -  0

       -  0

       -  b\ :sub:`9`

       -  b\ :sub:`8`

    -  .. _MEDIA-BUS-FMT-SBGGR10-2X8-PADLO-BE:

       -  MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_BE

       -  0x3005

       -
       -

       -

       -

       -

       -  b\ :sub:`9`

       -  b\ :sub:`8`

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

    -  .. row 20

       -
       -
       -
       -

       -

       -

       -

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -  0

       -  0

       -  0

       -  0

       -  0

       -  0

    -  .. _MEDIA-BUS-FMT-SBGGR10-2X8-PADLO-LE:

       -  MEDIA_BUS_FMT_SBGGR10_2X8_PADLO_LE

       -  0x3006

       -
       -

       -

       -

       -

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -  0

       -  0

       -  0

       -  0

       -  0

       -  0

    -  .. row 22

       -
       -
       -
       -

       -

       -

       -

       -  b\ :sub:`9`

       -  b\ :sub:`8`

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

    -  .. _MEDIA-BUS-FMT-SBGGR10-1X10:

       -  MEDIA_BUS_FMT_SBGGR10_1X10

       -  0x3007

       -
       -

       -

       -  b\ :sub:`9`

       -  b\ :sub:`8`

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SGBRG10-1X10:

       -  MEDIA_BUS_FMT_SGBRG10_1X10

       -  0x300e

       -
       -

       -

       -  g\ :sub:`9`

       -  g\ :sub:`8`

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SGRBG10-1X10:

       -  MEDIA_BUS_FMT_SGRBG10_1X10

       -  0x300a

       -
       -

       -

       -  g\ :sub:`9`

       -  g\ :sub:`8`

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SRGGB10-1X10:

       -  MEDIA_BUS_FMT_SRGGB10_1X10

       -  0x300f

       -
       -

       -

       -  r\ :sub:`9`

       -  r\ :sub:`8`

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SBGGR12-1X12:

       -  MEDIA_BUS_FMT_SBGGR12_1X12

       -  0x3008

       -
       -  b\ :sub:`11`

       -  b\ :sub:`10`

       -  b\ :sub:`9`

       -  b\ :sub:`8`

       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SGBRG12-1X12:

       -  MEDIA_BUS_FMT_SGBRG12_1X12

       -  0x3010

       -
       -  g\ :sub:`11`

       -  g\ :sub:`10`

       -  g\ :sub:`9`

       -  g\ :sub:`8`

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SGRBG12-1X12:

       -  MEDIA_BUS_FMT_SGRBG12_1X12

       -  0x3011

       -
       -  g\ :sub:`11`

       -  g\ :sub:`10`

       -  g\ :sub:`9`

       -  g\ :sub:`8`

       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-SRGGB12-1X12:

       -  MEDIA_BUS_FMT_SRGGB12_1X12

       -  0x3012

       -
       -  r\ :sub:`11`

       -  r\ :sub:`10`

       -  r\ :sub:`9`

       -  r\ :sub:`8`

       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

.. raw:: latex

    \end{adjustbox}\newline\newline


Packed YUV Formats
^^^^^^^^^^^^^^^^^^

Those data formats transfer pixel data as (possibly downsampled) Y, U
and V components. Some formats include dummy bits in some of their
samples and are collectively referred to as "YDYC" (Y-Dummy-Y-Chroma)
formats. One cannot rely on the values of these dummy bits as those are
undefined.

The format code is made of the following information.

-  The Y, U and V components order code, as transferred on the bus.
   Possible values are YUYV, UYVY, YVYU and VYUY for formats with no
   dummy bit, and YDYUYDYV, YDYVYDYU, YUYDYVYD and YVYDYUYD for YDYC
   formats.

-  The number of bits per pixel component. All components are
   transferred on the same number of bits. Common values are 8, 10 and
   12.

-  The number of bus samples per pixel. Pixels that are wider than the
   bus width must be transferred in multiple samples. Common values are
   1, 1.5 (encoded as 1_5) and 2.

-  The bus width. When the bus width is larger than the number of bits
   per pixel component, several components are packed in a single bus
   sample. The components are ordered as specified by the order code,
   with components on the left of the code transferred in the high order
   bits. Common values are 8 and 16.

For instance, a format where pixels are encoded as 8-bit YUV values
downsampled to 4:2:2 and transferred as 2 8-bit bus samples per pixel in
the U, Y, V, Y order will be named ``MEDIA_BUS_FMT_UYVY8_2X8``.

:ref:`v4l2-mbus-pixelcode-yuv8` lists existing packed YUV formats and
describes the organization of each pixel data in each sample. When a
format pattern is split across multiple samples each of the samples in
the pattern is described.

The role of each bit transferred over the bus is identified by one of
the following codes.

-  y\ :sub:`x` for luma component bit number x

-  u\ :sub:`x` for blue chroma component bit number x

-  v\ :sub:`x` for red chroma component bit number x

-  a\ :sub:`x` for alpha component bit number x

- for non-available bits (for positions higher than the bus width)

-  d for dummy bits

.. FIXME: I was unable to find a way to use adjustbox or landscape for this table!

.. tabularcolumns:: |p{7.6cm}|p{1.6cm}|p{0.7cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{-1.0cm}|

.. _v4l2-mbus-pixelcode-yuv8:

.. cssclass:: longtable

.. flat-table:: YUV Formats
    :header-rows:  2
    :stub-columns: 0
    :widths: 36 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    -  .. row 1

       -  Identifier

       -  Code

       -
       -  :cspan:`31` Data organization

    -  .. row 2

       -
       -
       -  Bit

       -  31

       -  30

       -  29

       -  28

       -  27

       -  26

       -  25

       -  24

       -  23

       -  22

       -  21

       -  10

       -  19

       -  18

       -  17

       -  16

       -  15

       -  14

       -  13

       -  12

       -  11

       -  10

       -  9

       -  8

       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

    -  .. _MEDIA-BUS-FMT-Y8-1X8:

       -  MEDIA_BUS_FMT_Y8_1X8

       -  0x2001

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-UV8-1X8:

       -  MEDIA_BUS_FMT_UV8_1X8

       -  0x2015

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 5

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-UYVY8-1_5X8:

       -  MEDIA_BUS_FMT_UYVY8_1_5X8

       -  0x2002

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 7

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 8

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 9

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 10

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 11

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-VYUY8-1_5X8:

       -  MEDIA_BUS_FMT_VYUY8_1_5X8

       -  0x2003

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 13

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 14

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 15

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 16

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 17

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YUYV8-1_5X8:

       -  MEDIA_BUS_FMT_YUYV8_1_5X8

       -  0x2004

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 19

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 20

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 21

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 22

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 23

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YVYU8-1_5X8:

       -  MEDIA_BUS_FMT_YVYU8_1_5X8

       -  0x2005

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 25

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 26

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 27

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 28

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 29

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-UYVY8-2X8:

       -  MEDIA_BUS_FMT_UYVY8_2X8

       -  0x2006

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 31

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 32

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 33

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-VYUY8-2X8:

       -  MEDIA_BUS_FMT_VYUY8_2X8

       -  0x2007

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 35

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 36

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 37

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YUYV8-2X8:

       -  MEDIA_BUS_FMT_YUYV8_2X8

       -  0x2008

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 39

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 40

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 41

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YVYU8-2X8:

       -  MEDIA_BUS_FMT_YVYU8_2X8

       -  0x2009

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 43

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 44

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 45

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-Y10-1X10:

       -  MEDIA_BUS_FMT_Y10_1X10

       -  0x200a

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-UYVY10-2X10:

       -  MEDIA_BUS_FMT_UYVY10_2X10

       -  0x2018

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 48

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 49

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 50

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-VYUY10-2X10:

       -  MEDIA_BUS_FMT_VYUY10_2X10

       -  0x2019

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 52

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 53

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 54

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YUYV10-2X10:

       -  MEDIA_BUS_FMT_YUYV10_2X10

       -  0x200b

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 56

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 57

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 58

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YVYU10-2X10:

       -  MEDIA_BUS_FMT_YVYU10_2X10

       -  0x200c

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 60

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 61

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 62

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-Y12-1X12:

       -  MEDIA_BUS_FMT_Y12_1X12

       -  0x2013

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-UYVY12-2X12:

       -  MEDIA_BUS_FMT_UYVY12_2X12

       -  0x201c

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`11`

       -  u\ :sub:`10`

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 65

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 66

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`11`

       -  v\ :sub:`10`

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 67

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-VYUY12-2X12:

       -  MEDIA_BUS_FMT_VYUY12_2X12

       -  0x201d

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`11`

       -  v\ :sub:`10`

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 69

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 70

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`11`

       -  u\ :sub:`10`

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 71

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YUYV12-2X12:

       -  MEDIA_BUS_FMT_YUYV12_2X12

       -  0x201e

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 73

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`11`

       -  u\ :sub:`10`

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 74

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 75

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`11`

       -  v\ :sub:`10`

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YVYU12-2X12:

       -  MEDIA_BUS_FMT_YVYU12_2X12

       -  0x201f

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 77

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`11`

       -  v\ :sub:`10`

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 78

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 79

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`11`

       -  u\ :sub:`10`

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-UYVY8-1X16:

       -  MEDIA_BUS_FMT_UYVY8_1X16

       -  0x200f

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 81

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-VYUY8-1X16:

       -  MEDIA_BUS_FMT_VYUY8_1X16

       -  0x2010

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 83

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YUYV8-1X16:

       -  MEDIA_BUS_FMT_YUYV8_1X16

       -  0x2011

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 85

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YVYU8-1X16:

       -  MEDIA_BUS_FMT_YVYU8_1X16

       -  0x2012

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 87

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YDYUYDYV8-1X16:

       -  MEDIA_BUS_FMT_YDYUYDYV8_1X16

       -  0x2014

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  d

       -  d

       -  d

       -  d

       -  d

       -  d

       -  d

       -  d

    -  .. row 89

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 90

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  d

       -  d

       -  d

       -  d

       -  d

       -  d

       -  d

       -  d

    -  .. row 91

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-UYVY10-1X20:

       -  MEDIA_BUS_FMT_UYVY10_1X20

       -  0x201a

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 93

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-VYUY10-1X20:

       -  MEDIA_BUS_FMT_VYUY10_1X20

       -  0x201b

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 95

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YUYV10-1X20:

       -  MEDIA_BUS_FMT_YUYV10_1X20

       -  0x200d

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 97

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YVYU10-1X20:

       -  MEDIA_BUS_FMT_YVYU10_1X20

       -  0x200e

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 99

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-VUY8-1X24:

       -  MEDIA_BUS_FMT_VUY8_1X24

       -  0x201a

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YUV8-1X24:

       -  MEDIA_BUS_FMT_YUV8_1X24

       -  0x2025

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-UYVY12-1X24:

       -  MEDIA_BUS_FMT_UYVY12_1X24

       -  0x2020

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`11`

       -  u\ :sub:`10`

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 103

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`11`

       -  v\ :sub:`10`

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-VYUY12-1X24:

       -  MEDIA_BUS_FMT_VYUY12_1X24

       -  0x2021

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  v\ :sub:`11`

       -  v\ :sub:`10`

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. row 105

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  u\ :sub:`11`

       -  u\ :sub:`10`

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YUYV12-1X24:

       -  MEDIA_BUS_FMT_YUYV12_1X24

       -  0x2022

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  u\ :sub:`11`

       -  u\ :sub:`10`

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. row 107

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  v\ :sub:`11`

       -  v\ :sub:`10`

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YVYU12-1X24:

       -  MEDIA_BUS_FMT_YVYU12_1X24

       -  0x2023

       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  v\ :sub:`11`

       -  v\ :sub:`10`

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. row 109

       -
       -
       -
       -

       -

       -

       -

       -

       -

       -

       -

       -  y\ :sub:`11`

       -  y\ :sub:`10`

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  u\ :sub:`11`

       -  u\ :sub:`10`

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-YUV10-1X30:

       -  MEDIA_BUS_FMT_YUV10_1X30

       -  0x2016

       -
       -

       -

       -  y\ :sub:`9`

       -  y\ :sub:`8`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  u\ :sub:`9`

       -  u\ :sub:`8`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

       -  v\ :sub:`9`

       -  v\ :sub:`8`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

    -  .. _MEDIA-BUS-FMT-AYUV8-1X32:

       -  MEDIA_BUS_FMT_AYUV8_1X32

       -  0x2017

       -
       -  a\ :sub:`7`

       -  a\ :sub:`6`

       -  a\ :sub:`5`

       -  a\ :sub:`4`

       -  a\ :sub:`3`

       -  a\ :sub:`2`

       -  a\ :sub:`1`

       -  a\ :sub:`0`

       -  y\ :sub:`7`

       -  y\ :sub:`6`

       -  y\ :sub:`5`

       -  y\ :sub:`4`

       -  y\ :sub:`3`

       -  y\ :sub:`2`

       -  y\ :sub:`1`

       -  y\ :sub:`0`

       -  u\ :sub:`7`

       -  u\ :sub:`6`

       -  u\ :sub:`5`

       -  u\ :sub:`4`

       -  u\ :sub:`3`

       -  u\ :sub:`2`

       -  u\ :sub:`1`

       -  u\ :sub:`0`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`



HSV/HSL Formats
^^^^^^^^^^^^^^^

Those formats transfer pixel data as RGB values in a
cylindrical-coordinate system using Hue-Saturation-Value or
Hue-Saturation-Lightness components. The format code is made of the
following information.

-  The hue, saturation, value or lightness and optional alpha components
   order code, as encoded in a pixel sample. The only currently
   supported value is AHSV.

-  The number of bits per component, for each component. The values can
   be different for all components. The only currently supported value
   is 8888.

-  The number of bus samples per pixel. Pixels that are wider than the
   bus width must be transferred in multiple samples. The only currently
   supported value is 1.

-  The bus width.

-  For formats where the total number of bits per pixel is smaller than
   the number of bus samples per pixel times the bus width, a padding
   value stating if the bytes are padded in their most high order bits
   (PADHI) or low order bits (PADLO).

-  For formats where the number of bus samples per pixel is larger than
   1, an endianness value stating if the pixel is transferred MSB first
   (BE) or LSB first (LE).

The following table lists existing HSV/HSL formats.

.. raw:: latex

    \newline\newline\begin{adjustbox}{width=\columnwidth}

.. tabularcolumns:: |p{6.2cm}|p{1.6cm}|p{0.7cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|p{0.5cm}|

.. _v4l2-mbus-pixelcode-hsv:

.. flat-table:: HSV/HSL formats
    :header-rows:  2
    :stub-columns: 0
    :widths: 28 7 3 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2

    -  .. row 1

       -  Identifier

       -  Code

       -
       -  :cspan:`31` Data organization

    -  .. row 2

       -
       -
       -  Bit

       -  31

       -  30

       -  29

       -  28

       -  27

       -  26

       -  25

       -  24

       -  23

       -  22

       -  21

       -  20

       -  19

       -  18

       -  17

       -  16

       -  15

       -  14

       -  13

       -  12

       -  11

       -  10

       -  9

       -  8

       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

    -  .. _MEDIA-BUS-FMT-AHSV8888-1X32:

       -  MEDIA_BUS_FMT_AHSV8888_1X32

       -  0x6001

       -
       -  a\ :sub:`7`

       -  a\ :sub:`6`

       -  a\ :sub:`5`

       -  a\ :sub:`4`

       -  a\ :sub:`3`

       -  a\ :sub:`2`

       -  a\ :sub:`1`

       -  a\ :sub:`0`

       -  h\ :sub:`7`

       -  h\ :sub:`6`

       -  h\ :sub:`5`

       -  h\ :sub:`4`

       -  h\ :sub:`3`

       -  h\ :sub:`2`

       -  h\ :sub:`1`

       -  h\ :sub:`0`

       -  s\ :sub:`7`

       -  s\ :sub:`6`

       -  s\ :sub:`5`

       -  s\ :sub:`4`

       -  s\ :sub:`3`

       -  s\ :sub:`2`

       -  s\ :sub:`1`

       -  s\ :sub:`0`

       -  v\ :sub:`7`

       -  v\ :sub:`6`

       -  v\ :sub:`5`

       -  v\ :sub:`4`

       -  v\ :sub:`3`

       -  v\ :sub:`2`

       -  v\ :sub:`1`

       -  v\ :sub:`0`

.. raw:: latex

    \end{adjustbox}\newline\newline


JPEG Compressed Formats
^^^^^^^^^^^^^^^^^^^^^^^

Those data formats consist of an ordered sequence of 8-bit bytes
obtained from JPEG compression process. Additionally to the ``_JPEG``
postfix the format code is made of the following information.

-  The number of bus samples per entropy encoded byte.

-  The bus width.

For instance, for a JPEG baseline process and an 8-bit bus width the
format will be named ``MEDIA_BUS_FMT_JPEG_1X8``.

The following table lists existing JPEG compressed formats.


.. _v4l2-mbus-pixelcode-jpeg:

.. tabularcolumns:: |p{5.6cm}|p{1.2cm}|p{10.7cm}|

.. flat-table:: JPEG Formats
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  Identifier

       -  Code

       -  Remarks

    -  .. _MEDIA-BUS-FMT-JPEG-1X8:

       -  MEDIA_BUS_FMT_JPEG_1X8

       -  0x4001

       -  Besides of its usage for the parallel bus this format is
	  recommended for transmission of JPEG data over MIPI CSI bus using
	  the User Defined 8-bit Data types.



.. _v4l2-mbus-vendor-spec-fmts:

Vendor and Device Specific Formats
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This section lists complex data formats that are either vendor or device
specific.

The following table lists the existing vendor and device specific
formats.


.. _v4l2-mbus-pixelcode-vendor-specific:

.. tabularcolumns:: |p{6.6cm}|p{1.2cm}|p{9.7cm}|

.. flat-table:: Vendor and device specific formats
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  Identifier

       -  Code

       -  Comments

    -  .. _MEDIA-BUS-FMT-S5C-UYVY-JPEG-1X8:

       -  MEDIA_BUS_FMT_S5C_UYVY_JPEG_1X8

       -  0x5001

       -  Interleaved raw UYVY and JPEG image format with embedded meta-data
	  used by Samsung S3C73MX camera sensors.
