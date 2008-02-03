#
# Makefile for the SGI specific kernel interface routines
# under Linux.
#

obj-y	+= ip22-mc.o ip22-hpc.o ip22-int.o ip22-time.o ip22-nvram.o \
	   ip22-platform.o ip22-reset.o ip22-setup.o

obj-$(CONFIG_SGI_IP22) += ip22-berr.o
obj-$(CONFIG_SGI_IP28) += ip28-berr.o
obj-$(CONFIG_EISA)	+= ip22-eisa.o

# EXTRA_CFLAGS += -Werror
