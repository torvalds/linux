# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_NETDEVSIM) += netdevsim.o

netdevsim-objs := \
	netdev.o \

ifeq ($(CONFIG_BPF_SYSCALL),y)
netdevsim-objs += \
	bpf.o
endif
