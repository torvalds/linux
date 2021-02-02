# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the kernel pcmcia subsystem (c/o David Hinds)
#

pcmcia_core-y					+= cs.o socket_sysfs.o
pcmcia_core-$(CONFIG_CARDBUS)			+= cardbus.o
obj-$(CONFIG_PCCARD)				+= pcmcia_core.o

pcmcia-y					+= ds.o pcmcia_resource.o cistpl.o pcmcia_cis.o
obj-$(CONFIG_PCMCIA)				+= pcmcia.o

pcmcia_rsrc-y					+= rsrc_mgr.o
pcmcia_rsrc-$(CONFIG_PCCARD_NONSTATIC)		+= rsrc_nonstatic.o
pcmcia_rsrc-$(CONFIG_PCCARD_IODYN)		+= rsrc_iodyn.o
obj-$(CONFIG_PCCARD)				+= pcmcia_rsrc.o


# socket drivers

obj-$(CONFIG_YENTA) 				+= yenta_socket.o

obj-$(CONFIG_PD6729)				+= pd6729.o
obj-$(CONFIG_I82365)				+= i82365.o
obj-$(CONFIG_I82092)				+= i82092.o
obj-$(CONFIG_TCIC)				+= tcic.o
obj-$(CONFIG_PCMCIA_SOC_COMMON)			+= soc_common.o
obj-$(CONFIG_PCMCIA_SA11XX_BASE)		+= sa11xx_base.o
obj-$(CONFIG_PCMCIA_SA1100)			+= sa1100_cs.o
obj-$(CONFIG_PCMCIA_SA1111)			+= sa1111_cs.o
obj-$(CONFIG_PCMCIA_BCM63XX)			+= bcm63xx_pcmcia.o
obj-$(CONFIG_PCMCIA_VRC4171)			+= vrc4171_card.o
obj-$(CONFIG_OMAP_CF)				+= omap_cf.o
obj-$(CONFIG_AT91_CF)				+= at91_cf.o
obj-$(CONFIG_ELECTRA_CF)			+= electra_cf.o
obj-$(CONFIG_PCMCIA_ALCHEMY_DEVBOARD)		+= db1xxx_ss.o
obj-$(CONFIG_PCMCIA_MAX1600)			+= max1600.o

sa1111_cs-y					+= sa1111_generic.o
sa1111_cs-$(CONFIG_ASSABET_NEPONSET)		+= sa1111_neponset.o
sa1111_cs-$(CONFIG_SA1100_BADGE4)		+= sa1111_badge4.o
sa1111_cs-$(CONFIG_SA1100_JORNADA720)		+= sa1111_jornada720.o
sa1111_cs-$(CONFIG_ARCH_LUBBOCK)		+= sa1111_lubbock.o

sa1100_cs-y					+= sa1100_generic.o
sa1100_cs-$(CONFIG_SA1100_COLLIE)		+= pxa2xx_sharpsl.o
sa1100_cs-$(CONFIG_SA1100_H3100)		+= sa1100_h3600.o
sa1100_cs-$(CONFIG_SA1100_H3600)		+= sa1100_h3600.o
sa1100_cs-$(CONFIG_SA1100_SIMPAD)		+= sa1100_simpad.o

pxa2xx_cm_x2xx_cs-y				+= pxa2xx_cm_x2xx.o pxa2xx_cm_x255.o pxa2xx_cm_x270.o
pxa2xx-obj-$(CONFIG_MACH_MAINSTONE)		+= pxa2xx_mainstone.o
pxa2xx-obj-$(CONFIG_PXA_SHARPSL)		+= pxa2xx_sharpsl.o
pxa2xx-obj-$(CONFIG_MACH_ARMCORE)		+= pxa2xx_cm_x2xx_cs.o
pxa2xx-obj-$(CONFIG_ARCOM_PCMCIA)		+= pxa2xx_viper.o
pxa2xx-obj-$(CONFIG_TRIZEPS_PCMCIA)		+= pxa2xx_trizeps4.o
pxa2xx-obj-$(CONFIG_MACH_PALMTX)		+= pxa2xx_palmtx.o
pxa2xx-obj-$(CONFIG_MACH_PALMTC)		+= pxa2xx_palmtc.o
pxa2xx-obj-$(CONFIG_MACH_PALMLD)		+= pxa2xx_palmld.o
pxa2xx-obj-$(CONFIG_MACH_E740)			+= pxa2xx_e740.o
pxa2xx-obj-$(CONFIG_MACH_STARGATE2)		+= pxa2xx_stargate2.o
pxa2xx-obj-$(CONFIG_MACH_VPAC270)		+= pxa2xx_vpac270.o
pxa2xx-obj-$(CONFIG_MACH_BALLOON3)		+= pxa2xx_balloon3.o
pxa2xx-obj-$(CONFIG_MACH_COLIBRI)		+= pxa2xx_colibri.o
pxa2xx-obj-$(CONFIG_MACH_COLIBRI320)		+= pxa2xx_colibri.o
pxa2xx-obj-$(CONFIG_MACH_H4700)			+= pxa2xx_hx4700.o

obj-$(CONFIG_PCMCIA_PXA2XX)			+= pxa2xx_base.o $(pxa2xx-obj-y)

obj-$(CONFIG_PCMCIA_XXS1500)			+= xxs1500_ss.o
