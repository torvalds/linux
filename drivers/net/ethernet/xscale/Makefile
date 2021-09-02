# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Intel XScale IXP device drivers.
#

# Keep this link order to avoid deferred probing
ifdef CONFIG_PTP_1588_CLOCK_IXP46X
obj-$(CONFIG_IXP4XX_ETH)		+= ptp_ixp46x.o
endif

obj-$(CONFIG_IXP4XX_ETH)		+= ixp4xx_eth.o
