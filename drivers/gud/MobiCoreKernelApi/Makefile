#
# this makefile is called from the kernel make syste
ifeq ($(MODE),release)
    ccflags-y += -O2 -DNDEBUG
else  # DEBUG
    # "-O" is needed to expand inlines
    ccflags-y += -O -g3 -DDEBUG
endif # DEBUG/RELEASE

ifdef MOBICORE_CFLAGS
    ccflags-y +=$(MOBICORE_CFLAGS)
endif

#Set the extra symbols
ifdef MCDRV_SYMBOLS_FILE
    KBUILD_EXTRA_SYMBOLS=$(MCDRV_SYMBOLS_FILE)
endif

ifeq ($(PLATFORM), ARM_VE_A9X4_QEMU)
	ccflags-y += -DMC_NETLINK_COMPAT
endif

ifeq ($(PLATFORM), MSM8974_SURF_STD)
	ccflags-y += -DMC_NETLINK_COMPAT_V37
endif

#EXTRA_CFLAGS += -DDEBUG -DDEBUG_VERBOSE
#EXTRA_CFLAGS += -Wno-declaration-after-statement
ccflags-y += -Wno-declaration-after-statement
# add our module to kernel.
obj-m += mcKernelApi.o

mcKernelApi-objs := main.o clientlib.o device.o session.o connection.o

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions \
		Module.markers Module.symvers modules.order

depend .depend dep:
	$(CC) $(CFLAGS) -M *.c > .depend

ifeq (.depend,$(wildcard .depend))
    include .depend
endif
