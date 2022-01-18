# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

obj-$(CONFIG_RTW89_CORE) += rtw89_core.o
rtw89_core-y += core.o \
		mac80211.o \
		mac.o \
		phy.o \
		fw.o \
		rtw8852a.o \
		rtw8852a_table.o \
		rtw8852a_rfk.o \
		rtw8852a_rfk_table.o \
		cam.o \
		efuse.o \
		regd.o \
		sar.o \
		coex.o \
		ps.o \
		ser.o

rtw89_core-$(CONFIG_RTW89_DEBUG) += debug.o

obj-$(CONFIG_RTW89_PCI) += rtw89_pci.o
rtw89_pci-y := pci.o

