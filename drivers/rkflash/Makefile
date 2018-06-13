# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_RK_NANDC_NAND) += rkflash_blk.o rknandc_base.o rkflash_debug.o nand_boot.o  flash.o nandc.o ftl_flash_plat.o
obj-$(CONFIG_RK_SFC_NAND) += rkflash_blk.o rksfc_base.o rkflash_debug.o sfc_nand_boot.o sfc_nand.o sfc.o ftl_flash_plat.o
obj-$(CONFIG_RK_SFC_NOR) += rkflash_blk.o rksfc_base.o rkflash_debug.o sfc_nor_boot.o sfc_nor.o sfc.o
obj-$(CONFIG_RK_SFC_NOR_MTD) += sfc_nor_mtd.o

ifdef CONFIG_THUMB2_KERNEL
obj-$(CONFIG_THUMB2_KERNEL) += rk_sftl_arm_v7_thumb.o
else
obj-$(CONFIG_ARM64) += rk_sftl_arm_v8.o
obj-$(CONFIG_ARM) += rk_sftl_arm_v7.o
endif
