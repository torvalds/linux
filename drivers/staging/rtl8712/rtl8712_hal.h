#ifndef __RTL8712_HAL_H__
#define __RTL8712_HAL_H__

enum _HW_VERSION {
	RTL8712_FPGA,
	RTL8712_1stCUT,	/*A Cut (RTL8712_ASIC)*/
	RTL8712_2ndCUT,	/*B Cut*/
	RTL8712_3rdCUT,	/*C Cut*/
};

enum _LOOPBACK_TYPE {
	RTL8712_AIR_TRX = 0,
	RTL8712_MAC_LBK,
	RTL8712_BB_LBK,
	RTL8712_MAC_FW_LBK = 4,
	RTL8712_BB_FW_LBK = 8,
};

enum RTL871X_HCI_TYPE {
	RTL8712_SDIO,
	RTL8712_USB,
};

enum RTL8712_RF_CONFIG {
	RTL8712_RF_1T1R,
	RTL8712_RF_1T2R,
	RTL8712_RF_2T2R
};

enum _RTL8712_HCI_TYPE_ {
	RTL8712_HCI_TYPE_PCIE = 0x01,
	RTL8712_HCI_TYPE_AP_PCIE = 0x81,
	RTL8712_HCI_TYPE_USB = 0x02,
	RTL8712_HCI_TYPE_92USB = 0x02,
	RTL8712_HCI_TYPE_AP_USB = 0x82,
	RTL8712_HCI_TYPE_72USB = 0x12,
	RTL8712_HCI_TYPE_SDIO = 0x04,
	RTL8712_HCI_TYPE_72SDIO = 0x14
};

struct fw_priv {   /*8-bytes alignment required*/
	/*--- long word 0 ----*/
	unsigned char signature_0;  /*0x12: CE product, 0x92: IT product*/
	unsigned char signature_1;  /*0x87: CE product, 0x81: IT product*/
	unsigned char hci_sel; /*0x81: PCI-AP, 01:PCIe, 02: 92S-U, 0x82: USB-AP,
			    * 0x12: 72S-U, 03:SDIO*/
	unsigned char chip_version; /*the same value as register value*/
	unsigned char customer_ID_0; /*customer  ID low byte*/
	unsigned char customer_ID_1; /*customer  ID high byte*/
	unsigned char rf_config;  /*0x11:  1T1R, 0x12: 1T2R, 0x92: 1T2R turbo,
			     * 0x22: 2T2R*/
	unsigned char usb_ep_num;  /* 4: 4EP, 6: 6EP, 11: 11EP*/
	/*--- long word 1 ----*/
	unsigned char regulatory_class_0; /*regulatory class bit map 0*/
	unsigned char regulatory_class_1; /*regulatory class bit map 1*/
	unsigned char regulatory_class_2; /*regulatory class bit map 2*/
	unsigned char regulatory_class_3; /*regulatory class bit map 3*/
	unsigned char rfintfs;    /* 0:SWSI, 1:HWSI, 2:HWPI*/
	unsigned char def_nettype;
	unsigned char turboMode;
	unsigned char lowPowerMode;/* 0: noral mode, 1: low power mode*/
	/*--- long word 2 ----*/
	unsigned char lbk_mode; /*0x00: normal, 0x03: MACLBK, 0x01: PHYLBK*/
	unsigned char mp_mode; /* 1: for MP use, 0: for normal driver */
	unsigned char vcsType; /* 0:off 1:on 2:auto */
	unsigned char vcsMode; /* 1:RTS/CTS 2:CTS to self */
	unsigned char rsvd022;
	unsigned char rsvd023;
	unsigned char rsvd024;
	unsigned char rsvd025;
	/*--- long word 3 ----*/
	unsigned char qos_en;    /*1: QoS enable*/
	unsigned char bw_40MHz_en;   /*1: 40MHz BW enable*/
	unsigned char AMSDU2AMPDU_en;   /*1: 4181 convert AMSDU to AMPDU,
				   * 0: disable*/
	unsigned char AMPDU_en;   /*1: 11n AMPDU enable*/
	unsigned char rate_control_offload; /*1: FW offloads,0: driver handles*/
	unsigned char aggregation_offload;  /*1: FW offloads,0: driver handles*/
	unsigned char rsvd030;
	unsigned char rsvd031;
	/*--- long word 4 ----*/
	unsigned char beacon_offload;   /* 1. FW offloads, 0: driver handles*/
	unsigned char MLME_offload;   /* 2. FW offloads, 0: driver handles*/
	unsigned char hwpc_offload;   /* 3. FW offloads, 0: driver handles*/
	unsigned char tcp_checksum_offload; /*4. FW offloads,0: driver handles*/
	unsigned char tcp_offload;    /* 5. FW offloads, 0: driver handles*/
	unsigned char ps_control_offload; /* 6. FW offloads, 0: driver handles*/
	unsigned char WWLAN_offload;   /* 7. FW offloads, 0: driver handles*/
	unsigned char rsvd040;
	/*--- long word 5 ----*/
	unsigned char tcp_tx_frame_len_L;  /*tcp tx packet length low byte*/
	unsigned char tcp_tx_frame_len_H;  /*tcp tx packet length high byte*/
	unsigned char tcp_rx_frame_len_L;  /*tcp rx packet length low byte*/
	unsigned char tcp_rx_frame_len_H;  /*tcp rx packet length high byte*/
	unsigned char rsvd050;
	unsigned char rsvd051;
	unsigned char rsvd052;
	unsigned char rsvd053;
};

struct fw_hdr {/*8-byte alinment required*/
	unsigned short	signature;
	unsigned short	version;	/*0x8000 ~ 0x8FFF for FPGA version,
					 *0x0000 ~ 0x7FFF for ASIC version,*/
	unsigned int		dmem_size;    /*define the size of boot loader*/
	unsigned int		img_IMEM_size; /*define the size of FW in IMEM*/
	unsigned int		img_SRAM_size; /*define the size of FW in SRAM*/
	unsigned int		fw_priv_sz; /*define the size of DMEM variable*/
	unsigned short	efuse_addr;
	unsigned short	h2ccnd_resp_addr;
	unsigned int		SVNRevision;
	unsigned int		release_time; /*Mon:Day:Hr:Min*/
	struct fw_priv	fwpriv;
};

struct hal_priv {
	/*Endpoint handles*/
	struct  net_device *pipehdls_r8712[10];
	u8 (*hal_bus_init)(struct _adapter *adapter);
};

uint	 rtl8712_hal_init(struct _adapter *padapter);

#endif
