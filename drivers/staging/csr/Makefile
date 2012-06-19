obj-$(CONFIG_CSR_WIFI)	+= oska/

ccflags-y	:= -DCSR_SME_USERSPACE -DCSR_SUPPORT_SME -DREMOTE_SYS_SAP -DCSR_WIFI_SECURITY_WAPI_ENABLE -DENABLE_SHUTDOWN

obj-$(CONFIG_CSR_WIFI)	+= csr_wifi.o
obj-$(CONFIG_CSR_WIFI)	+= csr_helper.o

csr_wifi-y :=	bh.o				\
		data_tx.o			\
		drv.o				\
		firmware.o			\
		indications.o			\
		inet.o				\
		init_hw.o			\
		io.o				\
		monitor.o			\
		netdev.o			\
		os.o				\
		putest.o			\
		sdio_events.o			\
		sdio_mmc.o			\
		sdio_stubs.o			\
		sme_blocking.o			\
		ul_int.o			\
		unifi_dbg.o			\
		unifi_event.o			\
		unifi_pdu_processing.o		\
		unifi_sme.o			\
		csr_formatted_io.o		\
		csr_wifi_hip_card_sdio.o	\
		csr_wifi_hip_card_sdio_intr.o	\
		csr_wifi_hip_card_sdio_mem.o	\
		csr_wifi_hip_chiphelper.o	\
		csr_wifi_hip_download.o		\
		csr_wifi_hip_dump.o		\
		csr_wifi_hip_packing.o		\
		csr_wifi_hip_send.o		\
		csr_wifi_hip_signals.o		\
		csr_wifi_hip_ta_sampling.o	\
		csr_wifi_hip_udi.o		\
		csr_wifi_hip_unifi_signal_names.o	\
		csr_wifi_hip_xbv.o		\
		csr_wifi_router_ctrl_sef.o	\
		csr_wifi_router_sef.o		\
		csr_wifi_router_transport.o	\
		csr_wifi_sme_sef.o		\
		csr_wifi_sme_converter_init.o	\
		csr_wifi_sme_free_downstream_contents.o		\
		csr_wifi_sme_free_upstream_contents.o		\
		csr_wifi_sme_serialize.o			\
		csr_wifi_router_ctrl_converter_init.o		\
		csr_wifi_router_ctrl_free_downstream_contents.o	\
		csr_wifi_router_ctrl_free_upstream_contents.o	\
		csr_wifi_router_ctrl_serialize.o		\
		csr_wifi_router_converter_init.o		\
		csr_wifi_router_free_downstream_contents.o	\
		csr_wifi_router_free_upstream_contents.o	\
		csr_wifi_router_serialize.o			\
		sme_mgt.o			\
		sme_sys.o			\
		sme_userspace.o

csr_helper-y :=	csr_time.o			\
		csr_util.o			\
		csr_framework_ext.o		\
		csr_pmem.o			\
		csr_wifi_serialize_primitive_types.o	\
		csr_serialize_primitive_types.o	\
		csr_utf16.o			\
		csr_msgconv.o			\
		csr_panic.o
