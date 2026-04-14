.. SPDX-License-Identifier: GPL-2.0

===========================
Affinity managed interrupts
===========================

The IRQ core provides support for managing interrupts according to a specified
CPU affinity. Under normal operation, an interrupt is associated with a
particular CPU. If that CPU is taken offline, the interrupt is migrated to
another online CPU.

Devices with large numbers of interrupt vectors can stress the available vector
space. For example, an NVMe device with 128 I/O queues typically requests one
interrupt per queue on systems with at least 128 CPUs. Two such devices
therefore request 256 interrupts. On x86, the interrupt vector space is
notoriously low, providing only 256 vectors per CPU, and the kernel reserves a
subset of these, further reducing the number available for device interrupts.
In practice this is not an issue because the interrupts are distributed across
many CPUs, so each CPU only receives a small number of vectors.

During system suspend, however, all secondary CPUs are taken offline and all
interrupts are migrated to the single CPU that remains online. This can exhaust
the available interrupt vectors on that CPU and cause the suspend operation to
fail.

Affinity‑managed interrupts address this limitation. Each interrupt is assigned
a CPU affinity mask that specifies the set of CPUs on which the interrupt may
be targeted. When a CPU in the mask goes offline, the interrupt is moved to the
next CPU in the mask. If the last CPU in the mask goes offline, the interrupt
is shut down. Drivers using affinity‑managed interrupts must ensure that the
associated queue is quiesced before the interrupt is disabled so that no
further interrupts are generated. When a CPU in the affinity mask comes back
online, the interrupt is re‑enabled.

Implementation
--------------

Devices must provide per‑instance interrupts, such as per‑I/O‑queue interrupts
for storage devices like NVMe. The driver allocates interrupt vectors with the
required affinity settings using struct irq_affinity. For MSI‑X devices, this
is done via pci_alloc_irq_vectors_affinity() with the PCI_IRQ_AFFINITY flag
set.

Based on the provided affinity information, the IRQ core attempts to spread the
interrupts evenly across the system. The affinity masks are computed during
this allocation step, but the final IRQ assignment is performed when
request_irq() is invoked.

Isolated CPUs
-------------

The affinity of managed interrupts is handled entirely in the kernel and cannot
be modified from user space through the /proc interfaces. The managed_irq
sub‑parameter of the isolcpus boot option specifies a CPU mask that managed
interrupts should attempt to avoid. This isolation is best‑effort and only
applies if the automatically assigned interrupt mask also contains online CPUs
outside the avoided mask. If the requested mask contains only isolated CPUs,
the setting has no effect.

CPUs listed in the avoided mask remain part of the interrupt’s affinity mask.
This means that if all non‑isolated CPUs go offline while isolated CPUs remain
online, the interrupt will be assigned to one of the isolated CPUs.

The following examples assume a system with 8 CPUs.

- A QEMU instance is booted with "-device virtio-scsi-pci".
  The MSI‑X device exposes 11 interrupts: 3 "management" interrupts and 8
  "queue" interrupts. The driver requests the 8 queue interrupts, each of which
  is affine to exactly one CPU. If that CPU goes offline, the interrupt is shut
  down.

  Assuming interrupt 48 is one of the queue interrupts, the following appears::

    /proc/irq/48/effective_affinity_list:7
    /proc/irq/48/smp_affinity_list:7

  This indicates that the interrupt is served only by CPU7. Shutting down CPU7
  does not migrate the interrupt to another CPU::

    /proc/irq/48/effective_affinity_list:0
    /proc/irq/48/smp_affinity_list:7

  This can be verified via the debugfs interface
  (/sys/kernel/debug/irq/irqs/48). The dstate field will include
  IRQD_IRQ_DISABLED, IRQD_IRQ_MASKED and IRQD_MANAGED_SHUTDOWN.

- A QEMU instance is booted with "-device virtio-scsi-pci,num_queues=2"
  and the kernel command line includes:
  "irqaffinity=0,1 isolcpus=domain,2-7 isolcpus=managed_irq,1-3,5-7".
  The MSI‑X device exposes 5 interrupts: 3 management interrupts and 2 queue
  interrupts. The management interrupts follow the irqaffinity= setting. The
  queue interrupts are spread across available CPUs::

    /proc/irq/47/effective_affinity_list:0
    /proc/irq/47/smp_affinity_list:0-3
    /proc/irq/48/effective_affinity_list:4
    /proc/irq/48/smp_affinity_list:4-7

  The two queue interrupts are evenly distributed. Interrupt 48 is placed on CPU4
  because the managed_irq mask avoids CPUs 5–7 when possible.

  Replacing the managed_irq argument with "isolcpus=managed_irq,1-3,4-5,7"
  results in::

    /proc/irq/48/effective_affinity_list:6
    /proc/irq/48/smp_affinity_list:4-7

  Interrupt 48 is now served on CPU6 because the system avoids CPUs 4, 5 and
  7. If CPU6 is taken offline, the interrupt migrates to one of the "isolated"
  CPUs::

    /proc/irq/48/effective_affinity_list:7
    /proc/irq/48/smp_affinity_list:4-7

  The interrupt is shut down once all CPUs listed in its smp_affinity mask are
  offline.
