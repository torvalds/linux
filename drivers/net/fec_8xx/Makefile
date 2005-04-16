#
# Makefile for the Motorola 8xx FEC ethernet controller
#

obj-$(CONFIG_FEC_8XX) += fec_8xx.o

fec_8xx-objs := fec_main.o fec_mii.o

# the platform instantatiation objects
ifeq ($(CONFIG_NETTA),y)
fec_8xx-objs	+= fec_8xx-netta.o
endif
