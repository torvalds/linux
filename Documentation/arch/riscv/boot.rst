.. SPDX-License-Identifier: GPL-2.0

===============================================
RISC-V Kernel Boot Requirements and Constraints
===============================================

:Author: Alexandre Ghiti <alexghiti@rivosinc.com>
:Date: 23 May 2023

This document describes what the RISC-V kernel expects from bootloaders and
firmware, and also the constraints that any developer must have in mind when
touching the early boot process. For the purposes of this document, the
``early boot process`` refers to any code that runs before the final virtual
mapping is set up.

Pre-kernel Requirements and Constraints
=======================================

The RISC-V kernel expects the following of bootloaders and platform firmware:

Register state
--------------

The RISC-V kernel expects:

  * ``$a0`` to contain the hartid of the current core.
  * ``$a1`` to contain the address of the devicetree in memory.

CSR state
---------

The RISC-V kernel expects:

  * ``$satp = 0``: the MMU, if present, must be disabled.

Reserved memory for resident firmware
-------------------------------------

The RISC-V kernel must not map any resident memory, or memory protected with
PMPs, in the direct mapping, so the firmware must correctly mark those regions
as per the devicetree specification and/or the UEFI specification.

Kernel location
---------------

The RISC-V kernel expects to be placed at a PMD boundary (2MB aligned for rv64
and 4MB aligned for rv32). Note that the EFI stub will physically relocate the
kernel if that's not the case.

Hardware description
--------------------

The firmware can pass either a devicetree or ACPI tables to the RISC-V kernel.

The devicetree is either passed directly to the kernel from the previous stage
using the ``$a1`` register, or when booting with UEFI, it can be passed using the
EFI configuration table.

The ACPI tables are passed to the kernel using the EFI configuration table. In
this case, a tiny devicetree is still created by the EFI stub. Please refer to
"EFI stub and devicetree" section below for details about this devicetree.

Kernel entry
------------

On SMP systems, there are 2 methods to enter the kernel:

- ``RISCV_BOOT_SPINWAIT``: the firmware releases all harts in the kernel, one hart
  wins a lottery and executes the early boot code while the other harts are
  parked waiting for the initialization to finish. This method is mostly used to
  support older firmwares without SBI HSM extension and M-mode RISC-V kernel.
- ``Ordered booting``: the firmware releases only one hart that will execute the
  initialization phase and then will start all other harts using the SBI HSM
  extension. The ordered booting method is the preferred booting method for
  booting the RISC-V kernel because it can support CPU hotplug and kexec.

UEFI
----

UEFI memory map
~~~~~~~~~~~~~~~

When booting with UEFI, the RISC-V kernel will use only the EFI memory map to
populate the system memory.

The UEFI firmware must parse the subnodes of the ``/reserved-memory`` devicetree
node and abide by the devicetree specification to convert the attributes of
those subnodes (``no-map`` and ``reusable``) into their correct EFI equivalent
(refer to section "3.5.4 /reserved-memory and UEFI" of the devicetree
specification v0.4-rc1).

RISCV_EFI_BOOT_PROTOCOL
~~~~~~~~~~~~~~~~~~~~~~~

When booting with UEFI, the EFI stub requires the boot hartid in order to pass
it to the RISC-V kernel in ``$a1``. The EFI stub retrieves the boot hartid using
one of the following methods:

- ``RISCV_EFI_BOOT_PROTOCOL`` (**preferred**).
- ``boot-hartid`` devicetree subnode (**deprecated**).

Any new firmware must implement ``RISCV_EFI_BOOT_PROTOCOL`` as the devicetree
based approach is deprecated now.

Early Boot Requirements and Constraints
=======================================

The RISC-V kernel's early boot process operates under the following constraints:

EFI stub and devicetree
-----------------------

When booting with UEFI, the devicetree is supplemented (or created) by the EFI
stub with the same parameters as arm64 which are described at the paragraph
"UEFI kernel support on ARM" in Documentation/arch/arm/uefi.rst.

Virtual mapping installation
----------------------------

The installation of the virtual mapping is done in 2 steps in the RISC-V kernel:

1. ``setup_vm()`` installs a temporary kernel mapping in ``early_pg_dir`` which
   allows discovery of the system memory. Only the kernel text/data are mapped
   at this point. When establishing this mapping, no allocation can be done
   (since the system memory is not known yet), so ``early_pg_dir`` page table is
   statically allocated (using only one table for each level).

2. ``setup_vm_final()`` creates the final kernel mapping in ``swapper_pg_dir``
   and takes advantage of the discovered system memory to create the linear
   mapping. When establishing this mapping, the kernel can allocate memory but
   cannot access it directly (since the direct mapping is not present yet), so
   it uses temporary mappings in the fixmap region to be able to access the
   newly allocated page table levels.

For ``virt_to_phys()`` and ``phys_to_virt()`` to be able to correctly convert
direct mapping addresses to physical addresses, they need to know the start of
the DRAM. This happens after step 1, right before step 2 installs the direct
mapping (see ``setup_bootmem()`` function in arch/riscv/mm/init.c). Any usage of
those macros before the final virtual mapping is installed must be carefully
examined.

Devicetree mapping via fixmap
-----------------------------

As the ``reserved_mem`` array is initialized with virtual addresses established
by ``setup_vm()``, and used with the mapping established by
``setup_vm_final()``, the RISC-V kernel uses the fixmap region to map the
devicetree. This ensures that the devicetree remains accessible by both virtual
mappings.

Pre-MMU execution
-----------------

A few pieces of code need to run before even the first virtual mapping is
established. These are the installation of the first virtual mapping itself,
patching of early alternatives and the early parsing of the kernel command line.
That code must be very carefully compiled as:

- ``-fno-pie``: This is needed for relocatable kernels which use ``-fPIE``,
  since otherwise, any access to a global symbol would go through the GOT which
  is only relocated virtually.
- ``-mcmodel=medany``: Any access to a global symbol must be PC-relative to
  avoid any relocations to happen before the MMU is setup.
- *all* instrumentation must also be disabled (that includes KASAN, ftrace and
  others).

As using a symbol from a different compilation unit requires this unit to be
compiled with those flags, we advise, as much as possible, not to use external
symbols.
