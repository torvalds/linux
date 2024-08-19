# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_DTPM) += dtpm.o
obj-$(CONFIG_DTPM_CPU) += dtpm_cpu.o
obj-$(CONFIG_DTPM_DEVFREQ) += dtpm_devfreq.o
obj-$(CONFIG_POWERCAP)	+= powercap_sys.o
obj-$(CONFIG_INTEL_RAPL_CORE) += intel_rapl_common.o
obj-$(CONFIG_INTEL_RAPL) += intel_rapl_msr.o
obj-$(CONFIG_IDLE_INJECT) += idle_inject.o
obj-$(CONFIG_QCOM_EPM) += qti_epm_hardware.o
qti_epm_hardware-y	+= qti_epm_hw.o qti_epm_interface.o
obj-$(CONFIG_QCOM_POWER_TELEMETRY) += qcom_power_telemetry.o
qcom_power_telemetry-y	+= qti_power_telemetry.o qti_power_telemetry_interface.o
