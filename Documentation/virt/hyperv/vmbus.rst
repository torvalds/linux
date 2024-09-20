.. SPDX-License-Identifier: GPL-2.0

VMBus
=====
VMBus is a software construct provided by Hyper-V to guest VMs.  It
consists of a control path and common facilities used by synthetic
devices that Hyper-V presents to guest VMs.   The control path is
used to offer synthetic devices to the guest VM and, in some cases,
to rescind those devices.   The common facilities include software
channels for communicating between the device driver in the guest VM
and the synthetic device implementation that is part of Hyper-V, and
signaling primitives to allow Hyper-V and the guest to interrupt
each other.

VMBus is modeled in Linux as a bus, with the expected /sys/bus/vmbus
entry in a running Linux guest.  The VMBus driver (drivers/hv/vmbus_drv.c)
establishes the VMBus control path with the Hyper-V host, then
registers itself as a Linux bus driver.  It implements the standard
bus functions for adding and removing devices to/from the bus.

Most synthetic devices offered by Hyper-V have a corresponding Linux
device driver.  These devices include:

* SCSI controller
* NIC
* Graphics frame buffer
* Keyboard
* Mouse
* PCI device pass-thru
* Heartbeat
* Time Sync
* Shutdown
* Memory balloon
* Key/Value Pair (KVP) exchange with Hyper-V
* Hyper-V online backup (a.k.a. VSS)

Guest VMs may have multiple instances of the synthetic SCSI
controller, synthetic NIC, and PCI pass-thru devices.  Other
synthetic devices are limited to a single instance per VM.  Not
listed above are a small number of synthetic devices offered by
Hyper-V that are used only by Windows guests and for which Linux
does not have a driver.

Hyper-V uses the terms "VSP" and "VSC" in describing synthetic
devices.  "VSP" refers to the Hyper-V code that implements a
particular synthetic device, while "VSC" refers to the driver for
the device in the guest VM.  For example, the Linux driver for the
synthetic NIC is referred to as "netvsc" and the Linux driver for
the synthetic SCSI controller is "storvsc".  These drivers contain
functions with names like "storvsc_connect_to_vsp".

VMBus channels
--------------
An instance of a synthetic device uses VMBus channels to communicate
between the VSP and the VSC.  Channels are bi-directional and used
for passing messages.   Most synthetic devices use a single channel,
but the synthetic SCSI controller and synthetic NIC may use multiple
channels to achieve higher performance and greater parallelism.

Each channel consists of two ring buffers.  These are classic ring
buffers from a university data structures textbook.  If the read
and writes pointers are equal, the ring buffer is considered to be
empty, so a full ring buffer always has at least one byte unused.
The "in" ring buffer is for messages from the Hyper-V host to the
guest, and the "out" ring buffer is for messages from the guest to
the Hyper-V host.  In Linux, the "in" and "out" designations are as
viewed by the guest side.  The ring buffers are memory that is
shared between the guest and the host, and they follow the standard
paradigm where the memory is allocated by the guest, with the list
of GPAs that make up the ring buffer communicated to the host.  Each
ring buffer consists of a header page (4 Kbytes) with the read and
write indices and some control flags, followed by the memory for the
actual ring.  The size of the ring is determined by the VSC in the
guest and is specific to each synthetic device.   The list of GPAs
making up the ring is communicated to the Hyper-V host over the
VMBus control path as a GPA Descriptor List (GPADL).  See function
vmbus_establish_gpadl().

Each ring buffer is mapped into contiguous Linux kernel virtual
space in three parts:  1) the 4 Kbyte header page, 2) the memory
that makes up the ring itself, and 3) a second mapping of the memory
that makes up the ring itself.  Because (2) and (3) are contiguous
in kernel virtual space, the code that copies data to and from the
ring buffer need not be concerned with ring buffer wrap-around.
Once a copy operation has completed, the read or write index may
need to be reset to point back into the first mapping, but the
actual data copy does not need to be broken into two parts.  This
approach also allows complex data structures to be easily accessed
directly in the ring without handling wrap-around.

On arm64 with page sizes > 4 Kbytes, the header page must still be
passed to Hyper-V as a 4 Kbyte area.  But the memory for the actual
ring must be aligned to PAGE_SIZE and have a size that is a multiple
of PAGE_SIZE so that the duplicate mapping trick can be done.  Hence
a portion of the header page is unused and not communicated to
Hyper-V.  This case is handled by vmbus_establish_gpadl().

Hyper-V enforces a limit on the aggregate amount of guest memory
that can be shared with the host via GPADLs.  This limit ensures
that a rogue guest can't force the consumption of excessive host
resources.  For Windows Server 2019 and later, this limit is
approximately 1280 Mbytes.  For versions prior to Windows Server
2019, the limit is approximately 384 Mbytes.

VMBus channel messages
----------------------
All messages sent in a VMBus channel have a standard header that includes
the message length, the offset of the message payload, some flags, and a
transactionID.  The portion of the message after the header is
unique to each VSP/VSC pair.

Messages follow one of two patterns:

* Unidirectional:  Either side sends a message and does not
  expect a response message
* Request/response:  One side (usually the guest) sends a message
  and expects a response

The transactionID (a.k.a. "requestID") is for matching requests &
responses.  Some synthetic devices allow multiple requests to be in-
flight simultaneously, so the guest specifies a transactionID when
sending a request.  Hyper-V sends back the same transactionID in the
matching response.

Messages passed between the VSP and VSC are control messages.  For
example, a message sent from the storvsc driver might be "execute
this SCSI command".   If a message also implies some data transfer
between the guest and the Hyper-V host, the actual data to be
transferred may be embedded with the control message, or it may be
specified as a separate data buffer that the Hyper-V host will
access as a DMA operation.  The former case is used when the size of
the data is small and the cost of copying the data to and from the
ring buffer is minimal.  For example, time sync messages from the
Hyper-V host to the guest contain the actual time value.  When the
data is larger, a separate data buffer is used.  In this case, the
control message contains a list of GPAs that describe the data
buffer.  For example, the storvsc driver uses this approach to
specify the data buffers to/from which disk I/O is done.

Three functions exist to send VMBus channel messages:

1. vmbus_sendpacket():  Control-only messages and messages with
   embedded data -- no GPAs
2. vmbus_sendpacket_pagebuffer(): Message with list of GPAs
   identifying data to transfer.  An offset and length is
   associated with each GPA so that multiple discontinuous areas
   of guest memory can be targeted.
3. vmbus_sendpacket_mpb_desc(): Message with list of GPAs
   identifying data to transfer.  A single offset and length is
   associated with a list of GPAs.  The GPAs must describe a
   single logical area of guest memory to be targeted.

Historically, Linux guests have trusted Hyper-V to send well-formed
and valid messages, and Linux drivers for synthetic devices did not
fully validate messages.  With the introduction of processor
technologies that fully encrypt guest memory and that allow the
guest to not trust the hypervisor (AMD SEV-SNP, Intel TDX), trusting
the Hyper-V host is no longer a valid assumption.  The drivers for
VMBus synthetic devices are being updated to fully validate any
values read from memory that is shared with Hyper-V, which includes
messages from VMBus devices.  To facilitate such validation,
messages read by the guest from the "in" ring buffer are copied to a
temporary buffer that is not shared with Hyper-V.  Validation is
performed in this temporary buffer without the risk of Hyper-V
maliciously modifying the message after it is validated but before
it is used.

Synthetic Interrupt Controller (synic)
--------------------------------------
Hyper-V provides each guest CPU with a synthetic interrupt controller
that is used by VMBus for host-guest communication. While each synic
defines 16 synthetic interrupts (SINT), Linux uses only one of the 16
(VMBUS_MESSAGE_SINT). All interrupts related to communication between
the Hyper-V host and a guest CPU use that SINT.

The SINT is mapped to a single per-CPU architectural interrupt (i.e,
an 8-bit x86/x64 interrupt vector, or an arm64 PPI INTID). Because
each CPU in the guest has a synic and may receive VMBus interrupts,
they are best modeled in Linux as per-CPU interrupts. This model works
well on arm64 where a single per-CPU Linux IRQ is allocated for
VMBUS_MESSAGE_SINT. This IRQ appears in /proc/interrupts as an IRQ labelled
"Hyper-V VMbus". Since x86/x64 lacks support for per-CPU IRQs, an x86
interrupt vector is statically allocated (HYPERVISOR_CALLBACK_VECTOR)
across all CPUs and explicitly coded to call vmbus_isr(). In this case,
there's no Linux IRQ, and the interrupts are visible in aggregate in
/proc/interrupts on the "HYP" line.

The synic provides the means to demultiplex the architectural interrupt into
one or more logical interrupts and route the logical interrupt to the proper
VMBus handler in Linux. This demultiplexing is done by vmbus_isr() and
related functions that access synic data structures.

The synic is not modeled in Linux as an irq chip or irq domain,
and the demultiplexed logical interrupts are not Linux IRQs. As such,
they don't appear in /proc/interrupts or /proc/irq. The CPU
affinity for one of these logical interrupts is controlled via an
entry under /sys/bus/vmbus as described below.

VMBus interrupts
----------------
VMBus provides a mechanism for the guest to interrupt the host when
the guest has queued new messages in a ring buffer.  The host
expects that the guest will send an interrupt only when an "out"
ring buffer transitions from empty to non-empty.  If the guest sends
interrupts at other times, the host deems such interrupts to be
unnecessary.  If a guest sends an excessive number of unnecessary
interrupts, the host may throttle that guest by suspending its
execution for a few seconds to prevent a denial-of-service attack.

Similarly, the host will interrupt the guest via the synic when
it sends a new message on the VMBus control path, or when a VMBus
channel "in" ring buffer transitions from empty to non-empty due to
the host inserting a new VMBus channel message. The control message stream
and each VMBus channel "in" ring buffer are separate logical interrupts
that are demultiplexed by vmbus_isr(). It demultiplexes by first checking
for channel interrupts by calling vmbus_chan_sched(), which looks at a synic
bitmap to determine which channels have pending interrupts on this CPU.
If multiple channels have pending interrupts for this CPU, they are
processed sequentially.  When all channel interrupts have been processed,
vmbus_isr() checks for and processes any messages received on the VMBus
control path.

The guest CPU that a VMBus channel will interrupt is selected by the
guest when the channel is created, and the host is informed of that
selection.  VMBus devices are broadly grouped into two categories:

1. "Slow" devices that need only one VMBus channel.  The devices
   (such as keyboard, mouse, heartbeat, and timesync) generate
   relatively few interrupts.  Their VMBus channels are all
   assigned to interrupt the VMBUS_CONNECT_CPU, which is always
   CPU 0.

2. "High speed" devices that may use multiple VMBus channels for
   higher parallelism and performance.  These devices include the
   synthetic SCSI controller and synthetic NIC.  Their VMBus
   channels interrupts are assigned to CPUs that are spread out
   among the available CPUs in the VM so that interrupts on
   multiple channels can be processed in parallel.

The assignment of VMBus channel interrupts to CPUs is done in the
function init_vp_index().  This assignment is done outside of the
normal Linux interrupt affinity mechanism, so the interrupts are
neither "unmanaged" nor "managed" interrupts.

The CPU that a VMBus channel will interrupt can be seen in
/sys/bus/vmbus/devices/<deviceGUID>/ channels/<channelRelID>/cpu.
When running on later versions of Hyper-V, the CPU can be changed
by writing a new value to this sysfs entry. Because VMBus channel
interrupts are not Linux IRQs, there are no entries in /proc/interrupts
or /proc/irq corresponding to individual VMBus channel interrupts.

An online CPU in a Linux guest may not be taken offline if it has
VMBus channel interrupts assigned to it.  Any such channel
interrupts must first be manually reassigned to another CPU as
described above.  When no channel interrupts are assigned to the
CPU, it can be taken offline.

The VMBus channel interrupt handling code is designed to work
correctly even if an interrupt is received on a CPU other than the
CPU assigned to the channel.  Specifically, the code does not use
CPU-based exclusion for correctness.  In normal operation, Hyper-V
will interrupt the assigned CPU.  But when the CPU assigned to a
channel is being changed via sysfs, the guest doesn't know exactly
when Hyper-V will make the transition.  The code must work correctly
even if there is a time lag before Hyper-V starts interrupting the
new CPU.  See comments in target_cpu_store().

VMBus device creation/deletion
------------------------------
Hyper-V and the Linux guest have a separate message-passing path
that is used for synthetic device creation and deletion. This
path does not use a VMBus channel.  See vmbus_post_msg() and
vmbus_on_msg_dpc().

The first step is for the guest to connect to the generic
Hyper-V VMBus mechanism.  As part of establishing this connection,
the guest and Hyper-V agree on a VMBus protocol version they will
use.  This negotiation allows newer Linux kernels to run on older
Hyper-V versions, and vice versa.

The guest then tells Hyper-V to "send offers".  Hyper-V sends an
offer message to the guest for each synthetic device that the VM
is configured to have. Each VMBus device type has a fixed GUID
known as the "class ID", and each VMBus device instance is also
identified by a GUID. The offer message from Hyper-V contains
both GUIDs to uniquely (within the VM) identify the device.
There is one offer message for each device instance, so a VM with
two synthetic NICs will get two offers messages with the NIC
class ID. The ordering of offer messages can vary from boot-to-boot
and must not be assumed to be consistent in Linux code. Offer
messages may also arrive long after Linux has initially booted
because Hyper-V supports adding devices, such as synthetic NICs,
to running VMs. A new offer message is processed by
vmbus_process_offer(), which indirectly invokes vmbus_add_channel_work().

Upon receipt of an offer message, the guest identifies the device
type based on the class ID, and invokes the correct driver to set up
the device.  Driver/device matching is performed using the standard
Linux mechanism.

The device driver probe function opens the primary VMBus channel to
the corresponding VSP. It allocates guest memory for the channel
ring buffers and shares the ring buffer with the Hyper-V host by
giving the host a list of GPAs for the ring buffer memory.  See
vmbus_establish_gpadl().

Once the ring buffer is set up, the device driver and VSP exchange
setup messages via the primary channel.  These messages may include
negotiating the device protocol version to be used between the Linux
VSC and the VSP on the Hyper-V host.  The setup messages may also
include creating additional VMBus channels, which are somewhat
mis-named as "sub-channels" since they are functionally
equivalent to the primary channel once they are created.

Finally, the device driver may create entries in /dev as with
any device driver.

The Hyper-V host can send a "rescind" message to the guest to
remove a device that was previously offered. Linux drivers must
handle such a rescind message at any time. Rescinding a device
invokes the device driver "remove" function to cleanly shut
down the device and remove it. Once a synthetic device is
rescinded, neither Hyper-V nor Linux retains any state about
its previous existence. Such a device might be re-added later,
in which case it is treated as an entirely new device. See
vmbus_onoffer_rescind().
