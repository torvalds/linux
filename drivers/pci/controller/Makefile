# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_PCIE_CADENCE) += cadence/
obj-$(CONFIG_PCI_FTPCI100) += pci-ftpci100.o
obj-$(CONFIG_PCI_IXP4XX) += pci-ixp4xx.o
obj-$(CONFIG_PCI_HYPERV) += pci-hyperv.o
obj-$(CONFIG_PCI_HYPERV_INTERFACE) += pci-hyperv-intf.o
obj-$(CONFIG_PCI_MVEBU) += pci-mvebu.o
obj-$(CONFIG_PCI_AARDVARK) += pci-aardvark.o
obj-$(CONFIG_PCI_TEGRA) += pci-tegra.o
obj-$(CONFIG_PCI_RCAR_GEN2) += pci-rcar-gen2.o
obj-$(CONFIG_PCIE_RCAR_HOST) += pcie-rcar.o pcie-rcar-host.o
obj-$(CONFIG_PCIE_RCAR_EP) += pcie-rcar.o pcie-rcar-ep.o
obj-$(CONFIG_PCI_HOST_COMMON) += pci-host-common.o
obj-$(CONFIG_PCI_HOST_GENERIC) += pci-host-generic.o
obj-$(CONFIG_PCI_HOST_THUNDER_ECAM) += pci-thunder-ecam.o
obj-$(CONFIG_PCI_HOST_THUNDER_PEM) += pci-thunder-pem.o
obj-$(CONFIG_PCIE_XILINX) += pcie-xilinx.o
obj-$(CONFIG_PCIE_XILINX_NWL) += pcie-xilinx-nwl.o
obj-$(CONFIG_PCIE_XILINX_CPM) += pcie-xilinx-cpm.o
obj-$(CONFIG_PCIE_XILINX_DMA_PL) += pcie-xilinx-dma-pl.o
obj-$(CONFIG_PCI_V3_SEMI) += pci-v3-semi.o
obj-$(CONFIG_PCI_XGENE) += pci-xgene.o
obj-$(CONFIG_PCI_XGENE_MSI) += pci-xgene-msi.o
obj-$(CONFIG_PCI_VERSATILE) += pci-versatile.o
obj-$(CONFIG_PCIE_IPROC) += pcie-iproc.o
obj-$(CONFIG_PCIE_IPROC_MSI) += pcie-iproc-msi.o
obj-$(CONFIG_PCIE_IPROC_PLATFORM) += pcie-iproc-platform.o
obj-$(CONFIG_PCIE_IPROC_BCMA) += pcie-iproc-bcma.o
obj-$(CONFIG_PCIE_ALTERA) += pcie-altera.o
obj-$(CONFIG_PCIE_ALTERA_MSI) += pcie-altera-msi.o
obj-$(CONFIG_PCIE_ROCKCHIP) += pcie-rockchip.o
obj-$(CONFIG_PCIE_ROCKCHIP_EP) += pcie-rockchip-ep.o
obj-$(CONFIG_PCIE_ROCKCHIP_HOST) += pcie-rockchip-host.o
obj-$(CONFIG_PCIE_MEDIATEK) += pcie-mediatek.o
obj-$(CONFIG_PCIE_MEDIATEK_GEN3) += pcie-mediatek-gen3.o
obj-$(CONFIG_VMD) += vmd.o
obj-$(CONFIG_PCIE_BRCMSTB) += pcie-brcmstb.o
obj-$(CONFIG_PCI_LOONGSON) += pci-loongson.o
obj-$(CONFIG_PCIE_HISI_ERR) += pcie-hisi-error.o
obj-$(CONFIG_PCIE_APPLE) += pcie-apple.o
obj-$(CONFIG_PCIE_MT7621) += pcie-mt7621.o

# pcie-hisi.o quirks are needed even without CONFIG_PCIE_DW
obj-y				+= dwc/
obj-y				+= mobiveil/
obj-y				+= plda/


# The following drivers are for devices that use the generic ACPI
# pci_root.c driver but don't support standard ECAM config access.
# They contain MCFG quirks to replace the generic ECAM accessors with
# device-specific ones that are shared with the DT driver.

# The ACPI driver is generic and should not require driver-specific
# config options to be enabled, so we always build these drivers on
# ARM64 and use internal ifdefs to only build the pieces we need
# depending on whether ACPI, the DT driver, or both are enabled.

ifdef CONFIG_ACPI
ifdef CONFIG_PCI_QUIRKS
obj-$(CONFIG_ARM64) += pci-thunder-ecam.o
obj-$(CONFIG_ARM64) += pci-thunder-pem.o
obj-$(CONFIG_ARM64) += pci-xgene.o
endif
endif
