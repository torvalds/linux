#
# Makefile for the linux kernel.
#

# Common objects
obj-y				:= timer.o

# CPU objects
obj-$(CONFIG_ARCH_SH7367)	+= setup-sh7367.o clock-sh7367.o
obj-$(CONFIG_ARCH_SH7377)	+= setup-sh7377.o clock-sh7367.o

# Board objects
obj-$(CONFIG_MACH_G3EVM)	+= board-g3evm.o
obj-$(CONFIG_MACH_G4EVM)	+= board-g4evm.o
