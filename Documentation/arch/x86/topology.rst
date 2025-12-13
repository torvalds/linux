.. SPDX-License-Identifier: GPL-2.0

============
x86 Topology
============

This documents and clarifies the main aspects of x86 topology modelling and
representation in the kernel. Update/change when doing changes to the
respective code.

The architecture-agnostic topology definitions are in
Documentation/admin-guide/cputopology.rst. This file holds x86-specific
differences/specialities which must not necessarily apply to the generic
definitions. Thus, the way to read up on Linux topology on x86 is to start
with the generic one and look at this one in parallel for the x86 specifics.

Needless to say, code should use the generic functions - this file is *only*
here to *document* the inner workings of x86 topology.

Started by Thomas Gleixner <tglx@linutronix.de> and Borislav Petkov <bp@alien8.de>.

The main aim of the topology facilities is to present adequate interfaces to
code which needs to know/query/use the structure of the running system wrt
threads, cores, packages, etc.

The kernel does not care about the concept of physical sockets because a
socket has no relevance to software. It's an electromechanical component. In
the past a socket always contained a single package (see below), but with the
advent of Multi Chip Modules (MCM) a socket can hold more than one package. So
there might be still references to sockets in the code, but they are of
historical nature and should be cleaned up.

The topology of a system is described in the units of:

    - packages
    - cores
    - threads

Package
=======
Packages contain a number of cores plus shared resources, e.g. DRAM
controller, shared caches etc.

Modern systems may also use the term 'Die' for package.

AMD nomenclature for package is 'Node'.

Package-related topology information in the kernel:

  - topology_num_threads_per_package()

    The number of threads in a package.

  - topology_num_cores_per_package()

    The number of cores in a package.

  - topology_max_dies_per_package()

    The maximum number of dies in a package.

  - cpuinfo_x86.topo.die_id:

    The physical ID of the die.

  - cpuinfo_x86.topo.pkg_id:

    The physical ID of the package. This information is retrieved via CPUID
    and deduced from the APIC IDs of the cores in the package.

    Modern systems use this value for the socket. There may be multiple
    packages within a socket. This value may differ from topo.die_id.

  - cpuinfo_x86.topo.logical_pkg_id:

    The logical ID of the package. As we do not trust BIOSes to enumerate the
    packages in a consistent way, we introduced the concept of logical package
    ID so we can sanely calculate the number of maximum possible packages in
    the system and have the packages enumerated linearly.

  - topology_max_packages():

    The maximum possible number of packages in the system. Helpful for per
    package facilities to preallocate per package information.

  - cpuinfo_x86.topo.llc_id:

      - On Intel, the first APIC ID of the list of CPUs sharing the Last Level
        Cache

      - On AMD, the Node ID or Core Complex ID containing the Last Level
        Cache. In general, it is a number identifying an LLC uniquely on the
        system.

Cores
=====
A core consists of 1 or more threads. It does not matter whether the threads
are SMT- or CMT-type threads.

AMDs nomenclature for a CMT core is "Compute Unit". The kernel always uses
"core".

Threads
=======
A thread is a single scheduling unit. It's the equivalent to a logical Linux
CPU.

AMDs nomenclature for CMT threads is "Compute Unit Core". The kernel always
uses "thread".

Thread-related topology information in the kernel:

  - topology_core_cpumask():

    The cpumask contains all online threads in the package to which a thread
    belongs.

    The number of online threads is also printed in /proc/cpuinfo "siblings."

  - topology_sibling_cpumask():

    The cpumask contains all online threads in the core to which a thread
    belongs.

  - topology_logical_package_id():

    The logical package ID to which a thread belongs.

  - topology_physical_package_id():

    The physical package ID to which a thread belongs.

  - topology_core_id();

    The ID of the core to which a thread belongs. It is also printed in /proc/cpuinfo
    "core_id."

  - topology_logical_core_id();

    The logical core ID to which a thread belongs.



System topology enumeration
===========================

The topology on x86 systems can be discovered using a combination of vendor
specific CPUID leaves which enumerate the processor topology and the cache
hierarchy.

The CPUID leaves in their preferred order of parsing for each x86 vendor is as
follows:

1) AMD

   1) CPUID leaf 0x80000026 [Extended CPU Topology] (Core::X86::Cpuid::ExCpuTopology)

      The extended CPUID leaf 0x80000026 is the extension of the CPUID leaf 0xB
      and provides the topology information of Core, Complex, CCD (Die), and
      Socket in each level.

      Support for the leaf is discovered by checking if the maximum extended
      CPUID level is >= 0x80000026 and then checking if `LogProcAtThisLevel`
      in `EBX[15:0]` at a particular level (starting from 0) is non-zero.

      The `LevelType` in `ECX[15:8]` at the level provides the topology domain
      the level describes - Core, Complex, CCD(Die), or the Socket.

      The kernel uses the `CoreMaskWidth` from `EAX[4:0]` to discover the
      number of bits that need to be right-shifted from `ExtendedLocalApicId`
      in `EDX[31:0]` in order to get a unique Topology ID for the topology 
      level. CPUs with the same Topology ID share the resources at that level.

      CPUID leaf 0x80000026 also provides more information regarding the power
      and efficiency rankings, and about the core type on AMD processors with
      heterogeneous characteristics.

      If CPUID leaf 0x80000026 is supported, further parsing is not required.

   2) CPUID leaf 0x0000000B [Extended Topology Enumeration] (Core::X86::Cpuid::ExtTopEnum)

      The extended CPUID leaf 0x0000000B is the predecessor on the extended
      CPUID leaf 0x80000026 and only describes the core, and the socket domains
      of the processor topology.

      The support for the leaf is discovered by checking if the maximum supported
      CPUID level is >= 0xB and then if `EBX[31:0]` at a particular level
      (starting from 0) is non-zero.

      The `LevelType` in `ECX[15:8]` at the level provides the topology domain
      that the level describes - Thread, or Processor (Socket).

      The kernel uses the `CoreMaskWidth` from `EAX[4:0]` to discover the
      number of bits that need to be right-shifted from the `ExtendedLocalApicId`
      in `EDX[31:0]` to get a unique Topology ID for that topology level. CPUs
      sharing the Topology ID share the resources at that level.

      If CPUID leaf 0xB is supported, further parsing is not required.


   3) CPUID leaf 0x80000008 ECX [Size Identifiers] (Core::X86::Cpuid::SizeId)

      If neither the CPUID leaf 0x80000026 nor 0xB is supported, the number of
      CPUs on the package is detected using the Size Identifier leaf
      0x80000008 ECX.

      The support for the leaf is discovered by checking if the supported
      extended CPUID level is >= 0x80000008.

      The shifts from the APIC ID for the Socket ID is calculated from the
      `ApicIdSize` field in `ECX[15:12]` if it is non-zero.

      If `ApicIdSize` is reported to be zero, the shift is calculated as the
      order of the `number of threads` calculated from `NC` field in
      `ECX[7:0]` which describes the `number of threads - 1` on the package.

      Unless Extended APIC ID is supported, the APIC ID used to find the
      Socket ID is from the `LocalApicId` field of CPUID leaf 0x00000001
      `EBX[31:24]`.

      The topology parsing continues to detect if Extended APIC ID is
      supported or not.


   4) CPUID leaf 0x8000001E [Extended APIC ID, Core Identifiers, Node Identifiers]
      (Core::X86::Cpuid::{ExtApicId,CoreId,NodeId})

      The support for Extended APIC ID can be detected by checking for the
      presence of `TopologyExtensions` in `ECX[22]` of CPUID leaf 0x80000001
      [Feature Identifiers] (Core::X86::Cpuid::FeatureExtIdEcx).

      If Topology Extensions is supported, the APIC ID from `ExtendedApicId`
      from CPUID leaf 0x8000001E `EAX[31:0]` should be preferred over that from
      `LocalApicId` field of CPUID leaf 0x00000001 `EBX[31:24]` for topology
      enumeration.

      On processors of Family 0x17 and above that do not support CPUID leaf
      0x80000026 or CPUID leaf 0xB, the shifts from the APIC ID for the Core
      ID is calculated using the order of `number of threads per core`
      calculated using the `ThreadsPerCore` field in `EBX[15:8]` which
      describes `number of threads per core - 1`.

      On Processors of Family 0x15, the Core ID from `EBX[7:0]` is used as the
      `cu_id` (Compute Unit ID) to detect CPUs that share the compute units.


   All AMD processors that support the `TopologyExtensions` feature store the
   `NodeId` from the `ECX[7:0]` of CPUID leaf 0x8000001E 
   (Core::X86::Cpuid::NodeId) as the per-CPU `node_id`. On older processors,
   the `node_id` was discovered using MSR_FAM10H_NODE_ID MSR (MSR
   0x0xc001_100c). The presence of the NODE_ID MSR was detected by checking
   `ECX[19]` of CPUID leaf 0x80000001 [Feature Identifiers]
   (Core::X86::Cpuid::FeatureExtIdEcx).


2) Intel

   On Intel platforms, the CPUID leaves that enumerate the processor
   topology are as follows:

   1) CPUID leaf 0x1F (V2 Extended Topology Enumeration Leaf)

      The CPUID leaf 0x1F is the extension of the CPUID leaf 0xB and provides
      the topology information of Core, Module, Tile, Die, DieGrp, and Socket
      in each level.

      The support for the leaf is discovered by checking if the supported
      CPUID level is >= 0x1F and then `EBX[31:0]` at a particular level
      (starting from 0) is non-zero.

      The `Domain Type` in `ECX[15:8]` of the sub-leaf provides the topology
      domain that the level describes - Core, Module, Tile, Die, DieGrp, and
      Socket.

      The kernel uses the value from `EAX[4:0]` to discover the number of
      bits that need to be right shifted from the `x2APIC ID` in `EDX[31:0]`
      to get a unique Topology ID for the topology level. CPUs with the same
      Topology ID share the resources at that level.

      If CPUID leaf 0x1F is supported, further parsing is not required.


   2) CPUID leaf 0x0000000B (Extended Topology Enumeration Leaf)

      The extended CPUID leaf 0x0000000B is the predecessor of the V2 Extended
      Topology Enumeration Leaf 0x1F and only describes the core, and the
      socket domains of the processor topology.

      The support for the leaf is iscovered by checking if the supported CPUID
      level is >= 0xB and then checking if `EBX[31:0]` at a particular level
      (starting from 0) is non-zero.

      CPUID leaf 0x0000000B shares the same layout as CPUID leaf 0x1F and
      should be enumerated in a similar manner.

      If CPUID leaf 0xB is supported, further parsing is not required.


   3) CPUID leaf 0x00000004 (Deterministic Cache Parameters Leaf)

      On Intel processors that support neither CPUID leaf 0x1F, nor CPUID leaf
      0xB, the shifts for the SMT domains is calculated using the number of
      CPUs sharing the L1 cache.

      Processors that feature Hyper-Threading is detected using `EDX[28]` of
      CPUID leaf 0x1 (Basic CPUID Information).

      The order of `Maximum number of addressable IDs for logical processors
      sharing this cache` from `EAX[25:14]` of level-0 of CPUID 0x4 provides
      the shifts from the APIC ID required to compute the Core ID.

      The APIC ID and Package information is computed using the data from
      CPUID leaf 0x1.


   4) CPUID leaf 0x00000001 (Basic CPUID Information)

      The mask and shifts to derive the Physical Package (socket) ID is
      computed using the `Maximum number of addressable IDs for logical
      processors in this physical package` from `EBX[23:16]` of CPUID leaf
      0x1.

     The APIC ID on the legacy platforms is derived from the `Initial APIC
     ID` field from `EBX[31:24]` of CPUID leaf 0x1.


3) Centaur and Zhaoxin

   Similar to Intel, Centaur and Zhaoxin use a combination of CPUID leaf
   0x00000004 (Deterministic Cache Parameters Leaf) and CPUID leaf 0x00000001
   (Basic CPUID Information) to derive the topology information.



System topology examples
========================

.. note::
  The alternative Linux CPU enumeration depends on how the BIOS enumerates the
  threads. Many BIOSes enumerate all threads 0 first and then all threads 1.
  That has the "advantage" that the logical Linux CPU numbers of threads 0 stay
  the same whether threads are enabled or not. That's merely an implementation
  detail and has no practical impact.

1) Single Package, Single Core::

   [package 0] -> [core 0] -> [thread 0] -> Linux CPU 0

2) Single Package, Dual Core

   a) One thread per core::

	[package 0] -> [core 0] -> [thread 0] -> Linux CPU 0
		    -> [core 1] -> [thread 0] -> Linux CPU 1

   b) Two threads per core::

	[package 0] -> [core 0] -> [thread 0] -> Linux CPU 0
				-> [thread 1] -> Linux CPU 1
		    -> [core 1] -> [thread 0] -> Linux CPU 2
				-> [thread 1] -> Linux CPU 3

      Alternative enumeration::

	[package 0] -> [core 0] -> [thread 0] -> Linux CPU 0
				-> [thread 1] -> Linux CPU 2
		    -> [core 1] -> [thread 0] -> Linux CPU 1
				-> [thread 1] -> Linux CPU 3

      AMD nomenclature for CMT systems::

	[node 0] -> [Compute Unit 0] -> [Compute Unit Core 0] -> Linux CPU 0
				     -> [Compute Unit Core 1] -> Linux CPU 1
		 -> [Compute Unit 1] -> [Compute Unit Core 0] -> Linux CPU 2
				     -> [Compute Unit Core 1] -> Linux CPU 3

4) Dual Package, Dual Core

   a) One thread per core::

	[package 0] -> [core 0] -> [thread 0] -> Linux CPU 0
		    -> [core 1] -> [thread 0] -> Linux CPU 1

	[package 1] -> [core 0] -> [thread 0] -> Linux CPU 2
		    -> [core 1] -> [thread 0] -> Linux CPU 3

   b) Two threads per core::

	[package 0] -> [core 0] -> [thread 0] -> Linux CPU 0
				-> [thread 1] -> Linux CPU 1
		    -> [core 1] -> [thread 0] -> Linux CPU 2
				-> [thread 1] -> Linux CPU 3

	[package 1] -> [core 0] -> [thread 0] -> Linux CPU 4
				-> [thread 1] -> Linux CPU 5
		    -> [core 1] -> [thread 0] -> Linux CPU 6
				-> [thread 1] -> Linux CPU 7

      Alternative enumeration::

	[package 0] -> [core 0] -> [thread 0] -> Linux CPU 0
				-> [thread 1] -> Linux CPU 4
		    -> [core 1] -> [thread 0] -> Linux CPU 1
				-> [thread 1] -> Linux CPU 5

	[package 1] -> [core 0] -> [thread 0] -> Linux CPU 2
				-> [thread 1] -> Linux CPU 6
		    -> [core 1] -> [thread 0] -> Linux CPU 3
				-> [thread 1] -> Linux CPU 7

      AMD nomenclature for CMT systems::

	[node 0] -> [Compute Unit 0] -> [Compute Unit Core 0] -> Linux CPU 0
				     -> [Compute Unit Core 1] -> Linux CPU 1
		 -> [Compute Unit 1] -> [Compute Unit Core 0] -> Linux CPU 2
				     -> [Compute Unit Core 1] -> Linux CPU 3

	[node 1] -> [Compute Unit 0] -> [Compute Unit Core 0] -> Linux CPU 4
				     -> [Compute Unit Core 1] -> Linux CPU 5
		 -> [Compute Unit 1] -> [Compute Unit Core 0] -> Linux CPU 6
				     -> [Compute Unit Core 1] -> Linux CPU 7
