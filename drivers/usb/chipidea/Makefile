obj-$(CONFIG_USB_CHIPIDEA)		+= ci_hdrc.o

ci_hdrc-y				:= core.o
ci_hdrc-$(CONFIG_USB_CHIPIDEA_UDC)	+= udc.o
ci_hdrc-$(CONFIG_USB_CHIPIDEA_DEBUG)	+= debug.o

ifneq ($(CONFIG_PCI),)
	obj-$(CONFIG_USB_CHIPIDEA)	+= ci13xxx_pci.o
endif

ifneq ($(CONFIG_ARCH_MSM),)
	obj-$(CONFIG_USB_CHIPIDEA)	+= ci13xxx_msm.o
endif
