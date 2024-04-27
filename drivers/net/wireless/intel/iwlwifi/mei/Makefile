# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_IWLMEI)	+= iwlmei.o
iwlmei-y += main.o
iwlmei-y += net.o
iwlmei-$(CONFIG_IWLWIFI_DEVICE_TRACING) += trace.o
CFLAGS_trace.o := -I$(src)

ccflags-y += -I $(src)/../
