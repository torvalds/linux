obj-$(CONFIG_RK_NAND) += rk_ftl.o rk_nand_base.o rk_nand_blk.o
ifdef CONFIG_THUMB2_KERNEL
rk_ftl-$(CONFIG_THUMB2_KERNEL) += rk_ftl_arm_v7_thumb.o
else
rk_ftl-$(CONFIG_ARM64) += rk_ftl_arm_v8.o
rk_ftl-$(CONFIG_ARM) += rk_ftl_arm_v7.o
endif
