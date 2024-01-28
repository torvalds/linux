.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later OR GPL-2.0

.. c:namespace:: dtv.legacy.osd

.. _dvb_osd:

==============
DVB OSD Device
==============

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

The DVB OSD device controls the OnScreen-Display of the AV7110 based
DVB-cards with hardware MPEG2 decoder. It can be accessed through
``/dev/dvb/adapter?/osd0``.
Data types and ioctl definitions can be accessed by including
``linux/dvb/osd.h`` in your application.

The OSD is not a frame-buffer like on many other cards.
It is a kind of canvas one can draw on.
The color-depth is limited depending on the memory size installed.
An appropriate palette of colors has to be set up.
The installed memory size can be identified with the `OSD_GET_CAPABILITY`_
ioctl.

OSD Data Types
==============

OSD_Command
-----------

Synopsis
~~~~~~~~

.. code-block:: c

    typedef enum {
	/* All functions return -2 on "not open" */
	OSD_Close = 1,
	OSD_Open,
	OSD_Show,
	OSD_Hide,
	OSD_Clear,
	OSD_Fill,
	OSD_SetColor,
	OSD_SetPalette,
	OSD_SetTrans,
	OSD_SetPixel,
	OSD_GetPixel,
	OSD_SetRow,
	OSD_SetBlock,
	OSD_FillRow,
	OSD_FillBlock,
	OSD_Line,
	OSD_Query,
	OSD_Test,
	OSD_Text,
	OSD_SetWindow,
	OSD_MoveWindow,
	OSD_OpenRaw,
    } OSD_Command;

Commands
~~~~~~~~

.. note::  All functions return -2 on "not open"

.. flat-table::
    :header-rows:  1
    :stub-columns: 0

    -  ..

       -  Command

       -  | Used variables of ``struct`` `osd_cmd_t`_.
          | Usage{variable} if alternative use.

       -  :cspan:`2` Description



    -  ..

       -  ``OSD_Close``

       -  -

       -  | Disables OSD and releases the buffers.
          | Returns 0 on success.

    -  ..

       -  ``OSD_Open``

       -  | x0,y0,x1,y1,
          | BitPerPixel[2/4/8]{color&0x0F},
          | mix[0..15]{color&0xF0}

       -  | Opens OSD with this size and bit depth
          | Returns 0 on success,
          | -1 on DRAM allocation error,
          | -2 on "already open".

    -  ..

       -  ``OSD_Show``

       - -

       -  | Enables OSD mode.
          | Returns 0 on success.

    -  ..

       -  ``OSD_Hide``

       - -

       -  | Disables OSD mode.
          | Returns 0 on success.

    -  ..

       -  ``OSD_Clear``

       - -

       -  | Sets all pixel to color 0.
          | Returns 0 on success.

    -  ..

       -  ``OSD_Fill``

       -  color

       -  | Sets all pixel to color <color>.
          | Returns 0 on success.

    -  ..

       -  ``OSD_SetColor``

       -  | color,
          | R{x0},G{y0},B{x1},
          | opacity{y1}

       -  | Set palette entry <num> to <r,g,b>, <mix> and <trans> apply
          | R,G,B: 0..255
          | R=Red, G=Green, B=Blue
          | opacity=0:      pixel opacity 0% (only video pixel shows)
          | opacity=1..254: pixel opacity as specified in header
          | opacity=255:    pixel opacity 100% (only OSD pixel shows)
          | Returns 0 on success, -1 on error.

    -  ..

       -  ``OSD_SetPalette``

       -  | firstcolor{color},
          | lastcolor{x0},data

       -  | Set a number of entries in the palette.
          | Sets the entries "firstcolor" through "lastcolor" from the
            array "data".
          | Data has 4 byte for each color:
          | R,G,B, and a opacity value: 0->transparent, 1..254->mix,
            255->pixel

    -  ..

       -  ``OSD_SetTrans``

       -  transparency{color}

       -  | Sets transparency of mixed pixel (0..15).
          | Returns 0 on success.

    -  ..

       -  ``OSD_SetPixel``

       -  x0,y0,color

       -  | Sets pixel <x>,<y> to color number <color>.
          | Returns 0 on success, -1 on error.

    -  ..

       -  ``OSD_GetPixel``

       -  x0,y0

       -  | Returns color number of pixel <x>,<y>,  or -1.
          | Command currently not supported by the AV7110!

    -  ..

       -  ``OSD_SetRow``

       -  x0,y0,x1,data

       -  | Fills pixels x0,y through  x1,y with the content of data[].
          | Returns 0 on success, -1 on clipping all pixel (no pixel
            drawn).

    -  ..

       -  ``OSD_SetBlock``

       -  | x0,y0,x1,y1,
          | increment{color},
          | data

       -  | Fills pixels x0,y0 through  x1,y1 with the content of data[].
          | Inc contains the width of one line in the data block,
          | inc<=0 uses block width as line width.
          | Returns 0 on success, -1 on clipping all pixel.

    -  ..

       -  ``OSD_FillRow``

       -  x0,y0,x1,color

       -  | Fills pixels x0,y through  x1,y with the color <color>.
          | Returns 0 on success, -1 on clipping all pixel.

    -  ..

       -  ``OSD_FillBlock``

       -  x0,y0,x1,y1,color

       -  | Fills pixels x0,y0 through  x1,y1 with the color <color>.
          | Returns 0 on success, -1 on clipping all pixel.

    -  ..

       -  ``OSD_Line``

       -  x0,y0,x1,y1,color

       -  | Draw a line from x0,y0 to x1,y1 with the color <color>.
          | Returns 0 on success.

    -  ..

       -  ``OSD_Query``

       -  | x0,y0,x1,y1,
          | xasp{color}; yasp=11

       -  | Fills parameters with the picture dimensions and the pixel
            aspect ratio.
          | Returns 0 on success.
          | Command currently not supported by the AV7110!

    -  ..

       -  ``OSD_Test``

       -  -

       -  | Draws a test picture.
          | For debugging purposes only.
          | Returns 0 on success.
    -  ..

       -  ``OSD_Text``

       -  x0,y0,size,color,text

       -  Draws a text at position x0,y0 with the color <color>.

    -  ..

       -  ``OSD_SetWindow``

       -  x0

       -  Set window with number 0<x0<8 as current.

    -  ..

       -  ``OSD_MoveWindow``

       -  x0,y0

       -  Move current window to (x0, y0).

    -  ..

       -  ``OSD_OpenRaw``

       -  | x0,y0,x1,y1,
          | `osd_raw_window_t`_ {color}

       -  Open other types of OSD windows.

Description
~~~~~~~~~~~

The ``OSD_Command`` data type is used with the `OSD_SEND_CMD`_ ioctl to
tell the driver which OSD_Command to execute.


-----

osd_cmd_t
---------

Synopsis
~~~~~~~~

.. code-block:: c

    typedef struct osd_cmd_s {
	OSD_Command cmd;
	int x0;
	int y0;
	int x1;
	int y1;
	int color;
	void __user *data;
    } osd_cmd_t;

Variables
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``OSD_Command cmd``

       -  `OSD_Command`_ to be executed.

    -  ..

       -  ``int x0``

       -  First horizontal position.

    -  ..

       -  ``int y0``

       -  First vertical position.

    -  ..

       -  ``int x1``

       -  Second horizontal position.

    -  ..

       -  ``int y1``

       -  Second vertical position.

    -  ..

       -  ``int color``

       -  Number of the color in the palette.

    -  ..

       -  ``void __user *data``

       -  Command specific Data.

Description
~~~~~~~~~~~

The ``osd_cmd_t`` data type is used with the `OSD_SEND_CMD`_ ioctl.
It contains the data for the OSD_Command and the `OSD_Command`_ itself.
The structure has to be passed to the driver and the components may be
modified by it.


-----


osd_raw_window_t
----------------

Synopsis
~~~~~~~~

.. code-block:: c

    typedef enum {
	OSD_BITMAP1,
	OSD_BITMAP2,
	OSD_BITMAP4,
	OSD_BITMAP8,
	OSD_BITMAP1HR,
	OSD_BITMAP2HR,
	OSD_BITMAP4HR,
	OSD_BITMAP8HR,
	OSD_YCRCB422,
	OSD_YCRCB444,
	OSD_YCRCB444HR,
	OSD_VIDEOTSIZE,
	OSD_VIDEOHSIZE,
	OSD_VIDEOQSIZE,
	OSD_VIDEODSIZE,
	OSD_VIDEOTHSIZE,
	OSD_VIDEOTQSIZE,
	OSD_VIDEOTDSIZE,
	OSD_VIDEONSIZE,
	OSD_CURSOR
    } osd_raw_window_t;

Constants
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``OSD_BITMAP1``

       -  :cspan:`1` 1 bit bitmap

    -  ..

       -  ``OSD_BITMAP2``

       -  2 bit bitmap

    -  ..

       -  ``OSD_BITMAP4``

       -  4 bit bitmap

    -  ..

       -  ``OSD_BITMAP8``

       -  8 bit bitmap

    -  ..

       -  ``OSD_BITMAP1HR``

       -  1 Bit bitmap half resolution

    -  ..

       -  ``OSD_BITMAP2HR``

       -  2 Bit bitmap half resolution

    -  ..

       -  ``OSD_BITMAP4HR``

       -  4 Bit bitmap half resolution

    -  ..

       -  ``OSD_BITMAP8HR``

       -  8 Bit bitmap half resolution

    -  ..

       -  ``OSD_YCRCB422``

       -  4:2:2 YCRCB Graphic Display

    -  ..

       -  ``OSD_YCRCB444``

       -  4:4:4 YCRCB Graphic Display

    -  ..

       -  ``OSD_YCRCB444HR``

       -  4:4:4 YCRCB graphic half resolution

    -  ..

       -  ``OSD_VIDEOTSIZE``

       -  True Size Normal MPEG Video Display

    -  ..

       -  ``OSD_VIDEOHSIZE``

       -  MPEG Video Display Half Resolution

    -  ..

       -  ``OSD_VIDEOQSIZE``

       -  MPEG Video Display Quarter Resolution

    -  ..

       -  ``OSD_VIDEODSIZE``

       -  MPEG Video Display Double Resolution

    -  ..

       -  ``OSD_VIDEOTHSIZE``

       -  True Size MPEG Video Display Half Resolution

    -  ..

       -  ``OSD_VIDEOTQSIZE``

       -  True Size MPEG Video Display Quarter Resolution

    -  ..

       -  ``OSD_VIDEOTDSIZE``

       -  True Size MPEG Video Display Double Resolution

    -  ..

       -  ``OSD_VIDEONSIZE``

       -  Full Size MPEG Video Display

    -  ..

       -  ``OSD_CURSOR``

       -  Cursor

Description
~~~~~~~~~~~

The ``osd_raw_window_t`` data type is used with the `OSD_Command`_
OSD_OpenRaw to tell the driver which type of OSD to open.


-----


osd_cap_t
---------

Synopsis
~~~~~~~~

.. code-block:: c

    typedef struct osd_cap_s {
	int  cmd;
    #define OSD_CAP_MEMSIZE         1
	long val;
    } osd_cap_t;

Variables
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int  cmd``

       -  Capability to query.

    -  ..

       -  ``long val``

       -  Used to store the Data.

Supported capabilities
~~~~~~~~~~~~~~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``OSD_CAP_MEMSIZE``

       -  Memory size installed on the card.

Description
~~~~~~~~~~~

This structure of data used with the `OSD_GET_CAPABILITY`_ call.


-----


OSD Function Calls
==================

OSD_SEND_CMD
------------

Synopsis
~~~~~~~~

.. c:macro:: OSD_SEND_CMD

.. code-block:: c

    int ioctl(int fd, int request = OSD_SEND_CMD, enum osd_cmd_t *cmd)


Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Pointer to the location of the structure `osd_cmd_t`_ for this
          command.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl sends the `OSD_Command`_ to the card.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``EINVAL``

       -  Command is out of range.


-----


OSD_GET_CAPABILITY
------------------

Synopsis
~~~~~~~~

.. c:macro:: OSD_GET_CAPABILITY

.. code-block:: c

    int ioctl(int fd, int request = OSD_GET_CAPABILITY,
    struct osd_cap_t *cap)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_.

    -  ..

       -  ``int request``

       -  Equals ``OSD_GET_CAPABILITY`` for this command.

    -  ..

       -  ``unsigned int *cap``

       -  Pointer to the location of the structure `osd_cap_t`_ for this
          command.

Description
~~~~~~~~~~~

.. attention:: Do **not** use in new drivers!
             See: :ref:`legacy_dvb_decoder_notes`

This ioctl is used to get the capabilities of the OSD of the AV7110 based
DVB-decoder-card in use.

.. note::
    The structure osd_cap_t has to be setup by the user and passed to the
    driver.

Return Value
~~~~~~~~~~~~

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

.. flat-table::
    :header-rows:  0
    :stub-columns: 0


    -  ..

       -  ``EINVAL``

       -  Unsupported capability.


-----


open()
------

Synopsis
~~~~~~~~

.. code-block:: c

    #include <fcntl.h>

.. c:function:: int open(const char *deviceName, int flags)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``const char *deviceName``

       -  Name of specific OSD device.

    -  ..

       -  :rspan:`3` ``int flags``

       -  :cspan:`1` A bit-wise OR of the following flags:

    -  ..

       -  ``O_RDONLY``

       -  read-only access

    -  ..

       -  ``O_RDWR``

       -  read/write access

    -  ..

       -  ``O_NONBLOCK``
       -  | Open in non-blocking mode
          | (blocking mode is the default)

Description
~~~~~~~~~~~

This system call opens a named OSD device (e.g.
``/dev/dvb/adapter?/osd0``) for subsequent use.

Return Value
~~~~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``ENODEV``

       -  Device driver not loaded/available.

    -  ..

       -  ``EINTERNAL``

       -  Internal error.

    -  ..

       -  ``EBUSY``

       -  Device or resource busy.

    -  ..

       -  ``EINVAL``

       -  Invalid argument.


-----


close()
-------

Synopsis
~~~~~~~~

.. c:function:: int close(int fd)

Arguments
~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``int fd``

       -  :cspan:`1` File descriptor returned by a previous call
          to `open()`_ .

Description
~~~~~~~~~~~

This system call closes a previously opened OSD device.

Return Value
~~~~~~~~~~~~

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    -  ..

       -  ``EBADF``

       -  fd is not a valid open file descriptor.
