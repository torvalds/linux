# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Microchip lan969x network device drivers.
#

obj-$(CONFIG_SPARX5_SWITCH) += lan969x-switch.o

lan969x-switch-y := lan969x_regs.o lan969x.o lan969x_calendar.o \
 lan969x_vcap_ag_api.o lan969x_vcap_impl.o

# Provide include files
ccflags-y += -I$(srctree)/drivers/net/ethernet/microchip/fdma
ccflags-y += -I$(srctree)/drivers/net/ethernet/microchip/vcap
