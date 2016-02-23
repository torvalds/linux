			Programmer's View of Cpia2

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
