#
# Makefile for the kernel westbridge hal
#

ifneq ($(CONFIG_WESTBRIDGE_DEBUG),y)
	EXTRA_CFLAGS		+= -DWESTBRIDGE_NDEBUG
endif

obj-$(CONFIG_WESTBRIDGE_GADGET_DRIVER)  += cyasgadgetctrl.o
cyasgadgetctrl-y		:= cyasgadget.o

