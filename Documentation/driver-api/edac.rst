Error Detection And Correction (EDAC) Devices
=============================================

Main Concepts used at the EDAC subsystem
----------------------------------------

There are several things to be aware of that aren't at all obvious, like
*sockets, *socket sets*, *banks*, *rows*, *chip-select rows*, *channels*,
etc...

These are some of the many terms that are thrown about that don't always
mean what people think they mean (Inconceivable!).  In the interest of
creating a common ground for discussion, terms and their definitions
will be established.

* Memory devices

The individual DRAM chips on a memory stick.  These devices commonly
output 4 and 8 bits each (x4, x8). Grouping several of these in parallel
provides the number of bits that the memory controller expects:
typically 72 bits, in order to provide 64 bits + 8 bits of ECC data.

* Memory Stick

A printed circuit board that aggregates multiple memory devices in
parallel.  In general, this is the Field Replaceable Unit (FRU) which
gets replaced, in the case of excessive errors. Most often it is also
called DIMM (Dual Inline Memory Module).

* Memory Socket

A physical connector on the motherboard that accepts a single memory
stick. Also called as "slot" on several datasheets.

* Channel

A memory controller channel, responsible to communicate with a group of
DIMMs. Each channel has its own independent control (command) and data
bus, and can be used independently or grouped with other channels.

* Branch

It is typically the highest hierarchy on a Fully-Buffered DIMM memory
controller. Typically, it contains two channels. Two channels at the
same branch can be used in single mode or in lockstep mode. When
lockstep is enabled, the cacheline is doubled, but it generally brings
some performance penalty. Also, it is generally not possible to point to
just one memory stick when an error occurs, as the error correction code
is calculated using two DIMMs instead of one. Due to that, it is capable
of correcting more errors than on single mode.

* Single-channel

The data accessed by the memory controller is contained into one dimm
only. E. g. if the data is 64 bits-wide, the data flows to the CPU using
one 64 bits parallel access. Typically used with SDR, DDR, DDR2 and DDR3
memories. FB-DIMM and RAMBUS use a different concept for channel, so
this concept doesn't apply there.

* Double-channel

The data size accessed by the memory controller is interlaced into two
dimms, accessed at the same time. E. g. if the DIMM is 64 bits-wide (72
bits with ECC), the data flows to the CPU using a 128 bits parallel
access.

* Chip-select row

This is the name of the DRAM signal used to select the DRAM ranks to be
accessed. Common chip-select rows for single channel are 64 bits, for
dual channel 128 bits. It may not be visible by the memory controller,
as some DIMM types have a memory buffer that can hide direct access to
it from the Memory Controller.

* Single-Ranked stick

A Single-ranked stick has 1 chip-select row of memory. Motherboards
commonly drive two chip-select pins to a memory stick. A single-ranked
stick, will occupy only one of those rows. The other will be unused.

.. _doubleranked:

* Double-Ranked stick

A double-ranked stick has two chip-select rows which access different
sets of memory devices.  The two rows cannot be accessed concurrently.

* Double-sided stick

**DEPRECATED TERM**, see :ref:`Double-Ranked stick <doubleranked>`.

A double-sided stick has two chip-select rows which access different sets
of memory devices. The two rows cannot be accessed concurrently.
"Double-sided" is irrespective of the memory devices being mounted on
both sides of the memory stick.

* Socket set

All of the memory sticks that are required for a single memory access or
all of the memory sticks spanned by a chip-select row.  A single socket
set has two chip-select rows and if double-sided sticks are used these
will occupy those chip-select rows.

* Bank

This term is avoided because it is unclear when needing to distinguish
between chip-select rows and socket sets.

* High Bandwidth Memory (HBM)

HBM is a new memory type with low power consumption and ultra-wide
communication lanes. It uses vertically stacked memory chips (DRAM dies)
interconnected by microscopic wires called "through-silicon vias," or
TSVs.

Several stacks of HBM chips connect to the CPU or GPU through an ultra-fast
interconnect called the "interposer". Therefore, HBM's characteristics
are nearly indistinguishable from on-chip integrated RAM.

Memory Controllers
------------------

Most of the EDAC core is focused on doing Memory Controller error detection.
The :c:func:`edac_mc_alloc`. It uses internally the struct ``mem_ctl_info``
to describe the memory controllers, with is an opaque struct for the EDAC
drivers. Only the EDAC core is allowed to touch it.

.. kernel-doc:: include/linux/edac.h

.. kernel-doc:: drivers/edac/edac_mc.h

PCI Controllers
---------------

The EDAC subsystem provides a mechanism to handle PCI controllers by calling
the :c:func:`edac_pci_alloc_ctl_info`. It will use the struct
:c:type:`edac_pci_ctl_info` to describe the PCI controllers.

.. kernel-doc:: drivers/edac/edac_pci.h

EDAC Blocks
-----------

The EDAC subsystem also provides a generic mechanism to report errors on
other parts of the hardware via :c:func:`edac_device_alloc_ctl_info` function.

The structures :c:type:`edac_dev_sysfs_block_attribute`,
:c:type:`edac_device_block`, :c:type:`edac_device_instance` and
:c:type:`edac_device_ctl_info` provide a generic or abstract 'edac_device'
representation at sysfs.

This set of structures and the code that implements the APIs for the same, provide for registering EDAC type devices which are NOT standard memory or
PCI, like:

- CPU caches (L1 and L2)
- DMA engines
- Core CPU switches
- Fabric switch units
- PCIe interface controllers
- other EDAC/ECC type devices that can be monitored for
  errors, etc.

It allows for a 2 level set of hierarchy.

For example, a cache could be composed of L1, L2 and L3 levels of cache.
Each CPU core would have its own L1 cache, while sharing L2 and maybe L3
caches. On such case, those can be represented via the following sysfs
nodes::

	/sys/devices/system/edac/..

	pci/		<existing pci directory (if available)>
	mc/		<existing memory device directory>
	cpu/cpu0/..	<L1 and L2 block directory>
		/L1-cache/ce_count
			 /ue_count
		/L2-cache/ce_count
			 /ue_count
	cpu/cpu1/..	<L1 and L2 block directory>
		/L1-cache/ce_count
			 /ue_count
		/L2-cache/ce_count
			 /ue_count
	...

	the L1 and L2 directories would be "edac_device_block's"

.. kernel-doc:: drivers/edac/edac_device.h


Heterogeneous system support
----------------------------

An AMD heterogeneous system is built by connecting the data fabrics of
both CPUs and GPUs via custom xGMI links. Thus, the data fabric on the
GPU nodes can be accessed the same way as the data fabric on CPU nodes.

The MI200 accelerators are data center GPUs. They have 2 data fabrics,
and each GPU data fabric contains four Unified Memory Controllers (UMC).
Each UMC contains eight channels. Each UMC channel controls one 128-bit
HBM2e (2GB) channel (equivalent to 8 X 2GB ranks).  This creates a total
of 4096-bits of DRAM data bus.

While the UMC is interfacing a 16GB (8high X 2GB DRAM) HBM stack, each UMC
channel is interfacing 2GB of DRAM (represented as rank).

Memory controllers on AMD GPU nodes can be represented in EDAC thusly:

	GPU DF / GPU Node -> EDAC MC
	GPU UMC           -> EDAC CSROW
	GPU UMC channel   -> EDAC CHANNEL

For example: a heterogeneous system with 1 AMD CPU is connected to
4 MI200 (Aldebaran) GPUs using xGMI.

Some more heterogeneous hardware details:

- The CPU UMC (Unified Memory Controller) is mostly the same as the GPU UMC.
  They have chip selects (csrows) and channels. However, the layouts are different
  for performance, physical layout, or other reasons.
- CPU UMCs use 1 channel, In this case UMC = EDAC channel. This follows the
  marketing speak. CPU has X memory channels, etc.
- CPU UMCs use up to 4 chip selects, So UMC chip select = EDAC CSROW.
- GPU UMCs use 1 chip select, So UMC = EDAC CSROW.
- GPU UMCs use 8 channels, So UMC channel = EDAC channel.

The EDAC subsystem provides a mechanism to handle AMD heterogeneous
systems by calling system specific ops for both CPUs and GPUs.

AMD GPU nodes are enumerated in sequential order based on the PCI
hierarchy, and the first GPU node is assumed to have a Node ID value
following those of the CPU nodes after latter are fully populated::

	$ ls /sys/devices/system/edac/mc/
		mc0   - CPU MC node 0
		mc1  |
		mc2  |- GPU card[0] => node 0(mc1), node 1(mc2)
		mc3  |
		mc4  |- GPU card[1] => node 0(mc3), node 1(mc4)
		mc5  |
		mc6  |- GPU card[2] => node 0(mc5), node 1(mc6)
		mc7  |
		mc8  |- GPU card[3] => node 0(mc7), node 1(mc8)

For example, a heterogeneous system with one AMD CPU is connected to
four MI200 (Aldebaran) GPUs using xGMI. This topology can be represented
via the following sysfs entries::

	/sys/devices/system/edac/mc/..

	CPU			# CPU node
	├── mc 0

	GPU Nodes are enumerated sequentially after CPU nodes have been populated
	GPU card 1		# Each MI200 GPU has 2 nodes/mcs
	├── mc 1		# GPU node 0 == mc1, Each MC node has 4 UMCs/CSROWs
	│   ├── csrow 0		# UMC 0
	│   │   ├── channel 0	# Each UMC has 8 channels
	│   │   ├── channel 1   # size of each channel is 2 GB, so each UMC has 16 GB
	│   │   ├── channel 2
	│   │   ├── channel 3
	│   │   ├── channel 4
	│   │   ├── channel 5
	│   │   ├── channel 6
	│   │   ├── channel 7
	│   ├── csrow 1		# UMC 1
	│   │   ├── channel 0
	│   │   ├── ..
	│   │   ├── channel 7
	│   ├── ..		..
	│   ├── csrow 3		# UMC 3
	│   │   ├── channel 0
	│   │   ├── ..
	│   │   ├── channel 7
	│   ├── rank 0
	│   ├── ..		..
	│   ├── rank 31		# total 32 ranks/dimms from 4 UMCs
	├
	├── mc 2		# GPU node 1 == mc2
	│   ├── ..		# each GPU has total 64 GB

	GPU card 2
	├── mc 3
	│   ├── ..
	├── mc 4
	│   ├── ..

	GPU card 3
	├── mc 5
	│   ├── ..
	├── mc 6
	│   ├── ..

	GPU card 4
	├── mc 7
	│   ├── ..
	├── mc 8
	│   ├── ..
