# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_NETDEVSIM) += netdevsim.o

netdevsim-objs := \
	netdev.o dev.o ethtool.o fib.o bus.o health.o udp_tunnels.o

ifeq ($(CONFIG_BPF_SYSCALL),y)
netdevsim-objs += \
	bpf.o
endif

ifneq ($(CONFIG_XFRM_OFFLOAD),)
netdevsim-objs += ipsec.o
endif
