# SPDX-License-Identifier: GPL-2.0
subdir-y += sifive
subdir-$(CONFIG_SOC_KENDRYTE) += kendryte

obj-$(CONFIG_BUILTIN_DTB) := $(addsuffix /, $(subdir-y))
