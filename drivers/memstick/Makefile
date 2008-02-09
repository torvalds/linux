#
# Makefile for the kernel MemoryStick device drivers.
#

ifeq ($(CONFIG_MEMSTICK_DEBUG),y)
	EXTRA_CFLAGS		+= -DDEBUG
endif

obj-$(CONFIG_MEMSTICK)		+= core/
obj-$(CONFIG_MEMSTICK)		+= host/

