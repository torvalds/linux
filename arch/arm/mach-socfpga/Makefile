#
# Makefile for the linux kernel.
#

obj-y					:= socfpga.o dma.o
obj-$(CONFIG_SMP)	+= headsmp.o platsmp.o
obj-$(CONFIG_HW_PERF_EVENTS) += socfpga_cti.o
obj-$(CONFIG_FPGADMA)	+= fpga-dma.o
obj-$(CONFIG_EDAC_ALTERA_OCRAM_ECC) += ocram.o
obj-$(CONFIG_EDAC_ALTERA_L2_ECC) += l2_cache.o

