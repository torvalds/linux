# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Microchip Lan966x network device drivers.
#

obj-$(CONFIG_LAN966X_SWITCH) += lan966x-switch.o

lan966x-switch-objs  := lan966x_main.o lan966x_phylink.o lan966x_port.o \
			lan966x_mac.o lan966x_ethtool.o lan966x_switchdev.o \
			lan966x_vlan.o lan966x_fdb.o lan966x_mdb.o \
			lan966x_ptp.o lan966x_fdma.o lan966x_lag.o \
			lan966x_tc.o lan966x_mqprio.o lan966x_taprio.o \
			lan966x_tbf.o lan966x_cbs.o lan966x_ets.o \
			lan966x_tc_matchall.o lan966x_police.o lan966x_mirror.o \
			lan966x_xdp.o lan966x_vcap_impl.o lan966x_vcap_ag_api.o \
			lan966x_tc_flower.o lan966x_goto.o

lan966x-switch-$(CONFIG_LAN966X_DCB) += lan966x_dcb.o
lan966x-switch-$(CONFIG_DEBUG_FS) += lan966x_vcap_debugfs.o

# Provide include files
ccflags-y += -I$(srctree)/drivers/net/ethernet/microchip/vcap
