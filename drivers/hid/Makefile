#
# Makefile for the HID driver
#

# Multipart objects.
hid-objs	:= hid-core.o hid-input.o

# Optional parts of multipart objects.

obj-$(CONFIG_HID)		+= hid.o

ifeq ($(CONFIG_INPUT_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

