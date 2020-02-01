# SPDX-License-Identifier: GPL-2.0
#
# Makefile for PTP 1588 clock support.
#

ptp-y					:= ptp_clock.o ptp_chardev.o ptp_sysfs.o
obj-$(CONFIG_PTP_1588_CLOCK)		+= ptp.o
obj-$(CONFIG_PTP_1588_CLOCK_DTE)	+= ptp_dte.o
obj-$(CONFIG_PTP_1588_CLOCK_IXP46X)	+= ptp_ixp46x.o
obj-$(CONFIG_PTP_1588_CLOCK_PCH)	+= ptp_pch.o
obj-$(CONFIG_PTP_1588_CLOCK_KVM)	+= ptp_kvm.o
obj-$(CONFIG_PTP_1588_CLOCK_QORIQ)	+= ptp-qoriq.o
ptp-qoriq-y				+= ptp_qoriq.o
ptp-qoriq-$(CONFIG_DEBUG_FS)		+= ptp_qoriq_debugfs.o
obj-$(CONFIG_PTP_1588_CLOCK_IDTCM)	+= ptp_clockmatrix.o