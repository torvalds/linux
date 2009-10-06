#
# Makefile of Intel Moorestown NAND controller driver
#

obj-$(CONFIG_MRST_NAND) += spectra.o
spectra-y := ffsport.o flash.o lld.o
spectra-$(CONFIG_MRST_NAND_HW) += lld_nand.o 
spectra-$(CONFIG_MRST_NAND_HW_DMA) += lld_cdma.o
spectra-$(CONFIG_MRST_NAND_EMU) += lld_emu.o

