#
# Makefile for the linux kernel.
#

# Common objects
obj-y				:= timer.o console.o

# CPU objects
obj-$(CONFIG_ARCH_SH7367)	+= setup-sh7367.o clock-sh7367.o intc-sh7367.o
obj-$(CONFIG_ARCH_SH7377)	+= setup-sh7377.o clock-sh7367.o intc-sh7377.o
obj-$(CONFIG_ARCH_SH7372)	+= setup-sh7372.o clock-sh7367.o intc-sh7372.o

# Pinmux setup
pfc-$(CONFIG_ARCH_SH7367)	:= pfc-sh7367.o
pfc-$(CONFIG_ARCH_SH7377)	:= pfc-sh7377.o
pfc-$(CONFIG_ARCH_SH7372)	:= pfc-sh7372.o
obj-$(CONFIG_GENERIC_GPIO)	+= $(pfc-y)

# Board objects
obj-$(CONFIG_MACH_G3EVM)	+= board-g3evm.o
obj-$(CONFIG_MACH_G4EVM)	+= board-g4evm.o
obj-$(CONFIG_MACH_AP4EVB)	+= board-ap4evb.o
