#
# Makefile for the HID driver
#

# Multipart objects.
hid-objs	:= hid-core.o hid-input.o

# Optional parts of multipart objects.
ifeq ($(CONFIG_HID_DEBUG),y)
hid-objs	+= hid-debug.o
endif

ifeq ($(CONFIG_INPUT_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif


obj-$(CONFIG_HID)		+= hid.o

