# Support ATE function
HAS_ATE=y

# Support 28xx QA ATE function
HAS_28xx_QA=n


HAS_NINTENDO=n

# Support LLTD function
HAS_LLTD=n

# Support WDS function
HAS_WDS=n

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

# Support for STA Ethernet Converter
HAS_ETH_CONVERT_SUPPORT=n

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
HAS_DOT11N_DRAFT3_SUPPORT=n

#Support features of Single SKU.
HAS_SINGLE_SKU_SUPPORT=n

#Support features of 802.11n
HAS_DOT11_N_SUPPORT=y



#Support for 2860/2880 co-exist
HAS_RT2880_RT2860_COEXIST=n

HAS_KTHREAD_SUPPORT=n


#Support for Auto channel select enhance
HAS_AUTO_CH_SELECT_ENHANCE=n

#Support bypass bridge
HAS_BG_FT_SUPPORT=n

#Support Antenna Diversity
HAS_ANTENNA_DIVERSITY_SUPPORT=y
#################################################

WFLAGS := -DAGGREGATION_SUPPORT -DPIGGYBACK_SUPPORT -DWMM_SUPPORT  -DLINUX -Wall -Wstrict-prototypes -Wno-trigraphs -Wpointer-sign

ifeq ($(HAS_KTHREAD_SUPPORT),y)
WFLAGS += -DKTHREAD_SUPPORT
endif


#################################################

# config for STA mode

WFLAGS += -DCONFIG_STA_SUPPORT -DDBG

ifeq ($(HAS_WPA_SUPPLICANT),y)
WFLAGS += -DWPA_SUPPLICANT_SUPPORT
ifeq ($(HAS_NATIVE_WPA_SUPPLICANT_SUPPORT),y)
WFLAGS += -DNATIVE_WPA_SUPPLICANT_SUPPORT
endif
endif


ifeq ($(HAS_ETH_CONVERT_SUPPORT), y)
WFLAGS += -DETH_CONVERT_SUPPORT  -DMAT_SUPPORT
endif

ifeq ($(HAS_ATE),y)
WFLAGS += -DRALINK_ATE
ifeq ($(HAS_28xx_QA),y)
WFLAGS += -DRALINK_28xx_QA
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
endif

ifeq ($(HAS_CS_SUPPORT),y)
WFLAGS += -DCARRIER_DETECTION_SUPPORT
endif

ifeq ($(HAS_ANTENNA_DIVERSITY_SUPPORT),y)
WFLAGS += -DANT_DIVERSITY_SUPPORT
endif

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


#################################################
# ChipSet specific definitions.
#
WFLAGS +=-DRTMP_MAC_PCI -DRT30xx -DRT3090  -DRTMP_PCI_SUPPORT -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT
#################################################


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

EXTRA_CFLAGS := $(WFLAGS)
