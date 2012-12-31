/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/ 
#ifndef _RTL8712_XMIT_H_
#define _RTL8712_XMIT_H_

#define HWXMIT_ENTRY	4

#define VO_QUEUE_INX	0
#define VI_QUEUE_INX	1
#define BE_QUEUE_INX	2
#define BK_QUEUE_INX	3
#define TS_QUEUE_INX	4
#define MGT_QUEUE_INX	5
#define BMC_QUEUE_INX	6
#define BCN_QUEUE_INX	7

#define HW_QUEUE_ENTRY	8

#define TXDESC_SIZE 32
#define TXDESC_OFFSET TXDESC_SIZE

#define NR_AMSDU_XMITFRAME 8
#define NR_TXAGG_XMITFRAME 8

#define MAX_AMSDU_XMITBUF_SZ 8704
#define MAX_TXAGG_XMITBUF_SZ 16384 //16k


#define tx_cmd tx_desc


//
//defined for TX DESC Operation
//

#define MAX_TID (15)

//OFFSET 0
#define OFFSET_SZ (0)
#define OFFSET_SHT (16)
#define OWN 	BIT(31)
#define FSG	BIT(27)
#define LSG	BIT(26)
#define TYPE_SHT (24)
#define TYPE_MSK (0x03000000)

//OFFSET 4
#define PKT_OFFSET_SZ (0)
#define QSEL_SHT (8)
#define HWPC BIT(31)

//OFFSET 8
#define BMC BIT(7)
#define BK BIT(30)
#define AGG_EN BIT(29)
#define RTS_RC_SHT (16)

//OFFSET 12
#define SEQ_SHT (16)

//OFFSET 16
#define TXBW BIT(18)

//OFFSET 20
#define DISFB BIT(15)
#define RSVD6_MSK (0x00E00000)
#define RSVD6_SHT (21)

struct tx_desc{

	//DWORD 0
	unsigned int txdw0;

	unsigned int txdw1;

	unsigned int txdw2;

	unsigned int txdw3;

	unsigned int txdw4;

	unsigned int txdw5;

	unsigned int txdw6;

	unsigned int txdw7;	

};


union txdesc {
	struct tx_desc txdesc;
	unsigned int value[TXDESC_SIZE>>2];	
};


#if 0
#define tx_desc tx_cmd

struct tx_cmd {

#ifdef CONFIG_LITTLE_ENDIAN
	// DWORD 1
	unsigned int	txpktsize:16;
	unsigned int	offset:8;
	unsigned int	frame_type:2;
	unsigned int	ls:1;
	unsigned int	fs:1;
	unsigned int	linip:1;
	unsigned int	amsdu:1;
	unsigned int	gf:1;
	unsigned int	own:1;	
	// DWORD 2
	unsigned int	macid:5;			
	unsigned int	moredata:1;
	unsigned int	morefrag:1;
	unsigned int	pifs:1;
	unsigned int	qsel:5;
	unsigned int	ack_policy:2;
	unsigned int	noacm:1;
	unsigned int	non_qos:1;
	unsigned int	key_id:2;
	unsigned int	oui:1;
	unsigned int	pkt_type:1;
	unsigned int	en_desc_id:1;
	unsigned int	sectype:2;
	unsigned int	wds:1;//padding0
	unsigned int	htc:1;//padding1
	unsigned int	pkt_offset:5;//padding_len (hw)	
	unsigned int	hwpc:1;		
	// DWORD 3
	unsigned int	data_retry_lmt:6;
	unsigned int	rty_lmt_en:1;
	unsigned int	tsfl:5;
	unsigned int	rts_rc:6;
	unsigned int	data_rc:6;
	unsigned int	rsvd2:5;
	unsigned int	agg_en:1;
	unsigned int	bk:1;
	unsigned int	own_mac:1;
	// DWORD 4
	unsigned int	nextheadpage:8;
	unsigned int	tailpage:8;
	unsigned int	seq:12;
	unsigned int	frag:4;	
	// DWORD 5
	unsigned int	rtsrate:6;
	unsigned int	disrtsfb:1;
	unsigned int	rts_ratefb_lmt:4;
	unsigned int	cts2self:1;
	unsigned int	rtsen:1;
	unsigned int	ra_brsr_id:3;
	unsigned int	txht:1;
	unsigned int	txshort:1;//for data
	unsigned int	txbw:1;
	unsigned int	txsc:2;
	unsigned int	stbc:2;
	unsigned int	rd:1;
	unsigned int	rtsht:1;
	unsigned int	rtsshort:1;
	unsigned int	rtsbw:1;
	unsigned int	rts_sc:2;
	unsigned int	rts_stbc:2;
	unsigned int	userate:1;	
	// DWORD 6
	unsigned int	packet_id:9;
	unsigned int	txrate:6;
	unsigned int	disfb:1;
	unsigned int	data_ratefb_lmt:5;
	unsigned int	txagc:11;	
	// DWORD 7
	unsigned int	ip_chksum:16;
	unsigned int	tcp_chksum:16;
	// DWORD 8
	unsigned int	txbuffsize:16;//pcie
	unsigned int	ip_hdr_offset:8;
	unsigned int	rsvd3:7;
	unsigned int	tcp_en:1;
/*	
	// DWORD 9
	unsigned int	tx_buffer_address:32;	//pcie
	// DWORD 10
	unsigned int	next_tx_desc_address:32;	//pcie
*/	

#else

#endif

} ;


union txcmd {
	struct tx_cmd cmd;
	uint value[8];	
};



struct amsdu_xmit_frame {
	
	_list	list;
	struct	pkt_attrib	attrib;
	_pkt *pkt;
	
	int frame_tag;
	 _adapter *	padapter;
	
#ifdef CONFIG_USB_HCI
	//insert urb, irp, and irpcnt info below...      
       u8 *mem_addr;      
       u32 sz[8];	   

#if defined(PLATFORM_OS_XP)||defined(PLATFORM_LINUX)
	PURB	pxmit_urb[8];
#endif

	
#ifdef PLATFORM_WINDOWS
	PIRP		pxmit_irp[8];
#endif
	u8 bpending[8];
	sint ac_tag[8];
	sint last[8];
       uint irpcnt;         
       uint fragcnt;
		   
#endif

	uint	mem[(MAX_AMSDU_XMITBUF_SZ>>2)];	

};

struct agg_xmit_frame {
	
	_list	list;
	struct	pkt_attrib	attrib;
	_pkt *pkt;
	
	int frame_tag;
	_adapter *padapter;
	
#ifdef CONFIG_USB_HCI
	//insert urb, irp, and irpcnt info below...       
       u8 *mem_addr;      
       u32 sz[8];	   

#if defined(PLATFORM_OS_XP)||defined(PLATFORM_LINUX)
	PURB	pxmit_urb[8];
#endif

#ifdef PLATFORM_WINDOWS
	PIRP		pxmit_irp[8];
#endif
	u8 bpending[8];
	sint ac_tag[8];
	sint last[8];
       uint irpcnt;         
       uint fragcnt;
	   
#endif

	uint	mem[(MAX_TXAGG_XMITBUF_SZ>>2)];

};



struct amsdu_xmit_frame *alloc_amsdu_xmitframe(struct xmit_priv *pxmitpriv);
int free_amsdu_xmitframe(struct xmit_priv *pxmitpriv, struct amsdu_xmit_frame *pxmitframe);
struct agg_xmit_frame *alloc_txagg_xmitframe(struct xmit_priv *pxmitpriv);
int free_txagg_xmitframe(struct xmit_priv *pxmitpriv, struct agg_xmit_frame *pxmitframe);
#endif
void update_txdesc(struct xmit_frame *pxmitframe, uint *ptxdesc, int sz);
void dump_xframe(_adapter *padapter, struct xmit_frame *pxmitframe);

int xmitframe_complete(_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);
int pframe_coalesce(_adapter *padapter, struct xmit_frame	*pxmitframe, u8 *pframe);


struct xmit_frame *dequeue_one_xmitframe(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit, struct tx_servq *ptxservq, _queue *pframe_queue);
struct xmit_frame *dequeue_amsdu_xmitframe(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit, struct tx_servq *ptxservq, _queue *pframe_queue);
struct xmit_frame *dequeue_xframe(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit, sint entry);

void do_queue_select(_adapter *padapter, struct pkt_attrib *pattrib);
u32 get_ff_hwaddr(struct xmit_frame	*pxmitframe);

#ifdef CONFIG_USB_TX_AGGREGATION
u8 xmitframe_aggr_1st(struct xmit_buf * pxmitbuf, struct xmit_frame * pxmitframe);
u8 dump_aggr_xframe(struct xmit_buf * pxmitbuf, struct xmit_frame * pxmitframe);
#endif //CONFIG_USB_TX_AGGREGATION


#endif

