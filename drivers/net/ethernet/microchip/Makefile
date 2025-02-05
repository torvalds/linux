# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Microchip network device drivers.
#

obj-$(CONFIG_ENC28J60) += enc28j60.o
obj-$(CONFIG_ENCX24J600) += encx24j600.o encx24j600-regmap.o
obj-$(CONFIG_LAN743X) += lan743x.o

lan743x-objs := lan743x_main.o lan743x_ethtool.o lan743x_ptp.o

obj-$(CONFIG_LAN865X) += lan865x/
obj-$(CONFIG_LAN966X_SWITCH) += lan966x/
obj-$(CONFIG_SPARX5_SWITCH) += sparx5/
obj-$(CONFIG_VCAP) += vcap/
obj-$(CONFIG_FDMA) += fdma/
