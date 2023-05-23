=================
MIDI 2.0 on Linux
=================

General
=======

MIDI 2.0 is an extended protocol for providing higher resolutions and
more fine controls over the legacy MIDI 1.0.  The fundamental changes
introduced for supporting MIDI 2.0 are:

- Support of Universal MIDI Packet (UMP)
- Support of MIDI 2.0 protocol messages
- Transparent conversions between UMP and legacy MIDI 1.0 byte stream
- MIDI-CI for property and profile configurations

UMP is a new container format to hold all MIDI protocol 1.0 and MIDI
2.0 protocol messages.  Unlike the former byte stream, it's 32bit
aligned, and each message can be put in a single packet.  UMP can send
the events up to 16 "UMP Groups", where each UMP Group contain up to
16 MIDI channels.

MIDI 2.0 protocol is an extended protocol to achieve the higher
resolution and more controls over the old MIDI 1.0 protocol.

MIDI-CI is a high-level protocol that can talk with the MIDI device
for the flexible profiles and configurations.  It's represented in the
form of special SysEx.

For Linux implementations, the kernel supports the UMP transport and
the encoding/decoding of MIDI protocols on UMP, while MIDI-CI is
supported in user-space over the standard SysEx.

As of this writing, only USB MIDI device supports the UMP and Linux
2.0 natively.  The UMP support itself is pretty generic, hence it
could be used by other transport layers, although it could be
implemented differently (e.g. as a ALSA sequencer client), too.

The access to UMP devices are provided in two ways: the access via
rawmidi device and the access via ALSA sequencer API.

ALSA sequencer API was extended to allow the payload of UMP packets.
It's allowed to connect freely between MIDI 1.0 and MIDI 2.0 sequencer
clients, and the events are converted transparently.


Kernel Configuration
====================

The following new configs are added for supporting MIDI 2.0:
`CONFIG_SND_UMP`, `CONFIG_SND_UMP_LEGACY_RAWMIDI`,
`CONFIG_SND_SEQ_UMP`, `CONFIG_SND_SEQ_UMP_CLIENT`, and
`CONFIG_SND_USB_AUDIO_MIDI_V2`.  The first visible one is
`CONFIG_SND_USB_AUDIO_MIDI_V2`, and when you choose it (to set `=y`),
the core support for UMP (`CONFIG_SND_UMP`) and the sequencer binding
(`CONFIG_SND_SEQ_UMP_CLIENT`) will be automatically selected.

Additionally, `CONFIG_SND_UMP_LEGACY_RAWMIDI=y` will enable the
support for the legacy raw MIDI device for UMP Endpoints.


Rawmidi Device with USB MIDI 2.0
================================

When a device supports MIDI 2.0, the USB-audio driver probes and uses
the MIDI 2.0 interface (that is found always at the altset 1) as
default instead of the MIDI 1.0 interface (at altset 0).  You can
switch back to the binding with the old MIDI 1.0 interface by passing
`midi2_enable=0` option to snd-usb-audio driver module, too.

When the MIDI 2.0 device is probed, the kernel creates a rawmidi
device for each UMP Endpoint of the device.  Its device name is
`/dev/snd/umpC*D*` and different from the standard rawmidi device name
`/dev/snd/midiC*D*` for MIDI 1.0, in order to avoid confusing the
legacy applications accessing mistakenly to UMP devices.

You can read and write UMP packet data directly from/to this UMP
rawmidi device.  For example, reading via `hexdump` like below will
show the incoming UMP packets of the card 0 device 0 in the hex
format::

  % hexdump -C /dev/snd/umpC0D0
  00000000  01 07 b0 20 00 07 b0 20  64 3c 90 20 64 3c 80 20  |... ... d<. d<. |

Unlike the MIDI 1.0 byte stream, UMP is a 32bit packet, and the size
for reading or writing the device is also aligned to 32bit (which is 4
bytes).

The 32-bit words in the UMP packet payload are always in CPU native
endianness.  Transport drivers are responsible to convert UMP words
from / to system endianness to required transport endianness / byte
order.

When `CONFIG_SND_UMP_LEGACY_RAWMIDI` is set, the driver creates
another standard raw MIDI device additionally as `/dev/snd/midiC*D*`.
This contains 16 substreams, and each substream corresponds to a
(0-based) UMP Group.  Legacy applications can access to the specified
group via each substream in MIDI 1.0 byte stream format.  With the
ALSA rawmidi API, you can open the arbitrary substream, while just
opening `/dev/snd/midiC*D*` will end up with opening the first
substream.

Each UMP Endpoint can provide the additional information, constructed
from USB MIDI 2.0 descriptors.  And a UMP Endpoint may contain one or
more UMP Blocks, where UMP Block is an abstraction introduced in the
ALSA UMP implementations to represent the associations among UMP
Groups.  UMP Block corresponds to Group Terminal Block (GTB) in USB
MIDI 2.0 specifications but provide a few more generic information.
The information of UMP Endpoints and UMP Blocks are found in the proc
file `/proc/asound/card*/midi*`.  For example::

  % cat /proc/asound/card1/midi0
  ProtoZOA MIDI
  
  Type: UMP
  EP Name: ProtoZOA
  EP Product ID: ABCD12345678
  UMP Version: 0x0000
  Protocol Caps: 0x00000100
  Protocol: 0x00000100
  Num Blocks: 3
  
  Block 0 (ProtoZOA Main)
    Direction: bidirection
    Active: Yes
    Groups: 1-1
    Is MIDI1: No

  Block 1 (ProtoZOA Ext IN)
    Direction: output
    Active: Yes
    Groups: 2-2
    Is MIDI1: Yes (Low Speed)
  ....

Note that `Groups` field shown in the proc file above indicates the
1-based UMP Group numbers (from-to).

Those additional UMP Endpoint and UMP Block information can be
obtained via the new ioctls `SNDRV_UMP_IOCTL_ENDPOINT_INFO` and
`SNDRV_UMP_IOCTL_BLOCK_INFO`, respectively.

The rawmidi name and the UMP Endpoint name are usually identical, and
in the case of USB MIDI, it's taken from `iInterface` of the
corresponding USB MIDI interface descriptor.  If it's not provided,
it's copied from `iProduct` of the USB device descriptor as a
fallback.

The Endpoint Product ID is a string field and supposed to be unique.
It's copied from `iSerialNumber` of the device for USB MIDI.

The protocol capabilities and the actual protocol bits are defined in
`asound.h`.


ALSA Sequencer with USB MIDI 2.0
================================

In addition to the rawmidi interfaces, ALSA sequencer interface
supports the new UMP MIDI 2.0 device, too.  Now, each ALSA sequencer
client may set its MIDI version (0, 1 or 2) to declare itself being
either the legacy, UMP MIDI 1.0 or UMP MIDI 2.0 device, respectively.
The first, legacy client is the one that sends/receives the old
sequencer event as was.  Meanwhile, UMP MIDI 1.0 and 2.0 clients send
and receive in the extended event record for UMP.  The MIDI version is
seen in the new `midi_version` field of `snd_seq_client_info`.

A UMP packet can be sent/received in a sequencer event embedded by
specifying the new event flag bit `SNDRV_SEQ_EVENT_UMP`.  When this
flag is set, the event has 16 byte (128 bit) data payload for holding
the UMP packet.  Without the `SNDRV_SEQ_EVENT_UMP` bit flag, the event
is treated as a legacy event as it was (with max 12 byte data
payload).

With `SNDRV_SEQ_EVENT_UMP` flag set, the type field of a UMP sequencer
event is ignored (but it should be set to 0 as default).

The type of each client can be seen in `/proc/asound/seq/clients`.
For example::

  % cat /proc/asound/seq/clients
  Client info
    cur  clients : 3
  ....
  Client  14 : "Midi Through" [Kernel Legacy]
    Port   0 : "Midi Through Port-0" (RWe-)
  Client  20 : "ProtoZOA" [Kernel UMP MIDI1]
    UMP Endpoint: ProtoZOA
    UMP Block 0: ProtoZOA Main [Active]
      Groups: 1-1
    UMP Block 1: ProtoZOA Ext IN [Active]
      Groups: 2-2
    UMP Block 2: ProtoZOA Ext OUT [Active]
      Groups: 3-3
    Port   0 : "MIDI 2.0" (RWeX) [In/Out]
    Port   1 : "ProtoZOA Main" (RWeX) [In/Out]
    Port   2 : "ProtoZOA Ext IN" (-We-) [Out]
    Port   3 : "ProtoZOA Ext OUT" (R-e-) [In]

Here you can find two types of kernel clients, "Legacy" for client 14,
and "UMP MIDI1" for client 20, which is a USB MIDI 2.0 device.
A USB MIDI 2.0 client gives always the port 0 as "MIDI 2.0" and the
rest ports from 1 for each UMP Group (e.g. port 1 for Group 1).
In this example, the device has three active groups (Main, Ext IN and
Ext OUT), and those are exposed as sequencer ports from 1 to 3.
The "MIDI 2.0" port is for a UMP Endpoint, and its difference from
other UMP Group ports is that UMP Endpoint port sends the events from
the all ports on the device ("catch-all"), while each UMP Group port
sends only the events from the given UMP Group.

Note that, although each UMP sequencer client usually creates 16
ports, those ports that don't belong to any UMP Blocks (or belonging
to inactive UMP Blocks) are marked as inactive, and they don't appear
in the proc outputs.  In the example above, the sequencer ports from 4
to 16 are present but not shown there.

The proc file above shows the UMP Block information, too.  The same
entry (but with more detailed information) is found in the rawmidi
proc output.

When clients are connected between different MIDI versions, the events
are translated automatically depending on the client's version, not
only between the legacy and the UMP MIDI 1.0/2.0 types, but also
between UMP MIDI 1.0 and 2.0 types, too.  For example, running
`aseqdump` program on the ProtoZOA Main port in the legacy mode will
give you the output like::

  % aseqdump -p 20:1
  Waiting for data. Press Ctrl+C to end.
  Source  Event                  Ch  Data
   20:1   Note on                 0, note 60, velocity 100
   20:1   Note off                0, note 60, velocity 100
   20:1   Control change          0, controller 11, value 4

When you run `aseqdump` in MIDI 2.0 mode, it'll receive the high
precision data like::

  % aseqdump -u 2 -p 20:1
  Waiting for data. Press Ctrl+C to end.
  Source  Event                  Ch  Data
   20:1   Note on                 0, note 60, velocity 0xc924, attr type = 0, data = 0x0
   20:1   Note off                0, note 60, velocity 0xc924, attr type = 0, data = 0x0
   20:1   Control change          0, controller 11, value 0x2000000

while the data is automatically converted by ALSA sequencer core.


Rawmidi API Extensions
======================

* The additional UMP Endpoint information can be obtained via the new
  ioctl `SNDRV_UMP_IOCTL_ENDPOINT_INFO`.  It contains the associated
  card and device numbers, the bit flags, the protocols, the number of
  UMP Blocks, the name string of the endpoint, etc.

  The protocols are specified in two field, the protocol capabilities
  and the current protocol.  Both contain the bit flags specifying the
  MIDI protocol version (`SNDRV_UMP_EP_INFO_PROTO_MIDI1` or
  `SNDRV_UMP_EP_INFO_PROTO_MIDI2`) in the upper byte and the jitter
  reduction timestamp (`SNDRV_UMP_EP_INFO_PROTO_JRTS_TX` and
  `SNDRV_UMP_EP_INFO_PROTO_JRTS_RX`) in the lower byte.

  A UMP Endpoint may contain up to 32 UMP Blocks, and the number of
  the currently assigned blocks are shown in the Endpoint information.

* Each UMP Block information can be obtained via another new ioctl
  `SNDRV_UMP_IOCTL_BLOCK_INFO`.  The block ID number (0-based) has to
  be passed for the block to query.  The received data contains the
  associated the direction of the block, the first associated group ID
  (0-based) and the number of groups, the name string of the block,
  etc.

  The direction is either `SNDRV_UMP_DIR_INPUT`,
  `SNDRV_UMP_DIR_OUTPUT` or `SNDRV_UMP_DIR_BIDIRECTION`.


Control API Extensions
======================

* The new ioctl `SNDRV_CTL_IOCTL_UMP_NEXT_DEVICE` is introduced for
  querying the next UMP rawmidi device, while the existing ioctl
  `SNDRV_CTL_IOCTL_RAWMIDI_NEXT_DEVICE` queries only the legacy
  rawmidi devices.

  For setting the subdevice (substream number) to be opened, use the
  ioctl `SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE` like the normal
  rawmidi.

* Two new ioctls `SNDRV_CTL_IOCTL_UMP_ENDPOINT_INFO` and
  `SNDRV_CTL_IOCTL_UMP_BLOCK_INFO` provide the UMP Endpoint and UMP
  Block information of the specified UMP device via ALSA control API
  without opening the actual (UMP) rawmidi device.
  The `card` field is ignored upon inquiry, always tied with the card
  of the control interface.


Sequencer API Extensions
========================

* `midi_version` field is added to `snd_seq_client_info` to indicate
  the current MIDI version (either 0, 1 or 2) of each client.
  When `midi_version` is 1 or 2, the alignment of read from a UMP
  sequencer client is also changed from the former 28 bytes to 32
  bytes for the extended payload.  The alignment size for the write
  isn't changed, but each event size may differ depending on the new
  bit flag below.

* `SNDRV_SEQ_EVENT_UMP` flag bit is added for each sequencer event
  flags.  When this bit flag is set, the sequencer event is extended
  to have a larger payload of 16 bytes instead of the legacy 12
  bytes, and the event contains the UMP packet in the payload.

* The new sequencer port type bit (`SNDRV_SEQ_PORT_TYPE_MIDI_UMP`)
  indicates the port being UMP-capable.

* The sequencer ports have new capability bits to indicate the
  inactive ports (`SNDRV_SEQ_PORT_CAP_INACTIVE`) and the UMP Endpoint
  port (`SNDRV_SEQ_PORT_CAP_UMP_ENDPOINT`).

* The event conversion of ALSA sequencer clients can be suppressed the
  new filter bit `SNDRV_SEQ_FILTER_NO_CONVERT` set to the client info.
  For example, the kernel pass-through client (`snd-seq-dummy`) sets
  this flag internally.

* The port information gained the new field `direction` to indicate
  the direction of the port (either `SNDRV_SEQ_PORT_DIR_INPUT`,
  `SNDRV_SEQ_PORT_DIR_OUTPUT` or `SNDRV_SEQ_PORT_DIR_BIDIRECTION`).

* Another additional field for the port information is `ump_group`
  which specifies the associated UMP Group Number (1-based).
  When it's non-zero, the UMP group field in the UMP packet updated
  upon delivery to the specified group (corrected to be 0-based).
  Each sequencer port is supposed to set this field if it's a port to
  specific to a certain UMP group.

* Each client may set the additional event filter for UMP Groups in
  `group_filter` bitmap.  The filter consists of bitmap from 1-based
  Group numbers.  For example, when the bit 1 is set, messages from
  Group 1 (i.e. the very first group) are filtered and not delivered.
  The bit 0 is reserved for future use.

* Two new ioctls are added for UMP-capable clients:
  `SNDRV_SEQ_IOCTL_GET_CLIENT_UMP_INFO` and
  `SNDRV_SEQ_IOCTL_SET_CLIENT_UMP_INFO`.  They are used to get and set
  either `snd_ump_endpoint_info` or `snd_ump_block_info` data
  associated with the sequencer client.  The USB MIDI driver provides
  those information from the underlying UMP rawmidi, while a
  user-space client may provide its own data via `*_SET` ioctl.
  For an Endpoint data, pass 0 to the `type` field, while for a Block
  data, pass the block number + 1 to the `type` field.
  Setting the data for a kernel client shall result in an error.
