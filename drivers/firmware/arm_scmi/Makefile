# SPDX-License-Identifier: GPL-2.0-only
scmi-bus-y = bus.o
scmi-driver-y = driver.o notify.o
scmi-transport-$(CONFIG_ARM_SCMI_HAVE_SHMEM) = shmem.o
scmi-transport-$(CONFIG_ARM_SCMI_TRANSPORT_MAILBOX) += mailbox.o
scmi-transport-$(CONFIG_ARM_SCMI_TRANSPORT_SMC) += smc.o
scmi-transport-$(CONFIG_ARM_SCMI_HAVE_MSG) += msg.o
scmi-transport-$(CONFIG_ARM_SCMI_TRANSPORT_VIRTIO) += virtio.o
scmi-transport-$(CONFIG_ARM_SCMI_TRANSPORT_OPTEE) += optee.o
scmi-protocols-y = base.o clock.o perf.o power.o reset.o sensors.o system.o voltage.o powercap.o
scmi-module-objs := $(scmi-bus-y) $(scmi-driver-y) $(scmi-protocols-y) \
		    $(scmi-transport-y)
obj-$(CONFIG_ARM_SCMI_PROTOCOL) += scmi-module.o
obj-$(CONFIG_ARM_SCMI_POWER_DOMAIN) += scmi_pm_domain.o
obj-$(CONFIG_ARM_SCMI_PERF_DOMAIN) += scmi_perf_domain.o
obj-$(CONFIG_ARM_SCMI_POWER_CONTROL) += scmi_power_control.o
obj-$(CONFIG_QTI_SCMI_PMU_PROTOCOL) += pmu_vendor.o
obj-$(CONFIG_QTI_SCMI_C1DCVS_PROTOCOL) += c1dcvs_vendor.o
obj-$(CONFIG_QTI_SCMI_VENDOR_PROTOCOL) += qcom_scmi_vendor.o
obj-$(CONFIG_QTI_SCMI_PLH_PROTOCOL) += plh_vendor.o

ifeq ($(CONFIG_THUMB2_KERNEL)$(CONFIG_CC_IS_CLANG),yy)
# The use of R7 in the SMCCC conflicts with the compiler's use of R7 as a frame
# pointer in Thumb2 mode, which is forcibly enabled by Clang when profiling
# hooks are inserted via the -pg switch.
CFLAGS_REMOVE_smc.o += $(CC_FLAGS_FTRACE)
endif
