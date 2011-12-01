//------------------------------------------------------------------------------
// Copyright (c) 2009-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// This file implements the host side P2P module.
//
// Author(s): ="Atheros"
//==============================================================================

#include <athdefs.h>
#include <a_types.h>
#include <a_osapi.h>
#include <a_debug.h>
#include <p2p_api.h>
#include <p2p.h>
#include <dl_list.h>
#include <wlan_api.h>
#include "p2p_internal.h"

#include <a_drv_api.h>
#define WPS_DEV_TYPE_BUFSIZE 21
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#include <wmi_api.h>

struct wmi_t;

extern A_STATUS wmi_p2p_go_neg_rsp_cmd(struct wmi_t *wmip, A_UINT8 status,
     A_UINT8 go_intent, A_UINT32 wps_method, A_UINT16 listen_freq,
     A_UINT8 *wpsbuf, A_UINT32 wpslen, A_UINT8 *p2pbuf, A_UINT32 p2plen,
     A_UINT8 dialog_token, A_UINT8 persistent_grp);

extern A_STATUS wmi_p2p_invite_req_rsp_cmd(struct wmi_t *wmip, A_UINT8 status,
        A_INT8 is_go, A_UINT8 *grp_bssid, A_UINT8 *p2pbuf,
            A_UINT8 p2plen, A_UINT8 dialog_token);

extern A_STATUS
wmi_p2p_go_neg_start(struct wmi_t *wmip, WMI_P2P_GO_NEG_START_CMD *go_param);

extern A_STATUS wmi_p2p_invite_cmd(struct wmi_t *wmip, WMI_P2P_INVITE_CMD *buf);

extern A_STATUS wmi_p2p_prov_disc_cmd(struct wmi_t *wmip,
                        WMI_P2P_PROV_DISC_REQ_CMD *buf);

extern A_STATUS wmi_p2p_stop_sdpd(struct wmi_t *wmip);

extern void wmi_node_return (struct wmi_t *wmip, bss_t *bss);

extern void wmi_node_update_timestamp(struct wmi_t *wmip, bss_t *bss);

extern void wmi_setup_node(struct wmi_t *wmip, bss_t *bss, const A_UINT8 *bssid);
extern bss_t *wmi_node_alloc(struct wmi_t *wmip, A_UINT8 len);

extern bss_t *wmi_find_node(struct wmi_t *wmip, const A_UINT8 *macaddr);


static struct p2p_sd_query *p2p_pending_sd_req(struct p2p_dev_ctx *p2p_dev_ctx,
                     struct host_p2p_dev *dev);


/* Global P2P context */
static struct p2p_ctx *g_p2p_ctx;

#define IEEE80211_ELEMID_VENDOR  221  /* vendor private */
#define IEEE80211_ELEMID_SSID 0

static A_STATUS p2p_parse_subelement(A_UINT8 id, const A_UINT8 *data,
                     A_UINT16 len, struct p2p_ie *p2p_ie)
{
    A_STATUS status = A_OK;
    const A_UINT8 *pos;
    A_UINT32 i, nlen;

    switch (id) {
    case P2P_ATTR_CAPABILITY:
        if (len < 2) {
            return A_ERROR;
        }
        p2p_ie->capability = data;
        break;
    case P2P_ATTR_GROUP_OWNER_INTENT:
        if (len < 1) {
            return A_ERROR;
        }
        p2p_ie->go_intent = data;
        break;
    case P2P_ATTR_STATUS:
        if (len < 1) {
            return A_ERROR;
        }
        p2p_ie->status = data;
        break;
    case P2P_ATTR_LISTEN_CHANNEL:
        if (len == 0) {
            break;
        }
        if (len < 2) {
            return A_ERROR;
        }
        p2p_ie->listen_channel = data;
        break;

    case P2P_ATTR_OPERATING_CHANNEL:
        if (len == 0) {
            break;
        }
        if (len < 2) {
            return A_ERROR;
        }
        p2p_ie->operating_channel = data;
        break;

    case P2P_ATTR_CHANNEL_LIST:
        if (len < 3) {
            return A_ERROR;
        }
        p2p_ie->channel_list = data;
        p2p_ie->channel_list_len = len;
        break;
    case P2P_ATTR_GROUP_INFO:
        p2p_ie->group_info = data;
        p2p_ie->group_info_len = len;
        break;
    case P2P_ATTR_DEVICE_INFO:
        if (len < ETH_ALEN + 2 + 8 + 1) {
            return A_ERROR;
        }
        p2p_ie->p2p_device_info = data;
        p2p_ie->p2p_device_info_len = len;
        pos = data;
        p2p_ie->p2p_device_addr = pos;
        pos += ETH_ALEN;
        p2p_ie->config_methods = WPA_GET_BE16(pos);
        pos += 2;
        p2p_ie->pri_dev_type = pos;
        pos += 8;
        p2p_ie->num_sec_dev_types = *pos++;
        if (p2p_ie->num_sec_dev_types * 8 > data + len - pos) {
            return A_ERROR;
        }
        pos += p2p_ie->num_sec_dev_types * 8;
        if (data + len - pos < 4) {
            return A_ERROR;
        }
        if (WPA_GET_BE16(pos) != ATTR_DEV_NAME) {
            return A_ERROR;
        }
        pos += 2;
        nlen = WPA_GET_BE16(pos);
        pos += 2;
        if (data + len - pos < (int) nlen || nlen > 32) {
            return A_ERROR;
        }
        A_MEMCPY(p2p_ie->device_name, pos, nlen);
        p2p_ie->dev_name_len = nlen;
        for (i = 0; i < nlen; i++) {
            if (p2p_ie->device_name[i] == '\0')
                break;
            if (p2p_ie->device_name[i] < 32)
                p2p_ie->device_name[i] = '_';
        }
        break;
    case P2P_ATTR_CONFIGURATION_TIMEOUT:
        if (len < 2) {
            return A_ERROR;
        }
        p2p_ie->config_timeout = data;
        break;
    case P2P_ATTR_INTENDED_INTERFACE_ADDR:
        if (len < ETH_ALEN) {
            return A_ERROR;
        }
        p2p_ie->intended_addr = data;
        break;
    default:
        break;
    }

    return status;
}


/**
 * p2p_parse_p2p_ie - Parse P2P IE
 */
static A_STATUS p2p_parse_p2p_ie(const A_UINT8 *data, A_UINT8 len, struct p2p_ie *msg)
{
    A_STATUS status = A_OK;
    const A_UINT8 *pos = data;
    const A_UINT8 *end = pos + len;

    while (pos < end) {
        A_UINT16 attr_len;
        if (pos + 2 >= end) {
            return A_ERROR;
        }
        attr_len = WPA_GET_LE16((pos + 1));
        if (pos + 3 + attr_len > end) {
            return A_ERROR;
        }
        if ((status = p2p_parse_subelement(pos[0], pos + 3, attr_len, msg)) != 
                              A_OK) {
            return A_ERROR;
        }
        pos += 3 + attr_len;
    }

    return status;
}

static A_STATUS wps_set_attr(struct wps_parse_attr *attr, A_UINT16 type,
            const A_UINT8 *pos, A_UINT16 len)
{
    A_STATUS status = A_OK;

    switch (type) {
    case ATTR_VERSION:
        if (len != 1) {
            return A_ERROR;
        }
        attr->version = pos;
        break;
    case ATTR_MSG_TYPE:
        if (len != 1) {
            return A_ERROR;
        }
        attr->msg_type = pos;
        break;
    case ATTR_ENROLLEE_NONCE:
        if (len != WPS_NONCE_LEN) {
            return A_ERROR;
        }
        attr->enrollee_nonce = pos;
        break;
    case ATTR_REGISTRAR_NONCE:
        if (len != WPS_NONCE_LEN) {
            return A_ERROR;
        }
        attr->registrar_nonce = pos;
        break;
    case ATTR_UUID_E:
        if (len != WPS_UUID_LEN) {
            return A_ERROR;
        }
        attr->uuid_e = pos;
        break;
    case ATTR_UUID_R:
        if (len != WPS_UUID_LEN) {
            return A_ERROR;
        }
        attr->uuid_r = pos;
        break;
    case ATTR_AUTH_TYPE_FLAGS:
        if (len != 2) {
            return A_ERROR;
        }
        attr->auth_type_flags = pos;
        break;
    case ATTR_ENCR_TYPE_FLAGS:
        if (len != 2) {
            return A_ERROR;
        }
        attr->encr_type_flags = pos;
        break;
    case ATTR_CONN_TYPE_FLAGS:
        if (len != 1) {
            return A_ERROR;
        }
        attr->conn_type_flags = pos;
        break;
    case ATTR_CONFIG_METHODS:
        if (len != 2) {
            return A_ERROR;
        }
        attr->config_methods = pos;
        break;
    case ATTR_SELECTED_REGISTRAR_CONFIG_METHODS:
        if (len != 2) {
            return A_ERROR;
        }
        attr->sel_reg_config_methods = pos;
        break;
    case ATTR_PRIMARY_DEV_TYPE:
        if (len != WPS_DEV_TYPE_LEN) {
            return A_ERROR;
        }
        attr->primary_dev_type = pos;
        break;
    case ATTR_RF_BANDS:
        if (len != 1) {
            return A_ERROR;
        }
        attr->rf_bands = pos;
        break;
    case ATTR_ASSOC_STATE:
        if (len != 2) {
            return A_ERROR;
        }
        attr->assoc_state = pos;
        break;
    case ATTR_CONFIG_ERROR:
        if (len != 2) {
            return A_ERROR;
        }
        attr->config_error = pos;
        break;
    case ATTR_DEV_PASSWORD_ID:
        if (len != 2) {
            return A_ERROR;
        }
        attr->dev_password_id = pos;
        break;
    case ATTR_OS_VERSION:
        if (len != 4) {
            return A_ERROR;
        }
        attr->os_version = pos;
        break;
    case ATTR_WPS_STATE:
        if (len != 1) {
            return A_ERROR;
        }
        attr->wps_state = pos;
        break;
    case ATTR_AUTHENTICATOR:
        if (len != WPS_AUTHENTICATOR_LEN) {
            return A_ERROR;
        }
        attr->authenticator = pos;
        break;
    case ATTR_R_HASH1:
        if (len != WPS_HASH_LEN) {
            return A_ERROR;
        }
        attr->r_hash1 = pos;
        break;
    case ATTR_R_HASH2:
        if (len != WPS_HASH_LEN) {
            return A_ERROR;
        }
        attr->r_hash2 = pos;
        break;
    case ATTR_E_HASH1:
        if (len != WPS_HASH_LEN) {
            return A_ERROR;
        }
        attr->e_hash1 = pos;
        break;
    case ATTR_E_HASH2:
        if (len != WPS_HASH_LEN) {
            return A_ERROR;
        }
        attr->e_hash2 = pos;
        break;
    case ATTR_R_SNONCE1:
        if (len != WPS_SECRET_NONCE_LEN) {
            return A_ERROR;
        }
        attr->r_snonce1 = pos;
        break;
    case ATTR_R_SNONCE2:
        if (len != WPS_SECRET_NONCE_LEN) {
            return A_ERROR;
        }
        attr->r_snonce2 = pos;
        break;
    case ATTR_E_SNONCE1:
        if (len != WPS_SECRET_NONCE_LEN) {
            return A_ERROR;
        }
        attr->e_snonce1 = pos;
        break;
    case ATTR_E_SNONCE2:
        if (len != WPS_SECRET_NONCE_LEN) {
            return A_ERROR;
        }
        attr->e_snonce2 = pos;
        break;
    case ATTR_KEY_WRAP_AUTH:
        if (len != WPS_KWA_LEN) {
            return A_ERROR;
        }
        attr->key_wrap_auth = pos;
        break;
    case ATTR_AUTH_TYPE:
        if (len != 2) {
            return A_ERROR;
        }
        attr->auth_type = pos;
        break;
    case ATTR_ENCR_TYPE:
        if (len != 2) {
            return A_ERROR;
        }
        attr->encr_type = pos;
        break;
    case ATTR_NETWORK_INDEX:
        if (len != 1) {
            return A_ERROR;
        }
        attr->network_idx = pos;
        break;
    case ATTR_NETWORK_KEY_INDEX:
        if (len != 1) {
            return A_ERROR;
        }
        attr->network_key_idx = pos;
        break;
    case ATTR_MAC_ADDR:
        if (len != ETH_ALEN) {
            return A_ERROR;
        }
        attr->mac_addr = pos;
        break;
    case ATTR_KEY_PROVIDED_AUTO:
        if (len != 1) {
            return A_ERROR;
        }
        attr->key_prov_auto = pos;
        break;
    case ATTR_802_1X_ENABLED:
        if (len != 1) {
            return A_ERROR;
        }
        attr->dot1x_enabled = pos;
        break;
    case ATTR_SELECTED_REGISTRAR:
        if (len != 1) {
            return A_ERROR;
        }
        attr->selected_registrar = pos;
        break;
    case ATTR_REQUEST_TYPE:
        if (len != 1) {
            return A_ERROR;
        }
        attr->request_type = pos;
        break;
    case ATTR_RESPONSE_TYPE:
        if (len != 1) {
            return A_ERROR;
        }
        attr->request_type = pos;
        break;
    case ATTR_MANUFACTURER:
        attr->manufacturer = pos;
        attr->manufacturer_len = len;
        break;
    case ATTR_MODEL_NAME:
        attr->model_name = pos;
        attr->model_name_len = len;
        break;
    case ATTR_MODEL_NUMBER:
        attr->model_number = pos;
        attr->model_number_len = len;
        break;
    case ATTR_SERIAL_NUMBER:
        attr->serial_number = pos;
        attr->serial_number_len = len;
        break;
    case ATTR_DEV_NAME:
        attr->dev_name = pos;
        attr->dev_name_len = len;
        break;
    case ATTR_PUBLIC_KEY:
        attr->public_key = pos;
        attr->public_key_len = len;
        break;
    case ATTR_ENCR_SETTINGS:
        attr->encr_settings = pos;
        attr->encr_settings_len = len;
        break;
    case ATTR_CRED:
        if (attr->num_cred >= MAX_CRED_COUNT) {
            break;
        }
        attr->cred[attr->num_cred] = pos;
        attr->cred_len[attr->num_cred] = len;
        attr->num_cred++;
        break;
    case ATTR_SSID:
        attr->ssid = pos;
        attr->ssid_len = len;
        break;
    case ATTR_NETWORK_KEY:
        attr->network_key = pos;
        attr->network_key_len = len;
        break;
    case ATTR_EAP_TYPE:
        attr->eap_type = pos;
        attr->eap_type_len = len;
        break;
    case ATTR_EAP_IDENTITY:
        attr->eap_identity = pos;
        attr->eap_identity_len = len;
        break;
    default:
        break;
    }

    return status;
}

static A_STATUS wps_parse_msg(const A_UINT8 *data, A_UINT8 len, struct wps_parse_attr *attr)
{
    A_STATUS status = A_OK;
    const A_UINT8 *pos, *end;
    A_UINT16 type;

    A_MEMZERO(attr, sizeof(*attr));
    pos = data;
    end = pos + len;

    while (pos < end) {
        if (end - pos < 4) {
            return A_ERROR;
        }

        type = WPA_GET_BE16(pos);
        pos += 2;
        len = WPA_GET_BE16(pos);
        pos += 2;
        if (len > end - pos) {
            return A_ERROR;
        }

        if ((status = wps_set_attr(attr, type, pos, len)) != A_OK) {
            return status;
        }

        pos += len;
    }

    return status;
}


static A_STATUS p2p_parse_wps_ie(const A_UINT8 *data, A_UINT8 len, struct p2p_ie *p2p_ie)
{
    A_STATUS status = A_OK;
    struct wps_parse_attr attr;

    if ( (status = wps_parse_msg(data, len, &attr)) != A_OK ) {
        return status;
    }
    
    if (attr.dev_name && attr.dev_name_len < sizeof(p2p_ie->wps_device_name)) {
        A_MEMCPY(p2p_ie->wps_device_name, attr.dev_name, attr.dev_name_len);
        p2p_ie->wps_dev_name_len = attr.dev_name_len;
    }

    if (attr.config_methods) {
        p2p_ie->wps_config_methods = WPA_GET_BE16((attr.config_methods));
    }

    if (attr.dev_password_id) {
        p2p_ie->dev_password_id = WPA_GET_BE16(attr.dev_password_id);
    }
    
    if(attr.primary_dev_type) {
        p2p_ie->wps_pri_dev_type = attr.primary_dev_type;
        
    }

    return status;
}
static A_STATUS p2p_get_ie(WMI_BI_FTYPE fType, const A_UINT8 *data, A_UINT32 len, A_UINT32 vendor_type, A_UINT8 **ie, A_UINT32 *ie_len) 
{
    const A_UINT8 *frm, *efrm;
    A_STATUS status = A_ENODEV;
    struct p2p_ie p2p_ie ;
    A_UINT8 tlv_len;
    
    switch(fType) {
        case BEACON_FTYPE:
        case PROBERESP_FTYPE: 
        /*
         * beacon/probe response frame format
         *  [8] time stamp
         *  [2] beacon interval
         *  [2] capability information
         *  [tlv]s...
         */
            data += 12;
            len -= 12;
            break; 
        case ACTION_MGMT_FTYPE:
         /* Action frame format:
          *   [1] Category code
          *   [1] Action field
          *   [3] WFA Specific OUI
          *   [1] OUI Type
          *   [1] OUI SubType
          *   [1] Dialog Token
          *   [TLV]s...
          *       - P2P IE, WSC IE
          */
            data += 8;
            len -= 8;
            break;
        case PROBEREQ_FTYPE:

             break;
        default:
             A_ASSERT(FALSE);
             break;
    }

    frm = data;
    efrm = (A_UINT8 *) (frm + len);

    while (frm < efrm) {
        switch (*frm) {
        case IEEE80211_ELEMID_VENDOR:
            tlv_len = frm[1]-4;
            if(isp2poui(frm) && (vendor_type == P2P_OUI) && (tlv_len)) {
                status = p2p_parse_p2p_ie(frm+6, frm[1]-4, &p2p_ie);
                if(status == A_OK && (frm[1] -4 !=0)) {
                    *ie = (A_UINT8 *)frm+6;
                    *ie_len = frm[1]-4 ;
                }
            }
            break;
        default:
            break;
        }
        /* Discontinue parsing if there is a bad IE */
        if (status != A_OK && status != A_ENODEV) {
            break;
        }
        frm += frm[1] + 2;
    }

    return status;
}
/**
 * p2p_parse_ies - Parse P2P message IEs (both WPS and P2P IE)
 */
static A_STATUS p2p_parse_ies(const A_UINT8 *data, A_UINT32 len, struct p2p_ie *p2p_ie)
{
    const A_UINT8 *frm, *efrm;
    A_STATUS status = A_ENODEV;
    A_BOOL found = FALSE;

    frm = data;
    efrm = (A_UINT8 *) (frm + len);

    while (frm < efrm) {
        switch (*frm) {
        case IEEE80211_ELEMID_VENDOR:
            if(iswscoui(frm)) {
                status = p2p_parse_wps_ie(frm+6, frm[1]-4, p2p_ie);
            } else if(isp2poui(frm)) {
                found = TRUE;
                status = p2p_parse_p2p_ie(frm+6, frm[1]-4, p2p_ie);
            }
            break;
        case IEEE80211_ELEMID_SSID:
            p2p_ie->ssid = frm;
            break;
        default:
            break;
        }
        /* Discontinue parsing if there is a bad IE */
        if (status != A_OK && status != A_ENODEV) {
            break;
        }
        frm += frm[1] + 2;
    }

    if (status == A_OK && !found) {
        status = A_ENODEV;
    }

    return status;
}

/* This function prunes the frame depending on its type to point the data ptr to
 * the IEs portion of the frame & calls the p2p_parse_ies() to parse the P2P & 
 * WPS IEs.
 */
static A_STATUS p2p_parse(WMI_BI_FTYPE fType, const A_UINT8 *data, A_UINT32 len, struct p2p_ie *p2p_ie)
{
    A_STATUS status = A_OK;

    switch(fType) {
    case BEACON_FTYPE:
    case PROBERESP_FTYPE: 
    /*
     * beacon/probe response frame format
     *  [8] time stamp
     *  [2] beacon interval
     *  [2] capability information
     *  [tlv]s...
     */
        data += 12;
        len -= 12;
        break; 
    case ACTION_MGMT_FTYPE:
    /* Action frame format:
     *   [1] Category code
     *   [1] Action field
     *   [3] WFA Specific OUI
     *   [1] OUI Type
     *   [1] OUI SubType
     *   [1] Dialog Token
     *   [TLV]s...
     *       - P2P IE, WSC IE
     */
        p2p_ie->dialog_token = *(data+7);
        data += 8;
        len -= 8;
        break;

    case PROBEREQ_FTYPE:

         break;
    default:
        A_ASSERT(FALSE);
        break;
    }
    
    status = p2p_parse_ies(data, len, p2p_ie);

    return status;
}

static A_STATUS p2p_group_info_parse(const A_UINT8 *group_info, A_UINT32 group_info_len,
                    struct p2p_group_info *info)
{
    const A_UINT8 *start, *end;

    A_MEMZERO(info, sizeof(*info));

    start = group_info;
    end = start + group_info_len;


    while (start < end) {
        struct p2p_client_info *cli;

        const A_UINT8 *t, *cend;
        A_UINT32 count;

        cli = &info->client[info->num_clients];
        cend = start + 1 + start[0];

        if (cend > end) {
            return A_ERROR; /* invalid data */
        }

        /* 'start' at begin of P2P Client Info Descriptor */
        /* t at Device Capability Bitmap */
        t = start + 1 + 2 * ETH_ALEN;
        if (t > cend) {
            return A_ERROR; /* invalid data */
        }

        cli->p2p_device_addr = start + 1;
        cli->p2p_interface_addr = start + 1 + ETH_ALEN;
        cli->dev_capab = t[0];

        if (t + 1 + 2 + 8 + 1 > cend) {
            return A_ERROR; /* invalid data */
        }

        cli->config_methods = WPA_GET_BE16(&t[1]);
        cli->pri_dev_type = &t[3];

        t += 1 + 2 + 8;
        /* t at Number of Secondary Device Types */
        cli->num_sec_dev_types = *t++;
        if (t + 8 * cli->num_sec_dev_types > cend) {
            return A_ERROR; /* invalid data */
        }
        cli->sec_dev_types = t;
        t += 8 * cli->num_sec_dev_types;

        /* t at Device Name in WPS TLV format */
        if (t + 2 + 2 > cend) {
            return A_ERROR; /* invalid data */
        }
        if (WPA_GET_BE16(t) != ATTR_DEV_NAME) {
            return A_ERROR; /* invalid Device Name TLV */
        }
        t += 2;
        count = WPA_GET_BE16(t);
        t += 2;
        if (count > cend - t) {
            return A_ERROR; /* invalid Device Name TLV */
        }
        if (count >= 32) {
            count = 32;
        }
        cli->dev_name = (const char *) t;
        cli->dev_name_len = count;

        start = cend;

        info->num_clients++;
        if (info->num_clients == P2P_MAX_GROUP_ENTRIES) {
            return A_ERROR;
        }
    }

    return A_OK;
}

static void p2p_copy_client_info(struct host_p2p_dev *dev,
                 struct p2p_client_info *cli)
{
    A_MEMCPY((dev->dev).device_name, cli->dev_name, cli->dev_name_len);
    (dev->dev).device_name[cli->dev_name_len] = '\0';
    (dev->dev).dev_capab = cli->dev_capab;
    (dev->dev).config_methods = cli->config_methods;
    A_MEMCPY((dev->dev).pri_dev_type, cli->pri_dev_type, 8);
}


//static A_STATUS ieee802_11_vendor_ie_concat(const A_UINT8 *ies,
//        A_UINT32 ies_len, A_UINT32 oui_type, A_UINT8 *buf, A_UINT32 *buflen)
//{
//    const A_UINT8 *end, *pos, *ie;
//    A_UINT8 len=0;
//
//    pos = ies;
//    end = ies + ies_len;
//    ie = NULL;
//
//    while (pos + 1 < end) {
//        if (pos + 2 + pos[1] > end)
//            return A_ERROR;
//        if (pos[0] == WLAN_EID_VENDOR_SPECIFIC && pos[1] >= 4 &&
//            WPA_GET_BE32(&pos[2]) == oui_type) {
//            ie = pos;
//            break;
//        }
//        pos += 2 + pos[1];
//    }
//    
//    if (ie == NULL) {
//        *buflen = 0;
//        return A_OK; /* No specified vendor IE found */
//    }
//
//    if (buf == NULL)
//        return A_ERROR;
//
//    /*
//     * There may be multiple vendor IEs in the message, so need to
//     * concatenate their data fields.
//     */
//    while (pos + 1 < end) {
//        if (pos + 2 + pos[1] > end)
//            break;
//        if (pos[0] == WLAN_EID_VENDOR_SPECIFIC && pos[1] >= 4 &&
//            WPA_GET_BE32(&pos[2]) == oui_type) {
//#define MAX_BUF_SIZE 512
//                A_ASSERT((len+pos[1]-4) <= MAX_BUF_SIZE);
//                A_MEMCPY(buf+len, pos+6, pos[1]-4);
//                len += pos[1]-4;
//            }
//        pos += 2 + pos[1];
//    }
//
//    /* store the length of the IE to return to the caller.
//     */
//    *buflen = len;
//
//    return A_OK;
//}

static A_INT16 p2p_channel_to_freq_j4(A_UINT8 reg_class, A_UINT8 channel)
{
    /* Table J-4 in P802.11REVmb/D4.0 - Global operating classes */
    /* TODO: more regulatory classes */
    switch (reg_class) {
    case 81:
        /* channels 1..13 */
        if (channel < 1 || channel > 13)
            return -1;
        return 2407 + 5 * channel;
    case 82:
        /* channel 14 */
        if (channel != 14)
            return -1;
        return 2414 + 5 * channel;
    case 83: /* channels 1..9; 40 MHz */
    case 84: /* channels 5..13; 40 MHz */
        if (channel < 1 || channel > 13)
            return -1;
        return 2407 + 5 * channel;
    case 115: /* channels 36,40,44,48; indoor only */
    case 118: /* channels 52,56,60,64; dfs */
        if (channel < 36 || channel > 64)
            return -1;
        return 5000 + 5 * channel;
    case 124: /* channels 149,153,157,161 */
    case 125: /* channels 149,153,157,161,165,169 */
        if (channel < 149 || channel > 161)
            return -1;
        return 5000 + 5 * channel;
    case 116: /* channels 36,44; 40 MHz; indoor only */
    case 117: /* channels 40,48; 40 MHz; indoor only */
    case 119: /* channels 52,60; 40 MHz; dfs */
    case 120: /* channels 56,64; 40 MHz; dfs */
        if (channel < 36 || channel > 64)
            return -1;
        return 5000 + 5 * channel;
    case 126: /* channels 149,157; 40 MHz */
    case 127: /* channels 153,161; 40 MHz */
        if (channel < 149 || channel > 161)
            return -1;
        return 5000 + 5 * channel;
    }
    return -1;
}


/**
 * p2p_channel_to_freq - Convert channel info to frequency
 * @country: Country code
 * @reg_class: Regulatory class
 * @channel: Channel number
 * Returns: Frequency in MHz or -1 if the specified channel is unknown
 */
static A_INT16 p2p_channel_to_freq(const A_CHAR *country, A_UINT8 reg_class, A_UINT8 channel)
{
    if (country[2] == 0x04) {
        return p2p_channel_to_freq_j4(reg_class, channel);
    }

    /* These are mainly for backwards compatibility; to be removed */
    switch (reg_class) {
    case 1: /* US/1, EU/1, JP/1 = 5 GHz, channels 36,40,44,48 */
        if (channel < 36 || channel > 48)
            return -1;
        return 5000 + 5 * channel;
    case 3: /* US/3 = 5 GHz, channels 149,153,157,161 */
    case 5: /* US/5 = 5 GHz, channels 149,153,157,161 */
        if (channel < 149 || channel > 161)
            return -1;
        return 5000 + 5 * channel;
    case 4: /* EU/4 = 2.407 GHz, channels 1..13 */
    case 12: /* US/12 = 2.407 GHz, channels 1..11 */
    case 30: /* JP/30 = 2.407 GHz, channels 1..13 */
        if (channel < 1 || channel > 13)
            return -1;
        return 2407 + 5 * channel;
    case 31: /* JP/31 = 2.414 GHz, channel 14 */
        if (channel != 14)
            return -1;
        return 2414 + 5 * channel;
    }

    return -1;
}

static void p2p_add_device_info_from_scan_data(struct p2p_dev_ctx *p2p_dev_ctx, struct host_p2p_dev *p2p_peer, A_UINT8 *addr, A_UINT16 channel, const A_UINT8 *p2p_dev_addr, struct p2p_ie *msg, const A_UINT8 *data, A_UINT32 len)
{
    if (A_MEMCMP(addr, p2p_dev_addr, ETH_ALEN) != 0) {
        A_MEMCPY(p2p_peer->dev.interface_addr, addr, ETH_ALEN);
    }

    if (msg->ssid &&
        (msg->ssid[1] != P2P_WILDCARD_SSID_LEN ||
         A_MEMCMP(msg->ssid + 2, P2P_WILDCARD_SSID, P2P_WILDCARD_SSID_LEN)
         != 0)) {
        A_MEMCPY(p2p_peer->dev.oper_ssid, msg->ssid + 2, msg->ssid[1]);
        p2p_peer->dev.oper_ssid_len = msg->ssid[1];

        A_MEMCPY(p2p_peer->dev.interface_addr, addr, ETH_ALEN);
    }

    p2p_peer->dev.listen_freq = channel;

    if (msg->group_info) {
        p2p_peer->dev.oper_freq = channel;
    }

    if (msg->pri_dev_type) {
        A_MEMCPY(p2p_peer->dev.pri_dev_type, msg->pri_dev_type,
              sizeof(p2p_peer->dev.pri_dev_type));
    }

    A_MEMCPY(p2p_peer->dev.device_name, msg->device_name, sizeof(p2p_peer->dev.device_name));

    p2p_peer->dev.config_methods = msg->config_methods ? msg->config_methods :
        msg->wps_config_methods;

    if (msg->capability) {
        p2p_peer->dev.dev_capab = msg->capability[0];
        p2p_peer->dev.group_capab = msg->capability[1];
    }

    if (msg->ext_listen_timing) {
        p2p_peer->dev.ext_listen_period = WPA_GET_LE16(msg->ext_listen_timing);
        p2p_peer->dev.ext_listen_interval =
            WPA_GET_LE16(msg->ext_listen_timing + 2);
    }

    /* TODO: Add group clients if this is a beacon containing grp info attribute
     * in the P2P IE.
     */
      
     /* If the device reported by other then probe req only then DONOT report again.
      * But if this dev is reported ONLY by probe req, report again with mode info.
      */
    if ((p2p_peer->dev.flags & P2P_DEV_REPORTED) && ((p2p_peer->dev.flags & P2P_DEV_PROBE_REQ_ONLY) == 0))
        return;

    if (p2p_peer->dev.flags & P2P_DEV_USER_REJECTED) {
        return;
    }

    /* report device to driver & App */
    A_WMI_P2PDEV_EVENT(p2p_dev_ctx->dev, addr, p2p_dev_addr, msg->pri_dev_type,
            msg->device_name, msg->dev_name_len,
            msg->config_methods,
            msg->capability[0], msg->capability[1]);

    p2p_peer->dev.flags |= P2P_DEV_REPORTED;

    p2p_peer->dev.flags &=
        ~(P2P_DEV_PROBE_REQ_ONLY | P2P_DEV_GROUP_CLIENT_ONLY);
    return;
}

static void p2p_add_device_info_from_action_frame(struct p2p_dev_ctx
    *p2p_dev_ctx, struct host_p2p_dev *p2p_peer,
                 A_UINT8 *addr, struct p2p_ie *msg)
{
    A_INT16 freq;

    if (msg->pri_dev_type) {
        A_MEMCPY(p2p_peer->dev.pri_dev_type, msg->pri_dev_type,
              sizeof(p2p_peer->dev.pri_dev_type));
    }

    A_MEMCPY(p2p_peer->dev.device_name, msg->device_name, sizeof(p2p_peer->dev.device_name));

    p2p_peer->dev.config_methods = msg->config_methods ? msg->config_methods :
        msg->wps_config_methods;

    if (msg->capability) {
        p2p_peer->dev.dev_capab = msg->capability[0];
        p2p_peer->dev.group_capab = msg->capability[1];
    }

    if (msg->listen_channel) {
        freq = p2p_channel_to_freq((const A_CHAR *)msg->listen_channel,
                        msg->listen_channel[3], msg->listen_channel[4]);
        if (freq > 0) {
            p2p_peer->dev.listen_freq = freq;
        }
    }

    if (msg->ext_listen_timing) {
        p2p_peer->dev.ext_listen_period = WPA_GET_LE16(msg->ext_listen_timing);
        p2p_peer->dev.ext_listen_interval =
            WPA_GET_LE16(msg->ext_listen_timing + 2);
    }

    if (p2p_peer->dev.flags & P2P_DEV_PROBE_REQ_ONLY) {
        p2p_peer->dev.flags &= ~P2P_DEV_PROBE_REQ_ONLY;
    }

    p2p_peer->dev.flags &= ~P2P_DEV_GROUP_CLIENT_ONLY;
    

    /* report device to driver & App */
    A_WMI_P2PDEV_EVENT(p2p_dev_ctx->dev, addr, p2p_peer->dev.p2p_device_addr,
            msg->pri_dev_type,
            msg->device_name, msg->dev_name_len,
            msg->config_methods,
            (msg->capability) ? msg->capability[0]:0, (msg->capability)?msg->capability[1]:0);

    p2p_peer->dev.flags |= P2P_DEV_REPORTED;

    return;
}

struct host_p2p_dev *p2p_get_device(void *ctx, const A_UINT8 *addr)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct host_p2p_dev *dev;
    PDL_LIST pListItem;

    ITERATE_OVER_LIST(&p2p_dev_ctx->devices, pListItem) {
        dev = A_CONTAINING_STRUCT(pListItem, struct host_p2p_dev, list);
        if (A_MEMCMP((dev->dev).p2p_device_addr, addr, ETH_ALEN) == 0) {
            return dev;
        }
    }

    return NULL;
}

struct host_p2p_dev *p2p_get_next_device(void *ctx, const A_UINT8 *addr, int first_element)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct host_p2p_dev *dev;
    PDL_LIST pListItem;

    if (first_element) {
        /*
         * If the request is for the FIRST element, there is no
         * address of the item before the desired item to return,
         * so we just return the first element in the list.
         */
        if (DL_LIST_IS_EMPTY(&p2p_dev_ctx->devices)) {
            return NULL;
        }
        pListItem = DL_LIST_GET_ITEM_AT_HEAD(&p2p_dev_ctx->devices);
        return A_CONTAINING_STRUCT(pListItem, struct host_p2p_dev, list);
    }
    ITERATE_OVER_LIST(&p2p_dev_ctx->devices, pListItem) {
        /*
         * Iterate over the list.  If we find the item with the
         * matching address, return the NEXT element in the list.
         * The DL_LIST is a doubly-linked list with &p2p_dev_ctx->devices
         * at both the head and tail of the list.  This struct is
         * not a list element itself, but is used to mark where the list
         * begins and ends.  Therefore, if the NEXT item in the list
         * is &p2p_dev_ctx->devices, we've reached the end, and we should
         * continue (which will break out of the ITERATE_OVER_LIST loop
         * and then return NULL
         */
        dev = A_CONTAINING_STRUCT(pListItem, struct host_p2p_dev, list);
        if (A_MEMCMP((dev->dev).p2p_device_addr, addr, ETH_ALEN) == 0) {
            if (pListItem->pNext == &p2p_dev_ctx->devices) {
                continue;
            }
            return A_CONTAINING_STRUCT(pListItem->pNext, struct host_p2p_dev, list);
        }
    }
    return NULL;
}

struct host_p2p_dev *p2p_get_device_intf_addrs(void *ctx, const A_UINT8 *intfaddr)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct host_p2p_dev *dev;
    PDL_LIST pListItem;

    ITERATE_OVER_LIST(&p2p_dev_ctx->devices, pListItem) {
        dev = A_CONTAINING_STRUCT(pListItem, struct host_p2p_dev, list);
        if (A_MEMCMP((dev->dev).interface_addr, intfaddr, ETH_ALEN) == 0) {
            return dev;
        }
    }

    return NULL;
}

static struct host_p2p_dev *p2p_create_device(struct p2p_dev_ctx *p2p_dev_ctx,
                    const A_UINT8 *addr)
{
    struct host_p2p_dev *p2p_dev = NULL;

    /* check if the device is already in the list. If so return the dev,
     */
    p2p_dev = p2p_get_device(p2p_dev_ctx, addr);

    if (p2p_dev) {
        return p2p_dev;
    }

    p2p_dev = A_MALLOC_NOWAIT(sizeof(struct host_p2p_dev));
    if (!p2p_dev) {
        return NULL;
    }
    A_MEMZERO(p2p_dev, sizeof(struct host_p2p_dev));
    DL_LIST_INIT(&p2p_dev->list);
    p2p_dev->p2p = p2p_dev_ctx;

    /* Add to the device list */
    DL_ListAdd(&p2p_dev_ctx->devices,&p2p_dev->list);
    A_MEMCPY((p2p_dev->dev).p2p_device_addr, addr, ETH_ALEN);
    p2p_dev->p2p_buf = NULL;
    p2p_dev->p2p_buf_len = 0;

    A_PRINTF("+: %x:%x\n", addr[4], addr[5]);

    return p2p_dev;
}

/* API Routines */

void *p2p_init(void *dev)
{
    struct p2p_dev_ctx *p2p_dev_ctx = NULL;

    /* Allocate the global P2P context if not yet allocated */
    if (!g_p2p_ctx) {
        g_p2p_ctx = (struct p2p_ctx *) A_MALLOC_NOWAIT(sizeof(struct p2p_ctx));
        if (!g_p2p_ctx) {
            return NULL;
        }
        A_MEMZERO(g_p2p_ctx, sizeof(struct p2p_ctx));

        /* Initialize dev common state.
         */
        g_p2p_ctx->go_intent = -1;
    }

    /* Allocate the device specific P2P context.
     */
    p2p_dev_ctx = (struct p2p_dev_ctx *)
                       A_MALLOC_NOWAIT(sizeof(struct p2p_dev_ctx));    
    if (!p2p_dev_ctx) {
        return NULL;
    }
    A_MEMZERO(p2p_dev_ctx, sizeof(struct p2p_dev_ctx));
    p2p_dev_ctx->p2p_ctx = g_p2p_ctx;

    /* Store a ref to the dev in the device context
     */
    p2p_dev_ctx->dev = dev;
    DL_LIST_INIT(&p2p_dev_ctx->devices);

    return (void *)p2p_dev_ctx;
}

void *p2p_bssinfo_rx(void *ctx, WMI_BI_FTYPE fType, A_UINT8 *addr, A_UINT16 channel, const A_UINT8 *data, A_UINT32 len)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct p2p_ie msg;
    struct host_p2p_dev *peer_dev = NULL;
    A_STATUS status = A_OK;
    const A_UINT8 *p2p_dev_addr=NULL;
    A_UINT8 myaddr[ETH_ALEN];

    A_MEMZERO(&msg, sizeof(struct p2p_ie));

    A_WMI_GET_DEVICE_ADDR(p2p_dev_ctx->dev, myaddr);

    status = p2p_parse(fType, data, len, &msg);
    if (status != A_OK) {
        /* No P2P IE or Bad P2P/WPS IE */
        return NULL;
    } else {
        if (fType == BEACON_FTYPE || fType == PROBERESP_FTYPE) {
            /* If the bssinfo msg is of Beacon|Probe-resp type check if
             * the device address or device id is present in the msg. if 
             * not ignore scan data without p2p device info or device id.
             */
            if (msg.p2p_device_addr) {
                p2p_dev_addr = msg.p2p_device_addr;
            } else if (msg.device_id) {
                p2p_dev_addr = msg.device_id;
            } else {
                /* No p2p device id or device address. Ignore this dev */
                return NULL;
            }

            if (!is_zero_mac_addr(p2p_dev_ctx->peer_filter) &&
                A_MEMCMP(p2p_dev_addr, p2p_dev_ctx->peer_filter,
                    ETH_ALEN) != 0) {
                /* dev does not match the set peer filter. Ignore this dev */
                return NULL;
            }

            peer_dev = p2p_create_device(p2p_dev_ctx, p2p_dev_addr);

            if (!peer_dev) {
                return NULL;
            }

            /* Update device info frm scan data */
            p2p_add_device_info_from_scan_data(p2p_dev_ctx, peer_dev, addr,
                            channel, p2p_dev_addr, &msg, data, len);
             
            p2p_get_ie(fType, data, len, P2P_OUI, &peer_dev->p2p_buf, &peer_dev->p2p_buf_len);

            /* Mark the device for SD if any.
             */
            if (p2p_pending_sd_req(p2p_dev_ctx, peer_dev)) {
                peer_dev->dev.flags |= P2P_DEV_SD_SCHEDULE;
            }

            /* Add device nodes for each client info descriptor in the Group Info element,
             * if the Group Info element is present in the probe-resp frame.
             */
            if (msg.group_info && msg.group_info_len) {
                struct p2p_group_info info;

                /* Group Info element present.
                 */
                if (p2p_group_info_parse(msg.group_info, msg.group_info_len, &info) == A_OK) {
                    PDL_LIST pListItem;
                    struct host_p2p_dev *dev;
                    A_UINT8 num;

                    /* Clear old data for this group.
                     */
                    ITERATE_OVER_LIST(&p2p_dev_ctx->devices, pListItem) {
                        dev = A_CONTAINING_STRUCT(pListItem, struct host_p2p_dev, list);
                        if (A_MEMCMP((dev->dev).member_in_go_iface, addr, ETH_ALEN) == 0) {
                            A_MEMZERO((dev->dev).member_in_go_iface, ETH_ALEN);
                            A_MEMZERO((dev->dev).member_in_go_dev, ETH_ALEN);
                        }
                    }

                    /* Update device list based on latest client info desc from this
                     * group.
                     */

                    for (num=0; num < info.num_clients; num++) {
                        struct p2p_client_info *cli = &info.client[num];

                        /* If this client info desc. is our own, discard it.
                         */
                        if (A_MEMCMP(cli->p2p_device_addr, myaddr, ETH_ALEN)
                                == 0) {
                            continue;
                        }

                        dev = p2p_get_device(p2p_dev_ctx, cli->p2p_device_addr);                        
                        if (dev) {
                            bss_t *bss=NULL;
                            /* Update device details if what is present is not got directly from the
                             * client.
                             */
                            if ((dev->dev).flags & (P2P_DEV_GROUP_CLIENT_ONLY | P2P_DEV_PROBE_REQ_ONLY)) {
                                p2p_copy_client_info(dev, cli);
                            }

                            if ((dev->dev).flags & P2P_DEV_PROBE_REQ_ONLY) {
                                (dev->dev).flags &= ~P2P_DEV_PROBE_REQ_ONLY;
                            }


                            /* Update the WLAN Node timestamp for this dev.
                             */
                            bss = wmi_find_node(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev), cli->p2p_device_addr);
                            if (bss) {
                                wmi_node_update_timestamp(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev), bss);
                                wmi_node_return(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev), bss);
                            }
                        } else {
                            bss_t *bss=NULL;
                            /* Add new device to the list. Also setup a Node entry in the WLAN node
                             * (Scan table).
                             */
                            dev = p2p_create_device(p2p_dev_ctx, cli->p2p_device_addr);
                            if (dev == NULL) {
                                continue;
                            }

                            (dev->dev).flags |= P2P_DEV_GROUP_CLIENT_ONLY;
                            p2p_copy_client_info(dev, cli);
                            (dev->dev).oper_freq = channel;

                            /* Mark the device for SD if any.
                             */
                            if (p2p_pending_sd_req(p2p_dev_ctx, peer_dev)) {
                                peer_dev->dev.flags |= P2P_DEV_SD_SCHEDULE;
                            }

                            /* Setup a WLAN node entry */
                            bss = wmi_node_alloc(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev), 0);

                            if (bss == NULL) {
                                p2p_device_free(dev);
                                continue;
                            }

                            wmi_setup_node(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev), bss, cli->p2p_device_addr);

                            /* Initialize bss state.
                             */
                            bss->p2p_dev = dev;
                            /* Bump up the p2p dev ref. count.
                             */
                            p2p_increment_dev_ref_count(dev);
                            bss->ni_frametype = fType;

                            /* report device to driver & App */
                            A_WMI_P2PDEV_EVENT(p2p_dev_ctx->dev, addr, cli->p2p_device_addr, cli->pri_dev_type,
                                   cli->dev_name, cli->dev_name_len,
                                   cli->config_methods,
                                   cli->dev_capab, 0);

                            dev->dev.flags |= P2P_DEV_REPORTED;
                        }

                        A_MEMCPY((dev->dev).interface_addr, cli->p2p_interface_addr, ETH_ALEN);
                        A_MEMCPY((dev->dev).member_in_go_dev, p2p_dev_addr, ETH_ALEN);
                        A_MEMCPY((dev->dev).member_in_go_iface, addr, ETH_ALEN);
                    }
                }
            }
        } /* BEACON_FTYPE || PROBERESP_FTYPE */
        else if (fType == ACTION_MGMT_FTYPE) {
            peer_dev = p2p_create_device(p2p_dev_ctx, addr);

            if (!peer_dev) {
                return NULL;
            }

            /* Update device info from ACTION frame (GO Neg. Req. or Invitation
             * or Provisional Disc Req. frames).
             */
            p2p_add_device_info_from_action_frame(p2p_dev_ctx, peer_dev, addr,
                         &msg);
            p2p_get_ie(fType, data, len, P2P_OUI, &peer_dev->p2p_buf, &peer_dev->p2p_buf_len);

            /* Mark the device for SD if any.
             */
            if (p2p_pending_sd_req(p2p_dev_ctx, peer_dev)) {
                peer_dev->dev.flags |= P2P_DEV_SD_SCHEDULE;
            }

        } /* ACTION_MGMT_FTYPE */
        else if (fType == PROBEREQ_FTYPE) {

            /* Add this peer to device list only if it is a probe req with wildcard SSID. Probe
             * req frames with non-wildcard SSID shall come from p2p groups & hence not used to 
             * populate our device list.
             */
            if (msg.ssid == NULL || msg.ssid[1] != P2P_WILDCARD_SSID_LEN ||
                A_MEMCMP(msg.ssid + 2, P2P_WILDCARD_SSID, P2P_WILDCARD_SSID_LEN)
                    != 0) {
                return NULL;
            }

            peer_dev = p2p_create_device(p2p_dev_ctx, addr);

            if (!peer_dev) {
                return NULL;
            }

            if (msg.capability) {
                peer_dev->dev.dev_capab = msg.capability[0];
                peer_dev->dev.group_capab = msg.capability[1];
            }

            if (msg.listen_channel) {
                A_MEMCPY(peer_dev->dev.country, msg.listen_channel, 3);
                peer_dev->dev.listen_freq = p2p_channel_to_freq(peer_dev->dev.country,
                               msg.listen_channel[3],
                               msg.listen_channel[4]);
            }

            A_MEMCPY(peer_dev->dev.device_name, msg.wps_device_name, sizeof(peer_dev->dev.device_name));

            if (msg.pri_dev_type) {
                A_MEMCPY(peer_dev->dev.pri_dev_type, msg.pri_dev_type,
                    sizeof(peer_dev->dev.pri_dev_type));
            }
            else if(msg.wps_pri_dev_type) {
                A_MEMCPY(peer_dev->dev.pri_dev_type, msg.wps_pri_dev_type,
                    sizeof(peer_dev->dev.pri_dev_type));
            }

            /* Mark the device for SD if any.
             */
            if (p2p_pending_sd_req(p2p_dev_ctx, peer_dev)) {
                peer_dev->dev.flags |= P2P_DEV_SD_SCHEDULE;
            }

            if (peer_dev->dev.flags & P2P_DEV_REPORTED || 
                   peer_dev->dev.flags & P2P_DEV_USER_REJECTED) {
                return (void *)peer_dev;
            }
            peer_dev->dev.flags |= P2P_DEV_PROBE_REQ_ONLY;

            /* report device to driver & App */
            A_WMI_P2PDEV_EVENT(p2p_dev_ctx->dev, addr, peer_dev->dev.p2p_device_addr,
                peer_dev->dev.pri_dev_type,
                msg.wps_device_name, msg.wps_dev_name_len,
                msg.wps_config_methods,
                (msg.capability) ? msg.capability[0]:0, (msg.capability)?msg.capability[1]:0);

            peer_dev->dev.flags |= P2P_DEV_REPORTED;

        } /* PROBEREQ_FTYPE */
    }

    return (void *)peer_dev;
}

void p2p_go_neg_req_rx(void *ctx, const A_UINT8 *datap, A_UINT8 len)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct p2p_ctx *p2p_cmn_ctx = (struct p2p_ctx *)p2p_dev_ctx->p2p_ctx;
    WMI_P2P_GO_NEG_REQ_EVENT *ev = (WMI_P2P_GO_NEG_REQ_EVENT *)datap;
    struct host_p2p_dev *peer = NULL;
    enum p2p_status_code result = P2P_SC_SUCCESS;

    /* Get hold of the peer device from the device list.
     */
    peer = p2p_get_device(p2p_dev_ctx, ev->sa);

    A_ASSERT(peer != NULL);

    if (peer->dev.flags & P2P_DEV_USER_REJECTED) {
        result = P2P_SC_FAIL_REJECTED_BY_USER;
        goto send_rsp;
    }

    if (peer->dev.wps_method == WPS_NOT_READY) {
        struct p2p_ie p2p_ie;

        A_MEMZERO(&p2p_ie, sizeof(struct p2p_ie));
        result = P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE;
        peer->dev.flags |= P2P_DEV_PEER_WAITING_RESPONSE;

        /* Parse the dev passwd id from the WPS iE in the GO Neg. req.
         */
        p2p_parse_wps_ie(ev->wps_buf, ev->wps_buflen, &p2p_ie);

        A_WMI_P2PGONEG_REQ_EVENT(p2p_dev_ctx->dev, ev->sa, p2p_ie.dev_password_id);
        goto send_rsp;
    }
        /*Parse P2P IE */
    {
        struct p2p_ie p2p_ie;
        A_MEMZERO(&p2p_ie, sizeof(struct p2p_ie));
        p2p_parse_p2p_ie(ev->p2p_buf, ev->p2p_buflen, &p2p_ie);
        if(p2p_ie.capability[1] &  P2P_GROUP_CAPAB_PERSISTENT_GROUP)
            peer->persistent_grp = 1;
    }

    /* The P2P/WPS IEs from the req buffer are copied to the resp. buffer.
     */
    
    /* Send WMI_P2P_GO_NEG_REQ_RSP_CMD with the user result
     * , WPS method to use, go_intent to use.
     */
send_rsp:
    wmi_p2p_go_neg_rsp_cmd(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev), result,
        p2p_cmn_ctx->go_intent, peer->dev.wps_method, peer->dev.listen_freq,
            ev->wps_buf, ev->wps_buflen, ev->p2p_buf, ev->p2p_buflen,
                ev->dialog_token, peer->persistent_grp);
    
    return;
}

void p2p_invite_req_rx(void *ctx, const A_UINT8 *datap, A_UINT8 len)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    WMI_P2P_INVITE_REQ_EVENT *inv_req_ev = (WMI_P2P_INVITE_REQ_EVENT *)datap;
    struct host_p2p_dev *dev = NULL;
    enum p2p_status_code status = P2P_SC_SUCCESS;
    A_INT8 is_go=0;
    A_UINT8 grp_bssid[ETH_ALEN];

    /* Get hold of the peer device from the device list.
     */
    dev = p2p_get_device(p2p_dev_ctx, inv_req_ev->sa);

    /* Device is not there in the list. Send a failure response for the
     * Invitation.
     */
    if (dev == NULL) {
        status = P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE;
    } else {
    /* if the peer device is present in our dev list, check if the user has
     * authorized to accept an invitation from this peer to join an active 
     * group.
     */
        if (!inv_req_ev->is_persistent) {
            if (is_zero_mac_addr(p2p_dev_ctx->p2p_auth_invite) ||
                (A_MEMCMP(inv_req_ev->sa,
                    p2p_dev_ctx->p2p_auth_invite, ETH_ALEN) &&
                 (!is_zero_mac_addr(inv_req_ev->go_dev_addr) &&
                 A_MEMCMP(inv_req_ev->go_dev_addr,
                     p2p_dev_ctx->p2p_auth_invite, ETH_ALEN)
             )))
            {
                /* Do not accept Invitation automatically. TODO: We can
                 * notify the user here & request approval.
                 */
                status = P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE;
            }
        } else  {
            /*Persistent Group */
            if(A_MEMCMP(dev->ssid, inv_req_ev->ssid.ssid, dev->ssid_len) == 0) {
                if(dev->role_go) {
                    A_MEMCPY(grp_bssid, inv_req_ev->go_dev_addr, ETH_ALEN);
                    is_go = 1;
                }
            }
        } 
    } /* dev != NULL */

    /* The P2P/WPS IEs from the req buffer are copied to the resp. buffer.
     */
    wmi_p2p_invite_req_rsp_cmd(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev), status,
        is_go, grp_bssid, inv_req_ev->p2p_buf,
            inv_req_ev->p2p_buflen, inv_req_ev->dialog_token);

    return;
}

void p2p_prov_disc_req_rx(void *ctx, const A_UINT8 *datap, A_UINT8 len)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    WMI_P2P_PROV_DISC_REQ_EVENT *prov_disc_req_ev =
                                (WMI_P2P_PROV_DISC_REQ_EVENT *)datap;
    struct host_p2p_dev *peer = NULL;

    /* Get hold of the peer device from the device list. The peer dev should
     * be present in our dev list.(atleast added to the list from the bss_info
     * ev that the firmware would have sent prior to this event).
     */
    peer = p2p_get_device(p2p_dev_ctx, prov_disc_req_ev->sa);
    
    /* Set up the WPS method in use by the peer in the device context.
     */
    if (peer) {
        peer->dev.flags &= ~(P2P_DEV_PD_PEER_DISPLAY | P2P_DEV_PD_PEER_KEYPAD);
    }
    if (prov_disc_req_ev->wps_config_method & WPS_CONFIG_DISPLAY) {
        if (peer) {
            peer->dev.flags |= P2P_DEV_PD_PEER_KEYPAD;
        }
    } else if (prov_disc_req_ev->wps_config_method & WPS_CONFIG_KEYPAD) {
        if (peer) {
            peer->dev.flags |= P2P_DEV_PD_PEER_DISPLAY;
        }
    }
    
    /* Report PD request event to the Supplicant so that the user App can 
     * be prompted for any necessary inputs.
     */

printk(KERN_ALERT "wps_cfg_method: %d, dev_cfg_method: %d, \
    dev_capab: %d, grp_capab: %d\n", prov_disc_req_ev->wps_config_method,
    prov_disc_req_ev->dev_config_methods, prov_disc_req_ev->device_capab,
    prov_disc_req_ev->group_capab);

    A_WMI_P2P_PROV_DISC_REQ_EVENT(p2p_dev_ctx->dev, prov_disc_req_ev->sa,
        prov_disc_req_ev->wps_config_method, prov_disc_req_ev->dev_addr,
        prov_disc_req_ev->pri_dev_type, prov_disc_req_ev->device_name,
        prov_disc_req_ev->dev_name_len,
        prov_disc_req_ev->dev_config_methods, prov_disc_req_ev->device_capab,
        prov_disc_req_ev->group_capab);

    return;
}

void p2p_prov_disc_resp_rx(void *ctx, const A_UINT8 *datap, A_UINT8 len)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    WMI_P2P_PROV_DISC_RESP_EVENT *prov_disc_resp_ev =
                                (WMI_P2P_PROV_DISC_RESP_EVENT *)datap;
    struct host_p2p_dev *peer = NULL;

    /* Get hold of the peer device from the device list. The peer dev should
     * be present in our dev list.(atleast added to the list from the bss_info
     * ev that the firmware would have sent prior to this event).
     */
    peer = p2p_get_device(p2p_dev_ctx, prov_disc_resp_ev->peer);

    /* Set up the WPS method in use by the peer in the device context.
     */
    if (peer) {
        peer->dev.flags &= ~(P2P_DEV_PD_PEER_DISPLAY | P2P_DEV_PD_PEER_KEYPAD);
    }
    if (prov_disc_resp_ev->config_methods & WPS_CONFIG_DISPLAY) {
        if (peer) {
            peer->dev.flags |= P2P_DEV_PD_PEER_KEYPAD;
        }
    } else if (prov_disc_resp_ev->config_methods & WPS_CONFIG_KEYPAD) {
        if (peer) {
            peer->dev.flags |= P2P_DEV_PD_PEER_DISPLAY;
        }
    }

    peer->dev.req_config_methods = 0;
    
    /* Report PD response event to the Supplicant so that the user App can 
     * be prompted for any necessary inputs.
     */
    A_WMI_P2P_PROV_DISC_RESP_EVENT(p2p_dev_ctx->dev, prov_disc_resp_ev->peer,
        prov_disc_resp_ev->config_methods);

    return;
}

A_STATUS p2p_auth_invite(void *ctx, A_UINT8 *auth_peer)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    A_STATUS status = A_OK;

    /* Mark the peer as authorized by the user for Invitation.
     */
    A_MEMCPY(p2p_dev_ctx->p2p_auth_invite, auth_peer, ETH_ALEN);

    return status;
}

A_STATUS p2p_auth_go_neg(void *ctx, WMI_P2P_GO_NEG_START_CMD *auth_go_neg_param)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct p2p_ctx *p2p_cmn_ctx = (struct p2p_ctx *)p2p_dev_ctx->p2p_ctx;
    struct host_p2p_dev *peer = NULL;
    A_STATUS status = A_OK;

    /* Get hold of the peer device.
     */
    peer = p2p_get_device(ctx, auth_go_neg_param->peer_addr);
    if (peer == NULL) {
        /* Unknown peer device.
         */
        status = A_ERROR;    
    } else {
        /* Mark the peer as authorized by the user & ready for GO Negotiation.
         */
        peer->dev.flags &= ~P2P_DEV_NOT_YET_READY;
        peer->dev.flags &= ~P2P_DEV_USER_REJECTED;

        p2p_cmn_ctx->go_intent = auth_go_neg_param->go_intent;
        peer->persistent_grp  = auth_go_neg_param->persistent_grp; 
        peer->dev.wps_method = auth_go_neg_param->wps_method;
    }

    return status;
}

A_STATUS p2p_peer_reject(void *ctx, A_UINT8 *peer_addr)
{
    struct host_p2p_dev *peer = NULL;
    A_STATUS status = A_OK;

    /* Get hold of the peer device.
     */
    peer = p2p_get_device(ctx, peer_addr);
    if (peer == NULL) {
        /* Unknown peer device.
         */
        status = A_ERROR;    
    } else {
        peer->dev.flags |= P2P_DEV_USER_REJECTED;
    }

    return status;
}

A_STATUS p2p_go_neg_start(void *ctx, WMI_P2P_GO_NEG_START_CMD *go_neg_param)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct host_p2p_dev *peer = NULL;
    struct host_p2p_dev *peer_go = NULL;
    A_STATUS status = A_OK;

    /* Get hold of the peer device.
     */
    peer = p2p_get_device(ctx, go_neg_param->peer_addr);
    if (peer == NULL) {
        /* Unknown peer device.
         */
        return A_ERROR;    
    }

    /* Monotonically increasing per device dialog tokens - generated from host
     * & wraps back to 0. Send in WMI param from host.
     */
    peer->dev.dialog_token++;
    if (peer->dev.dialog_token == 0) {
        peer->dev.dialog_token = 1;
    }

    /* Mark the peer as authorized by the user & ready for GO Negotiation.
     */
    peer->dev.flags &= ~P2P_DEV_NOT_YET_READY;
    peer->dev.flags &= ~P2P_DEV_USER_REJECTED;

    peer->dev.wps_method = go_neg_param->wps_method;
    
    if (peer->dev.listen_freq) {
        go_neg_param->listen_freq = peer->dev.listen_freq;
    } else if (peer->dev.oper_freq) {
        go_neg_param->listen_freq = peer->dev.oper_freq;
    }

    go_neg_param->dialog_token = peer->dev.dialog_token;
    go_neg_param->dev_capab = peer->dev.dev_capab;


    /* If this peer is a member of a group, then get info about the GO of that group. This is
     * useful to send device discoverability request to its GO, in case this peer does not
     * respond to our GO Negotiation request.
     */
    if (!is_zero_mac_addr(peer->dev.member_in_go_dev)) {
        A_MEMCPY(go_neg_param->member_in_go_dev, peer->dev.member_in_go_dev, ETH_ALEN);

        peer_go = p2p_get_device(ctx, peer->dev.member_in_go_dev);

        if (peer_go) {

            peer_go->dev.dialog_token++;
            if (peer_go->dev.dialog_token == 0) {
                peer_go->dev.dialog_token = 1;
            }
            go_neg_param->go_dev_dialog_token = peer_go->dev.dialog_token;

            go_neg_param->go_oper_freq = peer_go->dev.oper_freq;

            if (peer_go->dev.oper_ssid_len) {
                A_MEMCPY(go_neg_param->peer_go_ssid.ssid, peer_go->dev.oper_ssid,
                             peer_go->dev.oper_ssid_len);
                go_neg_param->peer_go_ssid.ssidLength = peer_go->dev.oper_ssid_len;
            }
        }
    }

    status = wmi_p2p_go_neg_start(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev),
                go_neg_param);

    return status;
}

A_STATUS p2p_invite_cmd(void *ctx, WMI_P2P_INVITE_CMD *invite_param)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct host_p2p_dev *peer = NULL;
    A_STATUS status = A_OK;

    /* Get hold of the peer device.
     */
    peer = p2p_get_device(ctx, invite_param->peer_addr);
    if (peer == NULL) {
        /* Unknown peer device.
         */
        return A_ERROR;    
    }

    /* Monotonically increasing per device dialog tokens - generated from host
     * & wraps back to 0. Send in WMI param from host.
     */
    peer->dev.dialog_token++;
    if (peer->dev.dialog_token == 0) {
        peer->dev.dialog_token = 1;
    }

    invite_param->dialog_token = peer->dev.dialog_token;

    status = wmi_p2p_invite_cmd(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev),
                invite_param);

    return status;
}

A_STATUS p2p_prov_disc_req(void *ctx, A_UINT8 *peer_addr, A_UINT16 wps_method)
{

    struct host_p2p_dev *peer = NULL;

    /* Get hold of the peer device.
     */
    peer = p2p_get_device(ctx, peer_addr);
    if (peer == NULL) {
        /* Unknown peer device.
         */
        peer = p2p_get_device_intf_addrs(ctx, peer_addr);
    }

    if (peer == NULL || (peer->dev.flags & P2P_DEV_PROBE_REQ_ONLY)) {
        return A_ERROR;
    }

    peer->dev.req_config_methods = wps_method;

    /* Return from here. PD will happen in the context of p2p_find().
     */
    return A_OK;
}


void p2p_send_prov_disc_req(struct host_p2p_dev *peer)
{
    struct p2p_dev_ctx *p2p_dev_ctx = peer->p2p;
    WMI_P2P_PROV_DISC_REQ_CMD  prov_disc_req;


    /* Set up the PD Request cmd params.
     */
    A_MEMZERO(&prov_disc_req, sizeof(WMI_P2P_PROV_DISC_REQ_CMD));

    A_MEMCPY(prov_disc_req.peer, peer->dev.p2p_device_addr, ATH_MAC_LEN);
    prov_disc_req.wps_method = peer->dev.req_config_methods;

    /* Monotonically increasing per device dialog tokens - generated from host
     * & wraps back to 0. Send in WMI param from host.
     */
    peer->dev.dialog_token++;
    if (peer->dev.dialog_token == 0) {
        peer->dev.dialog_token = 1;
    }
    prov_disc_req.dialog_token = peer->dev.dialog_token;

    if (peer->dev.listen_freq) {
        prov_disc_req.listen_freq = peer->dev.listen_freq;
    } else if (peer->dev.oper_freq) {
        prov_disc_req.listen_freq = peer->dev.oper_freq;
    }

    if (peer->dev.oper_ssid_len) {
        A_MEMCPY(prov_disc_req.go_oper_ssid.ssid, peer->dev.oper_ssid, peer->dev.oper_ssid_len);
        prov_disc_req.go_oper_ssid.ssidLength = peer->dev.oper_ssid_len;

        if (!is_zero_mac_addr(peer->dev.interface_addr)) {
            A_MEMCPY(prov_disc_req.go_dev_addr, peer->dev.interface_addr,
                 ATH_MAC_LEN);
        } else {
            A_MEMCPY(prov_disc_req.go_dev_addr, prov_disc_req.peer,
                 ATH_MAC_LEN);
        }
    }

    wmi_p2p_prov_disc_cmd(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev), &prov_disc_req);

    return;
}

void p2p_free_all_devices(void *ctx)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;

    /*
     * In order to free all of the devices from the dl_list, we CANNOT
     * simply call p2p_device_free on all of the peers.  This is because
     * there might be references to these devices from the scan table, and
     * just freeing the devices would potentially cause a segfault.
     * Therefore, we use wmi_free_allnodes to eliminate all references from
     * the scan table as well as to free the devices from the dl_list.
     */
    wmi_free_allnodes(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev));
}

void p2p_device_free(void *peer_dev)
{
    struct host_p2p_dev *peer = (struct host_p2p_dev *)peer_dev;

    /* decrement the ref count & free up the node if the ref. cnt becomes 
     * zero.
     */
    if (--peer->ref_cnt == 0) {
        DL_ListRemove(&peer->list);
        A_PRINTF("-: %x:%x\n", (peer->dev).p2p_device_addr[4],
                    (peer->dev).p2p_device_addr[5]);
        A_FREE(peer);
    }

    A_PRINTF("-%d\n", peer->ref_cnt);
    return;
}

A_STATUS p2p_peer(void *ctx, A_UINT8 *peer_addr, A_UINT8 next)
{
    struct host_p2p_dev *peer = NULL;
    A_STATUS status;

    /* Get hold of the peer device.
     */
    peer = p2p_get_device(ctx, peer_addr);
    if (peer) {
        A_PRINTF("peer Found\n");
        status = A_OK;
    } else {
         A_PRINTF("peer NOTFound\n");
         status = A_DEVICE_NOT_FOUND;
    }

    return status;
}


A_STATUS p2p_get_device_p2p_buf(void *ctx, A_UINT8 *peer_addr, A_UINT8 **ni_buf, A_UINT8 *ni_buf_len)
{
   struct host_p2p_dev *peer = NULL;
    A_STATUS status;
    /* Get hold of the peer device */
    peer = p2p_get_device(ctx, peer_addr);
    if (peer) {
        *ni_buf = peer->p2p_buf;
        *ni_buf_len = peer->p2p_buf_len;
        status = A_OK;
    } else {
         status = A_DEVICE_NOT_FOUND;
    }

    return status;

}

A_STATUS p2p_get_ifaddr (void *ctx, A_UINT8 *dev_addr)
{
    struct host_p2p_dev *peer = NULL;
    A_STATUS status;

    /* Get hold of the peer device.
     */
    peer = p2p_get_device(ctx, dev_addr);
    if (peer) {
        A_MEMCPY((dev_addr+6), peer->dev.interface_addr, ATH_MAC_LEN);
        status = A_OK;
    } else {
         status = A_DEVICE_NOT_FOUND;
    }
    
    return status;
    
}

A_STATUS p2p_get_devaddr (void *ctx, A_UINT8 *intf_addr)
{
    struct host_p2p_dev *peer = NULL;
    A_STATUS status;

    /* Get hold of the peer device by interface address.
     */
    peer = p2p_get_device_intf_addrs(ctx, intf_addr);
    if (peer) {
        A_MEMCPY((intf_addr+6), (peer->dev).p2p_device_addr, ATH_MAC_LEN);
        status = A_OK;
    } else {
         status = A_DEVICE_NOT_FOUND;
    }
    
    return status;
    
}

A_STATUS wmi_p2p_get_go_params(void *ctx, A_UINT8 *go_dev_addr,
             A_UINT16 *oper_freq, A_UINT8 *ssid, A_UINT8 *ssid_len)
{
    struct host_p2p_dev *peer = NULL;
    A_STATUS status;

    /* Get hold of the peer device.
     */
    peer = p2p_get_device(ctx, go_dev_addr);
    if (peer) {
        A_PRINTF("peer Found\n");
        *oper_freq = peer->dev.oper_freq;

        if (peer->dev.oper_ssid) {
            A_MEMCPY(ssid, peer->dev.oper_ssid, peer->dev.oper_ssid_len);
            *ssid_len = peer->dev.oper_ssid_len;
        }
        status = A_OK;
    } else {
         A_PRINTF("peer NOTFound\n");
         status = A_DEVICE_NOT_FOUND;
    }

    return status;
}

void p2p_increment_dev_ref_count(struct host_p2p_dev *dev)
{
    /* Increment the ref count. The dev ages out in 2 mins time (configurable)
     * Unlikely to have more than 255 references to the dev before it
     * ages out.
     */
    dev->ref_cnt++;

    A_PRINTF("+%d\n", dev->ref_cnt);

    return;
}

static void p2p_free_sd_query(struct p2p_sd_query *q)
{
    if (q == NULL) {
        return;
    }
    if (q->tlvs) {
        A_FREE(q->tlvs);
    }
    A_FREE(q);
}

static int p2p_unlink_sd_query(void *ctx, struct p2p_sd_query *query)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct p2p_sd_query *q, *prev;

    q = p2p_dev_ctx->sd_queries;
    prev = NULL;

    while (q) {
        if (q == query) {
            if (prev)
                prev->next = q->next;
            else
                p2p_dev_ctx->sd_queries = q->next;
            if (p2p_dev_ctx->sd_queries == query)
                p2p_dev_ctx->sd_queries = NULL;
            return 1;
        }
        prev = q;
        q = q->next;
    }
    return 0;
}

void p2p_free_all_sd_queries(void *ctx)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct p2p_sd_query *q, *next;

    /*
     * Walk through the list and free all of the p2p_sd_queries
     */
    q = p2p_dev_ctx->sd_queries;

    while (q) {
        next = q->next;
        if (p2p_unlink_sd_query(p2p_dev_ctx, q)) {
            p2p_free_sd_query(q);
        }
        q = next;
    }

    /*
     * Null out the sd_queries pointer in the p2p_dev_ctx.  This
     * item no longer exists!
     */
    p2p_dev_ctx->sd_queries = NULL;

    /*
     * Now null out the curr_sd_query pointer because if it exists,
     * the sd_query has been freed already.
     */
    p2p_dev_ctx->curr_sd_query = NULL;
}

A_STATUS p2p_sd_request(void *ctx, A_UINT8 *peer_addr, A_UINT8 *tlv_buf, A_UINT8 tlv_buflen, A_UINT32 *qid)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct p2p_sd_query *q;

    q = A_MALLOC_NOWAIT(sizeof(struct p2p_sd_query));
    if (q == NULL) {
        return A_ERROR;
    }

    A_MEMZERO(q, sizeof(struct p2p_sd_query));

    if (is_zero_mac_addr(peer_addr)) {
        q->for_all_peers = 1;
    } else {
        A_MEMCPY(q->peer, peer_addr, ETH_ALEN);
    }

    q->tlvs = A_MALLOC_NOWAIT(tlv_buflen);
    if (q->tlvs == NULL) {
        p2p_free_sd_query(q);
        return A_ERROR;
    }
    A_MEMZERO(q->tlvs, tlv_buflen);

    A_MEMCPY(q->tlvs, tlv_buf, tlv_buflen);
    q->tlv_len = tlv_buflen;

    q->next = p2p_dev_ctx->sd_queries;
    p2p_dev_ctx->sd_queries = q;

    *qid = (A_UINT32)q;

    return A_OK;
}

A_STATUS p2p_sdpd_tx_cmd(void *ctx, WMI_P2P_SDPD_TX_CMD *sdpd_tx_cmd, A_UINT32 *qid)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    A_STATUS status;

    *qid = 0;
    if (sdpd_tx_cmd->type == WMI_P2P_SD_TYPE_GAS_INITIAL_REQ) {
        status = p2p_sd_request(ctx, sdpd_tx_cmd->peer_addr, sdpd_tx_cmd->tlv, sdpd_tx_cmd->tlv_length, qid);
    } else {
        status = wmi_p2p_sdpd_tx_cmd(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev), sdpd_tx_cmd);
    }

    return status;
}

A_STATUS p2p_sd_cancel_request(void *ctx, A_UINT32 qid)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;

    if (p2p_unlink_sd_query(p2p_dev_ctx, (void *)qid)) {
        p2p_free_sd_query((void *)qid);
    }

    return A_OK;
}

void p2p_start_sdpd_event_rx(void *ctx)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct host_p2p_dev *dev;
    PDL_LIST pListItem;
    WMI_P2P_SDPD_TX_CMD p2p_sdpd_req;
    struct p2p_sd_query *query;

    ITERATE_OVER_LIST(&p2p_dev_ctx->devices, pListItem) {

        dev = A_CONTAINING_STRUCT(pListItem, struct host_p2p_dev, list);

        /* DO Service Discovery */
        if (dev->dev.flags & P2P_DEV_SD_SCHEDULE) {
            A_MEMZERO(&p2p_sdpd_req, sizeof(WMI_P2P_SDPD_TX_CMD));

            query = p2p_pending_sd_req(p2p_dev_ctx, dev);

            p2p_dev_ctx->curr_sd_query = query;

            if (query == NULL) {
                continue;
            } else {
                if (dev->dev.listen_freq) {
                    p2p_sdpd_req.freq = dev->dev.listen_freq;
                } else if (dev->dev.oper_freq) {
                    p2p_sdpd_req.freq = dev->dev.oper_freq;
                }
                p2p_sdpd_req.type =  WMI_P2P_SD_TYPE_GAS_INITIAL_REQ;
                A_MEMCPY(p2p_sdpd_req.peer_addr, dev->dev.p2p_device_addr, ETH_ALEN);
                A_MEMCPY(p2p_sdpd_req.tlv, query->tlvs, query->tlv_len);
                p2p_sdpd_req.tlv_length = query->tlv_len;
                p2p_sdpd_req.update_indic = p2p_dev_ctx->srv_update_indic;

                wmi_p2p_sdpd_tx_cmd(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev), &p2p_sdpd_req);
                return;
            }
        } else if (dev->dev.req_config_methods && !(dev->dev.flags & P2P_DEV_PD_FOR_JOIN)) {
            /*Do Provisional Discovery */
            p2p_send_prov_disc_req(dev);
            return;
        }
    }

    /* Send the WMI_P2P_SDPD_STOP_CMD for the firmware to continue with Listen in FIND.
     */
    wmi_p2p_stop_sdpd(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev));

    return;
}

void p2p_sdpd_rx_event_rx(void *ctx, const A_UINT8 *datap, A_UINT8 len)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    WMI_P2P_SDPD_RX_EVENT *ev;

    ev = (WMI_P2P_SDPD_RX_EVENT *)datap;

    /* Pass the event up to the Supplicant */
    switch (ev->type) {
        case WMI_P2P_SD_TYPE_GAS_INITIAL_REQ:
        case WMI_P2P_SD_TYPE_GAS_COMEBACK_REQ:
        case WMI_P2P_SD_TYPE_GAS_INITIAL_RESP:
        case WMI_P2P_SD_TYPE_GAS_COMEBACK_RESP:
            A_WMI_P2P_SD_RX_EVENT(p2p_dev_ctx->dev, ev);
            break;
        case WMI_P2P_SD_TYPE_STATUS_IND:
            A_PRINTF("SDPD Status Indication\n");
            break;
        case WMI_P2P_PD_TYPE_RESP:
            A_PRINTF("PD response got\n");
            break;
        default:
            A_PRINTF("SDPD resp event %x not supported \n", ev->type);
            break;
    }

    /* Check the SD transaction status if it is a SD response & drive the FSM appropriately.
     */
    switch (ev->type) {
        case WMI_P2P_SD_TYPE_GAS_INITIAL_RESP:
        case WMI_P2P_SD_TYPE_GAS_COMEBACK_RESP:

            if (ev->transaction_status == WMI_P2P_SDPD_TRANSACTION_COMP) {
                struct host_p2p_dev *sd_peer = NULL;

                sd_peer = p2p_get_device(p2p_dev_ctx, ev->peer_addr);

                if (sd_peer) {
	                sd_peer->dev.flags |= P2P_DEV_SD_INFO;
                }

                /* Remove directed queries that have been handled.
                 */
                if (p2p_dev_ctx->curr_sd_query && !(p2p_dev_ctx->curr_sd_query->for_all_peers)) {
                    if (p2p_unlink_sd_query(p2p_dev_ctx, p2p_dev_ctx->curr_sd_query)) {
                        p2p_free_sd_query((void *)p2p_dev_ctx->curr_sd_query);
                    }
                }
            }

        case WMI_P2P_SD_TYPE_STATUS_IND:

            if (ev->transaction_status == WMI_P2P_SDPD_TRANSACTION_COMP) {
                struct host_p2p_dev *sd_peer = NULL;

                /* Clean up SD pending state for this dev for which the SD transaction
                 * completed.
                 */
                sd_peer = p2p_get_device(p2p_dev_ctx, ev->peer_addr);

                /* Query for this device completed.
                 */
                if (sd_peer) {
                    sd_peer->dev.flags &= ~P2P_DEV_SD_SCHEDULE;
                }

                /* Proceed with the next SD/PD. 
                 */
                p2p_start_sdpd_event_rx(ctx);
            }
            break;

        case WMI_P2P_PD_TYPE_RESP:

            /* Proceed with the next SD/PD. 
             */
            p2p_start_sdpd_event_rx(ctx);
            break;
        
        default:
             break;
    }

    return;
}

static struct p2p_sd_query *p2p_pending_sd_req(struct p2p_dev_ctx *p2p_dev_ctx,
                     struct host_p2p_dev *dev)
{
    struct p2p_sd_query *q;

    if (!(dev->dev.dev_capab & P2P_DEV_CAPAB_SERVICE_DISCOVERY)) {
        return NULL; /* peer does not support SD */
    }

    for (q = p2p_dev_ctx->sd_queries; q; q = q->next) {
        if (q->for_all_peers && !(dev->dev.flags & P2P_DEV_SD_INFO)) {
            return q;
        }
        if (!q->for_all_peers &&
            A_MEMCMP(q->peer, dev->dev.p2p_device_addr, ETH_ALEN) == 0)
            return q;
    }

    return NULL;
}

void p2p_go_neg_complete_rx(void *ctx, const A_UINT8 *datap, A_UINT8 len)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    WMI_P2P_GO_NEG_RESULT_EVENT *ev = (WMI_P2P_GO_NEG_RESULT_EVENT *)datap;
    struct host_p2p_dev *peer = NULL;

    /* Get hold of the peer device from the device list.
     */
    peer = p2p_get_device(p2p_dev_ctx, ev->peer_device_addr);

    A_ASSERT(peer != NULL);
    /*Store minimal persistent group info for Invitation processing*/
    if(ev->persistent_grp) {
        peer->persistent_grp = ev->persistent_grp;
        peer->role_go = ev->role_go;
        peer->ssid_len = ev->ssid_len;
        A_MEMCPY(peer->ssid, ev->ssid, ev->ssid_len);
    }
     return;
}
/**
 * wpa_ssid_txt - Convert SSID to a printable string
 * @ssid: SSID (32-octet string)
 * @ssid_len: Length of ssid in octets
 * Returns: Pointer to a printable string
 *
 * This function can be used to convert SSIDs into printable form. In most
 * cases, SSIDs do not use unprintable characters, but IEEE 802.11 standard
 * does not limit the used character set, so anything could be used in an SSID.
 *
 * This function uses a static buffer, so only one call can be used at the
 * time, i.e., this is not re-entrant and the returned buffer must be used
 * before calling this again.
 */
static const char * wpa_ssid_txt(const u8 *ssid, A_UINT8 ssid_len)
{
    static char ssid_txt[33];
    char *pos;

    if (ssid_len > 32)
        ssid_len = 32;
    memcpy(ssid_txt, ssid, ssid_len);
    ssid_txt[ssid_len] = '\0';
    for (pos = ssid_txt; *pos != '\0'; pos++) {
      if ((u8) *pos < 32 || (u8) *pos >= 127)
        *pos = '_';
    }
    return ssid_txt;
}

static const char * p2p_go_state_text(enum p2p_go_state go_state)
{
    switch (go_state) {
    case UNKNOWN_GO:
        return "unknown";
    case LOCAL_GO:
        return "local";
    case REMOTE_GO:
        return "remote";
    }

    return "??";
}

static char * wps_dev_type_bin2str(const u8 dev_type[WPS_DEV_TYPE_LEN], char *buf,
                            A_UINT32 buf_len)
{
    int ret;

    ret = snprintf(buf, buf_len, "%u-%08X-%u",
		    WPA_GET_BE16(dev_type), WPA_GET_BE32(&dev_type[2]),
		    WPA_GET_BE16(&dev_type[6]));
    if (ret < 0 || (unsigned int) ret >= buf_len)
        return NULL;

    return buf;
}


static const char * p2p_wps_method_text(enum p2p_wps_method method)
{
    switch (method) {
    case WPS_NOT_READY:
        return "not-ready";
    case WPS_PIN_LABEL:
        return "Label";
    case WPS_PIN_DISPLAY:
        return "Display";
    case WPS_PIN_KEYPAD:
        return "Keypad";
    case WPS_PBC:
        return "PBC";
    }

    return "??";
}


int p2p_get_peer_info(void *ctx, A_UINT8 *peer_addr, A_UINT8 *buf, A_UINT32 buflen)
{
    struct host_p2p_dev *peer = NULL;
    struct p2p_device *dev;
    int res;
    A_UINT8 *pos, *end;
    A_UINT8 devtype[WPS_DEV_TYPE_BUFSIZE];
    bss_t *bss;
    A_UINT32 level = 0;
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;

    /* Get hold of the peer device
     */
    peer = p2p_get_device(ctx, peer_addr);
    if (!peer) {
        return 0;
    }

    dev = &peer->dev;

    bss = wmi_find_node(A_WMI_GET_WMI_CTX(p2p_dev_ctx->dev), dev->p2p_device_addr);
    if(bss) {
        level = bss->ni_snr;
        /* NB: max is 94 because noise is hardcoded to 161 */
        if (level > 94)
            level = 94;

        level += 161;   /*Add Noise -95dBm*/
        /*level greater than Noise max_level*/
        if(level >=64)
            level -=0x100;
    }
    pos = buf;
    end = buf + buflen;

    res = snprintf(pos, end - pos, MACSTR "\n",
                      MAC2STR(dev->p2p_device_addr));

    if (res < 0 || res >= end - pos) {
        return pos - buf;
        return A_NO_MEMORY;
    }

    pos += res;

    res = snprintf(pos, end - pos,
                      "listen_freq=%d\n"
                      "level=%d\n"
                      "wps_method=%s\n"
                      "interface_addr=" MACSTR "\n"
                      "member_in_go_dev=" MACSTR "\n"
                      "member_in_go_iface=" MACSTR "\n"
                      "pri_dev_type=%s\n"
                      "device_name=%s\n"
                      "config_methods=0x%x\n"
                      "dev_capab=0x%x\n"
                      "group_capab=0x%x\n"
                      "go_neg_req_sent=%d\n"
                      "go_state=%s\n"
                      "dialog_token=%u\n"
                      "intended_addr=" MACSTR "\n"
                      "country=%c%c\n"
                      "oper_freq=%d\n"
                      "req_config_methods=0x%x\n"
                      "flags=%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n"
                      "status=%d\n"
                      "wait_count=%u\n"
                      "invitation_reqs=%u\n",
                      dev->listen_freq,
                      level,
                      p2p_wps_method_text(dev->wps_method),
                      MAC2STR(dev->interface_addr),
                      MAC2STR(dev->member_in_go_dev),
                      MAC2STR(dev->member_in_go_iface),
                      wps_dev_type_bin2str(dev->pri_dev_type,
                                           devtype, sizeof(devtype)),
                      dev->device_name,
                      dev->config_methods,
                      dev->dev_capab,
                      dev->group_capab,
                      dev->go_neg_req_sent,
                      p2p_go_state_text(dev->go_state),
                      dev->dialog_token,
                      MAC2STR(dev->intended_addr),
                      dev->country[0] ? dev->country[0] : '_',
                      dev->country[1] ? dev->country[1] : '_',
                      dev->oper_freq,
                      dev->req_config_methods,
                      dev->flags & P2P_DEV_PROBE_REQ_ONLY ?
                      "[PROBE_REQ_ONLY]" : "",
                      dev->flags & P2P_DEV_REPORTED ? "[REPORTED]" : "",
                      dev->flags & P2P_DEV_NOT_YET_READY ?
                      "[NOT_YET_READY]" : "",
                      dev->flags & P2P_DEV_SD_INFO ? "[SD_INFO]" : "",
                      dev->flags & P2P_DEV_SD_SCHEDULE ? "[SD_SCHEDULE]" :
                      "",
                      dev->flags & P2P_DEV_PD_PEER_DISPLAY ?
                      "[PD_PEER_DISPLAY]" : "",
                      dev->flags & P2P_DEV_PD_PEER_KEYPAD ?
                      "[PD_PEER_KEYPAD]" : "",
                      dev->flags & P2P_DEV_USER_REJECTED ?
                      "[USER_REJECTED]" : "",
                      dev->flags & P2P_DEV_PEER_WAITING_RESPONSE ?
                      "[PEER_WAITING_RESPONSE]" : "",
                      dev->flags & P2P_DEV_PREFER_PERSISTENT_GROUP ?
                      "[PREFER_PERSISTENT_GROUP]" : "",
                      dev->flags & P2P_DEV_WAIT_GO_NEG_RESPONSE ?
                      "[WAIT_GO_NEG_RESPONSE]" : "",
                      dev->flags & P2P_DEV_WAIT_GO_NEG_CONFIRM ?
                      "[WAIT_GO_NEG_CONFIRM]" : "",
                      dev->flags & P2P_DEV_GROUP_CLIENT_ONLY ?
                      "[GROUP_CLIENT_ONLY]" : "",
                      dev->flags & P2P_DEV_FORCE_FREQ ?
                      "[FORCE_FREQ]" : "",
                      dev->status,
                      dev->wait_count,
                      dev->invitation_reqs);
    if (res < 0 || res >= end - pos) {
        return pos - buf;
    }

    pos += res;

    if (dev->ext_listen_period) {
        res = snprintf(pos, end - pos,
                          "ext_listen_period=%u\n"
                          "ext_listen_interval=%u\n",
                          dev->ext_listen_period,
                          dev->ext_listen_interval);
        if (res < 0 || res >= end - pos) {
	  return pos - buf;
        }
        pos += res;
    }

    if (dev->oper_ssid_len) {
        res = snprintf(pos, end - pos,
                          "oper_ssid=%s\n",
                          wpa_ssid_txt(dev->oper_ssid,
                                       dev->oper_ssid_len));
        if (res < 0 || res >= end - pos) {
            return pos - buf;
        }
    }
    return pos - buf;
}

int p2p_get_next_addr(void *ctx, A_UINT8 *addr, A_UINT8 *buf, A_UINT32 buflen, int first_element)
{
    struct host_p2p_dev *peer = NULL;
    struct p2p_device *dev;
    int res;
    A_UINT8 *pos, *end;

    peer = p2p_get_next_device(ctx, addr, first_element);

    if (!peer) {
        return 0;
    }

    dev = &peer->dev;

    pos = buf;
    end = buf + buflen;

    res = snprintf(pos, end - pos, MACSTR "\n",
                      MAC2STR(dev->p2p_device_addr));

    if (res < 0 || res >= end - pos) {
        return pos - buf;
        return A_NO_MEMORY;
    }

    pos += res;

    return pos - buf;
}

void p2p_clear_peers_reported_flag(void *ctx)
{
    struct p2p_dev_ctx *p2p_dev_ctx = (struct p2p_dev_ctx *)ctx;
    struct host_p2p_dev *dev;
    PDL_LIST pListItem;

    ITERATE_OVER_LIST(&p2p_dev_ctx->devices, pListItem) {
        dev = A_CONTAINING_STRUCT(pListItem, struct host_p2p_dev, list);
        dev->dev.flags &= (~P2P_DEV_REPORTED);
    }
}

void p2p_clear_peers_authorized_flag(void *ctx, const A_UINT8 *addr)
{
    struct host_p2p_dev *peer = NULL;

    /* Get hold of the peer device.
     */
    peer = p2p_get_device(ctx, addr);
    if (peer == NULL) {
        /* Find using interface addr.
         */
        peer = p2p_get_device_intf_addrs(ctx, addr);
    }

    if (peer) {
        peer->dev.wps_method = WPS_NOT_READY;
    }

    return;
}

