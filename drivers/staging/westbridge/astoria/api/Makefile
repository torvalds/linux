#
# Makefile for the kernel westbridge core.
#

ifeq ($(CONFIG_WESTBRIDGE_DEBUG),n)
	EXTRA_CFLAGS		+= -NDEBUG
endif

obj-$(CONFIG_WESTBRIDGE_DEVICE_DRIVER)		+= cyasapi.o
cyasapi-y			:= src/cyasdma.o src/cyasintr.o src/cyaslep2pep.o \
				   src/cyaslowlevel.o src/cyasmisc.o src/cyasmtp.o \
				   src/cyasstorage.o src/cyasusb.o


