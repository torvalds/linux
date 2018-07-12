/* SPDX-License-Identifier: GPL-2.0 */
/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Virtual Function Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#ifndef _I40E_LAN_HMC_H_
#define _I40E_LAN_HMC_H_

/* forward-declare the HW struct for the compiler */
struct i40e_hw;

/* HMC element context information */

/* Rx queue context data
 *
 * The sizes of the variables may be larger than needed due to crossing byte
 * boundaries. If we do not have the width of the variable set to the correct
 * size then we could end up shifting bits off the top of the variable when the
 * variable is at the top of a byte and crosses over into the next byte.
 */
struct i40e_hmc_obj_rxq {
	u16 head;
	u16 cpuid; /* bigger than needed, see above for reason */
	u64 base;
	u16 qlen;
#define I40E_RXQ_CTX_DBUFF_SHIFT 7
	u16 dbuff; /* bigger than needed, see above for reason */
#define I40E_RXQ_CTX_HBUFF_SHIFT 6
	u16 hbuff; /* bigger than needed, see above for reason */
	u8  dtype;
	u8  dsize;
	u8  crcstrip;
	u8  fc_ena;
	u8  l2tsel;
	u8  hsplit_0;
	u8  hsplit_1;
	u8  showiv;
	u32 rxmax; /* bigger than needed, see above for reason */
	u8  tphrdesc_ena;
	u8  tphwdesc_ena;
	u8  tphdata_ena;
	u8  tphhead_ena;
	u16 lrxqthresh; /* bigger than needed, see above for reason */
	u8  prefena;	/* NOTE: normally must be set to 1 at init */
};

/* Tx queue context data
*
* The sizes of the variables may be larger than needed due to crossing byte
* boundaries. If we do not have the width of the variable set to the correct
* size then we could end up shifting bits off the top of the variable when the
* variable is at the top of a byte and crosses over into the next byte.
*/
struct i40e_hmc_obj_txq {
	u16 head;
	u8  new_context;
	u64 base;
	u8  fc_ena;
	u8  timesync_ena;
	u8  fd_ena;
	u8  alt_vlan_ena;
	u16 thead_wb;
	u8  cpuid;
	u8  head_wb_ena;
	u16 qlen;
	u8  tphrdesc_ena;
	u8  tphrpacket_ena;
	u8  tphwdesc_ena;
	u64 head_wb_addr;
	u32 crc;
	u16 rdylist;
	u8  rdylist_act;
};

/* for hsplit_0 field of Rx HMC context */
enum i40e_hmc_obj_rx_hsplit_0 {
	I40E_HMC_OBJ_RX_HSPLIT_0_NO_SPLIT      = 0,
	I40E_HMC_OBJ_RX_HSPLIT_0_SPLIT_L2      = 1,
	I40E_HMC_OBJ_RX_HSPLIT_0_SPLIT_IP      = 2,
	I40E_HMC_OBJ_RX_HSPLIT_0_SPLIT_TCP_UDP = 4,
	I40E_HMC_OBJ_RX_HSPLIT_0_SPLIT_SCTP    = 8,
};

/* fcoe_cntx and fcoe_filt are for debugging purpose only */
struct i40e_hmc_obj_fcoe_cntx {
	u32 rsv[32];
};

struct i40e_hmc_obj_fcoe_filt {
	u32 rsv[8];
};

/* Context sizes for LAN objects */
enum i40e_hmc_lan_object_size {
	I40E_HMC_LAN_OBJ_SZ_8   = 0x3,
	I40E_HMC_LAN_OBJ_SZ_16  = 0x4,
	I40E_HMC_LAN_OBJ_SZ_32  = 0x5,
	I40E_HMC_LAN_OBJ_SZ_64  = 0x6,
	I40E_HMC_LAN_OBJ_SZ_128 = 0x7,
	I40E_HMC_LAN_OBJ_SZ_256 = 0x8,
	I40E_HMC_LAN_OBJ_SZ_512 = 0x9,
};

#define I40E_HMC_L2OBJ_BASE_ALIGNMENT 512
#define I40E_HMC_OBJ_SIZE_TXQ         128
#define I40E_HMC_OBJ_SIZE_RXQ         32
#define I40E_HMC_OBJ_SIZE_FCOE_CNTX   128
#define I40E_HMC_OBJ_SIZE_FCOE_FILT   64

enum i40e_hmc_lan_rsrc_type {
	I40E_HMC_LAN_FULL  = 0,
	I40E_HMC_LAN_TX    = 1,
	I40E_HMC_LAN_RX    = 2,
	I40E_HMC_FCOE_CTX  = 3,
	I40E_HMC_FCOE_FILT = 4,
	I40E_HMC_LAN_MAX   = 5
};

enum i40e_hmc_model {
	I40E_HMC_MODEL_DIRECT_PREFERRED = 0,
	I40E_HMC_MODEL_DIRECT_ONLY      = 1,
	I40E_HMC_MODEL_PAGED_ONLY       = 2,
	I40E_HMC_MODEL_UNKNOWN,
};

struct i40e_hmc_lan_create_obj_info {
	struct i40e_hmc_info *hmc_info;
	u32 rsrc_type;
	u32 start_idx;
	u32 count;
	enum i40e_sd_entry_type entry_type;
	u64 direct_mode_sz;
};

struct i40e_hmc_lan_delete_obj_info {
	struct i40e_hmc_info *hmc_info;
	u32 rsrc_type;
	u32 start_idx;
	u32 count;
};

i40e_status i40e_init_lan_hmc(struct i40e_hw *hw, u32 txq_num,
					u32 rxq_num, u32 fcoe_cntx_num,
					u32 fcoe_filt_num);
i40e_status i40e_configure_lan_hmc(struct i40e_hw *hw,
					     enum i40e_hmc_model model);
i40e_status i40e_shutdown_lan_hmc(struct i40e_hw *hw);

i40e_status i40e_clear_lan_tx_queue_context(struct i40e_hw *hw,
						      u16 queue);
i40e_status i40e_set_lan_tx_queue_context(struct i40e_hw *hw,
						    u16 queue,
						    struct i40e_hmc_obj_txq *s);
i40e_status i40e_clear_lan_rx_queue_context(struct i40e_hw *hw,
						      u16 queue);
i40e_status i40e_set_lan_rx_queue_context(struct i40e_hw *hw,
						    u16 queue,
						    struct i40e_hmc_obj_rxq *s);

#endif /* _I40E_LAN_HMC_H_ */
