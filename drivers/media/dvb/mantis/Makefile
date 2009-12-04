mantis-objs = 	mantis_core.o	\
		mantis_uart.o	\
		mantis_dma.o	\
		mantis_pci.o	\
		mantis_i2c.o	\
		mantis_dvb.o	\
		mantis_evm.o	\
		mantis_hif.o	\
		mantis_ca.o	\
		mantis_pcmcia.o	\
		mantis_vp1033.o	\
		mantis_vp1034.o	\
		mantis_vp1041.o	\
		mantis_vp2033.o	\
		mantis_vp2040.o	\
		mantis_vp3028.o	\
		mantis_vp3030.o

obj-$(CONFIG_DVB_MANTIS) += mantis.o

EXTRA_CFLAGS = -Idrivers/media/dvb/dvb-core/ -Idrivers/media/dvb/frontends/
