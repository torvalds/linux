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

#ifndef _DCB_82598_CONFIG_H_
#define _DCB_82598_CONFIG_H_

/* DCB register definitions */

#define IXGBE_DPMCS_MTSOS_SHIFT 16
#define IXGBE_DPMCS_TDPAC       0x00000001 /* 0 Round Robin, 1 DFP - Deficit Fixed Priority */
#define IXGBE_DPMCS_TRM         0x00000010 /* Transmit Recycle Mode */
#define IXGBE_DPMCS_ARBDIS      0x00000040 /* DCB arbiter disable */
#define IXGBE_DPMCS_TSOEF       0x00080000 /* TSO Expand Factor: 0=x4, 1=x2 */

#define IXGBE_RUPPBMR_MQA       0x80000000 /* Enable UP to queue mapping */

#define IXGBE_RT2CR_MCL_SHIFT   12 /* Offset to Max Credit Limit setting */
#define IXGBE_RT2CR_LSP         0x80000000 /* LSP enable bit */

#define IXGBE_RDRXCTL_MPBEN     0x00000010 /* DMA config for multiple packet buffers enable */
#define IXGBE_RDRXCTL_MCEN      0x00000040 /* DMA config for multiple cores (RSS) enable */

#define IXGBE_TDTQ2TCCR_MCL_SHIFT   12
#define IXGBE_TDTQ2TCCR_BWG_SHIFT   9
#define IXGBE_TDTQ2TCCR_GSP     0x40000000
#define IXGBE_TDTQ2TCCR_LSP     0x80000000

#define IXGBE_TDPT2TCCR_MCL_SHIFT   12
#define IXGBE_TDPT2TCCR_BWG_SHIFT   9
#define IXGBE_TDPT2TCCR_GSP     0x40000000
#define IXGBE_TDPT2TCCR_LSP     0x80000000

#define IXGBE_PDPMCS_TPPAC      0x00000020 /* 0 Round Robin, 1 for DFP - Deficit Fixed Priority */
#define IXGBE_PDPMCS_ARBDIS     0x00000040 /* Arbiter disable */
#define IXGBE_PDPMCS_TRM        0x00000100 /* Transmit Recycle Mode enable */

#define IXGBE_DTXCTL_ENDBUBD    0x00000004 /* Enable DBU buffer division */

#define IXGBE_TXPBSIZE_40KB     0x0000A000 /* 40KB Packet Buffer */
#define IXGBE_RXPBSIZE_48KB     0x0000C000 /* 48KB Packet Buffer */
#define IXGBE_RXPBSIZE_64KB     0x00010000 /* 64KB Packet Buffer */
#define IXGBE_RXPBSIZE_80KB     0x00014000 /* 80KB Packet Buffer */

#define IXGBE_RDRXCTL_RDMTS_1_2 0x00000000

/* DCB hardware-specific driver APIs */

/* DCB PFC functions */
s32 ixgbe_dcb_config_pfc_82598(struct ixgbe_hw *, struct ixgbe_dcb_config *);
s32 ixgbe_dcb_get_pfc_stats_82598(struct ixgbe_hw *, struct ixgbe_hw_stats *,
                                  u8);

/* DCB traffic class stats */
s32 ixgbe_dcb_config_tc_stats_82598(struct ixgbe_hw *);
s32 ixgbe_dcb_get_tc_stats_82598(struct ixgbe_hw *, struct ixgbe_hw_stats *,
                                 u8);

/* DCB config arbiters */
s32 ixgbe_dcb_config_tx_desc_arbiter_82598(struct ixgbe_hw *,
                                           struct ixgbe_dcb_config *);
s32 ixgbe_dcb_config_tx_data_arbiter_82598(struct ixgbe_hw *,
                                           struct ixgbe_dcb_config *);
s32 ixgbe_dcb_config_rx_arbiter_82598(struct ixgbe_hw *,
                                      struct ixgbe_dcb_config *);

/* DCB hw initialization */
s32 ixgbe_dcb_hw_config_82598(struct ixgbe_hw *, struct ixgbe_dcb_config *);

#endif /* _DCB_82598_CONFIG_H */
