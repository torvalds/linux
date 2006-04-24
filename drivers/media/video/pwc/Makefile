pwc-objs	:= pwc-if.o pwc-misc.o pwc-ctrl.o pwc-v4l.o pwc-uncompress.o
pwc-objs	+= pwc-dec1.o pwc-dec23.o pwc-kiara.o pwc-timon.o

obj-$(CONFIG_USB_PWC) += pwc.o

ifeq ($(CONFIG_USB_PWC_DEBUG),y)
EXTRA_CFLAGS += -DCONFIG_PWC_DEBUG=1
else
EXTRA_CFLAGS += -DCONFIG_PWC_DEBUG=0
endif


