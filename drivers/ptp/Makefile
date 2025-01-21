# SPDX-License-Identifier: GPL-2.0
#
# Makefile for PTP 1588 clock support.
#

ptp-y					:= ptp_clock.o ptp_chardev.o ptp_sysfs.o ptp_vclock.o
ptp_kvm-$(CONFIG_X86)			:= ptp_kvm_x86.o ptp_kvm_common.o
ptp_kvm-$(CONFIG_HAVE_ARM_SMCCC)	:= ptp_kvm_arm.o ptp_kvm_common.o
obj-$(CONFIG_PTP_1588_CLOCK)		+= ptp.o
obj-$(CONFIG_PTP_1588_CLOCK_DTE)	+= ptp_dte.o
obj-$(CONFIG_PTP_1588_CLOCK_INES)	+= ptp_ines.o
obj-$(CONFIG_PTP_1588_CLOCK_PCH)	+= ptp_pch.o
obj-$(CONFIG_PTP_1588_CLOCK_KVM)	+= ptp_kvm.o
obj-$(CONFIG_PTP_1588_CLOCK_VMCLOCK)	+= ptp_vmclock.o
obj-$(CONFIG_PTP_1588_CLOCK_QORIQ)	+= ptp-qoriq.o
ptp-qoriq-y				+= ptp_qoriq.o
ptp-qoriq-$(CONFIG_DEBUG_FS)		+= ptp_qoriq_debugfs.o
obj-$(CONFIG_PTP_1588_CLOCK_IDTCM)	+= ptp_clockmatrix.o
obj-$(CONFIG_PTP_1588_CLOCK_FC3W)	+= ptp_fc3.o
obj-$(CONFIG_PTP_1588_CLOCK_IDT82P33)	+= ptp_idt82p33.o
obj-$(CONFIG_PTP_1588_CLOCK_MOCK)	+= ptp_mock.o
obj-$(CONFIG_PTP_1588_CLOCK_VMW)	+= ptp_vmw.o
obj-$(CONFIG_PTP_1588_CLOCK_OCP)	+= ptp_ocp.o
obj-$(CONFIG_PTP_DFL_TOD)		+= ptp_dfl_tod.o
obj-$(CONFIG_PTP_S390)			+= ptp_s390.o
