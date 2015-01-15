# List of programs to build
hostprogs-y := testptp

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS_testptp.o += -I$(objtree)/usr/include
HOSTLOADLIBES_testptp := -lrt
