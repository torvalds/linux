=======================================
crashkernel memory reservation on arm64
=======================================

Author: Baoquan He <bhe@redhat.com>

Kdump mechanism is used to capture a corrupted kernel vmcore so that
it can be subsequently analyzed. In order to do this, a preliminarily
reserved memory is needed to pre-load the kdump kernel and boot such
kernel if corruption happens.

That reserved memory for kdump is adapted to be able to minimally
accommodate the kdump kernel and the user space programs needed for the
vmcore collection.

Kernel parameter
================

Through the kernel parameters below, memory can be reserved accordingly
during the early stage of the first kernel booting so that a continuous
large chunk of memomy can be found. The low memory reservation needs to
be considered if the crashkernel is reserved from the high memory area.

- crashkernel=size@offset
- crashkernel=size
- crashkernel=size,high crashkernel=size,low

Low memory and high memory
==========================

For kdump reservations, low memory is the memory area under a specific
limit, usually decided by the accessible address bits of the DMA-capable
devices needed by the kdump kernel to run. Those devices not related to
vmcore dumping can be ignored. On arm64, the low memory upper bound is
not fixed: it is 1G on the RPi4 platform but 4G on most other systems.
On special kernels built with CONFIG_ZONE_(DMA|DMA32) disabled, the
whole system RAM is low memory. Outside of the low memory described
above, the rest of system RAM is considered high memory.

Implementation
==============

1) crashkernel=size@offset
--------------------------

The crashkernel memory must be reserved at the user-specified region or
fail if already occupied.


2) crashkernel=size
-------------------

The crashkernel memory region will be reserved in any available position
according to the search order:

Firstly, the kernel searches the low memory area for an available region
with the specified size.

If searching for low memory fails, the kernel falls back to searching
the high memory area for an available region of the specified size. If
the reservation in high memory succeeds, a default size reservation in
the low memory will be done. Currently the default size is 128M,
sufficient for the low memory needs of the kdump kernel.

Note: crashkernel=size is the recommended option for crashkernel kernel
reservations. The user would not need to know the system memory layout
for a specific platform.

3) crashkernel=size,high crashkernel=size,low
---------------------------------------------

crashkernel=size,(high|low) are an important supplement to
crashkernel=size. They allows the user to specify how much memory needs
to be allocated from the high memory and low memory respectively. On
many systems the low memory is precious and crashkernel reservations
from this area should be kept to a minimum.

To reserve memory for crashkernel=size,high, searching is first
attempted from the high memory region. If the reservation succeeds, the
low memory reservation will be done subsequently.

If reservation from the high memory failed, the kernel falls back to
searching the low memory with the specified size in crashkernel=,high.
If it succeeds, no further reservation for low memory is needed.

Notes:

- If crashkernel=,low is not specified, the default low memory
  reservation will be done automatically.

- if crashkernel=0,low is specified, it means that the low memory
  reservation is omitted intentionally.
