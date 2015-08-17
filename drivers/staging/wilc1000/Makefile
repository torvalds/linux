obj-$(CONFIG_WILC1000) += wilc1000.o
obj-$(CONFIG_WILC1000_PREALLOCATE_DURING_SYSTEM_BOOT) += wilc_exported_buf.o


ccflags-$(CONFIG_WILC1000_SDIO) += -DWILC_SDIO -DCOMPLEMENT_BOOT
ccflags-$(CONFIG_WILC1000_HW_OOB_INTR) += -DWILC_SDIO_IRQ_GPIO
ccflags-$(CONFIG_WILC1000_SPI) += -DWILC_SPI

ccflags-y += -DSTA_FIRMWARE=\"atmel/wilc1000_fw.bin\" \
		-DAP_FIRMWARE=\"atmel/wilc1000_ap_fw.bin\" \
		-DP2P_CONCURRENCY_FIRMWARE=\"atmel/wilc1000_p2p_fw.bin\"

ccflags-y += -I$(src)/ -D__CHECK_ENDIAN__ -DWILC_ASIC_A0 \
		-DPLL_WORKAROUND -DCONNECT_DIRECT  -DAGING_ALG \
		-DWILC_PARSE_SCAN_IN_HOST -DDISABLE_PWRSAVE_AND_SCAN_DURING_IP \
		-Wno-unused-function -DUSE_WIRELESS -DWILC_DEBUGFS
#ccflags-y += -DTCP_ACK_FILTER

ccflags-$(CONFIG_WILC1000_PREALLOCATE_DURING_SYSTEM_BOOT) += -DMEMORY_STATIC \
								-DWILC_PREALLOC_AT_BOOT

ccflags-$(CONFIG_WILC1000_PREALLOCATE_AT_LOADING_DRIVER) += -DMEMORY_STATIC \
								-DWILC_PREALLOC_AT_INSMOD

ccflags-$(CONFIG_WILC1000_DYNAMICALLY_ALLOCATE_MEMROY) += -DWILC_NORMAL_ALLOC


wilc1000-objs := wilc_wfi_cfgoperations.o linux_wlan.o linux_mon.o \
			wilc_memory.o wilc_msgqueue.o \
			coreconfigurator.o host_interface.o \
			wilc_sdio.o wilc_spi.o wilc_wlan_cfg.o wilc_debugfs.o

wilc1000-$(CONFIG_WILC1000_SDIO) += linux_wlan_sdio.o
wilc1000-$(CONFIG_WILC1000_SPI) += linux_wlan_spi.o
