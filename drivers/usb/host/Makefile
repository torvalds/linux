# SPDX-License-Identifier: GPL-2.0
#
# Makefile for USB Host Controller Drivers
#

# tell define_trace.h where to find the xhci trace header
CFLAGS_xhci-trace.o := -I$(src)

fhci-y := fhci-hcd.o fhci-hub.o fhci-q.o
fhci-y += fhci-mem.o fhci-tds.o fhci-sched.o

fhci-$(CONFIG_FHCI_DEBUG) += fhci-dbg.o

xhci-hcd-y := xhci.o xhci-mem.o xhci-ext-caps.o
xhci-hcd-y += xhci-ring.o xhci-hub.o xhci-dbg.o
xhci-hcd-y += xhci-trace.o

ifneq ($(CONFIG_USB_XHCI_DBGCAP), )
	xhci-hcd-y += xhci-dbgcap.o xhci-dbgtty.o
endif

xhci-mtk-hcd-y := xhci-mtk.o xhci-mtk-sch.o

xhci-plat-hcd-y := xhci-plat.o
ifneq ($(CONFIG_USB_XHCI_MVEBU), )
	xhci-plat-hcd-y		+= xhci-mvebu.o
endif
ifneq ($(CONFIG_USB_XHCI_RCAR), )
	xhci-plat-hcd-y		+= xhci-rcar.o
endif

ifneq ($(CONFIG_DEBUG_FS),)
	xhci-hcd-y		+= xhci-debugfs.o
endif

obj-$(CONFIG_USB_PCI)	+= pci-quirks.o

obj-$(CONFIG_USB_EHCI_HCD)	+= ehci-hcd.o
obj-$(CONFIG_USB_EHCI_PCI)	+= ehci-pci.o
obj-$(CONFIG_USB_EHCI_HCD_PLATFORM)	+= ehci-platform.o
obj-$(CONFIG_USB_EHCI_HCD_NPCM7XX)	+= ehci-npcm7xx.o
obj-$(CONFIG_USB_EHCI_HCD_OMAP)	+= ehci-omap.o
obj-$(CONFIG_USB_EHCI_HCD_ORION)	+= ehci-orion.o
obj-$(CONFIG_USB_EHCI_HCD_SPEAR)	+= ehci-spear.o
obj-$(CONFIG_USB_EHCI_HCD_STI)	+= ehci-st.o
obj-$(CONFIG_USB_EHCI_EXYNOS)	+= ehci-exynos.o
obj-$(CONFIG_USB_EHCI_HCD_AT91) += ehci-atmel.o
obj-$(CONFIG_USB_EHCI_BRCMSTB)	+= ehci-brcm.o

obj-$(CONFIG_USB_OXU210HP_HCD)	+= oxu210hp-hcd.o
obj-$(CONFIG_USB_ISP116X_HCD)	+= isp116x-hcd.o
obj-$(CONFIG_USB_ISP1362_HCD)	+= isp1362-hcd.o

obj-$(CONFIG_USB_OHCI_HCD)	+= ohci-hcd.o
obj-$(CONFIG_USB_OHCI_HCD_PCI)	+= ohci-pci.o
obj-$(CONFIG_USB_OHCI_HCD_PLATFORM)	+= ohci-platform.o
obj-$(CONFIG_USB_OHCI_EXYNOS)	+= ohci-exynos.o
obj-$(CONFIG_USB_OHCI_HCD_OMAP1)	+= ohci-omap.o
obj-$(CONFIG_USB_OHCI_HCD_SPEAR)	+= ohci-spear.o
obj-$(CONFIG_USB_OHCI_HCD_STI)	+= ohci-st.o
obj-$(CONFIG_USB_OHCI_HCD_AT91)	+= ohci-at91.o
obj-$(CONFIG_USB_OHCI_HCD_S3C2410)	+= ohci-s3c2410.o
obj-$(CONFIG_USB_OHCI_HCD_LPC32XX)	+= ohci-nxp.o
obj-$(CONFIG_USB_OHCI_HCD_PXA27X)	+= ohci-pxa27x.o
obj-$(CONFIG_USB_OHCI_HCD_DAVINCI)	+= ohci-da8xx.o
obj-$(CONFIG_USB_OCTEON_HCD)	+= octeon-hcd.o

obj-$(CONFIG_USB_UHCI_HCD)	+= uhci-hcd.o
obj-$(CONFIG_USB_FHCI_HCD)	+= fhci.o
obj-$(CONFIG_USB_XHCI_HCD)	+= xhci-hcd.o
obj-$(CONFIG_USB_XHCI_PCI)	+= xhci-pci.o
obj-$(CONFIG_USB_XHCI_PCI_RENESAS)	+= xhci-pci-renesas.o
obj-$(CONFIG_USB_XHCI_PLATFORM) += xhci-plat-hcd.o
obj-$(CONFIG_USB_XHCI_HISTB)	+= xhci-histb.o
obj-$(CONFIG_USB_XHCI_MTK)	+= xhci-mtk-hcd.o
obj-$(CONFIG_USB_XHCI_TEGRA)	+= xhci-tegra.o
obj-$(CONFIG_USB_SL811_HCD)	+= sl811-hcd.o
obj-$(CONFIG_USB_SL811_CS)	+= sl811_cs.o
obj-$(CONFIG_USB_U132_HCD)	+= u132-hcd.o
obj-$(CONFIG_USB_R8A66597_HCD)	+= r8a66597-hcd.o
obj-$(CONFIG_USB_FSL_USB2)	+= fsl-mph-dr-of.o
obj-$(CONFIG_USB_EHCI_FSL)	+= fsl-mph-dr-of.o
obj-$(CONFIG_USB_EHCI_FSL)	+= ehci-fsl.o
obj-$(CONFIG_USB_EHCI_MV)	+= ehci-mv.o
obj-$(CONFIG_USB_HCD_BCMA)	+= bcma-hcd.o
obj-$(CONFIG_USB_HCD_SSB)	+= ssb-hcd.o
obj-$(CONFIG_USB_MAX3421_HCD)	+= max3421-hcd.o
obj-$(CONFIG_USB_XEN_HCD)	+= xen-hcd.o
