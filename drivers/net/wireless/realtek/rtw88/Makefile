# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

obj-$(CONFIG_RTW88_CORE)	+= rtw88.o
rtw88-y += main.o \
	   mac80211.o \
	   util.o \
	   debug.o \
	   tx.o \
	   rx.o \
	   mac.o \
	   phy.o \
	   coex.o \
	   efuse.o \
	   fw.o \
	   ps.o \
	   sec.o \
	   bf.o \
	   wow.o \
	   regd.o

rtw88-$(CONFIG_RTW88_8723DE)	+= rtw8723d.o rtw8723d_table.o

obj-$(CONFIG_RTW88_8822B)	+= rtw88_8822b.o
rtw88_8822b-objs		:= rtw8822b.o rtw8822b_table.o

obj-$(CONFIG_RTW88_8822BE)	+= rtw88_8822be.o
rtw88_8822be-objs		:= rtw8822be.o

obj-$(CONFIG_RTW88_8822C)	+= rtw88_8822c.o
rtw88_8822c-objs		:= rtw8822c.o rtw8822c_table.o

obj-$(CONFIG_RTW88_8822CE)	+= rtw88_8822ce.o
rtw88_8822ce-objs		:= rtw8822ce.o

obj-$(CONFIG_RTW88_PCI)		+= rtwpci.o
rtwpci-objs			:= pci.o
