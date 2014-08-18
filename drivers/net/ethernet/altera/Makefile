#
# Makefile for the Altera device drivers.
#

obj-$(CONFIG_ALTERA_TSE) += altera_tse.o
altera_tse-objs := altera_tse_main.o altera_tse_ethtool.o \
altera_msgdma.o altera_sgdma.o altera_utils.o
ccflags-y += -D__CHECK_ENDIAN__
