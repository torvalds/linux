=================
x86 IOMMU Support
=================

The architecture specs can be obtained from the below locations.

- Intel: http://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/vt-directed-io-spec.pdf
- AMD: https://www.amd.com/system/files/TechDocs/48882_IOMMU.pdf

This guide gives a quick cheat sheet for some basic understanding.

Basic stuff
-----------

ACPI enumerates and lists the different IOMMUs on the platform, and
device scope relationships between devices and which IOMMU controls
them.

Some ACPI Keywords:

- DMAR - Intel DMA Remapping table
- DRHD - Intel DMA Remapping Hardware Unit Definition
- RMRR - Intel Reserved Memory Region Reporting Structure
- IVRS - AMD I/O Virtualization Reporting Structure
- IVDB - AMD I/O Virtualization Definition Block
- IVHD - AMD I/O Virtualization Hardware Definition

What is Intel RMRR?
^^^^^^^^^^^^^^^^^^^

There are some devices the BIOS controls, for e.g USB devices to perform
PS2 emulation. The regions of memory used for these devices are marked
reserved in the e820 map. When we turn on DMA translation, DMA to those
regions will fail. Hence BIOS uses RMRR to specify these regions along with
devices that need to access these regions. OS is expected to setup
unity mappings for these regions for these devices to access these regions.

What is AMD IVRS?
^^^^^^^^^^^^^^^^^

The architecture defines an ACPI-compatible data structure called an I/O
Virtualization Reporting Structure (IVRS) that is used to convey information
related to I/O virtualization to system software.  The IVRS describes the
configuration and capabilities of the IOMMUs contained in the platform as
well as information about the devices that each IOMMU virtualizes.

The IVRS provides information about the following:

- IOMMUs present in the platform including their capabilities and proper configuration
- System I/O topology relevant to each IOMMU
- Peripheral devices that cannot be otherwise enumerated
- Memory regions used by SMI/SMM, platform firmware, and platform hardware. These are generally exclusion ranges to be configured by system software.

How is an I/O Virtual Address (IOVA) generated?
-----------------------------------------------

Well behaved drivers call dma_map_*() calls before sending command to device
that needs to perform DMA. Once DMA is completed and mapping is no longer
required, driver performs dma_unmap_*() calls to unmap the region.

Intel Specific Notes
--------------------

Graphics Problems?
^^^^^^^^^^^^^^^^^^

If you encounter issues with graphics devices, you can try adding
option intel_iommu=igfx_off to turn off the integrated graphics engine.
If this fixes anything, please ensure you file a bug reporting the problem.

Some exceptions to IOVA
^^^^^^^^^^^^^^^^^^^^^^^

Interrupt ranges are not address translated, (0xfee00000 - 0xfeefffff).
The same is true for peer to peer transactions. Hence we reserve the
address from PCI MMIO ranges so they are not allocated for IOVA addresses.

AMD Specific Notes
------------------

Graphics Problems?
^^^^^^^^^^^^^^^^^^

If you encounter issues with integrated graphics devices, you can try adding
option iommu=pt to the kernel command line use a 1:1 mapping for the IOMMU.  If
this fixes anything, please ensure you file a bug reporting the problem.

Fault reporting
---------------
When errors are reported, the IOMMU signals via an interrupt. The fault
reason and device that caused it is printed on the console.


Kernel Log Samples
------------------

Intel Boot Messages
^^^^^^^^^^^^^^^^^^^

Something like this gets printed indicating presence of DMAR tables
in ACPI:

::

	ACPI: DMAR (v001 A M I  OEMDMAR  0x00000001 MSFT 0x00000097) @ 0x000000007f5b5ef0

When DMAR is being processed and initialized by ACPI, prints DMAR locations
and any RMRR's processed:

::

	ACPI DMAR:Host address width 36
	ACPI DMAR:DRHD (flags: 0x00000000)base: 0x00000000fed90000
	ACPI DMAR:DRHD (flags: 0x00000000)base: 0x00000000fed91000
	ACPI DMAR:DRHD (flags: 0x00000001)base: 0x00000000fed93000
	ACPI DMAR:RMRR base: 0x00000000000ed000 end: 0x00000000000effff
	ACPI DMAR:RMRR base: 0x000000007f600000 end: 0x000000007fffffff

When DMAR is enabled for use, you will notice:

::

	PCI-DMA: Using DMAR IOMMU

Intel Fault reporting
^^^^^^^^^^^^^^^^^^^^^

::

	DMAR:[DMA Write] Request device [00:02.0] fault addr 6df084000
	DMAR:[fault reason 05] PTE Write access is not set
	DMAR:[DMA Write] Request device [00:02.0] fault addr 6df084000
	DMAR:[fault reason 05] PTE Write access is not set

AMD Boot Messages
^^^^^^^^^^^^^^^^^

Something like this gets printed indicating presence of the IOMMU:

::

	iommu: Default domain type: Translated
	iommu: DMA domain TLB invalidation policy: lazy mode

AMD Fault reporting
^^^^^^^^^^^^^^^^^^^

::

	AMD-Vi: Event logged [IO_PAGE_FAULT domain=0x0007 address=0xffffc02000 flags=0x0000]
	AMD-Vi: Event logged [IO_PAGE_FAULT device=07:00.0 domain=0x0007 address=0xffffc02000 flags=0x0000]
