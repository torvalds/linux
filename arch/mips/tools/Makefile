# SPDX-License-Identifier: GPL-2.0
hostprogs-y := elf-entry
PHONY += elf-entry
elf-entry: $(obj)/elf-entry
	@:

hostprogs-$(CONFIG_CPU_LOONGSON3_WORKAROUNDS) += loongson3-llsc-check
PHONY += loongson3-llsc-check
loongson3-llsc-check: $(obj)/loongson3-llsc-check
	@:
