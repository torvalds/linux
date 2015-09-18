ifndef CROSS_COMPILE
# vdso_test won't build for glibc < 2.16, so disable it
# hostprogs-y := vdso_test
hostprogs-$(CONFIG_X86) := vdso_standalone_test_x86
vdso_standalone_test_x86-objs := vdso_standalone_test_x86.o parse_vdso.o
vdso_test-objs := parse_vdso.o vdso_test.o

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS := -I$(objtree)/usr/include -std=gnu99
HOSTCFLAGS_vdso_standalone_test_x86.o := -fno-asynchronous-unwind-tables -fno-stack-protector
HOSTLOADLIBES_vdso_standalone_test_x86 := -nostdlib
ifeq ($(CONFIG_X86_32),y)
HOSTLOADLIBES_vdso_standalone_test_x86 += -lgcc_s
endif
endif
