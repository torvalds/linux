
ifeq ($(shell [[ `uname -r | cut -f 1,2 -d\.` < 2.6 ]] && echo pre2.6),pre2.6)
# system with a pre 2.6 kernel _don't_ use kbuild.
all:
	$(MAKE) -f Makefile.pre-2.6

clean:
	rm -f *.o

else
# systems with a 2.6 or later kernel use kbuild.
ifneq ($(KERNELRELEASE),)
obj-m	:= dt3155.o
dt3155-objs := dt3155_drv.o dt3155_isr.o dt3155_io.o allocator.o

else
KDIR	:= /lib/modules/$(shell uname -r)/build
PWD		:= $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	rm -rf *.o *.mod *.mod.c *.ko  .dt3155* .allocator.o.cmd  .tmp_versions

endif
endif
