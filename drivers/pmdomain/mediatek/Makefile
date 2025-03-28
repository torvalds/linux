# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_MTK_SCPSYS)		+= mtk-scpsys.o
obj-$(CONFIG_MTK_SCPSYS_PM_DOMAINS) 	+= mtk-pm-domains.o
obj-$(CONFIG_AIROHA_CPU_PM_DOMAIN) 	+= airoha-cpu-pmdomain.o

ifeq ($(CONFIG_THUMB2_KERNEL)$(CONFIG_CC_IS_CLANG),yy)
# The use of R7 in the SMCCC conflicts with the compiler's use of R7 as a frame
# pointer in Thumb2 mode, which is forcibly enabled by Clang when profiling
# hooks are inserted via the -pg switch.
CFLAGS_REMOVE_airoha-cpu-pmdomain.o += $(CC_FLAGS_FTRACE)
endif
