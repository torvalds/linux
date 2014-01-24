ccflags-$(CONFIG_USB_DWC2_DEBUG)	+= -DDEBUG
ccflags-$(CONFIG_USB_DWC2_VERBOSE)	+= -DVERBOSE_DEBUG

obj-$(CONFIG_USB_DWC2)			+= dwc2.o

dwc2-y					+= core.o core_intr.o

# NOTE: This driver at present only implements the Host mode
# of the controller. The existing s3c-hsotg driver supports
# Peripheral mode, but only for the Samsung S3C platforms.
# There are plans to merge the s3c-hsotg driver with this
# driver in the near future to create a dual-role driver. Once
# that is done, Host mode will become an optional feature that
# is selected with a config option.

dwc2-y					+= hcd.o hcd_intr.o
dwc2-y					+= hcd_queue.o hcd_ddma.o

dwc2-y					+= s3c-hsotg.o

ifneq ($(CONFIG_PCI),)
	obj-$(CONFIG_USB_DWC2)		+= dwc2_pci.o
endif
obj-$(CONFIG_USB_DWC2)			+= dwc2_platform.o

dwc2_pci-y				+= pci.o
dwc2_platform-y				+= platform.o
