.. SPDX-License-Identifier: GPL-2.0

=================================
Intel Touch Host Controller (THC)
=================================

Touch Host Controller is the name of the IP block in PCH that interface with Touch Devices (ex:
touchscreen, touchpad etc.). It is comprised of 3 key functional blocks:

- A natively half-duplex Quad I/O capable SPI master
- Low latency I2C interface to support HIDI2C compliant devices
- A HW sequencer with RW DMA capability to system memory

It has a single root space IOSF Primary interface that supports transactions to/from touch devices.
Host driver configures and controls the touch devices over THC interface. THC provides high
bandwidth DMA services to the touch driver and transfers the HID report to host system main memory.

Hardware sequencer within the THC is responsible for transferring (via DMA) data from touch devices
into system memory. A ring buffer is used to avoid data loss due to asynchronous nature of data
consumption (by host) in relation to data production (by touch device via DMA).

Unlike other common SPI/I2C controllers, THC handles the HID device data interrupt and reset
signals directly.

1. Overview
===========

1.1 THC software/hardware stack
-------------------------------

Below diagram illustrates the high-level architecture of THC software/hardware stack, which is fully
capable of supporting HIDSPI/HIDI2C protocol in Linux OS.

::

  ----------------------------------------------
 |      +-----------------------------------+   |
 |      |           Input Device            |   |
 |      +-----------------------------------+   |
 |      +-----------------------------------+   |
 |      |       HID Multi-touch Driver      |   |
 |      +-----------------------------------+   |
 |      +-----------------------------------+   |
 |      |             HID Core              |   |
 |      +-----------------------------------+   |
 |      +-----------------------------------+   |
 |      |    THC QuickSPI/QuickI2C Driver   |   |
 |      +-----------------------------------+   |
 |      +-----------------------------------+   |
 |      |      THC Hardware Driver          |   |
 |      +-----------------------------------+   |
 |      +----------------+ +----------------+   |
 |  SW  | PCI Bus Driver | | ACPI Resource  |   |
 |      +----------------+ +----------------+   |
  ----------------------------------------------
  ----------------------------------------------
 |      +-----------------------------------+   |
 |  HW  |              PCI Bus              |   |
 |      +-----------------------------------+   |
 |      +-----------------------------------+   |
 |      |           THC Controller          |   |
 |      +-----------------------------------+   |
 |      +-----------------------------------+   |
 |      |              Touch IC             |   |
 |      +-----------------------------------+   |
  ----------------------------------------------

Touch IC (TIC), also as known as the Touch devices (touchscreen or touchpad). The discrete analog
components that sense and transfer either discrete touch data or heatmap data in the form of HID
reports over the SPI/I2C bus to the THC Controller on the host.

THC Host Controller, which is a PCI device HBA (host bus adapter), integrated into the PCH, that
serves as a bridge between the Touch ICs and the host.

THC Hardware Driver, provides THC hardware operation APIs for above QuickSPI/QuickI2C driver, it
accesses THC MMIO registers to configure and control THC hardware.

THC QuickSPI/QuickI2C driver, also as known as HIDSPI/HIDI2C driver, is registered as a HID
low-level driver that manages the THC Controller and implements HIDSPI/HIDI2C protocol.


1.2 THC hardware diagram
------------------------
Below diagram shows THC hardware components::

                      ---------------------------------
                     |          THC Controller         |
                     |  +---------------------------+  |
                     |  |     PCI Config Space      |  |
                     |  +---------------------------+  |
                     |  +---------------------------+  |
                     |  +       MMIO Registers      |  |
                     |  +---------------------------+  |
 +---------------+   |  +------------+ +------------+  |
 | System Memory +---+--+      DMA   | |   PIO      |  |
 +---------------+   |  +------------+ +------------+  |
                     |  +---------------------------+  |
                     |  |       HW Sequencer        |  |
                     |  +---------------------------+  |
                     |  +------------+ +------------+  |
                     |  |  SPI/I2C   | |    GPIO    |  |
                     |  | Controller | | Controller |  |
                     |  +------------+ +------------+  |
                      ---------------------------------

As THC is exposed as a PCI devices, so it has standard PCI config space registers for PCI
enumeration and configuration.

MMIO Registers, which provide registers access for driver to configure and control THC hardware,
the registers include several categories: Interrupt status and control, DMA configure,
PIO (Programmed I/O, defined in section 3.2) status and control, SPI bus configure, I2C subIP
status and control, reset status and control...

THC provides two ways for driver to communicate with external Touch ICs: PIO and DMA.
PIO can let driver manually write/read data to/from Touch ICs, instead, THC DMA can
automatically write/read data without driver involved.

HW Sequencer includes THC major logic, it gets instruction from MMIO registers to control
SPI bus and I2C bus to finish a bus data transaction, it also can automatically handle
Touch ICs interrupt and start DMA receive/send data from/to Touch ICs according to interrupt
type. That means THC HW Sequencer understands HIDSPI/HIDI2C transfer protocol, and handle
the communication without driver involved, what driver needs to do is just configure the THC
properly, and prepare the formatted data packet or handle received data packet.

As THC supports HIDSPI/HIDI2C protocols, it has SPI controller and I2C subIP in it to expose
SPI bus and I2C bus. THC also integrates a GPIO controller to provide interrupt line support
and reset line support.

2. THC Hardware Interface
=========================

2.1 Host Interface
------------------

THC is exposed as "PCI Digitizer device" to the host. The PCI product and device IDs are
changed from different generations of processors. So the source code which enumerates drivers
needs to update from generation to generation.


2.2 Device Interface
--------------------

THC supports two types of bus for Touch IC connection: Enhanced SPI bus and I2C bus.

2.2.1 SPI Port
~~~~~~~~~~~~~~

When PORT_TYPE = 00b in MMIO registers, THC uses SPI interfaces to communicate with external
Touch IC. THC enhanced SPI Bus supports different SPI modes: standard Single IO mode,
Dual IO mode and Quad IO mode.

In Single IO mode, THC drives MOSI line to send data to Touch ICs, and receives data from Touch
ICs data from MISO line. In Dual IO mode, THC drivers MOSI and MISO both for data sending, and
also receives the data on both line. In Quad IO mode, there are other two lines (IO2 and IO3)
are added, THC drives MOSI (IO0), MISO (IO1), IO2 and IO3 at the same time for data sending, and
also receives the data on those 4 lines. Driver needs to configure THC in different mode by
setting different opcode.

Beside IO mode, driver also needs to configure SPI bus speed. THC supports up to 42MHz SPI clock
on Intel Lunar Lake platform.

For THC sending data to Touch IC, the data flow on SPI bus::

 | --------------------THC sends---------------------------------|
 <8Bits OPCode><24Bits Slave Address><Data><Data><Data>...........

For THC receiving data from Touch IC, the data flow on SPI bus::

 | ---------THC Sends---------------||-----Touch IC sends--------|
 <8Bits OPCode><24Bits Slave Address><Data><Data><Data>...........

2.2.2 I2C Port
~~~~~~~~~~~~~~

THC also integrates I2C controller in it, it's called I2C SubSystem. When PORT_TYPE = 01, THC
is configured to I2C mode. Comparing to SPI mode which can be configured through MMIO registers
directly, THC needs to use PIO read (by setting SubIP read opcode) to I2C subIP APB registers'
value and use PIO write (by setting SubIP write opcode) to do a write operation.

2.2.3 GPIO interface
~~~~~~~~~~~~~~~~~~~~

THC also includes two GPIO pins, one for interrupt and the other for device reset control.

Interrupt line can be configured to either level triggerred or edge triggerred by setting MMIO
Control register.

Reset line is controlled by BIOS (or EFI) through ACPI _RST method, driver needs to call this
device ACPI _RST method to reset touch IC during initialization.

3. High level concept
=====================

3.1 Opcode
----------

Opcode (operation code) is used to tell THC or Touch IC what the operation will be, such as PIO
read or PIO write.

When THC is configured to SPI mode, opcodes are used for determining the read/write IO mode.
There are some OPCode examples for SPI IO mode:

=======   ==============================
opcode    Corresponding SPI command
=======   ==============================
0x0B      Read Single I/O
0x02      Write Single I/O
0xBB      Read Dual I/O
0xB2      Write Dual I/O
0xEB      Read Quad I/O
0xE2      Write Quad I/O
=======   ==============================

In general, different touch IC has different OPCode definition. According to HIDSPI
protocol whitepaper, those OPCodes are defined in device ACPI table, and driver needs to
query those information through OS ACPI APIs during driver initialization, then configures
THC MMIO OPCode registers with correct setting.

When THC is working in I2C mode, opcodes are used to tell THC what's the next PIO type:
I2C SubIP APB register read, I2C SubIP APB register write, I2C touch IC device read,
I2C touch IC device write, I2C touch IC device write followed by read.

Here are the THC pre-defined opcodes for I2C mode:

=======   ===================================================   ===========
opcode    Corresponding I2C command                             Address
=======   ===================================================   ===========
0x12      Read I2C SubIP APB internal registers                 0h - FFh
0x13      Write I2C SubIP APB internal registers                0h - FFh
0x14      Read external Touch IC through I2C bus                N/A
0x18      Write external Touch IC through I2C bus               N/A
0x1C      Write then read external Touch IC through I2C bus     N/A
=======   ===================================================   ===========

3.2 PIO
-------

THC provides a programmed I/O (PIO) access interface for the driver to access the touch IC's
configuration registers, or access I2C subIP's configuration registers. To use PIO to perform
I/O operations, driver should pre-program PIO control registers and PIO data registers and kick
off the sequencing cycle. THC uses different PIO opcodes to distinguish different PIO
operations (PIO read/write/write followed by read).

If there is a Sequencing Cycle In Progress and an attempt is made to program any of the control,
address, or data register the cycle is blocked and a sequence error will be encountered.

A status bit indicates when the cycle has completed allowing the driver to know when read results
can be checked and/or when to initiate a new command. If enabled, the cycle done assertion can
interrupt driver with an interrupt.

Because THC only has 16 FIFO registers for PIO, so all the data transfer through PIO shouldn't
exceed 64 bytes.

As DMA needs max packet size for transferring configuration, and the max packet size information
always in HID device descriptor which needs THC driver to read it out from HID Device (Touch IC).
So PIO typical use case is, before DMA initialization, write RESET command (PIO write), read
RESET response (PIO read or PIO write followed by read), write Power ON command (PIO write), read
device descriptor (PIO read).

For how to issue a PIO operation, here is the steps which driver needs follow:

- Program read/write data size in THC_SS_BC.
- Program I/O target address in THC_SW_SEQ_DATA0_ADDR.
- If write, program the write data in THC_SW_SEQ_DATA0..THC_SW_SEQ_DATAn.
- Program the PIO opcode in THC_SS_CMD.
- Set TSSGO = 1 to start the PIO write sequence.
- If THC_SS_CD_IE = 1, SW will receives a MSI when the PIO is completed.
- If read, read out the data in THC_SW_SEQ_DATA0..THC_SW_SEQ_DATAn.

3.3 DMA
-------

THC has 4 DMA channels: Read DMA1, Read DMA2, Write DMA and Software DMA.

3.3.1 Read DMA Channel
~~~~~~~~~~~~~~~~~~~~~~

THC has two Read DMA engines: 1st RxDMA (RxDMA1) and 2nd RxDMA (RxDMA2). RxDMA1 is reserved for
raw data mode. RxDMA2 is used for HID data mode and it is the RxDMA engine currently driver uses
for HID input report data retrieval.

RxDMA's typical use case is auto receiving the data from Touch IC. Once RxDMA is enabled by
software, THC will start auto-handling receiving logic.

For SPI mode, THC RxDMA sequence is: when Touch IC triggers a interrupt to THC, THC reads out
report header to identify what's the report type, and what's the report length, according to
above information, THC reads out report body to internal FIFO and start RxDMA coping the data
to system memory. After that, THC update interrupt cause register with report type, and update
RxDMA PRD table read pointer, then trigger a MSI interrupt to notify driver RxDMA finishing
data receiving.

For I2C mode, THC RxDMA's behavior is a little bit different, because of HIDI2C protocol difference
with HIDSPI protocol, RxDMA only be used to receive input report. The sequence is, when Touch IC
triggers a interrupt to THC, THC first reads out 2 bytes from input report address to determine the
packet length, then use this packet length to start a DMA reading from input report address for
input report data. After that, THC update RxDMA PRD table read pointer, then trigger a MSI interrupt
to notify driver input report data is ready in system memory.

All above sequence is hardware automatically handled, all driver needs to do is configure RxDMA and
waiting for interrupt ready then read out the data from system memory.

3.3.2 Software DMA channel
~~~~~~~~~~~~~~~~~~~~~~~~~~

THC supports a software triggerred RxDMA mode to read the touch data from touch IC. This SW RxDMA
is the 3rd THC RxDMA engine with the similar functionalities as the existing two RxDMAs, the only
difference is this SW RxDMA is triggerred by software, and RxDMA2 is triggerred by external Touch IC
interrupt. It gives a flexiblity to software driver to use RxDMA read Touch IC data in any time.

Before software starts a SW RxDMA, it shall stop the 1st and 2nd RxDMA, clear PRD read/write pointer
and quiesce the device interrupt (THC_DEVINT_QUIESCE_HW_STS = 1), other operations are the same with
RxDMA.

3.3.3 Write DMA Channel
~~~~~~~~~~~~~~~~~~~~~~~

THC has one write DMA engine, which can be used for sending data to Touch IC automatically.
According to HIDSPI and HIDI2C protocol, every time only one command can be sent to touch IC, and
before last command is completely handled, next command cannot be sent, THC write DMA engine only
supports single PRD table.

What driver needs to do is, preparing PRD table and DMA buffer, then copy data to DMA buffer and
update PRD table with buffer address and buffer length, then start write DMA. THC will
automatically send the data to touch IC, and trigger a DMA completion interrupt once transferring
is done.

3.4 PRD
-------

Physical Region Descriptor (PRD) provides the memory mapping description for THC DMAs.

3.4.1 PRD table and entry
~~~~~~~~~~~~~~~~~~~~~~~~~

In order to improve physical DMA memory usage, modern drivers trend to allocate a virtually
contiguous, but physically fragmented buffer of memory for each data buffer. Linux OS also
provide SGL (scatter gather list) APIs to support this usage.

THC uses PRD table (physical region descriptor) to support the corresponding OS kernel
SGL that describes the virtual to physical buffer mapping.

::

  ------------------------      --------------       --------------
 | PRD table base address +----+ PRD table #1 +-----+ PRD Entry #1 |
  ------------------------      --------------       --------------
                                                     --------------
                                                    | PRD Entry #2 |
                                                     --------------
                                                     --------------
                                                    | PRD Entry #n |
                                                     --------------

The read DMA engine supports multiple PRD tables held within a circular buffer that allow the THC
to support multiple data buffers from the Touch IC. This allows host SW to arm the Read DMA engine
with multiple buffers, allowing the Touch IC to send multiple data frames to the THC without SW
interaction. This capability is required when the CPU processes touch frames slower than the
Touch IC can send them.

To simplify the design, SW assumes worst-case memory fragmentation. Therefore,each PRD table shall
contain the same number of PRD entries, allowing for a global register (per Touch IC) to hold the
number of PRD-entries per PRD table.

SW allocates up to 128 PRD tables per Read DMA engine as specified in the THC_M_PRT_RPRD_CNTRL.PCD
register field. The number of PRD tables should equal the number of data buffers.

Max OS memory fragmentation will be at a 4KB boundary, thus to address 1MB of virtually contiguous
memory 256 PRD entries are required for a single PRD Table. SW writes the number of PRD entries
for each PRD table in the THC_M_PRT_RPRD_CNTRL.PTEC register field. The PRD entry's length must be
multiple of 4KB except for the last entry in a PRD table.

SW allocates all the data buffers and PRD tables only once at host initialization.

3.4.2 PRD Write pointer and read pointer
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As PRD tables are organized as a Circular Buffer (CB), a read pointer and a write pointer for a CB
are needed.

DMA HW consumes the PRD tables in the CB, one PRD entry at a time until the EOP bit is found set
in a PRD entry. At this point HW increments the PRD read pointer. Thus, the read pointer points
to the PRD which the DMA engine is currently processing. This pointer rolls over once the circular
buffer's depth has been traversed with bit[7] the Rollover bit. E.g. if the DMA CB depth is equal
to 4 entries (0011b), then the read pointers will follow this pattern (HW is required to honor
this behavior): 00h 01h 02h 03h 80h 81h 82h 83h 00h 01h ...

The write pointer is updated by SW. The write pointer points to location in the DMA CB, where the
next PRD table is going to be stored. SW needs to ensure that this pointer rolls over once the
circular buffer's depth has been traversed with Bit[7] as the rollover bit. E.g. if the DMA CB
depth is equal to 5 entries (0100b), then the write pointers will follow this pattern (SW is
required to honor this behavior): 00h 01h 02h 03h 04h 80h 81h 82h 83h 84h 00h 01h ..

3.4.3 PRD descriptor structure
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Intel THC uses PRD entry descriptor for every PRD entry. Every PRD entry descriptor occupies
128 bits memories:

===================   ========   ===============================================
struct field          bit(s)     description
===================   ========   ===============================================
dest_addr             53..0      destination memory address, as every entry
                                 is 4KB, ignore lowest 10 bits of address.
reserved1             54..62     reserved
int_on_completion     63         completion interrupt enable bit, if this bit
                                 set it means THC will trigger a completion
                                 interrupt. This bit is set by SW driver.
len                   87..64     how many bytes of data in this entry.
end_of_prd            88         end of PRD table bit, if this bit is set,
                                 it means this entry is last entry in this PRD
                                 table. This bit is set by SW driver.
hw_status             90..89     HW status bits
reserved2             127..91    reserved
===================   ========   ===============================================

And one PRD table can include up to 256 PRD entries, as every entries is 4K bytes, so every
PRD table can describe 1M bytes memory.

.. code-block:: c

   struct thc_prd_table {
        struct thc_prd_entry entries[PRD_ENTRIES_NUM];
   };

In general, every PRD table means one HID touch data packet. Every DMA engine can support
up to 128 PRD tables (except write DMA, write DMA only has one PRD table). SW driver is responsible
to get max packet length from touch IC, and use this max packet length to create PRD entries for
each PRD table.

4. HIDSPI support (QuickSPI)
============================

Intel THC is total compatible with HIDSPI protocol, THC HW sequenser can accelerate HIDSPI
protocol transferring.

4.1 Reset Flow
--------------

- Call ACPI _RST method to reset Touch IC device.
- Read the reset response from TIC through PIO read.
- Issue a command to retrieve device descriptor from Touch IC through PIO write.
- Read the device descriptor from Touch IC through PIO read.
- If the device descriptor is valid, allocate DMA buffers and configure all DMA channels.
- Issue a command to retrieve report descriptor from Touch IC through DMA.

4.2 Input Report Data Flow
--------------------------

Basic Flow:

- Touch IC interrupts the THC Controller using an in-band THC interrupt.
- THC Sequencer reads the input report header by transmitting read approval as a signal
  to the Touch IC to prepare for host to read from the device.
- THC Sequencer executes a Input Report Body Read operation corresponding to the value
  reflected in “Input Report Length” field of the Input Report Header.
- THC DMA engine begins fetching data from the THC Sequencer and writes to host memory
  at PRD entry 0 for the current CB PRD table entry. This process continues until the
  THC Sequencer signals all data has been read or the THC DMA Read Engine reaches the
  end of it's last PRD entry (or both).
- The THC Sequencer checks for the “Last Fragment Flag” bit in the Input Report Header.
  If it is clear, the THC Sequencer enters an idle state.
- If the “Last Fragment Flag” bit is enabled the THC Sequencer enters End-of-Frame Processing.

THC Sequencer End of Frame Processing:

- THC DMA engine increments the read pointer of the Read PRD CB, sets EOF interrupt status
  in RxDMA2 register (THC_M_PRT_READ_DMA_INT_STS_2).
- If THC EOF interrupt is enabled by the driver in the control register (THC_M_PRT_READ_DMA_CNTRL_2),
  generates interrupt to software.

Sequence of steps to read data from RX DMA buffer:

- THC QuickSPI driver checks CB write Ptr and CB read Ptr to identify if any data frame in DMA
  circular buffers.
- THC QuickSPI driver gets first unprocessed PRD table.
- THC QuickSPI driver scans all PRD entries in this PRD table to calculate the total frame size.
- THC QuickSPI driver copies all frame data out.
- THC QuickSPI driver checks the data type according to input report body, and calls related
  callbacks to process the data.
- THC QuickSPI driver updates write Ptr.

4.3 Output Report Data Flow
---------------------------

Generic Output Report Flow:

- HID core calls raw_request callback with a request to THC QuickSPI driver.
- THC QuickSPI Driver converts request provided data into the output report packet and copies it
  to THC's write DMA buffer.
- Start TxDMA to complete the write operation.

5. HIDI2C support (QuickI2C)
============================

5.1 Reset Flow
--------------

- Read device descriptor from Touch IC device through PIO write followed by read.
- If the device descriptor is valid, allocate DMA buffers and configure all DMA channels.
- Use PIO or TxDMA to write a SET_POWER request to TIC's command register, and check if the
  write operation is successfully completed.
- Use PIO or TxDMA to write a RESET request to TIC's command register. If the write operation
  is successfully completed, wait for reset response from TIC.
- Use SWDMA to read report descriptor through TIC's report descriptor register.

5.2 Input Report Data Flow
--------------------------

Basic Flow:

- Touch IC asserts the interrupt indicating that it has an interrupt to send to HOST.
  THC Sequencer issues a READ request over the I2C bus. The HIDI2C device returns the
  first 2 bytes from the HIDI2C device which contains the length of the received data.
- THC Sequencer continues the Read operation as per the size of data indicated in the
  length field.
- THC DMA engine begins fetching data from the THC Sequencer and writes to host memory
  at PRD entry 0 for the current CB PRD table entry. THC writes 2Bytes for length field
  plus the remaining data to RxDMA buffer. This process continues until the THC Sequencer
  signals all data has been read or the THC DMA Read Engine reaches the end of it's last
  PRD entry (or both).
- THC Sequencer enters End-of-Input Report Processing.
- If the device has no more input reports to send to the host, it de-asserts the interrupt
  line. For any additional input reports, device keeps the interrupt line asserted and
  steps 1 through 4 in the flow are repeated.

THC Sequencer End of Input Report Processing:

- THC DMA engine increments the read pointer of the Read PRD CB, sets EOF interrupt status
  in RxDMA 2 register (THC_M_PRT_READ_DMA_INT_STS_2).
- If THC EOF interrupt is enabled by the driver in the control register
  (THC_M_PRT_READ_DMA_CNTRL_2), generates interrupt to software.

Sequence of steps to read data from RX DMA buffer:

- THC QuickI2C driver checks CB write Ptr and CB read Ptr to identify if any data frame in DMA
  circular buffers.
- THC QuickI2C driver gets first unprocessed PRD table.
- THC QuickI2C driver scans all PRD entries in this PRD table to calculate the total frame size.
- THC QuickI2C driver copies all frame data out.
- THC QuickI2C driver call hid_input_report to send the input report content to HID core, which
  includes Report ID + Report Data Content (remove the length field from the original report
  data).
- THC QuickI2C driver updates write Ptr.

5.3 Output Report Data Flow
---------------------------

Generic Output Report Flow:

- HID core call THC QuickI2C raw_request callback.
- THC QuickI2C uses PIO or TXDMA to write a SET_REPORT request to TIC's command register. Report
  type in SET_REPORT should be set to Output.
- THC QuickI2C programs TxDMA buffer with TX Data to be written to TIC's data register. The first
  2 bytes should indicate the length of the report followed by the report contents including
  Report ID.

6. THC Debugging
================

To debug THC, event tracing mechanism is used. To enable debug logs::

  echo 1 > /sys/kernel/debug/tracing/events/intel_thc/enable
  cat /sys/kernel/debug/tracing/trace

7. Reference
============
- HIDSPI: https://download.microsoft.com/download/c/a/0/ca07aef3-3e10-4022-b1e9-c98cea99465d/HidSpiProtocolSpec.pdf
- HIDI2C: https://download.microsoft.com/download/7/d/d/7dd44bb7-2a7a-4505-ac1c-7227d3d96d5b/hid-over-i2c-protocol-spec-v1-0.docx
