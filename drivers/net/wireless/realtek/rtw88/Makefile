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
	   efuse.o \
	   fw.o \
	   ps.o \
	   sec.o \
	   regd.o

rtw88-$(CONFIG_RTW88_8822BE)	+= rtw8822b.o rtw8822b_table.o
rtw88-$(CONFIG_RTW88_8822CE)	+= rtw8822c.o rtw8822c_table.o

obj-$(CONFIG_RTW88_PCI)		+= rtwpci.o
rtwpci-objs			:= pci.o
