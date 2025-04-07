# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_NET_DSA_REALTEK)		+= realtek_dsa.o
realtek_dsa-objs			:= rtl83xx.o

ifdef CONFIG_NET_DSA_REALTEK_MDIO
realtek_dsa-objs += realtek-mdio.o
endif

ifdef CONFIG_NET_DSA_REALTEK_SMI
realtek_dsa-objs += realtek-smi.o
endif

obj-$(CONFIG_NET_DSA_REALTEK_RTL8366RB) += rtl8366.o
rtl8366-objs 				:= rtl8366-core.o rtl8366rb.o
ifdef CONFIG_NET_DSA_REALTEK_RTL8366RB_LEDS
rtl8366-objs 				+= rtl8366rb-leds.o
endif
obj-$(CONFIG_NET_DSA_REALTEK_RTL8365MB) += rtl8365mb.o
