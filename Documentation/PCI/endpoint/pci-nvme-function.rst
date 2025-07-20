.. SPDX-License-Identifier: GPL-2.0

=================
PCI NVMe Function
=================

:Author: Damien Le Moal <dlemoal@kernel.org>

The PCI NVMe endpoint function implements a PCI NVMe controller using the NVMe
subsystem target core code. The driver for this function resides with the NVMe
subsystem as drivers/nvme/target/pci-epf.c.

See Documentation/nvme/nvme-pci-endpoint-target.rst for more details.
