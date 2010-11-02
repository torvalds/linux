#
# Makefile for the kernel westbridge device drivers.
#

ifneq ($(CONFIG_WESTBRIDGE_DEBUG),y)
	EXTRA_CFLAGS		+= -WESTBRIDGE_NDEBUG
endif

obj-$(CONFIG_WESTBRIDGE)		+= device/
obj-$(CONFIG_WESTBRIDGE)		+= block/
obj-$(CONFIG_WESTBRIDGE)		+= gadget/