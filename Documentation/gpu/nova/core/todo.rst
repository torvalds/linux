.. SPDX-License-Identifier: (GPL-2.0+ OR MIT)

=========
Task List
=========

Tasks may have the following fields:

- ``Complexity``: Describes the required familiarity with Rust and / or the
  corresponding kernel APIs or subsystems. There are four different complexities,
  ``Beginner``, ``Intermediate``, ``Advanced`` and ``Expert``.
- ``Reference``: References to other tasks.
- ``Link``: Links to external resources.
- ``Contact``: The person that can be contacted for further information about
  the task.

A task might have `[ABCD]` code after its name. This code can be used to grep
into the code for `TODO` entries related to it.

Enablement (Rust)
=================

Tasks that are not directly related to nova-core, but are preconditions in terms
of required APIs.

FromPrimitive API [FPRI]
------------------------

Sometimes the need arises to convert a number to a value of an enum or a
structure.

A good example from nova-core would be the ``Chipset`` enum type, which defines
the value ``AD102``. When probing the GPU the value ``0x192`` can be read from a
certain register indication the chipset AD102. Hence, the enum value ``AD102``
should be derived from the number ``0x192``. Currently, nova-core uses a custom
implementation (``Chipset::from_u32`` for this.

Instead, it would be desirable to have something like the ``FromPrimitive``
trait [1] from the num crate.

Having this generalization also helps with implementing a generic macro that
automatically generates the corresponding mappings between a value and a number.

| Complexity: Beginner
| Link: https://docs.rs/num/latest/num/trait.FromPrimitive.html

Conversion from byte slices for types implementing FromBytes [TRSM]
-------------------------------------------------------------------

We retrieve several structures from byte streams coming from the BIOS or loaded
firmware. At the moment converting the bytes slice into the proper type require
an inelegant `unsafe` operation; this will go away once `FromBytes` implements
a proper `from_bytes` method.

| Complexity: Beginner

CoherentAllocation improvements [COHA]
--------------------------------------

`CoherentAllocation` needs a safe way to write into the allocation, and to
obtain slices within the allocation.

| Complexity: Beginner
| Contact: Abdiel Janulgue

Generic register abstraction [REGA]
-----------------------------------

Work out how register constants and structures can be automatically generated
through generalized macros.

Example:

.. code-block:: rust

	register!(BOOT0, 0x0, u32, pci::Bar<SIZE>, Fields [
	   MINOR_REVISION(3:0, RO),
	   MAJOR_REVISION(7:4, RO),
	   REVISION(7:0, RO), // Virtual register combining major and minor rev.
	])

This could expand to something like:

.. code-block:: rust

	const BOOT0_OFFSET: usize = 0x00000000;
	const BOOT0_MINOR_REVISION_SHIFT: u8 = 0;
	const BOOT0_MINOR_REVISION_MASK: u32 = 0x0000000f;
	const BOOT0_MAJOR_REVISION_SHIFT: u8 = 4;
	const BOOT0_MAJOR_REVISION_MASK: u32 = 0x000000f0;
	const BOOT0_REVISION_SHIFT: u8 = BOOT0_MINOR_REVISION_SHIFT;
	const BOOT0_REVISION_MASK: u32 = BOOT0_MINOR_REVISION_MASK | BOOT0_MAJOR_REVISION_MASK;

	struct Boot0(u32);

	impl Boot0 {
	   #[inline]
	   fn read(bar: &RevocableGuard<'_, pci::Bar<SIZE>>) -> Self {
	      Self(bar.readl(BOOT0_OFFSET))
	   }

	   #[inline]
	   fn minor_revision(&self) -> u32 {
	      (self.0 & BOOT0_MINOR_REVISION_MASK) >> BOOT0_MINOR_REVISION_SHIFT
	   }

	   #[inline]
	   fn major_revision(&self) -> u32 {
	      (self.0 & BOOT0_MAJOR_REVISION_MASK) >> BOOT0_MAJOR_REVISION_SHIFT
	   }

	   #[inline]
	   fn revision(&self) -> u32 {
	      (self.0 & BOOT0_REVISION_MASK) >> BOOT0_REVISION_SHIFT
	   }
	}

Usage:

.. code-block:: rust

	let bar = bar.try_access().ok_or(ENXIO)?;

	let boot0 = Boot0::read(&bar);
	pr_info!("Revision: {}\n", boot0.revision());

A work-in-progress implementation currently resides in
`drivers/gpu/nova-core/regs/macros.rs` and is used in nova-core. It would be
nice to improve it (possibly using proc macros) and move it to the `kernel`
crate so it can be used by other components as well.

Features desired before this happens:

* Make I/O optional I/O (for field values that are not registers),
* Support other sizes than `u32`,
* Allow visibility control for registers and individual fields,
* Use Rust slice syntax to express fields ranges.

| Complexity: Advanced
| Contact: Alexandre Courbot

Numerical operations [NUMM]
---------------------------

Nova uses integer operations that are not part of the standard library (or not
implemented in an optimized way for the kernel). These include:

- The "Find Last Set Bit" (`fls` function of the C part of the kernel)
  operation.

A `num` core kernel module is being designed to provide these operations.

| Complexity: Intermediate
| Contact: Alexandre Courbot

Delay / Sleep abstractions [DLAY]
---------------------------------

Rust abstractions for the kernel's delay() and sleep() functions.

FUJITA Tomonori plans to work on abstractions for read_poll_timeout_atomic()
(and friends) [1].

| Complexity: Beginner
| Link: https://lore.kernel.org/netdev/20250228.080550.354359820929821928.fujita.tomonori@gmail.com/ [1]

IRQ abstractions
----------------

Rust abstractions for IRQ handling.

There is active ongoing work from Daniel Almeida [1] for the "core" abstractions
to request IRQs.

Besides optional review and testing work, the required ``pci::Device`` code
around those core abstractions needs to be worked out.

| Complexity: Intermediate
| Link: https://lore.kernel.org/lkml/20250122163932.46697-1-daniel.almeida@collabora.com/ [1]
| Contact: Daniel Almeida

Page abstraction for foreign pages
----------------------------------

Rust abstractions for pages not created by the Rust page abstraction without
direct ownership.

There is active onging work from Abdiel Janulgue [1] and Lina [2].

| Complexity: Advanced
| Link: https://lore.kernel.org/linux-mm/20241119112408.779243-1-abdiel.janulgue@gmail.com/ [1]
| Link: https://lore.kernel.org/rust-for-linux/20250202-rust-page-v1-0-e3170d7fe55e@asahilina.net/ [2]

Scatterlist / sg_table abstractions
-----------------------------------

Rust abstractions for scatterlist / sg_table.

There is preceding work from Abdiel Janulgue, which hasn't made it to the
mailing list yet.

| Complexity: Intermediate
| Contact: Abdiel Janulgue

PCI MISC APIs
-------------

Extend the existing PCI device / driver abstractions by SR-IOV, config space,
capability, MSI API abstractions.

| Complexity: Beginner

XArray bindings [XARR]
----------------------

We need bindings for `xa_alloc`/`xa_alloc_cyclic` in order to generate the
auxiliary device IDs.

| Complexity: Intermediate

Debugfs abstractions
--------------------

Rust abstraction for debugfs APIs.

| Reference: Export GSP log buffers
| Complexity: Intermediate

GPU (general)
=============

Initial Devinit support
-----------------------

Implement BIOS Device Initialization, i.e. memory sizing, waiting, PLL
configuration.

| Contact: Dave Airlie
| Complexity: Beginner

MMU / PT management
-------------------

Work out the architecture for MMU / page table management.

We need to consider that nova-drm will need rather fine-grained control,
especially in terms of locking, in order to be able to implement asynchronous
Vulkan queues.

While generally sharing the corresponding code is desirable, it needs to be
evaluated how (and if at all) sharing the corresponding code is expedient.

| Complexity: Expert

VRAM memory allocator
---------------------

Investigate options for a VRAM memory allocator.

Some possible options:
  - Rust abstractions for
    - RB tree (interval tree) / drm_mm
    - maple_tree
  - native Rust collections

| Complexity: Advanced

Instance Memory
---------------

Implement support for instmem (bar2) used to store page tables.

| Complexity: Intermediate
| Contact: Dave Airlie

GPU System Processor (GSP)
==========================

Export GSP log buffers
----------------------

Recent patches from Timur Tabi [1] added support to expose GSP-RM log buffers
(even after failure to probe the driver) through debugfs.

This is also an interesting feature for nova-core, especially in the early days.

| Link: https://lore.kernel.org/nouveau/20241030202952.694055-2-ttabi@nvidia.com/ [1]
| Reference: Debugfs abstractions
| Complexity: Intermediate

GSP firmware abstraction
------------------------

The GSP-RM firmware API is unstable and may incompatibly change from version to
version, in terms of data structures and semantics.

This problem is one of the big motivations for using Rust for nova-core, since
it turns out that Rust's procedural macro feature provides a rather elegant way
to address this issue:

1. generate Rust structures from the C headers in a separate namespace per version
2. build abstraction structures (within a generic namespace) that implement the
   firmware interfaces; annotate the differences in implementation with version
   identifiers
3. use a procedural macro to generate the actual per version implementation out
   of this abstraction
4. instantiate the correct version type one on runtime (can be sure that all
   have the same interface because it's defined by a common trait)

There is a PoC implementation of this pattern, in the context of the nova-core
PoC driver.

This task aims at refining the feature and ideally generalize it, to be usable
by other drivers as well.

| Complexity: Expert

GSP message queue
-----------------

Implement low level GSP message queue (command, status) for communication
between the kernel driver and GSP.

| Complexity: Advanced
| Contact: Dave Airlie

Bootstrap GSP
-------------

Call the boot firmware to boot the GSP processor; execute initial control
messages.

| Complexity: Intermediate
| Contact: Dave Airlie

Client / Device APIs
--------------------

Implement the GSP message interface for client / device allocation and the
corresponding client and device allocation APIs.

| Complexity: Intermediate
| Contact: Dave Airlie

Bar PDE handling
----------------

Synchronize page table handling for BARs between the kernel driver and GSP.

| Complexity: Beginner
| Contact: Dave Airlie

FIFO engine
-----------

Implement support for the FIFO engine, i.e. the corresponding GSP message
interface and provide an API for chid allocation and channel handling.

| Complexity: Advanced
| Contact: Dave Airlie

GR engine
---------

Implement support for the graphics engine, i.e. the corresponding GSP message
interface and provide an API for (golden) context creation and promotion.

| Complexity: Advanced
| Contact: Dave Airlie

CE engine
---------

Implement support for the copy engine, i.e. the corresponding GSP message
interface.

| Complexity: Intermediate
| Contact: Dave Airlie

VFN IRQ controller
------------------

Support for the VFN interrupt controller.

| Complexity: Intermediate
| Contact: Dave Airlie

External APIs
=============

nova-core base API
------------------

Work out the common pieces of the API to connect 2nd level drivers, i.e. vGPU
manager and nova-drm.

| Complexity: Advanced

vGPU manager API
----------------

Work out the API parts required by the vGPU manager, which are not covered by
the base API.

| Complexity: Advanced

nova-core C API
---------------

Implement a C wrapper for the APIs required by the vGPU manager driver.

| Complexity: Intermediate

Testing
=======

CI pipeline
-----------

Investigate option for continuous integration testing.

This can go from as simple as running KUnit tests over running (graphics) CTS to
booting up (multiple) guest VMs to test VFIO use-cases.

It might also be worth to consider the introduction of a new test suite directly
sitting on top of the uAPI for more targeted testing and debugging. There may be
options for collaboration / shared code with the Mesa project.

| Complexity: Advanced
