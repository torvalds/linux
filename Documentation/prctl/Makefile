ifndef CROSS_COMPILE
# List of programs to build
hostprogs-$(CONFIG_X86) := disable-tsc-ctxt-sw-stress-test disable-tsc-on-off-stress-test disable-tsc-test
# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS_disable-tsc-ctxt-sw-stress-test.o += -I$(objtree)/usr/include
HOSTCFLAGS_disable-tsc-on-off-stress-test.o += -I$(objtree)/usr/include
HOSTCFLAGS_disable-tsc-test.o += -I$(objtree)/usr/include
endif
