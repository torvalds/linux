/**
 * @file mlan_cmdevt.c
 *
 *  @brief This file contains the handling of CMD/EVENT in MLAN
 *
 *  Copyright (C) 2009-2017, Marvell International Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */

/*************************************************************
Change Log:
    05/12/2009: initial version
************************************************************/
#include "mlan.h"
#ifdef STA_SUPPORT
#include "mlan_join.h"
#endif
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_wmm.h"
#include "mlan_11n.h"
#include "mlan_11h.h"
#include "mlan_sdio.h"

/********************************************************
			Local Variables
********************************************************/

/*******************************************************
			Global Variables
********************************************************/

/********************************************************
			Local Functions
********************************************************/
#ifdef STA_SUPPORT
/**
 *  @brief This function inserts scan command node to scan_pending_q.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *  @return             N/A
 */
static t_void
wlan_queue_scan_cmd(IN mlan_private *pmpriv, IN cmd_ctrl_node *pcmd_node)
{
	mlan_adapter *pmadapter = pmpriv->adapter;

	ENTER();

	if (pcmd_node == MNULL)
		goto done;
	pcmd_node->cmd_flag |= CMD_F_SCAN;

	util_enqueue_list_tail(pmadapter->pmoal_handle,
			       &pmadapter->scan_pending_q,
			       (pmlan_linked_list)pcmd_node, MNULL, MNULL);

done:
	LEAVE();
}

/**
 *  @brief Internal function used to flush the scan pending queue
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
void
wlan_check_scan_queue(IN pmlan_adapter pmadapter)
{
	cmd_ctrl_node *pcmd_node = MNULL;
	t_u16 num = 0;

	pcmd_node =
		(cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						&pmadapter->scan_pending_q,
						MNULL, MNULL);
	if (!pcmd_node) {
		PRINTM(MERROR, "No pending scan command\n");
		return;
	}
	while (pcmd_node != (cmd_ctrl_node *)&pmadapter->scan_pending_q) {
		num++;
		pcmd_node = pcmd_node->pnext;
	}
	PRINTM(MERROR, "num_pending_scan=%d\n", num);
}
#endif

/**
 *  @brief This function will dump the pending commands id
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *
 *  @return             N/A
 */
static void
wlan_dump_pending_commands(pmlan_adapter pmadapter)
{
	cmd_ctrl_node *pcmd_node = MNULL;
	HostCmd_DS_COMMAND *pcmd;

	ENTER();
	wlan_request_cmd_lock(pmadapter);
	pcmd_node =
		(cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						&pmadapter->cmd_pending_q,
						MNULL, MNULL);
	if (!pcmd_node) {
		wlan_release_cmd_lock(pmadapter);
		LEAVE();
		return;
	}
	while (pcmd_node != (cmd_ctrl_node *)&pmadapter->cmd_pending_q) {
		pcmd = (HostCmd_DS_COMMAND *)(pcmd_node->cmdbuf->pbuf +
					      pcmd_node->cmdbuf->data_offset);
		PRINTM(MERROR, "pending command id: 0x%x ioctl_buf=%p\n",
		       wlan_le16_to_cpu(pcmd->command), pcmd_node->pioctl_buf);
		pcmd_node = pcmd_node->pnext;
	}
#ifdef STA_SUPPORT
	wlan_check_scan_queue(pmadapter);
#endif
	wlan_release_cmd_lock(pmadapter);
	LEAVE();
	return;
}

#define REASON_CODE_NO_CMD_NODE     1
#define REASON_CODE_CMD_TIMEOUT     2
#define REASON_CODE_CMD_TO_CARD_FAILURE 3
#define REASON_CODE_EXT_SCAN_TIMEOUT    4
/**
 *  @brief This function dump debug info
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *  @param reason        Reason code
 *
 *  @return     N/A
 */
t_void
wlan_dump_info(mlan_adapter *pmadapter, t_u8 reason)
{
	cmd_ctrl_node *pcmd_node = MNULL;
#ifdef DEBUG_LEVEL1
	t_u32 sec = 0, usec = 0;
#endif
	t_u8 i;
#ifdef SDIO_MULTI_PORT_TX_AGGR
	t_u8 j;
	t_u8 mp_aggr_pkt_limit = SDIO_MP_AGGR_DEF_PKT_LIMIT;
#endif
	t_u16 cmd_id, cmd_act;
	mlan_private *pmpriv = MNULL;

	ENTER();

	PRINTM(MERROR, "------------Dump info-----------\n", reason);
	switch (reason) {
	case REASON_CODE_NO_CMD_NODE:
		pmadapter->dbg.num_no_cmd_node++;
		PRINTM(MERROR, "No Free command node\n");
		break;
	case REASON_CODE_CMD_TIMEOUT:
		PRINTM(MERROR, "Commmand Timeout\n");
		break;
	case REASON_CODE_CMD_TO_CARD_FAILURE:
		PRINTM(MERROR, "Command to card failure\n");
		break;
	case REASON_CODE_EXT_SCAN_TIMEOUT:
		PRINTM(MERROR, "EXT_SCAN_STATUS event Timeout\n");
		break;
	default:
		break;
	}
	if ((reason == REASON_CODE_NO_CMD_NODE) &&
	    (pmadapter->dbg.num_no_cmd_node > 1)) {
		if (pmadapter->dbg.num_no_cmd_node >= 5)
			wlan_recv_event(wlan_get_priv
					(pmadapter, MLAN_BSS_ROLE_ANY),
					MLAN_EVENT_ID_DRV_DBG_DUMP, MNULL);
		LEAVE();
		return;
	}
	wlan_dump_pending_commands(pmadapter);
	if (reason != REASON_CODE_CMD_TIMEOUT) {
		if (!pmadapter->curr_cmd) {
			PRINTM(MERROR, "CurCmd Empty\n");
		} else {
			pcmd_node = pmadapter->curr_cmd;
			cmd_id = pmadapter->dbg.last_cmd_id[pmadapter->dbg.
							    last_cmd_index];
			cmd_act =
				pmadapter->dbg.last_cmd_act[pmadapter->dbg.
							    last_cmd_index];
			PRINTM_GET_SYS_TIME(MERROR, &sec, &usec);
			PRINTM(MERROR,
			       "Current cmd id (%lu.%06lu) = 0x%x, act = 0x%x\n",
			       sec, usec, cmd_id, cmd_act);
			if (pcmd_node->cmdbuf) {
				t_u8 *pcmd_buf;
				pcmd_buf =
					pcmd_node->cmdbuf->pbuf +
					pcmd_node->cmdbuf->data_offset +
					INTF_HEADER_LEN;
				for (i = 0; i < 16; i++)
					PRINTM(MERROR, "%02x ", *pcmd_buf++);
				PRINTM(MERROR, "\n");
			}
			pmpriv = pcmd_node->priv;
			if (pmpriv)
				PRINTM(MERROR, "BSS type = %d BSS role= %d\n",
				       pmpriv->bss_type, pmpriv->bss_role);
		}
	}
	PRINTM(MERROR, "mlan_processing =%d\n", pmadapter->mlan_processing);
	PRINTM(MERROR, "main_lock_flag =%d\n", pmadapter->main_lock_flag);
	PRINTM(MERROR, "main_process_cnt =%d\n", pmadapter->main_process_cnt);
	PRINTM(MERROR, "delay_task_flag =%d\n", pmadapter->delay_task_flag);
	PRINTM(MERROR, "mlan_rx_processing =%d\n",
	       pmadapter->mlan_rx_processing);
	PRINTM(MERROR, "rx_pkts_queued=%d\n", pmadapter->rx_pkts_queued);
	PRINTM(MERROR, "more_task_flag = %d\n", pmadapter->more_task_flag);
	PRINTM(MERROR, "num_cmd_timeout = %d\n", pmadapter->num_cmd_timeout);
	PRINTM(MERROR, "dbg.num_cmd_timeout = %d\n",
	       pmadapter->dbg.num_cmd_timeout);
	PRINTM(MERROR, "last_cmd_index = %d\n", pmadapter->dbg.last_cmd_index);
	PRINTM(MERROR, "last_cmd_id = ");
	for (i = 0; i < DBG_CMD_NUM; i++)
		PRINTM(MERROR, "0x%x ", pmadapter->dbg.last_cmd_id[i]);
	PRINTM(MERROR, "\n");
	PRINTM(MERROR, "last_cmd_act = ");
	for (i = 0; i < DBG_CMD_NUM; i++)
		PRINTM(MERROR, "0x%x ", pmadapter->dbg.last_cmd_act[i]);
	PRINTM(MERROR, "\n");
	PRINTM(MERROR, "last_cmd_resp_index = %d\n",
	       pmadapter->dbg.last_cmd_resp_index);
	PRINTM(MERROR, "last_cmd_resp_id = ");
	for (i = 0; i < DBG_CMD_NUM; i++)
		PRINTM(MERROR, "0x%x ", pmadapter->dbg.last_cmd_resp_id[i]);
	PRINTM(MERROR, "\n");
	PRINTM(MERROR, "last_event_index = %d\n",
	       pmadapter->dbg.last_event_index);
	PRINTM(MERROR, "last_event = ");
	for (i = 0; i < DBG_CMD_NUM; i++)
		PRINTM(MERROR, "0x%x ", pmadapter->dbg.last_event[i]);
	PRINTM(MERROR, "\n");

	PRINTM(MERROR, "num_data_h2c_failure = %d\n",
	       pmadapter->dbg.num_tx_host_to_card_failure);
	PRINTM(MERROR, "num_cmd_h2c_failure = %d\n",
	       pmadapter->dbg.num_cmd_host_to_card_failure);
	PRINTM(MERROR, "num_data_c2h_failure = %d\n",
	       pmadapter->dbg.num_rx_card_to_host_failure);
	PRINTM(MERROR, "num_cmdevt_c2h_failure = %d\n",
	       pmadapter->dbg.num_cmdevt_card_to_host_failure);
	PRINTM(MERROR, "num_int_read_failure = %d\n",
	       pmadapter->dbg.num_int_read_failure);
	PRINTM(MERROR, "last_int_status = %d\n",
	       pmadapter->dbg.last_int_status);
	PRINTM(MERROR, "num_alloc_buffer_failure = %d\n",
	       pmadapter->dbg.num_alloc_buffer_failure);
	PRINTM(MERROR, "num_pkt_dropped = %d\n",
	       pmadapter->dbg.num_pkt_dropped);
	PRINTM(MERROR, "num_no_cmd_node = %d\n",
	       pmadapter->dbg.num_no_cmd_node);
	PRINTM(MERROR, "num_event_deauth = %d\n",
	       pmadapter->dbg.num_event_deauth);
	PRINTM(MERROR, "num_event_disassoc = %d\n",
	       pmadapter->dbg.num_event_disassoc);
	PRINTM(MERROR, "num_event_link_lost = %d\n",
	       pmadapter->dbg.num_event_link_lost);
	PRINTM(MERROR, "num_cmd_deauth = %d\n", pmadapter->dbg.num_cmd_deauth);
	PRINTM(MERROR, "num_cmd_assoc_success = %d\n",
	       pmadapter->dbg.num_cmd_assoc_success);
	PRINTM(MERROR, "num_cmd_assoc_failure = %d\n",
	       pmadapter->dbg.num_cmd_assoc_failure);
	PRINTM(MERROR, "cmd_resp_received=%d\n", pmadapter->cmd_resp_received);
	PRINTM(MERROR, "event_received=%d\n", pmadapter->event_received);

	PRINTM(MERROR, "max_tx_buf_size=%d\n", pmadapter->max_tx_buf_size);
	PRINTM(MERROR, "tx_buf_size=%d\n", pmadapter->tx_buf_size);
	PRINTM(MERROR, "curr_tx_buf_size=%d\n", pmadapter->curr_tx_buf_size);

	PRINTM(MERROR, "data_sent=%d cmd_sent=%d\n", pmadapter->data_sent,
	       pmadapter->cmd_sent);

	PRINTM(MERROR, "ps_mode=%d ps_state=%d\n", pmadapter->ps_mode,
	       pmadapter->ps_state);
	PRINTM(MERROR, "wakeup_dev_req=%d wakeup_tries=%d\n",
	       pmadapter->pm_wakeup_card_req, pmadapter->pm_wakeup_fw_try);
	PRINTM(MERROR, "hs_configured=%d hs_activated=%d\n",
	       pmadapter->is_hs_configured, pmadapter->hs_activated);
	PRINTM(MERROR, "pps_uapsd_mode=%d sleep_pd=%d\n",
	       pmadapter->pps_uapsd_mode, pmadapter->sleep_period.period);
	PRINTM(MERROR, "tx_lock_flag = %d\n", pmadapter->tx_lock_flag);
	PRINTM(MERROR, "scan_processing = %d\n", pmadapter->scan_processing);
	PRINTM(MERROR, "mp_rd_bitmap=0x%x curr_rd_port=0x%x\n",
	       pmadapter->mp_rd_bitmap, pmadapter->curr_rd_port);
	PRINTM(MERROR, "mp_wr_bitmap=0x%x curr_wr_port=0x%x\n",
	       pmadapter->mp_wr_bitmap, pmadapter->curr_wr_port);
	PRINTM(MERROR, "mp_invalid_update=%d\n", pmadapter->mp_invalid_update);
#ifdef SDIO_MULTI_PORT_TX_AGGR
	PRINTM(MERROR, "last_recv_wr_bitmap=0x%x last_mp_index=%d\n",
	       pmadapter->last_recv_wr_bitmap, pmadapter->last_mp_index);
	for (i = 0; i < SDIO_MP_DBG_NUM; i++) {
		PRINTM(MERROR,
		       "mp_wr_bitmap: 0x%x mp_wr_ports=0x%x len=%d curr_wr_port=0x%x\n",
		       pmadapter->last_mp_wr_bitmap[i],
		       pmadapter->last_mp_wr_ports[i],
		       pmadapter->last_mp_wr_len[i],
		       pmadapter->last_curr_wr_port[i]);
		for (j = 0; j < mp_aggr_pkt_limit; j++) {
			PRINTM(MERROR, "0x%02x ",
			       pmadapter->last_mp_wr_info[i *
							  mp_aggr_pkt_limit +
							  j]);
		}
		PRINTM(MERROR, "\n");
	}
#endif
	for (i = 0; i < pmadapter->priv_num; ++i) {
		if (pmadapter->priv[i])
			wlan_dump_ralist(pmadapter->priv[i]);
	}
	if (reason != REASON_CODE_CMD_TIMEOUT) {
		if ((pmadapter->dbg.num_no_cmd_node >= 5)
		    || (pmadapter->pm_wakeup_card_req &&
			pmadapter->pm_wakeup_fw_try)
		    || (reason == REASON_CODE_EXT_SCAN_TIMEOUT)
			) {
			if (pmpriv)
				wlan_recv_event(pmpriv,
						MLAN_EVENT_ID_DRV_DBG_DUMP,
						MNULL);
			else
				wlan_recv_event(wlan_get_priv
						(pmadapter, MLAN_BSS_ROLE_ANY),
						MLAN_EVENT_ID_DRV_DBG_DUMP,
						MNULL);
		}
	}
	PRINTM(MERROR, "-------- Dump info End---------\n", reason);
	LEAVE();
	return;
}

/**
 *  @brief This function convert a given character to hex
 *
 *  @param chr        Character to be converted
 *
 *  @return           The converted hex if chr is a valid hex, else 0
 */
static t_u32
wlan_hexval(t_u8 chr)
{
	if (chr >= '0' && chr <= '9')
		return chr - '0';
	if (chr >= 'A' && chr <= 'F')
		return chr - 'A' + 10;
	if (chr >= 'a' && chr <= 'f')
		return chr - 'a' + 10;

	return 0;
}

/**
 *  @brief This function convert a given string to hex
 *
 *  @param a            A pointer to string to be converted
 *
 *  @return             The converted hex value if param a is a valid hex, else 0
 */
int
wlan_atox(t_u8 *a)
{
	int i = 0;

	ENTER();

	while (wlan_isxdigit(*a))
		i = i * 16 + wlan_hexval(*a++);

	LEAVE();
	return i;
}

/**
 *  @brief This function parse cal data from ASCII to hex
 *
 *  @param src          A pointer to source data
 *  @param len          Source data length
 *  @param dst          A pointer to a buf to store the parsed data
 *
 *  @return             The parsed hex data length
 */
static t_u32
wlan_parse_cal_cfg(t_u8 *src, t_size len, t_u8 *dst)
{
	t_u8 *ptr;
	t_u8 *dptr;

	ENTER();
	ptr = src;
	dptr = dst;

	while (ptr - src < len) {
		if (*ptr && (wlan_isspace(*ptr) || *ptr == '\t')) {
			ptr++;
			continue;
		}

		if (wlan_isxdigit(*ptr)) {
			*dptr++ = wlan_atox(ptr);
			ptr += 2;
		} else {
			ptr++;
		}
	}
	LEAVE();
	return dptr - dst;
}

/**
 *  @brief This function finds first occurrence of a char in a string
 *
 *  @param s            A pointer to the string to be searched
 *  @param c            The character to search for
 *
 *  @return             Location of the first occurrence of the char
 *                      if found, else NULL
 */
t_u8 *
wlan_strchr(t_u8 *s, int c)
{
	t_u8 *pos = s;
	while (*pos != '\0') {
		if (*pos == (t_u8)c)
			return pos;
		pos++;
	}
	return MNULL;
}

/**
 *    @brief WOAL parse ASCII format raw data to hex format
 *
 *    @param pmpriv       MOAL handle
 *    @param data         Source data
 *    @param size         data length
 *    @return             MLAN_STATUS_SUCCESS--success, otherwise--fail
 */
static t_u32
wlan_process_hostcmd_cfg(IN pmlan_private pmpriv, t_u8 *data, t_size size)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u8 *pos = data;
	t_u8 *intf_s, *intf_e;
	t_u8 *buf = MNULL;
	t_u8 *ptr = MNULL;
	t_u32 cmd_len = 0;
	t_u8 start_raw = MFALSE;
	mlan_ds_misc_cmd *hostcmd;
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;

	ENTER();
	ret = pcb->moal_malloc(pmadapter->pmoal_handle,
			       sizeof(mlan_ds_misc_cmd), MLAN_MEM_DEF,
			       (t_u8 **)&hostcmd);
	if (ret || !hostcmd) {
		PRINTM(MERROR, "Could not allocate buffer space!\n");
		LEAVE();
		return ret;
	}
	buf = hostcmd->cmd;
	ptr = buf;
	while ((pos - data) < size) {
		while (*pos == ' ' || *pos == '\t')
			pos++;
		if (*pos == '#') {	/* Line comment */
			while (*pos != '\n')
				pos++;
			pos++;
		}
		if ((*pos == '\r' && *(pos + 1) == '\n') ||
		    *pos == '\n' || *pos == '\0') {
			pos++;
			continue;	/* Needn't process this line */
		}

		if (*pos == '}') {
			cmd_len = *((t_u16 *)(buf + sizeof(t_u16)));
			hostcmd->len = cmd_len;
			ret = wlan_prepare_cmd(pmpriv, 0, 0, 0, MNULL,
					       (t_void *)hostcmd);
			memset(pmadapter, buf, 0, MLAN_SIZE_OF_CMD_BUFFER);
			ptr = buf;
			start_raw = MFALSE;
			pos++;
			continue;
		}

		if (start_raw == MFALSE) {
			intf_s = wlan_strchr(pos, '=');
			if (intf_s)
				intf_e = wlan_strchr(intf_s, '{');
			else
				intf_e = MNULL;

			if (intf_s && intf_e) {
				start_raw = MTRUE;
				pos = intf_e + 1;
				continue;
			}
		}

		if (start_raw) {
			/* Raw data block exists */
			while (*pos != '\n') {
				if ((*pos <= 'f' && *pos >= 'a') ||
				    (*pos <= 'F' && *pos >= 'A') ||
				    (*pos <= '9' && *pos >= '0')) {
					*ptr++ = wlan_atox(pos);
					pos += 2;
				} else
					pos++;
			}
		}
	}
	pcb->moal_mfree(pmadapter->pmoal_handle, (t_u8 *)hostcmd);
	LEAVE();
	return ret;
}

/**
 *  @brief This function initializes the command node.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *  @param cmd_oid      Cmd oid: treated as sub command
 *  @param pioctl_buf   A pointer to MLAN IOCTL Request buffer
 *  @param pdata_buf    A pointer to information buffer
 *
 *  @return             N/A
 */
static void
wlan_init_cmd_node(IN pmlan_private pmpriv,
		   IN cmd_ctrl_node *pcmd_node,
		   IN t_u32 cmd_oid,
		   IN t_void *pioctl_buf, IN t_void *pdata_buf)
{
	mlan_adapter *pmadapter = pmpriv->adapter;

	ENTER();

	if (pcmd_node == MNULL) {
		LEAVE();
		return;
	}
	pcmd_node->priv = pmpriv;
	pcmd_node->cmd_oid = cmd_oid;
	pcmd_node->pioctl_buf = pioctl_buf;
	pcmd_node->pdata_buf = pdata_buf;

	pcmd_node->cmdbuf = pcmd_node->pmbuf;

	/* Make sure head_ptr for cmd buf is Align */
	pcmd_node->cmdbuf->data_offset = 0;
	memset(pmadapter, pcmd_node->cmdbuf->pbuf, 0,
	       MRVDRV_SIZE_OF_CMD_BUFFER);

	/* Prepare mlan_buffer for command sending */
	pcmd_node->cmdbuf->buf_type = MLAN_BUF_TYPE_CMD;
	pcmd_node->cmdbuf->data_offset += INTF_HEADER_LEN;

	LEAVE();
}

/**
 *  @brief This function gets a free command node if available in
 *              command free queue.
 *
 *  @param pmadapter        A pointer to mlan_adapter structure
 *
 *  @return cmd_ctrl_node   A pointer to cmd_ctrl_node structure or MNULL
 */
static cmd_ctrl_node *
wlan_get_cmd_node(mlan_adapter *pmadapter)
{
	cmd_ctrl_node *pcmd_node;

	ENTER();

	if (pmadapter == MNULL) {
		LEAVE();
		return MNULL;
	}
	wlan_request_cmd_lock(pmadapter);
	if (util_peek_list(pmadapter->pmoal_handle, &pmadapter->cmd_free_q,
			   MNULL, MNULL)) {
		pcmd_node =
			(cmd_ctrl_node *)util_dequeue_list(pmadapter->
							   pmoal_handle,
							   &pmadapter->
							   cmd_free_q, MNULL,
							   MNULL);
	} else {
		PRINTM(MERROR,
		       "GET_CMD_NODE: cmd_ctrl_node is not available\n");
		pcmd_node = MNULL;
	}
	wlan_release_cmd_lock(pmadapter);
	LEAVE();
	return pcmd_node;
}

/**
 *  @brief This function cleans command node.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *
 *  @return             N/A
 */
static t_void
wlan_clean_cmd_node(pmlan_adapter pmadapter, cmd_ctrl_node *pcmd_node)
{
	ENTER();

	if (pcmd_node == MNULL) {
		LEAVE();
		return;
	}
	pcmd_node->cmd_oid = 0;
	pcmd_node->cmd_flag = 0;
	pcmd_node->pioctl_buf = MNULL;
	pcmd_node->pdata_buf = MNULL;

	if (pcmd_node->respbuf) {
		wlan_free_mlan_buffer(pmadapter, pcmd_node->respbuf);
		pcmd_node->respbuf = MNULL;
	}

	LEAVE();
	return;
}

#ifdef STA_SUPPORT
/**
 *  @brief This function will return the pointer to the first entry in
 *          pending cmd which is scan command
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *
 *  @return             A pointer to first entry match pioctl_req
 */
static cmd_ctrl_node *
wlan_get_pending_scan_cmd(pmlan_adapter pmadapter)
{
	cmd_ctrl_node *pcmd_node = MNULL;

	ENTER();

	pcmd_node =
		(cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						&pmadapter->cmd_pending_q,
						MNULL, MNULL);
	if (!pcmd_node) {
		LEAVE();
		return MNULL;
	}
	while (pcmd_node != (cmd_ctrl_node *)&pmadapter->cmd_pending_q) {
		if (pcmd_node->cmd_flag & CMD_F_SCAN) {
			LEAVE();
			return pcmd_node;
		}
		pcmd_node = pcmd_node->pnext;
	}
	LEAVE();
	return MNULL;
}
#endif

/**
 *  @brief This function will return the pointer to the first entry in
 *          pending cmd which matches the given pioctl_req
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *  @param pioctl_req   A pointer to mlan_ioctl_req buf
 *
 *  @return             A pointer to first entry match pioctl_req
 */
static cmd_ctrl_node *
wlan_get_pending_ioctl_cmd(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	cmd_ctrl_node *pcmd_node = MNULL;

	ENTER();

	pcmd_node =
		(cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						&pmadapter->cmd_pending_q,
						MNULL, MNULL);
	if (!pcmd_node) {
		LEAVE();
		return MNULL;
	}
	while (pcmd_node != (cmd_ctrl_node *)&pmadapter->cmd_pending_q) {
		if (pcmd_node->pioctl_buf &&
		    (pcmd_node->pioctl_buf == pioctl_req)) {
			LEAVE();
			return pcmd_node;
		}
		pcmd_node = pcmd_node->pnext;
	}
	LEAVE();
	return MNULL;
}

/**
 *  @brief This function will return the pointer to the first entry in
 *          pending cmd which matches the given bss_index
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *  @param bss_index    bss_index
 *
 *  @return             A pointer to first entry match pioctl_req
 */
static cmd_ctrl_node *
wlan_get_bss_pending_ioctl_cmd(pmlan_adapter pmadapter, t_u32 bss_index)
{
	cmd_ctrl_node *pcmd_node = MNULL;
	mlan_ioctl_req *pioctl_buf = MNULL;
	ENTER();

	pcmd_node =
		(cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						&pmadapter->cmd_pending_q,
						MNULL, MNULL);
	if (!pcmd_node) {
		LEAVE();
		return MNULL;
	}
	while (pcmd_node != (cmd_ctrl_node *)&pmadapter->cmd_pending_q) {
		if (pcmd_node->pioctl_buf) {
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
			if (pioctl_buf->bss_index == bss_index) {
				LEAVE();
				return pcmd_node;
			}
		}
		pcmd_node = pcmd_node->pnext;
	}
	LEAVE();
	return MNULL;
}

/**
 *  @brief This function handles the command response of host_cmd
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_ret_host_cmd(IN pmlan_private pmpriv,
		  IN HostCmd_DS_COMMAND *resp, IN mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *misc;
	t_u16 size = wlan_le16_to_cpu(resp->size);

	ENTER();

	PRINTM(MINFO, "host command response size = %d\n", size);
	size = MIN(size, MRVDRV_SIZE_OF_CMD_BUFFER);
	if (pioctl_buf) {
		misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc->param.hostcmd.len = size;
		memcpy(pmpriv->adapter, misc->param.hostcmd.cmd, (void *)resp,
		       size);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function sends host command to firmware.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_cmd_host_cmd(IN pmlan_private pmpriv,
		  IN HostCmd_DS_COMMAND *cmd, IN t_void *pdata_buf)
{
	mlan_ds_misc_cmd *pcmd_ptr = (mlan_ds_misc_cmd *)pdata_buf;

	ENTER();

	/* Copy the HOST command to command buffer */
	memcpy(pmpriv->adapter, (void *)cmd, pcmd_ptr->cmd,
	       MIN(MRVDRV_SIZE_OF_CMD_BUFFER, pcmd_ptr->len));
	PRINTM(MINFO, "Host command size = %d\n", pcmd_ptr->len);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function downloads a command to firmware.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_dnld_cmd_to_fw(IN mlan_private *pmpriv, IN cmd_ctrl_node *pcmd_node)
{

	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	HostCmd_DS_COMMAND *pcmd;
	mlan_ioctl_req *pioctl_buf = MNULL;
	t_u16 cmd_code;
	t_u16 cmd_size;
	t_u32 age_ts_usec;
#ifdef DEBUG_LEVEL1
	t_u32 sec = 0, usec = 0;
#endif

	ENTER();

	if (pcmd_node)
		if (pcmd_node->pioctl_buf != MNULL)
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
	if (!pmadapter || !pcmd_node) {
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_CMD_DNLD_FAIL;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	pcmd = (HostCmd_DS_COMMAND *)(pcmd_node->cmdbuf->pbuf +
				      pcmd_node->cmdbuf->data_offset);

	/* Sanity test */
	if (pcmd == MNULL || pcmd->size == 0) {
		PRINTM(MERROR,
		       "DNLD_CMD: pcmd is null or command size is zero, "
		       "Not sending\n");
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_CMD_DNLD_FAIL;
		wlan_request_cmd_lock(pmadapter);
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
		wlan_release_cmd_lock(pmadapter);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Set command sequence number */
	pmadapter->seq_num++;
	pcmd->seq_num =
		wlan_cpu_to_le16(HostCmd_SET_SEQ_NO_BSS_INFO
				 (pmadapter->seq_num, pcmd_node->priv->bss_num,
				  pcmd_node->priv->bss_type));
	cmd_code = wlan_le16_to_cpu(pcmd->command);
	cmd_size = wlan_le16_to_cpu(pcmd->size);

	pcmd_node->cmdbuf->data_len = cmd_size;

	wlan_request_cmd_lock(pmadapter);
	pmadapter->curr_cmd = pcmd_node;
	wlan_release_cmd_lock(pmadapter);

	/* Save the last command id and action to debug log */
	pmadapter->dbg.last_cmd_index =
		(pmadapter->dbg.last_cmd_index + 1) % DBG_CMD_NUM;
	pmadapter->dbg.last_cmd_id[pmadapter->dbg.last_cmd_index] = cmd_code;
	pmadapter->dbg.last_cmd_act[pmadapter->dbg.last_cmd_index] =
		wlan_le16_to_cpu(*(t_u16 *)((t_u8 *)pcmd + S_DS_GEN));
	pmadapter->callbacks.moal_get_system_time(pmadapter->pmoal_handle,
						  &pmadapter->dnld_cmd_in_secs,
						  &age_ts_usec);

	if (pcmd->command == HostCmd_CMD_HOST_CLOCK_CFG) {
		HostCmd_DS_HOST_CLOCK_CFG *host_clock =
			(HostCmd_DS_HOST_CLOCK_CFG *) & pcmd->params.
			host_clock_cfg;
		pmadapter->callbacks.moal_get_host_time_ns(&pmadapter->d1);
		PRINTM(MINFO, "WIFI_TS: d1: %llu\n", pmadapter->d1);
		/* Overwrite the time to be given to FW */
		host_clock->time = wlan_cpu_to_le64(pmadapter->d1);
	}

	PRINTM_GET_SYS_TIME(MCMND, &sec, &usec);
	PRINTM_NETINTF(MCMND, pmpriv);
	PRINTM(MCMND,
	       "DNLD_CMD (%lu.%06lu): 0x%x, act 0x%x, len %d, seqno 0x%x\n",
	       sec, usec, cmd_code,
	       wlan_le16_to_cpu(*(t_u16 *)((t_u8 *)pcmd + S_DS_GEN)), cmd_size,
	       wlan_le16_to_cpu(pcmd->seq_num));
	DBG_HEXDUMP(MCMD_D, "DNLD_CMD", (t_u8 *)pcmd, cmd_size);

	/* Send the command to lower layer */

	pcmd_node->cmdbuf->data_offset -= INTF_HEADER_LEN;
	pcmd_node->cmdbuf->data_len += INTF_HEADER_LEN;
	/* Extra header for SDIO is added here */
	ret = wlan_sdio_host_to_card(pmadapter, MLAN_TYPE_CMD,
				     pcmd_node->cmdbuf, MNULL);

	if (ret == MLAN_STATUS_FAILURE) {
		PRINTM(MERROR, "DNLD_CMD: Host to Card Failed\n");
		if (pcmd_node->pioctl_buf) {
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
			pioctl_buf->status_code = MLAN_ERROR_CMD_DNLD_FAIL;
		}

		wlan_request_cmd_lock(pmadapter);
		wlan_insert_cmd_to_free_q(pmadapter, pmadapter->curr_cmd);
		pmadapter->curr_cmd = MNULL;
		wlan_release_cmd_lock(pmadapter);
		if (pmadapter->dbg.last_cmd_index)
			pmadapter->dbg.last_cmd_index--;
		else
			pmadapter->dbg.last_cmd_index = DBG_CMD_NUM - 1;

		pmadapter->dbg.num_cmd_host_to_card_failure++;
		wlan_dump_info(pmadapter, REASON_CODE_CMD_TO_CARD_FAILURE);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Clear BSS_NO_BITS from HostCmd */
	cmd_code &= HostCmd_CMD_ID_MASK;

	/* For the command who has no command response, we should return here */
	if (cmd_code == HostCmd_CMD_FW_DUMP_EVENT
	    || cmd_code == HostCmd_CMD_SOFT_RESET) {
		if (pcmd_node->pioctl_buf) {
			PRINTM(MMSG,
			       "CMD(0x%x) has no cmd resp: free curr_cmd and do ioctl_complete\n",
			       cmd_code);
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
			wlan_request_cmd_lock(pmadapter);
			wlan_insert_cmd_to_free_q(pmadapter,
						  pmadapter->curr_cmd);
			pmadapter->curr_cmd = MNULL;
			wlan_release_cmd_lock(pmadapter);
		}
		goto done;
	}

	/* Setup the timer after transmit command */
	pcb->moal_start_timer(pmadapter->pmoal_handle,
			      pmadapter->pmlan_cmd_timer, MFALSE,
			      MRVDRV_TIMER_10S * 2);

	pmadapter->cmd_timer_is_set = MTRUE;

	ret = MLAN_STATUS_SUCCESS;

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function sends sleep confirm command to firmware.
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *
 *  @return           MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_dnld_sleep_confirm_cmd(mlan_adapter *pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	static t_u32 i;
	t_u16 cmd_len = 0;
	opt_sleep_confirm_buffer *sleep_cfm_buf =
		(opt_sleep_confirm_buffer *)(pmadapter->psleep_cfm->pbuf +
					     pmadapter->psleep_cfm->
					     data_offset);
	mlan_private *pmpriv = MNULL;

	ENTER();

	pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	if (!pmpriv) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	cmd_len = sizeof(OPT_Confirm_Sleep);
	pmadapter->seq_num++;
	sleep_cfm_buf->ps_cfm_sleep.seq_num =
		wlan_cpu_to_le16(HostCmd_SET_SEQ_NO_BSS_INFO
				 (pmadapter->seq_num, pmpriv->bss_num,
				  pmpriv->bss_type));
	DBG_HEXDUMP(MCMD_D, "SLEEP_CFM", &sleep_cfm_buf->ps_cfm_sleep,
		    sizeof(OPT_Confirm_Sleep));

	/* Send sleep confirm command to firmware */

	pmadapter->psleep_cfm->data_len = cmd_len + INTF_HEADER_LEN;
	ret = wlan_sdio_host_to_card(pmadapter, MLAN_TYPE_CMD,
				     pmadapter->psleep_cfm, MNULL);

	if (ret == MLAN_STATUS_FAILURE) {
		PRINTM(MERROR, "SLEEP_CFM: failed\n");
		pmadapter->dbg.num_cmd_sleep_cfm_host_to_card_failure++;
		goto done;
	} else {
		if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP)
			pmadapter->ps_state = PS_STATE_SLEEP_CFM;
#ifdef STA_SUPPORT
		if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA) {
			if (!sleep_cfm_buf->ps_cfm_sleep.sleep_cfm.resp_ctrl) {
				/* Response is not needed for sleep confirm command */
				pmadapter->ps_state = PS_STATE_SLEEP;
			} else {
				pmadapter->ps_state = PS_STATE_SLEEP_CFM;
			}

			if (!sleep_cfm_buf->ps_cfm_sleep.sleep_cfm.resp_ctrl
			    && (pmadapter->is_hs_configured &&
				!pmadapter->sleep_period.period)) {
				pmadapter->pm_wakeup_card_req = MTRUE;
				wlan_host_sleep_activated_event(wlan_get_priv
								(pmadapter,
								 MLAN_BSS_ROLE_STA),
								MTRUE);
			}
		}
#endif /* STA_SUPPORT */

#define NUM_SC_PER_LINE         16
		if (++i % NUM_SC_PER_LINE == 0)
			PRINTM(MEVENT, "+\n");
		else
			PRINTM(MEVENT, "+");
	}

done:
	LEAVE();
	return ret;
}

/********************************************************
			Global Functions
********************************************************/

/**
 *  @brief Event handler
 *
 *  @param priv     A pointer to mlan_private structure
 *  @param event_id Event ID
 *  @param pmevent  Event buffer
 *
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_recv_event(pmlan_private priv, mlan_event_id event_id, t_void *pmevent)
{
	pmlan_callbacks pcb = MNULL;

	ENTER();

	if (!priv) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pcb = &priv->adapter->callbacks;

	if (pmevent)
		/* The caller has provided the event. */
		pcb->moal_recv_event(priv->adapter->pmoal_handle,
				     (pmlan_event)pmevent);
	else {
		mlan_event mevent;

		memset(priv->adapter, &mevent, 0, sizeof(mlan_event));
		mevent.bss_index = priv->bss_index;
		mevent.event_id = event_id;
		mevent.event_len = 0;

		pcb->moal_recv_event(priv->adapter->pmoal_handle, &mevent);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function allocates the command buffer and links
 *          it to command free queue.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_alloc_cmd_buffer(IN mlan_adapter *pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	cmd_ctrl_node *pcmd_array = MNULL;
	t_u32 buf_size;
	t_u32 i;

	ENTER();

	/* Allocate and initialize cmd_ctrl_node */
	buf_size = sizeof(cmd_ctrl_node) * MRVDRV_NUM_OF_CMD_BUFFER;
	ret = pcb->moal_malloc(pmadapter->pmoal_handle, buf_size,
			       MLAN_MEM_DEF | MLAN_MEM_DMA,
			       (t_u8 **)&pcmd_array);
	if (ret != MLAN_STATUS_SUCCESS || !pcmd_array) {
		PRINTM(MERROR,
		       "ALLOC_CMD_BUF: Failed to allocate pcmd_array\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	pmadapter->cmd_pool = pcmd_array;
	memset(pmadapter, pmadapter->cmd_pool, 0, buf_size);

	/* Allocate and initialize command buffers */
	for (i = 0; i < MRVDRV_NUM_OF_CMD_BUFFER; i++) {
		pcmd_array[i].pmbuf = wlan_alloc_mlan_buffer(pmadapter,
							     MRVDRV_SIZE_OF_CMD_BUFFER,
							     0,
							     MOAL_MALLOC_BUFFER);
		if (!pcmd_array[i].pmbuf) {
			PRINTM(MERROR,
			       "ALLOC_CMD_BUF: Failed to allocate command buffer\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
	wlan_request_cmd_lock(pmadapter);
	for (i = 0; i < MRVDRV_NUM_OF_CMD_BUFFER; i++)
		wlan_insert_cmd_to_free_q(pmadapter, &pcmd_array[i]);
	wlan_release_cmd_lock(pmadapter);
	ret = MLAN_STATUS_SUCCESS;
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function frees the command buffer.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_free_cmd_buffer(IN mlan_adapter *pmadapter)
{
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	cmd_ctrl_node *pcmd_array;
	t_u32 i;

	ENTER();

	/* Need to check if cmd pool is allocated or not */
	if (pmadapter->cmd_pool == MNULL) {
		PRINTM(MINFO, "FREE_CMD_BUF: cmd_pool is Null\n");
		goto done;
	}

	pcmd_array = pmadapter->cmd_pool;

	/* Release shared memory buffers */
	for (i = 0; i < MRVDRV_NUM_OF_CMD_BUFFER; i++) {
		if (pcmd_array[i].pmbuf) {
			PRINTM(MINFO, "Free all the command buffer.\n");
			wlan_free_mlan_buffer(pmadapter, pcmd_array[i].pmbuf);
			pcmd_array[i].pmbuf = MNULL;
		}
		if (pcmd_array[i].respbuf) {
			wlan_free_mlan_buffer(pmadapter, pcmd_array[i].respbuf);
			pcmd_array[i].respbuf = MNULL;
		}
	}
	/* Release cmd_ctrl_node */
	if (pmadapter->cmd_pool) {
		PRINTM(MINFO, "Free command pool.\n");
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->cmd_pool);
		pmadapter->cmd_pool = MNULL;
	}

done:
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles events generated by firmware
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_process_event(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private priv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	pmlan_buffer pmbuf = pmadapter->pmlan_buffer_event;
	t_u32 eventcause = pmadapter->event_cause;
#ifdef DEBUG_LEVEL1
	t_u32 in_ts_sec = 0, in_ts_usec = 0;
#endif
	ENTER();

	/* Save the last event to debug log */
	pmadapter->dbg.last_event_index =
		(pmadapter->dbg.last_event_index + 1) % DBG_CMD_NUM;
	pmadapter->dbg.last_event[pmadapter->dbg.last_event_index] =
		(t_u16)eventcause;

	if ((eventcause & EVENT_ID_MASK) == EVENT_RADAR_DETECTED) {
		if (wlan_11h_dfs_event_preprocessing(pmadapter) ==
		    MLAN_STATUS_SUCCESS) {
			memcpy(pmadapter, (t_u8 *)&eventcause,
			       pmbuf->pbuf + pmbuf->data_offset,
			       sizeof(eventcause));
		} else {
			PRINTM(MERROR, "Error processing DFS Event: 0x%x\n",
			       eventcause);
			goto done;
		}
	}
	/* Get BSS number and corresponding priv */
	priv = wlan_get_priv_by_id(pmadapter, EVENT_GET_BSS_NUM(eventcause),
				   EVENT_GET_BSS_TYPE(eventcause));
	if (!priv)
		priv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	if (!priv) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Clear BSS_NO_BITS from event */
	eventcause &= EVENT_ID_MASK;
	pmadapter->event_cause = eventcause;

	if (pmbuf) {
		pmbuf->bss_index = priv->bss_index;
		memcpy(pmadapter,
		       pmbuf->pbuf + pmbuf->data_offset,
		       (t_u8 *)&eventcause, sizeof(eventcause));
	}

	if (MTRUE
	    && (eventcause != EVENT_PS_SLEEP && eventcause != EVENT_PS_AWAKE)
		) {
		PRINTM_GET_SYS_TIME(MEVENT, &in_ts_sec, &in_ts_usec);
		PRINTM_NETINTF(MEVENT, priv);
		PRINTM(MEVENT, "%lu.%06lu : Event: 0x%x\n", in_ts_sec,
		       in_ts_usec, eventcause);
	}

	ret = priv->ops.process_event(priv);
done:
	pmadapter->event_cause = 0;
	pmadapter->pmlan_buffer_event = MNULL;
	if (pmbuf)
		wlan_free_mlan_buffer(pmadapter, pmbuf);

	LEAVE();
	return ret;
}

/**
 *  @brief This function requests a lock on command queue.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
t_void
wlan_request_cmd_lock(IN mlan_adapter *pmadapter)
{
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;

	ENTER();

	/* Call MOAL spin lock callback function */
	pcb->moal_spin_lock(pmadapter->pmoal_handle, pmadapter->pmlan_cmd_lock);

	LEAVE();
	return;
}

/**
 *  @brief This function releases a lock on command queue.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
t_void
wlan_release_cmd_lock(IN mlan_adapter *pmadapter)
{
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;

	ENTER();

	/* Call MOAL spin unlock callback function */
	pcb->moal_spin_unlock(pmadapter->pmoal_handle,
			      pmadapter->pmlan_cmd_lock);

	LEAVE();
	return;
}

/**
 *  @brief This function prepare the command before sending to firmware.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd_no       Command number
 *  @param cmd_action   Command action: GET or SET
 *  @param cmd_oid      Cmd oid: treated as sub command
 *  @param pioctl_buf   A pointer to MLAN IOCTL Request buffer
 *  @param pdata_buf    A pointer to information buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_prepare_cmd(IN mlan_private *pmpriv,
		 IN t_u16 cmd_no,
		 IN t_u16 cmd_action,
		 IN t_u32 cmd_oid, IN t_void *pioctl_buf, IN t_void *pdata_buf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_adapter *pmadapter = MNULL;
	cmd_ctrl_node *pcmd_node = MNULL;
	HostCmd_DS_COMMAND *cmd_ptr = MNULL;
	pmlan_ioctl_req pioctl_req = (mlan_ioctl_req *)pioctl_buf;

	ENTER();

	if (!pmpriv) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pmadapter = pmpriv->adapter;

	/* Sanity test */
	if (!pmadapter || pmadapter->surprise_removed) {
		PRINTM(MERROR, "PREP_CMD: Card is Removed\n");
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_FW_NOT_READY;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	if (pmadapter->hw_status == WlanHardwareStatusReset) {
		if ((cmd_no != HostCmd_CMD_FUNC_INIT)
			) {
			PRINTM(MERROR, "PREP_CMD: FW is in reset state\n");
			if (pioctl_req)
				pioctl_req->status_code =
					MLAN_ERROR_FW_NOT_READY;
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}

	/* Get a new command node */
	pcmd_node = wlan_get_cmd_node(pmadapter);

	if (pcmd_node == MNULL) {
		PRINTM(MERROR, "PREP_CMD: No free cmd node\n");
		wlan_dump_info(pmadapter, REASON_CODE_NO_CMD_NODE);
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_NO_MEM;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
    /** reset num no cmd node */
	pmadapter->dbg.num_no_cmd_node = 0;

	/* Initialize the command node */
	wlan_init_cmd_node(pmpriv, pcmd_node, cmd_oid, pioctl_buf, pdata_buf);

	if (pcmd_node->cmdbuf == MNULL) {
		PRINTM(MERROR, "PREP_CMD: No free cmd buf\n");
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_NO_MEM;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	cmd_ptr =
		(HostCmd_DS_COMMAND *)(pcmd_node->cmdbuf->pbuf +
				       pcmd_node->cmdbuf->data_offset);
	cmd_ptr->command = cmd_no;
	cmd_ptr->result = 0;

	/* Prepare command */
	if (cmd_no)
		ret = pmpriv->ops.prepare_cmd(pmpriv, cmd_no, cmd_action,
					      cmd_oid, pioctl_buf, pdata_buf,
					      cmd_ptr);
	else {
		ret = wlan_cmd_host_cmd(pmpriv, cmd_ptr, pdata_buf);
		pcmd_node->cmd_flag |= CMD_F_HOSTCMD;
	}

	/* Return error, since the command preparation failed */
	if (ret != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "PREP_CMD: Command 0x%x preparation failed\n",
		       cmd_no);
		pcmd_node->pioctl_buf = MNULL;
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_CMD_DNLD_FAIL;
		wlan_request_cmd_lock(pmadapter);
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
		wlan_release_cmd_lock(pmadapter);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	wlan_request_cmd_lock(pmadapter);
	/* Send command */
#ifdef STA_SUPPORT
	if (cmd_no == HostCmd_CMD_802_11_SCAN
	    || cmd_no == HostCmd_CMD_802_11_SCAN_EXT) {
		if (cmd_no == HostCmd_CMD_802_11_SCAN_EXT &&
		    pmadapter->ext_scan && pmadapter->ext_scan_enh
		    && pmadapter->ext_scan_type == EXT_SCAN_ENHANCE) {
			wlan_insert_cmd_to_pending_q(pmadapter, pcmd_node,
						     MFALSE);
		} else
			wlan_queue_scan_cmd(pmpriv, pcmd_node);
	} else {
#endif
		if ((cmd_no == HostCmd_CMD_802_11_HS_CFG_ENH) &&
		    (cmd_action == HostCmd_ACT_GEN_SET) &&
		    (pmadapter->hs_cfg.conditions == HOST_SLEEP_CFG_CANCEL))
			wlan_insert_cmd_to_pending_q(pmadapter, pcmd_node,
						     MFALSE);
		else
			wlan_insert_cmd_to_pending_q(pmadapter, pcmd_node,
						     MTRUE);
#ifdef STA_SUPPORT
	}
#endif
	wlan_release_cmd_lock(pmadapter);
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function inserts command node to cmd_free_q
 *              after cleaning it.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *
 *  @return             N/A
 */
t_void
wlan_insert_cmd_to_free_q(IN mlan_adapter *pmadapter,
			  IN cmd_ctrl_node *pcmd_node)
{
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	mlan_ioctl_req *pioctl_req = MNULL;
	ENTER();

	if (pcmd_node == MNULL)
		goto done;
	if (pcmd_node->pioctl_buf) {
		pioctl_req = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
		if (pioctl_req->status_code != MLAN_ERROR_NO_ERROR)
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_req,
						 MLAN_STATUS_FAILURE);
		else
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_req,
						 MLAN_STATUS_SUCCESS);
	}
	/* Clean the node */
	wlan_clean_cmd_node(pmadapter, pcmd_node);

	/* Insert node into cmd_free_q */
	util_enqueue_list_tail(pmadapter->pmoal_handle, &pmadapter->cmd_free_q,
			       (pmlan_linked_list)pcmd_node, MNULL, MNULL);
done:
	LEAVE();
}

/**
 *  @brief This function queues the command to cmd list.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *  @param add_tail      Specify if the cmd needs to be queued in the header or tail
 *
 *  @return             N/A
 */
t_void
wlan_insert_cmd_to_pending_q(IN mlan_adapter *pmadapter,
			     IN cmd_ctrl_node *pcmd_node, IN t_u32 add_tail)
{
	HostCmd_DS_COMMAND *pcmd = MNULL;
	t_u16 command;

	ENTER();

	if (pcmd_node == MNULL) {
		PRINTM(MERROR, "QUEUE_CMD: pcmd_node is MNULL\n");
		goto done;
	}

	pcmd = (HostCmd_DS_COMMAND *)(pcmd_node->cmdbuf->pbuf +
				      pcmd_node->cmdbuf->data_offset);

	command = wlan_le16_to_cpu(pcmd->command);

	/* Exit_PS command needs to be queued in the header always. */
	if (command == HostCmd_CMD_802_11_PS_MODE_ENH) {
		HostCmd_DS_802_11_PS_MODE_ENH *pm = &pcmd->params.psmode_enh;
		if (wlan_le16_to_cpu(pm->action) == DIS_AUTO_PS) {
			if (pmadapter->ps_state != PS_STATE_AWAKE)
				add_tail = MFALSE;
		}
	}

	if (add_tail) {
		util_enqueue_list_tail(pmadapter->pmoal_handle,
				       &pmadapter->cmd_pending_q,
				       (pmlan_linked_list)pcmd_node,
				       MNULL, MNULL);
	} else {
		util_enqueue_list_head(pmadapter->pmoal_handle,
				       &pmadapter->cmd_pending_q,
				       (pmlan_linked_list)pcmd_node,
				       MNULL, MNULL);
	}

	PRINTM_NETINTF(MCMND, pcmd_node->priv);
	PRINTM(MCMND, "QUEUE_CMD: cmd=0x%x is queued\n", command);

done:
	LEAVE();
	return;
}

/**
 *  @brief This function executes next command in command
 *      pending queue. It will put firmware back to PS mode
 *      if applicable.
 *
 *  @param pmadapter     A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_exec_next_cmd(mlan_adapter *pmadapter)
{
	mlan_private *priv = MNULL;
	cmd_ctrl_node *pcmd_node = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	HostCmd_DS_COMMAND *pcmd;

	ENTER();

	/* Sanity test */
	if (pmadapter == MNULL) {
		PRINTM(MERROR, "EXEC_NEXT_CMD: pmadapter is MNULL\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	/* Check if already in processing */
	if (pmadapter->curr_cmd) {
		PRINTM(MERROR,
		       "EXEC_NEXT_CMD: there is command in processing!\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	wlan_request_cmd_lock(pmadapter);
	/* Check if any command is pending */
	pcmd_node =
		(cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						&pmadapter->cmd_pending_q,
						MNULL, MNULL);

	if (pcmd_node) {
		pcmd = (HostCmd_DS_COMMAND *)(pcmd_node->cmdbuf->pbuf +
					      pcmd_node->cmdbuf->data_offset);
		priv = pcmd_node->priv;

		if (pmadapter->ps_state != PS_STATE_AWAKE) {
			PRINTM(MERROR,
			       "Cannot send command in sleep state, this should not happen\n");
			wlan_release_cmd_lock(pmadapter);
			goto done;
		}

		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->cmd_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		wlan_release_cmd_lock(pmadapter);
		ret = wlan_dnld_cmd_to_fw(priv, pcmd_node);
		priv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
		/* Any command sent to the firmware when host is in sleep mode, should de-configure host sleep */
		/* We should skip the host sleep configuration command itself though */
		if (priv &&
		    (pcmd->command !=
		     wlan_cpu_to_le16(HostCmd_CMD_802_11_HS_CFG_ENH))) {
			if (pmadapter->hs_activated == MTRUE) {
				pmadapter->is_hs_configured = MFALSE;
				wlan_host_sleep_activated_event(priv, MFALSE);
			}
		}
		goto done;
	} else {
		wlan_release_cmd_lock(pmadapter);
	}
	ret = MLAN_STATUS_SUCCESS;
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function handles the command response
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_process_cmdresp(mlan_adapter *pmadapter)
{
	HostCmd_DS_COMMAND *resp = MNULL;
	mlan_private *pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	mlan_private *pmpriv_next = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 orig_cmdresp_no;
	t_u16 cmdresp_no;
	t_u16 cmdresp_result;
	mlan_ioctl_req *pioctl_buf = MNULL;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
#ifdef DEBUG_LEVEL1
	t_u32 sec = 0, usec = 0;
#endif
	t_u32 i;

	ENTER();

	/* Now we got response from FW, cancel the command timer */
	if (pmadapter->cmd_timer_is_set) {
		/* Cancel command timeout timer */
		pcb->moal_stop_timer(pmadapter->pmoal_handle,
				     pmadapter->pmlan_cmd_timer);
		/* Cancel command timeout timer */
		pmadapter->cmd_timer_is_set = MFALSE;
	}

	if (pmadapter->curr_cmd)
		if (pmadapter->curr_cmd->pioctl_buf != MNULL) {
			pioctl_buf =
				(mlan_ioctl_req *)pmadapter->curr_cmd->
				pioctl_buf;
		}

	if (!pmadapter->curr_cmd || !pmadapter->curr_cmd->respbuf) {
		resp = (HostCmd_DS_COMMAND *)pmadapter->upld_buf;
		resp->command = wlan_le16_to_cpu(resp->command);
		PRINTM(MERROR, "CMD_RESP: No curr_cmd, 0x%x\n", resp->command);
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_CMD_RESP_FAIL;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	pmadapter->num_cmd_timeout = 0;

	DBG_HEXDUMP(MCMD_D, "CMD_RESP",
		    pmadapter->curr_cmd->respbuf->pbuf +
		    pmadapter->curr_cmd->respbuf->data_offset,
		    pmadapter->curr_cmd->respbuf->data_len);

	resp = (HostCmd_DS_COMMAND *)(pmadapter->curr_cmd->respbuf->pbuf +
				      pmadapter->curr_cmd->respbuf->
				      data_offset);
	wlan_request_cmd_lock(pmadapter);
	if (pmadapter->curr_cmd->cmd_flag & CMD_F_CANCELED) {
		cmd_ctrl_node *free_cmd = pmadapter->curr_cmd;
		pmadapter->curr_cmd = MNULL;
		PRINTM(MCMND, "CMD_RESP: 0x%x been canceled!\n",
		       wlan_le16_to_cpu(resp->command));
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
		wlan_insert_cmd_to_free_q(pmadapter, free_cmd);
		wlan_release_cmd_lock(pmadapter);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	} else {
		wlan_release_cmd_lock(pmadapter);
	}
	if (pmadapter->curr_cmd->cmd_flag & CMD_F_HOSTCMD) {
		/* Copy original response back to response buffer */
		wlan_ret_host_cmd(pmpriv, resp, pioctl_buf);
	}
	orig_cmdresp_no = wlan_le16_to_cpu(resp->command);
	resp->size = wlan_le16_to_cpu(resp->size);
	resp->seq_num = wlan_le16_to_cpu(resp->seq_num);
	resp->result = wlan_le16_to_cpu(resp->result);

	/* Get BSS number and corresponding priv */
	pmpriv = wlan_get_priv_by_id(pmadapter,
				     HostCmd_GET_BSS_NO(resp->seq_num),
				     HostCmd_GET_BSS_TYPE(resp->seq_num));
	if (!pmpriv)
		pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	/* Clear RET_BIT from HostCmd */
	resp->command = (orig_cmdresp_no & HostCmd_CMD_ID_MASK);
	cmdresp_no = resp->command;

	cmdresp_result = resp->result;

	if (resp->command == HostCmd_CMD_HOST_CLOCK_CFG) {
		/* d2 needs to be fast - is there a way to avoid callback? */
		if (pmpriv) {
			pcb->moal_get_host_time_ns(&pmadapter->d2);
			PRINTM(MINFO,
			       "WIFI_TS: RTT for Host_CLOCK_CFG= %d ns\n",
			       pmadapter->d2 - pmadapter->d1);
		}
	}

	/* Save the last command response to debug log */
	pmadapter->dbg.last_cmd_resp_index =
		(pmadapter->dbg.last_cmd_resp_index + 1) % DBG_CMD_NUM;
	pmadapter->dbg.last_cmd_resp_id[pmadapter->dbg.last_cmd_resp_index] =
		orig_cmdresp_no;

	PRINTM_GET_SYS_TIME(MCMND, &sec, &usec);
	PRINTM_NETINTF(MCMND, pmadapter->curr_cmd->priv);
	PRINTM(MCMND,
	       "CMD_RESP (%lu.%06lu): 0x%x, result %d, len %d, seqno 0x%x\n",
	       sec, usec, orig_cmdresp_no, cmdresp_result, resp->size,
	       resp->seq_num);

	if (!(orig_cmdresp_no & HostCmd_RET_BIT)) {
		PRINTM(MERROR, "CMD_RESP: Invalid response to command!\n");
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_FW_CMDRESP;
		wlan_request_cmd_lock(pmadapter);
		wlan_insert_cmd_to_free_q(pmadapter, pmadapter->curr_cmd);
		pmadapter->curr_cmd = MNULL;
		wlan_release_cmd_lock(pmadapter);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	if (pmadapter->curr_cmd->cmd_flag & CMD_F_HOSTCMD) {
		pmadapter->curr_cmd->cmd_flag &= ~CMD_F_HOSTCMD;
		if ((cmdresp_result == HostCmd_RESULT_OK)
		    && (cmdresp_no == HostCmd_CMD_802_11_HS_CFG_ENH))
			ret = wlan_ret_802_11_hs_cfg(pmpriv, resp, pioctl_buf);
	} else {
		/* handle response */
		ret = pmpriv->ops.process_cmdresp(pmpriv, cmdresp_no, resp,
						  pioctl_buf);
	}

	/* Check init command response */
	if (pmadapter->hw_status == WlanHardwareStatusInitializing ||
	    pmadapter->hw_status == WlanHardwareStatusGetHwSpec) {
		if (ret == MLAN_STATUS_FAILURE) {
#if defined(STA_SUPPORT)
			if (pmadapter->pwarm_reset_ioctl_req) {
				/* warm reset failure */
				pmadapter->pwarm_reset_ioctl_req->status_code =
					MLAN_ERROR_CMD_RESP_FAIL;
				pcb->moal_ioctl_complete(pmadapter->
							 pmoal_handle,
							 pmadapter->
							 pwarm_reset_ioctl_req,
							 MLAN_STATUS_FAILURE);
				pmadapter->pwarm_reset_ioctl_req = MNULL;
				goto done;
			}
#endif
			PRINTM(MERROR,
			       "cmd 0x%02x failed during initialization\n",
			       cmdresp_no);
			wlan_init_fw_complete(pmadapter);
			goto done;
		}
	}

	wlan_request_cmd_lock(pmadapter);
	if (pmadapter->curr_cmd) {
		cmd_ctrl_node *free_cmd = pmadapter->curr_cmd;
		pioctl_buf = (mlan_ioctl_req *)pmadapter->curr_cmd->pioctl_buf;
		pmadapter->curr_cmd = MNULL;
		if (pioctl_buf && (ret == MLAN_STATUS_SUCCESS))
			pioctl_buf->status_code = MLAN_ERROR_NO_ERROR;
		else if (pioctl_buf && (ret == MLAN_STATUS_FAILURE) &&
			 !pioctl_buf->status_code)
			pioctl_buf->status_code = MLAN_ERROR_CMD_RESP_FAIL;

		/* Clean up and put current command back to cmd_free_q */
		wlan_insert_cmd_to_free_q(pmadapter, free_cmd);
	}
	wlan_release_cmd_lock(pmadapter);

	if ((pmadapter->hw_status == WlanHardwareStatusInitializing) &&
	    (pmadapter->last_init_cmd == cmdresp_no)) {
		i = pmpriv->bss_index + 1;
		while (i < pmadapter->priv_num &&
		       (!(pmpriv_next = pmadapter->priv[i])
			|| pmpriv_next->bss_virtual))
			i++;
		if (!pmpriv_next || i >= pmadapter->priv_num) {

#if defined(STA_SUPPORT)
			if (pmadapter->pwarm_reset_ioctl_req) {
				/* warm reset complete */
				pmadapter->hw_status = WlanHardwareStatusReady;
				pcb->moal_ioctl_complete(pmadapter->
							 pmoal_handle,
							 pmadapter->
							 pwarm_reset_ioctl_req,
							 MLAN_STATUS_SUCCESS);
				pmadapter->pwarm_reset_ioctl_req = MNULL;
				goto done;
			}
#endif
			pmadapter->hw_status = WlanHardwareStatusInitdone;
		} else {
			/* Issue init commands for the next interface */
			ret = pmpriv_next->ops.init_cmd(pmpriv_next, MFALSE);
		}
	} else if ((pmadapter->hw_status == WlanHardwareStatusGetHwSpec) &&
		   (HostCmd_CMD_GET_HW_SPEC == cmdresp_no)) {
		pmadapter->hw_status = WlanHardwareStatusGetHwSpecdone;
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function handles the timeout of command sending.
 *          It will re-send the same command again.
 *
 *  @param function_context   A pointer to function_context
 *  @return                   N/A
 */
t_void
wlan_cmd_timeout_func(t_void *function_context)
{
	mlan_adapter *pmadapter = (mlan_adapter *)function_context;
	cmd_ctrl_node *pcmd_node = MNULL;
	mlan_ioctl_req *pioctl_buf = MNULL;
#ifdef DEBUG_LEVEL1
	t_u32 sec = 0, usec = 0;
#endif
	t_u8 i;
	mlan_private *pmpriv = MNULL;

	ENTER();

	pmadapter->cmd_timer_is_set = MFALSE;
	if (!pmadapter->curr_cmd) {
		if (pmadapter->ext_scan && pmadapter->ext_scan_enh &&
		    pmadapter->scan_processing) {
			PRINTM(MMSG, "Ext scan enh timeout\n");
			pmadapter->ext_scan_timeout = MTRUE;
			wlan_dump_info(pmadapter, REASON_CODE_EXT_SCAN_TIMEOUT);
			goto exit;
		}
		PRINTM(MWARN, "CurCmd Empty\n");
		goto exit;
	}
	pmadapter->num_cmd_timeout++;
	pmadapter->dbg.num_cmd_timeout++;
	pcmd_node = pmadapter->curr_cmd;
	if (pcmd_node->pioctl_buf != MNULL) {
		pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
		pioctl_buf->status_code = MLAN_ERROR_CMD_TIMEOUT;
	}

	pmadapter->dbg.timeout_cmd_id =
		pmadapter->dbg.last_cmd_id[pmadapter->dbg.last_cmd_index];
	pmadapter->dbg.timeout_cmd_act =
		pmadapter->dbg.last_cmd_act[pmadapter->dbg.last_cmd_index];
	PRINTM_GET_SYS_TIME(MERROR, &sec, &usec);
	PRINTM(MERROR, "Timeout cmd id (%lu.%06lu) = 0x%x, act = 0x%x\n", sec,
	       usec, pmadapter->dbg.timeout_cmd_id,
	       pmadapter->dbg.timeout_cmd_act);
	if (pcmd_node->cmdbuf) {
		t_u8 *pcmd_buf;
		pcmd_buf =
			pcmd_node->cmdbuf->pbuf +
			pcmd_node->cmdbuf->data_offset + INTF_HEADER_LEN;
		for (i = 0; i < 16; i++)
			PRINTM(MERROR, "%02x ", *pcmd_buf++);
		PRINTM(MERROR, "\n");
	}

	pmpriv = pcmd_node->priv;
	if (pmpriv)
		PRINTM(MERROR, "BSS type = %d BSS role= %d\n", pmpriv->bss_type,
		       pmpriv->bss_role);
	wlan_dump_info(pmadapter, REASON_CODE_CMD_TIMEOUT);

	if (pmadapter->hw_status == WlanHardwareStatusInitializing ||
	    pmadapter->hw_status == WlanHardwareStatusGetHwSpec)
		wlan_init_fw_complete(pmadapter);
	else {
		/* Signal MOAL to perform extra handling for debugging */
		if (pmpriv) {
			wlan_recv_event(pmpriv, MLAN_EVENT_ID_DRV_DBG_DUMP,
					MNULL);
		} else {
			wlan_recv_event(wlan_get_priv
					(pmadapter, MLAN_BSS_ROLE_ANY),
					MLAN_EVENT_ID_DRV_DBG_DUMP, MNULL);
		}
	}

exit:
	LEAVE();
	return;
}

#ifdef STA_SUPPORT
/**
 *  @brief Internal function used to flush the scan pending queue
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
t_void
wlan_flush_scan_queue(IN pmlan_adapter pmadapter)
{

	cmd_ctrl_node *pcmd_node = MNULL;

	ENTER();

	wlan_request_cmd_lock(pmadapter);
	while ((pcmd_node =
		(cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						&pmadapter->scan_pending_q,
						MNULL, MNULL))) {
		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->scan_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		pcmd_node->pioctl_buf = MNULL;
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
	}

	pmadapter->scan_processing = MFALSE;
	wlan_release_cmd_lock(pmadapter);

	LEAVE();
}

/**
 *  @brief Cancel pending SCAN ioctl cmd.
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *  @param pioctl_req   A pointer to pmlan_ioctl_req
 *
 *  @return             MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING
 */
mlan_status
wlan_cancel_pending_scan_cmd(pmlan_adapter pmadapter,
			     pmlan_ioctl_req pioctl_req)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	cmd_ctrl_node *pcmd_node = MNULL;
	mlan_ioctl_req *pioctl_buf = MNULL;
	pmlan_private priv = MNULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	ENTER();

	PRINTM(MIOCTL, "Cancel scan command\n");
	wlan_request_cmd_lock(pmadapter);
	/* IOCTL will be completed, avoid calling IOCTL complete again from EVENT/CMDRESP */
	if (pmadapter->pscan_ioctl_req) {
		pioctl_buf = pmadapter->pscan_ioctl_req;
		priv = pmadapter->priv[pioctl_buf->bss_index];
		pmadapter->pscan_ioctl_req = MNULL;
		pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
		pcb->moal_ioctl_complete(pmadapter->pmoal_handle, pioctl_buf,
					 MLAN_STATUS_FAILURE);
	}

	if (pmadapter->curr_cmd && pmadapter->curr_cmd->pioctl_buf) {
		pioctl_buf = (mlan_ioctl_req *)pmadapter->curr_cmd->pioctl_buf;
		if (pioctl_buf->req_id == MLAN_IOCTL_SCAN) {
			PRINTM(MIOCTL, "wlan_cancel_scan: current command\n");
			pcmd_node = pmadapter->curr_cmd;
			pcmd_node->pioctl_buf = MNULL;
			pcmd_node->cmd_flag |= CMD_F_CANCELED;
			pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_buf,
						 MLAN_STATUS_FAILURE);
		}
	}
	while ((pcmd_node = wlan_get_pending_scan_cmd(pmadapter)) != MNULL) {
		PRINTM(MIOCTL,
		       "wlan_cancel_scan: find scan command in cmd_pending_q\n");
		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->cmd_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
	}
	wlan_release_cmd_lock(pmadapter);
	if (pmadapter->scan_processing &&
	    pmadapter->ext_scan_type == EXT_SCAN_ENHANCE) {
		if (priv) {
			wlan_prepare_cmd(priv, HostCmd_CMD_802_11_SCAN_EXT,
					 HostCmd_ACT_GEN_SET, 0, pioctl_req,
					 MNULL);
			wlan_recv_event(priv, MLAN_EVENT_ID_DRV_DEFER_HANDLING,
					MNULL);
			status = MLAN_STATUS_PENDING;
		}
	} else
		/* Cancel all pending scan command */
		wlan_flush_scan_queue(pmadapter);
	LEAVE();
	return status;
}
#endif

/**
 *  @brief Cancel all pending cmd.
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *
 *  @return             N/A
 */
t_void
wlan_cancel_all_pending_cmd(pmlan_adapter pmadapter)
{
	cmd_ctrl_node *pcmd_node = MNULL;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	mlan_ioctl_req *pioctl_buf = MNULL;
#ifdef STA_SUPPORT
	pmlan_private priv = MNULL;
#endif
	ENTER();
	/* Cancel current cmd */
	wlan_request_cmd_lock(pmadapter);
#ifdef STA_SUPPORT
	/* IOCTL will be completed, avoid calling IOCTL complete again from EVENT/CMDRESP */
	if (pmadapter->pscan_ioctl_req) {
		pioctl_buf = pmadapter->pscan_ioctl_req;
		priv = pmadapter->priv[pioctl_buf->bss_index];
		pmadapter->pscan_ioctl_req = MNULL;
		pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
		pcb->moal_ioctl_complete(pmadapter->pmoal_handle, pioctl_buf,
					 MLAN_STATUS_FAILURE);
	}
#endif
	if (pmadapter->curr_cmd) {
		pcmd_node = pmadapter->curr_cmd;
		pmadapter->curr_cmd = MNULL;
		if (pcmd_node->pioctl_buf) {
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
			pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_buf,
						 MLAN_STATUS_FAILURE);
			pcmd_node->pioctl_buf = MNULL;
		}
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
	}

	/* Cancel all pending command */
	while ((pcmd_node =
		(cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						&pmadapter->cmd_pending_q,
						MNULL, MNULL))) {
		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->cmd_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		if (pcmd_node->pioctl_buf) {
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
			pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_buf,
						 MLAN_STATUS_FAILURE);
			pcmd_node->pioctl_buf = MNULL;
		}
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
	}
	wlan_release_cmd_lock(pmadapter);
#ifdef STA_SUPPORT
	if (pmadapter->scan_processing &&
	    pmadapter->ext_scan_type == EXT_SCAN_ENHANCE) {
		if (priv) {
			wlan_prepare_cmd(priv, HostCmd_CMD_802_11_SCAN_EXT,
					 HostCmd_ACT_GEN_SET, 0, MNULL, MNULL);
			wlan_recv_event(priv, MLAN_EVENT_ID_DRV_DEFER_HANDLING,
					MNULL);
		}
	} else
		/* Cancel all pending scan command */
		wlan_flush_scan_queue(pmadapter);
#endif
	LEAVE();
}

/**
 *  @brief Cancel specific bss's pending ioctl cmd.
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *  @param bss_index    BSS index
 *
 *  @return             N/A
 */
t_void
wlan_cancel_bss_pending_cmd(pmlan_adapter pmadapter, t_u32 bss_index)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	cmd_ctrl_node *pcmd_node = MNULL;
	mlan_ioctl_req *pioctl_buf = MNULL;
#ifdef STA_SUPPORT
	t_u8 flash_scan = MFALSE;
#endif
#ifdef STA_SUPPORT
	pmlan_private priv = MNULL;
#endif
	ENTER();

	PRINTM(MIOCTL, "MOAL Cancel BSS IOCTL: bss_index=%d\n", (int)bss_index);
	wlan_request_cmd_lock(pmadapter);
#ifdef STA_SUPPORT
	if (pmadapter->pscan_ioctl_req &&
	    (pmadapter->pscan_ioctl_req->bss_index == bss_index)) {
		/* IOCTL will be completed, avoid calling IOCTL complete again from EVENT/CMDRESP */
		flash_scan = MTRUE;
		pioctl_buf = pmadapter->pscan_ioctl_req;
		priv = pmadapter->priv[pioctl_buf->bss_index];
		pmadapter->pscan_ioctl_req = MNULL;
		pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
		pcb->moal_ioctl_complete(pmadapter->pmoal_handle, pioctl_buf,
					 MLAN_STATUS_FAILURE);
	}
#endif
	if (pmadapter->curr_cmd && pmadapter->curr_cmd->pioctl_buf) {
		pioctl_buf = (mlan_ioctl_req *)pmadapter->curr_cmd->pioctl_buf;
		if (pioctl_buf->bss_index == bss_index) {
			pcmd_node = pmadapter->curr_cmd;
			pcmd_node->pioctl_buf = MNULL;
			pcmd_node->cmd_flag |= CMD_F_CANCELED;
#ifdef STA_SUPPORT
			if (pioctl_buf->req_id == MLAN_IOCTL_SCAN)
				flash_scan = MTRUE;
#endif
			pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_buf,
						 MLAN_STATUS_FAILURE);
		}
	}
	while ((pcmd_node =
		wlan_get_bss_pending_ioctl_cmd(pmadapter,
					       bss_index)) != MNULL) {
		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->cmd_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
		pcmd_node->pioctl_buf = MNULL;
#ifdef STA_SUPPORT
		if (pioctl_buf->req_id == MLAN_IOCTL_SCAN)
			flash_scan = MTRUE;
#endif
		pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
		pcb->moal_ioctl_complete(pmadapter->pmoal_handle, pioctl_buf,
					 MLAN_STATUS_FAILURE);
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
	}
	wlan_release_cmd_lock(pmadapter);
#ifdef STA_SUPPORT
	if (flash_scan) {
		if (pmadapter->scan_processing &&
		    pmadapter->ext_scan_type == EXT_SCAN_ENHANCE) {
			if (priv) {
				wlan_prepare_cmd(priv,
						 HostCmd_CMD_802_11_SCAN_EXT,
						 HostCmd_ACT_GEN_SET, 0, MNULL,
						 MNULL);
				wlan_recv_event(priv,
						MLAN_EVENT_ID_DRV_DEFER_HANDLING,
						MNULL);
			}
		} else
			/* Cancel all pending scan command */
			wlan_flush_scan_queue(pmadapter);
	}
#endif
	LEAVE();
	return;
}

/**
 *  @brief Cancel pending ioctl cmd.
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *  @param pioctl_req   A pointer to mlan_ioctl_req buf
 *
 *  @return             N/A
 */
t_void
wlan_cancel_pending_ioctl(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	cmd_ctrl_node *pcmd_node = MNULL;
	t_u8 find = MFALSE;
#ifdef STA_SUPPORT
	pmlan_private priv = MNULL;
#endif

	ENTER();

	PRINTM(MIOCTL, "MOAL Cancel IOCTL: 0x%x sub_id=0x%x action=%d\n",
	       pioctl_req->req_id, *((t_u32 *)pioctl_req->pbuf),
	       (int)pioctl_req->action);

	wlan_request_cmd_lock(pmadapter);
#ifdef STA_SUPPORT
	/* IOCTL will be completed, avoid calling IOCTL complete again from EVENT/CMDRESP */
	if (pmadapter->pscan_ioctl_req == pioctl_req) {
		priv = pmadapter->priv[pioctl_req->bss_index];
		pmadapter->pscan_ioctl_req = MNULL;
		find = MTRUE;
	}
#endif
	if ((pmadapter->curr_cmd) &&
	    (pmadapter->curr_cmd->pioctl_buf == pioctl_req)) {
		pcmd_node = pmadapter->curr_cmd;
		pcmd_node->pioctl_buf = MNULL;
		pcmd_node->cmd_flag |= CMD_F_CANCELED;
		find = MTRUE;
	}

	while ((pcmd_node =
		wlan_get_pending_ioctl_cmd(pmadapter, pioctl_req)) != MNULL) {
		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->cmd_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		pcmd_node->pioctl_buf = MNULL;
		find = MTRUE;
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
	}
	wlan_release_cmd_lock(pmadapter);
#ifdef STA_SUPPORT
	if (pioctl_req->req_id == MLAN_IOCTL_SCAN) {
		if (pmadapter->scan_processing &&
		    pmadapter->ext_scan_type == EXT_SCAN_ENHANCE) {
			if (priv) {
				wlan_prepare_cmd(priv,
						 HostCmd_CMD_802_11_SCAN_EXT,
						 HostCmd_ACT_GEN_SET, 0, MNULL,
						 MNULL);
				wlan_recv_event(priv,
						MLAN_EVENT_ID_DRV_DEFER_HANDLING,
						MNULL);
			}
		} else
			/* Cancel all pending scan command */
			wlan_flush_scan_queue(pmadapter);
	}
#endif
	if (find) {
		pioctl_req->status_code = MLAN_ERROR_CMD_CANCEL;
		pcb->moal_ioctl_complete(pmadapter->pmoal_handle, pioctl_req,
					 MLAN_STATUS_FAILURE);
	}

	LEAVE();
	return;
}

/**
 *  @brief Handle the version_ext resp
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_ver_ext(IN pmlan_private pmpriv,
		 IN HostCmd_DS_COMMAND *resp, IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_VERSION_EXT *ver_ext = &resp->params.verext;
	mlan_ds_get_info *info;
	ENTER();
	if (pioctl_buf) {
		info = (mlan_ds_get_info *)pioctl_buf->pbuf;
		info->param.ver_ext.version_str_sel = ver_ext->version_str_sel;
		memcpy(pmpriv->adapter, info->param.ver_ext.version_str,
		       ver_ext->version_str, sizeof(char) * 128);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Handle the rx mgmt forward registration resp
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_rx_mgmt_ind(IN pmlan_private pmpriv,
		     IN HostCmd_DS_COMMAND *resp, IN mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *misc = MNULL;
	ENTER();

	if (pioctl_buf) {
		misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc->param.mgmt_subtype_mask =
			wlan_le32_to_cpu(resp->params.rx_mgmt_ind.
					 mgmt_subtype_mask);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function checks conditions and prepares to
 *              send sleep confirm command to firmware if OK.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
t_void
wlan_check_ps_cond(mlan_adapter *pmadapter)
{
	ENTER();

	if (!pmadapter->cmd_sent &&
	    !pmadapter->curr_cmd && !IS_CARD_RX_RCVD(pmadapter)) {
		wlan_dnld_sleep_confirm_cmd(pmadapter);
	} else {
		PRINTM(MCMND, "Delay Sleep Confirm (%s%s%s)\n",
		       (pmadapter->cmd_sent) ? "D" : "",
		       (pmadapter->curr_cmd) ? "C" : "",
		       (IS_CARD_RX_RCVD(pmadapter)) ? "R" : "");
	}

	LEAVE();
}

/**
 *  @brief This function sends the HS_ACTIVATED event to the application
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param activated    MTRUE if activated, MFALSE if de-activated
 *
 *  @return             N/A
 */
t_void
wlan_host_sleep_activated_event(pmlan_private priv, t_u8 activated)
{
	ENTER();

	if (!priv) {
		LEAVE();
		return;
	}

	if (activated) {
		if (priv->adapter->is_hs_configured) {
			priv->adapter->hs_activated = MTRUE;
			wlan_update_rxreorder_tbl(priv->adapter, MTRUE);
			PRINTM(MEVENT, "hs_activated\n");
			wlan_recv_event(priv, MLAN_EVENT_ID_DRV_HS_ACTIVATED,
					MNULL);
		} else
			PRINTM(MWARN, "hs_activated: HS not configured !!!\n");
	} else {
		PRINTM(MEVENT, "hs_deactived\n");
		priv->adapter->hs_activated = MFALSE;
		wlan_recv_event(priv, MLAN_EVENT_ID_DRV_HS_DEACTIVATED, MNULL);
	}

	LEAVE();
	return;
}

/**
 *  @brief This function sends the HS_WAKEUP event to the application
 *
 *  @param priv         A pointer to mlan_private structure
 *
 *  @return             N/A
 */
t_void
wlan_host_sleep_wakeup_event(pmlan_private priv)
{
	ENTER();

	if (priv->adapter->is_hs_configured)
		wlan_recv_event(priv, MLAN_EVENT_ID_FW_HS_WAKEUP, MNULL);
	else
		PRINTM(MWARN, "hs_wakeup: Host Sleep not configured !!!\n");

	LEAVE();
}

/**
 *  @brief This function handles the command response of hs_cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_802_11_hs_cfg(IN pmlan_private pmpriv,
		       IN HostCmd_DS_COMMAND *resp,
		       IN mlan_ioctl_req *pioctl_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	HostCmd_DS_802_11_HS_CFG_ENH *phs_cfg = &resp->params.opt_hs_cfg;

	ENTER();

	if (wlan_le16_to_cpu(phs_cfg->action) == HS_ACTIVATE) {
		/* clean up curr_cmd to allow suspend */
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_NO_ERROR;
		/* Clean up and put current command back to cmd_free_q */
		wlan_request_cmd_lock(pmadapter);
		wlan_insert_cmd_to_free_q(pmadapter, pmadapter->curr_cmd);
		pmadapter->curr_cmd = MNULL;
		wlan_release_cmd_lock(pmadapter);
		wlan_host_sleep_activated_event(pmpriv, MTRUE);
		goto done;
	} else {
		phs_cfg->params.hs_config.conditions =
			wlan_le32_to_cpu(phs_cfg->params.hs_config.conditions);
		PRINTM(MCMND,
		       "CMD_RESP: HS_CFG cmd reply result=%#x,"
		       " conditions=0x%x gpio=0x%x gap=0x%x\n", resp->result,
		       phs_cfg->params.hs_config.conditions,
		       phs_cfg->params.hs_config.gpio,
		       phs_cfg->params.hs_config.gap);
	}
	if (phs_cfg->params.hs_config.conditions != HOST_SLEEP_CFG_CANCEL) {
		pmadapter->is_hs_configured = MTRUE;
	} else {
		pmadapter->is_hs_configured = MFALSE;
		if (pmadapter->hs_activated)
			wlan_host_sleep_activated_event(pmpriv, MFALSE);
	}

done:
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Perform hs related activities on receiving the power up interrupt
 *
 *  @param pmadapter  A pointer to the adapter structure
 *  @return           N/A
 */
t_void
wlan_process_hs_config(pmlan_adapter pmadapter)
{
	ENTER();
	PRINTM(MINFO, "Recevie interrupt/data in HS mode\n");
	if (pmadapter->hs_cfg.gap == HOST_SLEEP_CFG_GAP_FF)
		wlan_pm_wakeup_card(pmadapter);
	LEAVE();
	return;
}

/**
 *  @brief Check sleep confirm command response and set the state to ASLEEP
 *
 *  @param pmadapter  A pointer to the adapter structure
 *  @param pbuf       A pointer to the command response buffer
 *  @param upld_len   Command response buffer length
 *  @return           N/A
 */
void
wlan_process_sleep_confirm_resp(pmlan_adapter pmadapter, t_u8 *pbuf,
				t_u32 upld_len)
{
	HostCmd_DS_COMMAND *cmd;

	ENTER();

	if (!upld_len) {
		PRINTM(MERROR, "Command size is 0\n");
		LEAVE();
		return;
	}
	cmd = (HostCmd_DS_COMMAND *)pbuf;
	cmd->result = wlan_le16_to_cpu(cmd->result);
	cmd->command = wlan_le16_to_cpu(cmd->command);
	cmd->seq_num = wlan_le16_to_cpu(cmd->seq_num);

	/* Update sequence number */
	cmd->seq_num = HostCmd_GET_SEQ_NO(cmd->seq_num);
	/* Clear RET_BIT from HostCmd */
	cmd->command &= HostCmd_CMD_ID_MASK;

	if (cmd->command != HostCmd_CMD_802_11_PS_MODE_ENH) {
		PRINTM(MERROR,
		       "Received unexpected response for command %x, result = %x\n",
		       cmd->command, cmd->result);
		LEAVE();
		return;
	}
	PRINTM(MEVENT, "#\n");
	if (cmd->result != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "Sleep confirm command failed\n");
		pmadapter->pm_wakeup_card_req = MFALSE;
		pmadapter->ps_state = PS_STATE_AWAKE;
		LEAVE();
		return;
	}
	pmadapter->pm_wakeup_card_req = MTRUE;

	if (pmadapter->is_hs_configured) {
		wlan_host_sleep_activated_event(wlan_get_priv
						(pmadapter, MLAN_BSS_ROLE_ANY),
						MTRUE);
	}
	pmadapter->ps_state = PS_STATE_SLEEP;
	LEAVE();
}

/**
 *  @brief This function prepares command of power mode
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param ps_bitmap    PS bitmap
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_enh_power_mode(pmlan_private pmpriv,
			IN HostCmd_DS_COMMAND *cmd,
			IN t_u16 cmd_action,
			IN t_u16 ps_bitmap, IN t_void *pdata_buf)
{
	HostCmd_DS_802_11_PS_MODE_ENH *psmode_enh = &cmd->params.psmode_enh;
	t_u8 *tlv = MNULL;
	t_u16 cmd_size = 0;

	ENTER();

	PRINTM(MCMND, "PS Command: action = 0x%x, bitmap = 0x%x\n", cmd_action,
	       ps_bitmap);

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_PS_MODE_ENH);
	if (cmd_action == DIS_AUTO_PS) {
		psmode_enh->action = wlan_cpu_to_le16(DIS_AUTO_PS);
		psmode_enh->params.ps_bitmap = wlan_cpu_to_le16(ps_bitmap);
		cmd->size = wlan_cpu_to_le16(S_DS_GEN + AUTO_PS_FIX_SIZE);
	} else if (cmd_action == GET_PS) {
		psmode_enh->action = wlan_cpu_to_le16(GET_PS);
		psmode_enh->params.ps_bitmap = wlan_cpu_to_le16(ps_bitmap);
		cmd->size = wlan_cpu_to_le16(S_DS_GEN + AUTO_PS_FIX_SIZE);
	} else if (cmd_action == EN_AUTO_PS) {
		psmode_enh->action = wlan_cpu_to_le16(EN_AUTO_PS);
		psmode_enh->params.auto_ps.ps_bitmap =
			wlan_cpu_to_le16(ps_bitmap);
		cmd_size = S_DS_GEN + AUTO_PS_FIX_SIZE;
		tlv = (t_u8 *)cmd + cmd_size;
		if (ps_bitmap & BITMAP_STA_PS) {
			pmlan_adapter pmadapter = pmpriv->adapter;
			MrvlIEtypes_ps_param_t *ps_tlv =
				(MrvlIEtypes_ps_param_t *)tlv;
			ps_param *ps_mode = &ps_tlv->param;
			ps_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_PS_PARAM);
			ps_tlv->header.len =
				wlan_cpu_to_le16(sizeof(MrvlIEtypes_ps_param_t)
						 - sizeof(MrvlIEtypesHeader_t));
			cmd_size += sizeof(MrvlIEtypes_ps_param_t);
			tlv += sizeof(MrvlIEtypes_ps_param_t);
			ps_mode->null_pkt_interval =
				wlan_cpu_to_le16(pmadapter->null_pkt_interval);
			ps_mode->multiple_dtims =
				wlan_cpu_to_le16(pmadapter->multiple_dtim);
			ps_mode->bcn_miss_timeout =
				wlan_cpu_to_le16(pmadapter->bcn_miss_time_out);
			ps_mode->local_listen_interval =
				wlan_cpu_to_le16(pmadapter->
						 local_listen_interval);
			ps_mode->adhoc_wake_period =
				wlan_cpu_to_le16(pmadapter->adhoc_awake_period);
			ps_mode->delay_to_ps =
				wlan_cpu_to_le16(pmadapter->delay_to_ps);
			ps_mode->mode =
				wlan_cpu_to_le16(pmadapter->enhanced_ps_mode);
		}
		if (ps_bitmap & BITMAP_BCN_TMO) {
			MrvlIEtypes_bcn_timeout_t *bcn_tmo_tlv =
				(MrvlIEtypes_bcn_timeout_t *) tlv;
			mlan_ds_bcn_timeout *bcn_tmo =
				(mlan_ds_bcn_timeout *) pdata_buf;
			bcn_tmo_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_BCN_TIMEOUT);
			bcn_tmo_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_bcn_timeout_t) -
						 sizeof(MrvlIEtypesHeader_t));
			bcn_tmo_tlv->bcn_miss_tmo_window =
				wlan_cpu_to_le16(bcn_tmo->bcn_miss_tmo_window);
			bcn_tmo_tlv->bcn_miss_tmo_period =
				wlan_cpu_to_le16(bcn_tmo->bcn_miss_tmo_period);
			bcn_tmo_tlv->bcn_rq_tmo_window =
				wlan_cpu_to_le16(bcn_tmo->bcn_rq_tmo_window);
			bcn_tmo_tlv->bcn_rq_tmo_period =
				wlan_cpu_to_le16(bcn_tmo->bcn_rq_tmo_period);
			cmd_size += sizeof(MrvlIEtypes_bcn_timeout_t);
			tlv += sizeof(MrvlIEtypes_bcn_timeout_t);

			psmode_enh->params.auto_ps.ps_bitmap =
				wlan_cpu_to_le16((ps_bitmap & (~BITMAP_BCN_TMO))
						 | BITMAP_STA_PS);
		}
		if (ps_bitmap & BITMAP_AUTO_DS) {
			MrvlIEtypes_auto_ds_param_t *auto_ps_tlv =
				(MrvlIEtypes_auto_ds_param_t *)tlv;
			auto_ds_param *auto_ds = &auto_ps_tlv->param;
			t_u16 idletime = 0;
			auto_ps_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_AUTO_DS_PARAM);
			auto_ps_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_auto_ds_param_t) -
						 sizeof(MrvlIEtypesHeader_t));
			cmd_size += sizeof(MrvlIEtypes_auto_ds_param_t);
			tlv += sizeof(MrvlIEtypes_auto_ds_param_t);
			if (pdata_buf)
				idletime =
					((mlan_ds_auto_ds *)pdata_buf)->
					idletime;
			auto_ds->deep_sleep_timeout =
				wlan_cpu_to_le16(idletime);
		}
#if defined(UAP_SUPPORT)
		if (pdata_buf &&
		    (ps_bitmap & (BITMAP_UAP_INACT_PS | BITMAP_UAP_DTIM_PS))) {
			mlan_ds_ps_mgmt *ps_mgmt = (mlan_ds_ps_mgmt *)pdata_buf;
			MrvlIEtypes_sleep_param_t *sleep_tlv = MNULL;
			MrvlIEtypes_inact_sleep_param_t *inact_tlv = MNULL;
			if (ps_mgmt->flags & PS_FLAG_SLEEP_PARAM) {
				sleep_tlv = (MrvlIEtypes_sleep_param_t *)tlv;
				sleep_tlv->header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_AP_SLEEP_PARAM);
				sleep_tlv->header.len =
					wlan_cpu_to_le16(sizeof
							 (MrvlIEtypes_sleep_param_t)
							 -
							 sizeof
							 (MrvlIEtypesHeader_t));
				sleep_tlv->ctrl_bitmap =
					wlan_cpu_to_le32(ps_mgmt->sleep_param.
							 ctrl_bitmap);
				sleep_tlv->min_sleep =
					wlan_cpu_to_le32(ps_mgmt->sleep_param.
							 min_sleep);
				sleep_tlv->max_sleep =
					wlan_cpu_to_le32(ps_mgmt->sleep_param.
							 max_sleep);
				cmd_size += sizeof(MrvlIEtypes_sleep_param_t);
				tlv += sizeof(MrvlIEtypes_sleep_param_t);
			}
			if (ps_mgmt->flags & PS_FLAG_INACT_SLEEP_PARAM) {
				inact_tlv =
					(MrvlIEtypes_inact_sleep_param_t *)tlv;
				inact_tlv->header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_AP_INACT_SLEEP_PARAM);
				inact_tlv->header.len =
					wlan_cpu_to_le16(sizeof
							 (MrvlIEtypes_inact_sleep_param_t)
							 -
							 sizeof
							 (MrvlIEtypesHeader_t));
				inact_tlv->inactivity_to =
					wlan_cpu_to_le32(ps_mgmt->inact_param.
							 inactivity_to);
				inact_tlv->min_awake =
					wlan_cpu_to_le32(ps_mgmt->inact_param.
							 min_awake);
				inact_tlv->max_awake =
					wlan_cpu_to_le32(ps_mgmt->inact_param.
							 max_awake);
				cmd_size +=
					sizeof(MrvlIEtypes_inact_sleep_param_t);
				tlv += sizeof(MrvlIEtypes_inact_sleep_param_t);
			}
		}
#endif
		cmd->size = wlan_cpu_to_le16(cmd_size);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of ps_mode_enh
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_enh_power_mode(IN pmlan_private pmpriv,
			IN HostCmd_DS_COMMAND *resp,
			IN mlan_ioctl_req *pioctl_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	MrvlIEtypesHeader_t *mrvl_tlv = MNULL;
	MrvlIEtypes_auto_ds_param_t *auto_ds_tlv = MNULL;
	HostCmd_DS_802_11_PS_MODE_ENH *ps_mode = &resp->params.psmode_enh;

	ENTER();

	ps_mode->action = wlan_le16_to_cpu(ps_mode->action);
	PRINTM(MINFO, "CMD_RESP: PS_MODE cmd reply result=%#x action=0x%X\n",
	       resp->result, ps_mode->action);
	if (ps_mode->action == EN_AUTO_PS) {
		ps_mode->params.auto_ps.ps_bitmap =
			wlan_le16_to_cpu(ps_mode->params.auto_ps.ps_bitmap);
		if (ps_mode->params.auto_ps.ps_bitmap & BITMAP_AUTO_DS) {
			PRINTM(MCMND, "Enabled auto deep sleep\n");
			pmpriv->adapter->is_deep_sleep = MTRUE;
			mrvl_tlv =
				(MrvlIEtypesHeader_t *)((t_u8 *)ps_mode +
							AUTO_PS_FIX_SIZE);
			while (wlan_le16_to_cpu(mrvl_tlv->type) !=
			       TLV_TYPE_AUTO_DS_PARAM) {
				mrvl_tlv =
					(MrvlIEtypesHeader_t *)((t_u8 *)mrvl_tlv
								+
								wlan_le16_to_cpu
								(mrvl_tlv->len)
								+
								sizeof
								(MrvlIEtypesHeader_t));
			}
			auto_ds_tlv = (MrvlIEtypes_auto_ds_param_t *)mrvl_tlv;
			pmpriv->adapter->idle_time =
				wlan_le16_to_cpu(auto_ds_tlv->param.
						 deep_sleep_timeout);
		}
		if (ps_mode->params.auto_ps.ps_bitmap & BITMAP_STA_PS) {
			PRINTM(MCMND, "Enabled STA power save\n");
			if (pmadapter->sleep_period.period) {
				PRINTM(MCMND,
				       "Setting uapsd/pps mode to TRUE\n");
			}
		}
#if defined(UAP_SUPPORT)
		if (ps_mode->params.auto_ps.
		    ps_bitmap & (BITMAP_UAP_INACT_PS | BITMAP_UAP_DTIM_PS)) {
			pmadapter->ps_mode = Wlan802_11PowerModePSP;
			PRINTM(MCMND, "Enabled uAP power save\n");
		}
#endif
	} else if (ps_mode->action == DIS_AUTO_PS) {
		ps_mode->params.ps_bitmap =
			wlan_cpu_to_le16(ps_mode->params.ps_bitmap);
		if (ps_mode->params.ps_bitmap & BITMAP_AUTO_DS) {
			pmpriv->adapter->is_deep_sleep = MFALSE;
			PRINTM(MCMND, "Disabled auto deep sleep\n");
		}
		if (ps_mode->params.ps_bitmap & BITMAP_STA_PS) {
			PRINTM(MCMND, "Disabled STA power save\n");
			if (pmadapter->sleep_period.period) {
				pmadapter->delay_null_pkt = MFALSE;
				pmadapter->tx_lock_flag = MFALSE;
				pmadapter->pps_uapsd_mode = MFALSE;
			}
		}
#if defined(UAP_SUPPORT)
		if (ps_mode->params.
		    ps_bitmap & (BITMAP_UAP_INACT_PS | BITMAP_UAP_DTIM_PS)) {
			pmadapter->ps_mode = Wlan802_11PowerModeCAM;
			PRINTM(MCMND, "Disabled uAP power save\n");
		}
#endif
	} else if (ps_mode->action == GET_PS) {
		ps_mode->params.ps_bitmap =
			wlan_le16_to_cpu(ps_mode->params.ps_bitmap);
		if (ps_mode->params.auto_ps.
		    ps_bitmap & (BITMAP_STA_PS | BITMAP_UAP_INACT_PS |
				 BITMAP_UAP_DTIM_PS))
			pmadapter->ps_mode = Wlan802_11PowerModePSP;
		else
			pmadapter->ps_mode = Wlan802_11PowerModeCAM;
		PRINTM(MCMND, "ps_bitmap=0x%x\n", ps_mode->params.ps_bitmap);
		if (pioctl_buf) {
			mlan_ds_pm_cfg *pm_cfg =
				(mlan_ds_pm_cfg *)pioctl_buf->pbuf;
			if (pm_cfg->sub_command == MLAN_OID_PM_CFG_IEEE_PS) {
				if (ps_mode->params.auto_ps.
				    ps_bitmap & BITMAP_STA_PS)
					pm_cfg->param.ps_mode = 1;
				else
					pm_cfg->param.ps_mode = 0;
			}
#if defined(UAP_SUPPORT)
			if (pm_cfg->sub_command == MLAN_OID_PM_CFG_PS_MODE) {
				MrvlIEtypes_sleep_param_t *sleep_tlv = MNULL;
				MrvlIEtypes_inact_sleep_param_t *inact_tlv =
					MNULL;
				MrvlIEtypesHeader_t *tlv = MNULL;
				t_u16 tlv_type = 0;
				t_u16 tlv_len = 0;
				t_u16 tlv_buf_left = 0;
				pm_cfg->param.ps_mgmt.flags = PS_FLAG_PS_MODE;
				if (ps_mode->params.
				    ps_bitmap & BITMAP_UAP_INACT_PS)
					pm_cfg->param.ps_mgmt.ps_mode =
						PS_MODE_INACTIVITY;
				else if (ps_mode->params.
					 ps_bitmap & BITMAP_UAP_DTIM_PS)
					pm_cfg->param.ps_mgmt.ps_mode =
						PS_MODE_PERIODIC_DTIM;
				else
					pm_cfg->param.ps_mgmt.ps_mode =
						PS_MODE_DISABLE;
				tlv_buf_left =
					resp->size - (S_DS_GEN +
						      AUTO_PS_FIX_SIZE);
				tlv = (MrvlIEtypesHeader_t *)((t_u8 *)ps_mode +
							      AUTO_PS_FIX_SIZE);
				while (tlv_buf_left >=
				       sizeof(MrvlIEtypesHeader_t)) {
					tlv_type = wlan_le16_to_cpu(tlv->type);
					tlv_len = wlan_le16_to_cpu(tlv->len);
					switch (tlv_type) {
					case TLV_TYPE_AP_SLEEP_PARAM:
						sleep_tlv =
							(MrvlIEtypes_sleep_param_t
							 *)tlv;
						pm_cfg->param.ps_mgmt.flags |=
							PS_FLAG_SLEEP_PARAM;
						pm_cfg->param.ps_mgmt.
							sleep_param.
							ctrl_bitmap =
							wlan_le32_to_cpu
							(sleep_tlv->
							 ctrl_bitmap);
						pm_cfg->param.ps_mgmt.
							sleep_param.min_sleep =
							wlan_le32_to_cpu
							(sleep_tlv->min_sleep);
						pm_cfg->param.ps_mgmt.
							sleep_param.max_sleep =
							wlan_le32_to_cpu
							(sleep_tlv->max_sleep);
						break;
					case TLV_TYPE_AP_INACT_SLEEP_PARAM:
						inact_tlv =
							(MrvlIEtypes_inact_sleep_param_t
							 *)tlv;
						pm_cfg->param.ps_mgmt.flags |=
							PS_FLAG_INACT_SLEEP_PARAM;
						pm_cfg->param.ps_mgmt.
							inact_param.
							inactivity_to =
							wlan_le32_to_cpu
							(inact_tlv->
							 inactivity_to);
						pm_cfg->param.ps_mgmt.
							inact_param.min_awake =
							wlan_le32_to_cpu
							(inact_tlv->min_awake);
						pm_cfg->param.ps_mgmt.
							inact_param.max_awake =
							wlan_le32_to_cpu
							(inact_tlv->max_awake);
						break;
					}
					tlv_buf_left -=
						tlv_len +
						sizeof(MrvlIEtypesHeader_t);
					tlv = (MrvlIEtypesHeader_t *)((t_u8 *)
								      tlv +
								      tlv_len +
								      sizeof
								      (MrvlIEtypesHeader_t));
				}
			}
#endif
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of tx rate query
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_802_11_tx_rate_query(IN pmlan_private pmpriv,
			      IN HostCmd_DS_COMMAND *resp,
			      IN mlan_ioctl_req *pioctl_buf)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_ds_rate *rate = MNULL;
	ENTER();

	pmpriv->tx_rate = resp->params.tx_rate.tx_rate;
	pmpriv->tx_rate_info = resp->params.tx_rate.tx_rate_info;

	if (!pmpriv->is_data_rate_auto) {
		pmpriv->data_rate = wlan_index_to_data_rate(pmadapter,
							    pmpriv->tx_rate,
							    pmpriv->
							    tx_rate_info);
	}

	if (pioctl_buf) {
		rate = (mlan_ds_rate *)pioctl_buf->pbuf;
		if (rate->sub_command == MLAN_OID_RATE_CFG) {
			if (rate->param.rate_cfg.rate_type == MLAN_RATE_INDEX) {
				if ((pmpriv->tx_rate_info & 0x3) ==
				    MLAN_RATE_FORMAT_HT)
					/* HT rate */
					rate->param.rate_cfg.rate =
						pmpriv->tx_rate +
						MLAN_RATE_INDEX_MCS0;
				else
					/* LG rate */
					/* For HostCmd_CMD_802_11_TX_RATE_QUERY,
					 * there is a hole (0x4) in rate table
					 * between HR/DSSS and OFDM rates,
					 * so minus 1 for OFDM rate index */
					rate->param.rate_cfg.rate =
						(pmpriv->tx_rate >
						 MLAN_RATE_INDEX_OFDM0) ?
						pmpriv->tx_rate -
						1 : pmpriv->tx_rate;
			} else {
				/* rate_type = MLAN_RATE_VALUE */
				rate->param.rate_cfg.rate =
					wlan_index_to_data_rate(pmadapter,
								pmpriv->tx_rate,
								pmpriv->
								tx_rate_info);
			}
		} else if (rate->sub_command == MLAN_OID_GET_DATA_RATE) {
			/* Tx rate info */
			if ((pmpriv->tx_rate_info & 0x3) == MLAN_RATE_FORMAT_HT) {
				/* HT rate */
				rate->param.data_rate.tx_rate_format =
					MLAN_RATE_FORMAT_HT;
				rate->param.data_rate.tx_ht_bw =
					(pmpriv->tx_rate_info & 0xC) >> 2;
				rate->param.data_rate.tx_ht_gi =
					(pmpriv->tx_rate_info & 0x10) >> 4;
				rate->param.data_rate.tx_mcs_index =
					pmpriv->tx_rate;
				rate->param.data_rate.tx_data_rate =
					wlan_index_to_data_rate(pmadapter,
								pmpriv->tx_rate,
								pmpriv->
								tx_rate_info);
			} else {
				/* LG rate */
				rate->param.data_rate.tx_rate_format =
					MLAN_RATE_FORMAT_LG;
				/* For HostCmd_CMD_802_11_TX_RATE_QUERY,
				 * there is a hole in rate table
				 * between HR/DSSS and OFDM rates,
				 * so minus 1 for OFDM rate index */
				rate->param.data_rate.tx_data_rate =
					(pmpriv->tx_rate >
					 MLAN_RATE_INDEX_OFDM0) ? pmpriv->
					tx_rate - 1 : pmpriv->tx_rate;
			}

			/* Rx rate info */
			if ((pmpriv->rxpd_rate_info & 0x3) ==
			    MLAN_RATE_FORMAT_HT) {
				/* HT rate */
				rate->param.data_rate.rx_rate_format =
					MLAN_RATE_FORMAT_HT;
				rate->param.data_rate.rx_ht_bw =
					(pmpriv->rxpd_rate_info & 0xC) >> 2;
				rate->param.data_rate.rx_ht_gi =
					(pmpriv->rxpd_rate_info & 0x10) >> 4;
				rate->param.data_rate.rx_mcs_index =
					pmpriv->rxpd_rate;
				rate->param.data_rate.rx_data_rate =
					wlan_index_to_data_rate(pmadapter,
								pmpriv->
								rxpd_rate,
								pmpriv->
								rxpd_rate_info);
			} else {
				/* LG rate */
				rate->param.data_rate.rx_rate_format =
					MLAN_RATE_FORMAT_LG;
				/* For rate index in RxPD,
				 * there is a hole in rate table
				 * between HR/DSSS and OFDM rates,
				 * so minus 1 for OFDM rate index */
				rate->param.data_rate.rx_data_rate =
					(pmpriv->rxpd_rate >
					 MLAN_RATE_INDEX_OFDM0) ? pmpriv->
					rxpd_rate - 1 : pmpriv->rxpd_rate;
			}
		}
		pioctl_buf->data_read_written = sizeof(mlan_data_rate) +
			MLAN_SUB_COMMAND_SIZE;
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 * @brief This function prepares command of fw_wakeup_method.
 *
 * @param pmpriv       A pointer to mlan_private structure
 * @param cmd          A pointer to HostCmd_DS_COMMAND structure
 * @param cmd_action   The action: GET or SET
 * @param pdata_buf    A pointer to data buffer
 *
 * @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_802_11_fw_wakeup_method(IN pmlan_private pmpriv,
				 IN HostCmd_DS_COMMAND *cmd,
				 IN t_u16 cmd_action, IN t_u16 *pdata_buf)
{
	HostCmd_DS_802_11_FW_WAKEUP_METHOD *fwwm = &cmd->params.fwwakeupmethod;
	mlan_fw_wakeup_params *fw_wakeup_params = MNULL;
	MrvlIEtypes_WakeupSourceGPIO_t *tlv =
		(MrvlIEtypes_WakeupSourceGPIO_t *) ((t_u8 *)fwwm +
						    sizeof
						    (HostCmd_DS_802_11_FW_WAKEUP_METHOD));

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_FW_WAKE_METHOD);
	cmd->size = sizeof(HostCmd_DS_802_11_FW_WAKEUP_METHOD) + S_DS_GEN;
	fwwm->action = wlan_cpu_to_le16(cmd_action);
	switch (cmd_action) {
	case HostCmd_ACT_GEN_SET:
		fw_wakeup_params = (mlan_fw_wakeup_params *) pdata_buf;
		fwwm->method = wlan_cpu_to_le16(fw_wakeup_params->method);

		if (fw_wakeup_params->method == WAKEUP_FW_THRU_GPIO) {
			cmd->size += sizeof(MrvlIEtypes_WakeupSourceGPIO_t);
			tlv->header.type =
				wlan_cpu_to_le16
				(TLV_TYPE_HS_WAKEUP_SOURCE_GPIO);
			tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_WakeupSourceGPIO_t)
						 - sizeof(MrvlIEtypesHeader_t));
			tlv->ind_gpio = (t_u8)fw_wakeup_params->gpio_pin;
		}

		break;
	case HostCmd_ACT_GEN_GET:
	default:
		fwwm->method = wlan_cpu_to_le16(WAKEUP_FW_UNCHANGED);
		break;
	}
	cmd->size = wlan_cpu_to_le16(cmd->size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of fw_wakeup_method
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_fw_wakeup_method(IN pmlan_private pmpriv,
			  IN HostCmd_DS_COMMAND *resp,
			  IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_802_11_FW_WAKEUP_METHOD *fwwm = &resp->params.fwwakeupmethod;
	t_u16 action;
	MrvlIEtypes_WakeupSourceGPIO_t *gpio_tlv =
		(MrvlIEtypes_WakeupSourceGPIO_t *) ((t_u8 *)fwwm +
						    sizeof
						    (HostCmd_DS_802_11_FW_WAKEUP_METHOD));
	mlan_ds_pm_cfg *pmcfg = MNULL;

	ENTER();

	action = wlan_le16_to_cpu(fwwm->action);

	pmpriv->adapter->fw_wakeup_method = wlan_le16_to_cpu(fwwm->method);
	pmpriv->adapter->fw_wakeup_gpio_pin = 0;

	if ((resp->size -
	     (sizeof(HostCmd_DS_802_11_FW_WAKEUP_METHOD) + S_DS_GEN))
	    == sizeof(MrvlIEtypes_WakeupSourceGPIO_t)) {
		pmpriv->adapter->fw_wakeup_gpio_pin = gpio_tlv->ind_gpio;
	}
	PRINTM(MCMND, "FW wakeup method=%d, gpio=%d\n",
	       pmpriv->adapter->fw_wakeup_method,
	       pmpriv->adapter->fw_wakeup_gpio_pin);

	if (pioctl_buf) {
		pmcfg = (mlan_ds_pm_cfg *)pioctl_buf->pbuf;
		pmcfg->param.fw_wakeup_params.method =
			pmpriv->adapter->fw_wakeup_method;
		pmcfg->param.fw_wakeup_params.gpio_pin =
			pmpriv->adapter->fw_wakeup_gpio_pin;
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 * @brief This function prepares command of robustcoex.
 *
 * @param pmpriv       A pointer to mlan_private structure
 * @param cmd          A pointer to HostCmd_DS_COMMAND structure
 * @param cmd_action   The action: GET or SET
 * @param pdata_buf    A pointer to data buffer
 *
 * @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_robustcoex(IN pmlan_private pmpriv,
		    IN HostCmd_DS_COMMAND *cmd,
		    IN t_u16 cmd_action, IN t_u16 *pdata_buf)
{
	HostCmd_DS_802_11_ROBUSTCOEX *rbstcx = &cmd->params.robustcoexparams;
	mlan_ds_misc_robustcoex_params *robustcoex_params = MNULL;
	MrvlIEtypes_RobustcoexSourceGPIO_t *tlv =
		(MrvlIEtypes_RobustcoexSourceGPIO_t *) ((t_u8 *)rbstcx +
							sizeof
							(HostCmd_DS_802_11_ROBUSTCOEX));

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_ROBUSTCOEX);
	cmd->size = sizeof(HostCmd_DS_802_11_ROBUSTCOEX) + S_DS_GEN;
	rbstcx->action = wlan_cpu_to_le16(cmd_action);
	switch (cmd_action) {
	case HostCmd_ACT_GEN_SET:
		robustcoex_params =
			(mlan_ds_misc_robustcoex_params *) pdata_buf;
		if (robustcoex_params->method == ROBUSTCOEX_GPIO_CFG) {
			cmd->size += sizeof(MrvlIEtypes_RobustcoexSourceGPIO_t);
			tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_ROBUSTCOEX);
			tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_RobustcoexSourceGPIO_t)
						 - sizeof(MrvlIEtypesHeader_t));
			tlv->enable = (t_u8)robustcoex_params->enable;
			tlv->gpio_num = (t_u8)robustcoex_params->gpio_num;
			tlv->gpio_polarity =
				(t_u8)robustcoex_params->gpio_polarity;
		}
		break;
	case HostCmd_ACT_GEN_GET:
	default:
		break;
	}
	cmd->size = wlan_cpu_to_le16(cmd->size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of tx_rate_cfg.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_tx_rate_cfg(IN pmlan_private pmpriv,
		     IN HostCmd_DS_COMMAND *cmd,
		     IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	HostCmd_DS_TX_RATE_CFG *rate_cfg =
		(HostCmd_DS_TX_RATE_CFG *)&(cmd->params.tx_rate_cfg);
	MrvlRateScope_t *rate_scope;
	MrvlRateDropPattern_t *rate_drop;
	t_u16 *pbitmap_rates = (t_u16 *)pdata_buf;

	t_u32 i;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_TX_RATE_CFG);

	rate_cfg->action = wlan_cpu_to_le16(cmd_action);

	rate_scope = (MrvlRateScope_t *)((t_u8 *)rate_cfg +
					 sizeof(HostCmd_DS_TX_RATE_CFG));
	rate_scope->type = wlan_cpu_to_le16(TLV_TYPE_RATE_SCOPE);
	rate_scope->length = wlan_cpu_to_le16(sizeof(MrvlRateScope_t) -
					      sizeof(MrvlIEtypesHeader_t));
	if (pbitmap_rates != MNULL) {
		rate_scope->hr_dsss_rate_bitmap =
			wlan_cpu_to_le16(pbitmap_rates[0]);
		rate_scope->ofdm_rate_bitmap =
			wlan_cpu_to_le16(pbitmap_rates[1]);
		for (i = 0; i < NELEMENTS(rate_scope->ht_mcs_rate_bitmap); i++)
			rate_scope->ht_mcs_rate_bitmap[i] =
				wlan_cpu_to_le16(pbitmap_rates[2 + i]);
	} else {
		rate_scope->hr_dsss_rate_bitmap =
			wlan_cpu_to_le16(pmpriv->bitmap_rates[0]);
		rate_scope->ofdm_rate_bitmap =
			wlan_cpu_to_le16(pmpriv->bitmap_rates[1]);
		for (i = 0; i < NELEMENTS(rate_scope->ht_mcs_rate_bitmap); i++)
			rate_scope->ht_mcs_rate_bitmap[i] =
				wlan_cpu_to_le16(pmpriv->bitmap_rates[2 + i]);
	}

	rate_drop = (MrvlRateDropPattern_t *)((t_u8 *)rate_scope +
					      sizeof(MrvlRateScope_t));
	rate_drop->type = wlan_cpu_to_le16(TLV_TYPE_RATE_DROP_PATTERN);
	rate_drop->length = wlan_cpu_to_le16(sizeof(rate_drop->rate_drop_mode));
	rate_drop->rate_drop_mode = 0;

	cmd->size = wlan_cpu_to_le16(S_DS_GEN + sizeof(HostCmd_DS_TX_RATE_CFG) +
				     sizeof(MrvlRateScope_t) +
				     sizeof(MrvlRateDropPattern_t));

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of tx_rate_cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_tx_rate_cfg(IN pmlan_private pmpriv,
		     IN HostCmd_DS_COMMAND *resp, IN mlan_ioctl_req *pioctl_buf)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_ds_rate *ds_rate = MNULL;
	HostCmd_DS_TX_RATE_CFG *prate_cfg = MNULL;
	MrvlRateScope_t *prate_scope;
	MrvlIEtypesHeader_t *head = MNULL;
	t_u16 tlv, tlv_buf_len = 0;
	t_u8 *tlv_buf;
	t_u32 i;
	t_s32 index;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (resp == MNULL) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	prate_cfg = (HostCmd_DS_TX_RATE_CFG *)&(resp->params.tx_rate_cfg);

	tlv_buf = (t_u8 *)((t_u8 *)prate_cfg) + sizeof(HostCmd_DS_TX_RATE_CFG);
	if (tlv_buf) {
		tlv_buf_len = *(t_u16 *)(tlv_buf + sizeof(t_u16));
		tlv_buf_len = wlan_le16_to_cpu(tlv_buf_len);
	}

	while (tlv_buf && tlv_buf_len > 0) {
		tlv = (*tlv_buf);
		tlv = tlv | (*(tlv_buf + 1) << 8);

		switch (tlv) {
		case TLV_TYPE_RATE_SCOPE:
			prate_scope = (MrvlRateScope_t *)tlv_buf;
			pmpriv->bitmap_rates[0] =
				wlan_le16_to_cpu(prate_scope->
						 hr_dsss_rate_bitmap);
			pmpriv->bitmap_rates[1] =
				wlan_le16_to_cpu(prate_scope->ofdm_rate_bitmap);
			for (i = 0;
			     i < NELEMENTS(prate_scope->ht_mcs_rate_bitmap);
			     i++)
				pmpriv->bitmap_rates[2 + i] =
					wlan_le16_to_cpu(prate_scope->
							 ht_mcs_rate_bitmap[i]);
			break;
			/* Add RATE_DROP tlv here */
		}

		head = (MrvlIEtypesHeader_t *)tlv_buf;
		head->len = wlan_le16_to_cpu(head->len);
		tlv_buf += head->len + sizeof(MrvlIEtypesHeader_t);
		tlv_buf_len -= head->len;
	}

	pmpriv->is_data_rate_auto = wlan_is_rate_auto(pmpriv);

	if (pmpriv->is_data_rate_auto) {
		pmpriv->data_rate = 0;
	} else {
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_802_11_TX_RATE_QUERY,
				       HostCmd_ACT_GEN_GET, 0, MNULL, MNULL);

	}

	if (pioctl_buf) {
		ds_rate = (mlan_ds_rate *)pioctl_buf->pbuf;
		if (ds_rate == MNULL) {
			PRINTM(MERROR, "Request buffer not found!\n");
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		if (pmpriv->is_data_rate_auto) {
			ds_rate->param.rate_cfg.is_rate_auto = MTRUE;
			ds_rate->param.rate_cfg.rate_format =
				MLAN_RATE_FORMAT_AUTO;
		} else {
			ds_rate->param.rate_cfg.is_rate_auto = MFALSE;
			/* check the LG rate */
			index = wlan_get_rate_index(pmadapter,
						    &pmpriv->bitmap_rates[0],
						    4);
			if (index != -1) {
				if ((index >= MLAN_RATE_BITMAP_OFDM0) &&
				    (index <= MLAN_RATE_BITMAP_OFDM7))
					index -= (MLAN_RATE_BITMAP_OFDM0 -
						  MLAN_RATE_INDEX_OFDM0);
				ds_rate->param.rate_cfg.rate_format =
					MLAN_RATE_FORMAT_LG;
				ds_rate->param.rate_cfg.rate = index;
			}
			/* check the HT rate */
			index = wlan_get_rate_index(pmadapter,
						    &pmpriv->bitmap_rates[2],
						    16);
			if (index != -1) {
				ds_rate->param.rate_cfg.rate_format =
					MLAN_RATE_FORMAT_HT;
				ds_rate->param.rate_cfg.rate = index;
			}
			PRINTM(MINFO, "Rate index is %d\n",
			       ds_rate->param.rate_cfg.rate);
		}
		for (i = 0; i < MAX_BITMAP_RATES_SIZE; i++) {
			ds_rate->param.rate_cfg.bitmap_rates[i] =
				pmpriv->bitmap_rates[i];
		}

	}

	LEAVE();
	return ret;
}

/**
 *  @brief  This function issues adapter specific commands
 *          to initialize firmware
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_PENDING or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_adapter_get_hw_spec(IN pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private priv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	/*
	 * This should be issued in the very first to config
	 *   SDIO_GPIO interrupt mode.
	 */
	if (wlan_set_sdio_gpio_int(priv) != MLAN_STATUS_SUCCESS) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	ret = wlan_prepare_cmd(priv, HostCmd_CMD_FUNC_INIT,
			       HostCmd_ACT_GEN_SET, 0, MNULL, MNULL);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

    /** DPD data dnld cmd prepare */
	if ((pmadapter->pdpd_data) && (pmadapter->dpd_data_len > 0)) {
		ret = wlan_prepare_cmd(priv, HostCmd_CMD_CFG_DATA,
				       HostCmd_ACT_GEN_SET, OID_TYPE_DPD, MNULL,
				       MNULL);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		pmadapter->pdpd_data = MNULL;
		pmadapter->dpd_data_len = 0;
	}
	if ((pmadapter->ptxpwr_data) && (pmadapter->txpwr_data_len > 0)) {
		ret = wlan_process_hostcmd_cfg(priv, pmadapter->ptxpwr_data,
					       pmadapter->txpwr_data_len);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		pmadapter->ptxpwr_data = MNULL;
		pmadapter->txpwr_data_len = 0;
	}
    /** Cal data dnld cmd prepare */
	if ((pmadapter->pcal_data) && (pmadapter->cal_data_len > 0)) {
		ret = wlan_prepare_cmd(priv, HostCmd_CMD_CFG_DATA,
				       HostCmd_ACT_GEN_SET, OID_TYPE_CAL, MNULL,
				       MNULL);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		pmadapter->pcal_data = MNULL;
		pmadapter->cal_data_len = 0;
	}
	/* Get FW region and cfp tables */
	if (pmadapter->init_para.fw_region) {
		ret = wlan_prepare_cmd(priv, HostCmd_CMD_CHAN_REGION_CFG,
				       HostCmd_ACT_GEN_GET, 0, MNULL, MNULL);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
	/*
	 * Get HW spec
	 */
	ret = wlan_prepare_cmd(priv, HostCmd_CMD_GET_HW_SPEC,
			       HostCmd_ACT_GEN_GET, 0, MNULL, MNULL);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	ret = MLAN_STATUS_PENDING;
done:
	LEAVE();
	return ret;
}

/**
 *  @brief  This function issues adapter specific commands
 *          to initialize firmware
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_PENDING or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_adapter_init_cmd(IN pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = MNULL;
#ifdef STA_SUPPORT
	pmlan_private pmpriv_sta = MNULL;
#endif

	ENTER();

	pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
#ifdef STA_SUPPORT
	pmpriv_sta = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_STA);
#endif

	/* Get fw wakeup method */
	if (pmpriv) {
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_802_11_FW_WAKE_METHOD,
				       HostCmd_ACT_GEN_GET, 0, MNULL, MNULL);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
#if defined(SYSKT_MULTI) && defined(OOB_WAKEUP) || defined(SUSPEND_SDIO_PULL_DOWN)
	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_SDIO_PULL_CTRL,
			       HostCmd_ACT_GEN_SET, 0, MNULL, MNULL);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
}
#endif

    /* Reconfigure tx buf size */
ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_RECONFIGURE_TX_BUFF,
		       HostCmd_ACT_GEN_SET, 0, MNULL,
		       &pmadapter->max_tx_buf_size);
if (ret) {
	ret = MLAN_STATUS_FAILURE;
	goto done;
}
#if defined(STA_SUPPORT)
if (pmpriv_sta && (pmpriv_sta->state_11d.user_enable_11d == ENABLE_11D)) {
	/* Send command to FW to enable 11d */
	ret = wlan_prepare_cmd(pmpriv_sta,
			       HostCmd_CMD_802_11_SNMP_MIB,
			       HostCmd_ACT_GEN_SET,
			       Dot11D_i,
			       MNULL, &pmpriv_sta->state_11d.user_enable_11d);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
}
#endif

#if defined(STA_SUPPORT)
if (pmpriv_sta && (pmadapter->ps_mode == Wlan802_11PowerModePSP)) {
	ret = wlan_prepare_cmd(pmpriv_sta, HostCmd_CMD_802_11_PS_MODE_ENH,
			       EN_AUTO_PS, BITMAP_STA_PS, MNULL, MNULL);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
}
#endif

if (pmadapter->init_auto_ds) {
	mlan_ds_auto_ds auto_ds;
	/* Enable auto deep sleep */
	auto_ds.idletime = pmadapter->idle_time;
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_PS_MODE_ENH,
			       EN_AUTO_PS, BITMAP_AUTO_DS, MNULL, &auto_ds);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
}
#define DEF_AUTO_NULL_PKT_PERIOD    30
if (pmpriv_sta) {
	t_u32 value = DEF_AUTO_NULL_PKT_PERIOD;
	ret = wlan_prepare_cmd(pmpriv_sta,
			       HostCmd_CMD_802_11_SNMP_MIB,
			       HostCmd_ACT_GEN_SET,
			       NullPktPeriod_i, MNULL, &value);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
}
if (pmadapter->init_para.indrstcfg != 0xffffffff) {
	mlan_ds_ind_rst_cfg ind_rst_cfg;
	ind_rst_cfg.ir_mode = pmadapter->init_para.indrstcfg & 0xff;
	ind_rst_cfg.gpio_pin = (pmadapter->init_para.indrstcfg & 0xff00) >> 8;
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_INDEPENDENT_RESET_CFG,
			       HostCmd_ACT_GEN_SET,
			       0, MNULL, (t_void *)&ind_rst_cfg);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
}

if (pmadapter->inact_tmo) {
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_PS_INACTIVITY_TIMEOUT,
			       HostCmd_ACT_GEN_SET, 0, MNULL,
			       &pmadapter->inact_tmo);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
}
if (pmadapter->init_para.drcs_chantime_mode) {
	mlan_ds_drcs_cfg drcs_init_cfg[2];
	drcs_init_cfg[0].chan_idx = 0x1;
	drcs_init_cfg[0].chantime =
		(t_u8)(pmadapter->init_para.drcs_chantime_mode >> 8);
	/* switchtime use default value in fw */
	drcs_init_cfg[0].switchtime = 10;
	drcs_init_cfg[0].undozetime = 5;
	drcs_init_cfg[0].mode = (t_u8)(pmadapter->init_para.drcs_chantime_mode);
	drcs_init_cfg[1].chan_idx = 0x2;
	drcs_init_cfg[1].chantime =
		(t_u8)(pmadapter->init_para.drcs_chantime_mode >> 24);
	/* switchtime use default value in fw */
	drcs_init_cfg[1].switchtime = 10;
	drcs_init_cfg[1].undozetime = 5;
	drcs_init_cfg[1].mode =
		(t_u8)(pmadapter->init_para.drcs_chantime_mode >> 16);
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_DRCS_CONFIG,
			       HostCmd_ACT_GEN_SET, 0, MNULL,
			       (t_void *)drcs_init_cfg);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
}
ret = MLAN_STATUS_PENDING;
done:
LEAVE();
return ret;
}

#ifdef RX_PACKET_COALESCE
/**
 *  @brief This function prepares command of rx_pkt_coalesce_cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */

mlan_status
wlan_cmd_rx_pkt_coalesce_cfg(IN pmlan_private pmpriv,
			     IN HostCmd_DS_COMMAND *cmd,
			     IN t_u16 cmd_action, IN t_void *pdata_buf)
{

	mlan_ds_misc_rx_packet_coalesce *rx_pkt_cfg =
		(mlan_ds_misc_rx_packet_coalesce *)pdata_buf;
	HostCmd_DS_RX_PKT_COAL_CFG *prx_coal_cfg =
		(HostCmd_DS_RX_PKT_COAL_CFG *)&cmd->params.rx_pkt_coal_cfg;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_RX_PKT_COALESCE_CFG);
	prx_coal_cfg->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		prx_coal_cfg->packet_threshold =
			wlan_cpu_to_le32(rx_pkt_cfg->packet_threshold);
		prx_coal_cfg->delay = wlan_cpu_to_le16(rx_pkt_cfg->delay);
		PRINTM(MCMND,
		       "Set RX coal config: packet threshold=%d delay=%d\n",
		       rx_pkt_cfg->packet_threshold, rx_pkt_cfg->delay);
		cmd->size =
			wlan_cpu_to_le16(S_DS_GEN +
					 sizeof(HostCmd_DS_RX_PKT_COAL_CFG));
	} else {
		cmd->size = wlan_cpu_to_le16(S_DS_GEN + sizeof(cmd_action));
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;

}

/**
 *  @brief This function handles the command response of RX_PACKET_COAL_CFG
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_rx_pkt_coalesce_cfg(IN pmlan_private pmpriv,
			     const IN HostCmd_DS_COMMAND *resp,
			     OUT mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *pcfg = MNULL;
	const HostCmd_DS_RX_PKT_COAL_CFG *presp_cfg =
		&resp->params.rx_pkt_coal_cfg;

	ENTER();

	if (pioctl_buf) {
		pcfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		pcfg->param.rx_coalesce.packet_threshold =
			wlan_le32_to_cpu(presp_cfg->packet_threshold);
		pcfg->param.rx_coalesce.delay =
			wlan_le16_to_cpu(presp_cfg->delay);
		PRINTM(MCMND,
		       "Get rx pkt coalesce info: packet threshold=%d delay=%d\n",
		       pcfg->param.rx_coalesce.packet_threshold,
		       pcfg->param.rx_coalesce.delay);
		pioctl_buf->buf_len = sizeof(mlan_ds_misc_rx_packet_coalesce);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

#endif
/**
 *  @brief This function handle the multi_chan info event
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pevent       A pointer to event buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_handle_event_multi_chan_info(IN pmlan_private pmpriv, pmlan_buffer pevent)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	t_u32 interfaces = 0;
	MrvlIEtypes_multi_chan_info_t *pmulti_chan_info = MNULL;
	MrvlIEtypes_multi_chan_group_info_t *pmulti_chan_grp_info = MNULL;
	int tlv_buf_left = pevent->data_len - sizeof(mlan_event_id);
	t_u16 tlv_type, tlv_len;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private intf_priv = MNULL;
	int num_intf = 0, bss_type = 0, bss_num = 0;
	MrvlIEtypesHeader_t *tlv = MNULL;

	ENTER();

	PRINTM(MEVENT, "multi channel event\n");
	pmulti_chan_info =
		(MrvlIEtypes_multi_chan_info_t *)(pevent->pbuf +
						  pevent->data_offset +
						  sizeof(mlan_event_id));
	if (tlv_buf_left < sizeof(MrvlIEtypes_multi_chan_info_t) ||
	    wlan_le16_to_cpu(pmulti_chan_info->header.type) !=
	    TLV_TYPE_MULTI_CHAN_INFO) {
		PRINTM(MERROR, "Invalid multi channel event\n");
		goto done;
	}

	pmadapter->mc_status = wlan_le16_to_cpu(pmulti_chan_info->status);
	PRINTM(MEVENT, "mc_status=%d\n", pmadapter->mc_status);
	tlv_buf_left -= sizeof(MrvlIEtypes_multi_chan_info_t);
	tlv = (MrvlIEtypesHeader_t *)pmulti_chan_info->tlv_buffer;

	while (tlv_buf_left >= (int)sizeof(MrvlIEtypesHeader_t)) {
		tlv_type = wlan_le16_to_cpu(tlv->type);
		tlv_len = wlan_le16_to_cpu(tlv->len);
		if ((sizeof(MrvlIEtypesHeader_t) + tlv_len) >
		    (unsigned int)tlv_buf_left) {
			PRINTM(MERROR, "wrong tlv: tlvLen=%d, tlvBufLeft=%d\n",
			       tlv_len, tlv_buf_left);
			break;
		}
		if (tlv_type != TLV_TYPE_MULTI_CHAN_GROUP_INFO_TLV_ID) {
			PRINTM(MERROR, "wrong tlv type:0x%x\n", tlv_type);
			break;
		}
		pmulti_chan_grp_info =
			(MrvlIEtypes_multi_chan_group_info_t *)tlv;
		PRINTM(MEVENT, "mc_info: groupid=%d chan=%d, numintf=%d\n",
		       pmulti_chan_grp_info->chan_group_id,
		       pmulti_chan_grp_info->chan_band_info.chan_num,
		       pmulti_chan_grp_info->num_intf);
		num_intf = pmulti_chan_grp_info->num_intf;
		for (interfaces = 0; interfaces < num_intf; interfaces++) {
			bss_type =
				pmulti_chan_grp_info->
				bss_type_numlist[interfaces] >> 4;
			bss_num =
				pmulti_chan_grp_info->
				bss_type_numlist[interfaces] & BSS_NUM_MASK;
			PRINTM(MEVENT, "intf%d: bss_type=%d bss_num=%d\n",
			       interfaces, bss_type, bss_num);
			intf_priv =
				wlan_get_priv_by_id(pmadapter, bss_num,
						    bss_type);
			if (intf_priv) {
			} else {
				PRINTM(MERROR,
				       "Invalid bss_type, bss_num in multi_channel event\n");
			}
		}

		tlv_buf_left -= (sizeof(MrvlIEtypesHeader_t) + tlv_len);
		tlv = (MrvlIEtypesHeader_t *)((t_u8 *)tlv + tlv_len +
					      sizeof(MrvlIEtypesHeader_t));
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares the command MULTI_CHAN_CFG
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   Command action: GET or SET
 *  @param pdata_buf    A pointer to new setting buf
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_multi_chan_cfg(IN pmlan_private pmpriv,
			IN HostCmd_DS_COMMAND *cmd,
			IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	mlan_ds_multi_chan_cfg *multi_chan_cfg =
		(mlan_ds_multi_chan_cfg *)pdata_buf;
	HostCmd_DS_MULTI_CHAN_CFG *pmchan_cfg =
		(HostCmd_DS_MULTI_CHAN_CFG *)&cmd->params.multi_chan_cfg;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_MULTI_CHAN_CONFIG);
	pmchan_cfg->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		pmchan_cfg->buffer_weight = multi_chan_cfg->buffer_weight;
		pmchan_cfg->channel_time =
			wlan_cpu_to_le32(multi_chan_cfg->channel_time);
		PRINTM(MCMND,
		       "Set multi-channel: buffer_weight=%d channel_time=%d\n",
		       multi_chan_cfg->buffer_weight,
		       multi_chan_cfg->channel_time);
		cmd->size =
			wlan_cpu_to_le16(S_DS_GEN +
					 sizeof(HostCmd_DS_MULTI_CHAN_CFG));
	} else {
		cmd->size = wlan_cpu_to_le16(S_DS_GEN + sizeof(cmd_action));
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of MULTI_CHAN_CFG
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_multi_chan_cfg(IN pmlan_private pmpriv,
			const IN HostCmd_DS_COMMAND *resp,
			OUT mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *pcfg = MNULL;
	const HostCmd_DS_MULTI_CHAN_CFG *presp_cfg =
		&resp->params.multi_chan_cfg;

	ENTER();

	if (pioctl_buf) {
		pcfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		pcfg->param.multi_chan_cfg.channel_time =
			wlan_le32_to_cpu(presp_cfg->channel_time);
		pcfg->param.multi_chan_cfg.buffer_weight =
			presp_cfg->buffer_weight;
		pcfg->param.multi_chan_cfg.tlv_len =
			resp->size - (sizeof(HostCmd_DS_GEN) +
				      sizeof(HostCmd_DS_MULTI_CHAN_CFG));
		PRINTM(MCMND,
		       "Get multi-channel: buffer_weight=%d channel_time=%d tlv_len=%d\n",
		       pcfg->param.multi_chan_cfg.buffer_weight,
		       pcfg->param.multi_chan_cfg.channel_time,
		       pcfg->param.multi_chan_cfg.tlv_len);
		memcpy(pmpriv->adapter, pcfg->param.multi_chan_cfg.tlv_buf,
		       presp_cfg->tlv_buf, pcfg->param.multi_chan_cfg.tlv_len);
		pioctl_buf->buf_len =
			sizeof(mlan_ds_multi_chan_cfg) +
			pcfg->param.multi_chan_cfg.tlv_len;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares the command MULTI_CHAN_POLICY
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   Command action: GET or SET
 *  @param pdata_buf    A pointer to new setting buf
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_multi_chan_policy(IN pmlan_private pmpriv,
			   IN HostCmd_DS_COMMAND *cmd,
			   IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	t_u16 policy = 0;
	HostCmd_DS_MULTI_CHAN_POLICY *pmulti_chan_policy =
		(HostCmd_DS_MULTI_CHAN_POLICY *)&cmd->params.multi_chan_policy;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_MULTI_CHAN_POLICY);
	pmulti_chan_policy->action = wlan_cpu_to_le16(cmd_action);
	cmd->size =
		wlan_cpu_to_le16(S_DS_GEN +
				 sizeof(HostCmd_DS_MULTI_CHAN_POLICY));
	if (cmd_action == HostCmd_ACT_GEN_SET) {
		policy = *((t_u16 *)pdata_buf);
		pmulti_chan_policy->policy = wlan_cpu_to_le16(policy);
		PRINTM(MCMND, "Set multi-channel policy: %d\n", policy);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of MULTI_CHAN_POLICY
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_multi_chan_policy(IN pmlan_private pmpriv,
			   const IN HostCmd_DS_COMMAND *resp,
			   OUT mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *pcfg = MNULL;
	const HostCmd_DS_MULTI_CHAN_POLICY *presp_cfg =
		&resp->params.multi_chan_policy;

	ENTER();

	if (pioctl_buf) {
		pcfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		pcfg->param.multi_chan_policy =
			wlan_le16_to_cpu(presp_cfg->policy);

		if (pioctl_buf->action == HostCmd_ACT_GEN_SET) {
			if (pcfg->param.multi_chan_policy)
				pmpriv->adapter->mc_policy = MTRUE;
			else
				pmpriv->adapter->mc_policy = MFALSE;
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares the command DRCD_CFG
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   Command action: GET or SET
 *  @param pdata_buf    A pointer to new setting buf
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_drcs_cfg(IN pmlan_private pmpriv,
		  IN HostCmd_DS_COMMAND *cmd,
		  IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	mlan_ds_drcs_cfg *drcs_cfg = (mlan_ds_drcs_cfg *) pdata_buf;
	HostCmd_DS_DRCS_CFG *pdrcs_cfg =
		(HostCmd_DS_DRCS_CFG *) & cmd->params.drcs_cfg;
	MrvlTypes_DrcsTimeSlice_t *channel_time_slicing =
		&pdrcs_cfg->time_slicing;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_DRCS_CONFIG);
	pdrcs_cfg->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		channel_time_slicing->header.type =
			wlan_cpu_to_le16(MRVL_DRCS_TIME_SLICE_TLV_ID);
		channel_time_slicing->header.len =
			wlan_cpu_to_le16(sizeof(MrvlTypes_DrcsTimeSlice_t) -
					 sizeof(MrvlIEtypesHeader_t));
		channel_time_slicing->chan_idx =
			wlan_cpu_to_le16(drcs_cfg->chan_idx);
		channel_time_slicing->chantime = drcs_cfg->chantime;
		channel_time_slicing->switchtime = drcs_cfg->switchtime;
		channel_time_slicing->undozetime = drcs_cfg->undozetime;
		channel_time_slicing->mode = drcs_cfg->mode;
		PRINTM(MCMND,
		       "Set multi-channel: chan_idx=%d chantime=%d switchtime=%d undozetime=%d mode=%d\n",
		       channel_time_slicing->chan_idx,
		       channel_time_slicing->chantime,
		       channel_time_slicing->switchtime,
		       channel_time_slicing->undozetime,
		       channel_time_slicing->mode);
		cmd->size =
			wlan_cpu_to_le16(S_DS_GEN +
					 sizeof(HostCmd_DS_DRCS_CFG));
		/* Set two channels different parameters */
		if (0x3 != channel_time_slicing->chan_idx) {
			drcs_cfg++;
			channel_time_slicing = pdrcs_cfg->drcs_buf;
			channel_time_slicing->header.type =
				wlan_cpu_to_le16(MRVL_DRCS_TIME_SLICE_TLV_ID);
			channel_time_slicing->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlTypes_DrcsTimeSlice_t) -
						 sizeof(MrvlIEtypesHeader_t));
			channel_time_slicing->chan_idx =
				wlan_cpu_to_le16(drcs_cfg->chan_idx);
			channel_time_slicing->chantime = drcs_cfg->chantime;
			channel_time_slicing->switchtime = drcs_cfg->switchtime;
			channel_time_slicing->undozetime = drcs_cfg->undozetime;
			channel_time_slicing->mode = drcs_cfg->mode;
			PRINTM(MCMND,
			       "Set multi-channel: chan_idx=%d chantime=%d switchtime=%d undozetime=%d mode=%d\n",
			       channel_time_slicing->chan_idx,
			       channel_time_slicing->chantime,
			       channel_time_slicing->switchtime,
			       channel_time_slicing->undozetime,
			       channel_time_slicing->mode);
			cmd->size +=
				wlan_cpu_to_le16(sizeof
						 (MrvlTypes_DrcsTimeSlice_t));
		}
	} else {
		cmd->size = wlan_cpu_to_le16(S_DS_GEN + sizeof(cmd_action));
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of DRCS_CFG
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_drcs_cfg(IN pmlan_private pmpriv,
		  const IN HostCmd_DS_COMMAND *resp,
		  OUT mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *pcfg = MNULL;
	const HostCmd_DS_DRCS_CFG *presp_cfg = &resp->params.drcs_cfg;
	const MrvlTypes_DrcsTimeSlice_t *channel_time_slicing =
		&presp_cfg->time_slicing;
	const MrvlTypes_DrcsTimeSlice_t *channel_time_slicing1 = MNULL;
	mlan_ds_drcs_cfg *drcs_cfg1 = MNULL;

	ENTER();

	if (pioctl_buf) {
		pcfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		if (wlan_le16_to_cpu(channel_time_slicing->header.type) !=
		    MRVL_DRCS_TIME_SLICE_TLV_ID ||
		    wlan_le16_to_cpu(channel_time_slicing->header.len) !=
		    sizeof(MrvlTypes_DrcsTimeSlice_t) -
		    sizeof(MrvlIEtypesHeader_t)) {
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		pcfg->param.drcs_cfg[0].chan_idx =
			wlan_le16_to_cpu(channel_time_slicing->chan_idx);
		pcfg->param.drcs_cfg[0].chantime =
			channel_time_slicing->chantime;
		pcfg->param.drcs_cfg[0].switchtime =
			channel_time_slicing->switchtime;
		pcfg->param.drcs_cfg[0].undozetime =
			channel_time_slicing->undozetime;
		pcfg->param.drcs_cfg[0].mode = channel_time_slicing->mode;
		PRINTM(MCMND,
		       "multi-channel: chan_idx=%d chantime=%d switchtime=%d undozetime=%d mode=%d\n",
		       pcfg->param.drcs_cfg[0].chan_idx,
		       channel_time_slicing->chantime,
		       channel_time_slicing->switchtime,
		       channel_time_slicing->undozetime,
		       channel_time_slicing->mode);
		pioctl_buf->buf_len = sizeof(mlan_ds_drcs_cfg);
		/*Channel for chan_idx 1 and 2 have different parameters */
		if (0x3 != pcfg->param.drcs_cfg[0].chan_idx) {
			channel_time_slicing1 = presp_cfg->drcs_buf;
			if (wlan_le16_to_cpu(channel_time_slicing1->header.type)
			    != MRVL_DRCS_TIME_SLICE_TLV_ID ||
			    wlan_le16_to_cpu(channel_time_slicing1->header.
					     len) !=
			    sizeof(MrvlTypes_DrcsTimeSlice_t) -
			    sizeof(MrvlIEtypesHeader_t)) {
				LEAVE();
				return MLAN_STATUS_FAILURE;
			}
			drcs_cfg1 =
				(mlan_ds_drcs_cfg *) & pcfg->param.drcs_cfg[1];
			drcs_cfg1->chan_idx =
				wlan_le16_to_cpu(channel_time_slicing1->
						 chan_idx);
			drcs_cfg1->chantime = channel_time_slicing1->chantime;
			drcs_cfg1->switchtime =
				channel_time_slicing1->switchtime;
			drcs_cfg1->undozetime =
				channel_time_slicing1->undozetime;
			drcs_cfg1->mode = channel_time_slicing1->mode;
			PRINTM(MCMND,
			       "multi-channel: chan_idx=%d chantime=%d switchtime=%d undozetime=%d mode=%d\n",
			       drcs_cfg1->chan_idx, drcs_cfg1->chantime,
			       drcs_cfg1->switchtime, drcs_cfg1->undozetime,
			       drcs_cfg1->mode);
			pioctl_buf->buf_len += sizeof(mlan_ds_drcs_cfg);
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of get_hw_spec.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pcmd         A pointer to HostCmd_DS_COMMAND structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_get_hw_spec(IN pmlan_private pmpriv, IN HostCmd_DS_COMMAND *pcmd)
{
	HostCmd_DS_GET_HW_SPEC *hw_spec = &pcmd->params.hw_spec;

	ENTER();

	pcmd->command = wlan_cpu_to_le16(HostCmd_CMD_GET_HW_SPEC);
	pcmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_GET_HW_SPEC) + S_DS_GEN);
	memcpy(pmpriv->adapter, hw_spec->permanent_addr, pmpriv->curr_addr,
	       MLAN_MAC_ADDR_LENGTH);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of sdio rx aggr command.
 *
 *  @param pcmd         A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   Command action: GET or SET
 *  @param pdata_buf    A pointer to new setting buf

 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_sdio_rx_aggr_cfg(IN HostCmd_DS_COMMAND *pcmd,
			  IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	HostCmd_DS_SDIO_SP_RX_AGGR_CFG *cfg = &pcmd->params.sdio_rx_aggr;

	pcmd->command = wlan_cpu_to_le16(HostCmd_CMD_SDIO_SP_RX_AGGR_CFG);
	pcmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_SDIO_SP_RX_AGGR_CFG) +
				 S_DS_GEN);
	cfg->action = cmd_action;
	if (cmd_action == HostCmd_ACT_GEN_SET)
		cfg->enable = *(t_u8 *)pdata_buf;
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of sdio rx aggr command
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_sdio_rx_aggr_cfg(IN pmlan_private pmpriv, IN HostCmd_DS_COMMAND *resp)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	HostCmd_DS_SDIO_SP_RX_AGGR_CFG *cfg = &resp->params.sdio_rx_aggr;

	pmadapter->sdio_rx_aggr_enable = cfg->enable;
	pmadapter->sdio_rx_block_size = wlan_le16_to_cpu(cfg->sdio_block_size);
	PRINTM(MMSG, "SDIO rx aggr: %d block_size=%d\n",
	       cfg->enable, pmadapter->sdio_rx_block_size);
	if (!pmadapter->sdio_rx_block_size)
		pmadapter->sdio_rx_aggr_enable = MFALSE;
	if (pmadapter->sdio_rx_aggr_enable) {
		pmadapter->max_sp_rx_size = SDIO_CMD53_MAX_SIZE;
		wlan_re_alloc_sdio_rx_mpa_buffer(pmadapter);
	}
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of set_cfg_data.
 *
 *  @param pmpriv       A pointer to mlan_private strcture
 *  @param pcmd         A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   Command action: GET or SET
 *  @param pdata_buf    A pointer to cal_data buf
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_cfg_data(IN pmlan_private pmpriv,
		  IN HostCmd_DS_COMMAND *pcmd,
		  IN t_u16 cmd_action, IN t_u32 cmd_oid, IN t_void *pdata_buf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	HostCmd_DS_802_11_CFG_DATA *pcfg_data = &(pcmd->params.cfg_data);
	pmlan_adapter pmadapter = pmpriv->adapter;
	t_u32 len;
	t_u32 data_offset;
	t_u8 *temp_pcmd = (t_u8 *)pcmd;

	ENTER();

	data_offset = S_DS_GEN + sizeof(HostCmd_DS_802_11_CFG_DATA);

	if ((cmd_oid == OID_TYPE_CAL) && (pmadapter->pcal_data) &&
	    (pmadapter->cal_data_len > 0)) {
		len = wlan_parse_cal_cfg((t_u8 *)pmadapter->pcal_data,
					 pmadapter->cal_data_len,
					 (t_u8 *)(temp_pcmd + data_offset));
	} else if ((cmd_oid == OID_TYPE_DPD) && (pmadapter->pdpd_data) &&
		   (pmadapter->dpd_data_len > 0)) {
		len = wlan_parse_cal_cfg((t_u8 *)pmadapter->pdpd_data,
					 pmadapter->dpd_data_len,
					 (t_u8 *)(temp_pcmd + data_offset));
	} else {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	pcfg_data->action = cmd_action;
	pcfg_data->type = cmd_oid;
	pcfg_data->data_len = len;

	pcmd->command = HostCmd_CMD_CFG_DATA;
	pcmd->size = pcfg_data->data_len + data_offset;

	pcmd->command = wlan_cpu_to_le16(pcmd->command);
	pcmd->size = wlan_cpu_to_le16(pcmd->size);

	pcfg_data->action = wlan_cpu_to_le16(pcfg_data->action);
	pcfg_data->type = wlan_cpu_to_le16(pcfg_data->type);
	pcfg_data->data_len = wlan_cpu_to_le16(pcfg_data->data_len);

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function handles the command response of set_cfg_data
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to A pointer to mlan_ioctl_req
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_cfg_data(IN pmlan_private pmpriv,
		  IN HostCmd_DS_COMMAND *resp, IN t_void *pioctl_buf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (resp->result != HostCmd_RESULT_OK) {
		PRINTM(MERROR, "CFG data cmd resp failed\n");
		ret = MLAN_STATUS_FAILURE;
	}
	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares command of mac_control.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pcmd         A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   Command action
 *  @param pdata_buf    A pointer to command information buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_mac_control(IN pmlan_private pmpriv,
		     IN HostCmd_DS_COMMAND *pcmd,
		     IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	HostCmd_DS_MAC_CONTROL *pmac = &pcmd->params.mac_ctrl;
	t_u32 action = *((t_u32 *)pdata_buf);

	ENTER();

	if (cmd_action != HostCmd_ACT_GEN_SET) {
		PRINTM(MERROR, "wlan_cmd_mac_control(): support SET only.\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	pcmd->command = wlan_cpu_to_le16(HostCmd_CMD_MAC_CONTROL);
	pcmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_MAC_CONTROL) + S_DS_GEN);
	pmac->action = wlan_cpu_to_le32(action);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of mac_control
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_mac_control(IN pmlan_private pmpriv,
		     IN HostCmd_DS_COMMAND *resp, IN mlan_ioctl_req *pioctl_buf)
{
	ENTER();
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of get_hw_spec
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_get_hw_spec(IN pmlan_private pmpriv,
		     IN HostCmd_DS_COMMAND *resp, IN t_void *pioctl_buf)
{
	HostCmd_DS_GET_HW_SPEC *hw_spec = &resp->params.hw_spec;
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 i;
	t_u16 left_len;
	t_u16 tlv_type = 0;
	t_u16 tlv_len = 0;
	MrvlIEtypes_fw_ver_info_t *api_rev = MNULL;
	t_u16 api_id = 0;
	MrvlIEtypesHeader_t *tlv = MNULL;
	pmlan_ioctl_req pioctl_req = (mlan_ioctl_req *)pioctl_buf;
	MrvlIEtypes_Max_Conn_t *tlv_max_conn = MNULL;

	ENTER();

	pmadapter->fw_cap_info = wlan_le32_to_cpu(hw_spec->fw_cap_info);
	pmadapter->fw_cap_info &= pmadapter->init_para.dev_cap_mask;

	PRINTM(MMSG, "fw_cap_info=0x%x, dev_cap_mask=0x%x\n",
	       wlan_le32_to_cpu(hw_spec->fw_cap_info),
	       pmadapter->init_para.dev_cap_mask);
#ifdef STA_SUPPORT
	if (IS_SUPPORT_MULTI_BANDS(pmadapter))
		pmadapter->fw_bands = (t_u8)GET_FW_DEFAULT_BANDS(pmadapter);
	else
		pmadapter->fw_bands = BAND_B;

	pmadapter->config_bands = pmadapter->fw_bands;
	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i])
			pmadapter->priv[i]->config_bands = pmadapter->fw_bands;
	}

	if (pmadapter->fw_bands & BAND_A) {
		if (pmadapter->fw_bands & BAND_GN) {
			pmadapter->config_bands |= BAND_AN;
			for (i = 0; i < pmadapter->priv_num; i++) {
				if (pmadapter->priv[i])
					pmadapter->priv[i]->config_bands |=
						BAND_AN;
			}

			pmadapter->fw_bands |= BAND_AN;
		}
		if ((pmadapter->fw_bands & BAND_AN)
			) {
			pmadapter->adhoc_start_band = BAND_A | BAND_AN;
			pmadapter->adhoc_11n_enabled = MTRUE;
		} else
			pmadapter->adhoc_start_band = BAND_A;
		pmpriv->adhoc_channel = DEFAULT_AD_HOC_CHANNEL_A;
	} else if ((pmadapter->fw_bands & BAND_GN)
		) {
		pmadapter->adhoc_start_band = BAND_G | BAND_B | BAND_GN;
		pmpriv->adhoc_channel = DEFAULT_AD_HOC_CHANNEL;
		pmadapter->adhoc_11n_enabled = MTRUE;
	} else if (pmadapter->fw_bands & BAND_G) {
		pmadapter->adhoc_start_band = BAND_G | BAND_B;
		pmpriv->adhoc_channel = DEFAULT_AD_HOC_CHANNEL;
	} else if (pmadapter->fw_bands & BAND_B) {
		pmadapter->adhoc_start_band = BAND_B;
		pmpriv->adhoc_channel = DEFAULT_AD_HOC_CHANNEL;
	}
#endif /* STA_SUPPORT */

	pmadapter->fw_release_number =
		wlan_le32_to_cpu(hw_spec->fw_release_number);
	pmadapter->number_of_antenna =
		wlan_le16_to_cpu(hw_spec->number_of_antenna) & 0x00ff;
	pmadapter->antinfo =
		(wlan_le16_to_cpu(hw_spec->number_of_antenna) & 0xff00) >> 8;
	PRINTM(MCMND, "num_ant=%d, antinfo=0x%x\n",
	       pmadapter->number_of_antenna, pmadapter->antinfo);

	PRINTM(MINFO, "GET_HW_SPEC: fw_release_number- 0x%X\n",
	       pmadapter->fw_release_number);
	PRINTM(MINFO, "GET_HW_SPEC: Permanent addr- " MACSTR "\n",
	       MAC2STR(hw_spec->permanent_addr));
	PRINTM(MINFO, "GET_HW_SPEC: hw_if_version=0x%X  version=0x%X\n",
	       wlan_le16_to_cpu(hw_spec->hw_if_version),
	       wlan_le16_to_cpu(hw_spec->version));

	if (pmpriv->curr_addr[0] == 0xff)
		memmove(pmadapter, pmpriv->curr_addr, hw_spec->permanent_addr,
			MLAN_MAC_ADDR_LENGTH);
	memmove(pmadapter, pmadapter->permanent_addr, hw_spec->permanent_addr,
		MLAN_MAC_ADDR_LENGTH);
	pmadapter->hw_dot_11n_dev_cap =
		wlan_le32_to_cpu(hw_spec->dot_11n_dev_cap);
	pmadapter->hw_dev_mcs_support = hw_spec->dev_mcs_support;
	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i])
			wlan_update_11n_cap(pmadapter->priv[i]);
	}

	wlan_show_dot11ndevcap(pmadapter, pmadapter->hw_dot_11n_dev_cap);
	wlan_show_devmcssupport(pmadapter, pmadapter->hw_dev_mcs_support);
	if (ISSUPP_BEAMFORMING(pmadapter->hw_dot_11n_dev_cap)) {
		PRINTM(MCMND, "Enable Beamforming\n");
		for (i = 0; i < pmadapter->priv_num; i++) {
			if (pmadapter->priv[i])
				pmadapter->priv[i]->tx_bf_cap =
					DEFAULT_11N_TX_BF_CAP;
		}
	}
	pmadapter->mp_end_port = wlan_le16_to_cpu(hw_spec->mp_end_port);

	for (i = 1; i <= (unsigned)(MAX_PORT - pmadapter->mp_end_port); i++)
		pmadapter->mp_data_port_mask &= ~(1 << (MAX_PORT - i));

	pmadapter->max_mgmt_ie_index =
		wlan_le16_to_cpu(hw_spec->mgmt_buf_count);
	PRINTM(MINFO, "GET_HW_SPEC: mgmt IE count=%d\n",
	       pmadapter->max_mgmt_ie_index);
	if (!pmadapter->max_mgmt_ie_index)
		pmadapter->max_mgmt_ie_index = MAX_MGMT_IE_INDEX;

	pmadapter->region_code = wlan_le16_to_cpu(hw_spec->region_code);
	for (i = 0; i < MRVDRV_MAX_REGION_CODE; i++) {
		/* Use the region code to search for the index */
		if (pmadapter->region_code == region_code_index[i])
			break;
	}
	/* If it's unidentified region code, use the default */
	if (i >= MRVDRV_MAX_REGION_CODE) {
		pmadapter->region_code = MRVDRV_DEFAULT_REGION_CODE;
		PRINTM(MWARN,
		       "unidentified region code, use the default (0x%02x)\n",
		       MRVDRV_DEFAULT_REGION_CODE);
	}
	/* Synchronize CFP code with region code */
	pmadapter->cfp_code_bg = pmadapter->region_code;
	pmadapter->cfp_code_a = pmadapter->region_code;

	if (pmadapter->fw_cap_info & ENHANCE_EXT_SCAN_ENABLE)
		pmadapter->ext_scan_enh = MTRUE;

	if ((pmadapter->fw_cap_info & SDIO_SP_RX_AGGR_ENABLE) &&
	    pmadapter->sdio_rx_aggr_enable) {
		t_u8 sdio_sp_rx_aggr = MTRUE;
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_SDIO_SP_RX_AGGR_CFG,
				       HostCmd_ACT_GEN_SET, 0, MNULL,
				       &sdio_sp_rx_aggr);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	} else {
		pmadapter->sdio_rx_aggr_enable = MFALSE;
		PRINTM(MCMND, "FW: SDIO rx aggr disabled 0x%x\n",
		       pmadapter->fw_cap_info);
	}

	if (wlan_set_regiontable(pmpriv, (t_u8)pmadapter->region_code,
				 pmadapter->fw_bands)) {
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_CMD_SCAN_FAIL;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
#ifdef STA_SUPPORT
	if (wlan_11d_set_universaltable(pmpriv, pmadapter->fw_bands)) {
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_CMD_SCAN_FAIL;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
#endif /* STA_SUPPORT */
	if (pmadapter->fw_cap_info & FW_CAPINFO_ECSA) {
		t_u8 ecsa_enable = MTRUE;
		pmadapter->ecsa_enable = MTRUE;
		PRINTM(MCMND, "pmadapter->ecsa_enable=%d\n",
		       pmadapter->ecsa_enable);
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_SNMP_MIB,
				       HostCmd_ACT_GEN_SET, ECSAEnable_i, MNULL,
				       &ecsa_enable);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
	if (pmadapter->fw_cap_info & FW_CAPINFO_GET_LOG) {
		pmadapter->getlog_enable = MTRUE;
		PRINTM(MCMND, "pmadapter->getlog_enable=%d\n",
		       pmadapter->getlog_enable);
	}

	left_len = resp->size - sizeof(HostCmd_DS_GET_HW_SPEC) - S_DS_GEN;
	tlv = (MrvlIEtypesHeader_t *)((t_u8 *)hw_spec +
				      sizeof(HostCmd_DS_GET_HW_SPEC));
	while (left_len > sizeof(MrvlIEtypesHeader_t)) {
		tlv_type = wlan_le16_to_cpu(tlv->type);
		tlv_len = wlan_le16_to_cpu(tlv->len);
		switch (tlv_type) {
		case TLV_TYPE_FW_VER_INFO:
			api_rev = (MrvlIEtypes_fw_ver_info_t *) tlv;
			api_id = wlan_le16_to_cpu(api_rev->api_id);
			switch (api_id) {
			case FW_API_VER_ID:
				pmadapter->fw_ver = api_rev->major_ver;
				pmadapter->fw_min_ver = api_rev->minor_ver;
				PRINTM(MCMND, "fw ver=%d.%d\n",
				       api_rev->major_ver, api_rev->minor_ver);
				break;
			case UAP_FW_API_VER_ID:
				pmadapter->uap_fw_ver = api_rev->major_ver;
				PRINTM(MCMND, "uap fw ver=%d.%d\n",
				       api_rev->major_ver, api_rev->minor_ver);
				break;
			case CHANRPT_API_VER_ID:
				pmadapter->chanrpt_param_bandcfg =
					api_rev->minor_ver;
				PRINTM(MCMND, "chanrpt api ver=%d.%d\n",
				       api_rev->major_ver, api_rev->minor_ver);
				break;
			default:
				break;
			}
			break;
		case TLV_TYPE_MAX_CONN:
			tlv_max_conn = (MrvlIEtypes_Max_Conn_t *) tlv;
			PRINTM(MMSG, "max_p2p_conn = %d, max_sta_conn = %d\n",
			       tlv_max_conn->max_p2p_conn,
			       tlv_max_conn->max_sta_conn);
			if (tlv_max_conn->max_p2p_conn &&
			    tlv_max_conn->max_sta_conn)
				pmadapter->max_sta_conn =
					MIN(tlv_max_conn->max_sta_conn,
					    tlv_max_conn->max_p2p_conn);
			else if (tlv_max_conn->max_sta_conn)
				pmadapter->max_sta_conn =
					tlv_max_conn->max_sta_conn;
			else if (tlv_max_conn->max_p2p_conn)
				pmadapter->max_sta_conn =
					tlv_max_conn->max_p2p_conn;
			else
				pmadapter->max_sta_conn = 0;
			break;
		default:
			break;
		}
		left_len -= (sizeof(MrvlIEtypesHeader_t) + tlv_len);
		tlv = (MrvlIEtypesHeader_t *)((t_u8 *)tlv + tlv_len +
					      sizeof(MrvlIEtypesHeader_t));
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares command of radio_control.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_802_11_radio_control(IN pmlan_private pmpriv,
			      IN HostCmd_DS_COMMAND *cmd,
			      IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	HostCmd_DS_802_11_RADIO_CONTROL *pradio_control = &cmd->params.radio;
	t_u32 radio_ctl;
	ENTER();
	cmd->size = wlan_cpu_to_le16((sizeof(HostCmd_DS_802_11_RADIO_CONTROL))
				     + S_DS_GEN);
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_RADIO_CONTROL);
	pradio_control->action = wlan_cpu_to_le16(cmd_action);
	memcpy(pmpriv->adapter, &radio_ctl, pdata_buf, sizeof(t_u32));
	pradio_control->control = wlan_cpu_to_le16((t_u16)radio_ctl);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of radio_control
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_802_11_radio_control(IN pmlan_private pmpriv,
			      IN HostCmd_DS_COMMAND *resp,
			      IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_802_11_RADIO_CONTROL *pradio_ctrl =
		(HostCmd_DS_802_11_RADIO_CONTROL *)&resp->params.radio;
	mlan_ds_radio_cfg *radio_cfg = MNULL;
	mlan_adapter *pmadapter = pmpriv->adapter;

	ENTER();
	pmadapter->radio_on = wlan_le16_to_cpu(pradio_ctrl->control);
	if (pioctl_buf) {
		radio_cfg = (mlan_ds_radio_cfg *)pioctl_buf->pbuf;
		radio_cfg->param.radio_on_off = (t_u32)pmadapter->radio_on;
		pioctl_buf->data_read_written = sizeof(mlan_ds_radio_cfg);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of remain_on_channel.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_remain_on_channel(IN pmlan_private pmpriv,
			   IN HostCmd_DS_COMMAND *cmd,
			   IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	HostCmd_DS_REMAIN_ON_CHANNEL *remain_channel =
		&cmd->params.remain_on_chan;
	mlan_ds_remain_chan *cfg = (mlan_ds_remain_chan *)pdata_buf;
	ENTER();
	cmd->size = wlan_cpu_to_le16((sizeof(HostCmd_DS_REMAIN_ON_CHANNEL))
				     + S_DS_GEN);
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_REMAIN_ON_CHANNEL);
	remain_channel->action = cmd_action;
	if (cmd_action == HostCmd_ACT_GEN_SET) {
		if (cfg->remove) {
			remain_channel->action = HostCmd_ACT_GEN_REMOVE;
		} else {
			remain_channel->bandcfg = cfg->bandcfg;
			remain_channel->channel = cfg->channel;
			remain_channel->remain_period =
				wlan_cpu_to_le32(cfg->remain_period);
		}
	}
	remain_channel->action = wlan_cpu_to_le16(remain_channel->action);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of remain_on_channel
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_remain_on_channel(IN pmlan_private pmpriv,
			   IN HostCmd_DS_COMMAND *resp,
			   IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_REMAIN_ON_CHANNEL *remain_channel =
		&resp->params.remain_on_chan;
	mlan_ds_radio_cfg *radio_cfg = MNULL;

	ENTER();
	if (pioctl_buf) {
		radio_cfg = (mlan_ds_radio_cfg *)pioctl_buf->pbuf;
		radio_cfg->param.remain_chan.status = remain_channel->status;
		radio_cfg->param.remain_chan.bandcfg = remain_channel->bandcfg;
		radio_cfg->param.remain_chan.channel = remain_channel->channel;
		radio_cfg->param.remain_chan.remain_period =
			wlan_le32_to_cpu(remain_channel->remain_period);
		pioctl_buf->data_read_written = sizeof(mlan_ds_radio_cfg);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

#ifdef WIFI_DIRECT_SUPPORT

/**
 *  @brief This function prepares command of wifi direct mode.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_wifi_direct_mode(IN pmlan_private pmpriv,
			  IN HostCmd_DS_COMMAND *cmd,
			  IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	HostCmd_DS_WIFI_DIRECT_MODE *wfd_mode = &cmd->params.wifi_direct_mode;
	t_u16 mode = *((t_u16 *)pdata_buf);
	ENTER();
	cmd->size = wlan_cpu_to_le16((sizeof(HostCmd_DS_WIFI_DIRECT_MODE))
				     + S_DS_GEN);
	cmd->command = wlan_cpu_to_le16(HOST_CMD_WIFI_DIRECT_MODE_CONFIG);
	wfd_mode->action = wlan_cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_SET)
		wfd_mode->mode = wlan_cpu_to_le16(mode);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of wifi direct mode
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_wifi_direct_mode(IN pmlan_private pmpriv,
			  IN HostCmd_DS_COMMAND *resp,
			  IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_WIFI_DIRECT_MODE *wfd_mode = &resp->params.wifi_direct_mode;
	mlan_ds_bss *bss = MNULL;

	ENTER();
	if (pioctl_buf) {
		bss = (mlan_ds_bss *)pioctl_buf->pbuf;
		bss->param.wfd_mode = wlan_le16_to_cpu(wfd_mode->mode);
		pioctl_buf->data_read_written = sizeof(mlan_ds_bss);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of p2p_params_config.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_p2p_params_config(IN pmlan_private pmpriv,
			   IN HostCmd_DS_COMMAND *cmd,
			   IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG *p2p_config =
		&cmd->params.p2p_params_config;
	mlan_ds_wifi_direct_config *cfg =
		(mlan_ds_wifi_direct_config *)pdata_buf;
	MrvlIEtypes_NoA_setting_t *pnoa_tlv = MNULL;
	MrvlIEtypes_OPP_PS_setting_t *popp_ps_tlv = MNULL;
	t_u8 *tlv = MNULL;
	ENTER();

	cmd->size = sizeof(HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG) + S_DS_GEN;
	cmd->command = wlan_cpu_to_le16(HOST_CMD_P2P_PARAMS_CONFIG);
	p2p_config->action = wlan_cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_SET) {
		tlv = (t_u8 *)p2p_config +
			sizeof(HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG);
		if (cfg->flags & WIFI_DIRECT_NOA) {
			pnoa_tlv = (MrvlIEtypes_NoA_setting_t *)tlv;
			pnoa_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_WIFI_DIRECT_NOA);
			pnoa_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_NoA_setting_t) -
						 sizeof(MrvlIEtypesHeader_t));
			pnoa_tlv->enable = cfg->noa_enable;
			pnoa_tlv->index = wlan_cpu_to_le16(cfg->index);
			pnoa_tlv->noa_count = cfg->noa_count;
			pnoa_tlv->noa_duration =
				wlan_cpu_to_le32(cfg->noa_duration);
			pnoa_tlv->noa_interval =
				wlan_cpu_to_le32(cfg->noa_interval);
			cmd->size += sizeof(MrvlIEtypes_NoA_setting_t);
			tlv += sizeof(MrvlIEtypes_NoA_setting_t);
			PRINTM(MCMND,
			       "Set NOA: enable=%d index=%d, count=%d, duration=%d interval=%d\n",
			       cfg->noa_enable, cfg->index, cfg->noa_count,
			       (int)cfg->noa_duration, (int)cfg->noa_interval);
		}
		if (cfg->flags & WIFI_DIRECT_OPP_PS) {
			popp_ps_tlv = (MrvlIEtypes_OPP_PS_setting_t *)tlv;
			popp_ps_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_WIFI_DIRECT_OPP_PS);
			popp_ps_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_OPP_PS_setting_t)
						 - sizeof(MrvlIEtypesHeader_t));

			popp_ps_tlv->enable = cfg->ct_window;
			popp_ps_tlv->enable |= cfg->opp_ps_enable << 7;
			cmd->size += sizeof(MrvlIEtypes_OPP_PS_setting_t);
			PRINTM(MCMND, "Set OPP_PS: enable=%d ct_win=%d\n",
			       cfg->opp_ps_enable, cfg->ct_window);
		}
	} else if (cmd_action == HostCmd_ACT_GEN_GET) {
		tlv = (t_u8 *)p2p_config +
			sizeof(HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG);
		if (cfg->flags & WIFI_DIRECT_NOA) {
			pnoa_tlv = (MrvlIEtypes_NoA_setting_t *)tlv;
			pnoa_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_WIFI_DIRECT_NOA);
			pnoa_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_NoA_setting_t) -
						 sizeof(MrvlIEtypesHeader_t));
			cmd->size += sizeof(MrvlIEtypes_NoA_setting_t);
			tlv += sizeof(MrvlIEtypes_NoA_setting_t);
		}

		if (cfg->flags & WIFI_DIRECT_OPP_PS) {
			popp_ps_tlv = (MrvlIEtypes_OPP_PS_setting_t *)tlv;
			popp_ps_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_WIFI_DIRECT_OPP_PS);
			popp_ps_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_OPP_PS_setting_t)
						 - sizeof(MrvlIEtypesHeader_t));
			cmd->size += sizeof(MrvlIEtypes_OPP_PS_setting_t);
		}
	}
	cmd->size = wlan_cpu_to_le16(cmd->size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of p2p_params_config
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_p2p_params_config(IN pmlan_private pmpriv,
			   IN HostCmd_DS_COMMAND *resp,
			   IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG *p2p_config =
		&resp->params.p2p_params_config;
	mlan_ds_misc_cfg *cfg = MNULL;
	MrvlIEtypes_NoA_setting_t *pnoa_tlv = MNULL;
	MrvlIEtypes_OPP_PS_setting_t *popp_ps_tlv = MNULL;
	MrvlIEtypesHeader_t *tlv = MNULL;
	t_u16 tlv_buf_left = 0;
	t_u16 tlv_type = 0;
	t_u16 tlv_len = 0;

	ENTER();
	if (wlan_le16_to_cpu(p2p_config->action) == HostCmd_ACT_GEN_GET) {
		if (pioctl_buf) {
			cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
			tlv = (MrvlIEtypesHeader_t *)((t_u8 *)p2p_config +
						      sizeof
						      (HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG));
			tlv_buf_left =
				resp->size -
				(sizeof(HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG) +
				 S_DS_GEN);
			while (tlv_buf_left >= sizeof(MrvlIEtypesHeader_t)) {
				tlv_type = wlan_le16_to_cpu(tlv->type);
				tlv_len = wlan_le16_to_cpu(tlv->len);
				if (tlv_buf_left <
				    (tlv_len + sizeof(MrvlIEtypesHeader_t))) {
					PRINTM(MERROR,
					       "Error processing p2p param config TLVs, bytes left < TLV length\n");
					break;
				}
				switch (tlv_type) {
				case TLV_TYPE_WIFI_DIRECT_NOA:
					pnoa_tlv =
						(MrvlIEtypes_NoA_setting_t *)
						tlv;
					cfg->param.p2p_config.flags |=
						WIFI_DIRECT_NOA;
					cfg->param.p2p_config.noa_enable =
						pnoa_tlv->enable;
					cfg->param.p2p_config.index =
						wlan_le16_to_cpu(pnoa_tlv->
								 index);
					cfg->param.p2p_config.noa_count =
						pnoa_tlv->noa_count;
					cfg->param.p2p_config.noa_duration =
						wlan_le32_to_cpu(pnoa_tlv->
								 noa_duration);
					cfg->param.p2p_config.noa_interval =
						wlan_le32_to_cpu(pnoa_tlv->
								 noa_interval);
					PRINTM(MCMND,
					       "Get NOA: enable=%d index=%d, count=%d, duration=%d interval=%d\n",
					       cfg->param.p2p_config.noa_enable,
					       cfg->param.p2p_config.index,
					       cfg->param.p2p_config.noa_count,
					       (int)cfg->param.p2p_config.
					       noa_duration,
					       (int)cfg->param.p2p_config.
					       noa_interval);
					break;
				case TLV_TYPE_WIFI_DIRECT_OPP_PS:
					popp_ps_tlv =
						(MrvlIEtypes_OPP_PS_setting_t *)
						tlv;
					cfg->param.p2p_config.flags |=
						WIFI_DIRECT_OPP_PS;
					cfg->param.p2p_config.opp_ps_enable =
						(popp_ps_tlv->
						 enable & 0x80) >> 7;
					cfg->param.p2p_config.ct_window =
						popp_ps_tlv->enable & 0x7f;
					PRINTM(MCMND,
					       "Get OPP_PS: enable=%d ct_win=%d\n",
					       cfg->param.p2p_config.
					       opp_ps_enable,
					       cfg->param.p2p_config.ct_window);
					break;
				default:
					break;
				}
				tlv_buf_left -=
					tlv_len + sizeof(MrvlIEtypesHeader_t);
				tlv = (MrvlIEtypesHeader_t *)((t_u8 *)tlv +
							      tlv_len +
							      sizeof
							      (MrvlIEtypesHeader_t));
			}
			pioctl_buf->data_read_written =
				sizeof(mlan_ds_wifi_direct_config);
		}
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}
#endif

/**
 *  @brief This function prepares command of hs wakeup reason.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_hs_wakeup_reason(IN pmlan_private pmpriv,
			  IN HostCmd_DS_COMMAND *cmd, IN t_void *pdata_buf)
{
	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_HS_WAKEUP_REASON);
	cmd->size =
		wlan_cpu_to_le16((sizeof(HostCmd_DS_HS_WAKEUP_REASON)) +
				 S_DS_GEN);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of
 *          hs wakeup reason
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_hs_wakeup_reason(IN pmlan_private pmpriv,
			  IN HostCmd_DS_COMMAND *resp,
			  IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_HS_WAKEUP_REASON *hs_wakeup_reason =
		(HostCmd_DS_HS_WAKEUP_REASON *)&resp->params.hs_wakeup_reason;
	mlan_ds_pm_cfg *pm_cfg = MNULL;

	ENTER();

	pm_cfg = (mlan_ds_pm_cfg *)pioctl_buf->pbuf;
	pm_cfg->param.wakeup_reason.hs_wakeup_reason =
		wlan_le16_to_cpu(hs_wakeup_reason->wakeup_reason);
	pioctl_buf->data_read_written = sizeof(mlan_ds_pm_cfg);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/*
 *  @brief This function prepares command of cwmode control.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_cw_mode_ctrl(IN pmlan_private pmpriv,
		      IN HostCmd_DS_COMMAND *cmd,
		      IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	HostCmd_DS_CW_MODE_CTRL *cwmode_ctrl = &cmd->params.cwmode;
	mlan_ds_cw_mode_ctrl *cw_mode = (mlan_ds_cw_mode_ctrl *) pdata_buf;
	ENTER();
	cmd->size = wlan_cpu_to_le16((sizeof(HostCmd_DS_CW_MODE_CTRL))
				     + S_DS_GEN);
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_CW_MODE_CTRL);
	cwmode_ctrl->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		cwmode_ctrl->mode = cw_mode->mode;
		cwmode_ctrl->channel = cw_mode->channel;
		cwmode_ctrl->chanInfo = cw_mode->chanInfo;
		cwmode_ctrl->txPower = wlan_cpu_to_le16(cw_mode->txPower);
		cwmode_ctrl->rateInfo = wlan_cpu_to_le32(cw_mode->rateInfo);
		cwmode_ctrl->pktLength = wlan_cpu_to_le16(cw_mode->pktLength);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/*
 *  @brief This function handles the command response of cwmode_ctrl
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_cw_mode_ctrl(IN pmlan_private pmpriv,
		      IN HostCmd_DS_COMMAND *resp,
		      IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_CW_MODE_CTRL *cwmode_resp = &resp->params.cwmode;
	mlan_ds_misc_cfg *misc = MNULL;

	ENTER();
	if (pioctl_buf) {
		misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc->param.cwmode.mode = cwmode_resp->mode;
		misc->param.cwmode.channel = cwmode_resp->channel;
		misc->param.cwmode.chanInfo = cwmode_resp->chanInfo;
		misc->param.cwmode.txPower =
			wlan_le16_to_cpu(cwmode_resp->txPower);
		misc->param.cwmode.rateInfo =
			wlan_le32_to_cpu(cwmode_resp->rateInfo);;
		misc->param.cwmode.pktLength =
			wlan_le16_to_cpu(cwmode_resp->pktLength);;
		pioctl_buf->data_read_written = sizeof(mlan_ds_misc_cfg);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of rf_antenna.
 *
 *  @param pmpriv   A pointer to mlan_private structure
 *  @param cmd      A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_802_11_rf_antenna(IN pmlan_private pmpriv,
			   IN HostCmd_DS_COMMAND *cmd,
			   IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	HostCmd_DS_802_11_RF_ANTENNA *pantenna = &cmd->params.antenna;
	mlan_ds_ant_cfg_1x1 *ant_cfg_1x1 = (mlan_ds_ant_cfg_1x1 *) pdata_buf;

	ENTER();
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_RF_ANTENNA);
	cmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_802_11_RF_ANTENNA) +
				 S_DS_GEN);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		pantenna->action = wlan_cpu_to_le16(HostCmd_ACT_SET_BOTH);
		pantenna->antenna_mode =
			wlan_cpu_to_le16((t_u16)ant_cfg_1x1->antenna);
		pantenna->evaluate_time =
			wlan_cpu_to_le16((t_u16)ant_cfg_1x1->evaluate_time);
	} else {
		pantenna->action = wlan_cpu_to_le16(HostCmd_ACT_GET_BOTH);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of rf_antenna
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_802_11_rf_antenna(IN pmlan_private pmpriv,
			   IN HostCmd_DS_COMMAND *resp,
			   IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_802_11_RF_ANTENNA *pantenna = &resp->params.antenna;
	t_u16 ant_mode = wlan_le16_to_cpu(pantenna->antenna_mode);
	t_u16 evaluate_time = wlan_le16_to_cpu(pantenna->evaluate_time);
	t_u16 current_antenna = wlan_le16_to_cpu(pantenna->current_antenna);
	mlan_ds_radio_cfg *radio = MNULL;

	ENTER();

	PRINTM(MINFO,
	       "RF_ANT_RESP: action = 0x%x, Mode = 0x%04x, Evaluate time = %d, Current antenna = %d\n",
	       wlan_le16_to_cpu(pantenna->action), ant_mode, evaluate_time,
	       current_antenna);

	if (pioctl_buf) {
		radio = (mlan_ds_radio_cfg *)pioctl_buf->pbuf;
		radio->param.ant_cfg_1x1.antenna = ant_mode;
		radio->param.ant_cfg_1x1.evaluate_time = evaluate_time;
		radio->param.ant_cfg_1x1.current_antenna = current_antenna;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of reg_access.
 *
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_reg_access(IN HostCmd_DS_COMMAND *cmd,
		    IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	mlan_ds_reg_rw *reg_rw;

	ENTER();

	reg_rw = (mlan_ds_reg_rw *)pdata_buf;
	switch (cmd->command) {
	case HostCmd_CMD_MAC_REG_ACCESS:
		{
			HostCmd_DS_MAC_REG_ACCESS *mac_reg;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_MAC_REG_ACCESS) +
						 S_DS_GEN);
			mac_reg =
				(HostCmd_DS_MAC_REG_ACCESS *)&cmd->params.
				mac_reg;
			mac_reg->action = wlan_cpu_to_le16(cmd_action);
			mac_reg->offset =
				wlan_cpu_to_le16((t_u16)reg_rw->offset);
			mac_reg->value = wlan_cpu_to_le32(reg_rw->value);
			break;
		}
	case HostCmd_CMD_BBP_REG_ACCESS:
		{
			HostCmd_DS_BBP_REG_ACCESS *bbp_reg;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_BBP_REG_ACCESS) +
						 S_DS_GEN);
			bbp_reg =
				(HostCmd_DS_BBP_REG_ACCESS *)&cmd->params.
				bbp_reg;
			bbp_reg->action = wlan_cpu_to_le16(cmd_action);
			bbp_reg->offset =
				wlan_cpu_to_le16((t_u16)reg_rw->offset);
			bbp_reg->value = (t_u8)reg_rw->value;
			break;
		}
	case HostCmd_CMD_RF_REG_ACCESS:
		{
			HostCmd_DS_RF_REG_ACCESS *rf_reg;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_RF_REG_ACCESS) +
						 S_DS_GEN);
			rf_reg = (HostCmd_DS_RF_REG_ACCESS *)&cmd->params.
				rf_reg;
			rf_reg->action = wlan_cpu_to_le16(cmd_action);
			rf_reg->offset =
				wlan_cpu_to_le16((t_u16)reg_rw->offset);
			rf_reg->value = (t_u8)reg_rw->value;
			break;
		}
	case HostCmd_CMD_CAU_REG_ACCESS:
		{
			HostCmd_DS_RF_REG_ACCESS *cau_reg;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_RF_REG_ACCESS) +
						 S_DS_GEN);
			cau_reg =
				(HostCmd_DS_RF_REG_ACCESS *)&cmd->params.rf_reg;
			cau_reg->action = wlan_cpu_to_le16(cmd_action);
			cau_reg->offset =
				wlan_cpu_to_le16((t_u16)reg_rw->offset);
			cau_reg->value = (t_u8)reg_rw->value;
			break;
		}
	case HostCmd_CMD_TARGET_ACCESS:
		{
			HostCmd_DS_TARGET_ACCESS *target;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_TARGET_ACCESS) +
						 S_DS_GEN);
			target = (HostCmd_DS_TARGET_ACCESS *)&cmd->params.
				target;
			target->action = wlan_cpu_to_le16(cmd_action);
			target->csu_target =
				wlan_cpu_to_le16(MLAN_CSU_TARGET_PSU);
			target->address =
				wlan_cpu_to_le16((t_u16)reg_rw->offset);
			target->data = (t_u8)reg_rw->value;
			break;
		}
	case HostCmd_CMD_802_11_EEPROM_ACCESS:
		{
			mlan_ds_read_eeprom *rd_eeprom =
				(mlan_ds_read_eeprom *)pdata_buf;
			HostCmd_DS_802_11_EEPROM_ACCESS *cmd_eeprom =
				(HostCmd_DS_802_11_EEPROM_ACCESS *)&cmd->params.
				eeprom;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_802_11_EEPROM_ACCESS)
						 + S_DS_GEN);
			cmd_eeprom->action = wlan_cpu_to_le16(cmd_action);
			cmd_eeprom->offset =
				wlan_cpu_to_le16(rd_eeprom->offset);
			cmd_eeprom->byte_count =
				wlan_cpu_to_le16(rd_eeprom->byte_count);
			cmd_eeprom->value = 0;
			break;
		}
	default:
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	cmd->command = wlan_cpu_to_le16(cmd->command);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of reg_access
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param type         The type of reg access (MAC, BBP or RF)
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_reg_access(mlan_adapter *pmadapter,
		    t_u16 type,
		    IN HostCmd_DS_COMMAND *resp, IN mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_reg_mem *reg_mem = MNULL;
	mlan_ds_reg_rw *reg_rw = MNULL;

	ENTER();

	if (pioctl_buf) {
		reg_mem = (mlan_ds_reg_mem *)pioctl_buf->pbuf;
		reg_rw = &reg_mem->param.reg_rw;
		switch (type) {
		case HostCmd_CMD_MAC_REG_ACCESS:
			{
				HostCmd_DS_MAC_REG_ACCESS *reg;
				reg = (HostCmd_DS_MAC_REG_ACCESS *)&resp->
					params.mac_reg;
				reg_rw->offset =
					(t_u32)wlan_le16_to_cpu(reg->offset);
				reg_rw->value = wlan_le32_to_cpu(reg->value);
				break;
			}
		case HostCmd_CMD_BBP_REG_ACCESS:
			{
				HostCmd_DS_BBP_REG_ACCESS *reg;
				reg = (HostCmd_DS_BBP_REG_ACCESS *)&resp->
					params.bbp_reg;
				reg_rw->offset =
					(t_u32)wlan_le16_to_cpu(reg->offset);
				reg_rw->value = (t_u32)reg->value;
				break;
			}

		case HostCmd_CMD_RF_REG_ACCESS:
			{
				HostCmd_DS_RF_REG_ACCESS *reg;
				reg = (HostCmd_DS_RF_REG_ACCESS *)&resp->params.
					rf_reg;
				reg_rw->offset =
					(t_u32)wlan_le16_to_cpu(reg->offset);
				reg_rw->value = (t_u32)reg->value;
				break;
			}
		case HostCmd_CMD_CAU_REG_ACCESS:
			{
				HostCmd_DS_RF_REG_ACCESS *reg;
				reg = (HostCmd_DS_RF_REG_ACCESS *)&resp->params.
					rf_reg;
				reg_rw->offset =
					(t_u32)wlan_le16_to_cpu(reg->offset);
				reg_rw->value = (t_u32)reg->value;
				break;
			}
		case HostCmd_CMD_TARGET_ACCESS:
			{
				HostCmd_DS_TARGET_ACCESS *reg;
				reg = (HostCmd_DS_TARGET_ACCESS *)&resp->params.
					target;
				reg_rw->offset =
					(t_u32)wlan_le16_to_cpu(reg->address);
				reg_rw->value = (t_u32)reg->data;
				break;
			}
		case HostCmd_CMD_802_11_EEPROM_ACCESS:
			{
				mlan_ds_read_eeprom *eeprom =
					&reg_mem->param.rd_eeprom;
				HostCmd_DS_802_11_EEPROM_ACCESS *cmd_eeprom =
					(HostCmd_DS_802_11_EEPROM_ACCESS *)
					&resp->params.eeprom;
				cmd_eeprom->byte_count =
					wlan_le16_to_cpu(cmd_eeprom->
							 byte_count);
				PRINTM(MINFO, "EEPROM read len=%x\n",
				       cmd_eeprom->byte_count);
				if (eeprom->byte_count < cmd_eeprom->byte_count) {
					eeprom->byte_count = 0;
					PRINTM(MINFO,
					       "EEPROM read return length is too big\n");
					pioctl_buf->status_code =
						MLAN_ERROR_CMD_RESP_FAIL;
					LEAVE();
					return MLAN_STATUS_FAILURE;
				}
				eeprom->offset =
					wlan_le16_to_cpu(cmd_eeprom->offset);
				eeprom->byte_count = cmd_eeprom->byte_count;
				if (eeprom->byte_count > 0) {
					memcpy(pmadapter, &eeprom->value,
					       &cmd_eeprom->value,
					       MIN(MAX_EEPROM_DATA,
						   eeprom->byte_count));
					HEXDUMP("EEPROM",
						(char *)&eeprom->value,
						MIN(MAX_EEPROM_DATA,
						    eeprom->byte_count));
				}
				break;
			}
		default:
			pioctl_buf->status_code = MLAN_ERROR_CMD_RESP_FAIL;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of mem_access.
 *
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_mem_access(IN HostCmd_DS_COMMAND *cmd,
		    IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	mlan_ds_mem_rw *mem_rw = (mlan_ds_mem_rw *)pdata_buf;
	HostCmd_DS_MEM_ACCESS *mem_access =
		(HostCmd_DS_MEM_ACCESS *)&cmd->params.mem;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_MEM_ACCESS);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_MEM_ACCESS) + S_DS_GEN);

	mem_access->action = wlan_cpu_to_le16(cmd_action);
	mem_access->addr = wlan_cpu_to_le32(mem_rw->addr);
	mem_access->value = wlan_cpu_to_le32(mem_rw->value);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of mem_access
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_mem_access(IN pmlan_private pmpriv,
		    IN HostCmd_DS_COMMAND *resp, IN mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_reg_mem *reg_mem = MNULL;
	mlan_ds_mem_rw *mem_rw = MNULL;
	HostCmd_DS_MEM_ACCESS *mem = (HostCmd_DS_MEM_ACCESS *)&resp->params.mem;

	ENTER();

	if (pioctl_buf) {
		reg_mem = (mlan_ds_reg_mem *)pioctl_buf->pbuf;
		mem_rw = &reg_mem->param.mem_rw;

		mem_rw->addr = wlan_le32_to_cpu(mem->addr);
		mem_rw->value = wlan_le32_to_cpu(mem->value);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
*
*  @brief This function handles coex events generated by firmware
*
*  @param priv A pointer to mlan_private structure
*  @param pevent   A pointer to event buf
*
*  @return     N/A
*/
void
wlan_bt_coex_wlan_param_update_event(pmlan_private priv, pmlan_buffer pevent)
{
	pmlan_adapter pmadapter = priv->adapter;
	MrvlIEtypesHeader_t *tlv = MNULL;
	MrvlIETypes_BtCoexAggrWinSize_t *pCoexWinsize = MNULL;
	MrvlIEtypes_BtCoexScanTime_t *pScantlv = MNULL;
	t_s32 len = pevent->data_len - sizeof(t_u32);
	t_u8 *pCurrent_ptr = pevent->pbuf + pevent->data_offset + sizeof(t_u32);
	t_u16 tlv_type, tlv_len;

	ENTER();

	while (len >= sizeof(MrvlIEtypesHeader_t)) {
		tlv = (MrvlIEtypesHeader_t *)pCurrent_ptr;
		tlv_len = wlan_le16_to_cpu(tlv->len);
		tlv_type = wlan_le16_to_cpu(tlv->type);
		if ((tlv_len + sizeof(MrvlIEtypesHeader_t)) > len)
			break;
		switch (tlv_type) {
		case TLV_BTCOEX_WL_AGGR_WINSIZE:
			pCoexWinsize = (MrvlIETypes_BtCoexAggrWinSize_t *) tlv;
			pmadapter->coex_win_size = pCoexWinsize->coex_win_size;
			pmadapter->coex_tx_win_size = pCoexWinsize->tx_win_size;
			pmadapter->coex_rx_win_size = pCoexWinsize->rx_win_size;
			wlan_coex_ampdu_rxwinsize(pmadapter);
			wlan_update_ampdu_txwinsize(pmadapter);
			break;
		case TLV_BTCOEX_WL_SCANTIME:
			pScantlv = (MrvlIEtypes_BtCoexScanTime_t *) tlv;
			pmadapter->coex_scan = pScantlv->coex_scan;
			pmadapter->coex_min_scan_time =
				wlan_le16_to_cpu(pScantlv->min_scan_time);
			pmadapter->coex_max_scan_time =
				wlan_le16_to_cpu(pScantlv->max_scan_time);
			break;
		default:
			break;
		}
		len -= tlv_len + sizeof(MrvlIEtypesHeader_t);
		pCurrent_ptr += tlv_len + sizeof(MrvlIEtypesHeader_t);
	}
	PRINTM(MEVENT,
	       "coex_scan=%d min_scan=%d coex_win=%d, tx_win=%d rx_win=%d\n",
	       pmadapter->coex_scan, pmadapter->coex_min_scan_time,
	       pmadapter->coex_win_size, pmadapter->coex_tx_win_size,
	       pmadapter->coex_rx_win_size);

	LEAVE();
}

/**
 *  @brief This function prepares command of supplicant pmk
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_802_11_supplicant_pmk(IN pmlan_private pmpriv,
			       IN HostCmd_DS_COMMAND *cmd,
			       IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	MrvlIEtypes_PMK_t *ppmk_tlv = MNULL;
	MrvlIEtypes_Passphrase_t *ppassphrase_tlv = MNULL;
	MrvlIEtypes_SsIdParamSet_t *pssid_tlv = MNULL;
	MrvlIEtypes_Bssid_t *pbssid_tlv = MNULL;
	HostCmd_DS_802_11_SUPPLICANT_PMK *pesupplicant_psk =
		&cmd->params.esupplicant_psk;
	t_u8 *ptlv_buffer = (t_u8 *)pesupplicant_psk->tlv_buffer;
	mlan_ds_sec_cfg *sec = (mlan_ds_sec_cfg *)pdata_buf;
	mlan_ds_passphrase *psk = MNULL;
	t_u8 zero_mac[] = { 0, 0, 0, 0, 0, 0 };
	t_u8 ssid_flag = 0, bssid_flag = 0, pmk_flag = 0, passphrase_flag = 0;
	t_u8 zero[MLAN_MAX_KEY_LENGTH] = { 0 };
	MrvlIEtypes_fw_roam_enable_t *proam_tlv = MNULL;
	MrvlIEtypes_keyParams_t *key_tlv = MNULL;
	int length = 0;
	t_u8 userset_passphrase = 0;

	ENTER();
	if (sec->multi_passphrase)
		psk = (mlan_ds_passphrase *)&sec->param.
			roam_passphrase[userset_passphrase];
	else
		psk = (mlan_ds_passphrase *)&sec->param.passphrase;
	if (cmd_action == HostCmd_ACT_GEN_REMOVE) {
		cmd->size =
			sizeof(HostCmd_DS_802_11_SUPPLICANT_PMK) + S_DS_GEN - 1;
		proam_tlv = (MrvlIEtypes_fw_roam_enable_t *) ptlv_buffer;
		proam_tlv->header.type = wlan_cpu_to_le16(TLV_TYPE_ROAM);
		proam_tlv->header.len =
			sizeof(MrvlIEtypes_fw_roam_enable_t) -
			sizeof(MrvlIEtypesHeader_t);
		proam_tlv->roam_enable = MTRUE;
		ptlv_buffer +=
			(proam_tlv->header.len + sizeof(MrvlIEtypesHeader_t));
		cmd->size +=
			(proam_tlv->header.len + sizeof(MrvlIEtypesHeader_t));
		proam_tlv->header.len = wlan_cpu_to_le16(proam_tlv->header.len);

		cmd->command = wlan_cpu_to_le16(HostCmd_CMD_SUPPLICANT_PMK);
		pesupplicant_psk->action = wlan_cpu_to_le16(cmd_action);
		pesupplicant_psk->cache_result = 0;
		cmd->size = wlan_cpu_to_le16(cmd->size);
		LEAVE();
		return MLAN_STATUS_SUCCESS;
	}

	/*
	 * Parse the rest of the buf here
	 *  1) <ssid="valid ssid"> - This will get the passphrase, AKMP
	 *     for specified ssid, if none specified then it will get all.
	 *     Eg: iwpriv <mlanX> passphrase 0:ssid=marvell
	 *  2) <psk="psk">:<passphrase="passphare">:<bssid="00:50:43:ef:23:f3">
	 *     <ssid="valid ssid"> - passphrase and psk cannot be provided to
	 *     the same SSID, Takes one SSID at a time, If ssid= is present
	 *     the it should contain a passphrase or psk. If no arguments are
	 *     provided then AKMP=802.1x, and passphrase should be provided
	 *     after association.
	 *     End of each parameter should be followed by a ':'(except for the
	 *     last parameter) as the delimiter. If ':' has to be used in
	 *     an SSID then a '/' should be preceded to ':' as a escape.
	 *     Eg:iwpriv <mlanX> passphrase
	 *               "1:ssid=mrvl AP:psk=abcdefgh:bssid=00:50:43:ef:23:f3"
	 *     iwpriv <mlanX> passphrase
	 *            "1:ssid=mrvl/: AP:psk=abcdefgd:bssid=00:50:43:ef:23:f3"
	 *     iwpriv <mlanX> passphrase "1:ssid=mrvlAP:psk=abcdefgd"
	 *  3) <ssid="valid ssid"> - This will clear the passphrase
	 *     for specified ssid, if none specified then it will clear all.
	 *     Eg: iwpriv <mlanX> passphrase 2:ssid=marvell
	 */

	/* -1 is for t_u8 TlvBuffer[1] as this should not be included */
	cmd->size = sizeof(HostCmd_DS_802_11_SUPPLICANT_PMK) + S_DS_GEN - 1;
	if (psk &&
	    memcmp(pmpriv->adapter, (t_u8 *)&psk->bssid, zero_mac,
		   sizeof(zero_mac))) {
		pbssid_tlv = (MrvlIEtypes_Bssid_t *)ptlv_buffer;
		pbssid_tlv->header.type = wlan_cpu_to_le16(TLV_TYPE_BSSID);
		pbssid_tlv->header.len = MLAN_MAC_ADDR_LENGTH;
		memcpy(pmpriv->adapter, pbssid_tlv->bssid, (t_u8 *)&psk->bssid,
		       MLAN_MAC_ADDR_LENGTH);
		ptlv_buffer +=
			(pbssid_tlv->header.len + sizeof(MrvlIEtypesHeader_t));
		cmd->size +=
			(pbssid_tlv->header.len + sizeof(MrvlIEtypesHeader_t));
		pbssid_tlv->header.len =
			wlan_cpu_to_le16(pbssid_tlv->header.len);
		bssid_flag = 1;
	}
	if (psk && (psk->psk_type == MLAN_PSK_PMK)) {
		ppmk_tlv = (MrvlIEtypes_PMK_t *)ptlv_buffer;
		ppmk_tlv->header.type = wlan_cpu_to_le16(TLV_TYPE_PMK);
		ppmk_tlv->header.len = MLAN_MAX_KEY_LENGTH;
		memcpy(pmpriv->adapter, ppmk_tlv->pmk, psk->psk.pmk.pmk,
		       MLAN_MAX_KEY_LENGTH);
		ptlv_buffer +=
			(ppmk_tlv->header.len + sizeof(MrvlIEtypesHeader_t));
		cmd->size +=
			(ppmk_tlv->header.len + sizeof(MrvlIEtypesHeader_t));
		ppmk_tlv->header.len = wlan_cpu_to_le16(ppmk_tlv->header.len);
		pmk_flag = 1;
		if (memcmp
		    (pmpriv->adapter, psk->psk.pmk.pmk_r0, zero,
		     MLAN_MAX_KEY_LENGTH)) {
			ppmk_tlv = (MrvlIEtypes_PMK_t *)ptlv_buffer;
			ppmk_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_PMK_R0);
			ppmk_tlv->header.len = MLAN_MAX_KEY_LENGTH;
			memcpy(pmpriv->adapter, ppmk_tlv->pmk,
			       psk->psk.pmk.pmk_r0, MLAN_MAX_KEY_LENGTH);
			ptlv_buffer +=
				(ppmk_tlv->header.len +
				 sizeof(MrvlIEtypesHeader_t));
			cmd->size +=
				(ppmk_tlv->header.len +
				 sizeof(MrvlIEtypesHeader_t));
			ppmk_tlv->header.len =
				wlan_cpu_to_le16(ppmk_tlv->header.len);
		}
		if (memcmp
		    (pmpriv->adapter, psk->psk.pmk.pmk_r0_name, zero,
		     MLAN_MAX_PMKR0_NAME_LENGTH)) {
			ppmk_tlv = (MrvlIEtypes_PMK_t *)ptlv_buffer;
			ppmk_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_PMK_R0_NAME);
			ppmk_tlv->header.len = MLAN_MAX_PMKR0_NAME_LENGTH;
			memcpy(pmpriv->adapter, ppmk_tlv->pmk,
			       psk->psk.pmk.pmk_r0_name,
			       MLAN_MAX_PMKR0_NAME_LENGTH);
			ptlv_buffer +=
				(ppmk_tlv->header.len +
				 sizeof(MrvlIEtypesHeader_t));
			cmd->size +=
				(ppmk_tlv->header.len +
				 sizeof(MrvlIEtypesHeader_t));
			ppmk_tlv->header.len =
				wlan_cpu_to_le16(ppmk_tlv->header.len);
		}
	}
	if (pmpriv->adapter->fw_roaming &&
	    (pmpriv->adapter->userset_passphrase ||
	     psk->psk_type == MLAN_PSK_PMK)) {
		proam_tlv = (MrvlIEtypes_fw_roam_enable_t *) ptlv_buffer;
		proam_tlv->header.type = wlan_cpu_to_le16(TLV_TYPE_ROAM);
		proam_tlv->header.len =
			sizeof(MrvlIEtypes_fw_roam_enable_t) -
			sizeof(MrvlIEtypesHeader_t);
		proam_tlv->roam_enable = MTRUE;
		proam_tlv->userset_passphrase =
			pmpriv->adapter->userset_passphrase;
		ptlv_buffer +=
			(proam_tlv->header.len + sizeof(MrvlIEtypesHeader_t));
		cmd->size +=
			(proam_tlv->header.len + sizeof(MrvlIEtypesHeader_t));
		proam_tlv->header.len = wlan_cpu_to_le16(proam_tlv->header.len);
	}
	do {
		if (pmpriv->adapter->userset_passphrase &&
		    sec->multi_passphrase) {
			key_tlv = (MrvlIEtypes_keyParams_t *) ptlv_buffer;
			key_tlv->header.type =
				wlan_cpu_to_le16
				(TLV_TYPE_ROAM_OFFLOAD_USER_SET_PMK);
			ptlv_buffer += sizeof(MrvlIEtypesHeader_t);
			cmd->size += sizeof(MrvlIEtypesHeader_t);
			length = cmd->size;
		}
		if (psk->ssid.ssid_len) {
			pssid_tlv = (MrvlIEtypes_SsIdParamSet_t *)ptlv_buffer;
			pssid_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_SSID);
			pssid_tlv->header.len =
				(t_u16)MIN(MLAN_MAX_SSID_LENGTH,
					   psk->ssid.ssid_len);
			memcpy(pmpriv->adapter, (t_u8 *)pssid_tlv->ssid,
			       (t_u8 *)psk->ssid.ssid, MIN(MLAN_MAX_SSID_LENGTH,
							   psk->ssid.ssid_len));
			ptlv_buffer +=
				(pssid_tlv->header.len +
				 sizeof(MrvlIEtypesHeader_t));
			cmd->size +=
				(pssid_tlv->header.len +
				 sizeof(MrvlIEtypesHeader_t));
			pssid_tlv->header.len =
				wlan_cpu_to_le16(pssid_tlv->header.len);
			ssid_flag = 1;
		}
		if (psk->psk_type == MLAN_PSK_PASSPHRASE) {
			ppassphrase_tlv =
				(MrvlIEtypes_Passphrase_t *)ptlv_buffer;
			ppassphrase_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_PASSPHRASE);
			ppassphrase_tlv->header.len =
				(t_u16)MIN(MLAN_MAX_PASSPHRASE_LENGTH,
					   psk->psk.passphrase.passphrase_len);
			memcpy(pmpriv->adapter, ppassphrase_tlv->passphrase,
			       psk->psk.passphrase.passphrase,
			       MIN(MLAN_MAX_PASSPHRASE_LENGTH,
				   psk->psk.passphrase.passphrase_len));
			ptlv_buffer +=
				(ppassphrase_tlv->header.len +
				 sizeof(MrvlIEtypesHeader_t));
			cmd->size +=
				(ppassphrase_tlv->header.len +
				 sizeof(MrvlIEtypesHeader_t));
			ppassphrase_tlv->header.len =
				wlan_cpu_to_le16(ppassphrase_tlv->header.len);
			passphrase_flag = 1;
		}
		if (key_tlv)
			key_tlv->header.len =
				wlan_cpu_to_le16(cmd->size - length);
		userset_passphrase++;
		psk = (mlan_ds_passphrase *)&sec->param.
			roam_passphrase[userset_passphrase];
	} while (psk && sec->multi_passphrase &&
		 userset_passphrase < pmpriv->adapter->userset_passphrase);
	pmpriv->adapter->userset_passphrase = 0;
	if ((cmd_action == HostCmd_ACT_GEN_SET) &&
	    ((ssid_flag || bssid_flag) && (!pmk_flag && !passphrase_flag))) {
		PRINTM(MERROR,
		       "Invalid case,ssid/bssid present without pmk or passphrase\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_SUPPLICANT_PMK);
	pesupplicant_psk->action = wlan_cpu_to_le16(cmd_action);
	pesupplicant_psk->cache_result = 0;
	cmd->size = wlan_cpu_to_le16(cmd->size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Handle the supplicant pmk response
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return        MLAN_STATUS_SUCCESS MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_802_11_supplicant_pmk(IN pmlan_private pmpriv,
			       IN HostCmd_DS_COMMAND *resp,
			       IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_802_11_SUPPLICANT_PMK *supplicant_pmk_resp =
		&resp->params.esupplicant_psk;
	mlan_ds_sec_cfg sec_buf;
	mlan_ds_sec_cfg *sec = MNULL;
	MrvlIEtypes_PMK_t *ppmk_tlv = MNULL;
	MrvlIEtypes_Passphrase_t *passphrase_tlv = MNULL;
	MrvlIEtypes_SsIdParamSet_t *pssid_tlv = MNULL;
	MrvlIEtypes_Bssid_t *pbssid_tlv = MNULL;
	t_u8 *tlv_buf = (t_u8 *)supplicant_pmk_resp->tlv_buffer;
	t_u16 action = wlan_le16_to_cpu(supplicant_pmk_resp->action);
	int tlv_buf_len = 0;
	t_u16 tlv;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	tlv_buf_len = resp->size - (sizeof(HostCmd_DS_802_11_SUPPLICANT_PMK) +
				    S_DS_GEN - 1);
	if (pioctl_buf) {
		if (((mlan_ds_bss *)pioctl_buf->pbuf)->sub_command ==
		    MLAN_OID_BSS_FIND_BSS)
			sec = &sec_buf;
		else
			sec = (mlan_ds_sec_cfg *)pioctl_buf->pbuf;
		if (action == HostCmd_ACT_GEN_GET) {
			while (tlv_buf_len > 0) {
				tlv = (*tlv_buf) | (*(tlv_buf + 1) << 8);
				if ((tlv != TLV_TYPE_SSID) &&
				    (tlv != TLV_TYPE_BSSID) &&
				    (tlv != TLV_TYPE_PASSPHRASE)
				    && (tlv != TLV_TYPE_PMK))
					break;
				switch (tlv) {
				case TLV_TYPE_SSID:
					pssid_tlv =
						(MrvlIEtypes_SsIdParamSet_t *)
						tlv_buf;
					pssid_tlv->header.len =
						wlan_le16_to_cpu(pssid_tlv->
								 header.len);
					memcpy(pmpriv->adapter,
					       sec->param.passphrase.ssid.ssid,
					       pssid_tlv->ssid,
					       MIN(MLAN_MAX_SSID_LENGTH,
						   pssid_tlv->header.len));
					sec->param.passphrase.ssid.ssid_len =
						MIN(MLAN_MAX_SSID_LENGTH,
						    pssid_tlv->header.len);
					tlv_buf +=
						pssid_tlv->header.len +
						sizeof(MrvlIEtypesHeader_t);
					tlv_buf_len -=
						(pssid_tlv->header.len +
						 sizeof(MrvlIEtypesHeader_t));
					break;
				case TLV_TYPE_BSSID:
					pbssid_tlv =
						(MrvlIEtypes_Bssid_t *)tlv_buf;
					pbssid_tlv->header.len =
						wlan_le16_to_cpu(pbssid_tlv->
								 header.len);
					memcpy(pmpriv->adapter,
					       &sec->param.passphrase.bssid,
					       pbssid_tlv->bssid,
					       MLAN_MAC_ADDR_LENGTH);
					tlv_buf +=
						pbssid_tlv->header.len +
						sizeof(MrvlIEtypesHeader_t);
					tlv_buf_len -=
						(pbssid_tlv->header.len +
						 sizeof(MrvlIEtypesHeader_t));
					break;
				case TLV_TYPE_PASSPHRASE:
					passphrase_tlv =
						(MrvlIEtypes_Passphrase_t *)
						tlv_buf;
					passphrase_tlv->header.len =
						wlan_le16_to_cpu
						(passphrase_tlv->header.len);
					sec->param.passphrase.psk_type =
						MLAN_PSK_PASSPHRASE;
					sec->param.passphrase.psk.passphrase.
						passphrase_len =
						passphrase_tlv->header.len;
					memcpy(pmpriv->adapter,
					       sec->param.passphrase.psk.
					       passphrase.passphrase,
					       passphrase_tlv->passphrase,
					       MIN(MLAN_MAX_PASSPHRASE_LENGTH,
						   passphrase_tlv->header.len));
					tlv_buf +=
						passphrase_tlv->header.len +
						sizeof(MrvlIEtypesHeader_t);
					tlv_buf_len -=
						(passphrase_tlv->header.len +
						 sizeof(MrvlIEtypesHeader_t));
					break;
				case TLV_TYPE_PMK:
					ppmk_tlv = (MrvlIEtypes_PMK_t *)tlv_buf;
					ppmk_tlv->header.len =
						wlan_le16_to_cpu(ppmk_tlv->
								 header.len);
					sec->param.passphrase.psk_type =
						MLAN_PSK_PMK;
					memcpy(pmpriv->adapter,
					       sec->param.passphrase.psk.pmk.
					       pmk, ppmk_tlv->pmk,
					       MIN(MLAN_MAX_KEY_LENGTH,
						   ppmk_tlv->header.len));
					tlv_buf +=
						ppmk_tlv->header.len +
						sizeof(MrvlIEtypesHeader_t);
					tlv_buf_len -=
						(ppmk_tlv->header.len +
						 sizeof(MrvlIEtypesHeader_t));
					break;

				}
			}
#ifdef STA_SUPPORT
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA &&
			    ((mlan_ds_bss *)pioctl_buf->pbuf)->sub_command ==
			    MLAN_OID_BSS_FIND_BSS) {
				wlan_set_ewpa_mode(pmpriv,
						   &sec->param.passphrase);
				ret = wlan_find_bss(pmpriv, pioctl_buf);
			}
#endif
		} else if (action == HostCmd_ACT_GEN_SET) {
			PRINTM(MINFO, "Esupp PMK set: enable ewpa query\n");
			pmpriv->ewpa_query = MTRUE;
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares command of independent reset.
 *
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_ind_rst_cfg(IN HostCmd_DS_COMMAND *cmd,
		     IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	mlan_ds_ind_rst_cfg *pdata_ind_rst = (mlan_ds_ind_rst_cfg *) pdata_buf;
	HostCmd_DS_INDEPENDENT_RESET_CFG *ind_rst_cfg =
		(HostCmd_DS_INDEPENDENT_RESET_CFG *) & cmd->params.ind_rst_cfg;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_INDEPENDENT_RESET_CFG);
	cmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_INDEPENDENT_RESET_CFG) +
				 S_DS_GEN);

	ind_rst_cfg->action = wlan_cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_SET) {
		ind_rst_cfg->ir_mode = pdata_ind_rst->ir_mode;
		ind_rst_cfg->gpio_pin = pdata_ind_rst->gpio_pin;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of independent reset
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_ind_rst_cfg(IN pmlan_private pmpriv,
		     IN HostCmd_DS_COMMAND *resp, IN mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *misc = MNULL;
	const HostCmd_DS_INDEPENDENT_RESET_CFG *ind_rst_cfg =
		(HostCmd_DS_INDEPENDENT_RESET_CFG *) & resp->params.ind_rst_cfg;

	ENTER();

	if (pioctl_buf) {
		misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;

		if (wlan_le16_to_cpu(ind_rst_cfg->action) ==
		    HostCmd_ACT_GEN_GET) {
			misc->param.ind_rst_cfg.ir_mode = ind_rst_cfg->ir_mode;
			misc->param.ind_rst_cfg.gpio_pin =
				ind_rst_cfg->gpio_pin;
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of ps inactivity timeout.
 *
 *  @param pmpriv      A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_ps_inactivity_timeout(IN pmlan_private pmpriv,
			       IN HostCmd_DS_COMMAND *cmd,
			       IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	t_u16 timeout = *((t_u16 *)pdata_buf);
	HostCmd_DS_802_11_PS_INACTIVITY_TIMEOUT *ps_inact_tmo =
		(HostCmd_DS_802_11_PS_INACTIVITY_TIMEOUT *) & cmd->params.
		ps_inact_tmo;

	ENTER();

	cmd->command =
		wlan_cpu_to_le16(HostCmd_CMD_802_11_PS_INACTIVITY_TIMEOUT);
	cmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_802_11_PS_INACTIVITY_TIMEOUT)
				 + S_DS_GEN);

	ps_inact_tmo->action = wlan_cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_SET)
		ps_inact_tmo->inact_tmo = wlan_cpu_to_le16(timeout);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of HostCmd_CMD_GET_TSF
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_get_tsf(pmlan_private pmpriv,
		 IN HostCmd_DS_COMMAND *cmd, IN t_u16 cmd_action)
{
	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_GET_TSF);
	cmd->size = wlan_cpu_to_le16((sizeof(HostCmd_DS_TSF)) + S_DS_GEN);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of HostCmd_CMD_GET_TSF
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_get_tsf(IN pmlan_private pmpriv,
		 IN HostCmd_DS_COMMAND *resp, IN mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	HostCmd_DS_TSF *tsf_pointer = (HostCmd_DS_TSF *) & resp->params.tsf;

	ENTER();
	if (pioctl_buf) {
		misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc_cfg->param.misc_tsf = wlan_le64_to_cpu(tsf_pointer->tsf);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of sending host_clock_cfg.
 *
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_host_clock_cfg(IN HostCmd_DS_COMMAND *cmd,
			IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	mlan_ds_host_clock *hostclk = (mlan_ds_host_clock *) pdata_buf;
	HostCmd_DS_HOST_CLOCK_CFG *host_clock =
		(HostCmd_DS_HOST_CLOCK_CFG *) & cmd->params.host_clock_cfg;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_HOST_CLOCK_CFG);
	cmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_HOST_CLOCK_CFG) + S_DS_GEN);

	host_clock->action = wlan_cpu_to_le16(cmd_action);
	host_clock->time = wlan_cpu_to_le64(hostclk->time);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of host_clock_cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_host_clock_cfg(IN pmlan_private pmpriv,
			IN HostCmd_DS_COMMAND *resp,
			IN mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *cfg = MNULL;
	mlan_ds_host_clock *hostclk = MNULL;
	HostCmd_DS_HOST_CLOCK_CFG *host_clock =
		(HostCmd_DS_HOST_CLOCK_CFG *) & resp->params.host_clock_cfg;
	mlan_adapter *pmadapter = pmpriv->adapter;
	t_u64 cmd_rtt;

	ENTER();

	if (pioctl_buf) {
		cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		hostclk = &cfg->param.host_clock;

		hostclk->time = wlan_le64_to_cpu(host_clock->time);
		hostclk->fw_time = wlan_le64_to_cpu(host_clock->time);
		cmd_rtt = (pmadapter->d2 - pmadapter->d1) / 2;
		pmadapter->host_bbu_clk_delta =
			wlan_le64_to_cpu(host_clock->host_bbu_clk_delta);
		PRINTM(MINFO, "HW time: %ld, Host Time: %ld, RTT: %ld\n",
		       host_clock->hw_time, hostclk->time, cmd_rtt);
		hostclk->fw_time = wlan_le64_to_cpu(host_clock->hw_time) /*- cmd_rtt*/ ;	// Not adjusting cmd_rtt gave better results with 802.1as
		hostclk->host_bbu_clk_delta = pmadapter->host_bbu_clk_delta;

		/* Indicate ioctl complete */
		pioctl_buf->data_read_written =
			sizeof(mlan_ds_misc_cfg) + MLAN_SUB_COMMAND_SIZE;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of chan_region_cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_chan_region_cfg(IN pmlan_private pmpriv,
			 IN HostCmd_DS_COMMAND *resp,
			 IN mlan_ioctl_req *pioctl_buf)
{
	t_u16 action;
	t_u16 tlv, tlv_buf_len, tlv_buf_left;
	MrvlIEtypesHeader_t *head;
	HostCmd_DS_CHAN_REGION_CFG *reg = MNULL;
	t_u8 *tlv_buf = MNULL;
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	reg = (HostCmd_DS_CHAN_REGION_CFG *) & resp->params.reg_cfg;
	if (!reg) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	action = wlan_le16_to_cpu(reg->action);
	if (action != HostCmd_ACT_GEN_GET) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	tlv_buf = (t_u8 *)reg + sizeof(*reg);
	tlv_buf_left = wlan_le16_to_cpu(resp->size) - S_DS_GEN - sizeof(*reg);

	/* Add FW cfp tables and region info */
	wlan_add_fw_cfp_tables(pmpriv, tlv_buf, tlv_buf_left);

	if (!pioctl_buf)
		goto done;

	if (!pioctl_buf->pbuf) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;

	while (tlv_buf_left >= sizeof(*head)) {
		head = (MrvlIEtypesHeader_t *)tlv_buf;
		tlv = wlan_le16_to_cpu(head->type);
		tlv_buf_len = wlan_le16_to_cpu(head->len);

		if (tlv_buf_left < (sizeof(*head) + tlv_buf_len))
			break;

		switch (tlv) {
		case TLV_TYPE_CHAN_ATTR_CFG:
			DBG_HEXDUMP(MCMD_D, "CHAN:",
				    (t_u8 *)head + sizeof(*head), tlv_buf_left);
			if (tlv_buf_len >
			    misc_cfg->param.custom_reg_domain.cfg_len) {
				tlv_buf_len =
					misc_cfg->param.custom_reg_domain.
					cfg_len;
			}
			misc_cfg->param.custom_reg_domain.cfg_len = tlv_buf_len;
			memcpy(pmpriv->adapter,
			       misc_cfg->param.custom_reg_domain.cfg_buf,
			       (t_u8 *)head + sizeof(*head), tlv_buf_len);
			pioctl_buf->data_read_written = tlv_buf_len;
			break;
		}

		tlv_buf += (sizeof(*head) + tlv_buf_len);
		tlv_buf_left -= (sizeof(*head) + tlv_buf_len);
	}
done:
	LEAVE();
	return ret;
}

#if defined(SYSKT_MULTI) && defined(OOB_WAKEUP) || defined(SUSPEND_SDIO_PULL_DOWN)
/**
 *  @brief This function prepares command of sdio_pull_ctl
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_sdio_pull_ctl(pmlan_private pmpriv,
		       IN HostCmd_DS_COMMAND *cmd, IN t_u16 cmd_action)
{
	HostCmd_DS_SDIO_PULL_CTRL *pull_ctrl = &cmd->params.sdio_pull_ctl;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_SDIO_PULL_CTRL);
	cmd->size =
		wlan_cpu_to_le16((sizeof(HostCmd_DS_SDIO_PULL_CTRL)) +
				 S_DS_GEN);

	memset(pmpriv->adapter, pull_ctrl, 0,
	       sizeof(HostCmd_DS_SDIO_PULL_CTRL));
	pull_ctrl->action = wlan_cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_SET) {
		pull_ctrl->pull_up = wlan_cpu_to_le16(DEFAULT_PULLUP_DELAY);
		pull_ctrl->pull_down = wlan_cpu_to_le16(DEFAULT_PULLDOWN_DELAY);
		pull_ctrl->gpio_pullup_req = DEFAULT_GPIO_PULLUP_REQ;
		pull_ctrl->gpio_pullup_ack = DEFAULT_GPIO_ACK_PULLUP;
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}
#endif

/**
 *  @brief This function sends fw dump event command to firmware.
 *
 *  @param pmpriv         A pointer to mlan_private structure
 *  @param cmd            HostCmd_DS_COMMAND structure
 *  @param cmd_action     the action: GET or SET
 *  @param pdata_buf      A void pointer to information buffer
 *  @return               N/A
 */
mlan_status
wlan_cmd_fw_dump_event(IN pmlan_private pmpriv,
		       IN HostCmd_DS_COMMAND *cmd,
		       IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_FW_DUMP_EVENT);
	cmd->size = S_DS_GEN;
	cmd->size = wlan_cpu_to_le16(cmd->size);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function sends boot sleep configure command to firmware.
 *
 *  @param pmpriv         A pointer to mlan_private structure
 *  @param cmd          Hostcmd ID
 *  @param cmd_action   Command action
 *  @param pdata_buf    A void pointer to information buffer
 *  @return             MLAN_STATUS_SUCCESS/ MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_boot_sleep(IN pmlan_private pmpriv,
		    IN HostCmd_DS_COMMAND *cmd,
		    IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	HostCmd_DS_BOOT_SLEEP *boot_sleep = MNULL;
	t_u16 enable = *(t_u16 *)pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_BOOT_SLEEP);
	boot_sleep = &cmd->params.boot_sleep;
	boot_sleep->action = wlan_cpu_to_le16(cmd_action);
	boot_sleep->enable = wlan_cpu_to_le16(enable);

	cmd->size = S_DS_GEN + sizeof(HostCmd_DS_BOOT_SLEEP);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of boot sleep cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_boot_sleep(IN pmlan_private pmpriv,
		    IN HostCmd_DS_COMMAND *resp, IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_BOOT_SLEEP *boot_sleep = &resp->params.boot_sleep;
	mlan_ds_misc_cfg *cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;

	ENTER();

	cfg->param.boot_sleep = wlan_le16_to_cpu(boot_sleep->enable);
	PRINTM(MCMND, "boot sleep cfg status %u", cfg->param.boot_sleep);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}
