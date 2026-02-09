.. SPDX-License-Identifier: GPL-2.0

Confidential Computing VMs
==========================
Hyper-V can create and run Linux guests that are Confidential Computing
(CoCo) VMs. Such VMs cooperate with the physical processor to better protect
the confidentiality and integrity of data in the VM's memory, even in the
face of a hypervisor/VMM that has been compromised and may behave maliciously.
CoCo VMs on Hyper-V share the generic CoCo VM threat model and security
objectives described in Documentation/security/snp-tdx-threat-model.rst. Note
that Hyper-V specific code in Linux refers to CoCo VMs as "isolated VMs" or
"isolation VMs".

A Linux CoCo VM on Hyper-V requires the cooperation and interaction of the
following:

* Physical hardware with a processor that supports CoCo VMs

* The hardware runs a version of Windows/Hyper-V with support for CoCo VMs

* The VM runs a version of Linux that supports being a CoCo VM

The physical hardware requirements are as follows:

* AMD processor with SEV-SNP. Hyper-V does not run guest VMs with AMD SME,
  SEV, or SEV-ES encryption, and such encryption is not sufficient for a CoCo
  VM on Hyper-V.

* Intel processor with TDX

To create a CoCo VM, the "Isolated VM" attribute must be specified to Hyper-V
when the VM is created. A VM cannot be changed from a CoCo VM to a normal VM,
or vice versa, after it is created.

Operational Modes
-----------------
Hyper-V CoCo VMs can run in two modes. The mode is selected when the VM is
created and cannot be changed during the life of the VM.

* Fully-enlightened mode. In this mode, the guest operating system is
  enlightened to understand and manage all aspects of running as a CoCo VM.

* Paravisor mode. In this mode, a paravisor layer between the guest and the
  host provides some operations needed to run as a CoCo VM. The guest operating
  system can have fewer CoCo enlightenments than is required in the
  fully-enlightened case.

Conceptually, fully-enlightened mode and paravisor mode may be treated as
points on a spectrum spanning the degree of guest enlightenment needed to run
as a CoCo VM. Fully-enlightened mode is one end of the spectrum. A full
implementation of paravisor mode is the other end of the spectrum, where all
aspects of running as a CoCo VM are handled by the paravisor, and a normal
guest OS with no knowledge of memory encryption or other aspects of CoCo VMs
can run successfully. However, the Hyper-V implementation of paravisor mode
does not go this far, and is somewhere in the middle of the spectrum. Some
aspects of CoCo VMs are handled by the Hyper-V paravisor while the guest OS
must be enlightened for other aspects. Unfortunately, there is no
standardized enumeration of feature/functions that might be provided in the
paravisor, and there is no standardized mechanism for a guest OS to query the
paravisor for the feature/functions it provides. The understanding of what
the paravisor provides is hard-coded in the guest OS.

Paravisor mode has similarities to the `Coconut project`_, which aims to provide
a limited paravisor to provide services to the guest such as a virtual TPM.
However, the Hyper-V paravisor generally handles more aspects of CoCo VMs
than is currently envisioned for Coconut, and so is further toward the "no
guest enlightenments required" end of the spectrum.

.. _Coconut project: https://github.com/coconut-svsm/svsm

In the CoCo VM threat model, the paravisor is in the guest security domain
and must be trusted by the guest OS. By implication, the hypervisor/VMM must
protect itself against a potentially malicious paravisor just like it
protects against a potentially malicious guest.

The hardware architectural approach to fully-enlightened vs. paravisor mode
varies depending on the underlying processor.

* With AMD SEV-SNP processors, in fully-enlightened mode the guest OS runs in
  VMPL 0 and has full control of the guest context. In paravisor mode, the
  guest OS runs in VMPL 2 and the paravisor runs in VMPL 0. The paravisor
  running in VMPL 0 has privileges that the guest OS in VMPL 2 does not have.
  Certain operations require the guest to invoke the paravisor. Furthermore, in
  paravisor mode the guest OS operates in "virtual Top Of Memory" (vTOM) mode
  as defined by the SEV-SNP architecture. This mode simplifies guest management
  of memory encryption when a paravisor is used.

* With Intel TDX processor, in fully-enlightened mode the guest OS runs in an
  L1 VM. In paravisor mode, TD partitioning is used. The paravisor runs in the
  L1 VM, and the guest OS runs in a nested L2 VM.

Hyper-V exposes a synthetic MSR to guests that describes the CoCo mode. This
MSR indicates if the underlying processor uses AMD SEV-SNP or Intel TDX, and
whether a paravisor is being used. It is straightforward to build a single
kernel image that can boot and run properly on either architecture, and in
either mode.

Paravisor Effects
-----------------
Running in paravisor mode affects the following areas of generic Linux kernel
CoCo VM functionality:

* Initial guest memory setup. When a new VM is created in paravisor mode, the
  paravisor runs first and sets up the guest physical memory as encrypted. The
  guest Linux does normal memory initialization, except for explicitly marking
  appropriate ranges as decrypted (shared). In paravisor mode, Linux does not
  perform the early boot memory setup steps that are particularly tricky with
  AMD SEV-SNP in fully-enlightened mode.

* #VC/#VE exception handling. In paravisor mode, Hyper-V configures the guest
  CoCo VM to route #VC and #VE exceptions to VMPL 0 and the L1 VM,
  respectively, and not the guest Linux. Consequently, these exception handlers
  do not run in the guest Linux and are not a required enlightenment for a
  Linux guest in paravisor mode.

* CPUID flags. Both AMD SEV-SNP and Intel TDX provide a CPUID flag in the
  guest indicating that the VM is operating with the respective hardware
  support. While these CPUID flags are visible in fully-enlightened CoCo VMs,
  the paravisor filters out these flags and the guest Linux does not see them.
  Throughout the Linux kernel, explicitly testing these flags has mostly been
  eliminated in favor of the cc_platform_has() function, with the goal of
  abstracting the differences between SEV-SNP and TDX. But the
  cc_platform_has() abstraction also allows the Hyper-V paravisor configuration
  to selectively enable aspects of CoCo VM functionality even when the CPUID
  flags are not set. The exception is early boot memory setup on SEV-SNP, which
  tests the CPUID SEV-SNP flag. But not having the flag in Hyper-V paravisor
  mode VM achieves the desired effect or not running SEV-SNP specific early
  boot memory setup.

* Device emulation. In paravisor mode, the Hyper-V paravisor provides
  emulation of devices such as the IO-APIC and TPM. Because the emulation
  happens in the paravisor in the guest context (instead of the hypervisor/VMM
  context), MMIO accesses to these devices must be encrypted references instead
  of the decrypted references that would be used in a fully-enlightened CoCo
  VM. The __ioremap_caller() function has been enhanced to make a callback to
  check whether a particular address range should be treated as encrypted
  (private). See the "is_private_mmio" callback.

* Encrypt/decrypt memory transitions. In a CoCo VM, transitioning guest
  memory between encrypted and decrypted requires coordinating with the
  hypervisor/VMM. This is done via callbacks invoked from
  __set_memory_enc_pgtable(). In fully-enlightened mode, the normal SEV-SNP and
  TDX implementations of these callbacks are used. In paravisor mode, a Hyper-V
  specific set of callbacks is used. These callbacks invoke the paravisor so
  that the paravisor can coordinate the transitions and inform the hypervisor
  as necessary. See hv_vtom_init() where these callback are set up.

* Interrupt injection. In fully enlightened mode, a malicious hypervisor
  could inject interrupts into the guest OS at times that violate x86/x64
  architectural rules. For full protection, the guest OS should include
  enlightenments that use the interrupt injection management features provided
  by CoCo-capable processors. In paravisor mode, the paravisor mediates
  interrupt injection into the guest OS, and ensures that the guest OS only
  sees interrupts that are "legal". The paravisor uses the interrupt injection
  management features provided by the CoCo-capable physical processor, thereby
  masking these complexities from the guest OS.

Hyper-V Hypercalls
------------------
When in fully-enlightened mode, hypercalls made by the Linux guest are routed
directly to the hypervisor, just as in a non-CoCo VM. But in paravisor mode,
normal hypercalls trap to the paravisor first, which may in turn invoke the
hypervisor. But the paravisor is idiosyncratic in this regard, and a few
hypercalls made by the Linux guest must always be routed directly to the
hypervisor. These hypercall sites test for a paravisor being present, and use
a special invocation sequence. See hv_post_message(), for example.

Guest communication with Hyper-V
--------------------------------
Separate from the generic Linux kernel handling of memory encryption in Linux
CoCo VMs, Hyper-V has VMBus and VMBus devices that communicate using memory
shared between the Linux guest and the host. This shared memory must be
marked decrypted to enable communication. Furthermore, since the threat model
includes a compromised and potentially malicious host, the guest must guard
against leaking any unintended data to the host through this shared memory.

These Hyper-V and VMBus memory pages are marked as decrypted:

* VMBus monitor pages

* Synthetic interrupt controller (SynIC) related pages (unless supplied by
  the paravisor)

* Per-cpu hypercall input and output pages (unless running with a paravisor)

* VMBus ring buffers. The direct mapping is marked decrypted in
  __vmbus_establish_gpadl(). The secondary mapping created in
  hv_ringbuffer_init() must also include the "decrypted" attribute.

When the guest writes data to memory that is shared with the host, it must
ensure that only the intended data is written. Padding or unused fields must
be initialized to zeros before copying into the shared memory so that random
kernel data is not inadvertently given to the host.

Similarly, when the guest reads memory that is shared with the host, it must
validate the data before acting on it so that a malicious host cannot induce
the guest to expose unintended data. Doing such validation can be tricky
because the host can modify the shared memory areas even while or after
validation is performed. For messages passed from the host to the guest in a
VMBus ring buffer, the length of the message is validated, and the message is
copied into a temporary (encrypted) buffer for further validation and
processing. The copying adds a small amount of overhead, but is the only way
to protect against a malicious host. See hv_pkt_iter_first().

Many drivers for VMBus devices have been "hardened" by adding code to fully
validate messages received over VMBus, instead of assuming that Hyper-V is
acting cooperatively. Such drivers are marked as "allowed_in_isolated" in the
vmbus_devs[] table. Other drivers for VMBus devices that are not needed in a
CoCo VM have not been hardened, and they are not allowed to load in a CoCo
VM. See vmbus_is_valid_offer() where such devices are excluded.

Two VMBus devices depend on the Hyper-V host to do DMA data transfers:
storvsc for disk I/O and netvsc for network I/O. storvsc uses the normal
Linux kernel DMA APIs, and so bounce buffering through decrypted swiotlb
memory is done implicitly. netvsc has two modes for data transfers. The first
mode goes through send and receive buffer space that is explicitly allocated
by the netvsc driver, and is used for most smaller packets. These send and
receive buffers are marked decrypted by __vmbus_establish_gpadl(). Because
the netvsc driver explicitly copies packets to/from these buffers, the
equivalent of bounce buffering between encrypted and decrypted memory is
already part of the data path. The second mode uses the normal Linux kernel
DMA APIs, and is bounce buffered through swiotlb memory implicitly like in
storvsc.

Finally, the VMBus virtual PCI driver needs special handling in a CoCo VM.
Linux PCI device drivers access PCI config space using standard APIs provided
by the Linux PCI subsystem. On Hyper-V, these functions directly access MMIO
space, and the access traps to Hyper-V for emulation. But in CoCo VMs, memory
encryption prevents Hyper-V from reading the guest instruction stream to
emulate the access. So in a CoCo VM, these functions must make a hypercall
with arguments explicitly describing the access. See
_hv_pcifront_read_config() and _hv_pcifront_write_config() and the
"use_calls" flag indicating to use hypercalls.

Confidential VMBus
------------------
The confidential VMBus enables the confidential guest not to interact with
the untrusted host partition and the untrusted hypervisor. Instead, the guest
relies on the trusted paravisor to communicate with the devices processing
sensitive data. The hardware (SNP or TDX) encrypts the guest memory and the
register state while measuring the paravisor image using the platform security
processor to ensure trusted and confidential computing.

Confidential VMBus provides a secure communication channel between the guest
and the paravisor, ensuring that sensitive data is protected from hypervisor-
level access through memory encryption and register state isolation.

Confidential VMBus is an extension of Confidential Computing (CoCo) VMs
(a.k.a. "Isolated" VMs in Hyper-V terminology). Without Confidential VMBus,
guest VMBus device drivers (the "VSC"s in VMBus terminology) communicate
with VMBus servers (the VSPs) running on the Hyper-V host. The
communication must be through memory that has been decrypted so the
host can access it. With Confidential VMBus, one or more of the VSPs reside
in the trusted paravisor layer in the guest VM. Since the paravisor layer also
operates in encrypted memory, the memory used for communication with
such VSPs does not need to be decrypted and thereby exposed to the
Hyper-V host. The paravisor is responsible for communicating securely
with the Hyper-V host as necessary.

The data is transferred directly between the VM and a vPCI device (a.k.a.
a PCI pass-thru device, see :doc:`vpci`) that is directly assigned to VTL2
and that supports encrypted memory. In such a case, neither the host partition
nor the hypervisor has any access to the data. The guest needs to establish
a VMBus connection only with the paravisor for the channels that process
sensitive data, and the paravisor abstracts the details of communicating
with the specific devices away providing the guest with the well-established
VSP (Virtual Service Provider) interface that has had support in the Hyper-V
drivers for a decade.

In the case the device does not support encrypted memory, the paravisor
provides bounce-buffering, and although the data is not encrypted, the backing
pages aren't mapped into the host partition through SLAT. While not impossible,
it becomes much more difficult for the host partition to exfiltrate the data
than it would be with a conventional VMBus connection where the host partition
has direct access to the memory used for communication.

Here is the data flow for a conventional VMBus connection (`C` stands for the
client or VSC, `S` for the server or VSP, the `DEVICE` is a physical one, might
be with multiple virtual functions)::

  +---- GUEST ----+       +----- DEVICE ----+        +----- HOST -----+
  |               |       |                 |        |                |
  |               |       |                 |        |                |
  |               |       |                 ==========                |
  |               |       |                 |        |                |
  |               |       |                 |        |                |
  |               |       |                 |        |                |
  +----- C -------+       +-----------------+        +------- S ------+
         ||                                                   ||
         ||                                                   ||
  +------||------------------ VMBus --------------------------||------+
  |                     Interrupts, MMIO                              |
  +-------------------------------------------------------------------+

and the Confidential VMBus connection::

  +---- GUEST --------------- VTL0 ------+               +-- DEVICE --+
  |                                      |               |            |
  | +- PARAVISOR --------- VTL2 -----+   |               |            |
  | |     +-- VMBus Relay ------+    ====+================            |
  | |     |   Interrupts, MMIO  |    |   |               |            |
  | |     +-------- S ----------+    |   |               +------------+
  | |               ||               |   |
  | +---------+     ||               |   |
  | |  Linux  |     ||    OpenHCL    |   |
  | |  kernel |     ||               |   |
  | +---- C --+-----||---------------+   |
  |       ||        ||                   |
  +-------++------- C -------------------+               +------------+
          ||                                             |    HOST    |
          ||                                             +---- S -----+
  +-------||----------------- VMBus ---------------------------||-----+
  |                     Interrupts, MMIO                              |
  +-------------------------------------------------------------------+

An implementation of the VMBus relay that offers the Confidential VMBus
channels is available in the OpenVMM project as a part of the OpenHCL
paravisor. Please refer to

  * https://openvmm.dev/, and
  * https://github.com/microsoft/openvmm

for more information about the OpenHCL paravisor.

A guest that is running with a paravisor must determine at runtime if
Confidential VMBus is supported by the current paravisor. The x86_64-specific
approach relies on the CPUID Virtualization Stack leaf; the ARM64 implementation
is expected to support the Confidential VMBus unconditionally when running
ARM CCA guests.

Confidential VMBus is a characteristic of the VMBus connection as a whole,
and of each VMBus channel that is created. When a Confidential VMBus
connection is established, the paravisor provides the guest the message-passing
path that is used for VMBus device creation and deletion, and it provides a
per-CPU synthetic interrupt controller (SynIC) just like the SynIC that is
offered by the Hyper-V host. Each VMBus device that is offered to the guest
indicates the degree to which it participates in Confidential VMBus. The offer
indicates if the device uses encrypted ring buffers, and if the device uses
encrypted memory for DMA that is done outside the ring buffer. These settings
may be different for different devices using the same Confidential VMBus
connection.

Although these settings are separate, in practice it'll always be encrypted
ring buffer only, or both encrypted ring buffer and external data. If a channel
is offered by the paravisor with confidential VMBus, the ring buffer can always
be encrypted since it's strictly for communication between the VTL2 paravisor
and the VTL0 guest. However, other memory regions are often used for e.g. DMA,
so they need to be accessible by the underlying hardware, and must be
unencrypted (unless the device supports encrypted memory). Currently, there are
not any VSPs in OpenHCL that support encrypted external memory, but future
versions are expected to enable this capability.

Because some devices on a Confidential VMBus may require decrypted ring buffers
and DMA transfers, the guest must interact with two SynICs -- the one provided
by the paravisor and the one provided by the Hyper-V host when Confidential
VMBus is not offered. Interrupts are always signaled by the paravisor SynIC,
but the guest must check for messages and for channel interrupts on both SynICs.

In the case of a confidential VMBus, regular SynIC access by the guest is
intercepted by the paravisor (this includes various MSRs such as the SIMP and
SIEFP, as well as hypercalls like HvPostMessage and HvSignalEvent). If the
guest actually wants to communicate with the hypervisor, it has to use special
mechanisms (GHCB page on SNP, or tdcall on TDX). Messages can be of either
kind: with confidential VMBus, messages use the paravisor SynIC, and if the
guest chose to communicate directly to the hypervisor, they use the hypervisor
SynIC. For interrupt signaling, some channels may be running on the host
(non-confidential, using the VMBus relay) and use the hypervisor SynIC, and
some on the paravisor and use its SynIC. The RelIDs are coordinated by the
OpenHCL VMBus server and are guaranteed to be unique regardless of whether
the channel originated on the host or the paravisor.

load_unaligned_zeropad()
------------------------
When transitioning memory between encrypted and decrypted, the caller of
set_memory_encrypted() or set_memory_decrypted() is responsible for ensuring
the memory isn't in use and isn't referenced while the transition is in
progress. The transition has multiple steps, and includes interaction with
the Hyper-V host. The memory is in an inconsistent state until all steps are
complete. A reference while the state is inconsistent could result in an
exception that can't be cleanly fixed up.

However, the kernel load_unaligned_zeropad() mechanism may make stray
references that can't be prevented by the caller of set_memory_encrypted() or
set_memory_decrypted(), so there's specific code in the #VC or #VE exception
handler to fixup this case. But a CoCo VM running on Hyper-V may be
configured to run with a paravisor, with the #VC or #VE exception routed to
the paravisor. There's no architectural way to forward the exceptions back to
the guest kernel, and in such a case, the load_unaligned_zeropad() fixup code
in the #VC/#VE handlers doesn't run.

To avoid this problem, the Hyper-V specific functions for notifying the
hypervisor of the transition mark pages as "not present" while a transition
is in progress. If load_unaligned_zeropad() causes a stray reference, a
normal page fault is generated instead of #VC or #VE, and the page-fault-
based handlers for load_unaligned_zeropad() fixup the reference. When the
encrypted/decrypted transition is complete, the pages are marked as "present"
again. See hv_vtom_clear_present() and hv_vtom_set_host_visibility().
