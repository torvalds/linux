.. SPDX-License-Identifier: GPL-2.0

===========
ACPI Tables
===========

ACPI is the "Advanced Configuration and Power Interface", which is a standard
that defines how platforms and OS manage power and configure computer hardware.
For the purpose of this theory of operation, when referring to "ACPI" we will
usually refer to "ACPI Tables" - which are the way a platform (BIOS/EFI)
communicates static configuration information to the operation system.

The Following ACPI tables contain *static* configuration and performance data
about CXL devices.

.. toctree::
   :maxdepth: 1

   acpi/cedt.rst
   acpi/srat.rst
   acpi/hmat.rst
   acpi/slit.rst
   acpi/dsdt.rst

The SRAT table may also contain generic port/initiator content that is intended
to describe the generic port, but not information about the rest of the path to
the endpoint.

Linux uses these tables to configure kernel resources for statically configured
(by BIOS/EFI) CXL devices, such as:

- NUMA nodes
- Memory Tiers
- NUMA Abstract Distances
- SystemRAM Memory Regions
- Weighted Interleave Node Weights

ACPI Debugging
==============

The :code:`acpidump -b` command dumps the ACPI tables into binary format.

The :code:`iasl -d` command disassembles the files into human readable format.

Example :code:`acpidump -b && iasl -d cedt.dat` ::

   [000h 0000   4]   Signature : "CEDT"    [CXL Early Discovery Table]

Common Issues
-------------
Most failures described here result in a failure of the driver to surface
memory as a DAX device and/or kmem.

* CEDT CFMWS targets list UIDs do not match CEDT CHBS UIDs.
* CEDT CFMWS targets list UIDs do not match DSDT CXL Host Bridge UIDs.
* CEDT CFMWS Restriction Bits are not correct.
* CEDT CFMWS Memory regions are poorly aligned.
* CEDT CFMWS Memory regions spans a platform memory hole.
* CEDT CHBS UIDs do not match DSDT CXL Host Bridge UIDs.
* CEDT CHBS Specification version is incorrect.
* SRAT is missing regions described in CEDT CFMWS.

  * Result: failure to create a NUMA node for the region, or
    region is placed in wrong node.

* HMAT is missing data for regions described in CEDT CFMWS.

  * Result: NUMA node being placed in the wrong memory tier.

* SLIT has bad data.

  * Result: Lots of performance mechanisms in the kernel will be very unhappy.

All of these issues will appear to users as if the driver is failing to
support CXL - when in reality they are all the failure of a platform to
configure the ACPI tables correctly.
