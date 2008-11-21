/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2007 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _DCB_CONFIG_H_
#define _DCB_CONFIG_H_

#include "ixgbe_type.h"

/* DCB data structures */

#define IXGBE_MAX_PACKET_BUFFERS 8
#define MAX_USER_PRIORITY        8
#define MAX_TRAFFIC_CLASS        8
#define MAX_BW_GROUP             8
#define BW_PERCENT               100

#define DCB_TX_CONFIG            0
#define DCB_RX_CONFIG            1

/* DCB error Codes */
#define DCB_SUCCESS              0
#define DCB_ERR_CONFIG           -1
#define DCB_ERR_PARAM            -2

/* Transmit and receive Errors */
/* Error in bandwidth group allocation */
#define DCB_ERR_BW_GROUP        -3
/* Error in traffic class bandwidth allocation */
#define DCB_ERR_TC_BW           -4
/* Traffic class has both link strict and group strict enabled */
#define DCB_ERR_LS_GS           -5
/* Link strict traffic class has non zero bandwidth */
#define DCB_ERR_LS_BW_NONZERO   -6
/* Link strict bandwidth group has non zero bandwidth */
#define DCB_ERR_LS_BWG_NONZERO  -7
/*  Traffic class has zero bandwidth */
#define DCB_ERR_TC_BW_ZERO      -8

#define DCB_NOT_IMPLEMENTED      0x7FFFFFFF

struct dcb_pfc_tc_debug {
	u8  tc;
	u8  pause_status;
	u64 pause_quanta;
};

enum strict_prio_type {
	prio_none = 0,
	prio_group,
	prio_link
};

/* Traffic class bandwidth allocation per direction */
struct tc_bw_alloc {
	u8 bwg_id;		  /* Bandwidth Group (BWG) ID */
	u8 bwg_percent;		  /* % of BWG's bandwidth */
	u8 link_percent;	  /* % of link bandwidth */
	u8 up_to_tc_bitmap;	  /* User Priority to Traffic Class mapping */
	u16 data_credits_refill;  /* Credit refill amount in 64B granularity */
	u16 data_credits_max;	  /* Max credits for a configured packet buffer
				   * in 64B granularity.*/
	enum strict_prio_type prio_type; /* Link or Group Strict Priority */
};

enum dcb_pfc_type {
	pfc_disabled = 0,
	pfc_enabled_full,
	pfc_enabled_tx,
	pfc_enabled_rx
};

/* Traffic class configuration */
struct tc_configuration {
	struct tc_bw_alloc path[2]; /* One each for Tx/Rx */
	enum dcb_pfc_type  dcb_pfc; /* Class based flow control setting */

	u16 desc_credits_max; /* For Tx Descriptor arbitration */
	u8 tc; /* Traffic class (TC) */
};

enum dcb_rx_pba_cfg {
	pba_equal,     /* PBA[0-7] each use 64KB FIFO */
	pba_80_48      /* PBA[0-3] each use 80KB, PBA[4-7] each use 48KB */
};

/*
 * This structure contains many values encoded as fixed-point
 * numbers, meaning that some of bits are dedicated to the
 * magnitude and others to the fraction part. In the comments
 * this is shown as f=n, where n is the number of fraction bits.
 * These fraction bits are always the low-order bits. The size
 * of the magnitude is not specified.
 */
struct bcn_config {
	u32 rp_admin_mode[MAX_TRAFFIC_CLASS]; /* BCN enabled, per TC */
	u32 bcna_option[2]; /* BCNA Port + MAC Addr */
	u32 rp_w;        /* Derivative Weight, f=3 */
	u32 rp_gi;       /* Increase Gain, f=12 */
	u32 rp_gd;       /* Decrease Gain, f=12 */
	u32 rp_ru;       /* Rate Unit */
	u32 rp_alpha;    /* Max Decrease Factor, f=12 */
	u32 rp_beta;     /* Max Increase Factor, f=12 */
	u32 rp_ri;       /* Initial Rate */
	u32 rp_td;       /* Drift Interval Timer */
	u32 rp_rd;       /* Drift Increase */
	u32 rp_tmax;     /* Severe Congestion Backoff Timer Range */
	u32 rp_rmin;     /* Severe Congestion Restart Rate */
	u32 rp_wrtt;     /* RTT Moving Average Weight */
};

struct ixgbe_dcb_config {
	struct bcn_config bcn;

	struct tc_configuration tc_config[MAX_TRAFFIC_CLASS];
	u8     bw_percentage[2][MAX_BW_GROUP]; /* One each for Tx/Rx */

	bool  round_robin_enable;

	enum dcb_rx_pba_cfg rx_pba_cfg;

	u32  dcb_cfg_version; /* Not used...OS-specific? */
	u32  link_speed; /* For bandwidth allocation validation purpose */
};

/* DCB driver APIs */

/* DCB rule checking function.*/
s32 ixgbe_dcb_check_config(struct ixgbe_dcb_config *config);

/* DCB credits calculation */
s32 ixgbe_dcb_calculate_tc_credits(struct ixgbe_dcb_config *, u8);

/* DCB PFC functions */
s32 ixgbe_dcb_config_pfc(struct ixgbe_hw *, struct ixgbe_dcb_config *g);
s32 ixgbe_dcb_get_pfc_stats(struct ixgbe_hw *, struct ixgbe_hw_stats *, u8);

/* DCB traffic class stats */
s32 ixgbe_dcb_config_tc_stats(struct ixgbe_hw *);
s32 ixgbe_dcb_get_tc_stats(struct ixgbe_hw *, struct ixgbe_hw_stats *, u8);

/* DCB config arbiters */
s32 ixgbe_dcb_config_tx_desc_arbiter(struct ixgbe_hw *,
                                     struct ixgbe_dcb_config *);
s32 ixgbe_dcb_config_tx_data_arbiter(struct ixgbe_hw *,
                                     struct ixgbe_dcb_config *);
s32 ixgbe_dcb_config_rx_arbiter(struct ixgbe_hw *, struct ixgbe_dcb_config *);

/* DCB hw initialization */
s32 ixgbe_dcb_hw_config(struct ixgbe_hw *, struct ixgbe_dcb_config *);

/* DCB definitions for credit calculation */
#define MAX_CREDIT_REFILL       511  /* 0x1FF * 64B = 32704B */
#define MINIMUM_CREDIT_REFILL   5    /* 5*64B = 320B */
#define MINIMUM_CREDIT_FOR_JUMBO 145  /* 145= UpperBound((9*1024+54)/64B) for 9KB jumbo frame */
#define DCB_MAX_TSO_SIZE        (32*1024) /* MAX TSO packet size supported in DCB mode */
#define MINIMUM_CREDIT_FOR_TSO  (DCB_MAX_TSO_SIZE/64 + 1) /* 513 for 32KB TSO packet */
#define MAX_CREDIT              4095 /* Maximum credit supported: 256KB * 1204 / 64B */

#endif /* _DCB_CONFIG_H */
