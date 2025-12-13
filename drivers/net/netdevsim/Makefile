# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_NETDEVSIM) += netdevsim.o

netdevsim-objs := \
	netdev.o dev.o ethtool.o fib.o bus.o health.o hwstats.o udp_tunnels.o

ifeq ($(CONFIG_BPF_SYSCALL),y)
netdevsim-objs += \
	bpf.o
endif

ifneq ($(CONFIG_XFRM_OFFLOAD),)
netdevsim-objs += ipsec.o
endif

ifneq ($(CONFIG_PSAMPLE),)
netdevsim-objs += psample.o
endif

ifneq ($(CONFIG_INET_PSP),)
netdevsim-objs += psp.o
endif

ifneq ($(CONFIG_MACSEC),)
netdevsim-objs += macsec.o
endif
