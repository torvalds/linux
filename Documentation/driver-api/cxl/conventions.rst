.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

=======================================
Compute Express Link: Linux Conventions
=======================================

There exists shipping platforms that bend or break CXL specification
expectations. Record the details and the rationale for those deviations.
Borrow the ACPI Code First template format to capture the assumptions
and tradeoffs such that multiple platform implementations can follow the
same convention.

<(template) Title>
==================

Document
--------
CXL Revision <rev>, Version <ver>

License
-------
SPDX-License Identifier: CC-BY-4.0

Creator/Contributors
--------------------

Summary of the Change
---------------------

<Detail the conflict with the specification and where available the
assumptions and tradeoffs taken by the hardware platform.>


Benefits of the Change
----------------------

<Detail what happens if platforms and Linux do not adopt this
convention.>

References
----------

Detailed Description of the Change
----------------------------------

<Propose spec language that corrects the conflict.>


Resolve conflict between CFMWS, Platform Memory Holes, and Endpoint Decoders
============================================================================

Document
--------

CXL Revision 3.2, Version 1.0

License
-------

SPDX-License Identifier: CC-BY-4.0

Creator/Contributors
--------------------

- Fabio M. De Francesco, Intel
- Dan J. Williams, Intel
- Mahesh Natu, Intel

Summary of the Change
---------------------

According to the current Compute Express Link (CXL) Specifications (Revision
3.2, Version 1.0), the CXL Fixed Memory Window Structure (CFMWS) describes zero
or more Host Physical Address (HPA) windows associated with each CXL Host
Bridge. Each window represents a contiguous HPA range that may be interleaved
across one or more targets, including CXL Host Bridges. Each window has a set
of restrictions that govern its usage. It is the Operating System-directed
configuration and Power Management (OSPM) responsibility to utilize each window
for the specified use.

Table 9-22 of the current CXL Specifications states that the Window Size field
contains the total number of consecutive bytes of HPA this window describes.
This value must be a multiple of the Number of Interleave Ways (NIW) * 256 MB.

Platform Firmware (BIOS) might reserve physical addresses below 4 GB where a
memory gap such as the Low Memory Hole for PCIe MMIO may exist. In such cases,
the CFMWS Range Size may not adhere to the NIW * 256 MB rule.

The HPA represents the actual physical memory address space that the CXL devices
can decode and respond to, while the System Physical Address (SPA), a related
but distinct concept, represents the system-visible address space that users can
direct transaction to and so it excludes reserved regions.

BIOS publishes CFMWS to communicate the active SPA ranges that, on platforms
with LMH's, map to a strict subset of the HPA. The SPA range trims out the hole,
resulting in lost capacity in the Endpoints with no SPA to map to that part of
the HPA range that intersects the hole.

E.g, an x86 platform with two CFMWS and an LMH starting at 2 GB:

 +--------+------------+-------------------+------------------+-------------------+------+
 | Window | CFMWS Base |    CFMWS Size     | HDM Decoder Base |  HDM Decoder Size | Ways |
 +========+============+===================+==================+===================+======+
 |   0    |   0 GB     |       2 GB        |      0 GB        |       3 GB        |  12  |
 +--------+------------+-------------------+------------------+-------------------+------+
 |   1    |   4 GB     | NIW*256MB Aligned |      4 GB        | NIW*256MB Aligned |  12  |
 +--------+------------+-------------------+------------------+-------------------+------+

HDM decoder base and HDM decoder size represent all the 12 Endpoint Decoders of
a 12 ways region and all the intermediate Switch Decoders. They are configured
by the BIOS according to the NIW * 256MB rule, resulting in a HPA range size of
3GB. Instead, the CFMWS Base and CFMWS Size are used to configure the Root
Decoder HPA range that results smaller (2GB) than that of the Switch and
Endpoint Decoders in the hierarchy (3GB).

This creates 2 issues which lead to a failure to construct a region:

1) A mismatch in region size between root and any HDM decoder. The root decoders
   will always be smaller due to the trim.

2) The trim causes the root decoder to violate the (NIW * 256MB) rule.

This change allows a region with a base address of 0GB to bypass these checks to
allow for region creation with the trimmed root decoder address range.

This change does not allow for any other arbitrary region to violate these
checks - it is intended exclusively to enable x86 platforms which map CXL memory
under 4GB.

Despite the HDM decoders covering the PCIE hole HPA region, it is expected that
the platform will never route address accesses to the CXL complex because the
root decoder only covers the trimmed region (which excludes this). This is
outside the ability of Linux to enforce.

On the example platform, only the first 2GB will be potentially usable, but
Linux, aiming to adhere to the current specifications, fails to construct
Regions and attach Endpoint and intermediate Switch Decoders to them.

There are several points of failure that due to the expectation that the Root
Decoder HPA size, that is equal to the CFMWS from which it is configured, has
to be greater or equal to the matching Switch and Endpoint HDM Decoders.

In order to succeed with construction and attachment, Linux must construct a
Region with Root Decoder HPA range size, and then attach to that all the
intermediate Switch Decoders and Endpoint Decoders that belong to the hierarchy
regardless of their range sizes.

Benefits of the Change
----------------------

Without the change, the OSPM wouldn't match intermediate Switch and Endpoint
Decoders with Root Decoders configured with CFMWS HPA sizes that don't align
with the NIW * 256MB constraint, and so it leads to lost memdev capacity.

This change allows the OSPM to construct Regions and attach intermediate Switch
and Endpoint Decoders to them, so that the addressable part of the memory
devices total capacity is made available to the users.

References
----------

Compute Express Link Specification Revision 3.2, Version 1.0
<https://www.computeexpresslink.org/>

Detailed Description of the Change
----------------------------------

The description of the Window Size field in table 9-22 needs to account for
platforms with Low Memory Holes, where SPA ranges might be subsets of the
endpoints HPA. Therefore, it has to be changed to the following:

"The total number of consecutive bytes of HPA this window represents. This value
shall be a multiple of NIW * 256 MB.

On platforms that reserve physical addresses below 4 GB, such as the Low Memory
Hole for PCIe MMIO on x86, an instance of CFMWS whose Base HPA range is 0 might
have a size that doesn't align with the NIW * 256 MB constraint.

Note that the matching intermediate Switch Decoders and the Endpoint Decoders
HPA range sizes must still align to the above-mentioned rule, but the memory
capacity that exceeds the CFMWS window size won't be accessible.".
