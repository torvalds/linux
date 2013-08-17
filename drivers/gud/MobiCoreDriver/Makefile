#
# this makefile is called from the kernel make system. Thus we basiclly
# add things to "obj-m" here.

ifeq ($(MODE),release)
    ccflags-y = -O2 -DNDEBUG
else
    ccflags-y = -DDEBUG
endif # DEBUG/RELEASE

# CFLAGS from the build script
ifdef MOBICORE_CFLAGS
	ccflags-y += $(MOBICORE_CFLAGS)
endif
#EXTRA_CFLAGS+=-DDEBUG_VERBOSE

ccflags-y += -include $(M)/build_tag.h -Wall -D__$(PLATFORM)__
# add our module to kernel.
obj-m += mcDrvModule.o

mcDrvModule-objs :=logging.o ops.o mem.o api.o pm.o main.o

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions \
		Module.markers Module.symvers modules.order

