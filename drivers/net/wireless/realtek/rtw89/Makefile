# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

obj-$(CONFIG_RTW89_CORE) += rtw89_core.o
rtw89_core-y += core.o \
		mac80211.o \
		mac.o \
		mac_be.o \
		phy.o \
		phy_be.o \
		fw.o \
		cam.o \
		efuse.o \
		efuse_be.o \
		regd.o \
		sar.o \
		coex.o \
		ps.o \
		chan.o \
		ser.o \
		acpi.o \
		util.o

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

obj-$(CONFIG_RTW89_8852B_COMMON) += rtw89_8852b_common.o
rtw89_8852b_common-objs := rtw8852b_common.o

obj-$(CONFIG_RTW89_8852B) += rtw89_8852b.o
rtw89_8852b-objs := rtw8852b.o \
		    rtw8852b_table.o \
		    rtw8852b_rfk.o \
		    rtw8852b_rfk_table.o

obj-$(CONFIG_RTW89_8852BE) += rtw89_8852be.o
rtw89_8852be-objs := rtw8852be.o

obj-$(CONFIG_RTW89_8852BT) += rtw89_8852bt.o
rtw89_8852bt-objs := rtw8852bt.o \
		    rtw8852bt_rfk.o \
		    rtw8852bt_rfk_table.o

obj-$(CONFIG_RTW89_8852BTE) += rtw89_8852bte.o
rtw89_8852bte-objs := rtw8852bte.o

obj-$(CONFIG_RTW89_8852C) += rtw89_8852c.o
rtw89_8852c-objs := rtw8852c.o \
		    rtw8852c_table.o \
		    rtw8852c_rfk.o \
		    rtw8852c_rfk_table.o

obj-$(CONFIG_RTW89_8852CE) += rtw89_8852ce.o
rtw89_8852ce-objs := rtw8852ce.o

obj-$(CONFIG_RTW89_8922A) += rtw89_8922a.o
rtw89_8922a-objs := rtw8922a.o \
		    rtw8922a_rfk.o

obj-$(CONFIG_RTW89_8922AE) += rtw89_8922ae.o
rtw89_8922ae-objs := rtw8922ae.o

rtw89_core-$(CONFIG_RTW89_DEBUG) += debug.o

obj-$(CONFIG_RTW89_PCI) += rtw89_pci.o
rtw89_pci-y := pci.o pci_be.o

