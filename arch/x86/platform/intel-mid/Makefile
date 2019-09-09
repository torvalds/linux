# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_X86_INTEL_MID) += intel-mid.o intel_mid_vrtc.o pwr.o

# SFI specific code
ifdef CONFIG_X86_INTEL_MID
obj-$(CONFIG_SFI) += sfi.o device_libs/
endif
