#
# OTG infrastructure and transceiver drivers
#

obj-$(CONFIG_USB_GPIO_VBUS)	+= gpio_vbus.o
obj-$(CONFIG_ISP1301_OMAP)	+= isp1301_omap.o

ifeq ($(CONFIG_USB_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
else ifeq ($(CONFIG_USB_GADGET_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

