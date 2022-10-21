.. SPDX-License-Identifier: GPL-2.0

Acceptance criteria for vfio-pci device specific driver variants
================================================================

Overview
--------
The vfio-pci driver exists as a device agnostic driver using the
system IOMMU and relying on the robustness of platform fault
handling to provide isolated device access to userspace.  While the
vfio-pci driver does include some device specific support, further
extensions for yet more advanced device specific features are not
sustainable.  The vfio-pci driver has therefore split out
vfio-pci-core as a library that may be reused to implement features
requiring device specific knowledge, ex. saving and loading device
state for the purposes of supporting migration.

In support of such features, it's expected that some device specific
variants may interact with parent devices (ex. SR-IOV PF in support of
a user assigned VF) or other extensions that may not be otherwise
accessible via the vfio-pci base driver.  Authors of such drivers
should be diligent not to create exploitable interfaces via these
interactions or allow unchecked userspace data to have an effect
beyond the scope of the assigned device.

New driver submissions are therefore requested to have approval via
sign-off/ack/review/etc for any interactions with parent drivers.
Additionally, drivers should make an attempt to provide sufficient
documentation for reviewers to understand the device specific
extensions, for example in the case of migration data, how is the
device state composed and consumed, which portions are not otherwise
available to the user via vfio-pci, what safeguards exist to validate
the data, etc.  To that extent, authors should additionally expect to
require reviews from at least one of the listed reviewers, in addition
to the overall vfio maintainer.
