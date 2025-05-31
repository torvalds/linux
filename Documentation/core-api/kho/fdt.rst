.. SPDX-License-Identifier: GPL-2.0-or-later

=======
KHO FDT
=======

KHO uses the flattened device tree (FDT) container format and libfdt
library to create and parse the data that is passed between the
kernels. The properties in KHO FDT are stored in native format.
It includes the physical address of an in-memory structure describing
all preserved memory regions, as well as physical addresses of KHO users'
own FDTs. Interpreting those sub FDTs is the responsibility of KHO users.

KHO nodes and properties
========================

Property ``preserved-memory-map``
---------------------------------

KHO saves a special property named ``preserved-memory-map`` under the root node.
This node contains the physical address of an in-memory structure for KHO to
preserve memory regions across kexec.

Property ``compatible``
-----------------------

The ``compatible`` property determines compatibility between the kernel
that created the KHO FDT and the kernel that attempts to load it.
If the kernel that loads the KHO FDT is not compatible with it, the entire
KHO process will be bypassed.

Property ``fdt``
----------------

Generally, a KHO user serialize its state into its own FDT and instructs
KHO to preserve the underlying memory, such that after kexec, the new kernel
can recover its state from the preserved FDT.

A KHO user thus can create a node in KHO root tree and save the physical address
of its own FDT in that node's property ``fdt`` .

Examples
========

The following example demonstrates KHO FDT that preserves two memory
regions created with ``reserve_mem`` kernel command line parameter::

  /dts-v1/;

  / {
  	compatible = "kho-v1";

	preserved-memory-map = <0x40be16 0x1000000>;

  	memblock {
		fdt = <0x1517 0x1000000>;
  	};
  };

where the ``memblock`` node contains an FDT that is requested by the
subsystem memblock for preservation. The FDT contains the following
serialized data::

  /dts-v1/;

  / {
  	compatible = "memblock-v1";

  	n1 {
  		compatible = "reserve-mem-v1";
  		start = <0xc06b 0x4000000>;
  		size = <0x04 0x00>;
  	};

  	n2 {
  		compatible = "reserve-mem-v1";
  		start = <0xc067 0x4000000>;
  		size = <0x04 0x00>;
  	};
  };
