/*
 * BT-AMP support routines
 *
 * Copyright (C) 1999-2014, Broadcom Corporation
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
 * $Id: dhd_bta.c 434656 2013-11-07 01:11:33Z $
 */
#error "WLBTAMP is not defined"

#include <typedefs.h>
#include <osl.h>
#include <bcmcdc.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <proto/802.11.h>
#include <proto/802.11_bta.h>
#include <proto/bt_amp_hci.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhdioctl.h>
#include <dhd_dbg.h>

#include <dhd_bta.h>


#ifdef SEND_HCI_CMD_VIA_IOCTL
#define BTA_HCI_CMD_MAX_LEN HCI_CMD_PREAMBLE_SIZE + HCI_CMD_DATA_SIZE

/* Send HCI cmd via wl iovar HCI_cmd to the dongle. */
int
dhd_bta_docmd(dhd_pub_t *pub, void *cmd_buf, uint cmd_len)
{
	amp_hci_cmd_t *cmd = (amp_hci_cmd_t *)cmd_buf;
	uint8 buf[BTA_HCI_CMD_MAX_LEN + 16];
	uint len = sizeof(buf);
	wl_ioctl_t ioc;

	if (cmd_len < HCI_CMD_PREAMBLE_SIZE)
		return BCME_BADLEN;

	if ((uint)cmd->plen + HCI_CMD_PREAMBLE_SIZE > cmd_len)
		return BCME_BADLEN;

	len = bcm_mkiovar("HCI_cmd",
		(char *)cmd, (uint)cmd->plen + HCI_CMD_PREAMBLE_SIZE, (char *)buf, len);


	memset(&ioc, 0, sizeof(ioc));

	ioc.cmd = WLC_SET_VAR;
	ioc.buf = buf;
	ioc.len = len;
	ioc.set = TRUE;

	return dhd_wl_ioctl(pub, &ioc, ioc.buf, ioc.len);
}
#else /* !SEND_HCI_CMD_VIA_IOCTL */

static void
dhd_bta_flush_hcidata(dhd_pub_t *pub, uint16 llh)
{
	int prec;
	struct pktq *q;
	uint count = 0;

	q = dhd_bus_txq(pub->bus);
	if (q == NULL)
		return;

	DHD_BTA(("dhd: flushing HCI ACL data for logical link %u...\n", llh));

	dhd_os_sdlock_txq(pub);

	/* Walk through the txq and toss all HCI ACL data packets */
	PKTQ_PREC_ITER(q, prec) {
		void *head_pkt = NULL;

		while (pktq_ppeek(q, prec) != head_pkt) {
			void *pkt = pktq_pdeq(q, prec);
			int ifidx;

			dhd_prot_hdrpull(pub, &ifidx, pkt, NULL, NULL);

			if (PKTLEN(pub->osh, pkt) >= RFC1042_HDR_LEN) {
				struct ether_header *eh =
				        (struct ether_header *)PKTDATA(pub->osh, pkt);

				if (ntoh16(eh->ether_type) < ETHER_TYPE_MIN) {
					struct dot11_llc_snap_header *lsh =
					        (struct dot11_llc_snap_header *)&eh[1];

					if (bcmp(lsh, BT_SIG_SNAP_MPROT,
					         DOT11_LLC_SNAP_HDR_LEN - 2) == 0 &&
					    ntoh16(lsh->type) == BTA_PROT_L2CAP) {
						amp_hci_ACL_data_t *ACL_data =
						        (amp_hci_ACL_data_t *)&lsh[1];
						uint16 handle = ltoh16(ACL_data->handle);

						if (HCI_ACL_DATA_HANDLE(handle) == llh) {
							PKTFREE(pub->osh, pkt, TRUE);
							count ++;
							continue;
						}
					}
				}
			}

			dhd_prot_hdrpush(pub, ifidx, pkt);

			if (head_pkt == NULL)
				head_pkt = pkt;
			pktq_penq(q, prec, pkt);
		}
	}

	dhd_os_sdunlock_txq(pub);

	DHD_BTA(("dhd: flushed %u packet(s) for logical link %u...\n", count, llh));
}

/* Handle HCI cmd locally.
 * Return 0: continue to send the cmd across SDIO
 *        < 0: stop, fail
 *        > 0: stop, succuess
 */
static int
_dhd_bta_docmd(dhd_pub_t *pub, amp_hci_cmd_t *cmd)
{
	int status = 0;

	switch (ltoh16_ua((uint8 *)&cmd->opcode)) {
	case HCI_Enhanced_Flush: {
		eflush_cmd_parms_t *cmdparms = (eflush_cmd_parms_t *)cmd->parms;
		dhd_bta_flush_hcidata(pub, ltoh16_ua(cmdparms->llh));
		break;
	}
	default:
		break;
	}

	return status;
}

/* Send HCI cmd encapsulated in BT-SIG frame via data channel to the dongle. */
int
dhd_bta_docmd(dhd_pub_t *pub, void *cmd_buf, uint cmd_len)
{
	amp_hci_cmd_t *cmd = (amp_hci_cmd_t *)cmd_buf;
	struct ether_header *eh;
	struct dot11_llc_snap_header *lsh;
	osl_t *osh = pub->osh;
	uint len;
	void *p;
	int status;

	if (cmd_len < HCI_CMD_PREAMBLE_SIZE) {
		DHD_ERROR(("dhd_bta_docmd: short command, cmd_len %u\n", cmd_len));
		return BCME_BADLEN;
	}

	if ((len = (uint)cmd->plen + HCI_CMD_PREAMBLE_SIZE) > cmd_len) {
		DHD_ERROR(("dhd_bta_docmd: malformed command, len %u cmd_len %u\n",
		           len, cmd_len));
		/* return BCME_BADLEN; */
	}

	p = PKTGET(osh, pub->hdrlen + RFC1042_HDR_LEN + len, TRUE);
	if (p == NULL) {
		DHD_ERROR(("dhd_bta_docmd: out of memory\n"));
		return BCME_NOMEM;
	}


	/* intercept and handle the HCI cmd locally */
	if ((status = _dhd_bta_docmd(pub, cmd)) > 0)
		return 0;
	else if (status < 0)
		return status;

	/* copy in HCI cmd */
	PKTPULL(osh, p, pub->hdrlen + RFC1042_HDR_LEN);
	bcopy(cmd, PKTDATA(osh, p), len);

	/* copy in partial Ethernet header with BT-SIG LLC/SNAP header */
	PKTPUSH(osh, p, RFC1042_HDR_LEN);
	eh = (struct ether_header *)PKTDATA(osh, p);
	bzero(eh->ether_dhost, ETHER_ADDR_LEN);
	ETHER_SET_LOCALADDR(eh->ether_dhost);
	bcopy(&pub->mac, eh->ether_shost, ETHER_ADDR_LEN);
	eh->ether_type = hton16(len + DOT11_LLC_SNAP_HDR_LEN);
	lsh = (struct dot11_llc_snap_header *)&eh[1];
	bcopy(BT_SIG_SNAP_MPROT, lsh, DOT11_LLC_SNAP_HDR_LEN - 2);
	lsh->type = 0;

	return dhd_sendpkt(pub, 0, p);
}
#endif /* !SEND_HCI_CMD_VIA_IOCTL */

/* Send HCI ACL data to dongle via data channel */
int
dhd_bta_tx_hcidata(dhd_pub_t *pub, void *data_buf, uint data_len)
{
	amp_hci_ACL_data_t *data = (amp_hci_ACL_data_t *)data_buf;
	struct ether_header *eh;
	struct dot11_llc_snap_header *lsh;
	osl_t *osh = pub->osh;
	uint len;
	void *p;

	if (data_len < HCI_ACL_DATA_PREAMBLE_SIZE) {
		DHD_ERROR(("dhd_bta_tx_hcidata: short data_buf, data_len %u\n", data_len));
		return BCME_BADLEN;
	}

	if ((len = (uint)ltoh16(data->dlen) + HCI_ACL_DATA_PREAMBLE_SIZE) > data_len) {
		DHD_ERROR(("dhd_bta_tx_hcidata: malformed hci data, len %u data_len %u\n",
		           len, data_len));
		/* return BCME_BADLEN; */
	}

	p = PKTGET(osh, pub->hdrlen + RFC1042_HDR_LEN + len, TRUE);
	if (p == NULL) {
		DHD_ERROR(("dhd_bta_tx_hcidata: out of memory\n"));
		return BCME_NOMEM;
	}


	/* copy in HCI ACL data header and HCI ACL data */
	PKTPULL(osh, p, pub->hdrlen + RFC1042_HDR_LEN);
	bcopy(data, PKTDATA(osh, p), len);

	/* copy in partial Ethernet header with BT-SIG LLC/SNAP header */
	PKTPUSH(osh, p, RFC1042_HDR_LEN);
	eh = (struct ether_header *)PKTDATA(osh, p);
	bzero(eh->ether_dhost, ETHER_ADDR_LEN);
	bcopy(&pub->mac, eh->ether_shost, ETHER_ADDR_LEN);
	eh->ether_type = hton16(len + DOT11_LLC_SNAP_HDR_LEN);
	lsh = (struct dot11_llc_snap_header *)&eh[1];
	bcopy(BT_SIG_SNAP_MPROT, lsh, DOT11_LLC_SNAP_HDR_LEN - 2);
	lsh->type = HTON16(BTA_PROT_L2CAP);

	return dhd_sendpkt(pub, 0, p);
}

/* txcomplete callback */
void
dhd_bta_tx_hcidata_complete(dhd_pub_t *dhdp, void *txp, bool success)
{
	uint8 *pktdata = (uint8 *)PKTDATA(dhdp->osh, txp);
	amp_hci_ACL_data_t *ACL_data = (amp_hci_ACL_data_t *)(pktdata + RFC1042_HDR_LEN);
	uint16 handle = ltoh16(ACL_data->handle);
	uint16 llh = HCI_ACL_DATA_HANDLE(handle);

	wl_event_msg_t event;
	uint8 data[HCI_EVT_PREAMBLE_SIZE + sizeof(num_completed_data_blocks_evt_parms_t)];
	amp_hci_event_t *evt;
	num_completed_data_blocks_evt_parms_t *parms;

	uint16 len = HCI_EVT_PREAMBLE_SIZE + sizeof(num_completed_data_blocks_evt_parms_t);

	/* update the event struct */
	memset(&event, 0, sizeof(event));
	event.version = hton16(BCM_EVENT_MSG_VERSION);
	event.event_type = hton32(WLC_E_BTA_HCI_EVENT);
	event.status = 0;
	event.reason = 0;
	event.auth_type = 0;
	event.datalen = hton32(len);
	event.flags = 0;

	/* generate Number of Completed Blocks event */
	evt = (amp_hci_event_t *)data;
	evt->ecode = HCI_Number_of_Completed_Data_Blocks;
	evt->plen = sizeof(num_completed_data_blocks_evt_parms_t);

	parms = (num_completed_data_blocks_evt_parms_t *)evt->parms;
	htol16_ua_store(dhdp->maxdatablks, (uint8 *)&parms->num_blocks);
	parms->num_handles = 1;
	htol16_ua_store(llh, (uint8 *)&parms->completed[0].handle);
	parms->completed[0].pkts = 1;
	parms->completed[0].blocks = 1;

	dhd_sendup_event_common(dhdp, &event, data);
}

/* event callback */
void
dhd_bta_doevt(dhd_pub_t *dhdp, void *data_buf, uint data_len)
{
	amp_hci_event_t *evt = (amp_hci_event_t *)data_buf;

	ASSERT(dhdp);
	ASSERT(evt);

	switch (evt->ecode) {
	case HCI_Command_Complete: {
		cmd_complete_parms_t *parms = (cmd_complete_parms_t *)evt->parms;
		switch (ltoh16_ua((uint8 *)&parms->opcode)) {
		case HCI_Read_Data_Block_Size: {
			read_data_block_size_evt_parms_t *parms2 =
			        (read_data_block_size_evt_parms_t *)parms->parms;
			dhdp->maxdatablks = ltoh16_ua((uint8 *)&parms2->data_block_num);
			break;
		}
		}
		break;
	}

	case HCI_Flush_Occurred: {
		flush_occurred_evt_parms_t *evt_parms = (flush_occurred_evt_parms_t *)evt->parms;
		dhd_bta_flush_hcidata(dhdp, ltoh16_ua((uint8 *)&evt_parms->handle));
		break;
	}
	default:
		break;
	}
}
