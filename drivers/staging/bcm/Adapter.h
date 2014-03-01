/***********************************
*	Adapter.h
************************************/
#ifndef	__ADAPTER_H__
#define	__ADAPTER_H__

#define MAX_FRAGMENTEDIP_CLASSIFICATION_ENTRIES 256
#include "Debug.h"

struct bcm_leader {
	USHORT	Vcid;
	USHORT	PLength;
	UCHAR	Status;
	UCHAR	Unused[3];
} __packed;

struct bcm_packettosend {
	struct bcm_leader Leader;
	UCHAR	ucPayload;
} __packed;

struct bcm_control_packet {
	PVOID	ControlBuff;
	UINT	ControlBuffLen;
	struct bcm_control_packet *next;
} __packed;

struct bcm_link_request {
	struct bcm_leader Leader;
	UCHAR	szData[4];
} __packed;

#define MAX_IP_RANGE_LENGTH 4
#define MAX_PORT_RANGE 4
#define MAX_PROTOCOL_LENGTH   32
#define IPV6_ADDRESS_SIZEINBYTES 0x10

union u_ip_address {
	struct {
		ULONG ulIpv4Addr[MAX_IP_RANGE_LENGTH]; /* Source Ip Address Range */
		ULONG ulIpv4Mask[MAX_IP_RANGE_LENGTH]; /* Source Ip Mask Address Range */
	};
	struct {
		ULONG ulIpv6Addr[MAX_IP_RANGE_LENGTH * 4]; /* Source Ip Address Range */
		ULONG ulIpv6Mask[MAX_IP_RANGE_LENGTH * 4]; /* Source Ip Mask Address Range */
	};
	struct {
		UCHAR ucIpv4Address[MAX_IP_RANGE_LENGTH * IP_LENGTH_OF_ADDRESS];
		UCHAR ucIpv4Mask[MAX_IP_RANGE_LENGTH * IP_LENGTH_OF_ADDRESS];
	};
	struct {
		UCHAR ucIpv6Address[MAX_IP_RANGE_LENGTH * IPV6_ADDRESS_SIZEINBYTES];
		UCHAR ucIpv6Mask[MAX_IP_RANGE_LENGTH * IPV6_ADDRESS_SIZEINBYTES];
	};
};

struct bcm_hdr_suppression_contextinfo {
	UCHAR ucaHdrSuppressionInBuf[MAX_PHS_LENGTHS]; /* Intermediate buffer to accumulate pkt Header for PHS */
	UCHAR ucaHdrSuppressionOutBuf[MAX_PHS_LENGTHS + PHSI_LEN]; /* Intermediate buffer containing pkt Header after PHS */
};

struct bcm_classifier_rule {
	ULONG		ulSFID;
	UCHAR		ucReserved[2];
	B_UINT16	uiClassifierRuleIndex;
	bool		bUsed;
	USHORT		usVCID_Value;
	B_UINT8		u8ClassifierRulePriority; /* This field detemines the Classifier Priority */
	union u_ip_address	stSrcIpAddress;
	UCHAR		ucIPSourceAddressLength; /* Ip Source Address Length */

	union u_ip_address	stDestIpAddress;
	UCHAR		ucIPDestinationAddressLength; /* Ip Destination Address Length */
	UCHAR		ucIPTypeOfServiceLength; /* Type of service Length */
	UCHAR		ucTosLow; /* Tos Low */
	UCHAR		ucTosHigh; /* Tos High */
	UCHAR		ucTosMask; /* Tos Mask */

	UCHAR		ucProtocolLength; /* protocol Length */
	UCHAR		ucProtocol[MAX_PROTOCOL_LENGTH]; /* protocol Length */
	USHORT		usSrcPortRangeLo[MAX_PORT_RANGE];
	USHORT		usSrcPortRangeHi[MAX_PORT_RANGE];
	UCHAR		ucSrcPortRangeLength;

	USHORT		usDestPortRangeLo[MAX_PORT_RANGE];
	USHORT		usDestPortRangeHi[MAX_PORT_RANGE];
	UCHAR		ucDestPortRangeLength;

	bool		bProtocolValid;
	bool		bTOSValid;
	bool		bDestIpValid;
	bool		bSrcIpValid;

	/* For IPv6 Addressing */
	UCHAR		ucDirection;
	bool		bIpv6Protocol;
	UINT32		u32PHSRuleID;
	struct bcm_phs_rule sPhsRule;
	UCHAR		u8AssociatedPHSI;

	/* Classification fields for ETH CS */
	UCHAR		ucEthCSSrcMACLen;
	UCHAR		au8EThCSSrcMAC[MAC_ADDRESS_SIZE];
	UCHAR		au8EThCSSrcMACMask[MAC_ADDRESS_SIZE];
	UCHAR		ucEthCSDestMACLen;
	UCHAR		au8EThCSDestMAC[MAC_ADDRESS_SIZE];
	UCHAR		au8EThCSDestMACMask[MAC_ADDRESS_SIZE];
	UCHAR		ucEtherTypeLen;
	UCHAR		au8EthCSEtherType[NUM_ETHERTYPE_BYTES];
	UCHAR		usUserPriority[2];
	USHORT		usVLANID;
	USHORT		usValidityBitMap;
};

struct bcm_fragmented_packet_info {
	bool			bUsed;
	ULONG			ulSrcIpAddress;
	USHORT			usIpIdentification;
	struct bcm_classifier_rule *pstMatchedClassifierEntry;
	bool			bOutOfOrderFragment;
};

struct bcm_packet_info {
	/* classification extension Rule */
	ULONG		ulSFID;
	USHORT		usVCID_Value;
	UINT		uiThreshold;
	/* This field determines the priority of the SF Queues */
	B_UINT8		u8TrafficPriority;

	bool		bValid;
	bool		bActive;
	bool		bActivateRequestSent;

	B_UINT8		u8QueueType; /* BE or rtPS */

	UINT		uiMaxBucketSize; /* maximum size of the bucket for the queue */
	UINT		uiCurrentQueueDepthOnTarget;
	UINT		uiCurrentBytesOnHost;
	UINT		uiCurrentPacketsOnHost;
	UINT		uiDroppedCountBytes;
	UINT		uiDroppedCountPackets;
	UINT		uiSentBytes;
	UINT		uiSentPackets;
	UINT		uiCurrentDrainRate;
	UINT		uiThisPeriodSentBytes;
	LARGE_INTEGER	liDrainCalculated;
	UINT		uiCurrentTokenCount;
	LARGE_INTEGER	liLastUpdateTokenAt;
	UINT		uiMaxAllowedRate;
	UINT		NumOfPacketsSent;
	UCHAR		ucDirection;
	USHORT		usCID;
	struct bcm_mibs_parameters stMibsExtServiceFlowTable;
	UINT		uiCurrentRxRate;
	UINT		uiThisPeriodRxBytes;
	UINT		uiTotalRxBytes;
	UINT		uiTotalTxBytes;
	UINT		uiPendedLast;
	UCHAR		ucIpVersion;

	union {
		struct {
			struct sk_buff *FirstTxQueue;
			struct sk_buff *LastTxQueue;
		};
		struct {
			struct sk_buff *ControlHead;
			struct sk_buff *ControlTail;
		};
	};

	bool		bProtocolValid;
	bool		bTOSValid;
	bool		bDestIpValid;
	bool		bSrcIpValid;

	bool		bActiveSet;
	bool		bAdmittedSet;
	bool		bAuthorizedSet;
	bool		bClassifierPriority;
	UCHAR		ucServiceClassName[MAX_CLASS_NAME_LENGTH];
	bool		bHeaderSuppressionEnabled;
	spinlock_t	SFQueueLock;
	void		*pstSFIndication;
	struct timeval	stLastUpdateTokenAt;
	atomic_t	uiPerSFTxResourceCount;
	UINT		uiMaxLatency;
	UCHAR		bIPCSSupport;
	UCHAR		bEthCSSupport;
};

struct bcm_tarang_data {
	struct bcm_tarang_data	*next;
	struct bcm_mini_adapter	*Adapter;
	struct sk_buff		*RxAppControlHead;
	struct sk_buff		*RxAppControlTail;
	int			AppCtrlQueueLen;
	bool			MacTracingEnabled;
	bool			bApplicationToExit;
	struct bcm_mibs_dropped_cntrl_msg stDroppedAppCntrlMsgs;
	ULONG			RxCntrlMsgBitMask;
};

struct bcm_targetdsx_buffer {
	ULONG		ulTargetDsxBuffer;
	B_UINT16	tid;
	bool		valid;
};

typedef int (*FP_FLASH_WRITE)(struct bcm_mini_adapter *, UINT, PVOID);

typedef int (*FP_FLASH_WRITE_STATUS)(struct bcm_mini_adapter *, UINT, PVOID);

/*
 * Driver adapter data structure
 */
struct bcm_mini_adapter {
	struct bcm_mini_adapter	*next;
	struct net_device	*dev;
	u32			msg_enable;
	CHAR			*caDsxReqResp;
	atomic_t		ApplicationRunning;
	bool			AppCtrlQueueOverFlow;
	atomic_t		CurrentApplicationCount;
	atomic_t		RegisteredApplicationCount;
	bool			LinkUpStatus;
	bool			TimerActive;
	u32			StatisticsPointer;
	struct sk_buff		*RxControlHead;
	struct sk_buff		*RxControlTail;
	struct semaphore	RxAppControlQueuelock;
	struct semaphore	fw_download_sema;
	struct bcm_tarang_data	*pTarangs;
	spinlock_t		control_queue_lock;
	wait_queue_head_t	process_read_wait_queue;

	/* the pointer to the first packet we have queued in send
	 * deserialized miniport support variables
	 */
	atomic_t		TotalPacketCount;
	atomic_t		TxPktAvail;

	/* this to keep track of the Tx and Rx MailBox Registers. */
	atomic_t		CurrNumFreeTxDesc;
	/* to keep track the no of byte received */
	USHORT			PrevNumRecvDescs;
	USHORT			CurrNumRecvDescs;
	UINT			u32TotalDSD;
	struct bcm_packet_info	PackInfo[NO_OF_QUEUES];
	struct bcm_classifier_rule astClassifierTable[MAX_CLASSIFIERS];
	bool			TransferMode;

	/*************** qos ******************/
	bool			bETHCSEnabled;
	ULONG			BEBucketSize;
	ULONG			rtPSBucketSize;
	UCHAR			LinkStatus;
	bool			AutoLinkUp;
	bool			AutoSyncup;

	int			major;
	int			minor;
	wait_queue_head_t	tx_packet_wait_queue;
	wait_queue_head_t	process_rx_cntrlpkt;
	atomic_t		process_waiting;
	bool			fw_download_done;

	char			*txctlpacket[MAX_CNTRL_PKTS];
	atomic_t		cntrlpktCnt;
	atomic_t		index_app_read_cntrlpkt;
	atomic_t		index_wr_txcntrlpkt;
	atomic_t		index_rd_txcntrlpkt;
	UINT			index_datpkt;
	struct semaphore	rdmwrmsync;

	struct bcm_targetdsx_buffer	astTargetDsxBuffer[MAX_TARGET_DSX_BUFFERS];
	ULONG			ulFreeTargetBufferCnt;
	ULONG			ulCurrentTargetBuffer;
	ULONG			ulTotalTargetBuffersAvailable;
	unsigned long		chip_id;
	wait_queue_head_t	lowpower_mode_wait_queue;
	bool			bFlashBoot;
	bool			bBinDownloaded;
	bool			bCfgDownloaded;
	bool			bSyncUpRequestSent;
	USHORT			usBestEffortQueueIndex;
	wait_queue_head_t	ioctl_fw_dnld_wait_queue;
	bool			waiting_to_fw_download_done;
	pid_t			fw_download_process_pid;
	struct bcm_target_params *pstargetparams;
	bool			device_removed;
	bool			DeviceAccess;
	bool			bIsAutoCorrectEnabled;
	bool			bDDRInitDone;
	int			DDRSetting;
	ULONG			ulPowerSaveMode;
	spinlock_t		txtransmitlock;
	B_UINT8			txtransmit_running;
	/* Thread for control packet handling */
	struct task_struct	*control_packet_handler;
	/* thread for transmitting packets. */
	struct task_struct	*transmit_packet_thread;

	/* LED Related Structures */
	struct bcm_led_info	LEDInfo;

	/* Driver State for LED Blinking */
	enum bcm_led_events	DriverState;
	/* Interface Specific */
	PVOID			pvInterfaceAdapter;
	int (*bcm_file_download)(PVOID,
				struct file *,
				unsigned int);
	int (*bcm_file_readback_from_chip)(PVOID,
					struct file *,
					unsigned int);
	int (*interface_rdm)(PVOID,
			UINT,
			PVOID,
			int);
	int (*interface_wrm)(PVOID,
			UINT,
			PVOID,
			int);
	int (*interface_transmit)(PVOID, PVOID , UINT);
	bool			IdleMode;
	bool			bDregRequestSentInIdleMode;
	bool			bTriedToWakeUpFromlowPowerMode;
	bool			bShutStatus;
	bool			bWakeUpDevice;
	unsigned int		usIdleModePattern;
	/* BOOLEAN			bTriedToWakeUpFromShutdown; */
	bool			bLinkDownRequested;
	int			downloadDDR;
	struct bcm_phs_extension stBCMPhsContext;
	struct bcm_hdr_suppression_contextinfo stPhsTxContextInfo;
	uint8_t			ucaPHSPktRestoreBuf[2048];
	uint8_t			bPHSEnabled;
	bool			AutoFirmDld;
	bool			bMipsConfig;
	bool			bDPLLConfig;
	UINT32			aTxPktSizeHist[MIBS_MAX_HIST_ENTRIES];
	UINT32			aRxPktSizeHist[MIBS_MAX_HIST_ENTRIES];
	struct bcm_fragmented_packet_info astFragmentedPktClassifierTable[MAX_FRAGMENTEDIP_CLASSIFICATION_ENTRIES];
	atomic_t		uiMBupdate;
	UINT32			PmuMode;
	enum bcm_nvm_type	eNVMType;
	UINT			uiSectorSize;
	UINT			uiSectorSizeInCFG;
	bool			bSectorSizeOverride;
	bool			bStatusWrite;
	UINT			uiNVMDSDSize;
	UINT			uiVendorExtnFlag;
	/* it will always represent chosen DSD at any point of time.
	 * Generally it is Active DSD but in case of NVM RD/WR it might be different.
	 */
	UINT			ulFlashCalStart;
	ULONG			ulFlashControlSectionStart;
	ULONG			ulFlashWriteSize;
	ULONG			ulFlashID;
	FP_FLASH_WRITE		fpFlashWrite;
	FP_FLASH_WRITE_STATUS	fpFlashWriteWithStatusCheck;

	struct semaphore	NVMRdmWrmLock;
	struct device		*pstCreatedClassDevice;

	/*	BOOLEAN				InterfaceUpStatus; */
	struct bcm_flash2x_cs_info *psFlash2xCSInfo;
	struct bcm_flash_cs_info *psFlashCSInfo;
	struct bcm_flash2x_vendor_info *psFlash2xVendorInfo;
	UINT			uiFlashBaseAdd; /* Flash start address */
	UINT			uiActiveISOOffset; /* Active ISO offset chosen before f/w download */
	enum bcm_flash2x_section_val eActiveISO; /* Active ISO section val */
	enum bcm_flash2x_section_val eActiveDSD; /* Active DSD val chosen before f/w download */
	UINT			uiActiveDSDOffsetAtFwDld;  /* For accessing Active DSD chosen before f/w download */
	UINT			uiFlashLayoutMajorVersion;
	UINT			uiFlashLayoutMinorVersion;
	bool			bAllDSDWriteAllow;
	bool			bSigCorrupted;
	/* this should be set who so ever want to change the Headers. after Write it should be reset immediately. */
	bool			bHeaderChangeAllowed;
	int			SelectedChip;
	bool			bEndPointHalted;
	/* while bFlashRawRead will be true, Driver  ignore map lay out and consider flash as of without any map. */
	bool			bFlashRawRead;
	bool			bPreparingForLowPowerMode;
	bool			bDoSuspend;
	UINT			syscfgBefFwDld;
	bool			StopAllXaction;
	UINT32			liTimeSinceLastNetEntry; /* Used to Support extended CAPI requirements from */
	struct semaphore	LowPowerModeSync;
	ULONG			liDrainCalculated;
	UINT			gpioBitMap;
	struct bcm_debug_state	stDebugState;
};

#define GET_BCM_ADAPTER(net_dev) netdev_priv(net_dev)

struct bcm_eth_header {
	UCHAR	au8DestinationAddress[6];
	UCHAR	au8SourceAddress[6];
	USHORT	u16Etype;
} __packed;

struct bcm_firmware_info {
	void	__user *pvMappedFirmwareAddress;
	ULONG	u32FirmwareLength;
	ULONG	u32StartingAddress;
} __packed;

/* holds the value of net_device structure.. */
extern struct net_device *gblpnetdev;

struct bcm_ddr_setting {
	UINT ulRegAddress;
	UINT ulRegValue;
};
int InitAdapter(struct bcm_mini_adapter *psAdapter);

/* =====================================================================
 * Beceem vendor request codes for EP0
 * =====================================================================
 */

#define BCM_REQUEST_READ	0x2
#define BCM_REQUEST_WRITE	0x1
#define EP2_MPS_REG		0x0F0110A0
#define EP2_MPS			0x40

#define EP2_CFG_REG	0x0F0110A8
#define EP2_CFG_INT	0x27
#define EP2_CFG_BULK	0x25

#define EP4_MPS_REG	0x0F0110F0
#define EP4_MPS		0x8C

#define EP4_CFG_REG	0x0F0110F8

#define ISO_MPS_REG	0x0F0110C8
#define ISO_MPS		0x00000000

#define EP1 0
#define EP2 1
#define EP3 2
#define EP4 3
#define EP5 4
#define EP6 5

enum bcm_einterface_setting {
	DEFAULT_SETTING_0  = 0,
	ALTERNATE_SETTING_1 = 1,
};

#endif	/* __ADAPTER_H__ */
