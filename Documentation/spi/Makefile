# List of programs to build
hostprogs-y := spidev_test spidev_fdx

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS_spidev_test.o += -I$(objtree)/usr/include
HOSTCFLAGS_spidev_fdx.o += -I$(objtree)/usr/include
