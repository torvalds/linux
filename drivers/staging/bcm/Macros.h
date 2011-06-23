/*************************************
*	Macros.h
**************************************/
#ifndef	__MACROS_H__
#define __MACROS_H__

#define TX_TIMER_PERIOD 10	//10 msec
#define MAX_CLASSIFIERS 100
//#define MAX_CLASSIFIERS_PER_SF  20
#define MAX_TARGET_DSX_BUFFERS 24

#define MAX_CNTRL_PKTS 	100
#define MAX_DATA_PKTS 		200
#define MAX_ETH_SIZE 		1536
#define MAX_CNTL_PKT_SIZE 2048

#define MTU_SIZE 1400
#define TX_QLEN  5

#define MAC_ADDR_REGISTER 0xbf60d000


///////////Quality of Service///////////////////////////
#define NO_OF_QUEUES				17
#define HiPriority                  NO_OF_QUEUES-1
#define LowPriority                 0
#define BE                          2
#define rtPS                        4
#define ERTPS                       5
#define UGS                         6

#define BE_BUCKET_SIZE          	1024*1024*100  //32kb
#define rtPS_BUCKET_SIZE        	1024*1024*100    //8kb
#define MAX_ALLOWED_RATE    		1024*1024*100
#define TX_PACKET_THRESHOLD 		10
#define XSECONDS                    1*HZ
#define DSC_ACTIVATE_REQUEST    	248
#define QUEUE_DEPTH_OFFSET          0x1fc01000
#define MAX_DEVICE_DESC_SIZE 		2040
#define MAX_CTRL_QUEUE_LEN			100
#define MAX_APP_QUEUE_LEN			200
#define MAX_LATENCY_ALLOWED			0xFFFFFFFF
#define DEFAULT_UG_INTERVAL			250
#define DEFAULT_UGI_FACTOR			4

#define DEFAULT_PERSFCOUNT			60
#define MAX_CONNECTIONS				10
#define MAX_CLASS_NAME_LENGTH      	32

#define	ETH_LENGTH_OF_ADDRESS		6
#define MAX_MULTICAST_ADDRESSES     32
#define IP_LENGTH_OF_ADDRESS    	4

#define IP_PACKET_ONLY_MODE			0
#define ETH_PACKET_TUNNELING_MODE	1

////////////Link Request//////////////
#define SET_MAC_ADDRESS_REQUEST		0
#define SYNC_UP_REQUEST             1
#define SYNCED_UP                   2
#define LINK_UP_REQUEST             3
#define LINK_CONNECTED              4
#define SYNC_UP_NOTIFICATION    	2
#define LINK_UP_NOTIFICATION    	4


#define LINK_NET_ENTRY              0x0002
#define HMC_STATUS					0x0004
#define LINK_UP_CONTROL_REQ         0x83

#define STATS_POINTER_REQ_STATUS    0x86
#define NETWORK_ENTRY_REQ_PAYLOAD   198
#define LINK_DOWN_REQ_PAYLOAD  	 	226
#define SYNC_UP_REQ_PAYLOAD         228
#define STATISTICS_POINTER_REQ  	237
#define LINK_UP_REQ_PAYLOAD         245
#define LINK_UP_ACK                 246

#define STATS_MSG_SIZE              4
#define INDEX_TO_DATA               4

#define	GO_TO_IDLE_MODE_PAYLOAD			210
#define	COME_UP_FROM_IDLE_MODE_PAYLOAD	211
#define IDLE_MODE_SF_UPDATE_MSG			187

#define SKB_RESERVE_ETHERNET_HEADER	16
#define SKB_RESERVE_PHS_BYTES		32

#define IP_PACKET_ONLY_MODE			0
#define ETH_PACKET_TUNNELING_MODE	1

#define ETH_CS_802_3				1
#define ETH_CS_802_1Q_VLAN			3
#define IPV4_CS						1
#define IPV6_CS						2
#define	ETH_CS_MASK					0x3f

/** \brief Validity bit maps for TLVs in packet classification rule */

#define PKT_CLASSIFICATION_USER_PRIORITY_VALID		0
#define PKT_CLASSIFICATION_VLANID_VALID				1

#ifndef MIN
#define MIN(_a, _b) ((_a) < (_b)? (_a): (_b))
#endif


/*Leader related terms */
#define LEADER_STATUS   				0x00
#define LEADER_STATUS_TCP_ACK			0x1
#define LEADER_SIZE             		sizeof(LEADER)
#define MAC_ADDR_REQ_SIZE				sizeof(PACKETTOSEND)
#define SS_INFO_REQ_SIZE				sizeof(PACKETTOSEND)
#define	CM_REQUEST_SIZE					LEADER_SIZE + sizeof(stLocalSFChangeRequest)
#define IDLE_REQ_SIZE					sizeof(PACKETTOSEND)


#define MAX_TRANSFER_CTRL_BYTE_USB		2 * 1024

#define GET_MAILBOX1_REG_REQUEST        0x87
#define GET_MAILBOX1_REG_RESPONSE       0x67
#define VCID_CONTROL_PACKET             0x00

#define TRANSMIT_NETWORK_DATA           0x00
#define RECEIVED_NETWORK_DATA           0x20

#define CM_RESPONSES					0xA0
#define STATUS_RSP						0xA1
#define LINK_CONTROL_RESP				0xA2
#define	IDLE_MODE_STATUS				0xA3
#define STATS_POINTER_RESP				0xA6
#define MGMT_MSG_INFO_SW_STATUS         0xA7
#define AUTH_SS_HOST_MSG	    		0xA8

#define CM_DSA_ACK_PAYLOAD				247
#define CM_DSC_ACK_PAYLOAD				248
#define CM_DSD_ACK_PAYLOAD				249
#define CM_DSDEACTVATE					250
#define TOTAL_MASKED_ADDRESS_IN_BYTES	32

#define MAC_REQ				0
#define LINK_RESP			1
#define RSSI_INDICATION 	2

#define SS_INFO         	4
#define STATISTICS_INFO 	5
#define CM_INDICATION   	6
#define PARAM_RESP			7
#define BUFFER_1K 			1024
#define BUFFER_2K 			BUFFER_1K*2
#define BUFFER_4K 			BUFFER_2K*2
#define BUFFER_8K 			BUFFER_4K*2
#define BUFFER_16K 			BUFFER_8K*2
#define DOWNLINK_DIR 0
#define UPLINK_DIR 1

#define BCM_SIGNATURE		"BECEEM"


#define GPIO_OUTPUT_REGISTER	 0x0F00003C
#define BCM_GPIO_OUTPUT_SET_REG  0x0F000040
#define BCM_GPIO_OUTPUT_CLR_REG  0x0F000044
#define GPIO_MODE_REGISTER       0x0F000034
#define GPIO_PIN_STATE_REGISTER  0x0F000038


typedef struct _LINK_STATE {
	UCHAR	ucLinkStatus;
    UCHAR   bIdleMode;
	UCHAR	bShutdownMode;
}LINK_STATE, *PLINK_STATE;


enum enLinkStatus {
    WAIT_FOR_SYNC = 	1,
    PHY_SYNC_ACHIVED = 	2,
    LINKUP_IN_PROGRESS = 3,
    LINKUP_DONE     = 	4,
    DREG_RECEIVED =		5,
    LINK_STATUS_RESET_RECEIVED = 6,
    PERIODIC_WAKE_UP_NOTIFICATION_FRM_FW  = 7,
    LINK_SHUTDOWN_REQ_FROM_FIRMWARE = 8,
    COMPLETE_WAKE_UP_NOTIFICATION_FRM_FW =9
};

typedef enum _E_PHS_DSC_ACTION
{
	eAddPHSRule=0,
	eSetPHSRule,
	eDeletePHSRule,
	eDeleteAllPHSRules
}E_PHS_DSC_ACTION;


#define CM_CONTROL_NEWDSX_MULTICLASSIFIER_REQ		0x89    // Host to Mac
#define CM_CONTROL_NEWDSX_MULTICLASSIFIER_RESP      0xA9    // Mac to Host
#define MASK_DISABLE_HEADER_SUPPRESSION 			0x10 //0b000010000
#define	MINIMUM_PENDING_DESCRIPTORS					5

#define SHUTDOWN_HOSTINITIATED_REQUESTPAYLOAD 0xCC
#define SHUTDOWN_ACK_FROM_DRIVER 0x1
#define SHUTDOWN_NACK_FROM_DRIVER 0x2

#define LINK_SYNC_UP_SUBTYPE		0x0001
#define LINK_SYNC_DOWN_SUBTYPE		0x0001



#define CONT_MODE 1
#define SINGLE_DESCRIPTOR 1


#define DESCRIPTOR_LENGTH 0x30
#define FIRMWARE_DESCS_ADDRESS 0x1F100000


#define CLOCK_RESET_CNTRL_REG_1 0x0F00000C
#define CLOCK_RESET_CNTRL_REG_2 0x0F000840



#define TX_DESCRIPTOR_HEAD_REGISTER 0x0F010034
#define RX_DESCRIPTOR_HEAD_REGISTER 0x0F010094

#define STATISTICS_BEGIN_ADDR        0xbf60f02c

#define MAX_PENDING_CTRL_PACKET (MAX_CTRL_QUEUE_LEN-10)

#define WIMAX_MAX_MTU			(MTU_SIZE + ETH_HLEN)
#define AUTO_LINKUP_ENABLE              0x2
#define AUTO_SYNC_DISABLE              	0x1
#define AUTO_FIRM_DOWNLOAD              0x1
#define SETTLE_DOWN_TIME                50

#define HOST_BUS_SUSPEND_BIT            16

#define IDLE_MESSAGE 0x81

#define MIPS_CLOCK_133MHz 1

#define TARGET_CAN_GO_TO_IDLE_MODE 2
#define TARGET_CAN_NOT_GO_TO_IDLE_MODE 3
#define IDLE_MODE_PAYLOAD_LENGTH 8

#define IP_HEADER(Buffer) ((IPHeaderFormat*)(Buffer))
#define IPV4				4
#define IP_VERSION(byte)	(((byte&0xF0)>>4))

#define SET_MAC_ADDRESS  193
#define SET_MAC_ADDRESS_RESPONSE 236

#define IDLE_MODE_WAKEUP_PATTERN 0xd0ea1d1e
#define IDLE_MODE_WAKEUP_NOTIFIER_ADDRESS 0x1FC02FA8
#define IDLE_MODE_MAX_RETRY_COUNT 1000

#ifdef REL_4_1
#define CONFIG_BEGIN_ADDR 0xBF60B004
#else
#define CONFIG_BEGIN_ADDR 0xBF60B000
#endif

#define FIRMWARE_BEGIN_ADDR 0xBFC00000

#define INVALID_QUEUE_INDEX NO_OF_QUEUES

#define INVALID_PID (pid_t)-1
#define DDR_80_MHZ  	0
#define DDR_100_MHZ 	1
#define DDR_120_MHZ    	2 //  Additional Frequency for T3LP
#define DDR_133_MHZ    	3
#define DDR_140_MHZ    	4 //  Not Used (Reserved for future)
#define DDR_160_MHZ    	5 //  Additional Frequency for T3LP
#define DDR_180_MHZ    	6 //  Not Used (Reserved for future)
#define DDR_200_MHZ    	7 //  Not Used (Reserved for future)

#define MIPS_200_MHZ   0
#define MIPS_160_MHZ   1

#define PLL_800_MHZ    0
#define PLL_266_MHZ    1

#define DEVICE_POWERSAVE_MODE_AS_MANUAL_CLOCK_GATING        0
#define DEVICE_POWERSAVE_MODE_AS_PMU_CLOCK_GATING           1
#define DEVICE_POWERSAVE_MODE_AS_PMU_SHUTDOWN               2
#define DEVICE_POWERSAVE_MODE_AS_RESERVED                   3
#define DEVICE_POWERSAVE_MODE_AS_PROTOCOL_IDLE_MODE         4


#define EEPROM_REJECT_REG_1 0x0f003018
#define EEPROM_REJECT_REG_2 0x0f00301c
#define EEPROM_REJECT_REG_3 0x0f003008
#define EEPROM_REJECT_REG_4 0x0f003020
#define EEPROM_REJECT_MASK  0x0fffffff
#define VSG_MODE			  0x3

/* Idle Mode Related Registers */
#define DEBUG_INTERRUPT_GENERATOR_REGISTOR 0x0F00007C
#define SW_ABORT_IDLEMODE_LOC 		0x0FF01FFC

#define SW_ABORT_IDLEMODE_PATTERN 	0xd0ea1d1e
#define DEVICE_INT_OUT_EP_REG0		0x0F011870
#define DEVICE_INT_OUT_EP_REG1		0x0F011874

#define BIN_FILE "/lib/firmware/macxvi200.bin"
#define CFG_FILE "/lib/firmware/macxvi.cfg"
#define SF_MAX_ALLOWED_PACKETS_TO_BACKUP 128
#define MIN_VAL(x,y) 	((x)<(y)?(x):(y))
#define MAC_ADDRESS_SIZE 6
#define EEPROM_COMMAND_Q_REG    0x0F003018
#define EEPROM_READ_DATA_Q_REG  0x0F003020
#define CHIP_ID_REG 			0x0F000000
#define GPIO_MODE_REG			0x0F000034
#define GPIO_OUTPUT_REG			0x0F00003C
#define WIMAX_MAX_ALLOWED_RATE  1024*1024*50

#define T3 0xbece0300
#define TARGET_SFID_TXDESC_MAP_LOC 0xBFFFF400

#define RWM_READ 0
#define RWM_WRITE 1

#define T3LPB       0xbece3300
#define BCS220_2	0xbece3311
#define BCS220_2BC	0xBECE3310
#define BCS250_BC	0xbece3301
#define BCS220_3	0xbece3321


#define HPM_CONFIG_LDO145	0x0F000D54
#define HPM_CONFIG_MSW		0x0F000D58

#define T3B 0xbece0310
typedef enum eNVM_TYPE
{
	NVM_AUTODETECT = 0,
	NVM_EEPROM,
	NVM_FLASH,
	NVM_UNKNOWN
}NVM_TYPE;

typedef enum ePMU_MODES
{
	HYBRID_MODE_7C  = 0,
	INTERNAL_MODE_6 = 1,
	HYBRID_MODE_6   = 2
}PMU_MODE;

#define MAX_RDM_WRM_RETIRES 1

enum eAbortPattern {
	ABORT_SHUTDOWN_MODE = 1,
	ABORT_IDLE_REG = 1,
	ABORT_IDLE_MODE = 2,
	ABORT_IDLE_SYNCDOWN = 3
};


/* Offsets used by driver in skb cb variable */
#define SKB_CB_CLASSIFICATION_OFFSET    0
#define SKB_CB_LATENCY_OFFSET           1
#define SKB_CB_TCPACK_OFFSET            2

#endif	//__MACROS_H__
