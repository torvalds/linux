obj-$(CONFIG_WILC1000) += wilc1000.o

ccflags-y += -DSTA_FIRMWARE=\"atmel/wilc1000_fw.bin\" \
		-DAP_FIRMWARE=\"atmel/wilc1000_ap_fw.bin\" \
		-DP2P_CONCURRENCY_FIRMWARE=\"atmel/wilc1000_p2p_fw.bin\"

ccflags-y += -I$(src)/ -DWILC_ASIC_A0 -DWILC_DEBUGFS
#ccflags-y += -DTCP_ACK_FILTER

wilc1000-objs := wilc_wfi_cfgoperations.o linux_wlan.o linux_mon.o \
			wilc_msgqueue.o \
			coreconfigurator.o host_interface.o \
			wilc_wlan_cfg.o wilc_debugfs.o \
			wilc_wlan.o

obj-$(CONFIG_WILC1000_SDIO) += wilc1000-sdio.o
wilc1000-sdio-objs += wilc_sdio.o

obj-$(CONFIG_WILC1000_SPI) += wilc1000-spi.o
wilc1000-spi-objs += wilc_spi.o
