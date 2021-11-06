/*
 * Header file for QOS Algorithm on DHD
 *
 * Provides type definitions and function prototypes for the QOS Algorithm
 * Note that this algorithm is a platform independent layer
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
 */

#ifndef _DHD_QOS_ALGO_H_
#define _DHD_QOS_ALGO_H_

#define LOWLAT_AVG_PKT_SIZE_LOW 50u
#define LOWLAT_AVG_PKT_SIZE_HIGH 200u
#define LOWLAT_NUM_PKTS_LOW 1u
#define LOWLAT_NUM_PKTS_HIGH 8u
#define LOWLAT_DETECT_CNT_INC_THRESH 10u
#define LOWLAT_DETECT_CNT_DEC_THRESH 0u
#define LOWLAT_DETECT_CNT_UPGRADE_THRESH 4u

typedef struct qos_stat
{
	/* Statistics */
	unsigned long tx_pkts_prev;
	unsigned long tx_bytes_prev;
	unsigned long tx_pkts;
	unsigned long tx_bytes;

	/* low latency flow detection algorithm counts */
	unsigned char lowlat_detect_count;
	bool lowlat_flow;
} qos_stat_t;

/* QoS alogrithm parameter, controllable at runtime */
typedef struct _qos_algo_params
{
	/* The avg Tx packet size in the sampling interval must be between
	 * these two thresholds for QoS upgrade to take place.
	 * default values = LOWLAT_AVG_PKT_SIZE_LOW, LOWLAT_AVG_PKT_SIZE_HIGH
	 */
	unsigned long avg_pkt_size_low_thresh;
	unsigned long avg_pkt_size_high_thresh;
	/* The number of Tx packets in the sampling interval must be
	 * between these two thresholds for QoS upgrade to happen.
	 * default values = LOWLAT_NUM_PKTS_LOW, LOWLAT_NUM_PKTS_HIGH
	 */
	unsigned long num_pkts_low_thresh;
	unsigned long num_pkts_high_thresh;
	/* If low latency traffic is detected, then the low latency count
	 * is incremented till the first threshold is hit.
	 * If traffic ceases to be low latency, then the count is
	 * decremented till the second threshold is hit.
	 * default values = LOWLAT_DETECT_CNT_INC_THRESH, LOWLAT_DETECT_CNT_DEC_THRESH
	 */
	unsigned char detect_cnt_inc_thresh;
	unsigned char detect_cnt_dec_thresh;
	/* If the low latency count crosses this threshold, the flow will be upgraded.
	 * Default value =  LOWLAT_DETECT_CNT_UPGRADE_THRESH
	 */
	unsigned char detect_cnt_upgrade_thresh;
} qos_algo_params_t;

#define QOS_PARAMS(x) (&x->psk_qos->qos_params);

/*
 * Operates on a flow and returns 1 for upgrade and 0 for
 * no up-grade
 */
int dhd_qos_algo(dhd_info_t *dhd, qos_stat_t *qos, qos_algo_params_t *qos_params);
int qos_algo_params_init(qos_algo_params_t *qos_params);
#endif /* _DHD_QOS_ALGO_H_ */
