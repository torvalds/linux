#
# Makefile of Intel Moorestown NAND controller driver
#

obj-$(CONFIG_SPECTRA) += spectra.o
spectra-y := ffsport.o flash.o lld.o
spectra-$(CONFIG_SPECTRA_MRST_HW) += lld_nand.o 
spectra-$(CONFIG_SPECTRA_MRST_HW_DMA) += lld_cdma.o
spectra-$(CONFIG_SPECTRA_EMU) += lld_emu.o
spectra-$(CONFIG_SPECTRA_MTD) += lld_mtd.o

