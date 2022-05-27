# SPDX-License-Identifier: GPL-2.0

CFLAGS_core.o				:= -I$(src)
icc-core-objs				:= core.o bulk.o
icc-test-objs				:= debugfs-test.o

obj-$(CONFIG_INTERCONNECT)		+= icc-core.o
obj-$(CONFIG_INTERCONNECT_TEST)		+= icc-test.o
obj-$(CONFIG_INTERCONNECT_IMX)		+= imx/
obj-$(CONFIG_INTERCONNECT_QCOM)		+= qcom/
obj-$(CONFIG_INTERCONNECT_SAMSUNG)	+= samsung/
