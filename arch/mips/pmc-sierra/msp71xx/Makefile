#
# Makefile for the PMC-Sierra MSP SOCs
#
obj-y += msp_prom.o msp_setup.o msp_irq.o \
	 msp_time.o msp_serial.o msp_elb.o
obj-$(CONFIG_HAVE_GPIO_LIB) += gpio.o gpio_extended.o
obj-$(CONFIG_PMC_MSP7120_GW) += msp_hwbutton.o
obj-$(CONFIG_IRQ_MSP_SLP) += msp_irq_slp.o
obj-$(CONFIG_IRQ_MSP_CIC) += msp_irq_cic.o
obj-$(CONFIG_PCI) += msp_pci.o
obj-$(CONFIG_MSPETH) += msp_eth.o
obj-$(CONFIG_USB_MSP71XX) += msp_usb.o
