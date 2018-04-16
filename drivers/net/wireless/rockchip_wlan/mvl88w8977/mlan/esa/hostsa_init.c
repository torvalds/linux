/** @file hostsa_init.c
 *
 *  @brief This file defines the initialize /free  APIs for authenticator and supplicant.
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#include "mlan.h"
#ifdef STA_SUPPORT
#include "mlan_join.h"
#endif
#include "mlan_ioctl.h"
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_init.h"
#include "mlan_wmm.h"
#include "hostsa_def.h"

#include "authenticator_api.h"

/*********************
	Local Variables
 *********************/

/*********************
	Global Variables
 *********************/

/*********************
	Local Functions
 *********************/

/*********************
	Global Functions
 *********************/

/*********************
	Utility Handler
 *********************/

/**
 *  @brief alloc mlan buffer
 *
 *  @param pmlan_adapter   A void pointer
 *  @param data_len            request buffer len
 *  @param head_room        head room len
 *  @param malloc_flag        flag for mlan buffer

 *  @return          pointer to mlan buffer
 */
pmlan_buffer
hostsa_alloc_mlan_buffer(t_void *pmlan_adapter,
			 t_u32 data_len, t_u32 head_room, t_u32 malloc_flag)
{
	mlan_adapter *pmadapter = (mlan_adapter *)pmlan_adapter;
	pmlan_buffer newbuf = MNULL;

	newbuf = wlan_alloc_mlan_buffer(pmadapter, data_len, head_room,
					malloc_flag);

	return newbuf;
}

/**
 *  @brief free mlan buffer
 *
 *  @param pmlan_adapter   A void pointer
 *  @param pmbuf          a pointer to mlan buffer
 *
 *  @return
 */
void
hostsa_free_mlan_buffer(t_void *pmlan_adapter, mlan_buffer *pmbuf)
{
	mlan_adapter *pmadapter = (mlan_adapter *)pmlan_adapter;

	ENTER();

	wlan_free_mlan_buffer(pmadapter, pmbuf);

	LEAVE();
}

/**
 *  @brief send packet
 *
 *  @param pmlan_private   A void pointer
 *  @param pmbuf          a pointer to mlan buffer
 *  @param frameLen     paket len
 *
 *  @return
 */
void
hostsa_tx_packet(t_void *pmlan_private, pmlan_buffer pmbuf, t_u16 frameLen)
{
	mlan_private *pmpriv = (mlan_private *)pmlan_private;
	mlan_adapter *pmadapter = pmpriv->adapter;

	ENTER();

	pmbuf->bss_index = pmpriv->bss_index;
	pmbuf->data_len = frameLen;

	wlan_add_buf_bypass_txqueue(pmadapter, pmbuf);
	wlan_recv_event(pmpriv, MLAN_EVENT_ID_DRV_DEFER_HANDLING, MNULL);

	LEAVE();
}

/**
 *  @brief set key to fw
 *
 *  @param pmlan_private   A void pointer
 *  @param encrypt_key     key structure
 *
 *  @return
 */
void
wlan_set_encrypt_key(t_void *pmlan_private, mlan_ds_encrypt_key *encrypt_key)
{
	mlan_private *priv = (mlan_private *)pmlan_private;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (!encrypt_key->key_len) {
		PRINTM(MCMND, "Skip set key with key_len = 0\n");
		LEAVE();
		return;
	}

	ret = wlan_prepare_cmd(priv,
			       HostCmd_CMD_802_11_KEY_MATERIAL,
			       HostCmd_ACT_GEN_SET,
			       KEY_INFO_ENABLED, MNULL, encrypt_key);

	if (ret == MLAN_STATUS_SUCCESS)
		wlan_recv_event(priv, MLAN_EVENT_ID_DRV_DEFER_HANDLING, MNULL);

	LEAVE();

}

/**
 *  @brief clear key to fw
 *
 *  @param pmlan_private   A void pointer
 *
 *  @return
 */
void
wlan_clr_encrypt_key(t_void *pmlan_private)
{
	mlan_private *priv = (mlan_private *)pmlan_private;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_encrypt_key encrypt_key;

	ENTER();

	memset(priv->adapter, &encrypt_key, 0, sizeof(mlan_ds_encrypt_key));
	encrypt_key.key_disable = MTRUE;
	encrypt_key.key_flags = KEY_FLAG_REMOVE_KEY;

	ret = wlan_prepare_cmd(priv,
			       HostCmd_CMD_802_11_KEY_MATERIAL,
			       HostCmd_ACT_GEN_SET,
			       KEY_INFO_ENABLED, MNULL, &encrypt_key);

	if (ret == MLAN_STATUS_SUCCESS)
		wlan_recv_event(priv, MLAN_EVENT_ID_DRV_DEFER_HANDLING, MNULL);

	LEAVE();

}

/**
 *  @brief send deauth frame
 *
 *  @param pmlan_private   A void pointer
 *  @param addr     destination mac address
 *  @param reason  deauth reason
 *
 *  @return
 */
void
hostsa_SendDeauth(t_void *pmlan_private, t_u8 *addr, t_u16 reason)
{
	mlan_private *pmpriv = (mlan_private *)pmlan_private;
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_deauth_param deauth;

	ENTER();

	memcpy(pmadapter, deauth.mac_addr, addr, MLAN_MAC_ADDR_LENGTH);
	deauth.reason_code = reason;

	ret = wlan_prepare_cmd(pmpriv,
			       HOST_CMD_APCMD_STA_DEAUTH,
			       HostCmd_ACT_GEN_SET,
			       0, MNULL, (t_void *)&deauth);
	if (ret == MLAN_STATUS_SUCCESS)
		wlan_recv_event(pmpriv, MLAN_EVENT_ID_DRV_DEFER_HANDLING,
				MNULL);

	LEAVE();
}

/**
 *  @brief deauth all connected stations
 *
 *  @param pmlan_private   A void pointer
 *  @param reason  deauth reason
 *
 *  @return
 */
void
ApDisAssocAllSta(void *pmlan_private, t_u16 reason)
{
	mlan_private *pmpriv = (mlan_private *)pmlan_private;
	mlan_adapter *pmadapter = pmpriv->adapter;
	sta_node *sta_ptr;

	ENTER();

	sta_ptr = (sta_node *)util_peek_list(pmadapter->pmoal_handle,
					     &pmpriv->sta_list,
					     pmadapter->callbacks.
					     moal_spin_lock,
					     pmadapter->callbacks.
					     moal_spin_unlock);

	if (!sta_ptr) {
		LEAVE();
		return;
	}

	while (sta_ptr != (sta_node *)&pmpriv->sta_list) {
		hostsa_SendDeauth((t_void *)pmpriv, sta_ptr->mac_addr, reason);
		sta_ptr = sta_ptr->pnext;
	}

	LEAVE();
}

/**
 *  @brief get station entry
 *
 *  @param pmlan_private   A void pointer
 *  @param mac  pointer to station mac address
 *  @param ppconPtr    pointer to pointer to connection
 *
 *  @return
 */
void
Hostsa_get_station_entry(t_void *pmlan_private, t_u8 *mac, t_void **ppconPtr)
{
	mlan_private *priv = (mlan_private *)pmlan_private;
	sta_node *sta_ptr = MNULL;

	ENTER();

	sta_ptr = wlan_get_station_entry(priv, mac);
	if (sta_ptr)
		*ppconPtr = sta_ptr->cm_connectioninfo;
	else
		*ppconPtr = MNULL;

	LEAVE();
}

/**
 *  @brief find a connection
 *
 *  @param pmlan_private   A void pointer
 *  @param ppconPtr         a pointer to pointer to connection
 *  @param ppsta_node   a pointer to pointer to sta node
 *
 *  @return
 */
void
Hostsa_find_connection(t_void *pmlan_private, t_void **ppconPtr,
		       t_void **ppsta_node)
{
	mlan_private *priv = (mlan_private *)pmlan_private;
	sta_node *sta_ptr = MNULL;

	ENTER();

	sta_ptr = (sta_node *)util_peek_list(priv->adapter->pmoal_handle,
					     &priv->sta_list,
					     priv->adapter->callbacks.
					     moal_spin_lock,
					     priv->adapter->callbacks.
					     moal_spin_unlock);

	if (!sta_ptr) {
		LEAVE();
		return;
	}

	*ppsta_node = (t_void *)sta_ptr;
	*ppconPtr = sta_ptr->cm_connectioninfo;

	LEAVE();
}

/**
 *  @brief find next connection
 *
 *  @param pmlan_private   A void pointer
 *  @param ppconPtr         a pointer to pointer to connection
 *  @param ppsta_node   a pointer to pointer to sta node
 *
 *  @return
 */
void
Hostsa_find_next_connection(t_void *pmlan_private, t_void **ppconPtr,
			    t_void **ppsta_node)
{
	mlan_private *priv = (mlan_private *)pmlan_private;
	sta_node *sta_ptr = (sta_node *)*ppsta_node;

	ENTER();
	if (sta_ptr != (sta_node *)&priv->sta_list)
		sta_ptr = sta_ptr->pnext;

	*ppsta_node = MNULL;
	*ppconPtr = MNULL;
	if ((sta_ptr != MNULL) && (sta_ptr != (sta_node *)&priv->sta_list)) {
		*ppsta_node = (t_void *)sta_ptr;
		*ppconPtr = sta_ptr->cm_connectioninfo;
	}
	LEAVE();
}

/**
 *  @brief set management ie for beacon or probe response
 *
 *  @param pmlan_private   A void pointer
 *  @param pbuf                 ie buf
 *  @param len                   ie len
 *  @param clearIE             clear ie
 *
 *  @return
 */
void
Hostsa_set_mgmt_ie(t_void *pmlan_private, t_u8 *pbuf, t_u16 len, t_u8 clearIE)
{
	mlan_private *priv = (mlan_private *)pmlan_private;
	mlan_adapter *pmadapter = priv->adapter;
	mlan_ioctl_req *pioctl_req = MNULL;
	mlan_ds_misc_cfg *pds_misc_cfg = MNULL;
	custom_ie *pmgmt_ie = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (pmadapter == MNULL) {
		LEAVE();
		return;
	}

	/* allocate buffer for mlan_ioctl_req and mlan_ds_misc_cfg */
	/* FYI - will be freed as part of cmd_response handler */
	ret = pmadapter->callbacks.moal_malloc(pmadapter->pmoal_handle,
					       sizeof(mlan_ioctl_req) +
					       sizeof(mlan_ds_misc_cfg),
					       MLAN_MEM_DEF,
					       (t_u8 **)&pioctl_req);
	if ((ret != MLAN_STATUS_SUCCESS) || !pioctl_req) {
		PRINTM(MERROR, "%s(): Could not allocate ioctl req\n",
		       __func__);
		LEAVE();
		return;
	}
	pds_misc_cfg = (mlan_ds_misc_cfg *)((t_u8 *)pioctl_req +
					    sizeof(mlan_ioctl_req));

	/* prepare mlan_ioctl_req */
	memset(pmadapter, pioctl_req, 0x00, sizeof(mlan_ioctl_req));
	pioctl_req->req_id = MLAN_IOCTL_MISC_CFG;
	pioctl_req->action = MLAN_ACT_SET;
	pioctl_req->bss_index = priv->bss_index;
	pioctl_req->pbuf = (t_u8 *)pds_misc_cfg;
	pioctl_req->buf_len = sizeof(mlan_ds_misc_cfg);

	/* prepare mlan_ds_misc_cfg */
	memset(pmadapter, pds_misc_cfg, 0x00, sizeof(mlan_ds_misc_cfg));
	pds_misc_cfg->sub_command = MLAN_OID_MISC_CUSTOM_IE;
	pds_misc_cfg->param.cust_ie.type = TLV_TYPE_MGMT_IE;
	pds_misc_cfg->param.cust_ie.len = (sizeof(custom_ie) - MAX_IE_SIZE);

	/* configure custom_ie api settings */
	pmgmt_ie = (custom_ie *)&pds_misc_cfg->param.cust_ie.ie_data_list[0];
	pmgmt_ie->ie_index = 0xffff;	/* Auto index */
	pmgmt_ie->ie_length = len;
	pmgmt_ie->mgmt_subtype_mask = MBIT(8) | MBIT(5);	/* add IE for BEACON | PROBE_RSP */
	if (clearIE)
		pmgmt_ie->mgmt_subtype_mask = 0;
	memcpy(pmadapter, pmgmt_ie->ie_buffer, pbuf, len);

	pds_misc_cfg->param.cust_ie.len += pmgmt_ie->ie_length;

	DBG_HEXDUMP(MCMD_D, "authenticator: RSN or WPA IE",
		    (t_u8 *)pmgmt_ie, pds_misc_cfg->param.cust_ie.len);

	ret = wlan_misc_ioctl_custom_ie_list(pmadapter, pioctl_req, MFALSE);

	if (ret != MLAN_STATUS_SUCCESS && ret != MLAN_STATUS_PENDING) {
		PRINTM(MERROR,
		       "%s(): Could not set IE for priv=%p [priv_bss_idx=%d]!\n",
		       __func__, priv, priv->bss_index);
		/* TODO: how to handle this error case??  ignore & continue? */
	}
	/* free ioctl buffer memory before we leave */
	pmadapter->callbacks.moal_mfree(pmadapter->pmoal_handle,
					(t_u8 *)pioctl_req);

}

t_void
StaControlledPortOpen(t_void *pmlan_private)
{
	mlan_private *priv = (mlan_private *)pmlan_private;

	PRINTM(MERROR, "StaControlledPortOpen\n");
	if (priv->port_ctrl_mode == MTRUE) {
		PRINTM(MINFO, "PORT_REL: port_status = OPEN\n");
		priv->port_open = MTRUE;
	}
	priv->adapter->scan_block = MFALSE;
	wlan_recv_event(priv, MLAN_EVENT_ID_FW_PORT_RELEASE, MNULL);
}

void
hostsa_StaSendDeauth(t_void *pmlan_private, t_u8 *addr, t_u16 reason)
{
	mlan_private *pmpriv = (mlan_private *)pmlan_private;
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_deauth_param deauth;

	ENTER();

	memcpy(pmadapter, deauth.mac_addr, addr, MLAN_MAC_ADDR_LENGTH);
	deauth.reason_code = reason;

	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11_DEAUTHENTICATE,
			       HostCmd_ACT_GEN_SET,
			       0, MNULL, (t_void *)&deauth);
	if (ret == MLAN_STATUS_SUCCESS)
		wlan_recv_event(pmpriv, MLAN_EVENT_ID_DRV_DEFER_HANDLING,
				MNULL);

	LEAVE();
}

t_u8
Hostsa_get_bss_role(t_void *pmlan_private)
{
	mlan_private *pmpriv = (mlan_private *)pmlan_private;

	return GET_BSS_ROLE(pmpriv);
}

t_u8
Hostsa_get_intf_hr_len(t_void *pmlan_private)
{
	mlan_private *pmpriv = (mlan_private *)pmlan_private;

	return pmpriv->intf_hr_len;
}

/**
 *  @brief send event to moal to notice that 4 way handshake complete
 *
 *  @param pmlan_private   A void pointer
 *  @param addr  pointer to station mac address
 *
 *  @return
 */
t_void
Hostsa_sendEventRsnConnect(t_void *pmlan_private, t_u8 *addr)
{
	mlan_private *pmpriv = (mlan_private *)pmlan_private;
	mlan_adapter *pmadapter = pmpriv->adapter;
	t_u8 *event_buf = MNULL, *pos = MNULL;
	t_u32 event_cause = 0;
	mlan_event *pevent = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	ret = pmadapter->callbacks.moal_malloc(pmadapter->pmoal_handle,
					       MAX_EVENT_SIZE, MLAN_MEM_DEF,
					       &event_buf);
	if ((ret != MLAN_STATUS_SUCCESS) || !event_buf) {
		PRINTM(MERROR, "Could not allocate buffer for event buf\n");
		goto done;
	}
	pevent = (pmlan_event)event_buf;
	memset(pmadapter, event_buf, 0, MAX_EVENT_SIZE);

	pevent->event_id = MLAN_EVENT_ID_DRV_PASSTHRU;
	pevent->bss_index = pmpriv->bss_index;
	event_cause = wlan_cpu_to_le32(0x51);	/*MICRO_AP_EV_ID_RSN_CONNECT */
	memcpy(pmadapter, (t_u8 *)pevent->event_buf,
	       (t_u8 *)&event_cause, sizeof(event_cause));
	pos = pevent->event_buf + sizeof(event_cause) + 2;	/*reserved 2 byte */
	memcpy(pmadapter, (t_u8 *)pos, addr, MLAN_MAC_ADDR_LENGTH);

	pevent->event_len = MLAN_MAC_ADDR_LENGTH + sizeof(event_cause) + 2;
	wlan_recv_event(pmpriv, MLAN_EVENT_ID_DRV_PASSTHRU, event_buf);

done:
	if (event_buf)
		pmadapter->callbacks.moal_mfree(pmadapter->pmoal_handle,
						event_buf);

	LEAVE();
}

/**
 *  @brief This function initializes callbacks that hostsa interface uses.
 *
 *  @param pmpriv     A pointer to mlan_private structure
 *  @param putil_fns  A pointer to hostsa_util_fns structure
 *  @param pmlan_fns  A pointer to hostsa_mlan_fns structure
 *
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static t_void
hostsa_mlan_callbacks(IN pmlan_private pmpriv,
		      IN hostsa_util_fns *putil_fns,
		      IN hostsa_mlan_fns *pmlan_fns)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	pmlan_callbacks pcb = &pmadapter->callbacks;

	putil_fns->pmoal_handle = pmadapter->pmoal_handle;
	putil_fns->moal_malloc = pcb->moal_malloc;
	putil_fns->moal_mfree = pcb->moal_mfree;
	putil_fns->moal_memset = pcb->moal_memset;
	putil_fns->moal_memcpy = pcb->moal_memcpy;
	putil_fns->moal_memmove = pcb->moal_memmove;
	putil_fns->moal_memcmp = pcb->moal_memcmp;
	putil_fns->moal_udelay = pcb->moal_udelay;
	putil_fns->moal_get_system_time = pcb->moal_get_system_time;
	putil_fns->moal_init_timer = pcb->moal_init_timer;
	putil_fns->moal_free_timer = pcb->moal_free_timer;
	putil_fns->moal_start_timer = pcb->moal_start_timer;
	putil_fns->moal_stop_timer = pcb->moal_stop_timer;
	putil_fns->moal_init_lock = pcb->moal_init_lock;
	putil_fns->moal_free_lock = pcb->moal_free_lock;
	putil_fns->moal_spin_lock = pcb->moal_spin_lock;
	putil_fns->moal_spin_unlock = pcb->moal_spin_unlock;
	putil_fns->moal_print = pcb->moal_print;
	putil_fns->moal_print_netintf = pcb->moal_print_netintf;

	pmlan_fns->pmlan_private = pmpriv;
	pmlan_fns->pmlan_adapter = pmpriv->adapter;
	pmlan_fns->bss_index = pmpriv->bss_index;
	pmlan_fns->bss_type = pmpriv->bss_type;
#if 0
	pmlan_fns->mlan_add_buf_txqueue = mlan_add_buf_txqueue;
	pmlan_fns->mlan_select_wmm_queue = mlan_select_wmm_queue;
	pmlan_fns->mlan_write_data_complete = mlan_write_data_complete;
#endif
	pmlan_fns->hostsa_alloc_mlan_buffer = hostsa_alloc_mlan_buffer;
	pmlan_fns->hostsa_tx_packet = hostsa_tx_packet;
	pmlan_fns->hostsa_set_encrypt_key = wlan_set_encrypt_key;
	pmlan_fns->hostsa_clr_encrypt_key = wlan_clr_encrypt_key;
	pmlan_fns->hostsa_SendDeauth = hostsa_SendDeauth;
	pmlan_fns->Hostsa_DisAssocAllSta = ApDisAssocAllSta;
	pmlan_fns->hostsa_free_mlan_buffer = hostsa_free_mlan_buffer;
	pmlan_fns->Hostsa_get_station_entry = Hostsa_get_station_entry;
	pmlan_fns->Hostsa_set_mgmt_ie = Hostsa_set_mgmt_ie;
	pmlan_fns->Hostsa_find_connection = Hostsa_find_connection;
	pmlan_fns->Hostsa_find_next_connection = Hostsa_find_next_connection;
	pmlan_fns->Hostsa_StaControlledPortOpen = StaControlledPortOpen;
	pmlan_fns->hostsa_StaSendDeauth = hostsa_StaSendDeauth;
	pmlan_fns->Hostsa_get_bss_role = Hostsa_get_bss_role;
	pmlan_fns->Hostsa_get_intf_hr_len = Hostsa_get_intf_hr_len;
	pmlan_fns->Hostsa_sendEventRsnConnect = Hostsa_sendEventRsnConnect;
};

/**
 *  @brief Init hostsa data
 *
 *  @param pmpriv          A pointer to mlan_private structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
hostsa_init(pmlan_private pmpriv)
{
	hostsa_util_fns util_fns;
	hostsa_mlan_fns mlan_fns;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	hostsa_mlan_callbacks(pmpriv, &util_fns, &mlan_fns);
	ret = supplicant_authenticator_init(&pmpriv->psapriv, &util_fns,
					    &mlan_fns, pmpriv->curr_addr);

	LEAVE();
	return ret;
}

/**
 *  @brief Cleanup hostsa data
 *
 *  @param pmpriv     A pointer to mlan_private structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
hostsa_cleanup(pmlan_private pmpriv)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	supplicant_authenticator_free(pmpriv->psapriv);

	LEAVE();
	return ret;
}
