# SPDX-License-Identifier: GPL-2.0
#
# Makefile for drivers/platform/x86/intel
# Intel x86 Platform-Specific Drivers
#

obj-$(CONFIG_INTEL_ATOMISP2_PDX86)	+= atomisp2/
obj-$(CONFIG_INTEL_IFS)			+= ifs/
obj-$(CONFIG_INTEL_SAR_INT1092)		+= int1092/
obj-$(CONFIG_INTEL_SKL_INT3472)		+= int3472/
obj-$(CONFIG_INTEL_PMC_CORE)		+= pmc/
obj-$(CONFIG_INTEL_PMT_CLASS)		+= pmt/
obj-$(CONFIG_INTEL_SPEED_SELECT_INTERFACE) += speed_select_if/
obj-$(CONFIG_INTEL_TELEMETRY)		+= telemetry/
obj-$(CONFIG_INTEL_WMI)			+= wmi/
obj-$(CONFIG_INTEL_UNCORE_FREQ_CONTROL)	+= uncore-frequency/


# Intel input drivers
intel-target-$(CONFIG_INTEL_HID_EVENT)		+= hid.o
intel-target-$(CONFIG_INTEL_VBTN)		+= vbtn.o

# Intel miscellaneous drivers
intel-target-$(CONFIG_INTEL_INT0002_VGPIO)	+= int0002_vgpio.o
intel-target-$(CONFIG_INTEL_ISHTP_ECLITE)	+= ishtp_eclite.o
intel-target-$(CONFIG_INTEL_OAKTRAIL)		+= oaktrail.o
intel-target-$(CONFIG_INTEL_SDSI)		+= sdsi.o
intel-target-$(CONFIG_INTEL_VSEC)		+= vsec.o

# Intel PMIC / PMC / P-Unit drivers
intel-target-$(CONFIG_INTEL_BYTCRC_PWRSRC)	+= bytcrc_pwrsrc.o
intel-target-$(CONFIG_INTEL_BXTWC_PMIC_TMU)	+= bxtwc_tmu.o
intel-target-$(CONFIG_INTEL_CHTDC_TI_PWRBTN)	+= chtdc_ti_pwrbtn.o
intel-target-$(CONFIG_INTEL_CHTWC_INT33FE)	+= chtwc_int33fe.o
intel-target-$(CONFIG_X86_ANDROID_TABLETS)	+= crystal_cove_charger.o
intel-target-$(CONFIG_INTEL_MRFLD_PWRBTN)	+= mrfld_pwrbtn.o
intel-target-$(CONFIG_INTEL_PUNIT_IPC)		+= punit_ipc.o

# TPMI drivers
intel-target-$(CONFIG_INTEL_PLR_TPMI)		+= plr_tpmi.o
intel-target-$(CONFIG_INTEL_TPMI_POWER_DOMAINS)	+= tpmi_power_domains.o
intel-target-$(CONFIG_INTEL_TPMI)		+= vsec_tpmi.o

# Intel Uncore drivers
intel-target-$(CONFIG_INTEL_RST)		+= rst.o
intel-target-$(CONFIG_INTEL_SMARTCONNECT)	+= smartconnect.o
intel-target-$(CONFIG_INTEL_TURBO_MAX_3)	+= turbo_max_3.o

# Add 'intel' prefix to each module listed in intel-target-*
define INTEL_OBJ_TARGET
intel-$(1)-y := $(1).o
obj-$(2) += intel-$(1).o
endef

$(foreach target, $(basename $(intel-target-y)), $(eval $(call INTEL_OBJ_TARGET,$(target),y)))
$(foreach target, $(basename $(intel-target-m)), $(eval $(call INTEL_OBJ_TARGET,$(target),m)))
