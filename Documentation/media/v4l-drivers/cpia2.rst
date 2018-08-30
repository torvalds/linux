.. SPDX-License-Identifier: GPL-2.0

The cpia2 driver
================

Authors: Peter Pregler <Peter_Pregler@email.com>,
Scott J. Bertin <scottbertin@yahoo.com>, and
Jarl Totland <Jarl.Totland@bdc.no> for the original cpia driver, which
this one was modelled from.

Introduction
------------

This is a driver for STMicroelectronics's CPiA2 (second generation
Colour Processor Interface ASIC) based cameras. This camera outputs an MJPEG
stream at up to vga size. It implements the Video4Linux interface as much as
possible.  Since the V4L interface does not support compressed formats, only
an mjpeg enabled application can be used with the camera. We have modified the
gqcam application to view this stream.

The driver is implemented as two kernel modules. The cpia2 module
contains the camera functions and the V4L interface.  The cpia2_usb module
contains usb specific functions.  The main reason for this was the size of the
module was getting out of hand, so I separated them.  It is not likely that
there will be a parallel port version.

Features
--------

- Supports cameras with the Vision stv6410 (CIF) and stv6500 (VGA) cmos
  sensors. I only have the vga sensor, so can't test the other.
- Image formats: VGA, QVGA, CIF, QCIF, and a number of sizes in between.
  VGA and QVGA are the native image sizes for the VGA camera. CIF is done
  in the coprocessor by scaling QVGA.  All other sizes are done by clipping.
- Palette: YCrCb, compressed with MJPEG.
- Some compression parameters are settable.
- Sensor framerate is adjustable (up to 30 fps CIF, 15 fps VGA).
- Adjust brightness, color, contrast while streaming.
- Flicker control settable for 50 or 60 Hz mains frequency.

Making and installing the stv672 driver modules
-----------------------------------------------

Requirements
~~~~~~~~~~~~

Video4Linux must be either compiled into the kernel or
available as a module.  Video4Linux2 is automatically detected and made
available at compile time.

Setup
~~~~~

Use 'modprobe cpia2' to load and 'modprobe -r cpia2' to unload. This
may be done automatically by your distribution.

Driver options
~~~~~~~~~~~~~~

.. tabularcolumns:: |p{13ex}|L|


==============  ========================================================
Option		Description
==============  ========================================================
video_nr	video device to register (0=/dev/video0, etc)
		range -1 to 64.  default is -1 (first available)
		If you have more than 1 camera, this MUST be -1.
buffer_size	Size for each frame buffer in bytes (default 68k)
num_buffers	Number of frame buffers (1-32, default 3)
alternate	USB Alternate (2-7, default 7)
flicker_freq	Frequency for flicker reduction(50 or 60, default 60)
flicker_mode	0 to disable, or 1 to enable flicker reduction.
		(default 0). This is only effective if the camera
		uses a stv0672 coprocessor.
==============  ========================================================

Setting the options
~~~~~~~~~~~~~~~~~~~

If you are using modules, edit /etc/modules.conf and add an options
line like this:

.. code-block:: none

	options cpia2 num_buffers=3 buffer_size=65535

If the driver is compiled into the kernel, at boot time specify them
like this:

.. code-block:: none

	cpia2.num_buffers=3 cpia2.buffer_size=65535

What buffer size should I use?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The maximum image size depends on the alternate you choose, and the
frame rate achieved by the camera.  If the compression engine is able to
keep up with the frame rate, the maximum image size is given by the table
below.

The compression engine starts out at maximum compression, and will
increase image quality until it is close to the size in the table.  As long
as the compression engine can keep up with the frame rate, after a short time
the images will all be about the size in the table, regardless of resolution.

At low alternate settings, the compression engine may not be able to
compress the image enough and will reduce the frame rate by producing larger
images.

The default of 68k should be good for most users.  This will handle
any alternate at frame rates down to 15fps.  For lower frame rates, it may
be necessary to increase the buffer size to avoid having frames dropped due
to insufficient space.

========== ========== ======== =====
Alternate  bytes/ms   15fps    30fps
========== ========== ======== =====
    2         128      8533     4267
    3         384     25600    12800
    4         640     42667    21333
    5         768     51200    25600
    6         896     59733    29867
    7        1023     68200    34100
========== ========== ======== =====

Table: Image size(bytes)


How many buffers should I use?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For normal streaming, 3 should give the best results.  With only 2,
it is possible for the camera to finish sending one image just after a
program has started reading the other.  If this happens, the driver must drop
a frame.  The exception to this is if you have a heavily loaded machine.  In
this case use 2 buffers.  You are probably not reading at the full frame rate.
If the camera can send multiple images before a read finishes, it could
overwrite the third buffer before the read finishes, leading to a corrupt
image.  Single and double buffering have extra checks to avoid overwriting.

Using the camera
~~~~~~~~~~~~~~~~

We are providing a modified gqcam application to view the output. In
order to avoid confusion, here it is called mview.  There is also the qx5view
program which can also control the lights on the qx5 microscope. MJPEG Tools
(http://mjpeg.sourceforge.net) can also be used to record from the camera.

Notes to developers
~~~~~~~~~~~~~~~~~~~

   - This is a driver version stripped of the 2.4 back compatibility
     and old MJPEG ioctl API. See cpia2.sf.net for 2.4 support.

Programmer's overview of cpia2 driver
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Cpia2 is the second generation video coprocessor from VLSI Vision Ltd (now a
division of ST Microelectronics).  There are two versions.  The first is the
STV0672, which is capable of up to 30 frames per second (fps) in frame sizes
up to CIF, and 15 fps for VGA frames.  The STV0676 is an improved version,
which can handle up to 30 fps VGA.  Both coprocessors can be attached to two
CMOS sensors - the vvl6410 CIF sensor and the vvl6500 VGA sensor.  These will
be referred to as the 410 and the 500 sensors, or the CIF and VGA sensors.

The two chipsets operate almost identically.  The core is an 8051 processor,
running two different versions of firmware.  The 672 runs the VP4 video
processor code, the 676 runs VP5.  There are a few differences in register
mappings for the two chips.  In these cases, the symbols defined in the
header files are marked with VP4 or VP5 as part of the symbol name.

The cameras appear externally as three sets of registers. Setting register
values is the only way to control the camera.  Some settings are
interdependant, such as the sequence required to power up the camera. I will
try to make note of all of these cases.

The register sets are called blocks.  Block 0 is the system block.  This
section is always powered on when the camera is plugged in.  It contains
registers that control housekeeping functions such as powering up the video
processor.  The video processor is the VP block.  These registers control
how the video from the sensor is processed.  Examples are timing registers,
user mode (vga, qvga), scaling, cropping, framerates, and so on.  The last
block is the video compressor (VC).  The video stream sent from the camera is
compressed as Motion JPEG (JPEGA).  The VC controls all of the compression
parameters.  Looking at the file cpia2_registers.h, you can get a full view
of these registers and the possible values for most of them.

One or more registers can be set or read by sending a usb control message to
the camera.  There are three modes for this.  Block mode requests a number
of contiguous registers.  Random mode reads or writes random registers with
a tuple structure containing address/value pairs.  The repeat mode is only
used by VP4 to load a firmware patch.  It contains a starting address and
a sequence of bytes to be written into a gpio port.
