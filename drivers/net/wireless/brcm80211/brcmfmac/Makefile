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

ccflags-y += \
	-Idrivers/net/wireless/brcm80211/brcmfmac	\
	-Idrivers/net/wireless/brcm80211/include

ccflags-y += -D__CHECK_ENDIAN__

obj-$(CONFIG_BRCMFMAC) += brcmfmac.o
brcmfmac-objs += \
		wl_cfg80211.o \
		fwil.o \
		fweh.o \
		p2p.o \
		dhd_cdc.o \
		dhd_common.o \
		dhd_linux.o
brcmfmac-$(CONFIG_BRCMFMAC_SDIO) += \
		dhd_sdio.o \
		bcmsdh.o \
		bcmsdh_sdmmc.o \
		sdio_chip.o
brcmfmac-$(CONFIG_BRCMFMAC_USB) += \
		usb.o
brcmfmac-$(CONFIG_BRCMDBG) += \
		dhd_dbg.o
brcmfmac-$(CONFIG_BRCM_TRACING) += \
		tracepoint.o
