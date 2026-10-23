# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_NET_DSA_REALTEK)		+= realtek_dsa.o
realtek_dsa-y				:= rtl83xx.o
realtek_dsa-$(CONFIG_NET_DSA_REALTEK_MDIO)	+= realtek-mdio.o
realtek_dsa-$(CONFIG_NET_DSA_REALTEK_SMI)	+= realtek-smi.o

obj-$(CONFIG_NET_DSA_REALTEK_RTL8366RB) += rtl8366.o
rtl8366-y				:= rtl8366-core.o rtl8366rb.o
rtl8366-$(CONFIG_NET_DSA_REALTEK_RTL8366RB_LEDS) += rtl8366rb-leds.o

obj-$(CONFIG_NET_DSA_REALTEK_RTL8365MB) += rtl8365mb.o
rtl8365mb-y				:= rtl8365mb_main.o \
					   rtl8365mb_table.o \
					   rtl8365mb_vlan.o \
					   rtl8365mb_l2.o
