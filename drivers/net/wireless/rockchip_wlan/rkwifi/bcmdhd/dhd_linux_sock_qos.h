/*
 * Header file for DHD TPA (Traffic Pattern Analyzer)
 *
 * Provides type definitions and function prototypes to call into
 * DHD's QOS on Socket Flow module.
 *
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
 *
 */

#ifndef _DHD_LINUX_TPA_H_
#define _DHD_LINUX_TPA_H_

struct dhd_sock_flow_info;

#if defined(DHD_QOS_ON_SOCK_FLOW)
#define QOS_SAMPLING_INTVL_MS 100
/* Feature Enabed original implementation */
int dhd_init_sock_flows_buf(dhd_info_t *dhd, uint watchdog_ms);
int dhd_deinit_sock_flows_buf(dhd_info_t *dhd);
void dhd_update_sock_flows(dhd_info_t *dhd, struct sk_buff *skb);
void dhd_analyze_sock_flows(dhd_info_t *dhd, uint32 watchdog_ms);

/* sysfs call backs */
unsigned long dhd_sock_qos_get_status(dhd_info_t *dhd);
void dhd_sock_qos_set_status(dhd_info_t *dhd, unsigned long on_off);
ssize_t dhd_sock_qos_show_stats(dhd_info_t *dhd, char *buf, ssize_t sz);
void dhd_sock_qos_clear_stats(dhd_info_t *dhd);
unsigned long dhd_sock_qos_get_force_upgrade(dhd_info_t *dhd);
void dhd_sock_qos_set_force_upgrade(dhd_info_t *dhd, unsigned long force_upgrade);
int dhd_sock_qos_get_numfl_upgrd_thresh(dhd_info_t *dhd);
void dhd_sock_qos_set_numfl_upgrd_thresh(dhd_info_t *dhd, int upgrade_thresh);
void dhd_sock_qos_get_avgpktsize_thresh(dhd_info_t *dhd,
		unsigned long *avgpktsize_low,
		unsigned long *avgpktsize_high);
void dhd_sock_qos_set_avgpktsize_thresh(dhd_info_t *dhd,
		unsigned long avgpktsize_low,
		unsigned long avgpktsize_high);
void dhd_sock_qos_get_numpkts_thresh(dhd_info_t *dhd,
		unsigned long *numpkts_low,
		unsigned long *numpkts_high);
void dhd_sock_qos_set_numpkts_thresh(dhd_info_t *dhd,
		unsigned long numpkts_low,
		unsigned long numpkts_high);
void dhd_sock_qos_get_detectcnt_thresh(dhd_info_t *dhd,
		unsigned char *detectcnt_inc,
		unsigned char *detectcnt_dec);
void dhd_sock_qos_set_detectcnt_thresh(dhd_info_t *dhd,
		unsigned char detectcnt_inc,
		unsigned char detectcnt_dec);
int dhd_sock_qos_get_detectcnt_upgrd_thresh(dhd_info_t *dhd);
void dhd_sock_qos_set_detectcnt_upgrd_thresh(dhd_info_t *dhd,
		unsigned char detect_upgrd_thresh);
int dhd_sock_qos_get_maxfl(dhd_info_t *dhd);
void dhd_sock_qos_set_maxfl(dhd_info_t *dhd, unsigned int maxfl);

/* Update from Bus Layer */
void dhd_sock_qos_update_bus_flowid(dhd_info_t *dhd, void *pktbuf,
	uint32 bus_flow_id);

#else
/* Feature Disabled dummy implementations */

inline int dhd_init_sock_flows_buf(dhd_info_t *dhd, uint watchdog_ms)
{
		BCM_REFERENCE(dhd);
		return BCME_UNSUPPORTED;
}

inline int dhd_deinit_sock_flows_buf(dhd_info_t *dhd)
{
		BCM_REFERENCE(dhd);
		return BCME_UNSUPPORTED;
}

inline void dhd_update_sock_flows(dhd_info_t *dhd, struct sk_buff *skb)
{
		BCM_REFERENCE(dhd);
		BCM_REFERENCE(skb);
		return;
}

inline void dhd_analyze_sock_flows(dhd_info_t *dhd, uint32 watchdog_ms)
{
	BCM_REFERENCE(dhd);
	BCM_REFERENCE(dhd_watchdog_ms);
	return;
}

inline void dhd_sock_qos_update_bus_flowid(dhd_info_t *dhd, void *pktbuf,
	uint32 bus_flow_id)
{
	BCM_REFERENCE(dhd);
	BCM_REFERENCE(pktbuf);
	BCM_REFERENCE(bus_flow_id);
}
#endif  /* End of !DHD_QOS_ON_SOCK_FLOW */

#endif /* _DHD_LINUX_TPA_H_ */
