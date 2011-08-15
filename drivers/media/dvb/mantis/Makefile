mantis_core-objs :=	mantis_ioc.o	\
			mantis_uart.o	\
			mantis_dma.o	\
			mantis_pci.o	\
			mantis_i2c.o	\
			mantis_dvb.o	\
			mantis_evm.o	\
			mantis_hif.o	\
			mantis_ca.o	\
			mantis_pcmcia.o	\
			mantis_input.o

mantis-objs	:=	mantis_cards.o	\
			mantis_vp1033.o	\
			mantis_vp1034.o	\
			mantis_vp1041.o	\
			mantis_vp2033.o	\
			mantis_vp2040.o	\
			mantis_vp3030.o

hopper-objs	:=	hopper_cards.o	\
			hopper_vp3028.o

obj-$(CONFIG_MANTIS_CORE)	+= mantis_core.o
obj-$(CONFIG_DVB_MANTIS)	+= mantis.o
obj-$(CONFIG_DVB_HOPPER)	+= hopper.o

ccflags-y += -Idrivers/media/dvb/dvb-core/ -Idrivers/media/dvb/frontends/
