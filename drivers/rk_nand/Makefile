# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_RK_NAND) += rknand.o
rknand-y := rk_ftl.o rk_nand_base.o rk_nand_blk.o
ifdef CONFIG_THUMB2_KERNEL
rk_ftl-$(CONFIG_THUMB2_KERNEL) += rk_ftl_arm_v7_thumb.o
else
rk_ftl-$(CONFIG_ARM64) += rk_zftl_arm64.o rk_ftlv5_arm64.o
rk_ftl-$(CONFIG_ARM) += rk_zftl_arm32.o rk_ftlv5_arm32.o
endif
