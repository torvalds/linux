/** @file mlan_uap.h
 *
 *  @brief This file contains related macros, enum, and struct
 *  of uap functionalities
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

/********************************************************
Change log:
    02/05/2009: initial version
********************************************************/

#ifndef _MLAN_UAP_H_
#define _MLAN_UAP_H_

#ifdef BIG_ENDIAN_SUPPORT
/** Convert TxPD to little endian format from CPU format */
#define uap_endian_convert_TxPD(x)                                          \
	{                                                                   \
	    (x)->tx_pkt_length = wlan_cpu_to_le16((x)->tx_pkt_length);      \
	    (x)->tx_pkt_offset = wlan_cpu_to_le16((x)->tx_pkt_offset);      \
	    (x)->tx_pkt_type   = wlan_cpu_to_le16((x)->tx_pkt_type);        \
	    (x)->tx_control    = wlan_cpu_to_le32((x)->tx_control);         \
        (x)->tx_control_1  = wlan_cpu_to_le32((x)->tx_control_1);         \
	}
/** Convert RxPD from little endian format to CPU format */
#define uap_endian_convert_RxPD(x)                                          \
	{                                                                   \
	    (x)->rx_pkt_length = wlan_le16_to_cpu((x)->rx_pkt_length);      \
	    (x)->rx_pkt_offset = wlan_le16_to_cpu((x)->rx_pkt_offset);      \
	    (x)->rx_pkt_type   = wlan_le16_to_cpu((x)->rx_pkt_type);        \
	    (x)->seq_num       = wlan_le16_to_cpu((x)->seq_num);            \
        (x)->rx_info       = wlan_le32_to_cpu((x)->rx_info);            \
	}
#else
/** Convert TxPD to little endian format from CPU format */
#define uap_endian_convert_TxPD(x)  do {} while (0)
/** Convert RxPD from little endian format to CPU format */
#define uap_endian_convert_RxPD(x)  do {} while (0)
#endif /* BIG_ENDIAN_SUPPORT */

mlan_status wlan_uap_get_channel(IN pmlan_private pmpriv);

mlan_status wlan_uap_set_channel(IN pmlan_private pmpriv,
				 IN Band_Config_t uap_band_cfg,
				 IN t_u8 channel);

mlan_status wlan_uap_get_beacon_dtim(IN pmlan_private pmpriv);

mlan_status wlan_ops_uap_ioctl(t_void *adapter, pmlan_ioctl_req pioctl_req);

mlan_status wlan_ops_uap_prepare_cmd(IN t_void *priv,
				     IN t_u16 cmd_no,
				     IN t_u16 cmd_action,
				     IN t_u32 cmd_oid,
				     IN t_void *pioctl_buf,
				     IN t_void *pdata_buf, IN t_void *pcmd_buf);

mlan_status wlan_ops_uap_process_cmdresp(IN t_void *priv,
					 IN t_u16 cmdresp_no,
					 IN t_void *pcmd_buf,
					 IN t_void *pioctl);

mlan_status wlan_ops_uap_process_rx_packet(IN t_void *adapter,
					   IN pmlan_buffer pmbuf);

mlan_status wlan_ops_uap_process_event(IN t_void *priv);

t_void *wlan_ops_uap_process_txpd(IN t_void *priv, IN pmlan_buffer pmbuf);

mlan_status wlan_ops_uap_init_cmd(IN t_void *priv, IN t_u8 first_bss);

#endif /* _MLAN_UAP_H_ */
