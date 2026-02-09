.. SPDX-License-Identifier: GPL-2.0

============================
PCI Peer-to-Peer DMA Support
============================

The PCI bus has pretty decent support for performing DMA transfers
between two devices on the bus. This type of transaction is henceforth
called Peer-to-Peer (or P2P). However, there are a number of issues that
make P2P transactions tricky to do in a perfectly safe way.

For PCIe the routing of Transaction Layer Packets (TLPs) is well-defined up
until they reach a host bridge or root port. If the path includes PCIe switches
then based on the ACS settings the transaction can route entirely within
the PCIe hierarchy and never reach the root port. The kernel will evaluate
the PCIe topology and always permit P2P in these well-defined cases.

However, if the P2P transaction reaches the host bridge then it might have to
hairpin back out the same root port, be routed inside the CPU SOC to another
PCIe root port, or routed internally to the SOC.

The PCIe specification doesn't define the forwarding of transactions between
hierarchy domains and kernel defaults to blocking such routing. There is an
allow list to allow detecting known-good HW, in which case P2P between any
two PCIe devices will be permitted.

Since P2P inherently is doing transactions between two devices it requires two
drivers to be co-operating inside the kernel. The providing driver has to convey
its MMIO to the consuming driver. To meet the driver model lifecycle rules the
MMIO must have all DMA mapping removed, all CPU accesses prevented, all page
table mappings undone before the providing driver completes remove().

This requires the providing and consuming driver to actively work together to
guarantee that the consuming driver has stopped using the MMIO during a removal
cycle. This is done by either a synchronous invalidation shutdown or waiting
for all usage refcounts to reach zero.

At the lowest level the P2P subsystem offers a naked struct p2p_provider that
delegates lifecycle management to the providing driver. It is expected that
drivers using this option will wrap their MMIO memory in DMABUF and use DMABUF
to provide an invalidation shutdown. These MMIO addresess have no struct page, and
if used with mmap() must create special PTEs. As such there are very few
kernel uAPIs that can accept pointers to them; in particular they cannot be used
with read()/write(), including O_DIRECT.

Building on this, the subsystem offers a layer to wrap the MMIO in a ZONE_DEVICE
pgmap of MEMORY_DEVICE_PCI_P2PDMA to create struct pages. The lifecycle of
pgmap ensures that when the pgmap is destroyed all other drivers have stopped
using the MMIO. This option works with O_DIRECT flows, in some cases, if the
underlying subsystem supports handling MEMORY_DEVICE_PCI_P2PDMA through
FOLL_PCI_P2PDMA. The use of FOLL_LONGTERM is prevented. As this relies on pgmap
it also relies on architecture support along with alignment and minimum size
limitations.


Driver Writer's Guide
=====================

In a given P2P implementation there may be three or more different
types of kernel drivers in play:

* Provider - A driver which provides or publishes P2P resources like
  memory or doorbell registers to other drivers.
* Client - A driver which makes use of a resource by setting up a
  DMA transaction to or from it.
* Orchestrator - A driver which orchestrates the flow of data between
  clients and providers.

In many cases there could be overlap between these three types (i.e.,
it may be typical for a driver to be both a provider and a client).

For example, in the NVMe Target Copy Offload implementation:

* The NVMe PCI driver is both a client, provider and orchestrator
  in that it exposes any CMB (Controller Memory Buffer) as a P2P memory
  resource (provider), it accepts P2P memory pages as buffers in requests
  to be used directly (client) and it can also make use of the CMB as
  submission queue entries (orchestrator).
* The RDMA driver is a client in this arrangement so that an RNIC
  can DMA directly to the memory exposed by the NVMe device.
* The NVMe Target driver (nvmet) can orchestrate the data from the RNIC
  to the P2P memory (CMB) and then to the NVMe device (and vice versa).

This is currently the only arrangement supported by the kernel but
one could imagine slight tweaks to this that would allow for the same
functionality. For example, if a specific RNIC added a BAR with some
memory behind it, its driver could add support as a P2P provider and
then the NVMe Target could use the RNIC's memory instead of the CMB
in cases where the NVMe cards in use do not have CMB support.


Provider Drivers
----------------

A provider simply needs to register a BAR (or a portion of a BAR)
as a P2P DMA resource using :c:func:`pci_p2pdma_add_resource()`.
This will register struct pages for all the specified memory.

After that it may optionally publish all of its resources as
P2P memory using :c:func:`pci_p2pmem_publish()`. This will allow
any orchestrator drivers to find and use the memory. When marked in
this way, the resource must be regular memory with no side effects.

For the time being this is fairly rudimentary in that all resources
are typically going to be P2P memory. Future work will likely expand
this to include other types of resources like doorbells.


Client Drivers
--------------

A client driver only has to use the mapping API :c:func:`dma_map_sg()`
and :c:func:`dma_unmap_sg()` functions as usual, and the implementation
will do the right thing for the P2P capable memory.


Orchestrator Drivers
--------------------

The first task an orchestrator driver must do is compile a list of
all client devices that will be involved in a given transaction. For
example, the NVMe Target driver creates a list including the namespace
block device and the RNIC in use. If the orchestrator has access to
a specific P2P provider to use it may check compatibility using
:c:func:`pci_p2pdma_distance()` otherwise it may find a memory provider
that's compatible with all clients using  :c:func:`pci_p2pmem_find()`.
If more than one provider is supported, the one nearest to all the clients will
be chosen first. If more than one provider is an equal distance away, the
one returned will be chosen at random (it is not an arbitrary but
truly random). This function returns the PCI device to use for the provider
with a reference taken and therefore when it's no longer needed it should be
returned with pci_dev_put().

Once a provider is selected, the orchestrator can then use
:c:func:`pci_alloc_p2pmem()` and :c:func:`pci_free_p2pmem()` to
allocate P2P memory from the provider. :c:func:`pci_p2pmem_alloc_sgl()`
and :c:func:`pci_p2pmem_free_sgl()` are convenience functions for
allocating scatter-gather lists with P2P memory.

Struct Page Caveats
-------------------

While the MEMORY_DEVICE_PCI_P2PDMA pages can be installed in VMAs,
pin_user_pages() and related will not return them unless FOLL_PCI_P2PDMA is set.

The MEMORY_DEVICE_PCI_P2PDMA pages require care to support in the kernel. The
KVA is still MMIO and must still be accessed through the normal
readX()/writeX()/etc helpers. Direct CPU access (e.g. memcpy) is forbidden, just
like any other MMIO mapping. While this will actually work on some
architectures, others will experience corruption or just crash in the kernel.
Supporting FOLL_PCI_P2PDMA in a subsystem requires scrubbing it to ensure no CPU
access happens.


Usage With DMABUF
=================

DMABUF provides an alternative to the above struct page-based
client/provider/orchestrator system and should be used when struct page
doesn't exist. In this mode the exporting driver will wrap
some of its MMIO in a DMABUF and give the DMABUF FD to userspace.

Userspace can then pass the FD to an importing driver which will ask the
exporting driver to map it to the importer.

In this case the initiator and target pci_devices are known and the P2P subsystem
is used to determine the mapping type. The phys_addr_t-based DMA API is used to
establish the dma_addr_t.

Lifecycle is controlled by DMABUF move_notify(). When the exporting driver wants
to remove() it must deliver an invalidation shutdown to all DMABUF importing
drivers through move_notify() and synchronously DMA unmap all the MMIO.

No importing driver can continue to have a DMA map to the MMIO after the
exporting driver has destroyed its p2p_provider.


P2P DMA Support Library
=======================

.. kernel-doc:: drivers/pci/p2pdma.c
   :export:
