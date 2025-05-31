# SPDX-License-Identifier: GPL-2.0
#
# Makefile for memory devices
#

obj-$(CONFIG_DDR)		+= jedec_ddr_data.o
ifeq ($(CONFIG_DDR),y)
obj-$(CONFIG_OF)		+= of_memory.o
endif
obj-$(CONFIG_ARM_PL172_MPMC)	+= pl172.o
obj-$(CONFIG_ATMEL_EBI)		+= atmel-ebi.o
obj-$(CONFIG_BRCMSTB_DPFE)	+= brcmstb_dpfe.o
obj-$(CONFIG_BRCMSTB_MEMC)	+= brcmstb_memc.o
obj-$(CONFIG_BT1_L2_CTL)	+= bt1-l2-ctl.o
obj-$(CONFIG_TI_AEMIF)		+= ti-aemif.o
obj-$(CONFIG_TI_EMIF)		+= emif.o
obj-$(CONFIG_OMAP_GPMC)		+= omap-gpmc.o
obj-$(CONFIG_FSL_CORENET_CF)	+= fsl-corenet-cf.o
obj-$(CONFIG_FSL_IFC)		+= fsl_ifc.o
obj-$(CONFIG_MVEBU_DEVBUS)	+= mvebu-devbus.o
obj-$(CONFIG_JZ4780_NEMC)	+= jz4780-nemc.o
obj-$(CONFIG_MTK_SMI)		+= mtk-smi.o
obj-$(CONFIG_DA8XX_DDRCTL)	+= da8xx-ddrctl.o
obj-$(CONFIG_PL353_SMC)		+= pl353-smc.o
obj-$(CONFIG_RENESAS_RPCIF)	+= renesas-rpc-if.o
obj-$(CONFIG_STM32_FMC2_EBI)	+= stm32-fmc2-ebi.o
obj-$(CONFIG_STM32_OMM)		+= stm32_omm.o

obj-$(CONFIG_SAMSUNG_MC)	+= samsung/
obj-$(CONFIG_TEGRA_MC)		+= tegra/
obj-$(CONFIG_TI_EMIF_SRAM)	+= ti-emif-sram.o
obj-$(CONFIG_FPGA_DFL_EMIF)	+= dfl-emif.o

ti-emif-sram-objs		:= ti-emif-pm.o ti-emif-sram-pm.o

$(obj)/ti-emif-sram-pm.o: $(obj)/ti-emif-asm-offsets.h

$(obj)/ti-emif-asm-offsets.h: $(obj)/emif-asm-offsets.s FORCE
	$(call filechk,offsets,__TI_EMIF_ASM_OFFSETS_H__)

targets += emif-asm-offsets.s
clean-files += ti-emif-asm-offsets.h
