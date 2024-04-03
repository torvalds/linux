======================================================
Confidential Computing in Linux for x86 virtualization
======================================================

.. contents:: :local:

By: Elena Reshetova <elena.reshetova@intel.com> and Carlos Bilbao <carlos.bilbao@amd.com>

Motivation
==========

Kernel developers working on confidential computing for virtualized
environments in x86 operate under a set of assumptions regarding the Linux
kernel threat model that differ from the traditional view. Historically,
the Linux threat model acknowledges attackers residing in userspace, as
well as a limited set of external attackers that are able to interact with
the kernel through various networking or limited HW-specific exposed
interfaces (USB, thunderbolt). The goal of this document is to explain
additional attack vectors that arise in the confidential computing space
and discuss the proposed protection mechanisms for the Linux kernel.

Overview and terminology
========================

Confidential Computing (CoCo) is a broad term covering a wide range of
security technologies that aim to protect the confidentiality and integrity
of data in use (vs. data at rest or data in transit). At its core, CoCo
solutions provide a Trusted Execution Environment (TEE), where secure data
processing can be performed and, as a result, they are typically further
classified into different subtypes depending on the SW that is intended
to be run in TEE. This document focuses on a subclass of CoCo technologies
that are targeting virtualized environments and allow running Virtual
Machines (VM) inside TEE. From now on in this document will be referring
to this subclass of CoCo as 'Confidential Computing (CoCo) for the
virtualized environments (VE)'.

CoCo, in the virtualization context, refers to a set of HW and/or SW
technologies that allow for stronger security guarantees for the SW running
inside a CoCo VM. Namely, confidential computing allows its users to
confirm the trustworthiness of all SW pieces to include in its reduced
Trusted Computing Base (TCB) given its ability to attest the state of these
trusted components.

While the concrete implementation details differ between technologies, all
available mechanisms aim to provide increased confidentiality and
integrity for the VM's guest memory and execution state (vCPU registers),
more tightly controlled guest interrupt injection, as well as some
additional mechanisms to control guest-host page mapping. More details on
the x86-specific solutions can be found in
:doc:`Intel Trust Domain Extensions (TDX) </arch/x86/tdx>` and
`AMD Memory Encryption <https://www.amd.com/system/files/techdocs/sev-snp-strengthening-vm-isolation-with-integrity-protection-and-more.pdf>`_.

The basic CoCo guest layout includes the host, guest, the interfaces that
communicate guest and host, a platform capable of supporting CoCo VMs, and
a trusted intermediary between the guest VM and the underlying platform
that acts as a security manager. The host-side virtual machine monitor
(VMM) typically consists of a subset of traditional VMM features and
is still in charge of the guest lifecycle, i.e. create or destroy a CoCo
VM, manage its access to system resources, etc. However, since it
typically stays out of CoCo VM TCB, its access is limited to preserve the
security objectives.

In the following diagram, the "<--->" lines represent bi-directional
communication channels or interfaces between the CoCo security manager and
the rest of the components (data flow for guest, host, hardware) ::

    +-------------------+      +-----------------------+
    | CoCo guest VM     |<---->|                       |
    +-------------------+      |                       |
      | Interfaces |           | CoCo security manager |
    +-------------------+      |                       |
    | Host VMM          |<---->|                       |
    +-------------------+      |                       |
                               |                       |
    +--------------------+     |                       |
    | CoCo platform      |<--->|                       |
    +--------------------+     +-----------------------+

The specific details of the CoCo security manager vastly diverge between
technologies. For example, in some cases, it will be implemented in HW
while in others it may be pure SW.

Existing Linux kernel threat model
==================================

The overall components of the current Linux kernel threat model are::

     +-----------------------+      +-------------------+
     |                       |<---->| Userspace         |
     |                       |      +-------------------+
     |   External attack     |         | Interfaces |
     |       vectors         |      +-------------------+
     |                       |<---->| Linux Kernel      |
     |                       |      +-------------------+
     +-----------------------+      +-------------------+
                                    | Bootloader/BIOS   |
                                    +-------------------+
                                    +-------------------+
                                    | HW platform       |
                                    +-------------------+

There is also communication between the bootloader and the kernel during
the boot process, but this diagram does not represent it explicitly. The
"Interfaces" box represents the various interfaces that allow
communication between kernel and userspace. This includes system calls,
kernel APIs, device drivers, etc.

The existing Linux kernel threat model typically assumes execution on a
trusted HW platform with all of the firmware and bootloaders included on
its TCB. The primary attacker resides in the userspace, and all of the data
coming from there is generally considered untrusted, unless userspace is
privileged enough to perform trusted actions. In addition, external
attackers are typically considered, including those with access to enabled
external networks (e.g. Ethernet, Wireless, Bluetooth), exposed hardware
interfaces (e.g. USB, Thunderbolt), and the ability to modify the contents
of disks offline.

Regarding external attack vectors, it is interesting to note that in most
cases external attackers will try to exploit vulnerabilities in userspace
first, but that it is possible for an attacker to directly target the
kernel; particularly if the host has physical access. Examples of direct
kernel attacks include the vulnerabilities CVE-2019-19524, CVE-2022-0435
and CVE-2020-24490.

Confidential Computing threat model and its security objectives
===============================================================

Confidential Computing adds a new type of attacker to the above list: a
potentially misbehaving host (which can also include some part of a
traditional VMM or all of it), which is typically placed outside of the
CoCo VM TCB due to its large SW attack surface. It is important to note
that this doesn’t imply that the host or VMM are intentionally
malicious, but that there exists a security value in having a small CoCo
VM TCB. This new type of adversary may be viewed as a more powerful type
of external attacker, as it resides locally on the same physical machine
(in contrast to a remote network attacker) and has control over the guest
kernel communication with most of the HW::

                                 +------------------------+
                                 |    CoCo guest VM       |
   +-----------------------+     |  +-------------------+ |
   |                       |<--->|  | Userspace         | |
   |                       |     |  +-------------------+ |
   |   External attack     |     |     | Interfaces |     |
   |       vectors         |     |  +-------------------+ |
   |                       |<--->|  | Linux Kernel      | |
   |                       |     |  +-------------------+ |
   +-----------------------+     |  +-------------------+ |
                                 |  | Bootloader/BIOS   | |
   +-----------------------+     |  +-------------------+ |
   |                       |<--->+------------------------+
   |                       |          | Interfaces |
   |                       |     +------------------------+
   |     CoCo security     |<--->| Host/Host-side VMM |
   |      manager          |     +------------------------+
   |                       |     +------------------------+
   |                       |<--->|   CoCo platform        |
   +-----------------------+     +------------------------+

While traditionally the host has unlimited access to guest data and can
leverage this access to attack the guest, the CoCo systems mitigate such
attacks by adding security features like guest data confidentiality and
integrity protection. This threat model assumes that those features are
available and intact.

The **Linux kernel CoCo VM security objectives** can be summarized as follows:

1. Preserve the confidentiality and integrity of CoCo guest's private
memory and registers.

2. Prevent privileged escalation from a host into a CoCo guest Linux kernel.
While it is true that the host (and host-side VMM) requires some level of
privilege to create, destroy, or pause the guest, part of the goal of
preventing privileged escalation is to ensure that these operations do not
provide a pathway for attackers to gain access to the guest's kernel.

The above security objectives result in two primary **Linux kernel CoCo
VM assets**:

1. Guest kernel execution context.
2. Guest kernel private memory.

The host retains full control over the CoCo guest resources, and can deny
access to them at any time. Examples of resources include CPU time, memory
that the guest can consume, network bandwidth, etc. Because of this, the
host Denial of Service (DoS) attacks against CoCo guests are beyond the
scope of this threat model.

The **Linux CoCo VM attack surface** is any interface exposed from a CoCo
guest Linux kernel towards an untrusted host that is not covered by the
CoCo technology SW/HW protection. This includes any possible
side-channels, as well as transient execution side channels. Examples of
explicit (not side-channel) interfaces include accesses to port I/O, MMIO
and DMA interfaces, access to PCI configuration space, VMM-specific
hypercalls (towards Host-side VMM), access to shared memory pages,
interrupts allowed to be injected into the guest kernel by the host, as
well as CoCo technology-specific hypercalls, if present. Additionally, the
host in a CoCo system typically controls the process of creating a CoCo
guest: it has a method to load into a guest the firmware and bootloader
images, the kernel image together with the kernel command line. All of this
data should also be considered untrusted until its integrity and
authenticity is established via attestation.

The table below shows a threat matrix for the CoCo guest Linux kernel but
does not discuss potential mitigation strategies. The matrix refers to
CoCo-specific versions of the guest, host and platform.

.. list-table:: CoCo Linux guest kernel threat matrix
   :widths: auto
   :align: center
   :header-rows: 1

   * - Threat name
     - Threat description

   * - Guest malicious configuration
     - A misbehaving host modifies one of the following guest's
       configuration:

       1. Guest firmware or bootloader

       2. Guest kernel or module binaries

       3. Guest command line parameters

       This allows the host to break the integrity of the code running
       inside a CoCo guest, and violates the CoCo security objectives.

   * - CoCo guest data attacks
     - A misbehaving host retains full control of the CoCo guest's data
       in-transit between the guest and the host-managed physical or
       virtual devices. This allows any attack against confidentiality,
       integrity or freshness of such data.

   * - Malformed runtime input
     - A misbehaving host injects malformed input via any communication
       interface used by the guest's kernel code. If the code is not
       prepared to handle this input correctly, this can result in a host
       --> guest kernel privilege escalation. This includes traditional
       side-channel and/or transient execution attack vectors.

   * - Malicious runtime input
     - A misbehaving host injects a specific input value via any
       communication interface used by the guest's kernel code. The
       difference with the previous attack vector (malformed runtime input)
       is that this input is not malformed, but its value is crafted to
       impact the guest's kernel security. Examples of such inputs include
       providing a malicious time to the guest or the entropy to the guest
       random number generator. Additionally, the timing of such events can
       be an attack vector on its own, if it results in a particular guest
       kernel action (i.e. processing of a host-injected interrupt).
       resistant to supplied host input.

