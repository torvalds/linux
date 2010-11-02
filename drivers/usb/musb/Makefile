#
# for USB OTG silicon based on Mentor Graphics INVENTRA designs
#

ccflags-$(CONFIG_USB_MUSB_DEBUG) := -DDEBUG

obj-$(CONFIG_USB_MUSB_HDRC) += musb_hdrc.o

musb_hdrc-y := musb_core.o

musb_hdrc-$(CONFIG_ARCH_DAVINCI_DMx)		+= davinci.o
musb_hdrc-$(CONFIG_ARCH_DAVINCI_DA8XX)		+= da8xx.o
musb_hdrc-$(CONFIG_USB_TUSB6010)		+= tusb6010.o
musb_hdrc-$(CONFIG_ARCH_OMAP2430)		+= omap2430.o
ifeq ($(CONFIG_USB_MUSB_AM35X),y)
	musb_hdrc-$(CONFIG_ARCH_OMAP3430)	+= am35x.o
else
	musb_hdrc-$(CONFIG_ARCH_OMAP3430)	+= omap2430.o
endif
musb_hdrc-$(CONFIG_ARCH_OMAP4)			+= omap2430.o
musb_hdrc-$(CONFIG_BF54x)			+= blackfin.o
musb_hdrc-$(CONFIG_BF52x)			+= blackfin.o
musb_hdrc-$(CONFIG_USB_GADGET_MUSB_HDRC)	+= musb_gadget_ep0.o musb_gadget.o
musb_hdrc-$(CONFIG_USB_MUSB_HDRC_HCD)		+= musb_virthub.o musb_host.o
musb_hdrc-$(CONFIG_DEBUG_FS)			+= musb_debugfs.o

# the kconfig must guarantee that only one of the
# possible I/O schemes will be enabled at a time ...
# PIO only, or DMA (several potential schemes).
# though PIO is always there to back up DMA, and for ep0

ifneq ($(CONFIG_MUSB_PIO_ONLY),y)

  ifeq ($(CONFIG_USB_INVENTRA_DMA),y)
    musb_hdrc-y			+= musbhsdma.o

  else
    ifeq ($(CONFIG_USB_TI_CPPI_DMA),y)
      musb_hdrc-y		+= cppi_dma.o

    else
      ifeq ($(CONFIG_USB_TUSB_OMAP_DMA),y)
	musb_hdrc-y		+= tusb6010_omap.o

      endif
    endif
  endif
endif
