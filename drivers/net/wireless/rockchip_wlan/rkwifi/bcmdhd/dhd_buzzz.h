#ifndef _DHD_BUZZZ_H_INCLUDED_
#define _DHD_BUZZZ_H_INCLUDED_

*/
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#if defined(DHD_BUZZZ_LOG_ENABLED)
/*
 * Broadcom proprietary logging system. Deleted performance counters.
 */
void dhd_buzzz_attach(void);
void dhd_buzzz_detach(void);
void dhd_buzzz_panic(uint32 crash);
void dhd_buzzz_dump(void);
void dhd_buzzz_log_disable(void);
void dhd_buzzz_crash(void);

void dhd_buzzz_log0(uint32 evt_id);
void dhd_buzzz_log1(uint32 evt_id, uint32 arg1);
void dhd_buzzz_log2(uint32 evt_id, uint32 arg1, uintptr arg2);

void dhd_buzzz_fmt_reg(uint32 id, char * fmt);

extern void* dhd_os_create_buzzz_thread(void);
extern void dhd_os_destroy_buzzz_thread(void *thr_hdl);
extern void dhd_os_sched_buzzz_thread(void *thr_hdl);

#undef BUZZZ_EVT
#define BUZZZ_EVT(ID)		BUZZZ_EVT__## ID,

#undef BUZZZ_FMT
#define BUZZZ_FMT(ID, format) \
	dhd_buzzz_fmt_reg(BUZZZ_EVT__## ID, "\t" format);

typedef enum buzzz_evt_id
{
	BUZZZ_EVT__DHD = 100, /* BUZZZ_EVT(DHD) */
	BUZZZ_EVT(GENERAL_LOCK)
	BUZZZ_EVT(GENERAL_UNLOCK)
	BUZZZ_EVT(FLOWRING_LOCK)
	BUZZZ_EVT(FLOWRING_UNLOCK)
	BUZZZ_EVT(FLOWID_LOCK)
	BUZZZ_EVT(FLOWID_UNLOCK)

	BUZZZ_EVT(START_XMIT_BGN)
	BUZZZ_EVT(START_XMIT_END)
	BUZZZ_EVT(PROCESS_CTRL_BGN)
	BUZZZ_EVT(PROCESS_CTRL_END)
	BUZZZ_EVT(UPDATE_TXFLOWRINGS_BGN)
	BUZZZ_EVT(UPDATE_TXFLOWRINGS_END)
	BUZZZ_EVT(PROCESS_TXCPL_BGN)
	BUZZZ_EVT(PROCESS_TXCPL_END)
	BUZZZ_EVT(PROCESS_RXCPL_BGN)
	BUZZZ_EVT(PROCESS_RXCPL_END)

	BUZZZ_EVT(GET_SRC_ADDR)
	BUZZZ_EVT(WRITE_COMPLETE)
	BUZZZ_EVT(ALLOC_RING_SPACE)
	BUZZZ_EVT(ALLOC_RING_SPACE_RET)
	BUZZZ_EVT(ALLOC_RING_SPACE_FAIL)

	BUZZZ_EVT(PKTID_MAP_CLEAR)
	BUZZZ_EVT(PKTID_NOT_AVAILABLE)
	BUZZZ_EVT(PKTID_MAP_RSV)
	BUZZZ_EVT(PKTID_MAP_SAVE)
	BUZZZ_EVT(PKTID_MAP_ALLOC)
	BUZZZ_EVT(PKTID_MAP_FREE)
	BUZZZ_EVT(LOCKER_INUSE_ABORT)
	BUZZZ_EVT(BUFFER_TYPE_ABORT1)
	BUZZZ_EVT(BUFFER_TYPE_ABORT2)

	BUZZZ_EVT(UPD_READ_IDX)
	BUZZZ_EVT(STORE_RXCPLN_RD)
	BUZZZ_EVT(EARLY_UPD_RXCPLN_RD)

	BUZZZ_EVT(POST_TXDATA)
	BUZZZ_EVT(RETURN_RXBUF)
	BUZZZ_EVT(RXBUF_POST)
	BUZZZ_EVT(RXBUF_POST_EVENT)
	BUZZZ_EVT(RXBUF_POST_IOCTL)
	BUZZZ_EVT(RXBUF_POST_CTRL_PKTGET_FAIL)
	BUZZZ_EVT(RXBUF_POST_PKTGET_FAIL)
	BUZZZ_EVT(RXBUF_POST_PKTID_FAIL)

	BUZZZ_EVT(DHD_DUPLICATE_ALLOC)
	BUZZZ_EVT(DHD_DUPLICATE_FREE)
	BUZZZ_EVT(DHD_TEST_IS_ALLOC)
	BUZZZ_EVT(DHD_TEST_IS_FREE)

	BUZZZ_EVT(DHD_PROT_IOCT_BGN)
	BUZZZ_EVT(DHDMSGBUF_CMPLT_BGN)
	BUZZZ_EVT(DHDMSGBUF_CMPLT_END)
	BUZZZ_EVT(DHD_PROT_IOCT_END)
	BUZZZ_EVT(DHD_FILLUP_IOCT_REQST_BGN)
	BUZZZ_EVT(DHD_FILLUP_IOCT_REQST_END)
	BUZZZ_EVT(DHD_MSGBUF_RXBUF_POST_IOCTLRESP_BUFS_BGN)
	BUZZZ_EVT(DHD_MSGBUF_RXBUF_POST_IOCTLRESP_BUFS_END)
	BUZZZ_EVT(DHD_PROT_IOCTCMPLT_PROCESS_ONE)
	BUZZZ_EVT(DHD_PROT_IOCTCMPLT_PROCESS_TWO)
	BUZZZ_EVT(DHD_PROT_EVENT_PROCESS_BGN)
	BUZZZ_EVT(DHD_PROT_EVENT_PROCESS_END)
	BUZZZ_EVT(DHD_PROT_D2H_SYNC_LIVELOCK)
	BUZZZ_EVT(DHD_IOCTL_BUFPOST)
	BUZZZ_EVT(DHD_EVENT_BUFPOST)
	BUZZZ_EVT(DHD_PROC_MSG_TYPE)
	BUZZZ_EVT(DHD_BUS_RXCTL_ONE)
	BUZZZ_EVT(DHD_BUS_RXCTL_TWO)
} buzzz_evt_id_t;

static inline void dhd_buzzz_fmt_init(void)
{
	BUZZZ_FMT(DHD,				"DHD events")
	BUZZZ_FMT(GENERAL_LOCK,			"+++LOCK GENERAL flags<0x%08x>")
	BUZZZ_FMT(GENERAL_UNLOCK,		"---UNLK GENERAL flags<0x%08x>")
	BUZZZ_FMT(FLOWRING_LOCK,		"+++LOCK FLOWRING flags<0x%08x>")
	BUZZZ_FMT(FLOWRING_UNLOCK,		"---UNLK FLOWRING flags<0x%08x>")
	BUZZZ_FMT(FLOWID_LOCK,			"+++LOCK FLOWID flags<0x%08x>")
	BUZZZ_FMT(FLOWID_UNLOCK,		"---UNLK FLOWID flags<0x%08x>")

	BUZZZ_FMT(START_XMIT_BGN,		"{ dhd_start_xmit() ifidx<%u> skb<0x%p>")
	BUZZZ_FMT(START_XMIT_END,		"} dhd_start_xmit()")
	BUZZZ_FMT(PROCESS_CTRL_BGN,		"{ dhd_prot_process_ctrlbuf()")
	BUZZZ_FMT(PROCESS_CTRL_END,		"} dhd_prot_process_ctrlbuf()")
	BUZZZ_FMT(UPDATE_TXFLOWRINGS_BGN,	"{ dhd_update_txflowrings()");
	BUZZZ_FMT(UPDATE_TXFLOWRINGS_END,	"} dhd_update_txflowrings()");
	BUZZZ_FMT(PROCESS_TXCPL_BGN,		"{ dhd_prot_process_msgbuf_txcpl()")
	BUZZZ_FMT(PROCESS_TXCPL_END,		"} dhd_prot_process_msgbuf_txcpl()")
	BUZZZ_FMT(PROCESS_RXCPL_BGN,		"{ dhd_prot_process_msgbuf_rxcpl()")
	BUZZZ_FMT(PROCESS_RXCPL_END,		"} dhd_prot_process_msgbuf_rxcpl()")

	BUZZZ_FMT(GET_SRC_ADDR,			"bytes<%u> @<0x%p> prot_get_src_addr()")
	BUZZZ_FMT(WRITE_COMPLETE,		"WR<%u> prot_ring_write_complete")
	BUZZZ_FMT(ALLOC_RING_SPACE,		"{ dhd_alloc_ring_space nitems<%d>")
	BUZZZ_FMT(ALLOC_RING_SPACE_RET,		"} dhd_alloc_ring_space() alloc<%d> @<0x%p>")
	BUZZZ_FMT(ALLOC_RING_SPACE_FAIL,	"FAILURE } dhd_alloc_ring_space() alloc<%d>")

	BUZZZ_FMT(PKTID_MAP_CLEAR,		"pktid map clear")
	BUZZZ_FMT(PKTID_NOT_AVAILABLE,		"FAILURE pktid pool depletion failures<%u>")
	BUZZZ_FMT(PKTID_MAP_RSV,		"pktid<%u> pkt<0x%p> dhd_pktid_map_reserve()")
	BUZZZ_FMT(PKTID_MAP_SAVE,		"pktid<%u> pkt<0x%p> dhd_pktid_map_save()")
	BUZZZ_FMT(PKTID_MAP_ALLOC,		"pktid<%u> pkt<0x%p> dhd_pktid_map_alloc()")
	BUZZZ_FMT(PKTID_MAP_FREE,		"pktid<%u> pkt<0x%p> dhd_pktid_map_free()")
	BUZZZ_FMT(LOCKER_INUSE_ABORT,		"ASSERT  pktid<%u> pkt<0x%p> locker->inuse")
	BUZZZ_FMT(BUFFER_TYPE_ABORT1,		"ASSERT  pktid<%u> pkt<0x%p> locker->dma")
	BUZZZ_FMT(BUFFER_TYPE_ABORT2,		"ASSERT  locker->dma<%u> buf_type<%u>")

	BUZZZ_FMT(UPD_READ_IDX,			"RD<%u>  prot_upd_read_idx()")
	BUZZZ_FMT(STORE_RXCPLN_RD,		"RD<%u>  prot_store_rxcpln_read_idx()")
	BUZZZ_FMT(EARLY_UPD_RXCPLN_RD,		"RD<%u>  prot_early_upd_rxcpln_read_idx()")

	BUZZZ_FMT(POST_TXDATA,			"flr<%u> pkt<0x%p> dhd_prot_txdata()")
	BUZZZ_FMT(RETURN_RXBUF,			"cnt<%u> dhd_prot_return_rxbuf()");
	BUZZZ_FMT(RXBUF_POST,			"cnt<%u> dhd_prot_rxbufpost()");
	BUZZZ_FMT(RXBUF_POST_EVENT,		"event   dhd_prot_rxbufpost_ctrl()");
	BUZZZ_FMT(RXBUF_POST_IOCTL,		"ioctl   dhd_prot_rxbufpost_ctrl()");
	BUZZZ_FMT(RXBUF_POST_CTRL_PKTGET_FAIL,	"FAILURE pktget dhd_prot_rxbufpost_ctrl()");
	BUZZZ_FMT(RXBUF_POST_PKTGET_FAIL,	"FAILURE pktget loop<%u> dhd_prot_rxbufpost()")
	BUZZZ_FMT(RXBUF_POST_PKTID_FAIL,	"FAILURE pktid  loop<%u> dhd_prot_rxbufpost()")

	BUZZZ_FMT(DHD_DUPLICATE_ALLOC,		"ASSERT  dhd_pktid_audit(%u) DHD_DUPLICATE_ALLOC")
	BUZZZ_FMT(DHD_DUPLICATE_FREE,		"ASSERT  dhd_pktid_audit(%u) DHD_DUPLICATE_FREE")
	BUZZZ_FMT(DHD_TEST_IS_ALLOC,		"ASSERT  dhd_pktid_audit(%u) DHD_TEST_IS_ALLOC")
	BUZZZ_FMT(DHD_TEST_IS_FREE,		"ASSERT  dhd_pktid_audit(%u) DHD_TEST_IS_FREE")

	BUZZZ_FMT(DHD_PROT_IOCT_BGN,        "{ dhd_prot_ioct pending<%u> thread<0x%p>")
	BUZZZ_FMT(DHDMSGBUF_CMPLT_BGN,      "{  dhdmsgbuf_cmplt bus::retlen<%u> bus::pktid<%u>")
	BUZZZ_FMT(DHDMSGBUF_CMPLT_END,      "}  dhdmsgbuf_cmplt resp_len<%d> pktid<%u>")
	BUZZZ_FMT(DHD_PROT_IOCT_END,        "} dhd_prot_ioct pending<%u> thread<0x%p>")
	BUZZZ_FMT(DHD_FILLUP_IOCT_REQST_BGN, "{ dhd_fillup_ioct_reqst_ptrbased cmd<%u> transid<%u>")
	BUZZZ_FMT(DHD_FILLUP_IOCT_REQST_END,
		"} dhd_fillup_ioct_reqst_ptrbased transid<%u> bus::pktid<%u>")
	BUZZZ_FMT(DHD_MSGBUF_RXBUF_POST_IOCTLRESP_BUFS_BGN,
		"{ dhd_msgbuf_rxbuf_post_ioctlresp_bufs cur_posted<%u> bus::pktid<%u>")
	BUZZZ_FMT(DHD_MSGBUF_RXBUF_POST_IOCTLRESP_BUFS_END,
		"} dhd_msgbuf_rxbuf_post_ioctlresp_bufs cur_posted<%u> bus::pktid<%u>")
	BUZZZ_FMT(DHD_PROT_IOCTCMPLT_PROCESS_ONE,
		"{ dhd_prot_ioctlcmplt_process cmd<%d> transid<%d>")
	BUZZZ_FMT(DHD_PROT_IOCTCMPLT_PROCESS_TWO,
		"} dhd_prot_ioctlcmplt_process resplen<%u> pktid<%u>")
	BUZZZ_FMT(DHD_PROT_EVENT_PROCESS_BGN, "{ dhd_prot_event_process pktid<%u>")
	BUZZZ_FMT(DHD_PROT_EVENT_PROCESS_END, "} dhd_prot_event_process buflen<%u> pkt<0x%p>")
	BUZZZ_FMT(DHD_PROT_D2H_SYNC_LIVELOCK, " dhd_prot_d2h_sync_livelock seqnum<%u>")
	BUZZZ_FMT(DHD_IOCTL_BUFPOST, " dhd_prot_rxbufpost_ctrl ioctl pktid<%u> phyaddr<0x%x>")
	BUZZZ_FMT(DHD_EVENT_BUFPOST, " dhd_prot_rxbufpost_ctrl event pktid<%u> phyaddr<0x%x>")
	BUZZZ_FMT(DHD_PROC_MSG_TYPE, " dhd_process_msgtype msg<0x%x> epoch<%u>")
	BUZZZ_FMT(DHD_BUS_RXCTL_ONE, "dhd_bus_rxctl prev resplen<%u> pktid<%u>")
	BUZZZ_FMT(DHD_BUS_RXCTL_TWO, "dhd_bus_rxctl cur  resplen<%u> pktid<%u>")
}

#define BUZZZ_LOG(ID, N, ARG...)    dhd_buzzz_log ##N(BUZZZ_EVT__ ##ID, ##ARG)

#else  /* DHD_BUZZZ_LOG_ENABLED */
/*
 * Broadcom logging system - Empty implementaiton
 */

#define dhd_buzzz_attach()              do { /* noop */ } while (0)
#define dhd_buzzz_detach()              do { /* noop */ } while (0)
#define dhd_buzzz_panic(x)              do { /* noop */ } while (0)
#define BUZZZ_LOG(ID, N, ARG...)    do { /* noop */ } while (0)

#endif /* DHD_BUZZZ_LOG_ENABLED */

#endif /* _DHD_BUZZZ_H_INCLUDED_ */
