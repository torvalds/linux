# SPDX-License-Identifier: GPL-2.0
#
#  Makefile for the Linux Controller Area Network drivers.
#

obj-$(CONFIG_CAN_VCAN)		+= vcan.o
obj-$(CONFIG_CAN_VXCAN)		+= vxcan.o
obj-$(CONFIG_CAN_SLCAN)		+= slcan.o

obj-y				+= dev/
obj-y				+= rcar/
obj-y				+= spi/
obj-y				+= usb/
obj-y				+= softing/

obj-$(CONFIG_CAN_AT91)		+= at91_can.o
obj-$(CONFIG_CAN_CC770)		+= cc770/
obj-$(CONFIG_CAN_C_CAN)		+= c_can/
obj-$(CONFIG_CAN_FLEXCAN)	+= flexcan/
obj-$(CONFIG_CAN_GRCAN)		+= grcan.o
obj-$(CONFIG_CAN_IFI_CANFD)	+= ifi_canfd/
obj-$(CONFIG_CAN_JANZ_ICAN3)	+= janz-ican3.o
obj-$(CONFIG_CAN_KVASER_PCIEFD)	+= kvaser_pciefd.o
obj-$(CONFIG_CAN_MSCAN)		+= mscan/
obj-$(CONFIG_CAN_M_CAN)		+= m_can/
obj-$(CONFIG_CAN_PEAK_PCIEFD)	+= peak_canfd/
obj-$(CONFIG_CAN_SJA1000)	+= sja1000/
obj-$(CONFIG_CAN_SUN4I)		+= sun4i_can.o
obj-$(CONFIG_CAN_TI_HECC)	+= ti_hecc.o
obj-$(CONFIG_CAN_XILINXCAN)	+= xilinx_can.o
obj-$(CONFIG_PCH_CAN)		+= pch_can.o

subdir-ccflags-$(CONFIG_CAN_DEBUG_DEVICES) += -DDEBUG
