# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Renesas network device drivers
#

obj-$(CONFIG_SH_ETH) += sh_eth.o

ravb-objs := ravb_main.o ravb_ptp.o
obj-$(CONFIG_RAVB) += ravb.o

rswitch-objs := rswitch_main.o rswitch_l2.o
obj-$(CONFIG_RENESAS_ETHER_SWITCH) += rswitch.o

obj-$(CONFIG_RENESAS_GEN4_PTP) += rcar_gen4_ptp.o

obj-$(CONFIG_RTSN) += rtsn.o
