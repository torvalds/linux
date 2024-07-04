.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

===========================================
Compute Express Link Subsystem Maturity Map
===========================================

The Linux CXL subsystem tracks the dynamic `CXL specification
<https://computeexpresslink.org/cxl-specification-landing-page>`_ that
continues to respond to new use cases with new features, capability
updates and fixes. At any given point some aspects of the subsystem are
more mature than others. While the periodic pull requests summarize the
`work being incorporated each merge window
<https://lore.kernel.org/linux-cxl/?q=s%3APULL+s%3ACXL+tc%3Atorvalds+NOT+s%3ARe>`_,
those do not always convey progress relative to a starting point and a
future end goal.

What follows is a coarse breakdown of the subsystem's major
responsibilities along with a maturity score. The expectation is that
the change-history of this document provides an overview summary of the
subsystem maturation over time.

The maturity scores are:

- [3] Mature: Work in this area is complete and no changes on the horizon.
  Note that this score can regress from one kernel release to the next
  based on new test results or end user reports.

- [2] Stabilizing: Major functionality operational, common cases are
  mature, but known corner cases are still a work in progress.

- [1] Initial: Capability that has exited the Proof of Concept phase, but
  may still have significant gaps to close and fixes to apply as real
  world testing occurs.

- [0] Known gap: Feature is on a medium to long term horizon to
  implement.  If the specification has a feature that does not even have
  a '0' score in this document, there is a good chance that no one in
  the linux-cxl@vger.kernel.org community has started to look at it.

- X: Out of scope for kernel enabling, or kernel enabling not required

Feature and Capabilities
========================

Enumeration / Provisioning
--------------------------
All of the fundamental enumeration an object model of the subsystem is
in place, but there are several corner cases that are pending closure.


* [2] CXL Window Enumeration

  * [0] :ref:`Extended-linear memory-side cache <extended-linear>`
  * [0] Low Memory-hole
  * [0] Hetero-interleave

* [2] Switch Enumeration

  * [0] CXL register enumeration link-up dependency

* [2] HDM Decoder Configuration

  * [0] Decoder target and granularity constraints

* [2] Performance enumeration

  * [3] Endpoint CDAT
  * [3] Switch CDAT
  * [1] CDAT to Core-mm integration

    * [1] x86
    * [0] Arm64
    * [0] All other arch.

  * [0] Shared link

* [2] Hotplug
  (see CXL Window Enumeration)

  * [0] Handle Soft Reserved conflicts

* [0] :ref:`RCH link status <rch-link-status>`
* [0] Fabrics / G-FAM (chapter 7)
* [0] Global Access Endpoint


RAS
---
In many ways CXL can be seen as a standardization of what would normally
be handled by custom EDAC drivers. The open development here is
mainly caused by the enumeration corner cases above.

* [3] Component events (OS)
* [2] Component events (FFM)
* [1] Endpoint protocol errors (OS)
* [1] Endpoint protocol errors (FFM)
* [0] Switch protocol errors (OS)
* [1] Switch protocol errors (FFM)
* [2] DPA->HPA Address translation

    * [1] XOR Interleave translation
      (see CXL Window Enumeration)

* [1] Memory Failure coordination
* [0] Scrub control
* [2] ACPI error injection EINJ

  * [0] EINJ v2
  * [X] Compliance DOE

* [2] Native error injection
* [3] RCH error handling
* [1] VH error handling
* [0] PPR
* [0] Sparing
* [0] Device built in test


Mailbox commands
----------------

* [3] Firmware update
* [3] Health / Alerts
* [1] :ref:`Background commands <background-commands>`
* [3] Sanitization
* [3] Security commands
* [3] RAW Command Debug Passthrough
* [0] CEL-only-validation Passthrough
* [0] Switch CCI
* [3] Timestamp
* [1] PMEM labels
* [0] PMEM GPF / Dirty Shutdown
* [0] Scan Media

PMU
---
* [1] Type 3 PMU
* [0] Switch USP/ DSP, Root Port

Security
--------

* [X] CXL Trusted Execution Environment Security Protocol (TSP)
* [X] CXL IDE (subsumed by TSP)

Memory-pooling
--------------

* [1] Hotplug of LDs (via PCI hotplug)
* [0] Dynamic Capacity Device (DCD) Support

Multi-host sharing
------------------

* [0] Hardware coherent shared memory
* [0] Software managed coherency shared memory

Multi-host memory
-----------------

* [0] Dynamic Capacity Device Support
* [0] Sharing

Accelerator
-----------

* [0] Accelerator memory enumeration HDM-D (CXL 1.1/2.0 Type-2)
* [0] Accelerator memory enumeration HDM-DB (CXL 3.0 Type-2)
* [0] CXL.cache 68b (CXL 2.0)
* [0] CXL.cache 256b Cache IDs (CXL 3.0)

User Flow Support
-----------------

* [0] HPA->DPA Address translation (need xormaps export solution)

Details
=======

.. _extended-linear:

* **Extended-linear memory-side cache**: An HMAT proposal to enumerate the presence of a
  memory-side cache where the cache capacity extends the SRAT address
  range capacity. `See the ECN
  <https://lore.kernel.org/linux-cxl/6650e4f835a0e_195e294a8@dwillia2-mobl3.amr.corp.intel.com.notmuch/>`_
  for more details:

.. _rch-link-status:

* **RCH Link Status**: RCH (Restricted CXL Host) topologies, end up
  hiding some standard registers like PCIe Link Status / Capabilities in
  the CXL RCRB (Root Complex Register Block).

.. _background-commands:

* **Background commands**: The CXL background command mechanism is
  awkward as the single slot is monopolized potentially indefinitely by
  various commands. A `cancel on conflict
  <http://lore.kernel.org/r/66035c2e8ba17_770232948b@dwillia2-xfh.jf.intel.com.notmuch>`_
  facility is needed to make sure the kernel can ensure forward progress
  of priority commands.
