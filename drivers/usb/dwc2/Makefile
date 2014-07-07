ccflags-$(CONFIG_USB_DWC2_DEBUG)	+= -DDEBUG
ccflags-$(CONFIG_USB_DWC2_VERBOSE)	+= -DVERBOSE_DEBUG

obj-$(CONFIG_USB_DWC2_HOST)		+= dwc2.o
dwc2-y					:= core.o core_intr.o
dwc2-y					+= hcd.o hcd_intr.o
dwc2-y					+= hcd_queue.o hcd_ddma.o

# NOTE: The previous s3c-hsotg peripheral mode only driver has been moved to
# this location and renamed gadget.c. When building for dynamically linked
# modules, dwc2_gadget.ko will get built for peripheral mode. For host mode,
# the core module will be dwc2.ko, the PCI bus interface module will called
# dwc2_pci.ko and the platform interface module will be called dwc2_platform.ko.
# At present the host and gadget driver will be separate drivers, but there
# are plans in the near future to create a dual-role driver.

ifneq ($(CONFIG_USB_DWC2_PCI),)
	obj-$(CONFIG_USB_DWC2_HOST)	+= dwc2_pci.o
	dwc2_pci-y			:= pci.o
endif

ifneq ($(CONFIG_USB_DWC2_PLATFORM),)
	obj-$(CONFIG_USB_DWC2_HOST)	+= dwc2_platform.o
	dwc2_platform-y			:= platform.o
endif

obj-$(CONFIG_USB_DWC2_PERIPHERAL)	+= dwc2_gadget.o
dwc2_gadget-y				:= gadget.o
