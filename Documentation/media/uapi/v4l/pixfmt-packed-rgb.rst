.. -*- coding: utf-8; mode: rst -*-

.. _packed-rgb:

******************
Packed RGB formats
******************

Description
===========

These formats are designed to match the pixel formats of typical PC
graphics frame buffers. They occupy 8, 16, 24 or 32 bits per pixel.
These are all packed-pixel formats, meaning all the data for a pixel lie
next to each other in memory.

.. raw:: latex

    \newline\newline\begin{adjustbox}{width=\columnwidth}

.. tabularcolumns:: |p{4.5cm}|p{3.3cm}|p{0.7cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.2cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.2cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.2cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{1.7cm}|

.. _rgb-formats:

.. flat-table:: Packed RGB Image Formats
    :header-rows:  2
    :stub-columns: 0


    -  .. row 1

       -  Identifier

       -  Code

       -
       -  :cspan:`7` Byte 0 in memory

       -
       -  :cspan:`7` Byte 1

       -
       -  :cspan:`7` Byte 2

       -
       -  :cspan:`7` Byte 3

    -  .. row 2

       -
       -
       -  Bit

       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

       -
       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

       -
       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

       -
       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

    -  .. _V4L2-PIX-FMT-RGB332:

       -  ``V4L2_PIX_FMT_RGB332``

       -  'RGB1'

       -
       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _V4L2-PIX-FMT-ARGB444:

       -  ``V4L2_PIX_FMT_ARGB444``

       -  'AR12'

       -
       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -
       -  a\ :sub:`3`

       -  a\ :sub:`2`

       -  a\ :sub:`1`

       -  a\ :sub:`0`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _V4L2-PIX-FMT-XRGB444:

       -  ``V4L2_PIX_FMT_XRGB444``

       -  'XR12'

       -
       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -
       -

       -

       -

       -

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _V4L2-PIX-FMT-ARGB555:

       -  ``V4L2_PIX_FMT_ARGB555``

       -  'AR15'

       -
       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -
       -  a

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

    -  .. _V4L2-PIX-FMT-XRGB555:

       -  ``V4L2_PIX_FMT_XRGB555``

       -  'XR15'

       -
       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -
       -

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

    -  .. _V4L2-PIX-FMT-RGB565:

       -  ``V4L2_PIX_FMT_RGB565``

       -  'RGBP'

       -
       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -
       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

    -  .. _V4L2-PIX-FMT-ARGB555X:

       -  ``V4L2_PIX_FMT_ARGB555X``

       -  'AR15' | (1 << 31)

       -
       -  a

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -
       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _V4L2-PIX-FMT-XRGB555X:

       -  ``V4L2_PIX_FMT_XRGB555X``

       -  'XR15' | (1 << 31)

       -
       -

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -
       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _V4L2-PIX-FMT-RGB565X:

       -  ``V4L2_PIX_FMT_RGB565X``

       -  'RGBR'

       -
       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -
       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _V4L2-PIX-FMT-BGR24:

       -  ``V4L2_PIX_FMT_BGR24``

       -  'BGR3'

       -
       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -
       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -
       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _V4L2-PIX-FMT-RGB24:

       -  ``V4L2_PIX_FMT_RGB24``

       -  'RGB3'

       -
       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -
       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -
       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _V4L2-PIX-FMT-BGR666:

       -  ``V4L2_PIX_FMT_BGR666``

       -  'BGRH'

       -
       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -
       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -
       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -

       -

       -

       -

       -

       -

       -
       -

       -

       -

       -

       -

       -

       -

       -

    -  .. _V4L2-PIX-FMT-ABGR32:

       -  ``V4L2_PIX_FMT_ABGR32``

       -  'AR24'

       -
       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -
       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -
       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -
       -  a\ :sub:`7`

       -  a\ :sub:`6`

       -  a\ :sub:`5`

       -  a\ :sub:`4`

       -  a\ :sub:`3`

       -  a\ :sub:`2`

       -  a\ :sub:`1`

       -  a\ :sub:`0`

    -  .. _V4L2-PIX-FMT-XBGR32:

       -  ``V4L2_PIX_FMT_XBGR32``

       -  'XR24'

       -
       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -
       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -
       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -
       -

       -

       -

       -

       -

       -

       -

       -

    -  .. _V4L2-PIX-FMT-ARGB32:

       -  ``V4L2_PIX_FMT_ARGB32``

       -  'BA24'

       -
       -  a\ :sub:`7`

       -  a\ :sub:`6`

       -  a\ :sub:`5`

       -  a\ :sub:`4`

       -  a\ :sub:`3`

       -  a\ :sub:`2`

       -  a\ :sub:`1`

       -  a\ :sub:`0`

       -
       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -
       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -
       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _V4L2-PIX-FMT-XRGB32:

       -  ``V4L2_PIX_FMT_XRGB32``

       -  'BX24'

       -
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

       -
       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -
       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

.. raw:: latex

    \end{adjustbox}\newline\newline

.. note:: Bit 7 is the most significant bit.

The usage and value of the alpha bits (a) in the ARGB and ABGR formats
(collectively referred to as alpha formats) depend on the device type
and hardware operation. :ref:`Capture <capture>` devices (including
capture queues of mem-to-mem devices) fill the alpha component in
memory. When the device outputs an alpha channel the alpha component
will have a meaningful value. Otherwise, when the device doesn't output
an alpha channel but can set the alpha bit to a user-configurable value,
the :ref:`V4L2_CID_ALPHA_COMPONENT <v4l2-alpha-component>` control
is used to specify that alpha value, and the alpha component of all
pixels will be set to the value specified by that control. Otherwise a
corresponding format without an alpha component (XRGB or XBGR) must be
used instead of an alpha format.

:ref:`Output <output>` devices (including output queues of mem-to-mem
devices and :ref:`video output overlay <osd>` devices) read the alpha
component from memory. When the device processes the alpha channel the
alpha component must be filled with meaningful values by applications.
Otherwise a corresponding format without an alpha component (XRGB or
XBGR) must be used instead of an alpha format.

The XRGB and XBGR formats contain undefined bits (-). Applications,
devices and drivers must ignore those bits, for both
:ref:`capture` and :ref:`output` devices.

**Byte Order.**
Each cell is one byte.


.. raw:: latex

    \newline\newline\begin{adjustbox}{width=\columnwidth}

.. tabularcolumns:: |p{4.1cm}|p{1.1cm}|p{1.1cm}|p{1.1cm}|p{1.1cm}|p{1.1cm}|p{1.1cm}|p{1.1cm}|p{1.1cm}|p{1.1cm}|p{1.1cm}|p{1.1cm}|p{1.3cm}|

.. flat-table:: RGB byte order
    :header-rows:  0
    :stub-columns: 0
    :widths:       11 3 3 3 3 3 3 3 3 3 3 3 3


    -  .. row 1

       -  start + 0:

       -  B\ :sub:`00`

       -  G\ :sub:`00`

       -  R\ :sub:`00`

       -  B\ :sub:`01`

       -  G\ :sub:`01`

       -  R\ :sub:`01`

       -  B\ :sub:`02`

       -  G\ :sub:`02`

       -  R\ :sub:`02`

       -  B\ :sub:`03`

       -  G\ :sub:`03`

       -  R\ :sub:`03`

    -  .. row 2

       -  start + 12:

       -  B\ :sub:`10`

       -  G\ :sub:`10`

       -  R\ :sub:`10`

       -  B\ :sub:`11`

       -  G\ :sub:`11`

       -  R\ :sub:`11`

       -  B\ :sub:`12`

       -  G\ :sub:`12`

       -  R\ :sub:`12`

       -  B\ :sub:`13`

       -  G\ :sub:`13`

       -  R\ :sub:`13`

    -  .. row 3

       -  start + 24:

       -  B\ :sub:`20`

       -  G\ :sub:`20`

       -  R\ :sub:`20`

       -  B\ :sub:`21`

       -  G\ :sub:`21`

       -  R\ :sub:`21`

       -  B\ :sub:`22`

       -  G\ :sub:`22`

       -  R\ :sub:`22`

       -  B\ :sub:`23`

       -  G\ :sub:`23`

       -  R\ :sub:`23`

    -  .. row 4

       -  start + 36:

       -  B\ :sub:`30`

       -  G\ :sub:`30`

       -  R\ :sub:`30`

       -  B\ :sub:`31`

       -  G\ :sub:`31`

       -  R\ :sub:`31`

       -  B\ :sub:`32`

       -  G\ :sub:`32`

       -  R\ :sub:`32`

       -  B\ :sub:`33`

       -  G\ :sub:`33`

       -  R\ :sub:`33`

.. raw:: latex

    \end{adjustbox}\newline\newline

Formats defined in :ref:`rgb-formats-deprecated` are deprecated and
must not be used by new drivers. They are documented here for reference.
The meaning of their alpha bits (a) is ill-defined and interpreted as in
either the corresponding ARGB or XRGB format, depending on the driver.


.. raw:: latex

    \newline\newline
    \begin{adjustbox}{width=\columnwidth}

.. tabularcolumns:: |p{4.2cm}|p{1.0cm}|p{0.7cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.2cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.2cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.2cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{0.4cm}|p{1.7cm}|

.. _rgb-formats-deprecated:

.. flat-table:: Deprecated Packed RGB Image Formats
    :header-rows:  2
    :stub-columns: 0


    -  .. row 1

       -  Identifier

       -  Code

       -
       -  :cspan:`7` Byte 0 in memory

       -
       -  :cspan:`7` Byte 1

       -
       -  :cspan:`7` Byte 2

       -
       -  :cspan:`7` Byte 3

    -  .. row 2

       -
       -
       -  Bit

       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

       -
       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

       -
       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

       -
       -  7

       -  6

       -  5

       -  4

       -  3

       -  2

       -  1

       -  0

    -  .. _V4L2-PIX-FMT-RGB444:

       -  ``V4L2_PIX_FMT_RGB444``

       -  'R444'

       -
       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -
       -  a\ :sub:`3`

       -  a\ :sub:`2`

       -  a\ :sub:`1`

       -  a\ :sub:`0`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

    -  .. _V4L2-PIX-FMT-RGB555:

       -  ``V4L2_PIX_FMT_RGB555``

       -  'RGBO'

       -
       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -
       -  a

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

    -  .. _V4L2-PIX-FMT-RGB555X:

       -  ``V4L2_PIX_FMT_RGB555X``

       -  'RGBQ'

       -
       -  a

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -
       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

    -  .. _V4L2-PIX-FMT-BGR32:

       -  ``V4L2_PIX_FMT_BGR32``

       -  'BGR4'

       -
       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

       -
       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -
       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -
       -  a\ :sub:`7`

       -  a\ :sub:`6`

       -  a\ :sub:`5`

       -  a\ :sub:`4`

       -  a\ :sub:`3`

       -  a\ :sub:`2`

       -  a\ :sub:`1`

       -  a\ :sub:`0`

    -  .. _V4L2-PIX-FMT-RGB32:

       -  ``V4L2_PIX_FMT_RGB32``

       -  'RGB4'

       -
       -  a\ :sub:`7`

       -  a\ :sub:`6`

       -  a\ :sub:`5`

       -  a\ :sub:`4`

       -  a\ :sub:`3`

       -  a\ :sub:`2`

       -  a\ :sub:`1`

       -  a\ :sub:`0`

       -
       -  r\ :sub:`7`

       -  r\ :sub:`6`

       -  r\ :sub:`5`

       -  r\ :sub:`4`

       -  r\ :sub:`3`

       -  r\ :sub:`2`

       -  r\ :sub:`1`

       -  r\ :sub:`0`

       -
       -  g\ :sub:`7`

       -  g\ :sub:`6`

       -  g\ :sub:`5`

       -  g\ :sub:`4`

       -  g\ :sub:`3`

       -  g\ :sub:`2`

       -  g\ :sub:`1`

       -  g\ :sub:`0`

       -
       -  b\ :sub:`7`

       -  b\ :sub:`6`

       -  b\ :sub:`5`

       -  b\ :sub:`4`

       -  b\ :sub:`3`

       -  b\ :sub:`2`

       -  b\ :sub:`1`

       -  b\ :sub:`0`

.. raw:: latex

    \end{adjustbox}\newline\newline

A test utility to determine which RGB formats a driver actually supports
is available from the LinuxTV v4l-dvb repository. See
`https://linuxtv.org/repo/ <https://linuxtv.org/repo/>`__ for access
instructions.
