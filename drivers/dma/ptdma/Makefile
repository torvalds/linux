# SPDX-License-Identifier: GPL-2.0-only
#
# AMD Passthru DMA driver
#

obj-$(CONFIG_AMD_PTDMA) += ptdma.o

ptdma-objs := ptdma-dev.o ptdma-dmaengine.o

ptdma-$(CONFIG_PCI) += ptdma-pci.o
