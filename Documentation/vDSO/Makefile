# List of programs to build
hostprogs-$(CONFIG_X86) := vdso_test
vdso_test-objs := parse_vdso.o vdso_test.o

# Tell kbuild to always build the programs
##always := $(hostprogs-y)

HOSTCFLAGS := -I$(objtree)/usr/include -std=gnu99
HOSTLDFLAGS = -nostdlib -fno-asynchronous-unwind-tables
