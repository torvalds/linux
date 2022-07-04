#------------------------------------------------------------------------------
# Copyright (c) 2004-2011 Atheros Communications Inc.
# Copyright (c) 2011-2012 Qualcomm Atheros, Inc.
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

obj-$(CONFIG_ATH6KL) += ath6kl_core.o
ath6kl_core-y += debug.o
ath6kl_core-y += hif.o
ath6kl_core-y += htc_mbox.o
ath6kl_core-y += htc_pipe.o
ath6kl_core-y += bmi.o
ath6kl_core-y += cfg80211.o
ath6kl_core-y += init.o
ath6kl_core-y += main.o
ath6kl_core-y += txrx.o
ath6kl_core-y += wmi.o
ath6kl_core-y += core.o
ath6kl_core-y += recovery.o

# FIXME: temporarily silence -Wdangling-pointer on non W=1+ builds
ifndef KBUILD_EXTRA_WARN
CFLAGS_htc_mbox.o += $(call cc-disable-warning, dangling-pointer)
endif

ath6kl_core-$(CONFIG_NL80211_TESTMODE) += testmode.o
ath6kl_core-$(CONFIG_ATH6KL_TRACING) += trace.o

obj-$(CONFIG_ATH6KL_SDIO) += ath6kl_sdio.o
ath6kl_sdio-y += sdio.o

obj-$(CONFIG_ATH6KL_USB) += ath6kl_usb.o
ath6kl_usb-y += usb.o

# for tracing framework to find trace.h
CFLAGS_trace.o := -I$(src)
