#
# Makefile for the OneNAND MTD
#

# Core functionality.
obj-$(CONFIG_MTD_ONENAND)		+= onenand.o

# Board specific.
obj-$(CONFIG_MTD_ONENAND_GENERIC)	+= generic.o

# Simulator
obj-$(CONFIG_MTD_ONENAND_SIM)		+= onenand_sim.o

onenand-objs = onenand_base.o onenand_bbt.o
