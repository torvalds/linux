# SPDX-License-Identifier: GPL-2.0-only
obj-y	= scmi-bus.o scmi-driver.o scmi-protocols.o scmi-transport.o
scmi-bus-y = bus.o
scmi-driver-y = driver.o
scmi-transport-y = mailbox.o shmem.o
scmi-protocols-y = base.o clock.o perf.o power.o reset.o sensors.o
obj-$(CONFIG_ARM_SCMI_POWER_DOMAIN) += scmi_pm_domain.o
