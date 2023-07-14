.. SPDX-License-Identifier: GPL-2.0

=======================================
Linux NVMe feature and and quirk policy
=======================================

This file explains the policy used to decide what is supported by the
Linux NVMe driver and what is not.


Introduction
============

NVM Express is an open collection of standards and information.

The Linux NVMe host driver in drivers/nvme/host/ supports devices
implementing the NVM Express (NVMe) family of specifications, which
currently consists of a number of documents:

 - the NVMe Base specification
 - various Command Set specifications (e.g. NVM Command Set)
 - various Transport specifications (e.g. PCIe, Fibre Channel, RDMA, TCP)
 - the NVMe Management Interface specification

See https://nvmexpress.org/developers/ for the NVMe specifications.


Supported features
==================

NVMe is a large suite of specifications, and contains features that are only
useful or suitable for specific use-cases. It is important to note that Linux
does not aim to implement every feature in the specification.  Every additional
feature implemented introduces more code, more maintenance and potentially more
bugs.  Hence there is an inherent tradeoff between functionality and
maintainability of the NVMe host driver.

Any feature implemented in the Linux NVMe host driver must support the
following requirements:

  1. The feature is specified in a release version of an official NVMe
     specification, or in a ratified Technical Proposal (TP) that is
     available on NVMe website. Or if it is not directly related to the
     on-wire protocol, does not contradict any of the NVMe specifications.
  2. Does not conflict with the Linux architecture, nor the design of the
     NVMe host driver.
  3. Has a clear, indisputable value-proposition and a wide consensus across
     the community.

Vendor specific extensions are generally not supported in the NVMe host
driver.

It is strongly recommended to work with the Linux NVMe and block layer
maintainers and get feedback on specification changes that are intended
to be used by the Linux NVMe host driver in order to avoid conflict at a
later stage.


Quirks
======

Sometimes implementations of open standards fail to correctly implement parts
of the standards.  Linux uses identifier-based quirks to work around such
implementation bugs.  The intent of quirks is to deal with widely available
hardware, usually consumer, which Linux users can't use without these quirks.
Typically these implementations are not or only superficially tested with Linux
by the hardware manufacturer.

The Linux NVMe maintainers decide ad hoc whether to quirk implementations
based on the impact of the problem to Linux users and how it impacts
maintainability of the driver.  In general quirks are a last resort, if no
firmware updates or other workarounds are available from the vendor.

Quirks will not be added to the Linux kernel for hardware that isn't available
on the mass market.  Hardware that fails qualification for enterprise Linux
distributions, ChromeOS, Android or other consumers of the Linux kernel
should be fixed before it is shipped instead of relying on Linux quirks.
