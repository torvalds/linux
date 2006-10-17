#
# Makefile for the rest of the USB drivers
# (the ones that don't fit into any other categories)
#

obj-$(CONFIG_USB_ADUTUX)	+= adutux.o
obj-$(CONFIG_USB_AUERSWALD)	+= auerswald.o
obj-$(CONFIG_USB_CYPRESS_CY7C63)+= cypress_cy7c63.o
obj-$(CONFIG_USB_CYTHERM)	+= cytherm.o
obj-$(CONFIG_USB_EMI26)		+= emi26.o
obj-$(CONFIG_USB_EMI62)		+= emi62.o
obj-$(CONFIG_USB_FTDI_ELAN)	+= ftdi-elan.o
obj-$(CONFIG_USB_IDMOUSE)	+= idmouse.o
obj-$(CONFIG_USB_LCD)		+= usblcd.o
obj-$(CONFIG_USB_LD)		+= ldusb.o
obj-$(CONFIG_USB_LED)		+= usbled.o
obj-$(CONFIG_USB_LEGOTOWER)	+= legousbtower.o
obj-$(CONFIG_USB_PHIDGET)	+= phidget.o
obj-$(CONFIG_USB_PHIDGETKIT)	+= phidgetkit.o
obj-$(CONFIG_USB_PHIDGETMOTORCONTROL)	+= phidgetmotorcontrol.o
obj-$(CONFIG_USB_PHIDGETSERVO)	+= phidgetservo.o
obj-$(CONFIG_USB_RIO500)	+= rio500.o
obj-$(CONFIG_USB_TEST)		+= usbtest.o
obj-$(CONFIG_USB_TRANCEVIBRATOR)	+= trancevibrator.o
obj-$(CONFIG_USB_USS720)	+= uss720.o

obj-$(CONFIG_USB_SISUSBVGA)	+= sisusbvga/

ifeq ($(CONFIG_USB_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
