ccflags-$(CONFIG_USB_CHIPIDEA_DEBUG) := -DDEBUG

obj-$(CONFIG_USB_CHIPIDEA)		+= ci_hdrc.o

ci_hdrc-y				:= core.o
ci_hdrc-$(CONFIG_USB_CHIPIDEA_UDC)	+= udc.o
ci_hdrc-$(CONFIG_USB_CHIPIDEA_HOST)	+= host.o
ci_hdrc-$(CONFIG_USB_CHIPIDEA_DEBUG)	+= debug.o

# Glue/Bridge layers go here

obj-$(CONFIG_USB_CHIPIDEA)	+= ci13xxx_msm.o

# PCI doesn't provide stubs, need to check
ifneq ($(CONFIG_PCI),)
	obj-$(CONFIG_USB_CHIPIDEA)	+= ci13xxx_pci.o
endif

ifneq ($(CONFIG_OF_DEVICE),)
	obj-$(CONFIG_USB_CHIPIDEA)	+= ci13xxx_imx.o usbmisc_imx.o
endif
