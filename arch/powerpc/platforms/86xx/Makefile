#
# Makefile for the PowerPC 86xx linux kernel.
#


ifeq ($(CONFIG_PPC_86xx),y)
obj-$(CONFIG_SMP)		+= mpc86xx_smp.o
endif
obj-$(CONFIG_MPC8641_HPCN)	+= mpc86xx_hpcn.o
obj-$(CONFIG_PCI)		+= pci.o mpc86xx_pcie.o
