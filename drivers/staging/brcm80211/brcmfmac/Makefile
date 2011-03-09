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
	-DARP_OFFLOAD_SUPPORT	\
	-DBCMDBG		\
	-DBCMLXSDMMC		\
	-DBCMPLATFORM_BUS	\
	-DBCMSDIO		\
	-DBDC			\
	-DBRCM_FULLMAC		\
	-DDHD_DEBUG		\
	-DDHD_FIRSTREAD=64	\
	-DDHD_SCHED		\
	-DDHD_SDALIGN=64	\
	-DEMBEDDED_PLATFORM	\
	-DMAX_HDR_READ=64	\
	-DMMC_SDIO_ABORT	\
	-DPKT_FILTER_SUPPORT	\
	-DSHOW_EVENTS		\
	-DTOE			\
	-Idrivers/staging/brcm80211/brcmfmac		\
	-Idrivers/staging/brcm80211/include		\
	-Idrivers/staging/brcm80211/util

DHDOFILES = dhd_linux.o ../util/linux_osl.o ../util/bcmutils.o dhd_common.o dhd_custom_gpio.o	\
	wl_iw.o wl_cfg80211.o ../util/siutils.o ../util/sbutils.o ../util/aiutils.o ../util/hndpmu.o ../util/bcmwifi.o dhd_sdio.o	\
	dhd_linux_sched.o dhd_cdc.o bcmsdh_sdmmc.o bcmsdh.o bcmsdh_linux.o	\
	bcmsdh_sdmmc_linux.o

obj-m += brcmfmac.o
brcmfmac-objs += $(DHDOFILES)

