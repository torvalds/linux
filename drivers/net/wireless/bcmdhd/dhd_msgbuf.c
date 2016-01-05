/*
 * Header file describing the internal (inter-module) DHD interfaces.
 *
 * Provides type definitions and function prototypes used to link the
 * DHD OS, bus, and protocol modules.
 *
 * Copyright (C) 1999-2015, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhd_msgbuf.c 452261 2014-01-29 19:30:23Z $
 */
#include <typedefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <circularbuf.h>
#include <bcmmsgbuf.h>
#include <bcmendian.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_proto.h>
#include <dhd_bus.h>
#include <dhd_dbg.h>


#ifdef PROP_TXSTATUS
#include <wlfc_proto.h>
#include <dhd_wlfc.h>
#endif
#include <pcie_core.h>
#include <bcmpcie.h>

#define RETRIES 2		/* # of retries to retrieve matching ioctl response */
#define IOCTL_HDR_LEN	12

#define DEFAULT_RX_BUFFERS_TO_POST	255
#define RXBUFPOST_THRESHOLD			16
#define RX_BUF_BURST				8

#define DHD_STOP_QUEUE_THRESHOLD	24
#define DHD_START_QUEUE_THRESHOLD	32
#define MAX_INLINE_IOCTL_LEN	64	/* anything beyond this len will not be inline reqst */

/* Required for Native to PktId mapping incase of 64bit hosts */
#define MAX_PKTID_ITEMS		(2048)

/* Given packet pointer and physical address, macro should return unique 32 bit pktid */
/* And given 32bit pktid, macro should return packet pointer and physical address */
extern void *pktid_map_init(void *osh, uint32 count);
extern void pktid_map_uninit(void *pktid_map_handle);
extern uint32 pktid_map_unique(void *pktid_map_handle,
	void *pkt, dmaaddr_t physaddr, uint32 physlen, uint32 dma);
extern void *pktid_get_packet(void *pktid_map_handle,
	uint32 id, dmaaddr_t *physaddr, uint32 *physlen);

#define NATIVE_TO_PKTID_INIT(osh, count)	pktid_map_init(osh, count)
#define NATIVE_TO_PKTID_UNINIT(pktid_map_handle)	pktid_map_uninit(pktid_map_handle)

#define NATIVE_TO_PKTID(pktid_map_handle, pkt, pa, pa_len, dma)	\
	pktid_map_unique((pktid_map_handle), (void *)(pkt), (pa), (uint32) (pa_len), (uint32)dma)
#define PKTID_TO_NATIVE(pktid_map_handle, id, pa, pa_len)		\
	pktid_get_packet((pktid_map_handle), (uint32)(id), (void *)&(pa), (uint32 *) &(pa_len))

#define MODX(x, n)	((x) & ((n) -1))
#define align(x, n)	(MODX(x, n) ? ((x) - MODX(x, n) + (n)) : ((x) - MODX(x, n)))
#define RX_DMA_OFFSET	8
#define IOCT_RETBUF_SIZE	(RX_DMA_OFFSET + WLC_IOCTL_MAXLEN)

typedef struct dhd_prot {
	uint32 reqid;
	uint16 hdr_len;
	uint32 lastcmd;
	uint32 pending;
	uint16 rxbufpost;
	uint16 max_rxbufpost;
	uint16 active_tx_count;
	uint16 max_tx_count;
	dmaaddr_t htod_physaddr;
	dmaaddr_t dtoh_physaddr;
	bool txflow_en;
	circularbuf_t *dtohbuf;
	circularbuf_t *htodbuf;
	uint32	rx_dataoffset;
	void*	retbuf;
	dmaaddr_t retbuf_phys;
	void*	ioctbuf;	/* For holding ioct request buf */
	dmaaddr_t ioctbuf_phys;	/* physical address for ioctbuf */
	dhd_mb_ring_t mb_ring_fn;
	void *htod_ring;
	void *dtoh_ring;
	/* Flag to check if splitbuf support is enabled. */
	/* Set to False at dhd_prot_attach. Set to True at dhd_prot_init */
	bool htodsplit;
	bool dtohsplit;
	/* H2D/D2H Ctrl rings */
	dmaaddr_t htod_ctrl_physaddr;	/* DMA mapped physical addr ofr H2D ctrl ring */
	dmaaddr_t dtoh_ctrl_physaddr;	/* DMA mapped phys addr for D2H ctrl ring */
	circularbuf_t *htod_ctrlbuf;	/* Cbuf handle for H2D ctrl ring */
	circularbuf_t *dtoh_ctrlbuf;	/* Cbuf handle for D2H ctrl ring */
	void *htod_ctrl_ring; /* address for H2D control buf */
	void *dtoh_ctrl_ring; /* address for D2H control buf */


	uint16	ioctl_seq_no;
	uint16	data_seq_no;
	void *pktid_map_handle;
} dhd_prot_t;

static int dhdmsgbuf_query_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd,
	void *buf, uint len, uint8 action);
static int dhd_msgbuf_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd,
	void *buf, uint len, uint8 action);
static int dhdmsgbuf_cmplt(dhd_pub_t *dhd, uint32 id, uint32 len, void* buf, void* retbuf);
static int dhd_msgbuf_init_dtoh(dhd_pub_t *dhd);

static int dhd_msgbuf_rxbuf_post(dhd_pub_t *dhd);
static int dhd_msgbuf_init_htod(dhd_pub_t *dhd);
static int dhd_msgbuf_init_htod_ctrl(dhd_pub_t *dhd);
static int dhd_msgbuf_init_dtoh_ctrl(dhd_pub_t *dhd);
static int dhd_prot_rxbufpost(dhd_pub_t *dhd, uint32 count);
static void dhd_prot_return_rxbuf(dhd_pub_t *dhd, uint16 rxcnt);
static void dhd_prot_rxcmplt_process(dhd_pub_t *dhd, void* buf);
static void dhd_prot_event_process(dhd_pub_t *dhd, uint8* buf, uint16 len);
static void dhd_prot_process_msgtype(dhd_pub_t *dhd, uint8* buf, uint16 len);
static void dhd_process_msgtype(dhd_pub_t *dhd, uint8* buf, uint16 len);

static void dhd_prot_txstatus_process(dhd_pub_t *dhd, void * buf);
static void dhd_prot_ioctcmplt_process(dhd_pub_t *dhd, void * buf);
void* dhd_alloc_circularbuf_space(dhd_pub_t *dhd, circularbuf_t *handle, uint16 msglen, uint path);
static int dhd_fillup_ioct_reqst(dhd_pub_t *dhd, uint16 len, uint cmd, void* buf, int ifidx);
static int dhd_fillup_ioct_reqst_ptrbased(dhd_pub_t *dhd, uint16 len, uint cmd, void* buf,
	int ifidx);
static INLINE void dhd_prot_packet_free(dhd_pub_t *dhd, uint32 pktid);
static INLINE void *dhd_prot_packet_get(dhd_pub_t *dhd, uint32 pktid);

/* Linkage, sets prot link and updates hdrlen in pub */
int dhd_prot_attach(dhd_pub_t *dhd)
{
	uint alloced = 0;

	dhd_prot_t *msg_buf;
	if (!(msg_buf = (dhd_prot_t *)DHD_OS_PREALLOC(dhd, DHD_PREALLOC_PROT,
		sizeof(dhd_prot_t)))) {
			DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
			goto fail;
		}
	memset(msg_buf, 0, sizeof(dhd_prot_t));

	msg_buf->hdr_len = sizeof(ioctl_req_hdr_t) + sizeof(cmn_msg_hdr_t) + sizeof(ret_buf_t);
	msg_buf->dtohbuf = MALLOC(dhd->osh, sizeof(circularbuf_t));
	msg_buf->htodbuf = MALLOC(dhd->osh, sizeof(circularbuf_t));

	memset(msg_buf->dtohbuf, 0, sizeof(circularbuf_t));
	memset(msg_buf->htodbuf, 0, sizeof(circularbuf_t));

	dhd->prot = msg_buf;
	dhd->maxctl = WLC_IOCTL_MAXLEN + msg_buf->hdr_len;

	/* ret buf for ioctl */
	msg_buf->retbuf = DMA_ALLOC_CONSISTENT(dhd->osh, IOCT_RETBUF_SIZE, 4,
		&alloced, &msg_buf->retbuf_phys, NULL);
	if (msg_buf->retbuf ==  NULL) {
		ASSERT(0);
		return BCME_NOMEM;
	}

	ASSERT(MODX((unsigned long)msg_buf->retbuf, 4) == 0);

	msg_buf->ioctbuf = DMA_ALLOC_CONSISTENT(dhd->osh, MSGBUF_MAX_MSG_SIZE, 4,
		&alloced, &msg_buf->ioctbuf_phys, NULL);

	if (msg_buf->ioctbuf ==  NULL) {
		ASSERT(0);
		return BCME_NOMEM;
	}

	ASSERT(MODX((unsigned long)msg_buf->ioctbuf, 4) == 0);

	msg_buf->pktid_map_handle = NATIVE_TO_PKTID_INIT(dhd->osh, MAX_PKTID_ITEMS);
	if (msg_buf->pktid_map_handle == NULL) {
		ASSERT(0);
		return BCME_NOMEM;
	}

	msg_buf->htod_ring = DMA_ALLOC_CONSISTENT(dhd->osh, HOST_TO_DNGL_MSGBUF_SZ, 4,
		&alloced, &msg_buf->htod_physaddr, NULL);
	if (msg_buf->htod_ring ==  NULL) {
		ASSERT(0);
		return BCME_NOMEM;
	}

	ASSERT(MODX((unsigned long)msg_buf->htod_ring, 4) == 0);

	msg_buf->dtoh_ring = DMA_ALLOC_CONSISTENT(dhd->osh, DNGL_TO_HOST_MSGBUF_SZ, 4,
		&alloced, &msg_buf->dtoh_physaddr, NULL);
	if (msg_buf->dtoh_ring ==  NULL) {
		ASSERT(0);
		return BCME_NOMEM;
	}

	ASSERT(MODX((unsigned long)msg_buf->dtoh_ring, 4) == 0);

	/* At this point we assume splitbuf is not supported by dongle */
	msg_buf->htodsplit = FALSE;
	msg_buf->dtohsplit = FALSE;


	return 0;

fail:
#ifndef CONFIG_DHD_USE_STATIC_BUF
	if (msg_buf != NULL)
		MFREE(dhd->osh, msg_buf, sizeof(dhd_prot_t));
#endif /* CONFIG_DHD_USE_STATIC_BUF */
	return BCME_NOMEM;
}

/* Unlink, frees allocated protocol memory (including dhd_prot) */
void dhd_prot_detach(dhd_pub_t *dhd)
{
	 /* Stop the protocol module */
	if (dhd->prot) {

		if (dhd->prot->dtoh_ring) {
			DMA_FREE_CONSISTENT(dhd->osh, dhd->prot->dtoh_ring,
				DNGL_TO_HOST_MSGBUF_SZ, dhd->prot->dtoh_physaddr, NULL);

			dhd->prot->dtoh_ring = NULL;
			PHYSADDRHISET(dhd->prot->dtoh_physaddr, 0);
			PHYSADDRLOSET(dhd->prot->dtoh_physaddr, 0);
		}

		if (dhd->prot->htod_ring) {
			DMA_FREE_CONSISTENT(dhd->osh, dhd->prot->htod_ring,
				HOST_TO_DNGL_MSGBUF_SZ, dhd->prot->htod_physaddr, NULL);

			dhd->prot->htod_ring =  NULL;
			PHYSADDRHISET(dhd->prot->htod_physaddr, 0);
			PHYSADDRLOSET(dhd->prot->htod_physaddr, 0);
		}

		if (dhd->prot->dtohbuf) {
			MFREE(dhd->osh, dhd->prot->dtohbuf, sizeof(circularbuf_t));
			dhd->prot->dtohbuf = NULL;
		}

		if (dhd->prot->htodbuf) {
			MFREE(dhd->osh, dhd->prot->htodbuf, sizeof(circularbuf_t));
			dhd->prot->htodbuf = NULL;
		}

		if (dhd->prot->htod_ctrl_ring) {
			DMA_FREE_CONSISTENT(dhd->osh, dhd->prot->htod_ctrl_ring,
				HOST_TO_DNGL_CTRLRING_SZ, dhd->prot->htod_ctrl_physaddr, NULL);

			dhd->prot->htod_ctrl_ring = NULL;
			dhd->prot->htod_ctrl_physaddr = 0;
		}

		if (dhd->prot->dtoh_ctrl_ring) {
			DMA_FREE_CONSISTENT(dhd->osh, dhd->prot->dtoh_ctrl_ring,
				DNGL_TO_HOST_CTRLRING_SZ, dhd->prot->dtoh_ctrl_physaddr, NULL);

			dhd->prot->dtoh_ctrl_ring = NULL;
			dhd->prot->dtoh_ctrl_physaddr = 0;
		}

		if (dhd->prot->htod_ctrlbuf) {
			MFREE(dhd->osh, dhd->prot->htod_ctrlbuf, sizeof(circularbuf_t));
			dhd->prot->htod_ctrlbuf = NULL;
		}

		if (dhd->prot->dtoh_ctrlbuf) {
			MFREE(dhd->osh, dhd->prot->dtoh_ctrlbuf, sizeof(circularbuf_t));
			dhd->prot->dtoh_ctrlbuf = NULL;
		}

		if (dhd->prot->retbuf) {
			DMA_FREE_CONSISTENT(dhd->osh, dhd->prot->retbuf,
			IOCT_RETBUF_SIZE, dhd->prot->retbuf_phys, NULL);
			dhd->prot->retbuf = NULL;
		}

		if (dhd->prot->ioctbuf) {
			DMA_FREE_CONSISTENT(dhd->osh, dhd->prot->ioctbuf,
			MSGBUF_MAX_MSG_SIZE, dhd->prot->ioctbuf_phys, NULL);

			dhd->prot->ioctbuf = NULL;
		}

		NATIVE_TO_PKTID_UNINIT(dhd->prot->pktid_map_handle);

#ifndef CONFIG_DHD_USE_STATIC_BUF
		MFREE(dhd->osh, dhd->prot, sizeof(dhd_prot_t));
#endif /* CONFIG_DHD_USE_STATIC_BUF */

		dhd->prot = NULL;
	}
}

void
dhd_prot_rx_dataoffset(dhd_pub_t *dhd, uint32 rx_offset)
{
	dhd_prot_t *prot = dhd->prot;
	prot->rx_dataoffset = rx_offset;
}


/* Initialize protocol: sync w/dongle state.
 * Sets dongle media info (iswl, drv_version, mac address).
 */
int dhd_prot_init(dhd_pub_t *dhd)
{
	int ret = 0;
	wlc_rev_info_t revinfo;
	dhd_prot_t *prot = dhd->prot;
	uint32 shared_flags;
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	dhd_bus_cmn_readshared(dhd->bus, &prot->max_tx_count, TOTAL_LFRAG_PACKET_CNT);
	if (prot->max_tx_count == 0) {
		/* This can happen if LFrag pool is not enabled for the LFRAG's */
		/* on the dongle. Let's use some default value */
		prot->max_tx_count = 64;
	}
	DHD_INFO(("%s:%d: MAX_TX_COUNT = %d\n", __FUNCTION__, __LINE__, prot->max_tx_count));

	dhd_bus_cmn_readshared(dhd->bus, &prot->max_rxbufpost, MAX_HOST_RXBUFS);
	if (prot->max_rxbufpost == 0) {
		/* This would happen if the dongle firmware is not */
		/* using the latest shared structure template */
		prot->max_rxbufpost = DEFAULT_RX_BUFFERS_TO_POST;
	}
	DHD_INFO(("%s:%d: MAX_RXBUFPOST = %d\n", __FUNCTION__, __LINE__, prot->max_rxbufpost));

	prot->active_tx_count = 0;
	prot->txflow_en = FALSE;
	prot->mb_ring_fn = dhd_bus_get_mbintr_fn(dhd->bus);
	prot->data_seq_no = 0;
	prot->ioctl_seq_no = 0;
	/* initialise msgbufs */
	shared_flags = dhd_bus_get_sharedflags(dhd->bus);
	if (shared_flags & PCIE_SHARED_HTOD_SPLIT) {
		prot->htodsplit = TRUE;
		if (dhd_msgbuf_init_htod_ctrl(dhd) == BCME_NOMEM)
		{
			prot->htodsplit = FALSE;
			DHD_ERROR(("%s:%d: HTOD ctrl ring alloc failed!\n",
				__FUNCTION__, __LINE__));
		}
	}
	if (shared_flags & PCIE_SHARED_DTOH_SPLIT) {
		prot->dtohsplit = TRUE;
		if (dhd_msgbuf_init_dtoh_ctrl(dhd) == BCME_NOMEM)
		{
			prot->dtohsplit = FALSE;
			DHD_ERROR(("%s:%d: DTOH ctrl ring alloc failed!\n",
				__FUNCTION__, __LINE__));
		}
	}
	ret = dhd_msgbuf_init_htod(dhd);
	ret = dhd_msgbuf_init_dtoh(dhd);
	ret = dhd_msgbuf_rxbuf_post(dhd);


	/* Get the device rev info */
	memset(&revinfo, 0, sizeof(revinfo));
	ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_REVINFO, &revinfo, sizeof(revinfo), FALSE, 0);
	if (ret < 0)
		goto done;
#if defined(WL_CFG80211)
	if (dhd_download_fw_on_driverload)
#endif /* defined(WL_CFG80211) */
		ret = dhd_preinit_ioctls(dhd);
	/* Always assumes wl for now */
	dhd->iswl = TRUE;
done:
	return ret;

}

static INLINE void BCMFASTPATH
dhd_prot_packet_free(dhd_pub_t *dhd, uint32 pktid)
{
	void *PKTBUF;
	dmaaddr_t pa;
	uint32 pa_len;
	PKTBUF = PKTID_TO_NATIVE(dhd->prot->pktid_map_handle, pktid, pa, pa_len);
	DMA_UNMAP(dhd->osh, (uint) pa, (uint) pa_len, DMA_TX, 0, 0);
	PKTFREE(dhd->osh, PKTBUF, TRUE);
	return;
}

static INLINE void * BCMFASTPATH
dhd_prot_packet_get(dhd_pub_t *dhd, uint32 pktid)
{
	void *PKTBUF;
	ulong pa;
	uint32 pa_len;
	PKTBUF = PKTID_TO_NATIVE(dhd->prot->pktid_map_handle, pktid, pa, pa_len);
	DMA_UNMAP(dhd->osh, (uint) pa, (uint) pa_len, DMA_RX, 0, 0);
	return PKTBUF;
}

static int BCMFASTPATH
dhd_msgbuf_rxbuf_post(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	unsigned long flags;
	uint32 fillbufs;
	uint32 i;
	fillbufs = prot->max_rxbufpost - prot->rxbufpost;

	for (i = 0; i < fillbufs; ) {
		int retcount;
		uint32 buf_count = (fillbufs - i) > RX_BUF_BURST ? RX_BUF_BURST : (fillbufs - i);

		flags = dhd_os_spin_lock(dhd);
		retcount = dhd_prot_rxbufpost(dhd, buf_count);
		if (retcount > 0) {
			prot->rxbufpost += (uint16)retcount;
			i += (uint16)retcount;
			dhd_os_spin_unlock(dhd, flags);
		} else {
			dhd_os_spin_unlock(dhd, flags);
			break;
		}
	}

	return 0;
}

static int BCMFASTPATH
dhd_prot_rxbufpost(dhd_pub_t *dhd, uint32 count)
{
	void *p;
	uint16 pktsz = 2048;
	uint32 i;
	rxdesc_msghdr_t *rxbuf_post;
	rx_lenptr_tup_t *rx_tup;
	dmaaddr_t physaddr;
	uint32 pktlen;
	uint32 msglen = sizeof(rxdesc_msghdr_t) + count * sizeof(rx_lenptr_tup_t);

	dhd_prot_t *prot = dhd->prot;
	circularbuf_t *htod_msgbuf = (circularbuf_t *)prot->htodbuf;

	rxbuf_post = (rxdesc_msghdr_t *)dhd_alloc_circularbuf_space(dhd,
		htod_msgbuf, (uint16)msglen, HOST_TO_DNGL_DATA);
	if (rxbuf_post == NULL) {
		DHD_INFO(("%s:%d: HTOD Msgbuf Not available\n",
			__FUNCTION__, __LINE__));
		return -1;
	}

	/* CMN msg header */
	rxbuf_post->msg.msglen = htol16((uint16)msglen);
	rxbuf_post->msg.msgtype = MSG_TYPE_RXBUF_POST;
	rxbuf_post->msg.ifidx = 0;
	rxbuf_post->msg.u.seq.seq_no = htol16(++prot->data_seq_no);

	/* RX specific hdr */
	rxbuf_post->rsvd0 = 0;
	rxbuf_post->rsvd1 = 0;
	rxbuf_post->descnt = (uint8)count;

	rx_tup = (rx_lenptr_tup_t *) &(rxbuf_post->rx_tup[0]);

	for (i = 0; i < count; i++) {
		if ((p = PKTGET(dhd->osh, pktsz, FALSE)) == NULL) {
			DHD_ERROR(("%s:%d: PKTGET for rxbuf failed\n", __FUNCTION__, __LINE__));
			printf("%s:%d: PKTGET for rxbuf failed. Need to handle this gracefully\n",
				__FUNCTION__, __LINE__);
			return -1;
		}

		pktlen = PKTLEN(dhd->osh, p);
		physaddr = DMA_MAP(dhd->osh, PKTDATA(dhd->osh, p), pktlen, DMA_RX, 0, 0);
		if (physaddr == 0) {
			DHD_ERROR(("Something really bad, unless 0 is a valid phyaddr\n"));
			ASSERT(0);
		}
		/* Each bufid-len-ptr tuple */
		rx_tup->rxbufid = htol32(NATIVE_TO_PKTID(dhd->prot->pktid_map_handle,
			p, physaddr, pktlen, DMA_RX));
		rx_tup->len = htol16((uint16)PKTLEN(dhd->osh, p));
		rx_tup->rsvd2 = 0;
		rx_tup->ret_buf.high_addr = htol32(PHYSADDRHI(physaddr));
		rx_tup->ret_buf.low_addr  = htol32(PHYSADDRLO(physaddr));

		rx_tup++;
	}

	/* Since, we are filling the data directly into the bufptr obtained
	 * from the msgbuf, we can directly call the write_complete
	 */
	circularbuf_write_complete(htod_msgbuf, (uint16)msglen);

	return count;
}

void BCMFASTPATH
dhd_msgbuf_ringbell(void *ctx)
{
	dhd_pub_t *dhd = (dhd_pub_t *) ctx;
	dhd_prot_t *prot = dhd->prot;
	circularbuf_t *htod_msgbuf = (circularbuf_t *)prot->htodbuf;

	/* Following will take care of writing both the Write and End pointers (32 bits) */
	dhd_bus_cmn_writeshared(dhd->bus, &(CIRCULARBUF_WRITE_PTR(htod_msgbuf)),
		sizeof(uint32), HOST_TO_DNGL_WPTR);

	prot->mb_ring_fn(dhd->bus, *(uint32 *) &(CIRCULARBUF_WRITE_PTR(htod_msgbuf)));
}

void BCMFASTPATH
dhd_ctrlbuf_ringbell(void *ctx)
{
	dhd_pub_t *dhd = (dhd_pub_t *) ctx;
	dhd_prot_t *prot = dhd->prot;
	circularbuf_t *htod_ctrlbuf = (circularbuf_t *)prot->htod_ctrlbuf;

	/* Following will take care of writing both the Write and End pointers (32 bits) */
	dhd_bus_cmn_writeshared(dhd->bus, &(CIRCULARBUF_WRITE_PTR(htod_ctrlbuf)),
		sizeof(uint32), HTOD_CTRL_WPTR);

	prot->mb_ring_fn(dhd->bus, *(uint32 *) &(CIRCULARBUF_WRITE_PTR(htod_ctrlbuf)));
}

static int
dhd_msgbuf_init_htod(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	circularbuf_t *htod_msgbuf = (circularbuf_t *)prot->htodbuf;

	circularbuf_init(htod_msgbuf, prot->htod_ring, HOST_TO_DNGL_MSGBUF_SZ);
	circularbuf_register_cb(htod_msgbuf, dhd_msgbuf_ringbell, (void *)dhd);
	dhd_bus_cmn_writeshared(dhd->bus, &prot->htod_physaddr,
		sizeof(prot->htod_physaddr), HOST_TO_DNGL_BUF_ADDR);

	dhd_bus_cmn_writeshared(dhd->bus, &(CIRCULARBUF_WRITE_PTR(htod_msgbuf)),
		sizeof(uint32), HOST_TO_DNGL_WPTR);

	return 0;

}
static int
dhd_msgbuf_init_dtoh(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	circularbuf_t *dtoh_msgbuf = (circularbuf_t *)prot->dtohbuf;

	prot->rxbufpost = 0;
	circularbuf_init(dtoh_msgbuf, prot->dtoh_ring, DNGL_TO_HOST_MSGBUF_SZ);
	dhd_bus_cmn_writeshared(dhd->bus, &prot->dtoh_physaddr,
		sizeof(prot->dtoh_physaddr), DNGL_TO_HOST_BUF_ADDR);

	dhd_bus_cmn_writeshared(dhd->bus, &CIRCULARBUF_READ_PTR(dtoh_msgbuf),
		sizeof(uint16), DNGL_TO_HOST_RPTR);

	/* One dummy interrupt to the device. This would trigger */
	/* the msgbuf initializations at the device side.        */
	/* Send dummy intr to device here, only if support for split data/ctrl rings is disabled */
	/* Else send the dummy initialization intr at dtoh ctrl buf init */

	dhd_bus_ringbell(dhd->bus, PCIE_INTB);
	return 0;
}

/* Allocate space for HTOD ctrl ring on host and initialize handle/doorbell for the same */
static int dhd_msgbuf_init_htod_ctrl(dhd_pub_t *dhd)
{
	uint alloced;
	dhd_prot_t *prot = dhd->prot;
	prot->htod_ctrlbuf = MALLOC(dhd->osh, sizeof(circularbuf_t));
	memset(prot->htod_ctrlbuf, 0, sizeof(circularbuf_t));

	prot->htod_ctrl_ring = DMA_ALLOC_CONSISTENT(dhd->osh, HOST_TO_DNGL_CTRLRING_SZ, 4,
		&alloced, &prot->htod_ctrl_physaddr, NULL);
	if (prot->htod_ctrl_ring ==  NULL) {
		return BCME_NOMEM;
	}

	ASSERT(MODX((unsigned long)prot->htod_ctrl_ring, 4) == 0);

	circularbuf_init(prot->htod_ctrlbuf, prot->htod_ctrl_ring, HOST_TO_DNGL_CTRLRING_SZ);
	circularbuf_register_cb(prot->htod_ctrlbuf, dhd_ctrlbuf_ringbell, (void *)dhd);
	dhd_bus_cmn_writeshared(dhd->bus, &prot->htod_ctrl_physaddr,
		sizeof(prot->htod_ctrl_physaddr), HOST_TO_DNGL_CTRLBUF_ADDR);

	dhd_bus_cmn_writeshared(dhd->bus, &(CIRCULARBUF_WRITE_PTR(prot->htod_ctrlbuf)),
		sizeof(uint32), HTOD_CTRL_WPTR);

	return 0;
}
/* Allocate space for DTOH ctrl ring on host and initialize msgbuf handle in dhd_prot_t */
static int dhd_msgbuf_init_dtoh_ctrl(dhd_pub_t *dhd)
{
	uint alloced;
	dhd_prot_t *prot = dhd->prot;
	prot->dtoh_ctrlbuf = MALLOC(dhd->osh, sizeof(circularbuf_t));
	memset(prot->dtoh_ctrlbuf, 0, sizeof(circularbuf_t));

	prot->dtoh_ctrl_ring = DMA_ALLOC_CONSISTENT(dhd->osh, DNGL_TO_HOST_CTRLRING_SZ, 4,
		&alloced, &prot->dtoh_ctrl_physaddr, NULL);
	if (prot->dtoh_ctrl_ring ==  NULL) {
		return BCME_NOMEM;
	}
	ASSERT(MODX((unsigned long)prot->dtoh_ctrl_ring, 4) == 0);

	circularbuf_init(prot->dtoh_ctrlbuf, prot->dtoh_ctrl_ring, DNGL_TO_HOST_CTRLRING_SZ);
	dhd_bus_cmn_writeshared(dhd->bus, &prot->dtoh_ctrl_physaddr,
		sizeof(prot->dtoh_ctrl_physaddr), DNGL_TO_HOST_CTRLBUF_ADDR);

	dhd_bus_cmn_writeshared(dhd->bus, &(CIRCULARBUF_READ_PTR(prot->dtoh_ctrlbuf)),
		sizeof(uint32), DTOH_CTRL_RPTR);
	return 0;
}

int BCMFASTPATH
dhd_prot_process_msgbuf(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	circularbuf_t *dtoh_msgbuf = (circularbuf_t *)prot->dtohbuf;

	dhd_bus_cmn_readshared(dhd->bus, &CIRCULARBUF_WRITE_PTR(dtoh_msgbuf), DNGL_TO_HOST_WPTR);

	/* Process all the messages - DTOH direction */
	while (TRUE) {
		uint8 *src_addr;
		uint16 src_len;

		src_addr = circularbuf_get_read_ptr(dtoh_msgbuf, &src_len);
		if (src_addr == NULL)
			break;

		/* Prefetch data to populate the cache */
		OSL_PREFETCH(src_addr);

		dhd_prot_process_msgtype(dhd, src_addr, src_len);
		circularbuf_read_complete(dtoh_msgbuf, src_len);

		/* Write to dngl rd ptr */
		dhd_bus_cmn_writeshared(dhd->bus, &CIRCULARBUF_READ_PTR(dtoh_msgbuf),
			sizeof(uint16), DNGL_TO_HOST_RPTR);
	}

	return 0;
}

int BCMFASTPATH
dhd_prot_process_ctrlbuf(dhd_pub_t * dhd)
{
	dhd_prot_t *prot = dhd->prot;
	circularbuf_t *dtoh_ctrlbuf = (circularbuf_t *)prot->dtoh_ctrlbuf;

	dhd_bus_cmn_readshared(dhd->bus, &CIRCULARBUF_WRITE_PTR(dtoh_ctrlbuf), DTOH_CTRL_WPTR);

	/* Process all the messages - DTOH direction */
	while (TRUE) {
		uint8 *src_addr;
		uint16 src_len;

		src_addr = circularbuf_get_read_ptr(dtoh_ctrlbuf, &src_len);
		if (src_addr == NULL) {
			break;
		}
		/* Prefetch data to populate the cache */
		OSL_PREFETCH(src_addr);

		dhd_prot_process_msgtype(dhd, src_addr, src_len);
		circularbuf_read_complete(dtoh_ctrlbuf, src_len);

		/* Write to dngl rd ptr */
		dhd_bus_cmn_writeshared(dhd->bus, &CIRCULARBUF_READ_PTR(dtoh_ctrlbuf),
			sizeof(uint16), DTOH_CTRL_RPTR);
	}

	return 0;
}

static void BCMFASTPATH
dhd_prot_process_msgtype(dhd_pub_t *dhd, uint8* buf, uint16 len)
{
	dhd_prot_t *prot = dhd->prot;
	uint32 cur_dma_len = 0;

	DHD_TRACE(("%s: process msgbuf of len %d\n", __FUNCTION__, len));

	while (len > 0) {
		ASSERT(len > (sizeof(cmn_msg_hdr_t) + prot->rx_dataoffset));
		if (prot->rx_dataoffset) {
			cur_dma_len = *(uint32 *) buf;
			ASSERT(cur_dma_len <= len);
			buf += prot->rx_dataoffset;
			len -= (uint16)prot->rx_dataoffset;
		}
		else {
			cur_dma_len = len;
		}
		dhd_process_msgtype(dhd, buf, (uint16)cur_dma_len);
		len -= (uint16)cur_dma_len;
		buf += cur_dma_len;
	}
}


static void
dhd_check_sequence_num(cmn_msg_hdr_t *msg)
{
	static uint32 ioctl_seq_no_old = 0;
	static uint32 data_seq_no_old = 0;

	switch (msg->msgtype) {
		case MSG_TYPE_IOCTL_CMPLT:
			if (msg->u.seq.seq_no && msg->u.seq.seq_no != (ioctl_seq_no_old + 1))
			{
				DHD_ERROR(("Error in IOCTL MsgBuf Sequence number!!"
				"new seq no %u, old seq number %u\n",
				msg->u.seq.seq_no, ioctl_seq_no_old));
			}
			ioctl_seq_no_old  = msg->u.seq.seq_no;
			break;

		case MSG_TYPE_RX_CMPLT:
		case MSG_TYPE_WL_EVENT :
		case MSG_TYPE_TX_STATUS :
		case MSG_TYPE_LOOPBACK:
			if (msg->u.seq.seq_no && msg->u.seq.seq_no != (data_seq_no_old + 1))
			{
				DHD_ERROR(("Error in DATA MsgBuf Sequence number!!"
					"new seq no %u	 old seq number %u\n",
					msg->u.seq.seq_no, data_seq_no_old));
			}
			data_seq_no_old = msg->u.seq.seq_no;
			break;

		default:
			printf("Unknown MSGTYPE in %s \n", __FUNCTION__);
			break;

	}
}

static void BCMFASTPATH
dhd_process_msgtype(dhd_pub_t *dhd, uint8* buf, uint16 len)
{
	uint16 pktlen = len;
	uint16 msglen;
	uint8 msgtype;
	cmn_msg_hdr_t *msg = NULL;
	while (pktlen > 0) {
		msg = (cmn_msg_hdr_t *)buf;
		msgtype = msg->msgtype;
		msglen = msg->msglen;

		/* Prefetch data to populate the cache */
		OSL_PREFETCH(buf+msglen);

		dhd_check_sequence_num(msg);

		DHD_INFO(("msgtype %d, msglen is %d \n", msgtype, msglen));
		switch (msgtype) {
			case MSG_TYPE_IOCTL_CMPLT:
				DHD_INFO((" MSG_TYPE_IOCTL_CMPLT\n"));
				dhd_prot_ioctcmplt_process(dhd, buf);
				break;
			case MSG_TYPE_RX_CMPLT:
				DHD_INFO((" MSG_TYPE_RX_CMPLT\n"));
				dhd_prot_rxcmplt_process(dhd, buf);
				break;
			case MSG_TYPE_WL_EVENT:
				DHD_INFO((" MSG_TYPE_WL_EVENT\n"));
				dhd_prot_event_process(dhd, buf, msglen);
				break;
			case MSG_TYPE_TX_STATUS:
				DHD_INFO((" MSG_TYPE_TX_STATUS\n"));
				dhd_prot_txstatus_process(dhd, buf);
				break;
			case MSG_TYPE_LOOPBACK:
				bcm_print_bytes("LPBK RESP: ", (uint8 *)msg, msglen);
				DHD_ERROR((" MSG_TYPE_LOOPBACK, len %d\n", msglen));
				break;
			default :
				DHD_ERROR(("Unknown state in %s,"
				"rxoffset %d\n", __FUNCTION__, dhd->prot->rx_dataoffset));
				bcm_print_bytes("UNKNOWN msg", (uchar *)msg, msglen);
				break;

		}

		DHD_INFO(("pktlen is %d, msglen is %d\n", pktlen, msglen));
		if (pktlen < msglen)  {
			return;
		}
		pktlen = pktlen - msglen;
		buf = buf + msglen;
	}
}
static void
dhd_prot_ioctcmplt_process(dhd_pub_t *dhd, void * buf)
{
	uint32 retlen, status, inline_data = 0;
	uint32 pkt_id, xt_id;

	ioct_resp_hdr_t * ioct_resp = (ioct_resp_hdr_t *)buf;
	retlen = ltoh32(ioct_resp->ret_len);
	pkt_id = ltoh32(ioct_resp->pkt_id);
	xt_id = ltoh32(ioct_resp->xt_id);
	status = ioct_resp->status;
	if (retlen <= 4) {
		inline_data = ltoh32(ioct_resp->inline_data);
	} else {
		OSL_CACHE_INV((void *) dhd->prot->retbuf, retlen);
	}
	DHD_CTL(("status from the pkt_id is %d, ioctl is %d, ret_len is %d, xt_id %d\n",
		pkt_id, status, retlen, xt_id));

	if (retlen == 0)
		retlen = 1;

	dhd_bus_update_retlen(dhd->bus, retlen, pkt_id, status, inline_data);
	dhd_os_ioctl_resp_wake(dhd);
}

static void BCMFASTPATH
dhd_prot_txstatus_process(dhd_pub_t *dhd, void * buf)
{
	dhd_prot_t *prot = dhd->prot;
	txstatus_hdr_t * txstatus;
	unsigned long flags;
	uint32 pktid;

	/* locks required to protect circular buffer accesses */
	flags = dhd_os_spin_lock(dhd);

	txstatus = (txstatus_hdr_t *)buf;
	pktid = ltoh32(txstatus->pktid);

	prot->active_tx_count--;

	ASSERT(pktid != 0);
	dhd_prot_packet_free(dhd, pktid);

	if (prot->txflow_en == TRUE) {
		/* If the pktpool availability is above the high watermark, */
		/* let's resume the flow of packets to dongle. */
		if ((prot->max_tx_count - prot->active_tx_count) > DHD_START_QUEUE_THRESHOLD) {
			dhd_bus_start_queue(dhd->bus);
			prot->txflow_en = FALSE;
		}
	}

	dhd_os_spin_unlock(dhd, flags);
	return;
}

static void
dhd_prot_event_process(dhd_pub_t *dhd, uint8* buf, uint16 len)
{
	wl_event_hdr_t * evnt;
	uint32 bufid;
	uint16 buflen;
	int ifidx = 0;
	uint pkt_count = 1;
	void* pkt;
	unsigned long flags;

	/* Event complete header */
	evnt = (wl_event_hdr_t *)buf;
	bufid = ltoh32(evnt->rxbufid);
	buflen = ltoh16(evnt->retbuf_len);

	/* Post another rxbuf to the device */
	dhd_prot_return_rxbuf(dhd, 1);

	/* locks required to protect pktid_map */
	flags = dhd_os_spin_lock(dhd);

	pkt = dhd_prot_packet_get(dhd, ltoh32(bufid));

	dhd_os_spin_unlock(dhd, flags);

	/* DMA RX offset updated through shared area */
	if (dhd->prot->rx_dataoffset)
		PKTPULL(dhd->osh, pkt, dhd->prot->rx_dataoffset);

	PKTSETLEN(dhd->osh, pkt, buflen);

	/* remove WL header */
	PKTPULL(dhd->osh, pkt, 4); /* WL Header */

	dhd_bus_rx_frame(dhd->bus, pkt, ifidx, pkt_count);
}

static void BCMFASTPATH
dhd_prot_rxcmplt_process(dhd_pub_t *dhd, void* buf)
{
	rxcmplt_hdr_t *rxcmplt_h;
	rxcmplt_tup_t *rx_tup;
	uint32 bufid;
	uint16 buflen, cmpltcnt;
	uint16 data_offset;             /* offset at which data starts */
	void * pkt;
	int ifidx = 0;
	uint pkt_count = 0;
	uint32 i;
	void *pkthead = NULL;
	void *pkttail = NULL;

	/* RXCMPLT HDR */
	rxcmplt_h = (rxcmplt_hdr_t *)buf;
	cmpltcnt = ltoh16(rxcmplt_h->rxcmpltcnt);

	/* Post another set of rxbufs to the device */
	dhd_prot_return_rxbuf(dhd, cmpltcnt);
	ifidx = rxcmplt_h->msg.ifidx;

	rx_tup = (rxcmplt_tup_t *) &(rxcmplt_h->rx_tup[0]);
	for (i = 0; i < cmpltcnt; i++) {
		unsigned long flags;

		bufid = ltoh32(rx_tup->rxbufid);
		buflen = ltoh16(rx_tup->retbuf_len);

		/* offset from which data starts is populated in rxstatus0 */
		data_offset = ltoh16(rx_tup->data_offset);

		/* locks required to protect pktid_map */
		flags = dhd_os_spin_lock(dhd);
		pkt = dhd_prot_packet_get(dhd, ltoh32(bufid));
		dhd_os_spin_unlock(dhd, flags);

		/* data_offset from buf start */
		if (data_offset) {
			/* data offset given from dongle after split rx */
			PKTPULL(dhd->osh, pkt, data_offset); /* data offset */
		} else {
			/* DMA RX offset updated through shared area */
			if (dhd->prot->rx_dataoffset)
				PKTPULL(dhd->osh, pkt, dhd->prot->rx_dataoffset);
		}

		/* Actual length of the packet */
		PKTSETLEN(dhd->osh, pkt, buflen);

		/* remove WL header */
		PKTPULL(dhd->osh, pkt, 4); /* WL Header */

		pkt_count++;
		rx_tup++;

		/* Chain the packets and release in one shot to dhd_linux. */
		/* Interface and destination checks are not required here. */
		PKTSETNEXT(dhd->osh, pkt, NULL);
		if (pkttail == NULL) {
			pkthead = pkttail = pkt;
		} else {
			PKTSETNEXT(dhd->osh, pkttail, pkt);
			pkttail = pkt;
		}
	}

	if (pkthead) {
		/* Release the packets to dhd_linux */
		dhd_bus_rx_frame(dhd->bus, pkthead, ifidx, pkt_count);
	}
}
/* Stop protocol: sync w/dongle state. */
void dhd_prot_stop(dhd_pub_t *dhd)
{
	/* nothing to do for pcie */
}

/* Add any protocol-specific data header.
 * Caller must reserve prot_hdrlen prepend space.
 */
void dhd_prot_hdrpush(dhd_pub_t *dhd, int ifidx, void *PKTBUF)
{
	return;
}

#define PKTBUF pktbuf

int BCMFASTPATH
dhd_prot_txdata(dhd_pub_t *dhd, void *PKTBUF, uint8 ifidx)
{
	unsigned long flags;
	dhd_prot_t *prot = dhd->prot;
	circularbuf_t *htod_msgbuf = (circularbuf_t *)prot->htodbuf;
	txdescr_msghdr_t *txdesc = NULL;
	tx_lenptr_tup_t *tx_tup;
	dmaaddr_t physaddr;
	uint8 *pktdata;
	uint8 *etherhdr;
	uint16 pktlen;
	uint16 hdrlen;
	uint32 pktid;

	/* Extract the data pointer and length information */
	pktdata = PKTDATA(dhd->osh, PKTBUF);
	pktlen  = (uint16)PKTLEN(dhd->osh, PKTBUF);

	/* Extract the ethernet header and adjust the data pointer and length */
	etherhdr = pktdata;
	pktdata += ETHER_HDR_LEN;
	pktlen  -= ETHER_HDR_LEN;


	flags = dhd_os_spin_lock(dhd);

	/* Map the data pointer to a DMA-able address */
	physaddr = DMA_MAP(dhd->osh, pktdata, pktlen, DMA_TX, 0, 0);
	if (physaddr == 0) {
		DHD_ERROR(("Something really bad, unless 0 is a valid phyaddr\n"));
		ASSERT(0);
	}

	/* Create a unique 32-bit packet id */
	pktid = NATIVE_TO_PKTID(dhd->prot->pktid_map_handle, PKTBUF, physaddr, pktlen, DMA_TX);

	/* Reserve space in the circular buffer */
	hdrlen =  sizeof(txdescr_msghdr_t) + (1 * sizeof(tx_lenptr_tup_t));

	txdesc = (txdescr_msghdr_t *)dhd_alloc_circularbuf_space(dhd,
		htod_msgbuf, hdrlen, HOST_TO_DNGL_DATA);
	if (txdesc == NULL) {
		dhd_prot_packet_free(dhd, pktid);
		dhd_os_spin_unlock(dhd, flags);

		DHD_INFO(("%s:%d: HTOD Msgbuf Not available TxCount = %d\n",
			__FUNCTION__, __LINE__, prot->active_tx_count));
		return BCME_NORESOURCE;
	}

	/* Form the Tx descriptor message buffer */

	/* Common message hdr */
	txdesc->txcmn.msg.msglen = htol16(hdrlen);
	txdesc->txcmn.msg.msgtype = MSG_TYPE_TX_POST;
	txdesc->txcmn.msg.u.seq.seq_no = htol16(++prot->data_seq_no);

	/* Ethernet header */
	txdesc->txcmn.hdrlen = htol16(ETHER_HDR_LEN);
	bcopy(etherhdr, txdesc->txhdr, ETHER_HDR_LEN);

	/* Packet ID */
	txdesc->txcmn.pktid = htol32(pktid);

	/* Descriptor count - Linux needs only one */
	txdesc->txcmn.descrcnt = 0x1;

	tx_tup = (tx_lenptr_tup_t *) &(txdesc->tx_tup);

	/* Descriptor - 0 */
	tx_tup->pktlen = htol16(pktlen);
	tx_tup->ret_buf.high_addr = htol32(PHYSADDRHI(physaddr));
	tx_tup->ret_buf.low_addr  = htol32(PHYSADDRLO(physaddr));
	/* Descriptor 1 - should be filled here - if required */

	/* Reserved for future use */
	txdesc->txcmn.priority = (uint8)PKTPRIO(PKTBUF);
	txdesc->txcmn.flowid   = 0;
	txdesc->txcmn.msg.ifidx = ifidx;

	/* Since, we are filling the data directly into the bufptr obtained
	 * from the circularbuf, we can directly call the write_complete
	 */
	circularbuf_write_complete(htod_msgbuf, hdrlen);

	prot->active_tx_count++;

	/* If we have accounted for most of the lfrag packets on the dongle, */
	/* it's time to stop the packet flow - Assert flow control. */
	if ((prot->max_tx_count - prot->active_tx_count) < DHD_STOP_QUEUE_THRESHOLD) {
		dhd_bus_stop_queue(dhd->bus);
		prot->txflow_en = TRUE;
	}

	dhd_os_spin_unlock(dhd, flags);

	return BCME_OK;
}

#undef PKTBUF	/* Only defined in the above routine */
int dhd_prot_hdrpull(dhd_pub_t *dhd, int *ifidx, void *pkt, uchar *buf, uint *len)
{
	return 0;
}

static void BCMFASTPATH
dhd_prot_return_rxbuf(dhd_pub_t *dhd, uint16 rxcnt)
{
	dhd_prot_t *prot = dhd->prot;

	prot->rxbufpost -= rxcnt;
	if (prot->rxbufpost <= (prot->max_rxbufpost - RXBUFPOST_THRESHOLD))
		dhd_msgbuf_rxbuf_post(dhd);

	return;
}

/* Use protocol to issue ioctl to dongle */
int dhd_prot_ioctl(dhd_pub_t *dhd, int ifidx, wl_ioctl_t * ioc, void * buf, int len)
{
	dhd_prot_t *prot = dhd->prot;
	int ret = -1;
	uint8 action;

	if ((dhd->busstate == DHD_BUS_DOWN) || dhd->hang_was_sent) {
		DHD_ERROR(("%s : bus is down. we have nothing to do\n", __FUNCTION__));
		goto done;
	}

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(len <= WLC_IOCTL_MAXLEN);

	if (len > WLC_IOCTL_MAXLEN)
		goto done;

	if (prot->pending == TRUE) {
		DHD_ERROR(("packet is pending!!!! cmd=0x%x (%lu) lastcmd=0x%x (%lu)\n",
			ioc->cmd, (unsigned long)ioc->cmd, prot->lastcmd,
			(unsigned long)prot->lastcmd));
		if ((ioc->cmd == WLC_SET_VAR) || (ioc->cmd == WLC_GET_VAR)) {
			DHD_TRACE(("iovar cmd=%s\n", (char*)buf));
		}
		goto done;
	}

	prot->pending = TRUE;
	prot->lastcmd = ioc->cmd;
	action = ioc->set;
	if (action & WL_IOCTL_ACTION_SET) {
		ret = dhd_msgbuf_set_ioctl(dhd, ifidx, ioc->cmd, buf, len, action);
	} else {
		ret = dhdmsgbuf_query_ioctl(dhd, ifidx, ioc->cmd, buf, len, action);
		if (ret > 0)
			ioc->used = ret;
	}
	/* Too many programs assume ioctl() returns 0 on success */
	if (ret >= 0)
		ret = 0;
	else {
		DHD_INFO(("%s: status ret value is %d \n", __FUNCTION__, ret));
		dhd->dongle_error = ret;
	}

	/* Intercept the wme_dp ioctl here */
	if ((!ret) && (ioc->cmd == WLC_SET_VAR) && (!strcmp(buf, "wme_dp"))) {
		int slen, val = 0;

		slen = strlen("wme_dp") + 1;
		if (len >= (int)(slen + sizeof(int)))
			bcopy(((char *)buf + slen), &val, sizeof(int));
		dhd->wme_dp = (uint8) ltoh32(val);
	}

	prot->pending = FALSE;

done:
	return ret;

}

int
dhdmsgbuf_lpbk_req(dhd_pub_t *dhd, uint len)
{
	unsigned long flags;
	dhd_prot_t *prot = dhd->prot;
	circularbuf_t *htod_msgbuf;

	ioct_reqst_hdr_t *ioct_rqst;

	uint16 hdrlen = sizeof(ioct_reqst_hdr_t);
	uint16 msglen = len + hdrlen;

	if (dhd->prot->htodsplit)
		htod_msgbuf = (circularbuf_t *) prot->htod_ctrlbuf;
	else
		htod_msgbuf = (circularbuf_t *) prot->htodbuf;

	if (msglen  > MSGBUF_MAX_MSG_SIZE)
		msglen = MSGBUF_MAX_MSG_SIZE;

	msglen = align(msglen, 4);

	/* locks required to protect circular buffer accesses */
	flags = dhd_os_spin_lock(dhd);

	if (dhd->prot->htodsplit) {
		ioct_rqst = (ioct_reqst_hdr_t *)dhd_alloc_circularbuf_space(dhd,
			htod_msgbuf, msglen, HOST_TO_DNGL_CTRL);
	}
	else {
		ioct_rqst = (ioct_reqst_hdr_t *)dhd_alloc_circularbuf_space(dhd,
			htod_msgbuf, msglen, HOST_TO_DNGL_DATA);
	}

	if (ioct_rqst == NULL) {
		dhd_os_spin_unlock(dhd, flags);
		return 0;
	}

	{
		uint8 *ptr;
		uint16 i;

		ptr = (uint8 *)ioct_rqst;
		for (i = 0; i < msglen; i++) {
			ptr[i] = i % 256;
		}
	}


	/* Common msg buf hdr */
	ioct_rqst->msg.msglen = htol16(msglen);
	ioct_rqst->msg.msgtype = MSG_TYPE_LOOPBACK;
	ioct_rqst->msg.ifidx = 0;
	ioct_rqst->msg.u.seq.seq_no = htol16(++prot->data_seq_no);

	bcm_print_bytes("LPBK REQ: ", (uint8 *)ioct_rqst, msglen);

	circularbuf_write_complete(htod_msgbuf, msglen);

	dhd_os_spin_unlock(dhd, flags);

	return 0;
}


static int
dhdmsgbuf_query_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len, uint8 action)
{
	dhd_prot_t *prot = dhd->prot;

	int ret = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	/* Respond "bcmerror" and "bcmerrorstr" with local cache */
	if (cmd == WLC_GET_VAR && buf)
	{
		if (!strcmp((char *)buf, "bcmerrorstr"))
		{
			strncpy((char *)buf, bcmerrorstr(dhd->dongle_error), BCME_STRLEN);
			goto done;
		}
		else if (!strcmp((char *)buf, "bcmerror"))
		{
			*(int *)buf = dhd->dongle_error;
			goto done;
		}
	}

	/* Fill up msgbuf for ioctl req */
	if (len < MAX_INLINE_IOCTL_LEN) {
		/* Inline ioct resuest */
		ret = dhd_fillup_ioct_reqst(dhd, (uint16)len, cmd, buf, ifidx);
	} else {
		/* Non inline ioct resuest */
		ret = dhd_fillup_ioct_reqst_ptrbased(dhd, (uint16)len, cmd, buf, ifidx);
	}

	DHD_INFO(("ACTION %d ifdix %d cmd %d len %d \n",
		action, ifidx, cmd, len));

	/* wait for interrupt and get first fragment */
	ret = dhdmsgbuf_cmplt(dhd, prot->reqid, len, buf, prot->retbuf);

done:
	return ret;
}
static int
dhdmsgbuf_cmplt(dhd_pub_t *dhd, uint32 id, uint32 len, void* buf, void* retbuf)
{
	dhd_prot_t *prot = dhd->prot;
	ioct_resp_hdr_t  ioct_resp;
	uint8* data;
	int retlen;
	int msgbuf_len = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	retlen = dhd_bus_rxctl(dhd->bus, (uchar*)&ioct_resp, msgbuf_len);

	if (retlen <= 0)
		return -1;

	/* get ret buf */
	if (buf != NULL) {
		if (retlen <= 4) {
			bcopy((void*)&ioct_resp.inline_data, buf, retlen);
			DHD_INFO(("%s: data is %d, ret_len is %d\n",
				__FUNCTION__, ioct_resp.inline_data, retlen));
		}
		else {
			data = (uint8*)retbuf;
			bcopy((void*)&data[prot->rx_dataoffset], buf, retlen);
		}
	}
	return ioct_resp.status;
}
static int
dhd_msgbuf_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len, uint8 action)
{
	dhd_prot_t *prot = dhd->prot;

	int ret = 0;

	DHD_TRACE(("%s: Enter \n", __FUNCTION__));
	DHD_TRACE(("%s: cmd %d len %d\n", __FUNCTION__, cmd, len));

	if (dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s : bus is down. we have nothing to do\n", __FUNCTION__));
		return -EIO;
	}

	/* don't talk to the dongle if fw is about to be reloaded */
	if (dhd->hang_was_sent) {
		DHD_ERROR(("%s: HANG was sent up earlier. Not talking to the chip\n",
			__FUNCTION__));
		return -EIO;
	}

	/* Fill up msgbuf for ioctl req */
	if (len < MAX_INLINE_IOCTL_LEN) {
		/* Inline ioct resuest */
		ret = dhd_fillup_ioct_reqst(dhd, (uint16)len, cmd, buf, ifidx);
	} else {
		/* Non inline ioct resuest */
		ret = dhd_fillup_ioct_reqst_ptrbased(dhd, (uint16)len, cmd, buf, ifidx);
	}

	DHD_INFO(("ACTIOn %d ifdix %d cmd %d len %d \n",
		action, ifidx, cmd, len));

	ret = dhdmsgbuf_cmplt(dhd, prot->reqid, len, buf, prot->retbuf);

	return ret;
}
/* Handles a protocol control response asynchronously */
int dhd_prot_ctl_complete(dhd_pub_t *dhd)
{
	return 0;
}

/* Check for and handle local prot-specific iovar commands */
int dhd_prot_iovar_op(dhd_pub_t *dhd, const char *name,
                             void *params, int plen, void *arg, int len, bool set)
{
	return BCME_UNSUPPORTED;
}

/* Add prot dump output to a buffer */
void dhd_prot_dump(dhd_pub_t *dhd, struct bcmstrbuf *strbuf)
{

}

/* Update local copy of dongle statistics */
void dhd_prot_dstats(dhd_pub_t *dhd)
{
		return;
}

int dhd_process_pkt_reorder_info(dhd_pub_t *dhd, uchar *reorder_info_buf,
	uint reorder_info_len, void **pkt, uint32 *free_buf_count)
{
	return 0;
}
/* post a dummy message to interrupt dongle */
/* used to process cons commands */
int
dhd_post_dummy_msg(dhd_pub_t *dhd)
{
	unsigned long flags;
	hostevent_hdr_t *hevent = NULL;
	uint16 msglen = sizeof(hostevent_hdr_t);

	dhd_prot_t *prot = dhd->prot;
	circularbuf_t *htod_msgbuf;

	/* locks required to protect circular buffer accesses */
	flags = dhd_os_spin_lock(dhd);
	if (dhd->prot->htodsplit) {
		htod_msgbuf = (circularbuf_t *)prot->htod_ctrlbuf;
		hevent = (hostevent_hdr_t *)dhd_alloc_circularbuf_space(dhd,
			htod_msgbuf, msglen, HOST_TO_DNGL_CTRL);
	}
	else {
		htod_msgbuf = (circularbuf_t *)prot->htodbuf;
		hevent = (hostevent_hdr_t *)dhd_alloc_circularbuf_space(dhd,
			htod_msgbuf, msglen, HOST_TO_DNGL_DATA);
	}

	if (hevent == NULL) {
		dhd_os_spin_unlock(dhd, flags);
		return -1;
	}

	/* CMN msg header */
	hevent->msg.msglen = htol16(msglen);
	hevent->msg.msgtype = MSG_TYPE_HOST_EVNT;
	hevent->msg.ifidx = 0;
	hevent->msg.u.seq.seq_no = htol16(++prot->data_seq_no);

	/* Event payload */
	hevent->evnt_pyld = htol32(HOST_EVENT_CONS_CMD);

	/* Since, we are filling the data directly into the bufptr obtained
	 * from the msgbuf, we can directly call the write_complete
	 */
	circularbuf_write_complete(htod_msgbuf, msglen);
	dhd_os_spin_unlock(dhd, flags);

	return 0;
}
void * BCMFASTPATH
dhd_alloc_circularbuf_space(dhd_pub_t *dhd, circularbuf_t *handle, uint16 msglen, uint path)
{
	void * ret_buf;

	ret_buf = circularbuf_reserve_for_write(handle, msglen);
	if (ret_buf == NULL) {
		/* Try again after updating the read ptr from dongle */
		if (path == HOST_TO_DNGL_DATA)
			dhd_bus_cmn_readshared(dhd->bus, &(CIRCULARBUF_READ_PTR(handle)),
			HOST_TO_DNGL_RPTR);
		else if (path == HOST_TO_DNGL_CTRL)
			dhd_bus_cmn_readshared(dhd->bus, &(CIRCULARBUF_READ_PTR(handle)),
			HTOD_CTRL_RPTR);
		else
			DHD_ERROR(("%s:%d: Unknown path value \n", __FUNCTION__, __LINE__));
		ret_buf = circularbuf_reserve_for_write(handle, msglen);
		if (ret_buf == NULL) {
			DHD_INFO(("%s:%d: HTOD Msgbuf Not available \n", __FUNCTION__, __LINE__));
			return NULL;
		}
	}

	return ret_buf;
}
INLINE bool
dhd_prot_dtohsplit(dhd_pub_t* dhd)
{
	return dhd->prot->dtohsplit;
}
static int
dhd_fillup_ioct_reqst(dhd_pub_t *dhd, uint16 len, uint cmd, void* buf, int ifidx)
{
	dhd_prot_t *prot = dhd->prot;
	ioct_reqst_hdr_t *ioct_rqst;
	uint16 hdrlen = sizeof(ioct_reqst_hdr_t);
	uint16 msglen = len + hdrlen;
	circularbuf_t *htod_msgbuf;
	unsigned long flags;
	uint16 rqstlen = len;

	/* Limit ioct request to MSGBUF_MAX_MSG_SIZE bytes including hdrs */
	if (rqstlen + hdrlen > MSGBUF_MAX_MSG_SIZE)
		rqstlen = MSGBUF_MAX_MSG_SIZE - hdrlen;

	/* Messge = hdr + rqstbuf */
	msglen = rqstlen + hdrlen;

	/* align it to 4 bytes, so that all start addr form cbuf is 4 byte aligned */
	msglen = align(msglen, 4);

	/* locks required to protect circular buffer accesses */
	flags = dhd_os_spin_lock(dhd);

	/* Request for cbuf space */
	if (dhd->prot->htodsplit) {
		htod_msgbuf = (circularbuf_t *)prot->htod_ctrlbuf;
		ioct_rqst = (ioct_reqst_hdr_t *)dhd_alloc_circularbuf_space(dhd,
			htod_msgbuf, msglen, HOST_TO_DNGL_CTRL);
	}
	else {
		htod_msgbuf = (circularbuf_t *)prot->htodbuf;
		ioct_rqst = (ioct_reqst_hdr_t *)dhd_alloc_circularbuf_space(dhd,
			htod_msgbuf, msglen, HOST_TO_DNGL_DATA);
	}

	if (ioct_rqst == NULL) {
		dhd_os_spin_unlock(dhd, flags);
		return -1;
	}

	/* Common msg buf hdr */
	ioct_rqst->msg.msglen = htol16(msglen);
	ioct_rqst->msg.msgtype = MSG_TYPE_IOCTL_REQ;
	ioct_rqst->msg.ifidx = (uint8)ifidx;
	ioct_rqst->msg.u.seq.seq_no = htol16(++prot->ioctl_seq_no);

	/* Ioctl specific Message buf header */
	ioct_rqst->ioct_hdr.cmd = htol32(cmd);
	ioct_rqst->ioct_hdr.pkt_id = htol32(++prot->reqid);
	ioct_rqst->ioct_hdr.retbuf_len = htol16(len);
	ioct_rqst->ioct_hdr.xt_id = (uint16)ioct_rqst->ioct_hdr.pkt_id;
	DHD_CTL(("sending IOCTL_REQ cmd %d, pkt_id %d  xt_id %d\n",
		ioct_rqst->ioct_hdr.cmd, ioct_rqst->ioct_hdr.pkt_id, ioct_rqst->ioct_hdr.xt_id));

	/* Ret buf ptr */
	ioct_rqst->ret_buf.high_addr = htol32(PHYSADDRHI(prot->retbuf_phys));
	ioct_rqst->ret_buf.low_addr  = htol32(PHYSADDRLO(prot->retbuf_phys));

	/* copy ioct payload */
	if (buf)
		memcpy(&ioct_rqst[1], buf, rqstlen);

	/* upd wrt ptr and raise interrupt */
	circularbuf_write_complete(htod_msgbuf, msglen);
	dhd_os_spin_unlock(dhd, flags);

	return 0;
}
/* Non inline ioct request */
/* Form a ioctl request first as per ioctptr_reqst_hdr_t header in the circular buffer */
/* Form a separate request buffer where a 4 byte cmn header is added in the front */
/* buf contents from parent function is copied to remaining section of this buffer */
static int
dhd_fillup_ioct_reqst_ptrbased(dhd_pub_t *dhd, uint16 len, uint cmd, void* buf, int ifidx)
{
	dhd_prot_t *prot = dhd->prot;
	ioctptr_reqst_hdr_t *ioct_rqst;
	uint16 msglen = sizeof(ioctptr_reqst_hdr_t);
	circularbuf_t * htod_msgbuf;
	cmn_msg_hdr_t * ioct_buf;	/* For ioctl payload */
	uint16 alignlen, rqstlen = len;
	unsigned long flags;

	/* Limit ioct request to MSGBUF_MAX_MSG_SIZE bytes including hdrs */
	if ((rqstlen  + sizeof(cmn_msg_hdr_t)) > MSGBUF_MAX_MSG_SIZE)
		rqstlen = MSGBUF_MAX_MSG_SIZE - sizeof(cmn_msg_hdr_t);

	/* align it to 4 bytes, so that all start addr form cbuf is 4 byte aligned */
	alignlen = align(rqstlen, 4);

	/* locks required to protect circular buffer accesses */
	flags = dhd_os_spin_lock(dhd);
	/* Request for cbuf space */
	if (dhd->prot->htodsplit) {
		htod_msgbuf = (circularbuf_t *)prot->htod_ctrlbuf;
		ioct_rqst = (ioctptr_reqst_hdr_t*)dhd_alloc_circularbuf_space(dhd,
			htod_msgbuf, msglen, HOST_TO_DNGL_CTRL);
	}
	else {
		htod_msgbuf = (circularbuf_t *)prot->htodbuf;
		ioct_rqst = (ioctptr_reqst_hdr_t*)dhd_alloc_circularbuf_space(dhd,
			htod_msgbuf, msglen, HOST_TO_DNGL_DATA);
	}
	if (ioct_rqst == NULL) {
		dhd_os_spin_unlock(dhd, flags);
		return -1;
	}

	/* Common msg buf hdr */
	ioct_rqst->msg.msglen = htol16(msglen);
	ioct_rqst->msg.msgtype = MSG_TYPE_IOCTLPTR_REQ;
	ioct_rqst->msg.ifidx = (uint8)ifidx;
	ioct_rqst->msg.u.seq.seq_no = htol16(++prot->ioctl_seq_no);

	/* Ioctl specific Message buf header */
	ioct_rqst->ioct_hdr.cmd = htol32(cmd);
	ioct_rqst->ioct_hdr.pkt_id = htol32(++prot->reqid);
	ioct_rqst->ioct_hdr.retbuf_len = htol16(len);
	ioct_rqst->ioct_hdr.xt_id = (uint16)ioct_rqst->ioct_hdr.pkt_id;

	DHD_CTL(("sending IOCTL_PTRREQ cmd %d, pkt_id %d  xt_id %d\n",
		ioct_rqst->ioct_hdr.cmd, ioct_rqst->ioct_hdr.pkt_id, ioct_rqst->ioct_hdr.xt_id));

	/* Ret buf ptr */
	ioct_rqst->ret_buf.high_addr = htol32(PHYSADDRHI(prot->retbuf_phys));
	ioct_rqst->ret_buf.low_addr  = htol32(PHYSADDRLO(prot->retbuf_phys));

	/* copy ioct payload */
	ioct_buf = (cmn_msg_hdr_t *) prot->ioctbuf;
	ioct_buf->msglen = htol16(alignlen + sizeof(cmn_msg_hdr_t));
	ioct_buf->msgtype = MSG_TYPE_IOCT_PYLD;

	if (buf) {
		memcpy(&ioct_buf[1], buf, rqstlen);
		OSL_CACHE_FLUSH((void *) prot->ioctbuf, rqstlen+sizeof(cmn_msg_hdr_t));
	}

	if ((ulong)ioct_buf % 4)
		printf("host ioct address unaligned !!!!! \n");

	/* populate ioctl buffer info */
	ioct_rqst->ioct_hdr.buflen = htol16(alignlen + sizeof(cmn_msg_hdr_t));
	ioct_rqst->ioct_buf.high_addr = htol32(PHYSADDRHI(prot->ioctbuf_phys));
	ioct_rqst->ioct_buf.low_addr  = htol32(PHYSADDRLO(prot->ioctbuf_phys));

	/* upd wrt ptr and raise interrupt */
	circularbuf_write_complete(htod_msgbuf, msglen);

	dhd_os_spin_unlock(dhd, flags);

	return 0;
}

/* Packet to PacketID mapper */
typedef struct {
	ulong native;
	dmaaddr_t pa;
	uint32 pa_len;
	uchar dma;
} pktid_t;

typedef struct {
	void	*osh;
	void	*mwbmap_hdl;
	pktid_t *pktid_list;
	uint32	count;
} pktid_map_t;


void *pktid_map_init(void *osh, uint32 count)
{
	pktid_map_t *handle;

	handle = (pktid_map_t *) MALLOC(osh, sizeof(pktid_map_t));
	if (handle == NULL) {
		printf("%s:%d: MALLOC failed for size %d\n",
			__FUNCTION__, __LINE__, (uint32) sizeof(pktid_map_t));
		return NULL;
	}
	handle->osh = osh;
	handle->count = count;
	handle->mwbmap_hdl = bcm_mwbmap_init(osh, count);
	if (handle->mwbmap_hdl == NULL) {
		printf("%s:%d: bcm_mwbmap_init failed for count %d\n",
			__FUNCTION__, __LINE__, count);
		MFREE(osh, handle, sizeof(pktid_map_t));
		return NULL;
	}

	handle->pktid_list = (pktid_t *) MALLOC(osh, sizeof(pktid_t) * (count+1));
	if (handle->pktid_list == NULL) {
		printf("%s:%d: MALLOC failed for count %d / total = %d\n",
			__FUNCTION__, __LINE__, count, (uint32) sizeof(pktid_t) * count);
		bcm_mwbmap_fini(osh, handle->mwbmap_hdl);
		MFREE(osh, handle, sizeof(pktid_map_t));
		return NULL;
	}

	return handle;
}

void
pktid_map_uninit(void *pktid_map_handle)
{
	pktid_map_t *handle = (pktid_map_t *) pktid_map_handle;
	uint32 ix;

	if (handle != NULL) {
		void *osh = handle->osh;
		for (ix = 0; ix < MAX_PKTID_ITEMS; ix++)
		{
			if (!bcm_mwbmap_isfree(handle->mwbmap_hdl, ix)) {
				/* Mark the slot as free */
				bcm_mwbmap_free(handle->mwbmap_hdl, ix);
				/*
				Here we can do dma unmapping for 32 bit also.
				Since this in removal path, it will not affect performance
				*/
				DMA_UNMAP(osh, (uint) handle->pktid_list[ix+1].pa,
					(uint) handle->pktid_list[ix+1].pa_len,
					handle->pktid_list[ix+1].dma, 0, 0);
				PKTFREE(osh,
					(unsigned long*)handle->pktid_list[ix+1].native, TRUE);
			}
		}
		bcm_mwbmap_fini(osh, handle->mwbmap_hdl);
		MFREE(osh, handle->pktid_list, sizeof(pktid_t) * (handle->count+1));
		MFREE(osh, handle, sizeof(pktid_map_t));
	}
	return;
}

uint32 BCMFASTPATH
pktid_map_unique(void *pktid_map_handle, void *pkt, dmaaddr_t physaddr, uint32 physlen, uint32 dma)
{
	uint32 id;
	pktid_map_t *handle = (pktid_map_t *) pktid_map_handle;

	if (handle == NULL) {
		printf("%s:%d: Error !!! pktid_map_unique called without initing pktid_map\n",
			__FUNCTION__, __LINE__);
		return 0;
	}
	id = bcm_mwbmap_alloc(handle->mwbmap_hdl);
	if (id == BCM_MWBMAP_INVALID_IDX) {
		printf("%s:%d: bcm_mwbmap_alloc failed. Free Count = %d\n",
			__FUNCTION__, __LINE__, bcm_mwbmap_free_cnt(handle->mwbmap_hdl));
		return 0;
	}

	/* id=0 is invalid as we use this for error checking in the dongle */
	id += 1;
	handle->pktid_list[id].native = (ulong) pkt;
	handle->pktid_list[id].pa     = physaddr;
	handle->pktid_list[id].pa_len = (uint32) physlen;
	handle->pktid_list[id].dma = dma;

	return id;
}

void * BCMFASTPATH
pktid_get_packet(void *pktid_map_handle, uint32 id, dmaaddr_t *physaddr, uint32 *physlen)
{
	void *native = NULL;
	pktid_map_t *handle = (pktid_map_t *) pktid_map_handle;
	if (handle == NULL) {
		printf("%s:%d: Error !!! pktid_get_packet called without initing pktid_map\n",
			__FUNCTION__, __LINE__);
		return NULL;
	}

	/* Debug check */
	if (bcm_mwbmap_isfree(handle->mwbmap_hdl, (id-1))) {
		printf("%s:%d: Error !!!. How can the slot (%d) be free if the app is using it.\n",
			__FUNCTION__, __LINE__, (id-1));
		return NULL;
	}

	native = (void *) handle->pktid_list[id].native;
	*physaddr = handle->pktid_list[id].pa;
	*physlen  = (uint32) handle->pktid_list[id].pa_len;

	/* Mark the slot as free */
	bcm_mwbmap_free(handle->mwbmap_hdl, (id-1));

	return native;
}
