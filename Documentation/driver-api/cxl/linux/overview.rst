.. SPDX-License-Identifier: GPL-2.0

========
Overview
========

This section presents the configuration process of a CXL Type-3 memory device,
and how it is ultimately exposed to users as either a :code:`DAX` device or
normal memory pages via the kernel's page allocator.

Portions marked with a bullet are points at which certain kernel objects
are generated.

1) Early Boot

  a) BIOS, Build, and Boot Parameters

    i) EFI_MEMORY_SP
    ii) CONFIG_EFI_SOFT_RESERVE
    iii) CONFIG_MHP_DEFAULT_ONLINE_TYPE
    iv) nosoftreserve

  b) Memory Map Creation

    i) EFI Memory Map / E820 Consulted for Soft-Reserved

      * CXL Memory is set aside to be handled by the CXL driver

      * Soft-Reserved IO Resource created for CFMWS entry

  c) NUMA Node Creation

    * Nodes created from ACPI CEDT CFMWS and SRAT Proximity domains (PXM)

  d) Memory Tier Creation

    * A default memory_tier is created with all nodes.

  e) Contiguous Memory Allocation

    * Any requested CMA is allocated from Online nodes

  f) Init Finishes, Drivers start probing

2) ACPI and PCI Drivers

  a) Detects PCI device is CXL, marking it for probe by CXL driver

3) CXL Driver Operation

  a) Base device creation

    * root, port, and memdev devices created
    * CEDT CFMWS IO Resource creation

  b) Decoder creation

    * root, switch, and endpoint decoders created

  c) Logical device creation

    * memory_region and endpoint devices created

  d) Devices are associated with each other

    * If auto-decoder (BIOS-programmed decoders), driver validates
      configurations, builds associations, and locks configs at probe time.

    * If user-configured, validation and associations are built at
      decoder-commit time.

  e) Regions surfaced as DAX region

    * dax_region created

    * DAX device created via DAX driver

4) DAX Driver Operation

  a) DAX driver surfaces DAX region as one of two dax device modes

    * kmem - dax device is converted to hotplug memory blocks

      * DAX kmem IO Resource creation

    * hmem - dax device is left as daxdev to be accessed as a file.

      * If hmem, journey ends here.

  b) DAX kmem surfaces memory region to Memory Hotplug to add to page
     allocator as "driver managed memory"

5) Memory Hotplug

  a) mhp component surfaces a dax device memory region as multiple memory
     blocks to the page allocator

    * blocks appear in :code:`/sys/bus/memory/devices` and linked to a NUMA node

  b) blocks are onlined into the requested zone (NORMAL or MOVABLE)

    * Memory is marked "Driver Managed" to avoid kexec from using it as region
      for kernel updates
