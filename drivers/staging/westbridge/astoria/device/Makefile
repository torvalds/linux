#
# Makefile for the kernel westbridge device driver
#

ifneq ($(CONFIG_WESTBRIDGE_DEBUG),y)
	EXTRA_CFLAGS		+= -DWESTBRIDGE_NDEBUG
endif

obj-$(CONFIG_WESTBRIDGE_DEVICE_DRIVER)  += cyasdev.o


ifeq ($(CONFIG_MACH_OMAP3_WESTBRIDGE_AST_PNAND_HAL),y)
#moved for staging compatbility
#cyasdev-y		:= ../../../arch/arm/mach-omap2/cyashalomap_kernel.o cyasdevice.o
cyasdev-y		:= ../arch/arm/mach-omap2/cyashalomap_kernel.o cyasdevice.o \
			   ../api/src/cyasdma.o ../api/src/cyasintr.o ../api/src/cyaslep2pep.o \
			   ../api/src/cyaslowlevel.o ../api/src/cyasmisc.o ../api/src/cyasmtp.o \
			   ../api/src/cyasstorage.o ../api/src/cyasusb.o

else
# should not get here, need to be built with some hal
cyasdev-y		:= cyasdevice.o
endif
