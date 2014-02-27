AR6003_SRC_PATH  := drivers/net/wireless/ar6003

EXTRA_CFLAGS += -I${AR6003_SRC_PATH}/host/include
EXTRA_CFLAGS += -I${AR6003_SRC_PATH}/include
EXTRA_CFLAGS += -I${AR6003_SRC_PATH}/host/wlan/include
EXTRA_CFLAGS += -I${AR6003_SRC_PATH}/host/os/linux/include
EXTRA_CFLAGS += -I${AR6003_SRC_PATH}/host/os/
EXTRA_CFLAGS += -I${AR6003_SRC_PATH}/host/bmi/include
EXTRA_CFLAGS += -I${AR6003_SRC_PATH}/include/AR6002
EXTRA_CFLAGS += -I${AR6003_SRC_PATH}/host/hif/sdio/linux_sdio/include
EXTRA_CFLAGS += -Idrivers/net/wireless

EXTRA_CFLAGS += -DLINUX -D__KERNEL__ -DTCMD -DSEND_EVENT_TO_APP -DUSER_KEYS -DNO_SYNC_FLUSH -DHTC_EP_STAT_PROFILING -DWLAN_HEADERS -DATH_AR6K_11N_SUPPORT -DATH_SUPPORT_DFS -DANDROID_ENV -D__linux__ -DINIT_MODE_DRV_ENABLED -DBMIENABLE_SET -DAR600x_SD31_XXX -DATH6KL_SKIP_ABI_VERSION_CHECK -DATH6KL_CONFIG_HIF_VIRTUAL_SCATTER -DCONFIG_HOST_TCMD_SUPPORT -DCONFIG_AP_VIRTUAL_ADAPTER_SUPPORT -DSOFTMAC_FILE_USED -DDEBUG -DATH_DEBUG_MODULE -DAR6002_HEADERS_DEF -DAR6003_HEADERS_DEF -DMCKINLEY_HEADERS_DEF -DKERNEL_2_6

ifeq ($(CONFIG_BT),y)
EXTRA_CFLAGS += -DATH_AR6K_ENABLE_GMBOX
EXTRA_CFLAGS += -DHCI_TRANSPORT_SDIO
EXTRA_CFLAGS += -DSETUPHCI_ENABLED
EXTRA_CFLAGS += -DSETUPBTDEV_ENABLED
endif

EXTRA_CFLAGS += -DRK29
EXTRA_CFLAGS += -DTCHIP

olca3-objs := host/hif/sdio/linux_sdio/src/hif.o  \
		host/hif/sdio/linux_sdio/src/hif_scatter.o \
		host/hif/common/hif_bmi_reg_access.o \
		host/hif/common/hif_diag_reg_access.o \
		host/wmi/wmi.o \
		host/reorder/rcv_aggr.o \
		host/htc2/AR6000/ar6k.o \
		host/htc2/AR6000/ar6k_events.o \
		host/htc2/htc_services.o \
		host/htc2/htc.o \
		host/htc2/htc_send.o \
		host/htc2/htc_recv.o \
		host/wlan/src/wlan_recv_beacon.o \
		host/wlan/src/wlan_utils.o \
		host/wlan/src/wlan_node.o \
		host/bmi/src/bmi.o \
		host/miscdrv/credit_dist.o \
		host/miscdrv/common_drv.o \
		host/os/linux/hci_bridge.o \
		host/os/linux/netbuf.o \
		host/os/linux/ar6000_android.o \
		host/os/linux/ioctl.o \
		host/os/linux/wireless_ext.o \
		host/os/linux/ar6000_pm.o \
		host/os/linux/ar6000_raw_if.o \
		host/os/linux/ar6000_drv.o \
		host/os/linux/ar6k_pal.o \
		host/regtable/regtable.o \
		host/regtable/AR6002def.o \
		host/regtable/MCKINLEYdef.o \
		host/regtable/AR6003def.o \
		host/dfs/dfs_debug.o \
		host/dfs/dfs_process_phyerr.o \
		host/dfs/dfs.o \
		host/dfs/dfs_misc.o \
		host/dfs/dfs_bindetects.o \
		host/dfs/dfs_init.o \
		host/dfs/dfs_host_project.o \
		host/dfs/dfs_staggered.o \
		host/dfs/dfs_ar.o \
		host/dfs/dfs_process_radarevent.o \
		host/dfs/dfs_fcc_bin5.o 

ifeq ($(CONFIG_BT),y)
olca3-objs +=  host/htc2/AR6000/ar6k_gmbox.o \
		host/htc2/AR6000/ar6k_gmbox_hciuart.o \
		host/miscdrv/ar3kconfig.o \
		host/miscdrv/ar3kps/ar3kpsconfig.o \
		host/miscdrv/ar3kps/ar3kpsparser.o 
endif

obj-$(CONFIG_AR6003) += olca3.o

