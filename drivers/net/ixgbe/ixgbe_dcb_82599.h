/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2009 Intel Corporation.

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
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _DCB_82599_CONFIG_H_
#define _DCB_82599_CONFIG_H_

/* DCB register definitions */
#define IXGBE_RTTDCS_TDPAC      0x00000001 /* 0 Round Robin,
                                            * 1 WSP - Weighted Strict Priority
                                            */
#define IXGBE_RTTDCS_VMPAC      0x00000002 /* 0 Round Robin,
                                            * 1 WRR - Weighted Round Robin
                                            */
#define IXGBE_RTTDCS_TDRM       0x00000010 /* Transmit Recycle Mode */
#define IXGBE_RTTDCS_ARBDIS     0x00000040 /* DCB arbiter disable */
#define IXGBE_RTTDCS_BDPM       0x00400000 /* Bypass Data Pipe - must clear! */
#define IXGBE_RTTDCS_BPBFSM     0x00800000 /* Bypass PB Free Space - must
                                             * clear!
                                             */
#define IXGBE_RTTDCS_SPEED_CHG  0x80000000 /* Link speed change */

/* Receive UP2TC mapping */
#define IXGBE_RTRUP2TC_UP_SHIFT 3
/* Transmit UP2TC mapping */
#define IXGBE_RTTUP2TC_UP_SHIFT 3

#define IXGBE_RTRPT4C_MCL_SHIFT 12 /* Offset to Max Credit Limit setting */
#define IXGBE_RTRPT4C_BWG_SHIFT 9  /* Offset to BWG index */
#define IXGBE_RTRPT4C_GSP       0x40000000 /* GSP enable bit */
#define IXGBE_RTRPT4C_LSP       0x80000000 /* LSP enable bit */

#define IXGBE_RDRXCTL_MPBEN     0x00000010 /* DMA config for multiple packet
                                            * buffers enable
                                            */
#define IXGBE_RDRXCTL_MCEN      0x00000040 /* DMA config for multiple cores
                                            * (RSS) enable
                                            */

/* RTRPCS Bit Masks */
#define IXGBE_RTRPCS_RRM        0x00000002 /* Receive Recycle Mode enable */
/* Receive Arbitration Control: 0 Round Robin, 1 DFP */
#define IXGBE_RTRPCS_RAC        0x00000004
#define IXGBE_RTRPCS_ARBDIS     0x00000040 /* Arbitration disable bit */

/* RTTDT2C Bit Masks */
#define IXGBE_RTTDT2C_MCL_SHIFT 12
#define IXGBE_RTTDT2C_BWG_SHIFT 9
#define IXGBE_RTTDT2C_GSP       0x40000000
#define IXGBE_RTTDT2C_LSP       0x80000000

#define IXGBE_RTTPT2C_MCL_SHIFT 12
#define IXGBE_RTTPT2C_BWG_SHIFT 9
#define IXGBE_RTTPT2C_GSP       0x40000000
#define IXGBE_RTTPT2C_LSP       0x80000000

/* RTTPCS Bit Masks */
#define IXGBE_RTTPCS_TPPAC      0x00000020 /* 0 Round Robin,
                                            * 1 SP - Strict Priority
                                            */
#define IXGBE_RTTPCS_ARBDIS     0x00000040 /* Arbiter disable */
#define IXGBE_RTTPCS_TPRM       0x00000100 /* Transmit Recycle Mode enable */
#define IXGBE_RTTPCS_ARBD_SHIFT 22
#define IXGBE_RTTPCS_ARBD_DCB   0x4        /* Arbitration delay in DCB mode */

#define IXGBE_TXPBSIZE_20KB     0x00005000 /* 20KB Packet Buffer */
#define IXGBE_TXPBSIZE_40KB     0x0000A000 /* 40KB Packet Buffer */
#define IXGBE_RXPBSIZE_48KB     0x0000C000 /* 48KB Packet Buffer */
#define IXGBE_RXPBSIZE_64KB     0x00010000 /* 64KB Packet Buffer */
#define IXGBE_RXPBSIZE_80KB     0x00014000 /* 80KB Packet Buffer */
#define IXGBE_RXPBSIZE_128KB    0x00020000 /* 128KB Packet Buffer */

#define IXGBE_TXPBTHRESH_DCB    0xA        /* THRESH value for DCB mode */


/* DCB hardware-specific driver APIs */

/* DCB PFC functions */
s32 ixgbe_dcb_config_pfc_82599(struct ixgbe_hw *hw,
                               struct ixgbe_dcb_config *dcb_config);
s32 ixgbe_dcb_get_pfc_stats_82599(struct ixgbe_hw *hw,
                                  struct ixgbe_hw_stats *stats,
                                  u8 tc_count);

/* DCB traffic class stats */
s32 ixgbe_dcb_config_tc_stats_82599(struct ixgbe_hw *hw);
s32 ixgbe_dcb_get_tc_stats_82599(struct ixgbe_hw *hw,
                                 struct ixgbe_hw_stats *stats,
                                 u8 tc_count);

/* DCB config arbiters */
s32 ixgbe_dcb_config_tx_desc_arbiter_82599(struct ixgbe_hw *hw,
                                           struct ixgbe_dcb_config *dcb_config);
s32 ixgbe_dcb_config_tx_data_arbiter_82599(struct ixgbe_hw *hw,
                                           struct ixgbe_dcb_config *dcb_config);
s32 ixgbe_dcb_config_rx_arbiter_82599(struct ixgbe_hw *hw,
                                      struct ixgbe_dcb_config *dcb_config);


/* DCB hw initialization */
s32 ixgbe_dcb_hw_config_82599(struct ixgbe_hw *hw,
                              struct ixgbe_dcb_config *config);

#endif /* _DCB_82599_CONFIG_H */
