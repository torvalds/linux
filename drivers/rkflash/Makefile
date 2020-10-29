# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_RK_NANDC_NAND) += rkflash_blk.o rkflash_debug.o rksftl.o rknandc_base.o nand_boot.o flash.o nandc.o
obj-$(CONFIG_RK_SFC_NAND) += rkflash_blk.o rkflash_debug.o rksftl.o rksfc_base.o sfc_nand_boot.o sfc_nand.o sfc.o
obj-$(CONFIG_RK_SFC_NAND_MTD) += sfc_nand_mtd.o sfc_nand_mtd_bbt.o
obj-$(CONFIG_RK_SFC_NOR) += rkflash_blk.o rkflash_debug.o rksfc_base.o sfc_nor_boot.o sfc_nor.o sfc.o
obj-$(CONFIG_RK_SFC_NOR_MTD) += sfc_nor_mtd.o

ifdef CONFIG_THUMB2_KERNEL
rksftl-$(CONFIG_THUMB2_KERNEL) += rk_sftl_arm_v7_thumb.o
else
rksftl-$(CONFIG_ARM64) += rk_sftl_arm_v8.o
rksftl-$(CONFIG_ARM) += rk_sftl_arm_v7.o
endif
