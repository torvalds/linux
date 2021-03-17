/* SPDX-License-Identifier: GPL-2.0-only */
/* hermes.h
 *
 * Driver core for the "Hermes" wireless MAC controller, as used in
 * the Lucent Orinoco and Cabletron RoamAbout cards. It should also
 * work on the hfa3841 and hfa3842 MAC controller chips used in the
 * Prism I & II chipsets.
 *
 * This is not a complete driver, just low-level access routines for
 * the MAC controller itself.
 *
 * Based on the prism2 driver from Absolute Value Systems' linux-wlan
 * project, the Linux wvlan_cs driver, Lucent's HCF-Light
 * (wvlan_hcf.c) library, and the NetBSD wireless driver.
 *
 * Copyright (C) 2000, David Gibson, Linuxcare Australia.
 * (C) Copyright David Gibson, IBM Corp. 2001-2003.
 *
 * Portions taken from hfa384x.h.
 * Copyright (C) 1999 AbsoluteValue Systems, Inc. All Rights Reserved.
 */

#ifndef _HERMES_H
#define _HERMES_H

/* Notes on locking:
 *
 * As a module of low level hardware access routines, there is no
 * locking. Users of this module should ensure that they serialize
 * access to the hermes structure, and to the hardware
*/

#include <linux/if_ether.h>
#include <linux/io.h>

/*
 * Limits and constants
 */
#define		HERMES_ALLOC_LEN_MIN		(4)
#define		HERMES_ALLOC_LEN_MAX		(2400)
#define		HERMES_LTV_LEN_MAX		(34)
#define		HERMES_BAP_DATALEN_MAX		(4096)
#define		HERMES_BAP_OFFSET_MAX		(4096)
#define		HERMES_PORTID_MAX		(7)
#define		HERMES_NUMPORTS_MAX		(HERMES_PORTID_MAX + 1)
#define		HERMES_PDR_LEN_MAX		(260)	/* in bytes, from EK */
#define		HERMES_PDA_RECS_MAX		(200)	/* a guess */
#define		HERMES_PDA_LEN_MAX		(1024)	/* in bytes, from EK */
#define		HERMES_SCANRESULT_MAX		(35)
#define		HERMES_CHINFORESULT_MAX		(8)
#define		HERMES_MAX_MULTICAST		(16)
#define		HERMES_MAGIC			(0x7d1f)

/*
 * Hermes register offsets
 */
#define		HERMES_CMD			(0x00)
#define		HERMES_PARAM0			(0x02)
#define		HERMES_PARAM1			(0x04)
#define		HERMES_PARAM2			(0x06)
#define		HERMES_STATUS			(0x08)
#define		HERMES_RESP0			(0x0A)
#define		HERMES_RESP1			(0x0C)
#define		HERMES_RESP2			(0x0E)
#define		HERMES_INFOFID			(0x10)
#define		HERMES_RXFID			(0x20)
#define		HERMES_ALLOCFID			(0x22)
#define		HERMES_TXCOMPLFID		(0x24)
#define		HERMES_SELECT0			(0x18)
#define		HERMES_OFFSET0			(0x1C)
#define		HERMES_DATA0			(0x36)
#define		HERMES_SELECT1			(0x1A)
#define		HERMES_OFFSET1			(0x1E)
#define		HERMES_DATA1			(0x38)
#define		HERMES_EVSTAT			(0x30)
#define		HERMES_INTEN			(0x32)
#define		HERMES_EVACK			(0x34)
#define		HERMES_CONTROL			(0x14)
#define		HERMES_SWSUPPORT0		(0x28)
#define		HERMES_SWSUPPORT1		(0x2A)
#define		HERMES_SWSUPPORT2		(0x2C)
#define		HERMES_AUXPAGE			(0x3A)
#define		HERMES_AUXOFFSET		(0x3C)
#define		HERMES_AUXDATA			(0x3E)

/*
 * CMD register bitmasks
 */
#define		HERMES_CMD_BUSY			(0x8000)
#define		HERMES_CMD_AINFO		(0x7f00)
#define		HERMES_CMD_MACPORT		(0x0700)
#define		HERMES_CMD_RECL			(0x0100)
#define		HERMES_CMD_WRITE		(0x0100)
#define		HERMES_CMD_PROGMODE		(0x0300)
#define		HERMES_CMD_CMDCODE		(0x003f)

/*
 * STATUS register bitmasks
 */
#define		HERMES_STATUS_RESULT		(0x7f00)
#define		HERMES_STATUS_CMDCODE		(0x003f)

/*
 * OFFSET register bitmasks
 */
#define		HERMES_OFFSET_BUSY		(0x8000)
#define		HERMES_OFFSET_ERR		(0x4000)
#define		HERMES_OFFSET_DATAOFF		(0x0ffe)

/*
 * Event register bitmasks (INTEN, EVSTAT, EVACK)
 */
#define		HERMES_EV_TICK			(0x8000)
#define		HERMES_EV_WTERR			(0x4000)
#define		HERMES_EV_INFDROP		(0x2000)
#define		HERMES_EV_INFO			(0x0080)
#define		HERMES_EV_DTIM			(0x0020)
#define		HERMES_EV_CMD			(0x0010)
#define		HERMES_EV_ALLOC			(0x0008)
#define		HERMES_EV_TXEXC			(0x0004)
#define		HERMES_EV_TX			(0x0002)
#define		HERMES_EV_RX			(0x0001)

/*
 * Command codes
 */
/*--- Controller Commands ----------------------------*/
#define		HERMES_CMD_INIT			(0x0000)
#define		HERMES_CMD_ENABLE		(0x0001)
#define		HERMES_CMD_DISABLE		(0x0002)
#define		HERMES_CMD_DIAG			(0x0003)

/*--- Buffer Mgmt Commands ---------------------------*/
#define		HERMES_CMD_ALLOC		(0x000A)
#define		HERMES_CMD_TX			(0x000B)

/*--- Regulate Commands ------------------------------*/
#define		HERMES_CMD_NOTIFY		(0x0010)
#define		HERMES_CMD_INQUIRE		(0x0011)

/*--- Configure Commands -----------------------------*/
#define		HERMES_CMD_ACCESS		(0x0021)
#define		HERMES_CMD_DOWNLD		(0x0022)

/*--- Serial I/O Commands ----------------------------*/
#define		HERMES_CMD_READMIF		(0x0030)
#define		HERMES_CMD_WRITEMIF		(0x0031)

/*--- Debugging Commands -----------------------------*/
#define		HERMES_CMD_TEST			(0x0038)


/* Test command arguments */
#define		HERMES_TEST_SET_CHANNEL		0x0800
#define		HERMES_TEST_MONITOR		0x0b00
#define		HERMES_TEST_STOP		0x0f00

/* Authentication algorithms */
#define		HERMES_AUTH_OPEN		1
#define		HERMES_AUTH_SHARED_KEY		2

/* WEP settings */
#define		HERMES_WEP_PRIVACY_INVOKED	0x0001
#define		HERMES_WEP_EXCL_UNENCRYPTED	0x0002
#define		HERMES_WEP_HOST_ENCRYPT		0x0010
#define		HERMES_WEP_HOST_DECRYPT		0x0080

/* Symbol hostscan options */
#define		HERMES_HOSTSCAN_SYMBOL_5SEC	0x0001
#define		HERMES_HOSTSCAN_SYMBOL_ONCE	0x0002
#define		HERMES_HOSTSCAN_SYMBOL_PASSIVE	0x0040
#define		HERMES_HOSTSCAN_SYMBOL_BCAST	0x0080

/*
 * Frame structures and constants
 */

#define HERMES_DESCRIPTOR_OFFSET	0
#define HERMES_802_11_OFFSET		(14)
#define HERMES_802_3_OFFSET		(14 + 32)
#define HERMES_802_2_OFFSET		(14 + 32 + 14)
#define HERMES_TXCNTL2_OFFSET		(HERMES_802_3_OFFSET - 2)

#define HERMES_RXSTAT_ERR		(0x0003)
#define	HERMES_RXSTAT_BADCRC		(0x0001)
#define	HERMES_RXSTAT_UNDECRYPTABLE	(0x0002)
#define	HERMES_RXSTAT_MIC		(0x0010)	/* Frame contains MIC */
#define	HERMES_RXSTAT_MACPORT		(0x0700)
#define HERMES_RXSTAT_PCF		(0x1000)	/* Frame was received in CF period */
#define	HERMES_RXSTAT_MIC_KEY_ID	(0x1800)	/* MIC key used */
#define	HERMES_RXSTAT_MSGTYPE		(0xE000)
#define	HERMES_RXSTAT_1042		(0x2000)	/* RFC-1042 frame */
#define	HERMES_RXSTAT_TUNNEL		(0x4000)	/* bridge-tunnel encoded frame */
#define	HERMES_RXSTAT_WMP		(0x6000)	/* Wavelan-II Management Protocol frame */

/* Shift amount for key ID in RXSTAT and TXCTRL */
#define	HERMES_MIC_KEY_ID_SHIFT		11

struct hermes_tx_descriptor {
	__le16 status;
	__le16 reserved1;
	__le16 reserved2;
	__le32 sw_support;
	u8 retry_count;
	u8 tx_rate;
	__le16 tx_control;
} __packed;

#define HERMES_TXSTAT_RETRYERR		(0x0001)
#define HERMES_TXSTAT_AGEDERR		(0x0002)
#define HERMES_TXSTAT_DISCON		(0x0004)
#define HERMES_TXSTAT_FORMERR		(0x0008)

#define HERMES_TXCTRL_TX_OK		(0x0002)	/* ?? interrupt on Tx complete */
#define HERMES_TXCTRL_TX_EX		(0x0004)	/* ?? interrupt on Tx exception */
#define HERMES_TXCTRL_802_11		(0x0008)	/* We supply 802.11 header */
#define HERMES_TXCTRL_MIC		(0x0010)	/* 802.3 + TKIP */
#define HERMES_TXCTRL_MIC_KEY_ID	(0x1800)	/* MIC Key ID mask */
#define HERMES_TXCTRL_ALT_RTRY		(0x0020)

/* Inquiry constants and data types */

#define HERMES_INQ_TALLIES		(0xF100)
#define HERMES_INQ_SCAN			(0xF101)
#define HERMES_INQ_CHANNELINFO		(0xF102)
#define HERMES_INQ_HOSTSCAN		(0xF103)
#define HERMES_INQ_HOSTSCAN_SYMBOL	(0xF104)
#define HERMES_INQ_LINKSTATUS		(0xF200)
#define HERMES_INQ_SEC_STAT_AGERE	(0xF202)

struct hermes_tallies_frame {
	__le16 TxUnicastFrames;
	__le16 TxMulticastFrames;
	__le16 TxFragments;
	__le16 TxUnicastOctets;
	__le16 TxMulticastOctets;
	__le16 TxDeferredTransmissions;
	__le16 TxSingleRetryFrames;
	__le16 TxMultipleRetryFrames;
	__le16 TxRetryLimitExceeded;
	__le16 TxDiscards;
	__le16 RxUnicastFrames;
	__le16 RxMulticastFrames;
	__le16 RxFragments;
	__le16 RxUnicastOctets;
	__le16 RxMulticastOctets;
	__le16 RxFCSErrors;
	__le16 RxDiscards_NoBuffer;
	__le16 TxDiscardsWrongSA;
	__le16 RxWEPUndecryptable;
	__le16 RxMsgInMsgFragments;
	__le16 RxMsgInBadMsgFragments;
	/* Those last are probably not available in very old firmwares */
	__le16 RxDiscards_WEPICVError;
	__le16 RxDiscards_WEPExcluded;
} __packed;

/* Grabbed from wlan-ng - Thanks Mark... - Jean II
 * This is the result of a scan inquiry command */
/* Structure describing info about an Access Point */
struct prism2_scan_apinfo {
	__le16 channel;		/* Channel where the AP sits */
	__le16 noise;		/* Noise level */
	__le16 level;		/* Signal level */
	u8 bssid[ETH_ALEN];	/* MAC address of the Access Point */
	__le16 beacon_interv;	/* Beacon interval */
	__le16 capabilities;	/* Capabilities */
	__le16 essid_len;	/* ESSID length */
	u8 essid[32];		/* ESSID of the network */
	u8 rates[10];		/* Bit rate supported */
	__le16 proberesp_rate;	/* Data rate of the response frame */
	__le16 atim;		/* ATIM window time, Kus (hostscan only) */
} __packed;

/* Same stuff for the Lucent/Agere card.
 * Thanks to h1kari <h1kari AT dachb0den.com> - Jean II */
struct agere_scan_apinfo {
	__le16 channel;		/* Channel where the AP sits */
	__le16 noise;		/* Noise level */
	__le16 level;		/* Signal level */
	u8 bssid[ETH_ALEN];	/* MAC address of the Access Point */
	__le16 beacon_interv;	/* Beacon interval */
	__le16 capabilities;	/* Capabilities */
	/* bits: 0-ess, 1-ibss, 4-privacy [wep] */
	__le16 essid_len;	/* ESSID length */
	u8 essid[32];		/* ESSID of the network */
} __packed;

/* Moustafa: Scan structure for Symbol cards */
struct symbol_scan_apinfo {
	u8 channel;		/* Channel where the AP sits */
	u8 unknown1;		/* 8 in 2.9x and 3.9x f/w, 0 otherwise */
	__le16 noise;		/* Noise level */
	__le16 level;		/* Signal level */
	u8 bssid[ETH_ALEN];	/* MAC address of the Access Point */
	__le16 beacon_interv;	/* Beacon interval */
	__le16 capabilities;	/* Capabilities */
	/* bits: 0-ess, 1-ibss, 4-privacy [wep] */
	__le16 essid_len;	/* ESSID length */
	u8 essid[32];		/* ESSID of the network */
	__le16 rates[5];	/* Bit rate supported */
	__le16 basic_rates;	/* Basic rates bitmask */
	u8 unknown2[6];		/* Always FF:FF:FF:FF:00:00 */
	u8 unknown3[8];		/* Always 0, appeared in f/w 3.91-68 */
} __packed;

union hermes_scan_info {
	struct agere_scan_apinfo	a;
	struct prism2_scan_apinfo	p;
	struct symbol_scan_apinfo	s;
};

/* Extended scan struct for HERMES_INQ_CHANNELINFO.
 * wl_lkm calls this an ACS scan (Automatic Channel Select).
 * Keep out of union hermes_scan_info because it is much bigger than
 * the older scan structures. */
struct agere_ext_scan_info {
	__le16	reserved0;

	u8	noise;
	u8	level;
	u8	rx_flow;
	u8	rate;
	__le16	reserved1[2];

	__le16	frame_control;
	__le16	dur_id;
	u8	addr1[ETH_ALEN];
	u8	addr2[ETH_ALEN];
	u8	bssid[ETH_ALEN];
	__le16	sequence;
	u8	addr4[ETH_ALEN];

	__le16	data_length;

	/* Next 3 fields do not get filled in. */
	u8	daddr[ETH_ALEN];
	u8	saddr[ETH_ALEN];
	__le16	len_type;

	__le64	timestamp;
	__le16	beacon_interval;
	__le16	capabilities;
	u8	data[];
} __packed;

#define HERMES_LINKSTATUS_NOT_CONNECTED   (0x0000)
#define HERMES_LINKSTATUS_CONNECTED       (0x0001)
#define HERMES_LINKSTATUS_DISCONNECTED    (0x0002)
#define HERMES_LINKSTATUS_AP_CHANGE       (0x0003)
#define HERMES_LINKSTATUS_AP_OUT_OF_RANGE (0x0004)
#define HERMES_LINKSTATUS_AP_IN_RANGE     (0x0005)
#define HERMES_LINKSTATUS_ASSOC_FAILED    (0x0006)

struct hermes_linkstatus {
	__le16 linkstatus;         /* Link status */
} __packed;

struct hermes_response {
	u16 status, resp0, resp1, resp2;
};

/* "ID" structure - used for ESSID and station nickname */
struct hermes_idstring {
	__le16 len;
	__le16 val[16];
} __packed;

struct hermes_multicast {
	u8 addr[HERMES_MAX_MULTICAST][ETH_ALEN];
} __packed;

/* Timeouts */
#define HERMES_BAP_BUSY_TIMEOUT (10000) /* In iterations of ~1us */

struct hermes;

/* Functions to access hardware */
struct hermes_ops {
	int (*init)(struct hermes *hw);
	int (*cmd_wait)(struct hermes *hw, u16 cmd, u16 parm0,
			struct hermes_response *resp);
	int (*init_cmd_wait)(struct hermes *hw, u16 cmd,
			     u16 parm0, u16 parm1, u16 parm2,
			     struct hermes_response *resp);
	int (*allocate)(struct hermes *hw, u16 size, u16 *fid);
	int (*read_ltv)(struct hermes *hw, int bap, u16 rid, unsigned buflen,
			u16 *length, void *buf);
	int (*write_ltv)(struct hermes *hw, int bap, u16 rid,
			 u16 length, const void *value);
	int (*bap_pread)(struct hermes *hw, int bap, void *buf, int len,
			 u16 id, u16 offset);
	int (*bap_pwrite)(struct hermes *hw, int bap, const void *buf,
			  int len, u16 id, u16 offset);
	int (*read_pda)(struct hermes *hw, __le16 *pda,
			u32 pda_addr, u16 pda_len);
	int (*program_init)(struct hermes *hw, u32 entry_point);
	int (*program_end)(struct hermes *hw);
	int (*program)(struct hermes *hw, const char *buf,
		       u32 addr, u32 len);
	void (*lock_irqsave)(spinlock_t *lock, unsigned long *flags);
	void (*unlock_irqrestore)(spinlock_t *lock, unsigned long *flags);
	void (*lock_irq)(spinlock_t *lock);
	void (*unlock_irq)(spinlock_t *lock);
};

/* Basic control structure */
struct hermes {
	void __iomem *iobase;
	int reg_spacing;
#define HERMES_16BIT_REGSPACING	0
#define HERMES_32BIT_REGSPACING	1
	u16 inten; /* Which interrupts should be enabled? */
	bool eeprom_pda;
	const struct hermes_ops *ops;
	void *priv;
};

/* Register access convenience macros */
#define hermes_read_reg(hw, off) \
	(ioread16((hw)->iobase + ((off) << (hw)->reg_spacing)))
#define hermes_write_reg(hw, off, val) \
	(iowrite16((val), (hw)->iobase + ((off) << (hw)->reg_spacing)))
#define hermes_read_regn(hw, name) hermes_read_reg((hw), HERMES_##name)
#define hermes_write_regn(hw, name, val) \
	hermes_write_reg((hw), HERMES_##name, (val))

/* Function prototypes */
void hermes_struct_init(struct hermes *hw, void __iomem *address,
			int reg_spacing);

/* Inline functions */

static inline int hermes_present(struct hermes *hw)
{
	return hermes_read_regn(hw, SWSUPPORT0) == HERMES_MAGIC;
}

static inline void hermes_set_irqmask(struct hermes *hw, u16 events)
{
	hw->inten = events;
	hermes_write_regn(hw, INTEN, events);
}

static inline int hermes_enable_port(struct hermes *hw, int port)
{
	return hw->ops->cmd_wait(hw, HERMES_CMD_ENABLE | (port << 8),
				 0, NULL);
}

static inline int hermes_disable_port(struct hermes *hw, int port)
{
	return hw->ops->cmd_wait(hw, HERMES_CMD_DISABLE | (port << 8),
				 0, NULL);
}

/* Initiate an INQUIRE command (tallies or scan).  The result will come as an
 * information frame in __orinoco_ev_info() */
static inline int hermes_inquire(struct hermes *hw, u16 rid)
{
	return hw->ops->cmd_wait(hw, HERMES_CMD_INQUIRE, rid, NULL);
}

#define HERMES_BYTES_TO_RECLEN(n) ((((n) + 1) / 2) + 1)
#define HERMES_RECLEN_TO_BYTES(n) (((n) - 1) * 2)

/* Note that for the next two, the count is in 16-bit words, not bytes */
static inline void hermes_read_words(struct hermes *hw, int off,
				     void *buf, unsigned count)
{
	off = off << hw->reg_spacing;
	ioread16_rep(hw->iobase + off, buf, count);
}

static inline void hermes_write_bytes(struct hermes *hw, int off,
				      const char *buf, unsigned count)
{
	off = off << hw->reg_spacing;
	iowrite16_rep(hw->iobase + off, buf, count >> 1);
	if (unlikely(count & 1))
		iowrite8(buf[count - 1], hw->iobase + off);
}

static inline void hermes_clear_words(struct hermes *hw, int off,
				      unsigned count)
{
	unsigned i;

	off = off << hw->reg_spacing;

	for (i = 0; i < count; i++)
		iowrite16(0, hw->iobase + off);
}

#define HERMES_READ_RECORD(hw, bap, rid, buf) \
	(hw->ops->read_ltv((hw), (bap), (rid), sizeof(*buf), NULL, (buf)))
#define HERMES_WRITE_RECORD(hw, bap, rid, buf) \
	(hw->ops->write_ltv((hw), (bap), (rid), \
			    HERMES_BYTES_TO_RECLEN(sizeof(*buf)), (buf)))

static inline int hermes_read_wordrec(struct hermes *hw, int bap, u16 rid,
				      u16 *word)
{
	__le16 rec;
	int err;

	err = HERMES_READ_RECORD(hw, bap, rid, &rec);
	*word = le16_to_cpu(rec);
	return err;
}

static inline int hermes_write_wordrec(struct hermes *hw, int bap, u16 rid,
				       u16 word)
{
	__le16 rec = cpu_to_le16(word);
	return HERMES_WRITE_RECORD(hw, bap, rid, &rec);
}

#endif  /* _HERMES_H */
