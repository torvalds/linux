ifneq ($(CONFIG_CONNECTOR),)
obj-m += cn_test.o
endif

# List of programs to build
hostprogs-y := ucon

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS_ucon.o += -I$(objtree)/usr/include

all: modules

modules clean:
	$(MAKE) -C ../.. SUBDIRS=$(PWD) $@
