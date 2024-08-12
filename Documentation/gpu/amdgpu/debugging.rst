===============
 GPU Debugging
===============

GPUVM Debugging
===============

To aid in debugging GPU virtual memory related problems, the driver supports a
number of options module parameters:

`vm_fault_stop` - If non-0, halt the GPU memory controller on a GPU page fault.

`vm_update_mode` - If non-0, use the CPU to update GPU page tables rather than
the GPU.


Decoding a GPUVM Page Fault
===========================

If you see a GPU page fault in the kernel log, you can decode it to figure
out what is going wrong in your application.  A page fault in your kernel
log may look something like this:

::

 [gfxhub0] no-retry page fault (src_id:0 ring:24 vmid:3 pasid:32777, for process glxinfo pid 2424 thread glxinfo:cs0 pid 2425)
   in page starting at address 0x0000800102800000 from IH client 0x1b (UTCL2)
 VM_L2_PROTECTION_FAULT_STATUS:0x00301030
 	Faulty UTCL2 client ID: TCP (0x8)
 	MORE_FAULTS: 0x0
 	WALKER_ERROR: 0x0
 	PERMISSION_FAULTS: 0x3
 	MAPPING_ERROR: 0x0
 	RW: 0x0

First you have the memory hub, gfxhub and mmhub.  gfxhub is the memory
hub used for graphics, compute, and sdma on some chips.  mmhub is the
memory hub used for multi-media and sdma on some chips.

Next you have the vmid and pasid.  If the vmid is 0, this fault was likely
caused by the kernel driver or firmware.  If the vmid is non-0, it is generally
a fault in a user application.  The pasid is used to link a vmid to a system
process id.  If the process is active when the fault happens, the process
information will be printed.

The GPU virtual address that caused the fault comes next.

The client ID indicates the GPU block that caused the fault.
Some common client IDs:

- CB/DB: The color/depth backend of the graphics pipe
- CPF: Command Processor Frontend
- CPC: Command Processor Compute
- CPG: Command Processor Graphics
- TCP/SQC/SQG: Shaders
- SDMA: SDMA engines
- VCN: Video encode/decode engines
- JPEG: JPEG engines

PERMISSION_FAULTS describe what faults were encountered:

- bit 0: the PTE was not valid
- bit 1: the PTE read bit was not set
- bit 2: the PTE write bit was not set
- bit 3: the PTE execute bit was not set

Finally, RW, indicates whether the access was a read (0) or a write (1).

In the example above, a shader (cliend id = TCP) generated a read (RW = 0x0) to
an invalid page (PERMISSION_FAULTS = 0x3) at GPU virtual address
0x0000800102800000.  The user can then inspect their shader code and resource
descriptor state to determine what caused the GPU page fault.

UMR
===

`umr <https://gitlab.freedesktop.org/tomstdenis/umr>`_ is a general purpose
GPU debugging and diagnostics tool.  Please see the umr
`documentation <https://umr.readthedocs.io/en/main/>`_ for more information
about its capabilities.
