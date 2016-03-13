/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#ifndef __RTL8188E_XMIT_H__
#define __RTL8188E_XMIT_H__

#define		MAX_TX_AGG_PACKET_NUMBER	0xFF
/*  */
/*  Queue Select Value in TxDesc */
/*  */
#define QSLT_BK							0x2/* 0x01 */
#define QSLT_BE							0x0
#define QSLT_VI							0x5/* 0x4 */
#define QSLT_VO							0x7/* 0x6 */
#define QSLT_BEACON						0x10
#define QSLT_HIGH						0x11
#define QSLT_MGNT						0x12
#define QSLT_CMD						0x13

/* For 88e early mode */
#define SET_EARLYMODE_PKTNUM(__pAddr, __Value)			\
	SET_BITS_TO_LE_4BYTE(__pAddr, 0, 3, __Value)
#define SET_EARLYMODE_LEN0(__pAddr, __Value)			\
	SET_BITS_TO_LE_4BYTE(__pAddr, 4, 12, __Value)
#define SET_EARLYMODE_LEN1(__pAddr, __Value)			\
	SET_BITS_TO_LE_4BYTE(__pAddr, 16, 12, __Value)
#define SET_EARLYMODE_LEN2_1(__pAddr, __Value)			\
	SET_BITS_TO_LE_4BYTE(__pAddr, 28, 4, __Value)
#define SET_EARLYMODE_LEN2_2(__pAddr, __Value)			\
	SET_BITS_TO_LE_4BYTE(__pAddr+4, 0, 8, __Value)
#define SET_EARLYMODE_LEN3(__pAddr, __Value)			\
	SET_BITS_TO_LE_4BYTE(__pAddr+4, 8, 12, __Value)
#define SET_EARLYMODE_LEN4(__pAddr, __Value)			\
	SET_BITS_TO_LE_4BYTE(__pAddr+4, 20, 12, __Value)

/*  */
/* defined for TX DESC Operation */
/*  */

#define MAX_TID (15)

/* OFFSET 0 */
#define OFFSET_SZ	0
#define OFFSET_SHT	16
#define BMC		BIT(24)
#define LSG		BIT(26)
#define FSG		BIT(27)
#define OWN		BIT(31)


/* OFFSET 4 */
#define PKT_OFFSET_SZ		0
#define QSEL_SHT		8
#define RATE_ID_SHT		16
#define NAVUSEHDR		BIT(20)
#define SEC_TYPE_SHT		22
#define PKT_OFFSET_SHT		26

/* OFFSET 8 */
#define AGG_EN			BIT(12)
#define AGG_BK			BIT(16)
#define AMPDU_DENSITY_SHT	20
#define ANTSEL_A		BIT(24)
#define ANTSEL_B		BIT(25)
#define TX_ANT_CCK_SHT		26
#define TX_ANTL_SHT		28
#define TX_ANT_HT_SHT		30

/* OFFSET 12 */
#define SEQ_SHT			16
#define EN_HWSEQ		BIT(31)

/* OFFSET 16 */
#define QOS			BIT(6)
#define	HW_SSN			BIT(7)
#define USERATE			BIT(8)
#define DISDATAFB		BIT(10)
#define CTS_2_SELF		BIT(11)
#define	RTS_EN			BIT(12)
#define	HW_RTS_EN		BIT(13)
#define DATA_SHORT		BIT(24)
#define PWR_STATUS_SHT		15
#define DATA_SC_SHT		20
#define DATA_BW			BIT(25)

/* OFFSET 20 */
#define	RTY_LMT_EN		BIT(17)

enum TXDESC_SC {
	SC_DONT_CARE = 0x00,
	SC_UPPER = 0x01,
	SC_LOWER = 0x02,
	SC_DUPLICATE = 0x03
};
/* OFFSET 20 */
#define SGI			BIT(6)
#define USB_TXAGG_NUM_SHT	24

#define txdesc_set_ccx_sw_88e(txdesc, value) \
	do { \
		((struct txdesc_88e *)(txdesc))->sw1 = (((value)>>8) & 0x0f); \
		((struct txdesc_88e *)(txdesc))->sw0 = ((value) & 0xff); \
	} while (0)

struct txrpt_ccx_88e {
	/* offset 0 */
	u8 tag1:1;
	u8 pkt_num:3;
	u8 txdma_underflow:1;
	u8 int_bt:1;
	u8 int_tri:1;
	u8 int_ccx:1;

	/* offset 1 */
	u8 mac_id:6;
	u8 pkt_ok:1;
	u8 bmc:1;

	/* offset 2 */
	u8 retry_cnt:6;
	u8 lifetime_over:1;
	u8 retry_over:1;

	/* offset 3 */
	u8 ccx_qtime0;
	u8 ccx_qtime1;

	/* offset 5 */
	u8 final_data_rate;

	/* offset 6 */
	u8 sw1:4;
	u8 qsel:4;

	/* offset 7 */
	u8 sw0;
};

#define txrpt_ccx_sw_88e(txrpt_ccx) ((txrpt_ccx)->sw0 + ((txrpt_ccx)->sw1<<8))
#define txrpt_ccx_qtime_88e(txrpt_ccx)			\
	((txrpt_ccx)->ccx_qtime0+((txrpt_ccx)->ccx_qtime1<<8))

void rtl8188e_fill_fake_txdesc(struct adapter *padapter, u8 *pDesc,
			       u32 BufferLen, u8 IsPsPoll, u8 IsBTQosNull);
s32 rtl8188eu_init_xmit_priv(struct adapter *padapter);
s32 rtl8188eu_hal_xmit(struct adapter *padapter, struct xmit_frame *frame);
s32 rtl8188eu_mgnt_xmit(struct adapter *padapter, struct xmit_frame *frame);
s32 rtl8188eu_xmit_buf_handler(struct adapter *padapter);
#define hal_xmit_handler rtl8188eu_xmit_buf_handler
void rtl8188eu_xmit_tasklet(void *priv);
s32 rtl8188eu_xmitframe_complete(struct adapter *padapter,
				 struct xmit_priv *pxmitpriv,
				 struct xmit_buf *pxmitbuf);

void dump_txrpt_ccx_88e(void *buf);
void handle_txrpt_ccx_88e(struct adapter *adapter, u8 *buf);

void _dbg_dump_tx_info(struct adapter *padapter, int frame_tag,
		       struct tx_desc *ptxdesc);

#endif /* __RTL8188E_XMIT_H__ */
