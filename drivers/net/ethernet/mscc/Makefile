# SPDX-License-Identifier: (GPL-2.0 OR MIT)
obj-$(CONFIG_MSCC_OCELOT_SWITCH_LIB) += mscc_ocelot_switch_lib.o
mscc_ocelot_switch_lib-y := \
	ocelot.o \
	ocelot_devlink.o \
	ocelot_flower.o \
	ocelot_io.o \
	ocelot_mm.o \
	ocelot_police.o \
	ocelot_ptp.o \
	ocelot_stats.o \
	ocelot_vcap.o \
	vsc7514_regs.o
mscc_ocelot_switch_lib-$(CONFIG_BRIDGE_MRP) += ocelot_mrp.o
obj-$(CONFIG_MSCC_OCELOT_SWITCH) += mscc_ocelot.o
mscc_ocelot-y := \
	ocelot_fdma.o \
	ocelot_net.o \
	ocelot_vsc7514.o
