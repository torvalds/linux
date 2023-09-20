# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

obj-$(CONFIG_RTW89_CORE) += rtw89_core.o
rtw89_core-y += core.o \
		mac80211.o \
		mac.o \
		phy.o \
		fw.o \
		cam.o \
		efuse.o \
		regd.o \
		sar.o \
		coex.o \
		ps.o \
		chan.o \
		ser.o \
		acpi.o

rtw89_core-$(CONFIG_PM) += wow.o

obj-$(CONFIG_RTW89_8851B) += rtw89_8851b.o
rtw89_8851b-objs := rtw8851b.o \
		    rtw8851b_table.o \
		    rtw8851b_rfk.o \
		    rtw8851b_rfk_table.o

obj-$(CONFIG_RTW89_8851BE) += rtw89_8851be.o
rtw89_8851be-objs := rtw8851be.o

obj-$(CONFIG_RTW89_8852A) += rtw89_8852a.o
rtw89_8852a-objs := rtw8852a.o \
		    rtw8852a_table.o \
		    rtw8852a_rfk.o \
		    rtw8852a_rfk_table.o

obj-$(CONFIG_RTW89_8852AE) += rtw89_8852ae.o
rtw89_8852ae-objs := rtw8852ae.o

obj-$(CONFIG_RTW89_8852B) += rtw89_8852b.o
rtw89_8852b-objs := rtw8852b.o \
		    rtw8852b_table.o \
		    rtw8852b_rfk.o \
		    rtw8852b_rfk_table.o

obj-$(CONFIG_RTW89_8852BE) += rtw89_8852be.o
rtw89_8852be-objs := rtw8852be.o

obj-$(CONFIG_RTW89_8852C) += rtw89_8852c.o
rtw89_8852c-objs := rtw8852c.o \
		    rtw8852c_table.o \
		    rtw8852c_rfk.o \
		    rtw8852c_rfk_table.o

obj-$(CONFIG_RTW89_8852CE) += rtw89_8852ce.o
rtw89_8852ce-objs := rtw8852ce.o

rtw89_core-$(CONFIG_RTW89_DEBUG) += debug.o

obj-$(CONFIG_RTW89_PCI) += rtw89_pci.o
rtw89_pci-y := pci.o

