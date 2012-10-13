#
# OTG infrastructure and transceiver drivers
#
obj-$(CONFIG_USB_DWC_OTG)	+= dwc.o

dwc-objs := cil.o cil_intr.o param.o

ifeq ($(CONFIG_ARCH_SOCFPGA),y)
dwc-objs += platform.o
endif

ifeq ($(CONFIG_4xx_SOC),y)
dwc-objs += apmppc.o
endif

ifneq ($(CONFIG_DWC_DEVICE_ONLY),y)
dwc-objs += hcd.o hcd_intr.o \
		hcd_queue.o
endif

ifneq ($(CONFIG_DWC_HOST_ONLY),y)
dwc-objs += pcd.o pcd_intr.o
endif
