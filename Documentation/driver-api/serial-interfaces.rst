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

