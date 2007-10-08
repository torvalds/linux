/**
  * This file contains definitions of WLAN commands.
  */

#ifndef _HOST_H_
#define _HOST_H_

/** PUBLIC DEFINITIONS */
#define DEFAULT_AD_HOC_CHANNEL       6
#define DEFAULT_AD_HOC_CHANNEL_A    36

/** IEEE 802.11 oids */
#define OID_802_11_SSID                       0x00008002
#define OID_802_11_INFRASTRUCTURE_MODE        0x00008008
#define OID_802_11_FRAGMENTATION_THRESHOLD    0x00008009
#define OID_802_11_RTS_THRESHOLD              0x0000800A
#define OID_802_11_TX_ANTENNA_SELECTED        0x0000800D
#define OID_802_11_SUPPORTED_RATES            0x0000800E
#define OID_802_11_STATISTICS                 0x00008012
#define OID_802_11_TX_RETRYCOUNT              0x0000801D
#define OID_802_11D_ENABLE                    0x00008020

#define CMD_OPTION_WAITFORRSP             0x0002

/** Host command IDs */

/* Return command are almost always the same as the host command, but with
 * bit 15 set high.  There are a few exceptions, though...
 */
#define CMD_RET(cmd)			(0x8000 | cmd)

/* Return command convention exceptions: */
#define CMD_RET_802_11_ASSOCIATE      0x8012

/* Command codes */
#define CMD_CODE_DNLD                 0x0002
#define CMD_GET_HW_SPEC               0x0003
#define CMD_EEPROM_UPDATE             0x0004
#define CMD_802_11_RESET              0x0005
#define CMD_802_11_SCAN               0x0006
#define CMD_802_11_GET_LOG            0x000b
#define CMD_MAC_MULTICAST_ADR         0x0010
#define CMD_802_11_AUTHENTICATE       0x0011
#define CMD_802_11_EEPROM_ACCESS      0x0059
#define CMD_802_11_ASSOCIATE          0x0050
#define CMD_802_11_SET_WEP            0x0013
#define CMD_802_11_GET_STAT           0x0014
#define CMD_802_3_GET_STAT            0x0015
#define CMD_802_11_SNMP_MIB           0x0016
#define CMD_MAC_REG_MAP               0x0017
#define CMD_BBP_REG_MAP               0x0018
#define CMD_MAC_REG_ACCESS            0x0019
#define CMD_BBP_REG_ACCESS            0x001a
#define CMD_RF_REG_ACCESS             0x001b
#define CMD_802_11_RADIO_CONTROL      0x001c
#define CMD_802_11_RF_CHANNEL         0x001d
#define CMD_802_11_RF_TX_POWER        0x001e
#define CMD_802_11_RSSI               0x001f
#define CMD_802_11_RF_ANTENNA         0x0020

#define CMD_802_11_PS_MODE	      0x0021

#define CMD_802_11_DATA_RATE          0x0022
#define CMD_RF_REG_MAP                0x0023
#define CMD_802_11_DEAUTHENTICATE     0x0024
#define CMD_802_11_REASSOCIATE        0x0025
#define CMD_802_11_DISASSOCIATE       0x0026
#define CMD_MAC_CONTROL               0x0028
#define CMD_802_11_AD_HOC_START       0x002b
#define CMD_802_11_AD_HOC_JOIN        0x002c

#define CMD_802_11_QUERY_TKIP_REPLY_CNTRS  0x002e
#define CMD_802_11_ENABLE_RSN              0x002f
#define CMD_802_11_PAIRWISE_TSC       0x0036
#define CMD_802_11_GROUP_TSC          0x0037
#define CMD_802_11_KEY_MATERIAL       0x005e

#define CMD_802_11_SET_AFC            0x003c
#define CMD_802_11_GET_AFC            0x003d

#define CMD_802_11_AD_HOC_STOP        0x0040

#define CMD_802_11_BEACON_STOP        0x0049

#define CMD_802_11_MAC_ADDRESS        0x004D
#define CMD_802_11_EEPROM_ACCESS      0x0059

#define CMD_802_11_BAND_CONFIG        0x0058

#define CMD_802_11D_DOMAIN_INFO       0x005b

#define CMD_802_11_SLEEP_PARAMS          0x0066

#define CMD_802_11_INACTIVITY_TIMEOUT    0x0067

#define CMD_802_11_TPC_CFG               0x0072
#define CMD_802_11_PWR_CFG               0x0073

#define CMD_802_11_LED_GPIO_CTRL         0x004e

#define CMD_802_11_SUBSCRIBE_EVENT       0x0075

#define CMD_802_11_RATE_ADAPT_RATESET    0x0076

#define CMD_802_11_TX_RATE_QUERY	0x007f

#define CMD_GET_TSF                      0x0080

#define CMD_BT_ACCESS                 0x0087

#define CMD_FWT_ACCESS                0x0095

#define CMD_802_11_MONITOR_MODE       0x0098

#define CMD_MESH_ACCESS               0x009b

#define CMD_SET_BOOT2_VER                 0x00a5

/* For the IEEE Power Save */
#define CMD_SUBCMD_ENTER_PS               0x0030
#define CMD_SUBCMD_EXIT_PS                0x0031
#define CMD_SUBCMD_SLEEP_CONFIRMED        0x0034
#define CMD_SUBCMD_FULL_POWERDOWN         0x0035
#define CMD_SUBCMD_FULL_POWERUP           0x0036

#define CMD_ENABLE_RSN                    0x0001
#define CMD_DISABLE_RSN                   0x0000

#define CMD_ACT_SET                       0x0001
#define CMD_ACT_GET                       0x0000

#define CMD_ACT_GET_AES                   (CMD_ACT_GET + 2)
#define CMD_ACT_SET_AES                   (CMD_ACT_SET + 2)
#define CMD_ACT_REMOVE_AES                (CMD_ACT_SET + 3)

/* Define action or option for CMD_802_11_SET_WEP */
#define CMD_ACT_ADD                         0x0002
#define CMD_ACT_REMOVE                      0x0004
#define CMD_ACT_USE_DEFAULT                 0x0008

#define CMD_TYPE_WEP_40_BIT                 0x01
#define CMD_TYPE_WEP_104_BIT                0x02

#define CMD_NUM_OF_WEP_KEYS                 4

#define CMD_WEP_KEY_INDEX_MASK              0x3fff

/* Define action or option for CMD_802_11_RESET */
#define CMD_ACT_HALT                        0x0003

/* Define action or option for CMD_802_11_SCAN */
#define CMD_BSS_TYPE_BSS                    0x0001
#define CMD_BSS_TYPE_IBSS                   0x0002
#define CMD_BSS_TYPE_ANY                    0x0003

/* Define action or option for CMD_802_11_SCAN */
#define CMD_SCAN_TYPE_ACTIVE                0x0000
#define CMD_SCAN_TYPE_PASSIVE               0x0001

#define CMD_SCAN_RADIO_TYPE_BG		0

#define CMD_SCAN_PROBE_DELAY_TIME           0

/* Define action or option for CMD_MAC_CONTROL */
#define CMD_ACT_MAC_RX_ON                   0x0001
#define CMD_ACT_MAC_TX_ON                   0x0002
#define CMD_ACT_MAC_LOOPBACK_ON             0x0004
#define CMD_ACT_MAC_WEP_ENABLE              0x0008
#define CMD_ACT_MAC_INT_ENABLE              0x0010
#define CMD_ACT_MAC_MULTICAST_ENABLE        0x0020
#define CMD_ACT_MAC_BROADCAST_ENABLE        0x0040
#define CMD_ACT_MAC_PROMISCUOUS_ENABLE      0x0080
#define CMD_ACT_MAC_ALL_MULTICAST_ENABLE    0x0100
#define CMD_ACT_MAC_STRICT_PROTECTION_ENABLE  0x0400

/* Define action or option for CMD_802_11_RADIO_CONTROL */
#define CMD_TYPE_AUTO_PREAMBLE              0x0001
#define CMD_TYPE_SHORT_PREAMBLE             0x0002
#define CMD_TYPE_LONG_PREAMBLE              0x0003

#define TURN_ON_RF                              0x01
#define RADIO_ON                                0x01
#define RADIO_OFF                               0x00

#define SET_AUTO_PREAMBLE                       0x05
#define SET_SHORT_PREAMBLE                      0x03
#define SET_LONG_PREAMBLE                       0x01

/* Define action or option for CMD_802_11_RF_CHANNEL */
#define CMD_OPT_802_11_RF_CHANNEL_GET       0x00
#define CMD_OPT_802_11_RF_CHANNEL_SET       0x01

/* Define action or option for CMD_802_11_RF_TX_POWER */
#define CMD_ACT_TX_POWER_OPT_GET            0x0000
#define CMD_ACT_TX_POWER_OPT_SET_HIGH       0x8007
#define CMD_ACT_TX_POWER_OPT_SET_MID        0x8004
#define CMD_ACT_TX_POWER_OPT_SET_LOW        0x8000

#define CMD_ACT_TX_POWER_INDEX_HIGH         0x0007
#define CMD_ACT_TX_POWER_INDEX_MID          0x0004
#define CMD_ACT_TX_POWER_INDEX_LOW          0x0000

/* Define action or option for CMD_802_11_DATA_RATE */
#define CMD_ACT_SET_TX_AUTO                 0x0000
#define CMD_ACT_SET_TX_FIX_RATE             0x0001
#define CMD_ACT_GET_TX_RATE                 0x0002

#define CMD_ACT_SET_RX                      0x0001
#define CMD_ACT_SET_TX                      0x0002
#define CMD_ACT_SET_BOTH                    0x0003
#define CMD_ACT_GET_RX                      0x0004
#define CMD_ACT_GET_TX                      0x0008
#define CMD_ACT_GET_BOTH                    0x000c

/* Define action or option for CMD_802_11_PS_MODE */
#define CMD_TYPE_CAM                        0x0000
#define CMD_TYPE_MAX_PSP                    0x0001
#define CMD_TYPE_FAST_PSP                   0x0002

/* Define action or option for CMD_BT_ACCESS */
enum cmd_bt_access_opts {
	/* The bt commands start at 5 instead of 1 because the old dft commands
	 * are mapped to 1-4.  These old commands are no longer maintained and
	 * should not be called.
	 */
	CMD_ACT_BT_ACCESS_ADD = 5,
	CMD_ACT_BT_ACCESS_DEL,
	CMD_ACT_BT_ACCESS_LIST,
	CMD_ACT_BT_ACCESS_RESET,
	CMD_ACT_BT_ACCESS_SET_INVERT,
	CMD_ACT_BT_ACCESS_GET_INVERT
};

/* Define action or option for CMD_FWT_ACCESS */
enum cmd_fwt_access_opts {
	CMD_ACT_FWT_ACCESS_ADD = 1,
	CMD_ACT_FWT_ACCESS_DEL,
	CMD_ACT_FWT_ACCESS_LOOKUP,
	CMD_ACT_FWT_ACCESS_LIST,
	CMD_ACT_FWT_ACCESS_LIST_route,
	CMD_ACT_FWT_ACCESS_LIST_neighbor,
	CMD_ACT_FWT_ACCESS_RESET,
	CMD_ACT_FWT_ACCESS_CLEANUP,
	CMD_ACT_FWT_ACCESS_TIME,
};

/* Define action or option for CMD_MESH_ACCESS */
enum cmd_mesh_access_opts {
	CMD_ACT_MESH_GET_TTL = 1,
	CMD_ACT_MESH_SET_TTL,
	CMD_ACT_MESH_GET_STATS,
	CMD_ACT_MESH_GET_ANYCAST,
	CMD_ACT_MESH_SET_ANYCAST,
	CMD_ACT_MESH_SET_LINK_COSTS,
	CMD_ACT_MESH_GET_LINK_COSTS,
	CMD_ACT_MESH_SET_BCAST_RATE,
	CMD_ACT_MESH_GET_BCAST_RATE,
	CMD_ACT_MESH_SET_RREQ_DELAY,
	CMD_ACT_MESH_GET_RREQ_DELAY,
	CMD_ACT_MESH_SET_ROUTE_EXP,
	CMD_ACT_MESH_GET_ROUTE_EXP,
	CMD_ACT_MESH_SET_AUTOSTART_ENABLED,
	CMD_ACT_MESH_GET_AUTOSTART_ENABLED,
};

/** Card Event definition */
#define MACREG_INT_CODE_TX_PPA_FREE             0x00000000
#define MACREG_INT_CODE_TX_DMA_DONE             0x00000001
#define MACREG_INT_CODE_LINK_LOSE_W_SCAN        0x00000002
#define MACREG_INT_CODE_LINK_LOSE_NO_SCAN       0x00000003
#define MACREG_INT_CODE_LINK_SENSED             0x00000004
#define MACREG_INT_CODE_CMD_FINISHED            0x00000005
#define MACREG_INT_CODE_MIB_CHANGED             0x00000006
#define MACREG_INT_CODE_INIT_DONE               0x00000007
#define MACREG_INT_CODE_DEAUTHENTICATED         0x00000008
#define MACREG_INT_CODE_DISASSOCIATED           0x00000009
#define MACREG_INT_CODE_PS_AWAKE                0x0000000a
#define MACREG_INT_CODE_PS_SLEEP                0x0000000b
#define MACREG_INT_CODE_MIC_ERR_MULTICAST       0x0000000d
#define MACREG_INT_CODE_MIC_ERR_UNICAST         0x0000000e
#define MACREG_INT_CODE_WM_AWAKE                0x0000000f
#define MACREG_INT_CODE_ADHOC_BCN_LOST          0x00000011
#define MACREG_INT_CODE_RSSI_LOW		0x00000019
#define MACREG_INT_CODE_SNR_LOW			0x0000001a
#define MACREG_INT_CODE_MAX_FAIL		0x0000001b
#define MACREG_INT_CODE_RSSI_HIGH		0x0000001c
#define MACREG_INT_CODE_SNR_HIGH		0x0000001d
#define MACREG_INT_CODE_MESH_AUTO_STARTED	0x00000023

#endif				/* _HOST_H_ */
