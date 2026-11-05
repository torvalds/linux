==============
UALink Support
==============

Overview
========

Connected GPUs in a pod can directly access the remove memory on another GPU
over UALink.  Unlike RMDA, there is no copy involved; it is direct loads/stores
over the fabric.  Shared memory can only be accessed by a remote GPU if the
memory was exported and the importer has been authorized. For the memory to be
shared, it must be part of a unified physical address space shared between
nodes.  This address space is called NPA (Nework Physical Address) space.  This
address space is partitioned between the GPUs so that each GPU has its own
segment of the address space in which to export its memory.  Each GPU maintains
a dedicated set of page tables for their NPA space similar to GPUVM.  Note that
this mechanism only allows for GPU access to remote memory.  The remote memory
is not CPU accessible.

Exported memory is pinned.  This is similar to how dma-bufs in VRAM are pinned
for P2P access.  Remote TLB shootdowns from the exporter are used when the
exported memory is freed in order to remove access by remote GPUs.

To access remote memory, the driver can map NPA addresses into its per process
GPUVM page tables just like local memory.  Applications use opaque handles to
represent remote memory.  GPUs in a pod communicate with eachother directly to
exchange NPA addresses between importers and exporters.  If a node goes offline
or is reset, their peers will clean up any remaining refrences that are lost
when that happens.  NPA addresses are exchanged directly between the kernel
drivers using the scale up fabric.  NPA addresses are never exposed to user
space.

On the importer, the NPA space is like another physical address space. NPA
addresses can be used as physical addresses for GPUVM to provide GPU virtual
addresses to the memory for processes using the GPU.

On the exporter, the NPA space provides a way to expose discontiguous local
memory as a contiguous address range for remote GPUs.  This allows the exporter
to locally manage the pages mapped into the NPA space.

Remote NPAs are managed like another device specific TTM pool similar to
doorbells or VRAM, however they cannot be CPU mapped.


User Interface
==============
Two IOCTLs are provided to export and import remote memory.

Export Memory
-------------
To export memory, a UALINK handle must be created for an allocation that can be
shared with another node in the pod.  To do this the exporter calls the GEM
UALink IOCTL with the GEM handle to the buffer it wants to export.  The IOCTL
returns a unique 128 bit handle which can be shared with the remote host.
Calling export on the same GEM handle always returns the same UALink handle.
The UALink handle is destroyed when the GEM object reference count reaches 0.

Import Memory
-------------
To import remote memory, the UALink handle from the remote node must be
converted from a UALink handle to a local GEM object which represents the local
reference to the NPA space on the importer.  If the memory has already been
imported, it just returns a new reference to the existing GEM object.  If not,
the importer queries the exporter to get the NPA address.  Once it has that, the
importer can create the GEM to represent the NPA space used by the allocation.
The GEM object is then exported to the caller as a dma-buf. The dma-buf is
leveraged for dynamic attachment which provides the ability to revoke access
when necessary.


Device to Device Communications
===============================

Devices communicate via a protocol implemented in firmware.  Mesages sent to a
remote node generate an interrupt on that node for servicing.
