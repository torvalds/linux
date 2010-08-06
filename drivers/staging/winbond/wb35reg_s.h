#ifndef __WINBOND_WB35REG_S_H
#define __WINBOND_WB35REG_S_H

#include <linux/spinlock.h>
#include <linux/types.h>
#include <asm/atomic.h>

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

#ifdef _USE_FALLBACK_RATE_
#define DEFAULT_RATE_RETRY_LIMIT	2	/* (M38) as named */
#else
#define DEFAULT_RATE_RETRY_LIMIT	7	/* (M38) as named */
#endif

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
#endif
