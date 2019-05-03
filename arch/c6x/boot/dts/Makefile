# SPDX-License-Identifier: GPL-2.0
#
# Makefile for device trees
#

DTC_FLAGS ?= -p 1024

dtb-$(CONFIG_SOC_TMS320C6455) += dsk6455.dtb
dtb-$(CONFIG_SOC_TMS320C6457) += evmc6457.dtb
dtb-$(CONFIG_SOC_TMS320C6472) += evmc6472.dtb
dtb-$(CONFIG_SOC_TMS320C6474) += evmc6474.dtb
dtb-$(CONFIG_SOC_TMS320C6678) += evmc6678.dtb

ifneq ($(DTB),)
obj-y += $(DTB).dtb.o
endif
