# SPDX-License-Identifier: GPL-2.0
plus_sec := $(call as-instr,.arch_extension sec,+sec)
AFLAGS_smc.o := -Wa,-march=armv7-a$(plus_sec)

obj-y += setup.o smc.o
obj-$(CONFIG_SMP) += platsmp.o
obj-$(CONFIG_SUSPEND) += pm.o
