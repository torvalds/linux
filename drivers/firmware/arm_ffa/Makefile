# SPDX-License-Identifier: GPL-2.0-only
ffa-bus-y = bus.o
ffa-driver-y = driver.o
ffa-transport-$(CONFIG_ARM_FFA_SMCCC) += smccc.o
ffa-module-objs := $(ffa-bus-y) $(ffa-driver-y) $(ffa-transport-y)
obj-$(CONFIG_ARM_FFA_TRANSPORT) = ffa-module.o
