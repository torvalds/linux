#
# Makefile fragment for Broadcom 802.11n Networking Device Driver
#
# Copyright (c) 2010 Broadcom Corporation
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
# OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
# CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

ccflags-y :=					\
	-DBCMDBG				\
	-DWLC_HIGH				\
	-DSTA					\
	-DWME					\
	-DWL11N					\
	-DDBAND					\
	-DBCMDMA32				\
	-DBCMNVRAMR				\
	-Idrivers/staging/brcm80211/sys		\
	-Idrivers/staging/brcm80211/phy		\
	-Idrivers/staging/brcm80211/util	\
	-Idrivers/staging/brcm80211/include

PCI_CFLAGS := -DWLC_LOW

BRCM80211_OFILES := \
	util/siutils.o \
	util/aiutils.o \
	util/bcmotp.o \
	util/bcmsrom.o \
	util/bcmutils.o \
	util/bcmwifi.o \
	util/hndpmu.o \
	util/linux_osl.o \
	sys/wlc_alloc.o \
	sys/wlc_antsel.o \
	sys/wlc_channel.o \
	sys/wlc_event.o \
	sys/wlc_mac80211.o \
	sys/wlc_rate.o \
	sys/wlc_stf.o \
	sys/wl_mac80211.o \
	sys/wlc_ampdu.o

PCIFILES := \
	phy/wlc_phy_cmn.o \
	phy/wlc_phy_lcn.o \
	phy/wlc_phy_n.o \
	phy/wlc_phytbl_lcn.o \
	phy/wlc_phytbl_n.o \
	sys/wlc_bmac.o \
	sys/wlc_phy_shim.o \
	sys/wl_ucode_loader.o \
	util/hnddma.o \
	util/nicpci.o \
	util/nvram/nvram_ro.o \
	util/qmath.o

MODULEPFX := brcm80211

# PCI driver
ifeq ($(CONFIG_BRCM80211_PCI),y)
obj-m	+= $(MODULEPFX).o
ccflags-y += $(PCI_CFLAGS)
$(MODULEPFX)-objs	= $(BRCM80211_OFILES) $(PCIFILES)
endif

obj-$(CONFIG_BRCMFMAC)    += brcmfmac/
