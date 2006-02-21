#
# Makefile for the kernel pcmcia subsystem (c/o David Hinds)
#

ifeq ($(CONFIG_PCMCIA_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

pcmcia_core-y					+= cs.o cistpl.o rsrc_mgr.o socket_sysfs.o
pcmcia_core-$(CONFIG_CARDBUS)			+= cardbus.o
obj-$(CONFIG_PCCARD)				+= pcmcia_core.o

pcmcia-y					+= ds.o pcmcia_resource.o
pcmcia-$(CONFIG_PCMCIA_IOCTL)			+= pcmcia_ioctl.o
obj-$(CONFIG_PCMCIA)				+= pcmcia.o

obj-$(CONFIG_PCCARD_NONSTATIC)			+= rsrc_nonstatic.o


# socket drivers

obj-$(CONFIG_YENTA) 				+= yenta_socket.o

obj-$(CONFIG_PD6729)				+= pd6729.o
obj-$(CONFIG_I82365)				+= i82365.o
obj-$(CONFIG_I82092)				+= i82092.o
obj-$(CONFIG_TCIC)				+= tcic.o
obj-$(CONFIG_PCMCIA_M8XX)			+= m8xx_pcmcia.o
obj-$(CONFIG_HD64465_PCMCIA)			+= hd64465_ss.o
obj-$(CONFIG_PCMCIA_SA1100)			+= sa11xx_core.o sa1100_cs.o
obj-$(CONFIG_PCMCIA_SA1111)			+= sa11xx_core.o sa1111_cs.o
obj-$(CONFIG_PCMCIA_PXA2XX)                     += pxa2xx_core.o pxa2xx_cs.o
obj-$(CONFIG_M32R_PCC)				+= m32r_pcc.o
obj-$(CONFIG_M32R_CFC)				+= m32r_cfc.o
obj-$(CONFIG_PCMCIA_AU1X00)			+= au1x00_ss.o
obj-$(CONFIG_PCMCIA_VRC4171)			+= vrc4171_card.o
obj-$(CONFIG_PCMCIA_VRC4173)			+= vrc4173_cardu.o
obj-$(CONFIG_OMAP_CF)				+= omap_cf.o
obj-$(CONFIG_AT91_CF)				+= at91_cf.o

sa11xx_core-y					+= soc_common.o sa11xx_base.o
pxa2xx_core-y					+= soc_common.o pxa2xx_base.o

au1x00_ss-y					+= au1000_generic.o
au1x00_ss-$(CONFIG_MIPS_PB1000)			+= au1000_pb1x00.o
au1x00_ss-$(CONFIG_MIPS_PB1100)			+= au1000_pb1x00.o
au1x00_ss-$(CONFIG_MIPS_PB1200)			+= au1000_db1x00.o
au1x00_ss-$(CONFIG_MIPS_PB1500)			+= au1000_pb1x00.o
au1x00_ss-$(CONFIG_MIPS_DB1000)			+= au1000_db1x00.o
au1x00_ss-$(CONFIG_MIPS_DB1100)			+= au1000_db1x00.o
au1x00_ss-$(CONFIG_MIPS_DB1200)			+= au1000_db1x00.o
au1x00_ss-$(CONFIG_MIPS_DB1500)			+= au1000_db1x00.o
au1x00_ss-$(CONFIG_MIPS_DB1550)			+= au1000_db1x00.o
au1x00_ss-$(CONFIG_MIPS_XXS1500)		+= au1000_xxs1500.o

sa1111_cs-y					+= sa1111_generic.o
sa1111_cs-$(CONFIG_ASSABET_NEPONSET)		+= sa1100_neponset.o
sa1111_cs-$(CONFIG_SA1100_BADGE4)		+= sa1100_badge4.o
sa1111_cs-$(CONFIG_SA1100_JORNADA720)		+= sa1100_jornada720.o

sa1100_cs-y					+= sa1100_generic.o
sa1100_cs-$(CONFIG_SA1100_ASSABET)		+= sa1100_assabet.o
sa1100_cs-$(CONFIG_SA1100_CERF)			+= sa1100_cerf.o
sa1100_cs-$(CONFIG_SA1100_COLLIE)              += pxa2xx_sharpsl.o
sa1100_cs-$(CONFIG_SA1100_H3600)		+= sa1100_h3600.o
sa1100_cs-$(CONFIG_SA1100_SHANNON)		+= sa1100_shannon.o
sa1100_cs-$(CONFIG_SA1100_SIMPAD)		+= sa1100_simpad.o

pxa2xx_cs-$(CONFIG_ARCH_LUBBOCK)		+= pxa2xx_lubbock.o sa1111_generic.o
pxa2xx_cs-$(CONFIG_MACH_MAINSTONE)		+= pxa2xx_mainstone.o
pxa2xx_cs-$(CONFIG_PXA_SHARPSL)			+= pxa2xx_sharpsl.o

