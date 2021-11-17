# SPDX-License-Identifier: (GPL-2.0 OR MIT)
obj-$(CONFIG_MSCC_OCELOT_SWITCH_LIB) += mscc_ocelot_switch_lib.o
mscc_ocelot_switch_lib-y := \
	ocelot.o \
	ocelot_io.o \
	ocelot_police.o \
	ocelot_vcap.o \
	ocelot_flower.o \
	ocelot_ptp.o \
	ocelot_devlink.o
mscc_ocelot_switch_lib-$(CONFIG_BRIDGE_MRP) += ocelot_mrp.o
obj-$(CONFIG_MSCC_OCELOT_SWITCH) += mscc_ocelot.o
mscc_ocelot-y := \
	ocelot_vsc7514.o \
	ocelot_net.o
