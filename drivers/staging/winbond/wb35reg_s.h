#ifndef __WINBOND_WB35REG_S_H
#define __WINBOND_WB35REG_S_H

#include <linux/spinlock.h>
#include <linux/types.h>
#include <asm/atomic.h>

struct hw_data;

/* =========================================================================
 *
 *			HAL setting function
 *
 *		========================================
 *		|Uxx| 	|Dxx|	|Mxx|	|BB|	|RF|
 *		========================================
 *			|					|
 *		Wb35Reg_Read		Wb35Reg_Write
 *
 *		----------------------------------------
 *				WbUsb_CallUSBDASync	supplied By WbUsb module
 * ==========================================================================
 */
#define GetBit(dwData, i)	(dwData & (0x00000001 << i))
#define SetBit(dwData, i)	(dwData | (0x00000001 << i))
#define ClearBit(dwData, i)	(dwData & ~(0x00000001 << i))

#define	IGNORE_INCREMENT	0
#define	AUTO_INCREMENT		0
#define	NO_INCREMENT		1
#define REG_DIRECTION(_x, _y)	((_y)->DIRECT == 0 ? usb_rcvctrlpipe(_x, 0) : usb_sndctrlpipe(_x, 0))
#define REG_BUF_SIZE(_x)	((_x)->bRequest == 0x04 ? cpu_to_le16((_x)->wLength) : 4)

#define BB48_DEFAULT_AL2230_11B		0x0033447c
#define BB4C_DEFAULT_AL2230_11B		0x0A00FEFF
#define BB48_DEFAULT_AL2230_11G		0x00332C1B
#define BB4C_DEFAULT_AL2230_11G		0x0A00FEFF


#define BB48_DEFAULT_WB242_11B		0x00292315	/* backoff  2dB */
#define BB4C_DEFAULT_WB242_11B		0x0800FEFF	/* backoff  2dB */
#define BB48_DEFAULT_WB242_11G		0x00453B24
#define BB4C_DEFAULT_WB242_11G		0x0E00FEFF

/*
 * ====================================
 *  Default setting for Mxx
 * ====================================
 */
#define DEFAULT_CWMIN			31	/* (M2C) CWmin. Its value is in the range 0-31. */
#define DEFAULT_CWMAX			1023	/* (M2C) CWmax. Its value is in the range 0-1023. */
#define DEFAULT_AID			1	/* (M34) AID. Its value is in the range 1-2007. */

#define DEFAULT_RATE_RETRY_LIMIT	2	/* (M38) as named */

#define DEFAULT_LONG_RETRY_LIMIT	7	/* (M38) LongRetryLimit. Its value is in the range 0-15. */
#define DEFAULT_SHORT_RETRY_LIMIT	7	/* (M38) ShortRetryLimit. Its value is in the range 0-15. */
#define DEFAULT_PIFST			25	/* (M3C) PIFS Time. Its value is in the range 0-65535. */
#define DEFAULT_EIFST			354	/* (M3C) EIFS Time. Its value is in the range 0-1048575. */
#define DEFAULT_DIFST			45	/* (M3C) DIFS Time. Its value is in the range 0-65535. */
#define DEFAULT_SIFST			5	/* (M3C) SIFS Time. Its value is in the range 0-65535. */
#define DEFAULT_OSIFST			10	/* (M3C) Original SIFS Time. Its value is in the range 0-15. */
#define DEFAULT_ATIMWD			0	/* (M40) ATIM Window. Its value is in the range 0-65535. */
#define DEFAULT_SLOT_TIME		20	/* (M40) ($) SlotTime. Its value is in the range 0-255. */
#define DEFAULT_MAX_TX_MSDU_LIFE_TIME	512	/* (M44) MaxTxMSDULifeTime. Its value is in the range 0-4294967295. */
#define DEFAULT_BEACON_INTERVAL		500	/* (M48) Beacon Interval. Its value is in the range 0-65535. */
#define DEFAULT_PROBE_DELAY_TIME	200	/* (M48) Probe Delay Time. Its value is in the range 0-65535. */
#define DEFAULT_PROTOCOL_VERSION	0	/* (M4C) */
#define DEFAULT_MAC_POWER_STATE		2	/* (M4C) 2: MAC at power active */
#define DEFAULT_DTIM_ALERT_TIME		0


struct wb35_reg_queue {
	struct urb	*urb;
	void		*pUsbReq;
	void		*Next;
	union {
		u32	VALUE;
		u32	*pBuffer;
	};
	u8		RESERVED[4];	/* space reserved for communication */
	u16		INDEX;		/* For storing the register index */
	u8		RESERVED_VALID;	/* Indicate whether the RESERVED space is valid at this command. */
	u8		DIRECT;		/* 0:In   1:Out */
};

/*
 * ====================================
 * Internal variable for module
 * ====================================
 */
#define MAX_SQ3_FILTER_SIZE		5
struct wb35_reg {
	/*
	 * ============================
	 *  Register Bank backup
	 * ============================
	 */
	u32	U1B0;			/* bit16 record the h/w radio on/off status */
	u32	U1BC_LEDConfigure;
	u32	D00_DmaControl;
	u32	M00_MacControl;
	union {
		struct {
			u32	M04_MulticastAddress1;
			u32	M08_MulticastAddress2;
		};
		u8		Multicast[8];	/* contents of card multicast registers */
	};

	u32	M24_MacControl;
	u32	M28_MacControl;
	u32	M2C_MacControl;
	u32	M38_MacControl;
	u32	M3C_MacControl;
	u32	M40_MacControl;
	u32	M44_MacControl;
	u32	M48_MacControl;
	u32	M4C_MacStatus;
	u32	M60_MacControl;
	u32	M68_MacControl;
	u32	M70_MacControl;
	u32	M74_MacControl;
	u32	M78_ERPInformation;
	u32	M7C_MacControl;
	u32	M80_MacControl;
	u32	M84_MacControl;
	u32	M88_MacControl;
	u32	M98_MacControl;

	/* Baseband register */
	u32	BB0C;	/* Used for LNA calculation */
	u32	BB2C;
	u32	BB30;	/* 11b acquisition control register */
	u32	BB3C;
	u32	BB48;
	u32	BB4C;
	u32	BB50;	/* mode control register */
	u32	BB54;
	u32	BB58;	/* IQ_ALPHA */
	u32	BB5C;	/* For test */
	u32	BB60;	/* for WTO read value */

	/* VM */
	spinlock_t	EP0VM_spin_lock; /* 4B */
	u32		EP0VM_status; /* $$ */
	struct wb35_reg_queue *reg_first;
	struct wb35_reg_queue *reg_last;
	atomic_t	RegFireCount;

	/* Hardware status */
	u8	EP0vm_state;
	u8	mac_power_save;
	u8	EEPROMPhyType; /*
				* 0 ~ 15 for Maxim (0 ĄV MAX2825, 1 ĄV MAX2827, 2 ĄV MAX2828, 3 ĄV MAX2829),
				* 16 ~ 31 for Airoha (16 ĄV AL2230, 11 - AL7230)
				* 32 ~ Reserved
				* 33 ~ 47 For WB242 ( 33 - WB242, 34 - WB242 with new Txvga 0.5 db step)
				* 48 ~ 255 ARE RESERVED.
				*/
	u8	EEPROMRegion;	/* Region setting in EEPROM */

	u32	SyncIoPause; /* If user use the Sync Io to access Hw, then pause the async access */

	u8	LNAValue[4]; /* Table for speed up running */
	u32	SQ3_filter[MAX_SQ3_FILTER_SIZE];
	u32	SQ3_index;
};

/* =====================================================================
 * Function declaration
 * =====================================================================
 */
void hal_remove_mapping_key(struct hw_data *hw_data, u8 *mac_addr);
void hal_remove_default_key(struct hw_data *hw_data, u32 index);
unsigned char hal_set_mapping_key(struct hw_data *adapter, u8 *mac_addr,
				  u8 null_key, u8 wep_on, u8 *tx_tsc,
				  u8 *rx_tsc, u8 key_type, u8 key_len,
				  u8 *key_data);
unsigned char hal_set_default_key(struct hw_data *adapter, u8 index,
				  u8 null_key, u8 wep_on, u8 *tx_tsc,
				  u8 *rx_tsc, u8 key_type, u8 key_len,
				  u8 *key_data);
void hal_clear_all_default_key(struct hw_data *hw_data);
void hal_clear_all_group_key(struct hw_data *hw_data);
void hal_clear_all_mapping_key(struct hw_data *hw_data);
void hal_clear_all_key(struct hw_data *hw_data);
void hal_set_power_save_mode(struct hw_data *hw_data, unsigned char power_save,
			     unsigned char wakeup, unsigned char dtim);
void hal_get_power_save_mode(struct hw_data *hw_data, u8 *in_pwr_save);
void hal_set_slot_time(struct hw_data *hw_data, u8 type);

#define hal_set_atim_window(_A, _ATM)

void hal_start_bss(struct hw_data *hw_data, u8 mac_op_mode);

/* 0:BSS STA 1:IBSS STA */
void hal_join_request(struct hw_data *hw_data, u8 bss_type);

void hal_stop_sync_bss(struct hw_data *hw_data);
void hal_resume_sync_bss(struct hw_data *hw_data);
void hal_set_aid(struct hw_data *hw_data, u16 aid);
void hal_set_bssid(struct hw_data *hw_data, u8 *bssid);
void hal_get_bssid(struct hw_data *hw_data, u8 *bssid);
void hal_set_listen_interval(struct hw_data *hw_data, u16 listen_interval);
void hal_set_cap_info(struct hw_data *hw_data, u16 capability_info);
void hal_set_ssid(struct hw_data *hw_data, u8 *ssid, u8 ssid_len);
void hal_start_tx0(struct hw_data *hw_data);

#define hal_get_cwmin(_A)	((_A)->cwmin)

void hal_set_cwmax(struct hw_data *hw_data, u16 cwin_max);

#define hal_get_cwmax(_A)	((_A)->cwmax)

void hal_set_rsn_wpa(struct hw_data *hw_data, u32 *rsn_ie_bitmap,
		     u32 *rsn_oui_type , unsigned char desired_auth_mode);
void hal_set_connect_info(struct hw_data *hw_data, unsigned char bo_connect);
u8 hal_get_est_sq3(struct hw_data *hw_data, u8 count);
void hal_descriptor_indicate(struct hw_data *hw_data,
			     struct wb35_descriptor *des);
u8 hal_get_antenna_number(struct hw_data *hw_data);
u32 hal_get_bss_pk_cnt(struct hw_data *hw_data);

#define hal_get_region_from_EEPROM(_A)	((_A)->reg.EEPROMRegion)
#define hal_get_tx_buffer(_A, _B)	Wb35Tx_get_tx_buffer(_A, _B)
#define hal_software_set(_A)		(_A->SoftwareSet)
#define hal_driver_init_OK(_A)		(_A->IsInitOK)
#define hal_rssi_boundary_high(_A)	(_A->RSSI_high)
#define hal_rssi_boundary_low(_A)	(_A->RSSI_low)
#define hal_scan_interval(_A)		(_A->Scan_Interval)

#define PHY_DEBUG(msg, args...)

/* return 100ms count */
#define hal_get_time_count(_P)		(_P->time_count / 10)
#define hal_detect_error(_P)		(_P->WbUsb.DetectCount)

#define hal_ibss_disconnect(_A)		(hal_stop_sync_bss(_A))

#endif
