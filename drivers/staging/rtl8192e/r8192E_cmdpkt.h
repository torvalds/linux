/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#ifndef R819XUSB_CMDPKT_H
#define R819XUSB_CMDPKT_H
#define	CMPK_RX_TX_FB_SIZE		sizeof(struct cmpk_txfb)
#define CMPK_TX_SET_CONFIG_SIZE		sizeof(struct cmpk_set_cfg)
#define CMPK_BOTH_QUERY_CONFIG_SIZE	sizeof(struct cmpk_set_cfg)
#define CMPK_RX_TX_STS_SIZE		sizeof(struct cmpk_tx_status)
#define CMPK_RX_DBG_MSG_SIZE		sizeof(struct cmpk_rx_dbginfo)
#define	CMPK_TX_RAHIS_SIZE		sizeof(struct cmpk_tx_rahis)

#define ISR_TxBcnOk			BIT27
#define ISR_TxBcnErr			BIT26
#define ISR_BcnTimerIntr		BIT13


struct cmpk_txfb {
	u8	element_id;
	u8	length;
	u8	TID:4;				/* */
	u8	fail_reason:3;		/* */
	u8	tok:1;
	u8	reserve1:4;			/* */
	u8	pkt_type:2;		/* */
	u8	bandwidth:1;		/* */
	u8	qos_pkt:1;			/* */

	u8	reserve2;			/* */
	u8	retry_cnt;			/* */
	u16	pkt_id;				/* */

	u16	seq_num;			/* */
	u8	s_rate;
	u8	f_rate;

	u8	s_rts_rate;			/* */
	u8	f_rts_rate;			/* */
	u16	pkt_length;			/* */

	u16	reserve3;			/* */
	u16	duration;			/* */
};

struct cmpk_intr_sta {
	u8	element_id;
	u8	length;
	u16	reserve;
	u32	interrupt_status;
};


struct cmpk_set_cfg {
	u8	element_id;
	u8	length;
	u16	reserve1;
	u8	cfg_reserve1:3;
	u8	cfg_size:2;
	u8	cfg_type:2;
	u8	cfg_action:1;
	u8	cfg_reserve2;
	u8	cfg_page:4;
	u8	cfg_reserve3:4;
	u8	cfg_offset;
	u32	value;
	u32	mask;
};

#define		cmpk_query_cfg_t	struct cmpk_set_cfg

struct cmpk_tx_status {
	u16	reserve1;
	u8	length;
	u8	element_id;

	u16	txfail;
	u16	txok;

	u16	txmcok;
	u16	txretry;

	u16  txucok;
	u16	txbcok;

	u16	txbcfail;
	u16	txmcfail;

	u16	reserve2;
	u16	txucfail;

	u32	txmclength;
	u32	txbclength;
	u32	txuclength;

	u16	reserve3_23;
	u8	reserve3_1;
	u8	rate;
} __packed;

struct cmpk_rx_dbginfo {
	u16	reserve1;
	u8	length;
	u8	element_id;


};

struct cmpk_tx_rahis {
	u8	element_id;
	u8	length;
	u16	reserved1;

	u16	cck[4];

	u16	ofdm[8];





	u16	ht_mcs[4][16];

} __packed;

enum cmpk_element {
	RX_TX_FEEDBACK = 0,
	RX_INTERRUPT_STATUS		= 1,
	TX_SET_CONFIG			= 2,
	BOTH_QUERY_CONFIG		= 3,
	RX_TX_STATUS			= 4,
	RX_DBGINFO_FEEDBACK		= 5,
	RX_TX_PER_PKT_FEEDBACK		= 6,
	RX_TX_RATE_HISTORY		= 7,
	RX_CMD_ELE_MAX
};

extern  u32 cmpk_message_handle_rx(struct net_device *dev,
				   struct rtllib_rx_stats *pstats);
extern bool cmpk_message_handle_tx(struct net_device *dev,
				   u8 *codevirtualaddress, u32 packettype,
				   u32 buffer_len);


#endif
