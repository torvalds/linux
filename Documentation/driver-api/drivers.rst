====================
Linux Device Drivers
====================

Driver Basics
=============

Driver Entry and Exit points
----------------------------

.. kernel-doc:: include/linux/init.h
   :internal:

Atomic and pointer manipulation
-------------------------------

.. kernel-doc:: arch/x86/include/asm/atomic.h
   :internal:

Delaying, scheduling, and timer routines
----------------------------------------

.. kernel-doc:: include/linux/sched.h
   :internal:

.. kernel-doc:: kernel/sched/core.c
   :export:

.. kernel-doc:: kernel/sched/cpupri.c
   :internal:

.. kernel-doc:: kernel/sched/fair.c
   :internal:

.. kernel-doc:: include/linux/completion.h
   :internal:

.. kernel-doc:: kernel/time/timer.c
   :export:

Wait queues and Wake events
---------------------------

.. kernel-doc:: include/linux/wait.h
   :internal:

.. kernel-doc:: kernel/sched/wait.c
   :export:

High-resolution timers
----------------------

.. kernel-doc:: include/linux/ktime.h
   :internal:

.. kernel-doc:: include/linux/hrtimer.h
   :internal:

.. kernel-doc:: kernel/time/hrtimer.c
   :export:

Workqueues and Kevents
----------------------

.. kernel-doc:: include/linux/workqueue.h
   :internal:

.. kernel-doc:: kernel/workqueue.c
   :export:

Internal Functions
------------------

.. kernel-doc:: kernel/exit.c
   :internal:

.. kernel-doc:: kernel/signal.c
   :internal:

.. kernel-doc:: include/linux/kthread.h
   :internal:

.. kernel-doc:: kernel/kthread.c
   :export:

Kernel objects manipulation
---------------------------

.. kernel-doc:: lib/kobject.c
   :export:

Kernel utility functions
------------------------

.. kernel-doc:: include/linux/kernel.h
   :internal:

.. kernel-doc:: kernel/printk/printk.c
   :export:

.. kernel-doc:: kernel/panic.c
   :export:

.. kernel-doc:: kernel/sys.c
   :export:

.. kernel-doc:: kernel/rcu/srcu.c
   :export:

.. kernel-doc:: kernel/rcu/tree.c
   :export:

.. kernel-doc:: kernel/rcu/tree_plugin.h
   :export:

.. kernel-doc:: kernel/rcu/update.c
   :export:

Device Resource Management
--------------------------

.. kernel-doc:: drivers/base/devres.c
   :export:

Device drivers infrastructure
=============================

The Basic Device Driver-Model Structures
----------------------------------------

.. kernel-doc:: include/linux/device.h
   :internal:

Device Drivers Base
-------------------

.. kernel-doc:: drivers/base/init.c
   :internal:

.. kernel-doc:: drivers/base/driver.c
   :export:

.. kernel-doc:: drivers/base/core.c
   :export:

.. kernel-doc:: drivers/base/syscore.c
   :export:

.. kernel-doc:: drivers/base/class.c
   :export:

.. kernel-doc:: drivers/base/node.c
   :internal:

.. kernel-doc:: drivers/base/firmware_class.c
   :export:

.. kernel-doc:: drivers/base/transport_class.c
   :export:

.. kernel-doc:: drivers/base/dd.c
   :export:

.. kernel-doc:: include/linux/platform_device.h
   :internal:

.. kernel-doc:: drivers/base/platform.c
   :export:

.. kernel-doc:: drivers/base/bus.c
   :export:

Buffer Sharing and Synchronization
----------------------------------

The dma-buf subsystem provides the framework for sharing buffers for
hardware (DMA) access across multiple device drivers and subsystems, and
for synchronizing asynchronous hardware access.

This is used, for example, by drm "prime" multi-GPU support, but is of
course not limited to GPU use cases.

The three main components of this are: (1) dma-buf, representing a
sg_table and exposed to userspace as a file descriptor to allow passing
between devices, (2) fence, which provides a mechanism to signal when
one device as finished access, and (3) reservation, which manages the
shared or exclusive fence(s) associated with the buffer.

dma-buf
~~~~~~~

.. kernel-doc:: drivers/dma-buf/dma-buf.c
   :export:

.. kernel-doc:: include/linux/dma-buf.h
   :internal:

reservation
~~~~~~~~~~~

.. kernel-doc:: drivers/dma-buf/reservation.c
   :doc: Reservation Object Overview

.. kernel-doc:: drivers/dma-buf/reservation.c
   :export:

.. kernel-doc:: include/linux/reservation.h
   :internal:

fence
~~~~~

.. kernel-doc:: drivers/dma-buf/fence.c
   :export:

.. kernel-doc:: include/linux/fence.h
   :internal:

.. kernel-doc:: drivers/dma-buf/seqno-fence.c
   :export:

.. kernel-doc:: include/linux/seqno-fence.h
   :internal:

.. kernel-doc:: drivers/dma-buf/fence-array.c
   :export:

.. kernel-doc:: include/linux/fence-array.h
   :internal:

.. kernel-doc:: drivers/dma-buf/reservation.c
   :export:

.. kernel-doc:: include/linux/reservation.h
   :internal:

.. kernel-doc:: drivers/dma-buf/sync_file.c
   :export:

.. kernel-doc:: include/linux/sync_file.h
   :internal:

Device Drivers DMA Management
-----------------------------

.. kernel-doc:: drivers/base/dma-coherent.c
   :export:

.. kernel-doc:: drivers/base/dma-mapping.c
   :export:

Device Drivers Power Management
-------------------------------

.. kernel-doc:: drivers/base/power/main.c
   :export:

Device Drivers ACPI Support
---------------------------

.. kernel-doc:: drivers/acpi/scan.c
   :export:

.. kernel-doc:: drivers/acpi/scan.c
   :internal:

Device drivers PnP support
--------------------------

.. kernel-doc:: drivers/pnp/core.c
   :internal:

.. kernel-doc:: drivers/pnp/card.c
   :export:

.. kernel-doc:: drivers/pnp/driver.c
   :internal:

.. kernel-doc:: drivers/pnp/manager.c
   :export:

.. kernel-doc:: drivers/pnp/support.c
   :export:

Userspace IO devices
--------------------

.. kernel-doc:: drivers/uio/uio.c
   :export:

.. kernel-doc:: include/linux/uio_driver.h
   :internal:

Parallel Port Devices
=====================

.. kernel-doc:: include/linux/parport.h
   :internal:

.. kernel-doc:: drivers/parport/ieee1284.c
   :export:

.. kernel-doc:: drivers/parport/share.c
   :export:

.. kernel-doc:: drivers/parport/daisy.c
   :internal:

Message-based devices
=====================

Fusion message devices
----------------------

.. kernel-doc:: drivers/message/fusion/mptbase.c
   :export:

.. kernel-doc:: drivers/message/fusion/mptbase.c
   :internal:

.. kernel-doc:: drivers/message/fusion/mptscsih.c
   :export:

.. kernel-doc:: drivers/message/fusion/mptscsih.c
   :internal:

.. kernel-doc:: drivers/message/fusion/mptctl.c
   :internal:

.. kernel-doc:: drivers/message/fusion/mptspi.c
   :internal:

.. kernel-doc:: drivers/message/fusion/mptfc.c
   :internal:

.. kernel-doc:: drivers/message/fusion/mptlan.c
   :internal:

Sound Devices
=============

.. kernel-doc:: include/sound/core.h
   :internal:

.. kernel-doc:: sound/sound_core.c
   :export:

.. kernel-doc:: include/sound/pcm.h
   :internal:

.. kernel-doc:: sound/core/pcm.c
   :export:

.. kernel-doc:: sound/core/device.c
   :export:

.. kernel-doc:: sound/core/info.c
   :export:

.. kernel-doc:: sound/core/rawmidi.c
   :export:

.. kernel-doc:: sound/core/sound.c
   :export:

.. kernel-doc:: sound/core/memory.c
   :export:

.. kernel-doc:: sound/core/pcm_memory.c
   :export:

.. kernel-doc:: sound/core/init.c
   :export:

.. kernel-doc:: sound/core/isadma.c
   :export:

.. kernel-doc:: sound/core/control.c
   :export:

.. kernel-doc:: sound/core/pcm_lib.c
   :export:

.. kernel-doc:: sound/core/hwdep.c
   :export:

.. kernel-doc:: sound/core/pcm_native.c
   :export:

.. kernel-doc:: sound/core/memalloc.c
   :export:

16x50 UART Driver
=================

.. kernel-doc:: drivers/tty/serial/serial_core.c
   :export:

.. kernel-doc:: drivers/tty/serial/8250/8250_core.c
   :export:

Frame Buffer Library
====================

The frame buffer drivers depend heavily on four data structures. These
structures are declared in include/linux/fb.h. They are fb_info,
fb_var_screeninfo, fb_fix_screeninfo and fb_monospecs. The last
three can be made available to and from userland.

fb_info defines the current state of a particular video card. Inside
fb_info, there exists a fb_ops structure which is a collection of
needed functions to make fbdev and fbcon work. fb_info is only visible
to the kernel.

fb_var_screeninfo is used to describe the features of a video card
that are user defined. With fb_var_screeninfo, things such as depth
and the resolution may be defined.

The next structure is fb_fix_screeninfo. This defines the properties
of a card that are created when a mode is set and can't be changed
otherwise. A good example of this is the start of the frame buffer
memory. This "locks" the address of the frame buffer memory, so that it
cannot be changed or moved.

The last structure is fb_monospecs. In the old API, there was little
importance for fb_monospecs. This allowed for forbidden things such as
setting a mode of 800x600 on a fix frequency monitor. With the new API,
fb_monospecs prevents such things, and if used correctly, can prevent a
monitor from being cooked. fb_monospecs will not be useful until
kernels 2.5.x.

Frame Buffer Memory
-------------------

.. kernel-doc:: drivers/video/fbdev/core/fbmem.c
   :export:

Frame Buffer Colormap
---------------------

.. kernel-doc:: drivers/video/fbdev/core/fbcmap.c
   :export:

Frame Buffer Video Mode Database
--------------------------------

.. kernel-doc:: drivers/video/fbdev/core/modedb.c
   :internal:

.. kernel-doc:: drivers/video/fbdev/core/modedb.c
   :export:

Frame Buffer Macintosh Video Mode Database
------------------------------------------

.. kernel-doc:: drivers/video/fbdev/macmodes.c
   :export:

Frame Buffer Fonts
------------------

Refer to the file lib/fonts/fonts.c for more information.

Input Subsystem
===============

Input core
----------

.. kernel-doc:: include/linux/input.h
   :internal:

.. kernel-doc:: drivers/input/input.c
   :export:

.. kernel-doc:: drivers/input/ff-core.c
   :export:

.. kernel-doc:: drivers/input/ff-memless.c
   :export:

Multitouch Library
------------------

.. kernel-doc:: include/linux/input/mt.h
   :internal:

.. kernel-doc:: drivers/input/input-mt.c
   :export:

Polled input devices
--------------------

.. kernel-doc:: include/linux/input-polldev.h
   :internal:

.. kernel-doc:: drivers/input/input-polldev.c
   :export:

Matrix keyboards/keypads
------------------------

.. kernel-doc:: include/linux/input/matrix_keypad.h
   :internal:

Sparse keymap support
---------------------

.. kernel-doc:: include/linux/input/sparse-keymap.h
   :internal:

.. kernel-doc:: drivers/input/sparse-keymap.c
   :export:

Serial Peripheral Interface (SPI)
=================================

SPI is the "Serial Peripheral Interface", widely used with embedded
systems because it is a simple and efficient interface: basically a
multiplexed shift register. Its three signal wires hold a clock (SCK,
often in the range of 1-20 MHz), a "Master Out, Slave In" (MOSI) data
line, and a "Master In, Slave Out" (MISO) data line. SPI is a full
duplex protocol; for each bit shifted out the MOSI line (one per clock)
another is shifted in on the MISO line. Those bits are assembled into
words of various sizes on the way to and from system memory. An
additional chipselect line is usually active-low (nCS); four signals are
normally used for each peripheral, plus sometimes an interrupt.

The SPI bus facilities listed here provide a generalized interface to
declare SPI busses and devices, manage them according to the standard
Linux driver model, and perform input/output operations. At this time,
only "master" side interfaces are supported, where Linux talks to SPI
peripherals and does not implement such a peripheral itself. (Interfaces
to support implementing SPI slaves would necessarily look different.)

The programming interface is structured around two kinds of driver, and
two kinds of device. A "Controller Driver" abstracts the controller
hardware, which may be as simple as a set of GPIO pins or as complex as
a pair of FIFOs connected to dual DMA engines on the other side of the
SPI shift register (maximizing throughput). Such drivers bridge between
whatever bus they sit on (often the platform bus) and SPI, and expose
the SPI side of their device as a :c:type:`struct spi_master
<spi_master>`. SPI devices are children of that master,
represented as a :c:type:`struct spi_device <spi_device>` and
manufactured from :c:type:`struct spi_board_info
<spi_board_info>` descriptors which are usually provided by
board-specific initialization code. A :c:type:`struct spi_driver
<spi_driver>` is called a "Protocol Driver", and is bound to a
spi_device using normal driver model calls.

The I/O model is a set of queued messages. Protocol drivers submit one
or more :c:type:`struct spi_message <spi_message>` objects,
which are processed and completed asynchronously. (There are synchronous
wrappers, however.) Messages are built from one or more
:c:type:`struct spi_transfer <spi_transfer>` objects, each of
which wraps a full duplex SPI transfer. A variety of protocol tweaking
options are needed, because different chips adopt very different
policies for how they use the bits transferred with SPI.

.. kernel-doc:: include/linux/spi/spi.h
   :internal:

.. kernel-doc:: drivers/spi/spi.c
   :functions: spi_register_board_info

.. kernel-doc:: drivers/spi/spi.c
   :export:

I\ :sup:`2`\ C and SMBus Subsystem
==================================

I\ :sup:`2`\ C (or without fancy typography, "I2C") is an acronym for
the "Inter-IC" bus, a simple bus protocol which is widely used where low
data rate communications suffice. Since it's also a licensed trademark,
some vendors use another name (such as "Two-Wire Interface", TWI) for
the same bus. I2C only needs two signals (SCL for clock, SDA for data),
conserving board real estate and minimizing signal quality issues. Most
I2C devices use seven bit addresses, and bus speeds of up to 400 kHz;
there's a high speed extension (3.4 MHz) that's not yet found wide use.
I2C is a multi-master bus; open drain signaling is used to arbitrate
between masters, as well as to handshake and to synchronize clocks from
slower clients.

The Linux I2C programming interfaces support only the master side of bus
interactions, not the slave side. The programming interface is
structured around two kinds of driver, and two kinds of device. An I2C
"Adapter Driver" abstracts the controller hardware; it binds to a
physical device (perhaps a PCI device or platform_device) and exposes a
:c:type:`struct i2c_adapter <i2c_adapter>` representing each
I2C bus segment it manages. On each I2C bus segment will be I2C devices
represented by a :c:type:`struct i2c_client <i2c_client>`.
Those devices will be bound to a :c:type:`struct i2c_driver
<i2c_driver>`, which should follow the standard Linux driver
model. (At this writing, a legacy model is more widely used.) There are
functions to perform various I2C protocol operations; at this writing
all such functions are usable only from task context.

The System Management Bus (SMBus) is a sibling protocol. Most SMBus
systems are also I2C conformant. The electrical constraints are tighter
for SMBus, and it standardizes particular protocol messages and idioms.
Controllers that support I2C can also support most SMBus operations, but
SMBus controllers don't support all the protocol options that an I2C
controller will. There are functions to perform various SMBus protocol
operations, either using I2C primitives or by issuing SMBus commands to
i2c_adapter devices which don't support those I2C operations.

.. kernel-doc:: include/linux/i2c.h
   :internal:

.. kernel-doc:: drivers/i2c/i2c-boardinfo.c
   :functions: i2c_register_board_info

.. kernel-doc:: drivers/i2c/i2c-core.c
   :export:

High Speed Synchronous Serial Interface (HSI)
=============================================

High Speed Synchronous Serial Interface (HSI) is a serial interface
mainly used for connecting application engines (APE) with cellular modem
engines (CMT) in cellular handsets. HSI provides multiplexing for up to
16 logical channels, low-latency and full duplex communication.

.. kernel-doc:: include/linux/hsi/hsi.h
   :internal:

.. kernel-doc:: drivers/hsi/hsi_core.c
   :export:

Pulse-Width Modulation (PWM)
============================

Pulse-width modulation is a modulation technique primarily used to
control power supplied to electrical devices.

The PWM framework provides an abstraction for providers and consumers of
PWM signals. A controller that provides one or more PWM signals is
registered as :c:type:`struct pwm_chip <pwm_chip>`. Providers
are expected to embed this structure in a driver-specific structure.
This structure contains fields that describe a particular chip.

A chip exposes one or more PWM signal sources, each of which exposed as
a :c:type:`struct pwm_device <pwm_device>`. Operations can be
performed on PWM devices to control the period, duty cycle, polarity and
active state of the signal.

Note that PWM devices are exclusive resources: they can always only be
used by one consumer at a time.

.. kernel-doc:: include/linux/pwm.h
   :internal:

.. kernel-doc:: drivers/pwm/core.c
   :export:
