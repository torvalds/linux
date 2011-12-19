#------------------------------------------------------------------------------
# Copyright (c) 2004-2010 Atheros Communications Inc.
# All rights reserved.
#
#
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
#
#
# Author(s): ="Atheros"
#------------------------------------------------------------------------------

obj-$(CONFIG_ATH6KL_SDIO) := ath6kl_sdio.o
ath6kl_sdio-y += debug.o
ath6kl_sdio-y += hif.o
ath6kl_sdio-y += htc.o
ath6kl_sdio-y += bmi.o
ath6kl_sdio-y += cfg80211.o
ath6kl_sdio-y += init.o
ath6kl_sdio-y += main.o
ath6kl_sdio-y += txrx.o
ath6kl_sdio-y += wmi.o
ath6kl_sdio-y += sdio.o
ath6kl_sdio-$(CONFIG_NL80211_TESTMODE) += testmode.o

obj-$(CONFIG_ATH6KL_USB) += ath6kl_usb.o
ath6kl_usb-y += debug.o
ath6kl_usb-y += hif.o
ath6kl_usb-y += htc.o
ath6kl_usb-y += bmi.o
ath6kl_usb-y += cfg80211.o
ath6kl_usb-y += init.o
ath6kl_usb-y += main.o
ath6kl_usb-y += txrx.o
ath6kl_usb-y += wmi.o
ath6kl_usb-y += usb.o
ath6kl_usb-$(CONFIG_NL80211_TESTMODE) += testmode.o

ccflags-y += -D__CHECK_ENDIAN__
