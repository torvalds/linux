#
#  Makefile for the Linux Controller Area Network drivers.
#

obj-$(CONFIG_CAN_VCAN)		+= vcan.o
obj-$(CONFIG_CAN_SLCAN)		+= slcan.o

obj-$(CONFIG_CAN_DEV)		+= can-dev.o
can-dev-y			:= dev.o

can-dev-$(CONFIG_CAN_LEDS)	+= led.o

obj-y				+= spi/
obj-y				+= usb/
obj-y				+= softing/

obj-$(CONFIG_CAN_AT91)		+= at91_can.o
obj-$(CONFIG_CAN_BFIN)		+= bfin_can.o
obj-$(CONFIG_CAN_CC770)		+= cc770/
obj-$(CONFIG_CAN_C_CAN)		+= c_can/
obj-$(CONFIG_CAN_FLEXCAN)	+= flexcan.o
obj-$(CONFIG_CAN_GRCAN)		+= grcan.o
obj-$(CONFIG_CAN_IFI_CANFD)	+= ifi_canfd/
obj-$(CONFIG_CAN_JANZ_ICAN3)	+= janz-ican3.o
obj-$(CONFIG_CAN_MSCAN)		+= mscan/
obj-$(CONFIG_CAN_M_CAN)		+= m_can/
obj-$(CONFIG_CAN_RCAR)		+= rcar_can.o
obj-$(CONFIG_CAN_SJA1000)	+= sja1000/
obj-$(CONFIG_CAN_SUN4I)		+= sun4i_can.o
obj-$(CONFIG_CAN_TI_HECC)	+= ti_hecc.o
obj-$(CONFIG_CAN_XILINXCAN)	+= xilinx_can.o
obj-$(CONFIG_PCH_CAN)		+= pch_can.o

subdir-ccflags-y += -D__CHECK_ENDIAN__
subdir-ccflags-$(CONFIG_CAN_DEBUG_DEVICES) += -DDEBUG
