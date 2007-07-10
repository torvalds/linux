ifeq ($(CONFIG_PPC64),y)
EXTRA_CFLAGS			+= -mno-minimal-toc
endif

mpic-msi-obj-$(CONFIG_PCI_MSI)	+= mpic_msi.o mpic_u3msi.o
obj-$(CONFIG_MPIC)		+= mpic.o $(mpic-msi-obj-y)

obj-$(CONFIG_PPC_MPC106)	+= grackle.o
obj-$(CONFIG_PPC_DCR)		+= dcr.o
obj-$(CONFIG_PPC_DCR_NATIVE)	+= dcr-low.o
obj-$(CONFIG_PPC_PMI)		+= pmi.o
obj-$(CONFIG_U3_DART)		+= dart_iommu.o
obj-$(CONFIG_MMIO_NVRAM)	+= mmio_nvram.o
obj-$(CONFIG_FSL_SOC)		+= fsl_soc.o
obj-$(CONFIG_TSI108_BRIDGE)	+= tsi108_pci.o tsi108_dev.o
obj-$(CONFIG_QUICC_ENGINE)	+= qe_lib/
mv64x60-$(CONFIG_PCI)		+= mv64x60_pci.o
obj-$(CONFIG_MV64X60)		+= $(mv64x60-y) mv64x60_pic.o mv64x60_dev.o
obj-$(CONFIG_RTC_DRV_CMOS)	+= rtc_cmos_setup.o

# contains only the suspend handler for time
ifeq ($(CONFIG_RTC_CLASS),)
obj-$(CONFIG_PM)		+= timer.o
endif

ifeq ($(CONFIG_PPC_MERGE),y)
obj-$(CONFIG_PPC_INDIRECT_PCI)	+= indirect_pci.o
obj-$(CONFIG_PPC_I8259)		+= i8259.o
obj-$(CONFIG_PPC_83xx)		+= ipic.o
obj-$(CONFIG_4xx)		+= uic.o
endif

# Temporary hack until we have migrated to asm-powerpc
ifeq ($(ARCH),powerpc)
obj-$(CONFIG_CPM2)		+= cpm2_common.o cpm2_pic.o
obj-$(CONFIG_8xx)		+= mpc8xx_pic.o commproc.o
obj-$(CONFIG_UCODE_PATCH)	+= micropatch.o
endif
