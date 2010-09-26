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

ccflags-y :=			\
	-DLINUX			\
	-DBCMDRIVER		\
	-DBCMDONGLEHOST		\
	-DDHDTHREAD		\
	-DBCMWPA2		\
	-DUNRELEASEDCHIP	\
	-Dlinux			\
	-DDHD_SDALIGN=64	\
	-DMAX_HDR_READ=64	\
	-DDHD_FIRSTREAD=64	\
	-DDHD_GPL		\
	-DDHD_SCHED		\
	-DBDC			\
	-DTOE			\
	-DDHD_BCMEVENTS		\
	-DSHOW_EVENTS		\
	-DBCMSDIO		\
	-DDHD_GPL		\
	-DBCMLXSDMMC		\
	-DBCMPLATFORM_BUS	\
	-DOEM_CHROMIUMOS	\
	-DEMBEDDED_PLATFORM	\
	-DARP_OFFLOAD_SUPPORT	\
	-DPKT_FILTER_SUPPORT	\
	-DBRCM_FULLMAC		\
	-DCONFIG_CFG80211	\
	-DMMC_SDIO_ABORT	\
	-DDHD_DEBUG_TRAP	\
	-DBCMDBG		\
	-DDHD_DEBUG		\
	-Idrivers/staging/brcm80211/brcmfmac		\
	-Idrivers/staging/brcm80211/brcmfmac/include	\
	-Idrivers/staging/brcm80211/include		\
	-Idrivers/staging/brcm80211/util

DHDOFILES = dhd_linux.o linux_osl.o bcmutils.o dhd_common.o dhd_custom_gpio.o	\
	wl_iw.o wl_cfg80211.o ../util/siutils.o ../util/sbutils.o ../util/aiutils.o ../util/hndpmu.o bcmwifi.o dhd_sdio.o	\
	dhd_linux_sched.o dhd_cdc.o bcmsdh_sdmmc.o bcmsdh.o bcmsdh_linux.o	\
	bcmsdh_sdmmc_linux.o

#obj-$(CONFIG_BRCMFMAC) += brcmfmac.o
obj-m += brcmfmac.o
brcmfmac-objs += $(DHDOFILES)
EXTRA_LDFLAGS += --strip-debug
