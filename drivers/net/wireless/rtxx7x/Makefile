ifeq ($(WIFI_MODE),)
RT28xx_MODE = STA
else
RT28xx_MODE = $(WIFI_MODE)
endif

ifeq ($(TARGET),)
TARGET = LINUX
endif

ifeq ($(CHIPSET),)
CHIPSET = 5370
endif

#OS ABL - YES or NO
OSABL = NO

ifneq ($(TARGET),THREADX)
#RT28xx_DIR = home directory of RT28xx source code
RT28xx_DIR = $(shell pwd)
endif

RTMP_SRC_DIR = $(RT28xx_DIR)/RT$(CHIPSET)

#PLATFORM: Target platform
#PLATFORM = JZSOC
#PLATFORM = PC
#PLATFORM = 5VT
#PLATFORM = IKANOS_V160
#PLATFORM = IKANOS_V180
#PLATFORM = SIGMA
#PLATFORM = SIGMA_8622
#PLATFORM = INIC
#PLATFORM = STAR
#PLATFORM = IXP
#PLATFORM = INF_TWINPASS
#PLATFORM = INF_DANUBE
#PLATFORM = INF_AR9
#PLATFORM = INF_VR9
#PLATFORM = BRCM_6358
#PLATFORM = INF_AMAZON_SE
#PLATFORM = CAVM_OCTEON
#PLATFORM = CMPC
#PLATFORM = RALINK_2880
#PLATFORM = RALINK_3052
#PLATFORM = SMDK
#PLATFORM = RMI
#PLATFORM = RMI_64
#PLATFORM = KODAK_DC
#PLATFORM = DM6446
#PLATFORM = FREESCALE8377
#PLATFORM = BL2348
#PLATFORM = BLUBB
#PLATFORM = BLPMP
#PLATFORM = MT85XX
#PLATFORM = NXP_TV550
#PLATFORM = MVL5
#PLATFORM = RALINK_3352
#LATFORM = IMX51
#PLATFORM = Telechips
PLATFORM = sunxi

#APSOC
ifeq ($(CHIPSET),3050)
PLATFORM = RALINK_3050
endif
ifeq ($(CHIPSET),3052)
PLATFORM = RALINK_3052
endif
ifeq ($(CHIPSET),3350)
PLATFORM = RALINK_3050
endif
ifeq ($(CHIPSET),3352)
PLATFORM = RALINK_3352
endif


#RELEASE Package
RELEASE = DPO


ifeq ($(TARGET),LINUX)
MAKE = make
endif

ifeq ($(TARGET), UCOS)
MAKE = make
endif
ifeq ($(TARGET),THREADX)
MAKE = gmake
endif

ifeq ($(TARGET), ECOS)
MAKE = make
MODULE = $(shell pwd | sed "s/.*\///" ).o
export MODULE
endif




ifeq ($(PLATFORM),sunxi)
#LINUX_SRC = ~/workspace/ics-exdroid/lichee/linux-3.0
#LINUX_SRC_MODULE = ~/workspace/ics-exdroid/lichee/linux-3.0/drivers/net/wireless
#CROSS_COMPILE = ~/workspace/ics-exdroid/lichee/buildroot/output/external-toolchain/bin/arm-none-linux-gnueabi-
LINUX_SRC = $(RT28xx_DIR)/../../../../
LINUX_SRC_MODULE = $(RT28xx_DIR)/../
endif

# kernel tree build
ifneq ($(KERNELRELEASE),)
# add source here
###############################start of os/linux/config.mk####################################
# Support ATE function
HAS_ATE=n
# Support ATE NEW TXCONT solution
HAS_NEW_TXCONT=n

# Support ATE NEW TXCARR solution
HAS_NEW_TXCARR=n

# Support ATE NEW TXCARS solution
HAS_NEW_TXCARS=n

#-----------------------------------------------#
# NOTE : RT2xxx does not support this feature !
# Do not touch this block !!! 
ifeq ($(CHIPSET),2860)
HAS_NEW_TXCONT=n
HAS_NEW_TXCARR=n
HAS_NEW_TXCARS=n
endif
ifeq ($(CHIPSET),2870)
HAS_NEW_TXCONT=n
HAS_NEW_TXCARR=n
HAS_NEW_TXCARS=n
endif
ifeq ($(CHIPSET),2880)
HAS_NEW_TXCONT=n
HAS_NEW_TXCARR=n
HAS_NEW_TXCARS=n
endif
ifeq ($(CHIPSET),2070)
HAS_NEW_TXCONT=n
HAS_NEW_TXCARR=n
HAS_NEW_TXCARS=n
endif
#-----------------------------------------------#

# Support QA ATE function
HAS_QA_SUPPORT=n

HAS_RSSI_FEEDBACK=n

# Support XLINK mode
HAS_XLINK=n


HAS_NINTENDO=n

# Support LLTD function
HAS_LLTD=n


# Support AP-Client function
HAS_APCLI=n

# Support Wpa_Supplicant
HAS_WPA_SUPPLICANT=y


# Support Native WpaSupplicant for Network Maganger
HAS_NATIVE_WPA_SUPPLICANT_SUPPORT=y

#Support Net interface block while Tx-Sw queue full
HAS_BLOCK_NET_IF=n

#Support IGMP-Snooping function.
HAS_IGMP_SNOOP_SUPPORT=n

#Support DFS function
HAS_DFS_SUPPORT=n

#Support Carrier-Sense function
HAS_CS_SUPPORT=n

#Support USB load firmware by multibyte
HAS_USB_FIRMWARE_MULTIBYTE_WRITE=y

# Support user specific transmit rate of Multicast packet.
HAS_MCAST_RATE_SPECIFIC_SUPPORT=n

# Support for Multiple Cards
HAS_MC_SUPPORT=n

#Support for PCI-MSI
HAS_MSI_SUPPORT=n


#Support for IEEE802.11e DLS
HAS_QOS_DLS_SUPPORT=n

#Support for EXT_CHANNEL
HAS_EXT_BUILD_CHANNEL_LIST=n

#Support for IDS 
HAS_IDS_SUPPORT=n


#Support for Net-SNMP
HAS_SNMP_SUPPORT=n

#Support features of 802.11n Draft3
HAS_DOT11N_DRAFT3_SUPPORT=y

#Support features of Single SKU. 
HAS_SINGLE_SKU_SUPPORT=n

#Support features of 802.11n
HAS_DOT11_N_SUPPORT=y


#Support for RT5392 RT5372
HAS_TEMPERATURE_COMPENSATION=n

#Support for 2860/2880 co-exist 
HAS_RT2880_RT2860_COEXIST=n

HAS_KTHREAD_SUPPORT=n





#Support for Auto channel select enhance
HAS_AUTO_CH_SELECT_ENHANCE=n

#Support statistics count
HAS_STATS_COUNT=y


#Support USB_BULK_BUF_ALIGMENT
HAS_USB_BULK_BUF_ALIGMENT=y


#Support USB_BULK_BUF_ALIGMENT
HAS_USB_BULK_BUF_ALIGMENT2=n


#Support for USB_SUPPORT_SELECTIVE_SUSPEND
HAS_USB_SUPPORT_SELECTIVE_SUSPEND=y


#Support ANDROID_SUPPORT
HAS_ANDROID_SUPPORT=y





#Client support WDS function
HAS_CLIENT_WDS_SUPPORT=n

#Support for Bridge Fast Path & Bridge Fast Path function open to other module
HAS_BGFP_SUPPORT=n
HAS_BGFP_OPEN_SUPPORT=n

# Support HOSTAPD function
HAS_HOSTAPD_SUPPORT=n

#Support GreenAP function
HAS_GREENAP_SUPPORT=n

#Support MAC80211 LINUX-only function
#Please make sure the version for CFG80211.ko and MAC80211.ko is same as the one
#our driver references to.
HAS_CFG80211_SUPPORT=n

#Support RFKILL hardware block/unblock LINUX-only function
HAS_RFKILL_HW_SUPPORT=n




HAS_RTMP_FLASH_SUPPORT=n

ifeq ($(OSABL),YES)
HAS_OSABL_FUNC_SUPPORT=y
HAS_OSABL_OS_PCI_SUPPORT=y
HAS_OSABL_OS_USB_SUPPORT=y
HAS_OSABL_OS_RBUS_SUPPORT=n
HAS_OSABL_OS_AP_SUPPORT=y
HAS_OSABL_OS_STA_SUPPORT=y
endif

HAS_LED_CONTROL_SUPPORT=y


#Support WIDI feature
#Must enable HAS_WSC at the same time.



HAS_RESOURCE_BOOT_ALLOC=n

HAS_REFUSE_SCAN_QUERY_WHILE_SCANING=n

#################################################

CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld

WFLAGS := -DAGGREGATION_SUPPORT -DPIGGYBACK_SUPPORT -DWMM_SUPPORT  -DLINUX -Wall -Wstrict-prototypes -Wno-trigraphs 
WFLAGS += -DSYSTEM_LOG_SUPPORT  -DRT28xx_MODE=$(RT28xx_MODE) -DCHIPSET=$(CHIPSET) -DRESOURCE_PRE_ALLOC

ifeq ($(HAS_RESOURCE_BOOT_ALLOC),y)
WFLAGS += -DRESOURCE_BOOT_ALLOC
endif

ifeq ($(HAS_REFUSE_SCAN_QUERY_WHILE_SCANING),y)
WFLAGS += -DREFUSE_SCAN_QUERY_WHILE_SCANING
endif

ifeq ($(HAS_USB_FIRMWARE_MULTIBYTE_WRITE),y)
WFLAGS += -DUSB_FIRMWARE_MULTIBYTE_WRITE -DMULTIWRITE_BYTES=64
endif


ifeq ($(HAS_KTHREAD_SUPPORT),y)
WFLAGS += -DKTHREAD_SUPPORT
endif

ifeq ($(HAS_RTMP_FLASH_SUPPORT),y)
WFLAGS += -DRTMP_FLASH_SUPPORT
endif


#################################################

# config for STA mode

ifeq ($(RT28xx_MODE),STA)
WFLAGS += -DCONFIG_STA_SUPPORT -DDBG

ifeq ($(HAS_XLINK),y)
WFLAGS += -DXLINK_SUPPORT
endif


ifeq ($(HAS_WPA_SUPPLICANT),y)
WFLAGS += -DWPA_SUPPLICANT_SUPPORT
ifeq ($(HAS_NATIVE_WPA_SUPPLICANT_SUPPORT),y)
WFLAGS += -DNATIVE_WPA_SUPPLICANT_SUPPORT
endif
endif




ifeq ($(HAS_ATE),y)
WFLAGS += -DRALINK_ATE
WFLAGS += -DCONFIG_RT2880_ATE_CMD_NEW
ifeq ($(HAS_NEW_TXCONT),y)
WFLAGS += -DNEW_TXCONT
endif
ifeq ($(HAS_NEW_TXCARR),y)
WFLAGS += -DNEW_TXCARR
endif
ifeq ($(HAS_NEW_TXCARS),y)
WFLAGS += -DNEW_TXCARRSUPP
endif
ifeq ($(HAS_QA_SUPPORT),y)
WFLAGS += -DRALINK_QA
endif
endif


ifeq ($(HAS_SNMP_SUPPORT),y)
WFLAGS += -DSNMP_SUPPORT
endif

ifeq ($(HAS_QOS_DLS_SUPPORT),y)
WFLAGS += -DQOS_DLS_SUPPORT
endif

ifeq ($(HAS_DOT11_N_SUPPORT),y)
WFLAGS += -DDOT11_N_SUPPORT
ifeq ($(HAS_DOT11N_DRAFT3_SUPPORT),y)
WFLAGS += -DDOT11N_DRAFT3
endif
endif



ifeq ($(HAS_CS_SUPPORT),y)
WFLAGS += -DCARRIER_DETECTION_SUPPORT
endif

ifeq ($(HAS_STATS_COUNT),y)
WFLAGS += -DSTATS_COUNT_SUPPORT
endif

ifeq ($(HAS_USB_BULK_BUF_ALIGMENT),y)
WFLAGS += -DUSB_BULK_BUF_ALIGMENT
endif

ifeq ($(HAS_USB_BULK_BUF_ALIGMENT2),y)
WFLAGS += -DUSB_BULK_BUF_ALIGMENT2
endif


ifeq ($(HAS_ANDROID_SUPPORT),y)
WFLAGS += -DANDROID_SUPPORT -DCONFIG_PM
endif


ifeq ($(HAS_USB_SUPPORT_SELECTIVE_SUSPEND),y)
WFLAGS += -DUSB_SUPPORT_SELECTIVE_SUSPEND -DCONFIG_PM
endif


ifeq ($(HAS_CFG80211_SUPPORT),y)
WFLAGS += -DRT_CFG80211_SUPPORT
ifeq ($(HAS_RFKILL_HW_SUPPORT),y)
WFLAGS += -DRFKILL_HW_SUPPORT
endif
endif

ifeq ($(OSABL),YES)
WFLAGS += -DOS_ABL_SUPPORT
ifeq ($(HAS_OSABL_FUNC_SUPPORT),y)
WFLAGS += -DOS_ABL_FUNC_SUPPORT
endif
ifeq ($(HAS_OSABL_OS_PCI_SUPPORT),y)
WFLAGS += -DOS_ABL_OS_PCI_SUPPORT
endif
ifeq ($(HAS_OSABL_OS_USB_SUPPORT),y)
WFLAGS += -DOS_ABL_OS_USB_SUPPORT
endif
ifeq ($(HAS_OSABL_OS_RBUS_SUPPORT),y)
WFLAGS += -DOS_ABL_OS_RBUS_SUPPORT
endif
ifeq ($(HAS_OSABL_OS_AP_SUPPORT),y)
WFLAGS += -DOS_ABL_OS_AP_SUPPORT
endif
ifeq ($(HAS_OSABL_OS_STA_SUPPORT),y)
WFLAGS += -DOS_ABL_OS_STA_SUPPORT
endif
endif



ifeq ($(HAS_WIDI_SUPPORT),y)
WFLAGS += -DWIDI_SUPPORT
endif

endif
# endif of ifeq ($(RT28xx_MODE),STA)

#################################################

#################################################

#
# Common compiler flag
#






ifeq ($(HAS_EXT_BUILD_CHANNEL_LIST),y)
WFLAGS += -DEXT_BUILD_CHANNEL_LIST
endif

ifeq ($(HAS_IDS_SUPPORT),y)
WFLAGS += -DIDS_SUPPORT
endif



ifeq ($(HAS_TEMPERATURE_COMPENSATION),y)
WFLAGS += -DRTMP_TEMPERATURE_COMPENSATION
endif



ifeq ($(OSABL),YES)
WFLAGS += -DEXPORT_SYMTAB
endif

ifeq ($(HAS_CLIENT_WDS_SUPPORT),y)
WFLAGS += -DCLIENT_WDS
endif

ifeq ($(HAS_BGFP_SUPPORT),y)
WFLAGS += -DBG_FT_SUPPORT
endif

ifeq ($(HAS_BGFP_OPEN_SUPPORT),y)
WFLAGS += -DBG_FT_OPEN_SUPPORT
endif


ifeq ($(HAS_LED_CONTROL_SUPPORT),y)
WFLAGS += -DLED_CONTROL_SUPPORT
endif

#################################################
# ChipSet specific definitions.
#
ifeq ($(CHIPSET),2860)
WFLAGS +=-DRTMP_MAC_PCI -DRTMP_PCI_SUPPORT -DRT2860 -DRT28xx -DA_BAND_SUPPORT
CHIPSET_DAT = 2860
ifeq ($(HAS_DFS_SUPPORT),y)
WFLAGS += -DDFS_SOFTWARE_SUPPORT
endif
endif

ifeq ($(CHIPSET),3090)
WFLAGS +=-DRTMP_MAC_PCI -DRT30xx -DRT3090  -DRTMP_PCI_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DSPECIFIC_VCORECAL_SUPPORT
CHIPSET_DAT = 2860
endif

ifeq ($(CHIPSET),2870)
WFLAGS +=-DRTMP_MAC_USB -DRTMP_USB_SUPPORT -DRT2870 -DRT28xx -DRTMP_TIMER_TASK_SUPPORT -DA_BAND_SUPPORT
CHIPSET_DAT = 2870
ifeq ($(HAS_DFS_SUPPORT),y)
WFLAGS += -DDFS_SOFTWARE_SUPPORT
endif

endif

ifeq ($(CHIPSET),2070)
WFLAGS +=-DRTMP_MAC_USB -DRT30xx -DRT3070 -DRT2070 -DRTMP_USB_SUPPORT -DRTMP_TIMER_TASK_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT
CHIPSET_DAT = 2870
endif

ifeq ($(CHIPSET),3070)
WFLAGS +=-DRTMP_MAC_USB -DRT30xx -DRT3070 -DRTMP_USB_SUPPORT -DRTMP_TIMER_TASK_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DSPECIFIC_VCORECAL_SUPPORT
CHIPSET_DAT = 2870
endif

ifeq ($(CHIPSET),2880)
WFLAGS += -DRT2880 -DRT28xx -DRTMP_MAC_PCI -DCONFIG_RALINK_RT2880_MP -DRTMP_RBUS_SUPPORT -DMERGE_ARCH_TEAM -DA_BAND_SUPPORT -DCONFIG_SWMCU_SUPPORT
ifeq ($(HAS_DFS_SUPPORT),y)
WFLAGS += -DDFS_HARDWARE_SUPPORT -DDFS_FCC_BW40_FIX -DDFS_DEBUG
endif
ifeq ($(HAS_WIFI_LED_SHARE), y)
WFLAGS += -DCONFIG_WIFI_LED_SHARE
endif
endif

ifeq ($(CHIPSET),3572)
WFLAGS +=-DRTMP_MAC_USB -DRTMP_USB_SUPPORT -DRT2870 -DRT28xx -DRT30xx -DRT35xx -DRTMP_TIMER_TASK_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DA_BAND_SUPPORT -DSPECIFIC_VCORECAL_SUPPORT
CHIPSET_DAT = 2870
ifeq ($(HAS_DFS_SUPPORT),y)
WFLAGS += -DDFS_SOFTWARE_SUPPORT
endif
endif

ifeq ($(CHIPSET),3062)
WFLAGS +=-DRTMP_MAC_PCI -DRT2860 -DRT28xx -DRT30xx -DRT35xx -DRT3062 -DRTMP_PCI_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DSPECIFIC_VCORECAL_SUPPORT
CHIPSET_DAT = 2860
endif

ifeq ($(CHIPSET),3562)
WFLAGS +=-DRTMP_MAC_PCI -DRT2860 -DRT28xx -DRT30xx -DRT35xx -DRTMP_PCI_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DA_BAND_SUPPORT -DSPECIFIC_VCORECAL_SUPPORT
ifeq ($(HAS_DFS_SUPPORT),y)
WFLAGS += -DDFS_HARDWARE_SUPPORT  -DDFS_DEBUG 
endif

CHIPSET_DAT = 2860
endif

ifeq ($(CHIPSET),3593)
WFLAGS +=-DRTMP_MAC_PCI -DDOT11N_SS3_SUPPORT -DNEW_RATE_ADAPT_SUPPORT -DRT3593 -DRT28xx -DRT30xx -DRT35xx -DRTMP_PCI_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DA_BAND_SUPPORT -DRTMP_FREQ_CALIBRATION_SUPPORT -DNEW_MBSSID_MODE -DSPECIFIC_BCN_BUF_SUPPORT
ifeq ($(HAS_DFS_SUPPORT),y)
WFLAGS += -DDFS_HARDWARE_SUPPORT  -DDFS_DEBUG
endif

CHIPSET_DAT = 2860
endif

ifeq ($(CHIPSET),3390)
WFLAGS +=-DRTMP_MAC_PCI -DRT30xx -DRT33xx -DRT3090 -DRT3390 -DRTMP_PCI_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DRTMP_INTERNAL_TX_ALC -DSPECIFIC_VCORECAL_SUPPORT
CHIPSET_DAT = 2860
endif

ifeq ($(CHIPSET),3370)
WFLAGS +=-DRTMP_MAC_USB -DRT30xx -DRT33xx -DRT3070 -DRT3370 -DRTMP_USB_SUPPORT -DRTMP_TIMER_TASK_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DRTMP_INTERNAL_TX_ALC -DSPECIFIC_VCORECAL_SUPPORT
CHIPSET_DAT = 2870
endif

ifeq ($(CHIPSET),5390)
WFLAGS +=-DRTMP_MAC_PCI -DRT30xx -DRT33xx -DRT3090 -DRT3390 -DRT5390 -DRTMP_PCI_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DSPECIFIC_BCN_BUF_SUPPORT -DRTMP_FREQ_CALIBRATION_SUPPORT -DRTMP_INTERNAL_TX_ALC -DSPECIFIC_VCORECAL_SUPPORT
CHIPSET_DAT = 2860
endif

ifeq ($(CHIPSET),5370)
WFLAGS +=-DRTMP_MAC_USB -DRT30xx -DRT33xx -DRT3070 -DRT3370 -DRT5370 -DRTMP_USB_SUPPORT -DRTMP_TIMER_TASK_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DRTMP_INTERNAL_TX_ALC -DSPECIFIC_BCN_BUF_SUPPORT -DRTMP_FREQ_CALIBRATION_SUPPORT -DSPECIFIC_VCORECAL_SUPPORT
CHIPSET_DAT = 2870
endif

ifeq ($(CHIPSET), 3052)
WFLAGS += -DRTMP_MAC_PCI -DRTMP_RBUS_SUPPORT -DRT3052 -DRT305x -DRTMP_RF_RW_SUPPORT -DCONFIG_SWMCU_SUPPORT -DSPECIFIC_VCORECAL_SUPPORT
CHIPSET_DAT = 2870
ifeq ($(HAS_WIFI_LED_SHARE), y)
WFLAGS += -DCONFIG_WIFI_LED_SHARE
endif
endif

ifeq ($(CHIPSET), 3352)
WFLAGS += -DRTMP_MAC_PCI -DRTMP_RBUS_SUPPORT -DRT3352 -DRT305x -DRTMP_RF_RW_SUPPORT -DSPECIFIC_BCN_BUF_SUPPORT -DVCORECAL_SUPPORT -DCONFIG_SWMCU_SUPPORT -DRTMP_INTERNAL_TX_ALC
CHIPSET_DAT = 2860
ifeq ($(HAS_WIFI_LED_SHARE), y)
WFLAGS += -DCONFIG_WIFI_LED_SHARE
endif
endif

ifeq ($(CHIPSET), 5350)
WFLAGS += -DRTMP_MAC_PCI -DRTMP_RBUS_SUPPORT -DRT5350 -DRT305x -DRT3050 -DRT3350 -DRTMP_RF_RW_SUPPORT -DSPECIFIC_BCN_BUF_SUPPORT -DVCORECAL_SUPPORT -DCONFIG_SWMCU_SUPPORT -DRTMP_INTERNAL_TX_ALC -DRTMP_FREQ_CALIBRATION_SUPPORT
CHIPSET_DAT = 2860
ifeq ($(HAS_WIFI_LED_SHARE), y)
WFLAGS += -DCONFIG_WIFI_LED_SHARE
endif
endif




ifeq ($(CHIPSET),USB)
#3572
WFLAGS +=-DRTMP_MAC_USB -DRTMP_USB_SUPPORT -DRT2870 -DRT28xx -DRT30xx -DRT35xx -DRTMP_TIMER_TASK_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DA_BAND_SUPPORT -DSPECIFIC_VCORECAL_SUPPORT
#3370
WFLAGS += -DRT33xx -DRT3070 -DRT3370 -DRTMP_TIMER_TASK_SUPPORT -DRTMP_INTERNAL_TX_ALC
CHIPSET_DAT = 2870
ifeq ($(HAS_DFS_SUPPORT),y)
WFLAGS += -DDFS_SOFTWARE_SUPPORT
endif
endif


ifeq ($(CHIPSET),PCI)
#3562		
WFLAGS +=-DRTMP_MAC_PCI -DRT2860 -DRT28xx -DRT30xx -DRT35xx -DRTMP_PCI_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DA_BAND_SUPPORT -DSPECIFIC_VCORECAL_SUPPORT
#3390
WFLAGS +=-DRT33xx -DRT3090 -DRT3390 -DRTMP_INTERNAL_TX_ALC
ifeq ($(HAS_DFS_SUPPORT),y)
WFLAGS += -DDFS_HARDWARE_SUPPORT  -DDFS_DEBUG 
endif
endif


ifeq ($(CHIPSET),RBUS)
WFLAGS += -DMERGE_ARCH_TEAM -DCONFIG_SWMCU_SUPPORT -DCONFIG_RA_NAT_NONE -DRTMP_RBUS_SUPPORT
#5350, 3050, 3350
WFLAGS +=-DRTMP_MAC_PCI -DRT305x -DRT5350 -DRT3050 -DRT3350 -DRTMP_PCI_SUPPORT -DRTMP_RF_RW_SUPPORT -DA_BAND_SUPPORT -DVCORECAL_SUPPORT -DSPECIFIC_BCN_BUF_SUPPORT
ifeq ($(HAS_DFS_SUPPORT),y)
WFLAGS += -DDFS_HARDWARE_SUPPORT  -DDFS_DEBUG 
endif
endif


#################################################


ifeq ($(PLATFORM),5VT)
#WFLAGS += -DCONFIG_5VT_ENHANCE
endif

ifeq ($(HAS_BLOCK_NET_IF),y)
WFLAGS += -DBLOCK_NET_IF
endif

ifeq ($(HAS_DFS_SUPPORT),y)
WFLAGS += -DDFS_SUPPORT
endif

ifeq ($(HAS_MC_SUPPORT),y)
WFLAGS += -DMULTIPLE_CARD_SUPPORT
endif

ifeq ($(HAS_LLTD),y)
WFLAGS += -DLLTD_SUPPORT
endif

ifeq ($(PLATFORM),RMI)
WFLAGS += -DRT_BIG_ENDIAN 
endif

ifeq ($(PLATFORM),BL2348)
WFLAGS += -DRT_BIG_ENDIAN
endif

ifeq ($(PLATFORM),BLUBB)
WFLAGS += -DRT_BIG_ENDIAN
endif

ifeq ($(PLATFORM),BLPMP)
WFLAGS += -DRT_BIG_ENDIAN
endif

ifeq ($(PLATFORM),RMI_64)
WFLAGS += -DRT_BIG_ENDIAN 
endif
ifeq ($(PLATFORM),IXP)
WFLAGS += -DRT_BIG_ENDIAN
endif

ifeq ($(PLATFORM),IKANOS_V160)
WFLAGS += -DRT_BIG_ENDIAN -DIKANOS_VX_1X0
endif

ifeq ($(PLATFORM),IKANOS_V180)
WFLAGS += -DRT_BIG_ENDIAN -DIKANOS_VX_1X0
endif

ifeq ($(PLATFORM),INF_TWINPASS)
WFLAGS += -DRT_BIG_ENDIAN -DINF_TWINPASS
endif

ifeq ($(PLATFORM),INF_DANUBE)
ifneq (,$(findstring 2.4,$(LINUX_SRC)))
# Linux 2.4
WFLAGS += -DINF_DANUBE -DRT_BIG_ENDIAN
else
# Linux 2.6
WFLAGS += -DRT_BIG_ENDIAN
endif
endif

ifeq ($(PLATFORM),INF_AR9)
WFLAGS += -DRT_BIG_ENDIAN -DINF_AR9
# support MAPI function for AR9.
#WFLAGS += -DAR9_MAPI_SUPPORT
endif

ifeq ($(PLATFORM),INF_VR9)
WFLAGS += -DRT_BIG_ENDIAN -DINF_AR9 -DINF_VR9
endif

ifeq ($(PLATFORM),CAVM_OCTEON)
WFLAGS += -DRT_BIG_ENDIAN
endif

ifeq ($(PLATFORM),BRCM_6358)
WFLAGS += -DRT_BIG_ENDIAN -DBRCM_6358
endif

ifeq ($(PLATFORM),INF_AMAZON_SE)
WFLAGS += -DRT_BIG_ENDIAN -DINF_AMAZON_SE
endif

ifeq ($(PLATFORM),RALINK_3052)
WFLAGS += -DPLATFORM_RALINK_3052
endif

ifeq ($(PLATFORM),FREESCALE8377)
#EXTRA_CFLAGS := -v -I$(RT28xx_DIR)/include -I$(LINUX_SRC)/include $(WFLAGS)-O2 -Wall -Wstrict-prototypes -Wno-trigraphs 
#export EXTRA_CFLAGS
WFLAGS += -DRT_BIG_ENDIAN
EXTRA_CFLAGS := $(WFLAGS) -I$(RT28xx_DIR)/include
endif

ifeq ($(PLATFORM),ST)
#WFLAGS += -DST
WFLAGS += -DST
endif

#kernel build options for 2.4
# move to Makefile outside LINUX_SRC := /opt/star/kernel/linux-2.4.27-star

ifeq ($(PLATFORM),RALINK_3052)
CFLAGS := -D__KERNEL__ -I$(LINUX_SRC)/include/asm-mips/mach-generic -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fno-strict-aliasing -fno-common -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe  -finline-limit=100000 -march=mips2 -mabi=32 -Wa,--trap -DLINUX -nostdinc -iwithprefix include $(WFLAGS)
export CFLAGS
endif

ifeq ($(PLATFORM), RALINK_2880)
CFLAGS := -D__KERNEL__ -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fno-strict-aliasing -fno-common -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe  -finline-limit=100000 -march=mips2 -mabi=32 -Wa,--trap -DLINUX -nostdinc -iwithprefix include $(WFLAGS)
export CFLAGS
endif

ifeq ($(PLATFORM),STAR)
CFLAGS := -D__KERNEL__ -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fno-strict-aliasing -fno-common -Uarm -fno-common -pipe -mapcs-32 -D__LINUX_ARM_ARCH__=4 -march=armv4  -mshort-load-bytes -msoft-float -Uarm -DMODULE -DMODVERSIONS -include $(LINUX_SRC)/include/linux/modversions.h $(WFLAGS)

export CFLAGS
endif

ifeq ($(PLATFORM),SIGMA)
CFLAGS := -D__KERNEL__ -I$(RT28xx_DIR)/include -I$(LINUX_SRC)/include -I$(LINUX_SRC)/include/asm/gcc -I$(LINUX_SRC)/include/asm-mips/mach-tango2 -I$(LINUX_SRC)/include/asm-mips/mach-tango2 -DEM86XX_CHIP=EM86XX_CHIPID_TANGO2 -DEM86XX_REVISION=6 -I$(LINUX_SRC)/include/asm-mips/mach-generic -I$(RT2860_DIR)/include -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -ffreestanding -O2     -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe  -mabi=32 -march=mips32r2 -Wa,-32 -Wa,-march=mips32r2 -Wa,-mips32r2 -Wa,--trap -DMODULE $(WFLAGS) -DSIGMA863X_PLATFORM

export CFLAGS
endif

ifeq ($(PLATFORM),SIGMA_8622)
CFLAGS := -D__KERNEL__ -I$(CROSS_COMPILE_INCLUDE)/include -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fno-strict-aliasing -fno-common -fno-common -pipe -fno-builtin -D__linux__ -DNO_MM -mapcs-32 -march=armv4 -mtune=arm7tdmi -msoft-float -DMODULE -mshort-load-bytes -nostdinc -iwithprefix -DMODULE $(WFLAGS)
export CFLAGS
endif

ifeq ($(PLATFORM),5VT)
CFLAGS := -D__KERNEL__ -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -mlittle-endian -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -O3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -fno-omit-frame-pointer -mapcs -mno-sched-prolog -mabi=apcs-gnu -mno-thumb-interwork -D__LINUX_ARM_ARCH__=5 -march=armv5te -mtune=arm926ej-s --param max-inline-insns-single=40000  -Uarm -Wdeclaration-after-statement -Wno-pointer-sign -DMODULE $(WFLAGS) 

export CFLAGS
endif

ifeq ($(PLATFORM),IKANOS_V160)
CFLAGS := -D__KERNEL__ -I$(LINUX_SRC)/include -I$(LINUX_SRC)/include/asm/gcc -I$(LINUX_SRC)/include/asm-mips/mach-tango2 -I$(LINUX_SRC)/include/asm-mips/mach-tango2 -I$(LINUX_SRC)/include/asm-mips/mach-generic -I$(RT28xx_DIR)/include -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -ffreestanding -O2 -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe -march=lx4189 -Wa, -DMODULE $(WFLAGS)
export CFLAGS
endif

ifeq ($(PLATFORM),IKANOS_V180)
CFLAGS := -D__KERNEL__ -I$(LINUX_SRC)/include -I$(LINUX_SRC)/include/asm/gcc -I$(LINUX_SRC)/include/asm-mips/mach-tango2 -I$(LINUX_SRC)/include/asm-mips/mach-tango2 -I$(LINUX_SRC)/include/asm-mips/mach-generic -I$(RT28xx_DIR)/include -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -ffreestanding -O2 -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe -mips32r2 -Wa, -DMODULE $(WFLAGS)
export CFLAGS
endif

ifeq ($(PLATFORM),INF_TWINPASS)
CFLAGS := -D__KERNEL__ -DMODULE -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fomit-frame-pointer -fno-strict-aliasing -fno-common -G 0 -mno-abicalls -fno-pic -march=4kc -mips32 -Wa,--trap -pipe -mlong-calls $(WFLAGS)
export CFLAGS
endif

ifeq ($(PLATFORM),INF_DANUBE)
	ifneq (,$(findstring 2.4,$(LINUX_SRC)))
	CFLAGS := -I$(RT28xx_DIR)/include $(WFLAGS) -Wundef -fno-strict-aliasing -fno-common -ffreestanding -Os -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe -msoft-float  -mabi=32 -march=mips32 -Wa,-32 -Wa,-march=mips32 -Wa,-mips32 -Wa,--trap -I$(LINUX_SRC)/include/asm-mips/mach-generic
	else
	CFLAGS := -I$(RT28xx_DIR)/include $(WFLAGS) -Wundef -fno-strict-aliasing -fno-common -ffreestanding -Os -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe -msoft-float  -mabi=32 -march=mips32r2 -Wa,-32 -Wa,-march=mips32r2 -Wa,-mips32r2 -Wa,--trap -I$(LINUX_SRC)/include/asm-mips/mach-generic
	endif
export CFLAGS
endif

ifeq ($(PLATFORM),INF_AR9)
CFLAGS := -I$(RT28xx_DIR)/include $(WFLAGS) -Wundef -fno-strict-aliasing -fno-common -fno-pic -ffreestanding -Os -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe -msoft-float  -mabi=32 -mlong-calls -march=mips32r2 -mtune=34kc -march=mips32r2 -Wa,-32 -Wa,-march=mips32r2 -Wa,-mips32r2 -Wa,--trap -I$(LINUX_SRC)/include/asm-mips/mach-generic
export CFLAGS
endif

ifeq ($(PLATFORM),INF_VR9)
CFLAGS := -I$(RT28xx_DIR)/include $(WFLAGS) -Wundef -fno-strict-aliasing -fno-common -fno-pic -ffreestanding -Os -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe -msoft-float  -mabi=32 -mlong-calls -march=mips32r2 -march=mips32r2 -Wa,-32 -Wa,-march=mips32r2 -Wa,-mips32r2 -Wa,--trap -I$(LINUX_SRC)/include/asm-mips/mach-generic
export CFLAGS
endif

ifeq ($(PLATFORM),BRCM_6358)
CFLAGS := $(WFLAGS) -I$(RT28xx_DIR)/include -nostdinc -iwithprefix include -D__KERNEL__ -Wall -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -I $(LINUX_SRC)/include/asm/gcc -G 0 -mno-abicalls -fno-pic -pipe  -finline-limit=100000 -mabi=32 -march=mips32 -Wa,-32 -Wa,-march=mips32 -Wa,-mips32 -Wa,--trap -I$(LINUX_SRC)/include/asm-mips/mach-bcm963xx -I$(LINUX_SRC)/include/asm-mips/mach-generic  -Os -fomit-frame-pointer -Wdeclaration-after-statement  -DMODULE -mlong-calls
export CFLAGS
endif

ifeq ($(PLATFORM),INF_AMAZON_SE)
CFLAGS := -D__KERNEL__ -DMODULE=1 -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fno-strict-aliasing -fno-common -DCONFIG_IFX_ALG_QOS -DCONFIG_WAN_VLAN_SUPPORT -fomit-frame-pointer -DIFX_PPPOE_FRAME -G 0 -fno-pic -mno-abicalls -mlong-calls -pipe -finline-limit=100000 -mabi=32 -march=mips32 -Wa,-32 -Wa,-march=mips32 -Wa,-mips32 -Wa,--trap -nostdinc -iwithprefix include $(WFLAGS)
export CFLAGS
endif

ifeq ($(PLATFORM),ST)
CFLAGS := -D__KERNEL__ -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -Wall -O2 -Wundef -Wstrict-prototypes -Wno-trigraphs -Wdeclaration-after-statement -Wno-pointer-sign -fno-strict-aliasing -fno-common -fomit-frame-pointer -ffreestanding -m4-nofpu -o $(WFLAGS) 
export CFLAGS
endif

ifeq ($(PLATFORM),PC)
    ifneq (,$(findstring 2.4,$(LINUX_SRC)))
	# Linux 2.4
	CFLAGS := -D__KERNEL__ -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -O2 -fomit-frame-pointer -fno-strict-aliasing -fno-common -pipe -mpreferred-stack-boundary=2 -march=i686 -DMODULE -DMODVERSIONS -include $(LINUX_SRC)/include/linux/modversions.h $(WFLAGS)
	export CFLAGS
    else
	# Linux 2.6
	EXTRA_CFLAGS := $(WFLAGS) -I$(RT28xx_DIR)/include
    endif
endif

#If the kernel version of RMI is newer than 2.6.27, please change "CFLAGS" to "EXTRA_FLAGS"
ifeq ($(PLATFORM),RMI)
EXTRA_CFLAGS := -D__KERNEL__ -DMODULE=1 -I$(LINUX_SRC)/include -I$(LINUX_SRC)/include/asm-mips/mach-generic  -I$(RT28xx_DIR)/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fno-strict-aliasing -fno-common -DCONFIG_IFX_ALG_QOS -DCONFIG_WAN_VLAN_SUPPORT -fomit-frame-pointer -DIFX_PPPOE_FRAME -G 0 -fno-pic -mno-abicalls -mlong-calls -pipe -finline-limit=100000 -mabi=32 -G 0 -mno-abicalls -fno-pic -pipe -msoft-float -march=xlr -ffreestanding  -march=xlr -Wa,--trap, -nostdinc -iwithprefix include $(WFLAGS)
export EXTRA_CFLAGS
endif

ifeq ($(PLATFORM),RMI_64)
EXTRA_CFLAGS := -D__KERNEL__ -DMODULE=1 -I$(LINUX_SRC)/include -I$(LINUX_SRC)/include/asm-mips/mach-generic  -I$(RT28xx_DIR)/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fno-strict-aliasing -fno-common -DCONFIG_IFX_ALG_QOS -DCONFIG_WAN_VLAN_SUPPORT -fomit-frame-pointer -DIFX_PPPOE_FRAME -G 0 -fno-pic -mno-abicalls -mlong-calls -pipe -finline-limit=100000 -mabi=64 -G 0 -mno-abicalls -fno-pic -pipe -msoft-float -march=xlr -ffreestanding  -march=xlr -Wa,--trap, -nostdinc -iwithprefix include $(WFLAGS)
export EXTRA_CFLAGS
endif

ifeq ($(PLATFORM),IXP)
	CFLAGS := -v -D__KERNEL__ -DMODULE -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -mbig-endian -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fno-strict-aliasing -fno-common -Uarm -fno-common -pipe -mapcs-32 -D__LINUX_ARM_ARCH__=5 -mcpu=xscale -mtune=xscale -malignment-traps -msoft-float $(WFLAGS)
        EXTRA_CFLAGS := -v $(WFLAGS) -I$(RT28xx_DIR)/include -mbig-endian
	export CFLAGS        
endif

ifeq ($(PLATFORM),SMDK)
        EXTRA_CFLAGS := $(WFLAGS) -I$(RT28xx_DIR)/include
endif

ifeq ($(PLATFORM),CAVM_OCTEON)
	EXTRA_CFLAGS := $(WFLAGS) -I$(RT28xx_DIR)/include \
				    -mabi=64 $(WFLAGS)
export CFLAGS
endif

ifeq ($(PLATFORM),DM6446)
	CFLAGS := -nostdinc -iwithprefix include -D__KERNEL__ -I$(RT28xx_DIR)/include -I$(LINUX_SRC)/include  -Wall -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Os -fno-omit-frame-pointer -fno-omit-frame-pointer -mapcs -mno-sched-prolog -mlittle-endian -mabi=apcs-gnu -D__LINUX_ARM_ARCH__=5 -march=armv5te -mtune=arm9tdmi -msoft-float -Uarm -Wdeclaration-after-statement -c -o $(WFLAGS)
export CFLAGS
endif

ifeq ($(PLATFORM),BL2348)
CFLAGS := -D__KERNEL__ -I$(RT28xx_DIR)/include -I$(LINUX_SRC)/include -I$(LINUX_SRC)/include/asm/gcc -I$(LINUX_SRC)/include/asm-mips/mach-tango2 -I$(LINUX_SRC)/include/asm-mips/mach-tango2 -DEM86XX_CHIP=EM86XX_CHIPID_TANGO2 -DEM86XX_REVISION=6 -I$(LINUX_SRC)/include/asm-mips/mach-generic -I$(RT2860_DIR)/include -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -ffreestanding -O2     -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe  -mabi=32 -march=mips32r2 -Wa,-32 -Wa,-march=mips32r2 -Wa,-mips32r2 -Wa,--trap -DMODULE $(WFLAGS) -DSIGMA863X_PLATFORM -DEXPORT_SYMTAB -DPLATFORM_BL2348
export CFLAGS
endif

ifeq ($(PLATFORM),BLUBB)
CFLAGS := -D__KERNEL__ -I$(RT28xx_DIR)/include -I$(LINUX_SRC)/include -I$(LINUX_SRC)/include/asm/gcc -I$(LINUX_SRC)/include/asm-mips/mach-tango2 -I$(LINUX_SRC)/include/asm-mips/mach-tango2 -DEM86XX_CHIP=EM86XX_CHIPID_TANGO2 -DEM86XX_REVISION=6 -I$(LINUX_SRC)/include/asm-mips/mach-generic -I$(RT2860_DIR)/include -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -ffreestanding -O2     -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe  -mabi=32 -march=mips32r2 -Wa,-32 -Wa,-march=mips32r2 -Wa,-mips32r2 -Wa,--trap -DMODULE $(WFLAGS) -DSIGMA863X_PLATFORM -DEXPORT_SYMTAB -DPLATFORM_BL2348
export CFLAGS
endif

ifeq ($(PLATFORM),BLPMP)
CFLAGS := -D__KERNEL__ -I$(RT28xx_DIR)/include -I$(LINUX_SRC)/include -I$(LINUX_SRC)/include/asm/gcc -I$(LINUX_SRC)/include/asm-mips/mach-tango2 -I$(LINUX_SRC)/include/asm-mips/mach-tango2 -DEM86XX_CHIP=EM86XX_CHIPID_TANGO2 -DEM86XX_REVISION=6 -I$(LINUX_SRC)/include/asm-mips/mach-generic -I$(RT2860_DIR)/include -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -ffreestanding -O2     -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe  -mabi=32 -march=mips32r2 -Wa,-32 -Wa,-march=mips32r2 -Wa,-mips32r2 -Wa,--trap -DMODULE $(WFLAGS) -DSIGMA863X_PLATFORM -DEXPORT_SYMTAB
export CFLAGS
endif

ifeq ($(PLATFORM),MT85XX)
    ifneq (,$(findstring 2.4,$(LINUX_SRC)))
	# Linux 2.4
	CFLAGS := -D__KERNEL__ -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -O2 -fomit-frame-pointer -fno-strict-aliasing -fno-common -pipe -mpreferred-stack-boundary=2 -march=i686 -DMODULE -DMODVERSIONS -include $(LINUX_SRC)/include/linux/modversions.h $(WFLAGS)
	export CFLAGS
    else
	# Linux 2.6
	EXTRA_CFLAGS += $(WFLAGS) -I$(RT28xx_DIR)/include
	EXTRA_CFLAGS += -D _NO_TYPEDEF_BOOL_ \
	                -D _NO_TYPEDEF_UCHAR_ \
	                -D _NO_TYPEDEF_UINT8_ \
	                -D _NO_TYPEDEF_UINT16_ \
	                -D _NO_TYPEDEF_UINT32_ \
	                -D _NO_TYPEDEF_UINT64_ \
	                -D _NO_TYPEDEF_CHAR_ \
	                -D _NO_TYPEDEF_INT32_ \
	                -D _NO_TYPEDEF_INT64_ \
	                
    endif
endif

ifeq ($(PLATFORM),NXP_TV550)
    ifneq (,$(findstring 2.4,$(LINUX_SRC)))
        # Linux 2.4
        CFLAGS := -D__KERNEL__ -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -O2 -fomit-frame-pointer -fno-strict-aliasing -fno-common -pipe -mpreferred-stack-boundary=2 -march=mips -DMODULE -DMODVERSIONS -include $(LINUX_SRC)/include/linux/modversions.h $(WFLAGS)
        export CFLAGS
    else
        # Linux 2.6
        EXTRA_CFLAGS := $(WFLAGS) -I$(RT28xx_DIR)/include
    endif
endif

ifeq ($(PLATFORM),MVL5)
CFLAGS := -D__KERNEL__ -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -mlittle-endian -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -O3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -fno-omit-frame-pointer -mapcs -mno-sched-prolog -mno-thumb-interwork -D__LINUX_ARM_ARCH__=5 -march=armv5te -mtune=arm926ej-s --param max-inline-insns-single=40000  -Uarm -Wdeclaration-after-statement -Wno-pointer-sign -DMODULE $(WFLAGS) 
export CFLAGS
endif

ifeq ($(PLATFORM),RALINK_3352)
CFLAGS := -D__KERNEL__ -I$(LINUX_SRC)/include/asm-mips/mach-generic -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fno-strict-aliasing -fno-common -fomit-frame-pointer -G 0 -mno-abicalls -fno-pic -pipe  -finline-limit=100000 -march=mips2 -mabi=32 -Wa,--trap -DLINUX -nostdinc -iwithprefix include $(WFLAGS)
export CFLAGS
endif

ifeq ($(PLATFORM),sunxi)
#	EXTRA_CFLAGS := $(WFLAGS) -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -I$(src)/include
	EXTRA_CFLAGS := $(WFLAGS) -O2  -Wno-unused-variable -Wno-unused-value -Wno-unused-label -Wno-unused-parameter -Wno-uninitialized -Wno-unused -Wno-unused-function  -I$(LINUX_SRC)/include -I$(RT28xx_DIR)/include -I$(src)/include -DCONFIG_LITTLE_ENDIAN
export EXTRA_CFLAGS
endif

#################################end of os/linux/config.mk####################################



###############################start of os/linux/Makefile.6####################################
#include $(RT28xx_DIR)/os/linux/config.mk


#ifdef CONFIG_STA_SUPPORT
ifeq ($(RT28xx_MODE), STA)
MOD_NAME = rt$(CHIPSET)sta
DAT_PATH = /etc/Wireless/RT$(CHIPSET_DAT)STA
DAT_FILE_NAME = RT$(CHIPSET_DAT)STA.dat
endif
#endif // CONFIG_STA_SUPPORT //


obj-$(CONFIG_RTXX7X_SW) := $(MOD_NAME).o

#ifdef CONFIG_STA_SUPPORT
rt$(CHIPSET)sta-objs := \
	common/crypt_md5.o\
	common/crypt_sha2.o\
	common/crypt_hmac.o\
	common/crypt_aes.o\
	common/crypt_arc4.o\
	common/mlme.o\
	common/cmm_wep.o\
	common/action.o\
	common/cmm_data.o\
	common/rtmp_init.o\
	common/rtmp_init_inf.o\
	common/cmm_tkip.o\
	common/cmm_aes.o\
	common/cmm_sync.o\
	common/eeprom.o\
	common/cmm_sanity.o\
	common/cmm_info.o\
	common/cmm_cfg.o\
	common/cmm_wpa.o\
	common/dfs.o\
	common/spectrum.o\
	common/rtmp_timer.o\
	common/rt_channel.o\
	common/cmm_profile.o\
	common/cmm_asic.o\
	common/cmm_cmd.o\
	os/linux/rt_profile.o\
	chips/rtmp_chip.o\
	sta/assoc.o\
	sta/auth.o\
	sta/auth_rsp.o\
	sta/sync.o\
	sta/sanity.o\
	sta/rtmp_data.o\
	sta/connect.o\
	sta/wpa.o\
	sta/ags.o\
	sta/sta_cfg.o

ifeq ($(OSABL),NO)
rt$(CHIPSET)sta-objs += \
	common/rt_os_util.o\
	os/linux/sta_ioctl.o\
	os/linux/rt_linux.o\
	os/linux/rt_main_dev.o
else
rt$(CHIPSET)sta-objs += \
	os/linux/rt_symb.o
endif

#ifdef DOT11_N_SUPPORT
ifeq ($(HAS_DOT11_N_SUPPORT),y)
rt$(CHIPSET)sta-objs += \
	common/ba_action.o
endif
#endif // DOT11_N_SUPPORT //

#ifdef ETH_CONVERT
ifeq ($(HAS_ETH_CONVERT_SUPPORT), y)
rt$(CHIPSET)sta-objs += \
	common/cmm_mat.o \
	common/cmm_mat_iparp.o \
	common/cmm_mat_pppoe.o \
	common/cmm_mat_ipv6.o
endif
#endif // ETH_CONVERT //





ifeq ($(HAS_BLOCK_NET_IF),y)
rt$(CHIPSET)sta-objs += common/netif_block.o
endif

ifeq ($(HAS_QOS_DLS_SUPPORT),y)
rt$(CHIPSET)sta-objs += sta/dls.o
endif



#ifdef LED_CONTROL_SUPPORT
ifeq ($(HAS_LED_CONTROL_SUPPORT),y)
rt$(CHIPSET)sta-objs += \
	common/rt_led.o
endif
#endif // LED_CONTROL_SUPPORT //



#ifdef RT5350
ifeq ($(CHIPSET),5350)
rt$(CHIPSET)sta-objs += \
	common/cmm_mac_pci.o\
	common/cmm_data_pci.o\
	common/ee_flash.o\
	common/rtmp_swmcu.o\
	common/rt_rf.o\
	chips/rt305x.o\
	chips/rt5350.o

ifeq ($(OSABL),NO)
rt$(CHIPSET)sta-objs += \
	os/linux/rt_pci_rbus.o\
	os/linux/rt_proc.o\
	os/linux/rt_rbus_pci_util.o\
	os/linux/rt_rbus_pci_drv.o\
	os/linux/rbus_main_dev.o
endif
endif
#endif // RT5350 //



#ifdef RT3070
ifeq ($(CHIPSET),2070)
rt$(CHIPSET)sta-objs += \
	common/cmm_mac_usb.o\
	common/rtusb_io.o\
	common/rtusb_data.o\
	common/cmm_data_usb.o\
	common/ee_prom.o\
	common/ee_efuse.o\
	common/rtmp_mcu.o\
	chips/rt30xx.o\
	common/rt_rf.o\
	common/rtusb_bulk.o\
	os/linux/rt_usb.o\
	chips/rt3070.o

ifeq ($(OSABL),NO)
rt$(CHIPSET)sta-objs += \
	common/rtusb_dev_id.o\
	os/linux/rt_usb_util.o\
	os/linux/usb_main_dev.o
endif
endif

ifeq ($(CHIPSET),3070)
rt$(CHIPSET)sta-objs += \
	common/cmm_mac_usb.o\
	common/rtusb_io.o\
	common/rtusb_data.o\
	common/cmm_data_usb.o\
	common/ee_prom.o\
	common/ee_efuse.o\
	common/rtmp_mcu.o\
	chips/rt30xx.o\
	common/rt_rf.o\
	common/rtusb_bulk.o\
	os/linux/rt_usb.o\
	chips/rt3070.o

ifeq ($(OSABL),NO)
rt$(CHIPSET)sta-objs += \
	common/rtusb_dev_id.o\
	os/linux/rt_usb_util.o\
	os/linux/usb_main_dev.o
endif
endif
#endif // RT3070 //




#ifdef RT3370
ifeq ($(CHIPSET),3370)
rt$(CHIPSET)sta-objs += \
	common/cmm_mac_usb.o\
	common/rtusb_io.o\
	common/rtusb_data.o\
	common/cmm_data_usb.o\
	common/rtusb_bulk.o\
	common/ee_prom.o\
	common/ee_efuse.o\
	common/rtmp_mcu.o\
	common/rt_rf.o\
	chips/rt3070.o\
	chips/rt30xx.o\
	chips/rt33xx.o\
	chips/rt3370.o

ifeq ($(OSABL),NO)
rt$(CHIPSET)sta-objs += \
	common/rtusb_dev_id.o\
	os/linux/rt_usb.o\
	os/linux/rt_usb_util.o\
	os/linux/usb_main_dev.o
endif
endif
#endif // RT3370 //


#ifdef RT5370
ifeq ($(CHIPSET),5370)
rt$(CHIPSET)sta-objs += \
        common/cmm_mac_usb.o\
        common/rtusb_io.o\
        common/rtusb_data.o\
        common/cmm_data_usb.o\
        common/rtusb_bulk.o\
        common/ee_prom.o\
        common/ee_efuse.o\
        common/rtmp_mcu.o\
        common/rt_rf.o\
	os/linux/rt_usb.o\
	common/frq_cal.o\
        chips/rt3070.o\
        chips/rt30xx.o\
        chips/rt33xx.o\
        chips/rt3370.o\
	chips/rt5390.o

ifeq ($(OSABL),NO)
rt$(CHIPSET)sta-objs += \
        common/rtusb_dev_id.o\
        os/linux/rt_usb_util.o\
        os/linux/usb_main_dev.o
endif
endif
#endif // RT5370 //

ifeq ($(CHIPSET),PCI)
rt$(CHIPSET)sta-objs += \
	common/cmm_mac_pci.o\
	common/cmm_data_pci.o\
	os/linux/rt_rbus_pci_drv.o\
	common/rtmp_mcu.o\
	common/ee_prom.o\
	common/ee_efuse.o\
	common/rt_rf.o\
	chips/rt28xx.o\
	chips/rt30xx.o\
	chips/rt35xx.o\
	chips/rt3090.o\
	chips/rt33xx.o\
	chips/rt3390.o\
	chips/rt5390.o

ifeq ($(OSABL),NO)
rt$(CHIPSET)sta-objs += \
	os/linux/rt_pci_rbus.o\
	os/linux/rt_rbus_pci_util.o\
	os/linux/pci_main_dev.o
endif
endif

ifeq ($(CHIPSET),USB)
rt$(CHIPSET)sta-objs += \
	common/cmm_mac_usb.o\
	common/rtusb_io.o\
	common/rtusb_data.o\
	common/cmm_data_usb.o\
	common/ee_prom.o\
	common/ee_efuse.o\
	common/rtmp_mcu.o\
	common/rt_rf.o\
	common/rtusb_bulk.o\
	os/linux/rt_usb.o\
	chips/rt28xx.o\
	chips/rt3070.o\
	chips/rt30xx.o\
	chips/rt33xx.o\
	chips/rt35xx.o\
	chips/rt3370.o\
	chips/rt5390.o

ifeq ($(OSABL),NO)
rt$(CHIPSET)sta-objs += \
	common/rtusb_dev_id.o\
	os/linux/rt_usb_util.o\
	os/linux/usb_main_dev.o
endif
endif

ifeq ($(HAS_ATE),y)
rt$(CHIPSET)sta-objs += common/rt_ate.o
endif







#ifdef CRDA_SUPPORT
ifeq ($(OSABL),NO)
ifeq ($(HAS_CFG80211_SUPPORT),y)
rt$(CHIPSET)sta-objs += \
	os/linux/cfg80211.o \
	os/linux/cfg80211drv.o
endif
endif

ifeq ($(OSABL),YES)
ifeq ($(HAS_CFG80211_SUPPORT),y)
rt$(CHIPSET)sta-objs += \
	os/linux/cfg80211drv.o
endif
endif
#endif // CRDA_SUPPORT //


#endif // CONFIG_STA_SUPPORT //


#################################end of os/linux/Makefile.6####################################

# add souce here end
else
# make in the directory,origin


export OSABL RT28xx_DIR RT28xx_MODE LINUX_SRC CROSS_COMPILE CROSS_COMPILE_INCLUDE PLATFORM RELEASE CHIPSET RTMP_SRC_DIR LINUX_SRC_MODULE TARGET

# The targets that may be used.
PHONY += all build_tools test UCOS THREADX LINUX release prerelease clean uninstall install libwapi osabl

ifeq ($(TARGET),LINUX)
all: build_tools $(TARGET)
else
all: $(TARGET)
endif 



build_tools:
	$(MAKE) -C tools
	$(RT28xx_DIR)/tools/bin2h

test:
	$(MAKE) -C tools test

UCOS:
	$(MAKE) -C os/ucos/ MODE=$(RT28xx_MODE)
	echo $(RT28xx_MODE)

ECOS:
	$(MAKE) -C os/ecos/ MODE=$(RT28xx_MODE)
	cp -f os/ecos/$(MODULE) $(MODULE)

THREADX:
	$(MAKE) -C $(RT28xx_DIR)/os/Threadx -f $(RT28xx_DIR)/os/ThreadX/Makefile

LINUX:
ifneq (,$(findstring 2.4,$(LINUX_SRC)))

ifeq ($(OSABL),YES)
	cp -f os/linux/Makefile.4.util $(RT28xx_DIR)/os/linux/Makefile
	$(MAKE) -C $(RT28xx_DIR)/os/linux/
endif

	cp -f os/linux/Makefile.4 $(RT28xx_DIR)/os/linux/Makefile
	$(MAKE) -C $(RT28xx_DIR)/os/linux/

ifeq ($(OSABL),YES)
	cp -f os/linux/Makefile.4.netif $(RT28xx_DIR)/os/linux/Makefile
	$(MAKE) -C $(RT28xx_DIR)/os/linux/
endif

ifeq ($(RT28xx_MODE),AP)
	cp -f $(RT28xx_DIR)/os/linux/rt$(CHIPSET)ap.o /tftpboot
ifeq ($(OSABL),YES)
	cp -f $(RT28xx_DIR)/os/linux/rtutil$(CHIPSET)ap.o /tftpboot
	cp -f $(RT28xx_DIR)/os/linux/rtnet$(CHIPSET)ap.o /tftpboot
endif
ifeq ($(PLATFORM),INF_AMAZON_SE)
	cp -f /tftpboot/rt2870ap.o /backup/ifx/build/root_filesystem/lib/modules/2.4.31-Amazon_SE-3.6.2.2-R0416_Ralink/kernel/drivers/net
endif
else	
ifeq ($(RT28xx_MODE),APSTA)
	cp -f $(RT28xx_DIR)/os/linux/rt$(CHIPSET)apsta.o /tftpboot
ifeq ($(OSABL),YES)
	cp -f $(RT28xx_DIR)/os/linux/rtutil$(CHIPSET)apsta.o /tftpboot
	cp -f $(RT28xx_DIR)/os/linux/rtnet$(CHIPSET)apsta.o /tftpboot
endif
else
	cp -f $(RT28xx_DIR)/os/linux/rt$(CHIPSET)sta.o /tftpboot
ifeq ($(OSABL),YES)
	cp -f $(RT28xx_DIR)/os/linux/rtutil$(CHIPSET)sta.o /tftpboot
	cp -f $(RT28xx_DIR)/os/linux/rtnet$(CHIPSET)sta.o /tftpboot
endif
endif	
endif	
else

ifeq ($(OSABL),YES)
	cp -f os/linux/Makefile.6.util $(RT28xx_DIR)/os/linux/Makefile
ifeq ($(PLATFORM),IMX51)
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
ifeq ($(PLATFORM),Telechips)
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
	$(MAKE) -C $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
endif
endif
endif

	cp -f os/linux/Makefile.6 $(RT28xx_DIR)/os/linux/Makefile
ifeq ($(PLATFORM),DM6446)
	$(MAKE)  ARCH=arm CROSS_COMPILE=arm_v5t_le- -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
ifeq ($(PLATFORM),FREESCALE8377)
	$(MAKE) ARCH=powerpc CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
ifeq ($(PLATFORM),IMX51)
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
ifeq ($(PLATFORM),Telechips)
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
	$(MAKE) -C $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
endif
endif
endif
endif

ifeq ($(OSABL),YES)
	cp -f os/linux/Makefile.6.netif $(RT28xx_DIR)/os/linux/Makefile
ifeq ($(PLATFORM),IMX51)
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
ifeq ($(PLATFORM),Telechips)
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
	$(MAKE) -C $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
endif
endif
endif

ifeq ($(RT28xx_MODE),AP)
	cp -f $(RT28xx_DIR)/os/linux/rt$(CHIPSET)ap.ko /tftpboot
ifeq ($(OSABL),YES)
	cp -f $(RT28xx_DIR)/os/linux/rtutil$(CHIPSET)ap.ko /tftpboot
	cp -f $(RT28xx_DIR)/os/linux/rtnet$(CHIPSET)ap.ko /tftpboot
endif
	rm -f os/linux/rt$(CHIPSET)ap.ko.lzma
	/root/bin/lzma e os/linux/rt$(CHIPSET)ap.ko os/linux/rt$(CHIPSET)ap.ko.lzma
else	
ifeq ($(RT28xx_MODE),APSTA)
	cp -f $(RT28xx_DIR)/os/linux/rt$(CHIPSET)apsta.ko /tftpboot
ifeq ($(OSABL),YES)
	cp -f $(RT28xx_DIR)/os/linux/rtutil$(CHIPSET)apsta.ko /tftpboot
	cp -f $(RT28xx_DIR)/os/linux/rtnet$(CHIPSET)apsta.ko /tftpboot
endif
else
	cp -f $(RT28xx_DIR)/os/linux/rt$(CHIPSET)sta.ko $(RT28xx_DIR)
ifeq ($(OSABL),YES)
	cp -f $(RT28xx_DIR)/os/linux/rtutil$(CHIPSET)sta.ko /tftpboot
	cp -f $(RT28xx_DIR)/os/linux/rtnet$(CHIPSET)sta.ko /tftpboot
endif
endif
endif
endif


release:
	$(MAKE) -C $(RT28xx_DIR)/striptool -f Makefile.release clean
	$(MAKE) -C $(RT28xx_DIR)/striptool -f Makefile.release
	striptool/striptool.out
ifeq ($(RELEASE), DPO)
	gcc -o striptool/banner striptool/banner.c
	./striptool/banner -b striptool/copyright.gpl -s DPO/ -d DPO_GPL -R
	./striptool/banner -b striptool/copyright.frm -s DPO_GPL/include/firmware.h
endif

prerelease:
ifeq ($(CHIPSET), 2880)
	$(MAKE) -C $(RT28xx_DIR)/os/linux -f Makefile.release.2880 prerelease
else
	$(MAKE) -C $(RT28xx_DIR)/os/linux -f Makefile.release prerelease
endif
	cp $(RT28xx_DIR)/os/linux/Makefile.DPB $(RTMP_SRC_DIR)/os/linux/.
	cp $(RT28xx_DIR)/os/linux/Makefile.DPA $(RTMP_SRC_DIR)/os/linux/.
	cp $(RT28xx_DIR)/os/linux/Makefile.DPC $(RTMP_SRC_DIR)/os/linux/.
ifeq ($(RT28xx_MODE),STA)
	cp $(RT28xx_DIR)/os/linux/Makefile.DPD $(RTMP_SRC_DIR)/os/linux/.
	cp $(RT28xx_DIR)/os/linux/Makefile.DPO $(RTMP_SRC_DIR)/os/linux/.
endif	

clean:
ifeq ($(TARGET), LINUX)
	cp -f os/linux/Makefile.clean os/linux/Makefile
	$(MAKE) -C os/linux clean
	rm -rf os/linux/Makefile
	rm -f *.o
	rm -f .*.o.cmd
	rm -f .*.*.o.cmd
	rm -f .*.ko.cmd
	rm -f *.ko	
	rm -f *.mod.o
	rm -f *.mod.c
endif	
ifeq ($(TARGET), UCOS)
	$(MAKE) -C os/ucos clean MODE=$(RT28xx_MODE)
endif
ifeq ($(TARGET), ECOS)
	$(MAKE) -C os/ecos clean MODE=$(RT28xx_MODE)
endif

uninstall:
ifeq ($(TARGET), LINUX)
ifneq (,$(findstring 2.4,$(LINUX_SRC)))
	$(MAKE) -C $(RT28xx_DIR)/os/linux -f Makefile.4 uninstall
else
	$(MAKE) -C $(RT28xx_DIR)/os/linux -f Makefile.6 uninstall
endif
endif

install:
ifeq ($(TARGET), LINUX)
ifneq (,$(findstring 2.4,$(LINUX_SRC)))
	$(MAKE) -C $(RT28xx_DIR)/os/linux -f Makefile.4 install
else
	$(MAKE) -C $(RT28xx_DIR)/os/linux -f Makefile.6 install
endif
endif

libwapi:
ifneq (,$(findstring 2.4,$(LINUX_SRC)))
	cp -f os/linux/Makefile.libwapi.4 $(RT28xx_DIR)/os/linux/Makefile
	$(MAKE) -C $(RT28xx_DIR)/os/linux/
else
	cp -f os/linux/Makefile.libwapi.6 $(RT28xx_DIR)/os/linux/Makefile	
	$(MAKE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules	
endif	

osutil:
ifeq ($(OSABL),YES)
ifneq (,$(findstring 2.4,$(LINUX_SRC)))
	cp -f os/linux/Makefile.4.util $(RT28xx_DIR)/os/linux/Makefile
	$(MAKE) -C $(RT28xx_DIR)/os/linux/
else
	cp -f os/linux/Makefile.6.util $(RT28xx_DIR)/os/linux/Makefile
ifeq ($(PLATFORM),IMX51)
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
ifeq ($(PLATFORM),Telechips)
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
	$(MAKE) -C $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
endif
endif
endif
endif

osnet:
ifeq ($(OSABL),YES)
ifneq (,$(findstring 2.4,$(LINUX_SRC)))
	cp -f os/linux/Makefile.4.netif $(RT28xx_DIR)/os/linux/Makefile
	$(MAKE) -C $(RT28xx_DIR)/os/linux/
else
	cp -f os/linux/Makefile.6.netif $(RT28xx_DIR)/os/linux/Makefile
ifeq ($(PLATFORM),IMX51)
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
ifeq ($(PLATFORM),Telechips)
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
	$(MAKE) -C $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
endif
endif
endif
endif

osdrv:
ifneq (,$(findstring 2.4,$(LINUX_SRC)))
	cp -f os/linux/Makefile.4 $(RT28xx_DIR)/os/linux/Makefile
	$(MAKE) -C $(RT28xx_DIR)/os/linux/
else
	cp -f os/linux/Makefile.6 $(RT28xx_DIR)/os/linux/Makefile
ifeq ($(PLATFORM),IMX51)
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
ifeq ($(PLATFORM),Telechips)
	$(MAKE) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) -C  $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
else
	$(MAKE) -C $(LINUX_SRC) SUBDIRS=$(RT28xx_DIR)/os/linux modules
endif
endif
endif

# Declare the contents of the .PHONY variable as phony.  We keep that information in a variable
.PHONY: $(PHONY)



endif
#end of ifneq ($(KERNELRELEASE),)
