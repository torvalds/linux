#
# for USB OTG silicon based on Mentor Graphics INVENTRA designs
#

obj-$(CONFIG_USB_MUSB_HDRC) += musb_hdrc.o

musb_hdrc-y := musb_core.o

musb_hdrc-y					+= musb_gadget_ep0.o musb_gadget.o
musb_hdrc-y					+= musb_virthub.o musb_host.o
musb_hdrc-$(CONFIG_DEBUG_FS)			+= musb_debugfs.o

# Hardware Glue Layer
obj-$(CONFIG_USB_MUSB_OMAP2PLUS)		+= omap2430.o
obj-$(CONFIG_USB_MUSB_AM35X)			+= am35x.o
obj-$(CONFIG_USB_MUSB_DSPS)			+= musb_dsps.o
obj-$(CONFIG_USB_MUSB_TUSB6010)			+= tusb6010.o
obj-$(CONFIG_USB_MUSB_DAVINCI)			+= davinci.o
obj-$(CONFIG_USB_MUSB_DA8XX)			+= da8xx.o
obj-$(CONFIG_USB_MUSB_BLACKFIN)			+= blackfin.o
obj-$(CONFIG_USB_MUSB_UX500)			+= ux500.o

# the kconfig must guarantee that only one of the
# possible I/O schemes will be enabled at a time ...
# PIO only, or DMA (several potential schemes).
# though PIO is always there to back up DMA, and for ep0

musb_hdrc-$(CONFIG_USB_INVENTRA_DMA)		+= musbhsdma.o
musb_hdrc-$(CONFIG_USB_TI_CPPI_DMA)		+= cppi_dma.o
musb_hdrc-$(CONFIG_USB_TUSB_OMAP_DMA)		+= tusb6010_omap.o
musb_hdrc-$(CONFIG_USB_UX500_DMA)		+= ux500_dma.o
