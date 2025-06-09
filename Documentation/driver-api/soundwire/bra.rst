==========================
Bulk Register Access (BRA)
==========================

Conventions
-----------

Capitalized words used in this documentation are intentional and refer
to concepts of the SoundWire 1.x specification.

Introduction
------------

The SoundWire 1.x specification provides a mechanism to speed-up
command/control transfers by reclaiming parts of the audio
bandwidth. The Bulk Register Access (BRA) protocol is a standard
solution based on the Bulk Payload Transport (BPT) definitions.

The regular control channel uses Column 0 and can only send/retrieve
one byte per frame with write/read commands. With a typical 48kHz
frame rate, only 48kB/s can be transferred.

The optional Bulk Register Access capability can transmit up to 12
Mbits/s and reduce transfer times by several orders of magnitude, but
has multiple design constraints:

  (1) Each frame can only support a read or a write transfer, with a
      10-byte overhead per frame (header and footer response).

  (2) The read/writes SHALL be from/to contiguous register addresses
      in the same frame. A fragmented register space decreases the
      efficiency of the protocol by requiring multiple BRA transfers
      scheduled in different frames.

  (3) The targeted Peripheral device SHALL support the optional Data
      Port 0, and likewise the Manager SHALL expose audio-like Ports
      to insert BRA packets in the audio payload using the concepts of
      Sample Interval, HSTART, HSTOP, etc.

  (4) The BRA transport efficiency depends on the available
      bandwidth. If there are no on-going audio transfers, the entire
      frame minus Column 0 can be reclaimed for BRA. The frame shape
      also impacts efficiency: since Column0 cannot be used for
      BTP/BRA, the frame should rely on a large number of columns and
      minimize the number of rows. The bus clock should be as high as
      possible.

  (5) The number of bits transferred per frame SHALL be a multiple of
      8 bits. Padding bits SHALL be inserted if necessary at the end
      of the data.

  (6) The regular read/write commands can be issued in parallel with
      BRA transfers. This is convenient to e.g. deal with alerts, jack
      detection or change the volume during firmware download, but
      accessing the same address with two independent protocols has to
      be avoided to avoid undefined behavior.

  (7) Some implementations may not be capable of handling the
      bandwidth of the BRA protocol, e.g. in the case of a slow I2C
      bus behind the SoundWire IP. In this case, the transfers may
      need to be spaced in time or flow-controlled.

  (8) Each BRA packet SHALL be marked as 'Active' when valid data is
      to be transmitted. This allows for software to allocate a BRA
      stream but not transmit/discard data while processing the
      results or preparing the next batch of data, or allowing the
      peripheral to deal with the previous transfer. In addition BRA
      transfer can be started early on without data being ready.

  (9) Up to 470 bytes may be transmitted per frame.

  (10) The address is represented with 32 bits and does not rely on
       the paging registers used for the regular command/control
       protocol in Column 0.


Error checking
--------------

Firmware download is one of the key usages of the Bulk Register Access
protocol. To make sure the binary data integrity is not compromised by
transmission or programming errors, each BRA packet provides:

  (1) A CRC on the 7-byte header. This CRC helps the Peripheral Device
      check if it is addressed and set the start address and number of
      bytes. The Peripheral Device provides a response in Byte 7.

  (2) A CRC on the data block (header excluded). This CRC is
      transmitted as the last-but-one byte in the packet, prior to the
      footer response.

The header response can be one of:
  (a) Ack
  (b) Nak
  (c) Not Ready

The footer response can be one of:
  (1) Ack
  (2) Nak  (CRC failure)
  (3) Good (operation completed)
  (4) Bad  (operation failed)

Example frame
-------------

The example below is not to scale and makes simplifying assumptions
for clarity. The different chunks in the BRA packets are not required
to start on a new SoundWire Row, and the scale of data may vary.

      ::

	+---+--------------------------------------------+
	+   |                                            |
	+   |             BRA HEADER                     |
	+   |                                            |
	+   +--------------------------------------------+
	+ C |             HEADER CRC                     |
	+ O +--------------------------------------------+
	+ M | 	          HEADER RESPONSE                |
	+ M +--------------------------------------------+
	+ A |                                            |
	+ N |                                            |
	+ D |                 DATA                       |
	+   |                                            |
	+   |                                            |
	+   |                                            |
	+   +--------------------------------------------+
	+   |             DATA CRC                       |
	+   +--------------------------------------------+
	+   | 	          FOOTER RESPONSE                |
	+---+--------------------------------------------+


Assuming the frame uses N columns, the configuration shown above can
be programmed by setting the DP0 registers as:

    - HSTART = 1
    - HSTOP = N - 1
    - Sampling Interval = N
    - WordLength = N - 1

Addressing restrictions
-----------------------

The Device Number specified in the Header follows the SoundWire
definitions, and broadcast and group addressing are permitted. For now
the Linux implementation only allows for a single BPT transfer to a
single device at a time. This might be revisited at a later point as
an optimization to send the same firmware to multiple devices, but
this would only be beneficial for single-link solutions.

In the case of multiple Peripheral devices attached to different
Managers, the broadcast and group addressing is not supported by the
SoundWire specification. Each device must be handled with separate BRA
streams, possibly in parallel - the links are really independent.

Unsupported features
--------------------

The Bulk Register Access specification provides a number of
capabilities that are not supported in known implementations, such as:

  (1) Transfers initiated by a Peripheral Device. The BRA Initiator is
      always the Manager Device.

  (2) Flow-control capabilities and retransmission based on the
      'NotReady' header response require extra buffering in the
      SoundWire IP and are not implemented.

Bi-directional handling
-----------------------

The BRA protocol can handle writes as well as reads, and in each
packet the header and footer response are provided by the Peripheral
Target device. On the Peripheral device, the BRA protocol is handled
by a single DP0 data port, and at the low-level the bus ownership can
will change for header/footer response as well as the data transmitted
during a read.

On the host side, most implementations rely on a Port-like concept,
with two FIFOs consuming/generating data transfers in parallel
(Host->Peripheral and Peripheral->Host). The amount of data
consumed/produced by these FIFOs is not symmetrical, as a result
hardware typically inserts markers to help software and hardware
interpret raw data

Each packet will typically have:

  (1) a 'Start of Packet' indicator.

  (2) an 'End of Packet' indicator.

  (3) a packet identifier to correlate the data requested and
      transmitted, and the error status for each frame

Hardware implementations can check errors at the frame level, and
retry a transfer in case of errors. However, as for the flow-control
case, this requires extra buffering and intelligence in the
hardware. The Linux support assumes that the entire transfer is
cancelled if a single error is detected in one of the responses.

Abstraction required
~~~~~~~~~~~~~~~~~~~~

There are no standard registers or mandatory implementation at the
Manager level, so the low-level BPT/BRA details must be hidden in
Manager-specific code. For example the Cadence IP format above is not
known to the codec drivers.

Likewise, codec drivers should not have to know the frame size. The
computation of CRC and handling of responses is handled in helpers and
Manager-specific code.

The host BRA driver may also have restrictions on pages allocated for
DMA, or other host-DSP communication protocols. The codec driver
should not be aware of any of these restrictions, since it might be
reused in combination with different implementations of Manager IPs.

Concurrency between BRA and regular read/write
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The existing 'nread/nwrite' API already relies on a notion of start
address and number of bytes, so it would be possible to extend this
API with a 'hint' requesting BPT/BRA be used.

However BRA transfers could be quite long, and the use of a single
mutex for regular read/write and BRA is a show-stopper. Independent
operation of the control/command and BRA transfers is a fundamental
requirement, e.g. to change the volume level with the existing regmap
interface while downloading firmware. The integration must however
ensure that there are no concurrent access to the same address with
the command/control protocol and the BRA protocol.

In addition, the 'sdw_msg' structure hard-codes support for 16-bit
addresses and paging registers which are irrelevant for BPT/BRA
support based on native 32-bit addresses. A separate API with
'sdw_bpt_msg' makes more sense.

One possible strategy to speed-up all initialization tasks would be to
start a BRA transfer for firmware download, then deal with all the
"regular" read/writes in parallel with the command channel, and last
to wait for the BRA transfers to complete. This would allow for a
degree of overlap instead of a purely sequential solution. As such,
the BRA API must support async transfers and expose a separate wait
function.


Peripheral/bus interface
------------------------

The bus interface for BPT/BRA is made of two functions:

    - sdw_bpt_send_async(bpt_message)

      This function sends the data using the Manager
      implementation-defined capabilities (typically DMA or IPC
      protocol).

      Queueing is currently not supported, the caller
      needs to wait for completion of the requested transfer.

   - sdw_bpt_wait()

      This function waits for the entire message provided by the
      codec driver in the 'send_async' stage. Intermediate status for
      smaller chunks will not be provided back to the codec driver,
      only a return code will be provided.

Regmap use
~~~~~~~~~~

Existing codec drivers rely on regmap to download firmware to
Peripherals. regmap exposes an async interface similar to the
send/wait API suggested above, so at a high-level it would seem
natural to combine BRA and regmap. The regmap layer could check if BRA
is available or not, and use a regular read-write command channel in
the latter case.

The regmap integration will be handled in a second step.

BRA stream model
----------------

For regular audio transfers, the machine driver exposes a dailink
connecting CPU DAI(s) and Codec DAI(s).

This model is not required BRA support:

   (1) The SoundWire DAIs are mainly wrappers for SoundWire Data
       Ports, with possibly some analog or audio conversion
       capabilities bolted behind the Data Port. In the context of
       BRA, the DP0 is the destination. DP0 registers are standard and
       can be programmed blindly without knowing what Peripheral is
       connected to each link. In addition, if there are multiple
       Peripherals on a link and some of them do not support DP0, the
       write commands to program DP0 registers will generate harmless
       COMMAND_IGNORED responses that will be wired-ORed with
       responses from Peripherals which support DP0. In other words,
       the DP0 programming can be done with broadcast commands, and
       the information on the Target device can be added only in the
       BRA Header.

   (2) At the CPU level, the DAI concept is not useful for BRA; the
       machine driver will not create a dailink relying on DP0. The
       only concept that is needed is the notion of port.

   (3) The stream concept relies on a set of master_rt and slave_rt
       concepts. All of these entities represent ports and not DAIs.

   (4) With the assumption that a single BRA stream is used per link,
       that stream can connect master ports as well as all peripheral
       DP0 ports.

   (5) BRA transfers only make sense in the context of one
       Manager/Link, so the BRA stream handling does not rely on the
       concept of multi-link aggregation allowed by regular DAI links.

Audio DMA support
-----------------

Some DMAs, such as HDaudio, require an audio format field to be
set. This format is in turn used to define acceptable bursts. BPT/BRA
support is not fully compatible with these definitions in that the
format and bandwidth may vary between read and write commands.

In addition, on Intel HDaudio Intel platforms the DMAs need to be
programmed with a PCM format matching the bandwidth of the BPT/BRA
transfer. The format is based on 192kHz 32-bit samples, and the number
of channels varies to adjust the bandwidth. The notion of channel is
completely notional since the data is not typical audio
PCM. Programming such channels helps reserve enough bandwidth and adjust
FIFO sizes to avoid xruns.

Alignment requirements are currently not enforced at the core level
but at the platform-level, e.g. for Intel the data sizes must be
multiples of 32 bytes.
