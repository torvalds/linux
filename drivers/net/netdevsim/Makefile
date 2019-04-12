# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_NETDEVSIM) += netdevsim.o

netdevsim-objs := \
	netdev.o devlink.o fib.o sdev.o \

ifeq ($(CONFIG_BPF_SYSCALL),y)
netdevsim-objs += \
	bpf.o
endif

ifneq ($(CONFIG_XFRM_OFFLOAD),)
netdevsim-objs += ipsec.o
endif
