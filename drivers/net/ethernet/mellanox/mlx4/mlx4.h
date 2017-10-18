/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MLX4_H
#define MLX4_H

#include <linux/mutex.h>
#include <linux/radix-tree.h>
#include <linux/rbtree.h>
#include <linux/timer.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <net/devlink.h>
#include <linux/rwsem.h>

#include <linux/mlx4/device.h>
#include <linux/mlx4/driver.h>
#include <linux/mlx4/doorbell.h>
#include <linux/mlx4/cmd.h>
#include "fw_qos.h"

#define DRV_NAME	"mlx4_core"
#define PFX		DRV_NAME ": "
#define DRV_VERSION	"4.0-0"

#define MLX4_FS_UDP_UC_EN		(1 << 1)
#define MLX4_FS_TCP_UC_EN		(1 << 2)
#define MLX4_FS_NUM_OF_L2_ADDR		8
#define MLX4_FS_MGM_LOG_ENTRY_SIZE	7
#define MLX4_FS_NUM_MCG			(1 << 17)

#define INIT_HCA_TPT_MW_ENABLE          (1 << 7)

#define MLX4_QUERY_IF_STAT_RESET	BIT(31)

enum {
	MLX4_HCR_BASE		= 0x80680,
	MLX4_HCR_SIZE		= 0x0001c,
	MLX4_CLR_INT_SIZE	= 0x00008,
	MLX4_SLAVE_COMM_BASE	= 0x0,
	MLX4_COMM_PAGESIZE	= 0x1000,
	MLX4_CLOCK_SIZE		= 0x00008,
	MLX4_COMM_CHAN_CAPS	= 0x8,
	MLX4_COMM_CHAN_FLAGS	= 0xc
};

enum {
	MLX4_DEFAULT_MGM_LOG_ENTRY_SIZE = 10,
	MLX4_MIN_MGM_LOG_ENTRY_SIZE = 7,
	MLX4_MAX_MGM_LOG_ENTRY_SIZE = 12,
	MLX4_MAX_QP_PER_MGM = 4 * ((1 << MLX4_MAX_MGM_LOG_ENTRY_SIZE) / 16 - 2),
	MLX4_MTT_ENTRY_PER_SEG	= 8,
};

enum {
	MLX4_NUM_PDS		= 1 << 15
};

enum {
	MLX4_CMPT_TYPE_QP	= 0,
	MLX4_CMPT_TYPE_SRQ	= 1,
	MLX4_CMPT_TYPE_CQ	= 2,
	MLX4_CMPT_TYPE_EQ	= 3,
	MLX4_CMPT_NUM_TYPE
};

enum {
	MLX4_CMPT_SHIFT		= 24,
	MLX4_NUM_CMPTS		= MLX4_CMPT_NUM_TYPE << MLX4_CMPT_SHIFT
};

enum mlx4_mpt_state {
	MLX4_MPT_DISABLED = 0,
	MLX4_MPT_EN_HW,
	MLX4_MPT_EN_SW
};

#define MLX4_COMM_TIME		10000
#define MLX4_COMM_OFFLINE_TIME_OUT 30000
#define MLX4_COMM_CMD_NA_OP    0x0


enum {
	MLX4_COMM_CMD_RESET,
	MLX4_COMM_CMD_VHCR0,
	MLX4_COMM_CMD_VHCR1,
	MLX4_COMM_CMD_VHCR2,
	MLX4_COMM_CMD_VHCR_EN,
	MLX4_COMM_CMD_VHCR_POST,
	MLX4_COMM_CMD_FLR = 254
};

enum {
	MLX4_VF_SMI_DISABLED,
	MLX4_VF_SMI_ENABLED
};

/*The flag indicates that the slave should delay the RESET cmd*/
#define MLX4_DELAY_RESET_SLAVE 0xbbbbbbb
/*indicates how many retries will be done if we are in the middle of FLR*/
#define NUM_OF_RESET_RETRIES	10
#define SLEEP_TIME_IN_RESET	(2 * 1000)
enum mlx4_resource {
	RES_QP,
	RES_CQ,
	RES_SRQ,
	RES_XRCD,
	RES_MPT,
	RES_MTT,
	RES_MAC,
	RES_VLAN,
	RES_NPORT_ID,
	RES_COUNTER,
	RES_FS_RULE,
	RES_EQ,
	MLX4_NUM_OF_RESOURCE_TYPE
};

enum mlx4_alloc_mode {
	RES_OP_RESERVE,
	RES_OP_RESERVE_AND_MAP,
	RES_OP_MAP_ICM,
};

enum mlx4_res_tracker_free_type {
	RES_TR_FREE_ALL,
	RES_TR_FREE_SLAVES_ONLY,
	RES_TR_FREE_STRUCTS_ONLY,
};

/*
 *Virtual HCR structures.
 * mlx4_vhcr is the sw representation, in machine endianness
 *
 * mlx4_vhcr_cmd is the formalized structure, the one that is passed
 * to FW to go through communication channel.
 * It is big endian, and has the same structure as the physical HCR
 * used by command interface
 */
struct mlx4_vhcr {
	u64	in_param;
	u64	out_param;
	u32	in_modifier;
	u32	errno;
	u16	op;
	u16	token;
	u8	op_modifier;
	u8	e_bit;
};

struct mlx4_vhcr_cmd {
	__be64 in_param;
	__be32 in_modifier;
	u32 reserved1;
	__be64 out_param;
	__be16 token;
	u16 reserved;
	u8 status;
	u8 flags;
	__be16 opcode;
};

struct mlx4_cmd_info {
	u16 opcode;
	bool has_inbox;
	bool has_outbox;
	bool out_is_imm;
	bool encode_slave_id;
	int (*verify)(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
		      struct mlx4_cmd_mailbox *inbox);
	int (*wrapper)(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
		       struct mlx4_cmd_mailbox *inbox,
		       struct mlx4_cmd_mailbox *outbox,
		       struct mlx4_cmd_info *cmd);
};

#ifdef CONFIG_MLX4_DEBUG
extern int mlx4_debug_level;
#else /* CONFIG_MLX4_DEBUG */
#define mlx4_debug_level	(0)
#endif /* CONFIG_MLX4_DEBUG */

#define mlx4_dbg(mdev, format, ...)					\
do {									\
	if (mlx4_debug_level)						\
		dev_printk(KERN_DEBUG,					\
			   &(mdev)->persist->pdev->dev, format,		\
			   ##__VA_ARGS__);				\
} while (0)

#define mlx4_err(mdev, format, ...)					\
	dev_err(&(mdev)->persist->pdev->dev, format, ##__VA_ARGS__)
#define mlx4_info(mdev, format, ...)					\
	dev_info(&(mdev)->persist->pdev->dev, format, ##__VA_ARGS__)
#define mlx4_warn(mdev, format, ...)					\
	dev_warn(&(mdev)->persist->pdev->dev, format, ##__VA_ARGS__)

extern int log_mtts_per_seg;
extern int mlx4_internal_err_reset;

#define MLX4_MAX_NUM_SLAVES	(min(MLX4_MAX_NUM_PF + MLX4_MAX_NUM_VF, \
				     MLX4_MFUNC_MAX))
#define ALL_SLAVES 0xff

struct mlx4_bitmap {
	u32			last;
	u32			top;
	u32			max;
	u32                     reserved_top;
	u32			mask;
	u32			avail;
	u32			effective_len;
	spinlock_t		lock;
	unsigned long	       *table;
};

struct mlx4_buddy {
	unsigned long	      **bits;
	unsigned int	       *num_free;
	u32			max_order;
	spinlock_t		lock;
};

struct mlx4_icm;

struct mlx4_icm_table {
	u64			virt;
	int			num_icm;
	u32			num_obj;
	int			obj_size;
	int			lowmem;
	int			coherent;
	struct mutex		mutex;
	struct mlx4_icm	      **icm;
};

#define MLX4_MPT_FLAG_SW_OWNS	    (0xfUL << 28)
#define MLX4_MPT_FLAG_FREE	    (0x3UL << 28)
#define MLX4_MPT_FLAG_MIO	    (1 << 17)
#define MLX4_MPT_FLAG_BIND_ENABLE   (1 << 15)
#define MLX4_MPT_FLAG_PHYSICAL	    (1 <<  9)
#define MLX4_MPT_FLAG_REGION	    (1 <<  8)

#define MLX4_MPT_PD_MASK	    (0x1FFFFUL)
#define MLX4_MPT_PD_VF_MASK	    (0xFE0000UL)
#define MLX4_MPT_PD_FLAG_FAST_REG   (1 << 27)
#define MLX4_MPT_PD_FLAG_RAE	    (1 << 28)
#define MLX4_MPT_PD_FLAG_EN_INV	    (3 << 24)

#define MLX4_MPT_QP_FLAG_BOUND_QP   (1 << 7)

#define MLX4_MPT_STATUS_SW		0xF0
#define MLX4_MPT_STATUS_HW		0x00

#define MLX4_CQE_SIZE_MASK_STRIDE	0x3
#define MLX4_EQE_SIZE_MASK_STRIDE	0x30

#define MLX4_EQ_ASYNC			0
#define MLX4_EQ_TO_CQ_VECTOR(vector)	((vector) - \
					 !!((int)(vector) >= MLX4_EQ_ASYNC))
#define MLX4_CQ_TO_EQ_VECTOR(vector)	((vector) + \
					 !!((int)(vector) >= MLX4_EQ_ASYNC))

/*
 * Must be packed because mtt_seg is 64 bits but only aligned to 32 bits.
 */
struct mlx4_mpt_entry {
	__be32 flags;
	__be32 qpn;
	__be32 key;
	__be32 pd_flags;
	__be64 start;
	__be64 length;
	__be32 lkey;
	__be32 win_cnt;
	u8	reserved1[3];
	u8	mtt_rep;
	__be64 mtt_addr;
	__be32 mtt_sz;
	__be32 entity_size;
	__be32 first_byte_offset;
} __packed;

/*
 * Must be packed because start is 64 bits but only aligned to 32 bits.
 */
struct mlx4_eq_context {
	__be32			flags;
	u16			reserved1[3];
	__be16			page_offset;
	u8			log_eq_size;
	u8			reserved2[4];
	u8			eq_period;
	u8			reserved3;
	u8			eq_max_count;
	u8			reserved4[3];
	u8			intr;
	u8			log_page_size;
	u8			reserved5[2];
	u8			mtt_base_addr_h;
	__be32			mtt_base_addr_l;
	u32			reserved6[2];
	__be32			consumer_index;
	__be32			producer_index;
	u32			reserved7[4];
};

struct mlx4_cq_context {
	__be32			flags;
	u16			reserved1[3];
	__be16			page_offset;
	__be32			logsize_usrpage;
	__be16			cq_period;
	__be16			cq_max_count;
	u8			reserved2[3];
	u8			comp_eqn;
	u8			log_page_size;
	u8			reserved3[2];
	u8			mtt_base_addr_h;
	__be32			mtt_base_addr_l;
	__be32			last_notified_index;
	__be32			solicit_producer_index;
	__be32			consumer_index;
	__be32			producer_index;
	u32			reserved4[2];
	__be64			db_rec_addr;
};

struct mlx4_srq_context {
	__be32			state_logsize_srqn;
	u8			logstride;
	u8			reserved1;
	__be16			xrcd;
	__be32			pg_offset_cqn;
	u32			reserved2;
	u8			log_page_size;
	u8			reserved3[2];
	u8			mtt_base_addr_h;
	__be32			mtt_base_addr_l;
	__be32			pd;
	__be16			limit_watermark;
	__be16			wqe_cnt;
	u16			reserved4;
	__be16			wqe_counter;
	u32			reserved5;
	__be64			db_rec_addr;
};

struct mlx4_eq_tasklet {
	struct list_head list;
	struct list_head process_list;
	struct tasklet_struct task;
	/* lock on completion tasklet list */
	spinlock_t lock;
};

struct mlx4_eq {
	struct mlx4_dev	       *dev;
	void __iomem	       *doorbell;
	int			eqn;
	u32			cons_index;
	u16			irq;
	u16			have_irq;
	int			nent;
	struct mlx4_buf_list   *page_list;
	struct mlx4_mtt		mtt;
	struct mlx4_eq_tasklet	tasklet_ctx;
	struct mlx4_active_ports actv_ports;
	u32			ref_count;
	cpumask_var_t		affinity_mask;
};

struct mlx4_slave_eqe {
	u8 type;
	u8 port;
	u32 param;
};

struct mlx4_slave_event_eq_info {
	int eqn;
	u16 token;
};

struct mlx4_profile {
	int			num_qp;
	int			rdmarc_per_qp;
	int			num_srq;
	int			num_cq;
	int			num_mcg;
	int			num_mpt;
	unsigned		num_mtt;
};

struct mlx4_fw {
	u64			clr_int_base;
	u64			catas_offset;
	u64			comm_base;
	u64			clock_offset;
	struct mlx4_icm	       *fw_icm;
	struct mlx4_icm	       *aux_icm;
	u32			catas_size;
	u16			fw_pages;
	u8			clr_int_bar;
	u8			catas_bar;
	u8			comm_bar;
	u8			clock_bar;
};

struct mlx4_comm {
	u32			slave_write;
	u32			slave_read;
};

enum {
	MLX4_MCAST_CONFIG       = 0,
	MLX4_MCAST_DISABLE      = 1,
	MLX4_MCAST_ENABLE       = 2,
};

#define VLAN_FLTR_SIZE	128

struct mlx4_vlan_fltr {
	__be32 entry[VLAN_FLTR_SIZE];
};

struct mlx4_mcast_entry {
	struct list_head list;
	u64 addr;
};

struct mlx4_promisc_qp {
	struct list_head list;
	u32 qpn;
};

struct mlx4_steer_index {
	struct list_head list;
	unsigned int index;
	struct list_head duplicates;
};

#define MLX4_EVENT_TYPES_NUM 64

struct mlx4_slave_state {
	u8 comm_toggle;
	u8 last_cmd;
	u8 init_port_mask;
	bool active;
	bool old_vlan_api;
	bool vst_qinq_supported;
	u8 function;
	dma_addr_t vhcr_dma;
	u16 user_mtu[MLX4_MAX_PORTS + 1];
	u16 mtu[MLX4_MAX_PORTS + 1];
	__be32 ib_cap_mask[MLX4_MAX_PORTS + 1];
	struct mlx4_slave_eqe eq[MLX4_MFUNC_MAX_EQES];
	struct list_head mcast_filters[MLX4_MAX_PORTS + 1];
	struct mlx4_vlan_fltr *vlan_filter[MLX4_MAX_PORTS + 1];
	/* event type to eq number lookup */
	struct mlx4_slave_event_eq_info event_eq[MLX4_EVENT_TYPES_NUM];
	u16 eq_pi;
	u16 eq_ci;
	spinlock_t lock;
	/*initialized via the kzalloc*/
	u8 is_slave_going_down;
	u32 cookie;
	enum slave_port_state port_state[MLX4_MAX_PORTS + 1];
};

#define MLX4_VGT 4095
#define NO_INDX  (-1)

struct mlx4_vport_state {
	u64 mac;
	u16 default_vlan;
	u8  default_qos;
	__be16 vlan_proto;
	u32 tx_rate;
	bool spoofchk;
	u32 link_state;
	u8 qos_vport;
	__be64 guid;
};

struct mlx4_vf_admin_state {
	struct mlx4_vport_state vport[MLX4_MAX_PORTS + 1];
	u8 enable_smi[MLX4_MAX_PORTS + 1];
};

struct mlx4_vport_oper_state {
	struct mlx4_vport_state state;
	int mac_idx;
	int vlan_idx;
};

struct mlx4_vf_oper_state {
	struct mlx4_vport_oper_state vport[MLX4_MAX_PORTS + 1];
	u8 smi_enabled[MLX4_MAX_PORTS + 1];
};

struct slave_list {
	struct mutex mutex;
	struct list_head res_list[MLX4_NUM_OF_RESOURCE_TYPE];
};

struct resource_allocator {
	spinlock_t alloc_lock; /* protect quotas */
	union {
		int res_reserved;
		int res_port_rsvd[MLX4_MAX_PORTS];
	};
	union {
		int res_free;
		int res_port_free[MLX4_MAX_PORTS];
	};
	int *quota;
	int *allocated;
	int *guaranteed;
};

struct mlx4_resource_tracker {
	spinlock_t lock;
	/* tree for each resources */
	struct rb_root res_tree[MLX4_NUM_OF_RESOURCE_TYPE];
	/* num_of_slave's lists, one per slave */
	struct slave_list *slave_list;
	struct resource_allocator res_alloc[MLX4_NUM_OF_RESOURCE_TYPE];
};

#define SLAVE_EVENT_EQ_SIZE	128
struct mlx4_slave_event_eq {
	u32 eqn;
	u32 cons;
	u32 prod;
	spinlock_t event_lock;
	struct mlx4_eqe event_eqe[SLAVE_EVENT_EQ_SIZE];
};

struct mlx4_qos_manager {
	int num_of_qos_vfs;
	DECLARE_BITMAP(priority_bm, MLX4_NUM_UP);
};

struct mlx4_master_qp0_state {
	int proxy_qp0_active;
	int qp0_active;
	int port_active;
};

struct mlx4_mfunc_master_ctx {
	struct mlx4_slave_state *slave_state;
	struct mlx4_vf_admin_state *vf_admin;
	struct mlx4_vf_oper_state *vf_oper;
	struct mlx4_master_qp0_state qp0_state[MLX4_MAX_PORTS + 1];
	int			init_port_ref[MLX4_MAX_PORTS + 1];
	u16			max_mtu[MLX4_MAX_PORTS + 1];
	u16			max_user_mtu[MLX4_MAX_PORTS + 1];
	u8			pptx;
	u8			pprx;
	int			disable_mcast_ref[MLX4_MAX_PORTS + 1];
	struct mlx4_resource_tracker res_tracker;
	struct workqueue_struct *comm_wq;
	struct work_struct	comm_work;
	struct work_struct	slave_event_work;
	struct work_struct	slave_flr_event_work;
	spinlock_t		slave_state_lock;
	__be32			comm_arm_bit_vector[4];
	struct mlx4_eqe		cmd_eqe;
	struct mlx4_slave_event_eq slave_eq;
	struct mutex		gen_eqe_mutex[MLX4_MFUNC_MAX];
	struct mlx4_qos_manager qos_ctl[MLX4_MAX_PORTS + 1];
};

struct mlx4_mfunc {
	struct mlx4_comm __iomem       *comm;
	struct mlx4_vhcr_cmd	       *vhcr;
	dma_addr_t			vhcr_dma;

	struct mlx4_mfunc_master_ctx	master;
};

#define MGM_QPN_MASK       0x00FFFFFF
#define MGM_BLCK_LB_BIT    30

struct mlx4_mgm {
	__be32			next_gid_index;
	__be32			members_count;
	u32			reserved[2];
	u8			gid[16];
	__be32			qp[MLX4_MAX_QP_PER_MGM];
};

struct mlx4_cmd {
	struct pci_pool	       *pool;
	void __iomem	       *hcr;
	struct mutex		slave_cmd_mutex;
	struct semaphore	poll_sem;
	struct semaphore	event_sem;
	struct rw_semaphore	switch_sem;
	int			max_cmds;
	spinlock_t		context_lock;
	int			free_head;
	struct mlx4_cmd_context *context;
	u16			token_mask;
	u8			use_events;
	u8			toggle;
	u8			comm_toggle;
	u8			initialized;
};

enum {
	MLX4_VF_IMMED_VLAN_FLAG_VLAN = 1 << 0,
	MLX4_VF_IMMED_VLAN_FLAG_QOS = 1 << 1,
	MLX4_VF_IMMED_VLAN_FLAG_LINK_DISABLE = 1 << 2,
};
struct mlx4_vf_immed_vlan_work {
	struct work_struct	work;
	struct mlx4_priv	*priv;
	int			flags;
	int			slave;
	int			vlan_ix;
	int			orig_vlan_ix;
	u8			port;
	u8			qos;
	u8                      qos_vport;
	u16			vlan_id;
	u16			orig_vlan_id;
	__be16			vlan_proto;
};


struct mlx4_uar_table {
	struct mlx4_bitmap	bitmap;
};

struct mlx4_mr_table {
	struct mlx4_bitmap	mpt_bitmap;
	struct mlx4_buddy	mtt_buddy;
	u64			mtt_base;
	u64			mpt_base;
	struct mlx4_icm_table	mtt_table;
	struct mlx4_icm_table	dmpt_table;
};

struct mlx4_cq_table {
	struct mlx4_bitmap	bitmap;
	spinlock_t		lock;
	struct radix_tree_root	tree;
	struct mlx4_icm_table	table;
	struct mlx4_icm_table	cmpt_table;
};

struct mlx4_eq_table {
	struct mlx4_bitmap	bitmap;
	char		       *irq_names;
	void __iomem	       *clr_int;
	void __iomem	      **uar_map;
	u32			clr_mask;
	struct mlx4_eq	       *eq;
	struct mlx4_icm_table	table;
	struct mlx4_icm_table	cmpt_table;
	int			have_irq;
	u8			inta_pin;
};

struct mlx4_srq_table {
	struct mlx4_bitmap	bitmap;
	spinlock_t		lock;
	struct radix_tree_root	tree;
	struct mlx4_icm_table	table;
	struct mlx4_icm_table	cmpt_table;
};

enum mlx4_qp_table_zones {
	MLX4_QP_TABLE_ZONE_GENERAL,
	MLX4_QP_TABLE_ZONE_RSS,
	MLX4_QP_TABLE_ZONE_RAW_ETH,
	MLX4_QP_TABLE_ZONE_NUM
};

struct mlx4_qp_table {
	struct mlx4_bitmap	*bitmap_gen;
	struct mlx4_zone_allocator *zones;
	u32			zones_uids[MLX4_QP_TABLE_ZONE_NUM];
	u32			rdmarc_base;
	int			rdmarc_shift;
	spinlock_t		lock;
	struct mlx4_icm_table	qp_table;
	struct mlx4_icm_table	auxc_table;
	struct mlx4_icm_table	altc_table;
	struct mlx4_icm_table	rdmarc_table;
	struct mlx4_icm_table	cmpt_table;
};

struct mlx4_mcg_table {
	struct mutex		mutex;
	struct mlx4_bitmap	bitmap;
	struct mlx4_icm_table	table;
};

struct mlx4_catas_err {
	u32 __iomem	       *map;
	struct timer_list	timer;
	struct list_head	list;
};

#define MLX4_MAX_MAC_NUM	128
#define MLX4_MAC_TABLE_SIZE	(MLX4_MAX_MAC_NUM << 3)

struct mlx4_mac_table {
	__be64			entries[MLX4_MAX_MAC_NUM];
	int			refs[MLX4_MAX_MAC_NUM];
	bool			is_dup[MLX4_MAX_MAC_NUM];
	struct mutex		mutex;
	int			total;
	int			max;
};

#define MLX4_ROCE_GID_ENTRY_SIZE	16

struct mlx4_roce_gid_entry {
	u8 raw[MLX4_ROCE_GID_ENTRY_SIZE];
};

struct mlx4_roce_gid_table {
	struct mlx4_roce_gid_entry	roce_gids[MLX4_ROCE_MAX_GIDS];
	struct mutex			mutex;
};

#define MLX4_MAX_VLAN_NUM	128
#define MLX4_VLAN_TABLE_SIZE	(MLX4_MAX_VLAN_NUM << 2)

struct mlx4_vlan_table {
	__be32			entries[MLX4_MAX_VLAN_NUM];
	int			refs[MLX4_MAX_VLAN_NUM];
	int			is_dup[MLX4_MAX_VLAN_NUM];
	struct mutex		mutex;
	int			total;
	int			max;
};

#define SET_PORT_GEN_ALL_VALID	(MLX4_FLAG_V_MTU_MASK	| \
				 MLX4_FLAG_V_PPRX_MASK	| \
				 MLX4_FLAG_V_PPTX_MASK)
#define SET_PORT_PROMISC_SHIFT		31
#define SET_PORT_MC_PROMISC_SHIFT	30

enum {
	MCAST_DIRECT_ONLY	= 0,
	MCAST_DIRECT		= 1,
	MCAST_DEFAULT		= 2
};


struct mlx4_set_port_general_context {
	u16 reserved1;
	u8 flags2;
	u8 flags;
	union {
		u8 ignore_fcs;
		u8 roce_mode;
	};
	u8 reserved2;
	__be16 mtu;
	u8 pptx;
	u8 pfctx;
	u16 reserved3;
	u8 pprx;
	u8 pfcrx;
	u16 reserved4;
	u32 reserved5;
	u8 phv_en;
	u8 reserved6[5];
	__be16 user_mtu;
};

struct mlx4_set_port_rqp_calc_context {
	__be32 base_qpn;
	u8 rererved;
	u8 n_mac;
	u8 n_vlan;
	u8 n_prio;
	u8 reserved2[3];
	u8 mac_miss;
	u8 intra_no_vlan;
	u8 no_vlan;
	u8 intra_vlan_miss;
	u8 vlan_miss;
	u8 reserved3[3];
	u8 no_vlan_prio;
	__be32 promisc;
	__be32 mcast;
};

struct mlx4_port_info {
	struct mlx4_dev	       *dev;
	int			port;
	char			dev_name[16];
	struct device_attribute port_attr;
	enum mlx4_port_type	tmp_type;
	char			dev_mtu_name[16];
	struct device_attribute port_mtu_attr;
	struct mlx4_mac_table	mac_table;
	struct mlx4_vlan_table	vlan_table;
	struct mlx4_roce_gid_table gid_table;
	int			base_qpn;
	struct cpu_rmap		*rmap;
	struct devlink_port	devlink_port;
};

struct mlx4_sense {
	struct mlx4_dev		*dev;
	u8			do_sense_port[MLX4_MAX_PORTS + 1];
	u8			sense_allowed[MLX4_MAX_PORTS + 1];
	struct delayed_work	sense_poll;
};

struct mlx4_msix_ctl {
	DECLARE_BITMAP(pool_bm, MAX_MSIX);
	struct mutex	pool_lock;
};

struct mlx4_steer {
	struct list_head promisc_qps[MLX4_NUM_STEERS];
	struct list_head steer_entries[MLX4_NUM_STEERS];
};

enum {
	MLX4_PCI_DEV_IS_VF		= 1 << 0,
	MLX4_PCI_DEV_FORCE_SENSE_PORT	= 1 << 1,
};

enum {
	MLX4_NO_RR	= 0,
	MLX4_USE_RR	= 1,
};

struct mlx4_priv {
	struct mlx4_dev		dev;

	struct list_head	dev_list;
	struct list_head	ctx_list;
	spinlock_t		ctx_lock;

	int			pci_dev_data;
	int                     removed;

	struct list_head        pgdir_list;
	struct mutex            pgdir_mutex;

	struct mlx4_fw		fw;
	struct mlx4_cmd		cmd;
	struct mlx4_mfunc	mfunc;

	struct mlx4_bitmap	pd_bitmap;
	struct mlx4_bitmap	xrcd_bitmap;
	struct mlx4_uar_table	uar_table;
	struct mlx4_mr_table	mr_table;
	struct mlx4_cq_table	cq_table;
	struct mlx4_eq_table	eq_table;
	struct mlx4_srq_table	srq_table;
	struct mlx4_qp_table	qp_table;
	struct mlx4_mcg_table	mcg_table;
	struct mlx4_bitmap	counters_bitmap;
	int			def_counter[MLX4_MAX_PORTS];

	struct mlx4_catas_err	catas_err;

	void __iomem	       *clr_base;

	struct mlx4_uar		driver_uar;
	void __iomem	       *kar;
	struct mlx4_port_info	port[MLX4_MAX_PORTS + 1];
	struct mlx4_sense       sense;
	struct mutex		port_mutex;
	struct mlx4_msix_ctl	msix_ctl;
	struct mlx4_steer	*steer;
	struct list_head	bf_list;
	struct mutex		bf_mutex;
	struct io_mapping	*bf_mapping;
	void __iomem            *clock_mapping;
	int			reserved_mtts;
	int			fs_hash_mode;
	u8 virt2phys_pkey[MLX4_MFUNC_MAX][MLX4_MAX_PORTS][MLX4_MAX_PORT_PKEYS];
	struct mlx4_port_map	v2p; /* cached port mapping configuration */
	struct mutex		bond_mutex; /* for bond mode */
	__be64			slave_node_guids[MLX4_MFUNC_MAX];

	atomic_t		opreq_count;
	struct work_struct	opreq_task;
};

static inline struct mlx4_priv *mlx4_priv(struct mlx4_dev *dev)
{
	return container_of(dev, struct mlx4_priv, dev);
}

#define MLX4_SENSE_RANGE	(HZ * 3)

extern struct workqueue_struct *mlx4_wq;

u32 mlx4_bitmap_alloc(struct mlx4_bitmap *bitmap);
void mlx4_bitmap_free(struct mlx4_bitmap *bitmap, u32 obj, int use_rr);
u32 mlx4_bitmap_alloc_range(struct mlx4_bitmap *bitmap, int cnt,
			    int align, u32 skip_mask);
void mlx4_bitmap_free_range(struct mlx4_bitmap *bitmap, u32 obj, int cnt,
			    int use_rr);
u32 mlx4_bitmap_avail(struct mlx4_bitmap *bitmap);
int mlx4_bitmap_init(struct mlx4_bitmap *bitmap, u32 num, u32 mask,
		     u32 reserved_bot, u32 resetrved_top);
void mlx4_bitmap_cleanup(struct mlx4_bitmap *bitmap);

int mlx4_reset(struct mlx4_dev *dev);

int mlx4_alloc_eq_table(struct mlx4_dev *dev);
void mlx4_free_eq_table(struct mlx4_dev *dev);

int mlx4_init_pd_table(struct mlx4_dev *dev);
int mlx4_init_xrcd_table(struct mlx4_dev *dev);
int mlx4_init_uar_table(struct mlx4_dev *dev);
int mlx4_init_mr_table(struct mlx4_dev *dev);
int mlx4_init_eq_table(struct mlx4_dev *dev);
int mlx4_init_cq_table(struct mlx4_dev *dev);
int mlx4_init_qp_table(struct mlx4_dev *dev);
int mlx4_init_srq_table(struct mlx4_dev *dev);
int mlx4_init_mcg_table(struct mlx4_dev *dev);

void mlx4_cleanup_pd_table(struct mlx4_dev *dev);
void mlx4_cleanup_xrcd_table(struct mlx4_dev *dev);
void mlx4_cleanup_uar_table(struct mlx4_dev *dev);
void mlx4_cleanup_mr_table(struct mlx4_dev *dev);
void mlx4_cleanup_eq_table(struct mlx4_dev *dev);
void mlx4_cleanup_cq_table(struct mlx4_dev *dev);
void mlx4_cleanup_qp_table(struct mlx4_dev *dev);
void mlx4_cleanup_srq_table(struct mlx4_dev *dev);
void mlx4_cleanup_mcg_table(struct mlx4_dev *dev);
int __mlx4_qp_alloc_icm(struct mlx4_dev *dev, int qpn);
void __mlx4_qp_free_icm(struct mlx4_dev *dev, int qpn);
int __mlx4_cq_alloc_icm(struct mlx4_dev *dev, int *cqn);
void __mlx4_cq_free_icm(struct mlx4_dev *dev, int cqn);
int __mlx4_srq_alloc_icm(struct mlx4_dev *dev, int *srqn);
void __mlx4_srq_free_icm(struct mlx4_dev *dev, int srqn);
int __mlx4_mpt_reserve(struct mlx4_dev *dev);
void __mlx4_mpt_release(struct mlx4_dev *dev, u32 index);
int __mlx4_mpt_alloc_icm(struct mlx4_dev *dev, u32 index);
void __mlx4_mpt_free_icm(struct mlx4_dev *dev, u32 index);
u32 __mlx4_alloc_mtt_range(struct mlx4_dev *dev, int order);
void __mlx4_free_mtt_range(struct mlx4_dev *dev, u32 first_seg, int order);

int mlx4_WRITE_MTT_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);
int mlx4_SYNC_TPT_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);
int mlx4_SW2HW_MPT_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);
int mlx4_HW2SW_MPT_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);
int mlx4_QUERY_MPT_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);
int mlx4_SW2HW_EQ_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd);
int mlx4_CONFIG_DEV_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			    struct mlx4_cmd_info *cmd);
int mlx4_DMA_wrapper(struct mlx4_dev *dev, int slave,
		     struct mlx4_vhcr *vhcr,
		     struct mlx4_cmd_mailbox *inbox,
		     struct mlx4_cmd_mailbox *outbox,
		     struct mlx4_cmd_info *cmd);
int __mlx4_qp_reserve_range(struct mlx4_dev *dev, int cnt, int align,
			    int *base, u8 flags);
void __mlx4_qp_release_range(struct mlx4_dev *dev, int base_qpn, int cnt);
int __mlx4_register_mac(struct mlx4_dev *dev, u8 port, u64 mac);
void __mlx4_unregister_mac(struct mlx4_dev *dev, u8 port, u64 mac);
int __mlx4_write_mtt(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		     int start_index, int npages, u64 *page_list);
int __mlx4_counter_alloc(struct mlx4_dev *dev, u32 *idx);
void __mlx4_counter_free(struct mlx4_dev *dev, u32 idx);
int mlx4_calc_vf_counters(struct mlx4_dev *dev, int slave, int port,
			  struct mlx4_counter *data);
int __mlx4_xrcd_alloc(struct mlx4_dev *dev, u32 *xrcdn);
void __mlx4_xrcd_free(struct mlx4_dev *dev, u32 xrcdn);

void mlx4_start_catas_poll(struct mlx4_dev *dev);
void mlx4_stop_catas_poll(struct mlx4_dev *dev);
int mlx4_catas_init(struct mlx4_dev *dev);
void mlx4_catas_end(struct mlx4_dev *dev);
int mlx4_restart_one(struct pci_dev *pdev);
int mlx4_register_device(struct mlx4_dev *dev);
void mlx4_unregister_device(struct mlx4_dev *dev);
void mlx4_dispatch_event(struct mlx4_dev *dev, enum mlx4_dev_event type,
			 unsigned long param);

struct mlx4_dev_cap;
struct mlx4_init_hca_param;

u64 mlx4_make_profile(struct mlx4_dev *dev,
		      struct mlx4_profile *request,
		      struct mlx4_dev_cap *dev_cap,
		      struct mlx4_init_hca_param *init_hca);
void mlx4_master_comm_channel(struct work_struct *work);
void mlx4_gen_slave_eqe(struct work_struct *work);
void mlx4_master_handle_slave_flr(struct work_struct *work);

int mlx4_ALLOC_RES_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);
int mlx4_FREE_RES_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd);
int mlx4_MAP_EQ_wrapper(struct mlx4_dev *dev, int slave,
			struct mlx4_vhcr *vhcr, struct mlx4_cmd_mailbox *inbox,
			struct mlx4_cmd_mailbox *outbox,
			struct mlx4_cmd_info *cmd);
int mlx4_COMM_INT_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd);
int mlx4_HW2SW_EQ_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd);
int mlx4_QUERY_EQ_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd);
int mlx4_SW2HW_CQ_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd);
int mlx4_HW2SW_CQ_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd);
int mlx4_QUERY_CQ_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd);
int mlx4_MODIFY_CQ_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);
int mlx4_SW2HW_SRQ_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);
int mlx4_HW2SW_SRQ_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);
int mlx4_QUERY_SRQ_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);
int mlx4_ARM_SRQ_wrapper(struct mlx4_dev *dev, int slave,
			 struct mlx4_vhcr *vhcr,
			 struct mlx4_cmd_mailbox *inbox,
			 struct mlx4_cmd_mailbox *outbox,
			 struct mlx4_cmd_info *cmd);
int mlx4_GEN_QP_wrapper(struct mlx4_dev *dev, int slave,
			struct mlx4_vhcr *vhcr,
			struct mlx4_cmd_mailbox *inbox,
			struct mlx4_cmd_mailbox *outbox,
			struct mlx4_cmd_info *cmd);
int mlx4_RST2INIT_QP_wrapper(struct mlx4_dev *dev, int slave,
			     struct mlx4_vhcr *vhcr,
			     struct mlx4_cmd_mailbox *inbox,
			     struct mlx4_cmd_mailbox *outbox,
			     struct mlx4_cmd_info *cmd);
int mlx4_INIT2INIT_QP_wrapper(struct mlx4_dev *dev, int slave,
			      struct mlx4_vhcr *vhcr,
			      struct mlx4_cmd_mailbox *inbox,
			      struct mlx4_cmd_mailbox *outbox,
			      struct mlx4_cmd_info *cmd);
int mlx4_INIT2RTR_QP_wrapper(struct mlx4_dev *dev, int slave,
			     struct mlx4_vhcr *vhcr,
			     struct mlx4_cmd_mailbox *inbox,
			     struct mlx4_cmd_mailbox *outbox,
			     struct mlx4_cmd_info *cmd);
int mlx4_RTR2RTS_QP_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			    struct mlx4_cmd_info *cmd);
int mlx4_RTS2RTS_QP_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			    struct mlx4_cmd_info *cmd);
int mlx4_SQERR2RTS_QP_wrapper(struct mlx4_dev *dev, int slave,
			      struct mlx4_vhcr *vhcr,
			      struct mlx4_cmd_mailbox *inbox,
			      struct mlx4_cmd_mailbox *outbox,
			      struct mlx4_cmd_info *cmd);
int mlx4_2ERR_QP_wrapper(struct mlx4_dev *dev, int slave,
			 struct mlx4_vhcr *vhcr,
			 struct mlx4_cmd_mailbox *inbox,
			 struct mlx4_cmd_mailbox *outbox,
			 struct mlx4_cmd_info *cmd);
int mlx4_RTS2SQD_QP_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			    struct mlx4_cmd_info *cmd);
int mlx4_SQD2SQD_QP_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			    struct mlx4_cmd_info *cmd);
int mlx4_SQD2RTS_QP_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			    struct mlx4_cmd_info *cmd);
int mlx4_2RST_QP_wrapper(struct mlx4_dev *dev, int slave,
			 struct mlx4_vhcr *vhcr,
			 struct mlx4_cmd_mailbox *inbox,
			 struct mlx4_cmd_mailbox *outbox,
			 struct mlx4_cmd_info *cmd);
int mlx4_QUERY_QP_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd);

int mlx4_GEN_EQE(struct mlx4_dev *dev, int slave, struct mlx4_eqe *eqe);

enum {
	MLX4_CMD_CLEANUP_STRUCT = 1UL << 0,
	MLX4_CMD_CLEANUP_POOL	= 1UL << 1,
	MLX4_CMD_CLEANUP_HCR	= 1UL << 2,
	MLX4_CMD_CLEANUP_VHCR	= 1UL << 3,
	MLX4_CMD_CLEANUP_ALL	= (MLX4_CMD_CLEANUP_VHCR << 1) - 1
};

int mlx4_cmd_init(struct mlx4_dev *dev);
void mlx4_cmd_cleanup(struct mlx4_dev *dev, int cleanup_mask);
int mlx4_multi_func_init(struct mlx4_dev *dev);
int mlx4_ARM_COMM_CHANNEL(struct mlx4_dev *dev);
void mlx4_multi_func_cleanup(struct mlx4_dev *dev);
void mlx4_cmd_event(struct mlx4_dev *dev, u16 token, u8 status, u64 out_param);
int mlx4_cmd_use_events(struct mlx4_dev *dev);
void mlx4_cmd_use_polling(struct mlx4_dev *dev);

int mlx4_comm_cmd(struct mlx4_dev *dev, u8 cmd, u16 param,
		  u16 op, unsigned long timeout);

void mlx4_cq_tasklet_cb(unsigned long data);
void mlx4_cq_completion(struct mlx4_dev *dev, u32 cqn);
void mlx4_cq_event(struct mlx4_dev *dev, u32 cqn, int event_type);

void mlx4_qp_event(struct mlx4_dev *dev, u32 qpn, int event_type);

void mlx4_srq_event(struct mlx4_dev *dev, u32 srqn, int event_type);

void mlx4_enter_error_state(struct mlx4_dev_persistent *persist);
int mlx4_comm_internal_err(u32 slave_read);

int mlx4_SENSE_PORT(struct mlx4_dev *dev, int port,
		    enum mlx4_port_type *type);
void mlx4_do_sense_ports(struct mlx4_dev *dev,
			 enum mlx4_port_type *stype,
			 enum mlx4_port_type *defaults);
void mlx4_start_sense(struct mlx4_dev *dev);
void mlx4_stop_sense(struct mlx4_dev *dev);
void mlx4_sense_init(struct mlx4_dev *dev);
int mlx4_check_port_params(struct mlx4_dev *dev,
			   enum mlx4_port_type *port_type);
int mlx4_change_port_types(struct mlx4_dev *dev,
			   enum mlx4_port_type *port_types);

void mlx4_init_mac_table(struct mlx4_dev *dev, struct mlx4_mac_table *table);
void mlx4_init_vlan_table(struct mlx4_dev *dev, struct mlx4_vlan_table *table);
void mlx4_init_roce_gid_table(struct mlx4_dev *dev,
			      struct mlx4_roce_gid_table *table);
void __mlx4_unregister_vlan(struct mlx4_dev *dev, u8 port, u16 vlan);
int __mlx4_register_vlan(struct mlx4_dev *dev, u8 port, u16 vlan, int *index);
int mlx4_bond_vlan_table(struct mlx4_dev *dev);
int mlx4_unbond_vlan_table(struct mlx4_dev *dev);
int mlx4_bond_mac_table(struct mlx4_dev *dev);
int mlx4_unbond_mac_table(struct mlx4_dev *dev);

int mlx4_SET_PORT(struct mlx4_dev *dev, u8 port, int pkey_tbl_sz);
/* resource tracker functions*/
int mlx4_get_slave_from_resource_id(struct mlx4_dev *dev,
				    enum mlx4_resource resource_type,
				    u64 resource_id, int *slave);
void mlx4_delete_all_resources_for_slave(struct mlx4_dev *dev, int slave_id);
void mlx4_reset_roce_gids(struct mlx4_dev *dev, int slave);
int mlx4_init_resource_tracker(struct mlx4_dev *dev);

void mlx4_free_resource_tracker(struct mlx4_dev *dev,
				enum mlx4_res_tracker_free_type type);

int mlx4_QUERY_FW_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd);
int mlx4_SET_PORT_wrapper(struct mlx4_dev *dev, int slave,
			  struct mlx4_vhcr *vhcr,
			  struct mlx4_cmd_mailbox *inbox,
			  struct mlx4_cmd_mailbox *outbox,
			  struct mlx4_cmd_info *cmd);
int mlx4_INIT_PORT_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);
int mlx4_CLOSE_PORT_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			    struct mlx4_cmd_info *cmd);
int mlx4_QUERY_DEV_CAP_wrapper(struct mlx4_dev *dev, int slave,
			       struct mlx4_vhcr *vhcr,
			       struct mlx4_cmd_mailbox *inbox,
			       struct mlx4_cmd_mailbox *outbox,
			       struct mlx4_cmd_info *cmd);
int mlx4_QUERY_PORT_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			    struct mlx4_cmd_info *cmd);
int mlx4_get_port_ib_caps(struct mlx4_dev *dev, u8 port, __be32 *caps);

int mlx4_get_slave_pkey_gid_tbl_len(struct mlx4_dev *dev, u8 port,
				    int *gid_tbl_len, int *pkey_tbl_len);

int mlx4_QP_ATTACH_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);

int mlx4_UPDATE_QP_wrapper(struct mlx4_dev *dev, int slave,
			   struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox,
			   struct mlx4_cmd_mailbox *outbox,
			   struct mlx4_cmd_info *cmd);

int mlx4_PROMISC_wrapper(struct mlx4_dev *dev, int slave,
			 struct mlx4_vhcr *vhcr,
			 struct mlx4_cmd_mailbox *inbox,
			 struct mlx4_cmd_mailbox *outbox,
			 struct mlx4_cmd_info *cmd);
int mlx4_qp_detach_common(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
			  enum mlx4_protocol prot, enum mlx4_steer_type steer);
int mlx4_qp_attach_common(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
			  int block_mcast_loopback, enum mlx4_protocol prot,
			  enum mlx4_steer_type steer);
int mlx4_trans_to_dmfs_attach(struct mlx4_dev *dev, struct mlx4_qp *qp,
			      u8 gid[16], u8 port,
			      int block_mcast_loopback,
			      enum mlx4_protocol prot, u64 *reg_id);
int mlx4_SET_MCAST_FLTR_wrapper(struct mlx4_dev *dev, int slave,
				struct mlx4_vhcr *vhcr,
				struct mlx4_cmd_mailbox *inbox,
				struct mlx4_cmd_mailbox *outbox,
				struct mlx4_cmd_info *cmd);
int mlx4_SET_VLAN_FLTR_wrapper(struct mlx4_dev *dev, int slave,
			       struct mlx4_vhcr *vhcr,
			       struct mlx4_cmd_mailbox *inbox,
			       struct mlx4_cmd_mailbox *outbox,
			       struct mlx4_cmd_info *cmd);
int mlx4_common_set_vlan_fltr(struct mlx4_dev *dev, int function,
				     int port, void *buf);
int mlx4_DUMP_ETH_STATS_wrapper(struct mlx4_dev *dev, int slave,
				   struct mlx4_vhcr *vhcr,
				   struct mlx4_cmd_mailbox *inbox,
				   struct mlx4_cmd_mailbox *outbox,
				struct mlx4_cmd_info *cmd);
int mlx4_PKEY_TABLE_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			    struct mlx4_cmd_info *cmd);
int mlx4_QUERY_IF_STAT_wrapper(struct mlx4_dev *dev, int slave,
			       struct mlx4_vhcr *vhcr,
			       struct mlx4_cmd_mailbox *inbox,
			       struct mlx4_cmd_mailbox *outbox,
			       struct mlx4_cmd_info *cmd);
int mlx4_QP_FLOW_STEERING_ATTACH_wrapper(struct mlx4_dev *dev, int slave,
					 struct mlx4_vhcr *vhcr,
					 struct mlx4_cmd_mailbox *inbox,
					 struct mlx4_cmd_mailbox *outbox,
					 struct mlx4_cmd_info *cmd);
int mlx4_QP_FLOW_STEERING_DETACH_wrapper(struct mlx4_dev *dev, int slave,
					 struct mlx4_vhcr *vhcr,
					 struct mlx4_cmd_mailbox *inbox,
					 struct mlx4_cmd_mailbox *outbox,
					 struct mlx4_cmd_info *cmd);
int mlx4_ACCESS_REG_wrapper(struct mlx4_dev *dev, int slave,
			    struct mlx4_vhcr *vhcr,
			    struct mlx4_cmd_mailbox *inbox,
			    struct mlx4_cmd_mailbox *outbox,
			    struct mlx4_cmd_info *cmd);

int mlx4_get_mgm_entry_size(struct mlx4_dev *dev);
int mlx4_get_qp_per_mgm(struct mlx4_dev *dev);

static inline void set_param_l(u64 *arg, u32 val)
{
	*arg = (*arg & 0xffffffff00000000ULL) | (u64) val;
}

static inline void set_param_h(u64 *arg, u32 val)
{
	*arg = (*arg & 0xffffffff) | ((u64) val << 32);
}

static inline u32 get_param_l(u64 *arg)
{
	return (u32) (*arg & 0xffffffff);
}

static inline u32 get_param_h(u64 *arg)
{
	return (u32)(*arg >> 32);
}

static inline spinlock_t *mlx4_tlock(struct mlx4_dev *dev)
{
	return &mlx4_priv(dev)->mfunc.master.res_tracker.lock;
}

#define NOT_MASKED_PD_BITS 17

void mlx4_vf_immed_vlan_work_handler(struct work_struct *_work);

void mlx4_init_quotas(struct mlx4_dev *dev);

/* for VFs, replace zero MACs with randomly-generated MACs at driver start */
void mlx4_replace_zero_macs(struct mlx4_dev *dev);
int mlx4_get_slave_num_gids(struct mlx4_dev *dev, int slave, int port);
/* Returns the VF index of slave */
int mlx4_get_vf_indx(struct mlx4_dev *dev, int slave);
int mlx4_config_mad_demux(struct mlx4_dev *dev);
int mlx4_do_bond(struct mlx4_dev *dev, bool enable);
int mlx4_bond_fs_rules(struct mlx4_dev *dev);
int mlx4_unbond_fs_rules(struct mlx4_dev *dev);

enum mlx4_zone_flags {
	MLX4_ZONE_ALLOW_ALLOC_FROM_LOWER_PRIO	= 1UL << 0,
	MLX4_ZONE_ALLOW_ALLOC_FROM_EQ_PRIO	= 1UL << 1,
	MLX4_ZONE_FALLBACK_TO_HIGHER_PRIO	= 1UL << 2,
	MLX4_ZONE_USE_RR			= 1UL << 3,
};

enum mlx4_zone_alloc_flags {
	/* No two objects could overlap between zones. UID
	 * could be left unused. If this flag is given and
	 * two overlapped zones are used, an object will be free'd
	 * from the smallest possible matching zone.
	 */
	MLX4_ZONE_ALLOC_FLAGS_NO_OVERLAP	= 1UL << 0,
};

struct mlx4_zone_allocator;

/* Create a new zone allocator */
struct mlx4_zone_allocator *mlx4_zone_allocator_create(enum mlx4_zone_alloc_flags flags);

/* Attach a mlx4_bitmap <bitmap> of priority <priority> to the zone allocator
 * <zone_alloc>. Allocating an object from this zone adds an offset <offset>.
 * Similarly, when searching for an object to free, this offset it taken into
 * account. The use_rr mlx4_ib parameter for allocating objects from this <bitmap>
 * is given through the MLX4_ZONE_USE_RR flag in <flags>.
 * When an allocation fails, <zone_alloc> tries to allocate from other zones
 * according to the policy set by <flags>. <puid> is the unique identifier
 * received to this zone.
 */
int mlx4_zone_add_one(struct mlx4_zone_allocator *zone_alloc,
		      struct mlx4_bitmap *bitmap,
		      u32 flags,
		      int priority,
		      int offset,
		      u32 *puid);

/* Remove bitmap indicated by <uid> from <zone_alloc> */
int mlx4_zone_remove_one(struct mlx4_zone_allocator *zone_alloc, u32 uid);

/* Delete the zone allocator <zone_alloc. This function doesn't destroy
 * the attached bitmaps.
 */
void mlx4_zone_allocator_destroy(struct mlx4_zone_allocator *zone_alloc);

/* Allocate <count> objects with align <align> and skip_mask <skip_mask>
 * from the mlx4_bitmap whose uid is <uid>. The bitmap which we actually
 * allocated from is returned in <puid>. If the allocation fails, a negative
 * number is returned. Otherwise, the offset of the first object is returned.
 */
u32 mlx4_zone_alloc_entries(struct mlx4_zone_allocator *zones, u32 uid, int count,
			    int align, u32 skip_mask, u32 *puid);

/* Free <count> objects, start from <obj> of the uid <uid> from zone_allocator
 * <zones>.
 */
u32 mlx4_zone_free_entries(struct mlx4_zone_allocator *zones,
			   u32 uid, u32 obj, u32 count);

/* If <zones> was allocated with MLX4_ZONE_ALLOC_FLAGS_NO_OVERLAP, instead of
 * specifying the uid when freeing an object, zone allocator could figure it by
 * itself. Other parameters are similar to mlx4_zone_free.
 */
u32 mlx4_zone_free_entries_unique(struct mlx4_zone_allocator *zones, u32 obj, u32 count);

/* Returns a pointer to mlx4_bitmap that was attached to <zones> with <uid> */
struct mlx4_bitmap *mlx4_zone_get_bitmap(struct mlx4_zone_allocator *zones, u32 uid);

#endif /* MLX4_H */
