/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_H
#define _QED_H

#include <linux/types.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/zlib.h>
#include <linux/hashtable.h>
#include <linux/qed/qed_if.h>
#include "qed_debug.h"
#include "qed_hsi.h"

extern const struct qed_common_ops qed_common_ops_pass;
#define DRV_MODULE_VERSION "8.10.9.20"

#define MAX_HWFNS_PER_DEVICE    (4)
#define NAME_SIZE 16
#define VER_SIZE 16

#define QED_WFQ_UNIT	100

#define ISCSI_BDQ_ID(_port_id) (_port_id)
#define QED_WID_SIZE            (1024)
#define QED_PF_DEMS_SIZE        (4)

/* cau states */
enum qed_coalescing_mode {
	QED_COAL_MODE_DISABLE,
	QED_COAL_MODE_ENABLE
};

struct qed_eth_cb_ops;
struct qed_dev_info;
union qed_mcp_protocol_stats;
enum qed_mcp_protocol_type;

/* helpers */
static inline u32 qed_db_addr(u32 cid, u32 DEMS)
{
	u32 db_addr = FIELD_VALUE(DB_LEGACY_ADDR_DEMS, DEMS) |
		      (cid * QED_PF_DEMS_SIZE);

	return db_addr;
}

static inline u32 qed_db_addr_vf(u32 cid, u32 DEMS)
{
	u32 db_addr = FIELD_VALUE(DB_LEGACY_ADDR_DEMS, DEMS) |
		      FIELD_VALUE(DB_LEGACY_ADDR_ICID, cid);

	return db_addr;
}

#define ALIGNED_TYPE_SIZE(type_name, p_hwfn)				     \
	((sizeof(type_name) + (u32)(1 << (p_hwfn->cdev->cache_shift)) - 1) & \
	 ~((1 << (p_hwfn->cdev->cache_shift)) - 1))

#define for_each_hwfn(cdev, i)  for (i = 0; i < cdev->num_hwfns; i++)

#define D_TRINE(val, cond1, cond2, true1, true2, def) \
	(val == (cond1) ? true1 :		      \
	 (val == (cond2) ? true2 : def))

/* forward */
struct qed_ptt_pool;
struct qed_spq;
struct qed_sb_info;
struct qed_sb_attn_info;
struct qed_cxt_mngr;
struct qed_sb_sp_info;
struct qed_ll2_info;
struct qed_mcp_info;

struct qed_rt_data {
	u32	*init_val;
	bool	*b_valid;
};

enum qed_tunn_mode {
	QED_MODE_L2GENEVE_TUNN,
	QED_MODE_IPGENEVE_TUNN,
	QED_MODE_L2GRE_TUNN,
	QED_MODE_IPGRE_TUNN,
	QED_MODE_VXLAN_TUNN,
};

enum qed_tunn_clss {
	QED_TUNN_CLSS_MAC_VLAN,
	QED_TUNN_CLSS_MAC_VNI,
	QED_TUNN_CLSS_INNER_MAC_VLAN,
	QED_TUNN_CLSS_INNER_MAC_VNI,
	MAX_QED_TUNN_CLSS,
};

struct qed_tunn_start_params {
	unsigned long	tunn_mode;
	u16		vxlan_udp_port;
	u16		geneve_udp_port;
	u8		update_vxlan_udp_port;
	u8		update_geneve_udp_port;
	u8		tunn_clss_vxlan;
	u8		tunn_clss_l2geneve;
	u8		tunn_clss_ipgeneve;
	u8		tunn_clss_l2gre;
	u8		tunn_clss_ipgre;
};

struct qed_tunn_update_params {
	unsigned long	tunn_mode_update_mask;
	unsigned long	tunn_mode;
	u16		vxlan_udp_port;
	u16		geneve_udp_port;
	u8		update_rx_pf_clss;
	u8		update_tx_pf_clss;
	u8		update_vxlan_udp_port;
	u8		update_geneve_udp_port;
	u8		tunn_clss_vxlan;
	u8		tunn_clss_l2geneve;
	u8		tunn_clss_ipgeneve;
	u8		tunn_clss_l2gre;
	u8		tunn_clss_ipgre;
};

/* The PCI personality is not quite synonymous to protocol ID:
 * 1. All personalities need CORE connections
 * 2. The Ethernet personality may support also the RoCE protocol
 */
enum qed_pci_personality {
	QED_PCI_ETH,
	QED_PCI_ISCSI,
	QED_PCI_ETH_ROCE,
	QED_PCI_DEFAULT /* default in shmem */
};

/* All VFs are symmetric, all counters are PF + all VFs */
struct qed_qm_iids {
	u32 cids;
	u32 vf_cids;
	u32 tids;
};

/* HW / FW resources, output of features supported below, most information
 * is received from MFW.
 */
enum qed_resources {
	QED_SB,
	QED_L2_QUEUE,
	QED_VPORT,
	QED_RSS_ENG,
	QED_PQ,
	QED_RL,
	QED_MAC,
	QED_VLAN,
	QED_RDMA_CNQ_RAM,
	QED_ILT,
	QED_LL2_QUEUE,
	QED_CMDQS_CQS,
	QED_RDMA_STATS_QUEUE,
	QED_MAX_RESC,
};

enum QED_FEATURE {
	QED_PF_L2_QUE,
	QED_VF,
	QED_RDMA_CNQ,
	QED_VF_L2_QUE,
	QED_MAX_FEATURES,
};

enum QED_PORT_MODE {
	QED_PORT_MODE_DE_2X40G,
	QED_PORT_MODE_DE_2X50G,
	QED_PORT_MODE_DE_1X100G,
	QED_PORT_MODE_DE_4X10G_F,
	QED_PORT_MODE_DE_4X10G_E,
	QED_PORT_MODE_DE_4X20G,
	QED_PORT_MODE_DE_1X40G,
	QED_PORT_MODE_DE_2X25G,
	QED_PORT_MODE_DE_1X25G
};

enum qed_dev_cap {
	QED_DEV_CAP_ETH,
	QED_DEV_CAP_ISCSI,
	QED_DEV_CAP_ROCE,
};

enum qed_wol_support {
	QED_WOL_SUPPORT_NONE,
	QED_WOL_SUPPORT_PME,
};

struct qed_hw_info {
	/* PCI personality */
	enum qed_pci_personality	personality;

	/* Resource Allocation scheme results */
	u32				resc_start[QED_MAX_RESC];
	u32				resc_num[QED_MAX_RESC];
	u32				feat_num[QED_MAX_FEATURES];

#define RESC_START(_p_hwfn, resc) ((_p_hwfn)->hw_info.resc_start[resc])
#define RESC_NUM(_p_hwfn, resc) ((_p_hwfn)->hw_info.resc_num[resc])
#define RESC_END(_p_hwfn, resc) (RESC_START(_p_hwfn, resc) + \
				 RESC_NUM(_p_hwfn, resc))
#define FEAT_NUM(_p_hwfn, resc) ((_p_hwfn)->hw_info.feat_num[resc])

	u8				num_tc;
	u8				offload_tc;
	u8				non_offload_tc;

	u32				concrete_fid;
	u16				opaque_fid;
	u16				ovlan;
	u32				part_num[4];

	unsigned char			hw_mac_addr[ETH_ALEN];

	struct qed_igu_info		*p_igu_info;

	u32				port_mode;
	u32				hw_mode;
	unsigned long		device_capabilities;
	u16				mtu;

	enum qed_wol_support b_wol_support;
};

/* maximun size of read/write commands (HW limit) */
#define DMAE_MAX_RW_SIZE        0x2000

struct qed_dmae_info {
	/* Mutex for synchronizing access to functions */
	struct mutex	mutex;

	u8		channel;

	dma_addr_t	completion_word_phys_addr;

	/* The memory location where the DMAE writes the completion
	 * value when an operation is finished on this context.
	 */
	u32		*p_completion_word;

	dma_addr_t	intermediate_buffer_phys_addr;

	/* An intermediate buffer for DMAE operations that use virtual
	 * addresses - data is DMA'd to/from this buffer and then
	 * memcpy'd to/from the virtual address
	 */
	u32		*p_intermediate_buffer;

	dma_addr_t	dmae_cmd_phys_addr;
	struct dmae_cmd *p_dmae_cmd;
};

struct qed_wfq_data {
	/* when feature is configured for at least 1 vport */
	u32	min_speed;
	bool	configured;
};

struct qed_qm_info {
	struct init_qm_pq_params	*qm_pq_params;
	struct init_qm_vport_params	*qm_vport_params;
	struct init_qm_port_params	*qm_port_params;
	u16				start_pq;
	u8				start_vport;
	u8				pure_lb_pq;
	u8				offload_pq;
	u8				pure_ack_pq;
	u8 ooo_pq;
	u8				vf_queues_offset;
	u16				num_pqs;
	u16				num_vf_pqs;
	u8				num_vports;
	u8				max_phys_tcs_per_port;
	bool				pf_rl_en;
	bool				pf_wfq_en;
	bool				vport_rl_en;
	bool				vport_wfq_en;
	u8				pf_wfq;
	u32				pf_rl;
	struct qed_wfq_data		*wfq_data;
	u8 num_pf_rls;
};

struct storm_stats {
	u32     address;
	u32     len;
};

struct qed_storm_stats {
	struct storm_stats mstats;
	struct storm_stats pstats;
	struct storm_stats tstats;
	struct storm_stats ustats;
};

struct qed_fw_data {
	struct fw_ver_info	*fw_ver_info;
	const u8		*modes_tree_buf;
	union init_op		*init_ops;
	const u32		*arr_data;
	u32			init_ops_size;
};

struct qed_simd_fp_handler {
	void	*token;
	void	(*func)(void *);
};

struct qed_hwfn {
	struct qed_dev			*cdev;
	u8				my_id;          /* ID inside the PF */
#define IS_LEAD_HWFN(edev)              (!((edev)->my_id))
	u8				rel_pf_id;      /* Relative to engine*/
	u8				abs_pf_id;
#define QED_PATH_ID(_p_hwfn)		((_p_hwfn)->abs_pf_id & 1)
	u8				port_id;
	bool				b_active;

	u32				dp_module;
	u8				dp_level;
	char				name[NAME_SIZE];

	bool				first_on_engine;
	bool				hw_init_done;

	u8				num_funcs_on_engine;
	u8 enabled_func_idx;

	/* BAR access */
	void __iomem			*regview;
	void __iomem			*doorbells;
	u64				db_phys_addr;
	unsigned long			db_size;

	/* PTT pool */
	struct qed_ptt_pool		*p_ptt_pool;

	/* HW info */
	struct qed_hw_info		hw_info;

	/* rt_array (for init-tool) */
	struct qed_rt_data		rt_data;

	/* SPQ */
	struct qed_spq			*p_spq;

	/* EQ */
	struct qed_eq			*p_eq;

	/* Consolidate Q*/
	struct qed_consq		*p_consq;

	/* Slow-Path definitions */
	struct tasklet_struct		*sp_dpc;
	bool				b_sp_dpc_enabled;

	struct qed_ptt			*p_main_ptt;
	struct qed_ptt			*p_dpc_ptt;

	struct qed_sb_sp_info		*p_sp_sb;
	struct qed_sb_attn_info		*p_sb_attn;

	/* Protocol related */
	bool				using_ll2;
	struct qed_ll2_info		*p_ll2_info;
	struct qed_ooo_info		*p_ooo_info;
	struct qed_rdma_info		*p_rdma_info;
	struct qed_iscsi_info		*p_iscsi_info;
	struct qed_pf_params		pf_params;

	bool b_rdma_enabled_in_prs;
	u32 rdma_prs_search_reg;

	/* Array of sb_info of all status blocks */
	struct qed_sb_info		*sbs_info[MAX_SB_PER_PF_MIMD];
	u16				num_sbs;

	struct qed_cxt_mngr		*p_cxt_mngr;

	/* Flag indicating whether interrupts are enabled or not*/
	bool				b_int_enabled;
	bool				b_int_requested;

	/* True if the driver requests for the link */
	bool				b_drv_link_init;

	struct qed_vf_iov		*vf_iov_info;
	struct qed_pf_iov		*pf_iov_info;
	struct qed_mcp_info		*mcp_info;

	struct qed_dcbx_info		*p_dcbx_info;

	struct qed_dmae_info		dmae_info;

	/* QM init */
	struct qed_qm_info		qm_info;
	struct qed_storm_stats		storm_stats;

	/* Buffer for unzipping firmware data */
	void				*unzip_buf;

	struct dbg_tools_data		dbg_info;

	/* PWM region specific data */
	u32				dpi_size;
	u32				dpi_count;

	/* This is used to calculate the doorbell address */
	u32 dpi_start_offset;

	/* If one of the following is set then EDPM shouldn't be used */
	u8 dcbx_no_edpm;
	u8 db_bar_no_edpm;

	struct qed_simd_fp_handler	simd_proto_handler[64];

#ifdef CONFIG_QED_SRIOV
	struct workqueue_struct *iov_wq;
	struct delayed_work iov_task;
	unsigned long iov_task_flags;
#endif

	struct z_stream_s		*stream;
	struct qed_roce_ll2_info	*ll2;
};

struct pci_params {
	int		pm_cap;

	unsigned long	mem_start;
	unsigned long	mem_end;
	unsigned int	irq;
	u8		pf_num;
};

struct qed_int_param {
	u32	int_mode;
	u8	num_vectors;
	u8	min_msix_cnt; /* for minimal functionality */
};

struct qed_int_params {
	struct qed_int_param	in;
	struct qed_int_param	out;
	struct msix_entry	*msix_table;
	bool			fp_initialized;
	u8			fp_msix_base;
	u8			fp_msix_cnt;
	u8			rdma_msix_base;
	u8			rdma_msix_cnt;
};

struct qed_dbg_feature {
	struct dentry *dentry;
	u8 *dump_buf;
	u32 buf_size;
	u32 dumped_dwords;
};

struct qed_dbg_params {
	struct qed_dbg_feature features[DBG_FEATURE_NUM];
	u8 engine_for_debug;
	bool print_data;
};

struct qed_dev {
	u32	dp_module;
	u8	dp_level;
	char	name[NAME_SIZE];

	u8	type;
#define QED_DEV_TYPE_BB (0 << 0)
#define QED_DEV_TYPE_AH BIT(0)
/* Translate type/revision combo into the proper conditions */
#define QED_IS_BB(dev)  ((dev)->type == QED_DEV_TYPE_BB)
#define QED_IS_BB_A0(dev)       (QED_IS_BB(dev) && \
				 CHIP_REV_IS_A0(dev))
#define QED_IS_BB_B0(dev)       (QED_IS_BB(dev) && \
				 CHIP_REV_IS_B0(dev))
#define QED_IS_AH(dev)  ((dev)->type == QED_DEV_TYPE_AH)
#define QED_IS_K2(dev)  QED_IS_AH(dev)

#define QED_GET_TYPE(dev)       (QED_IS_BB_A0(dev) ? CHIP_BB_A0 : \
				 QED_IS_BB_B0(dev) ? CHIP_BB_B0 : CHIP_K2)

	u16	vendor_id;
	u16	device_id;

	u16	chip_num;
#define CHIP_NUM_MASK                   0xffff
#define CHIP_NUM_SHIFT                  16

	u16	chip_rev;
#define CHIP_REV_MASK                   0xf
#define CHIP_REV_SHIFT                  12
#define CHIP_REV_IS_A0(_cdev)   (!(_cdev)->chip_rev)
#define CHIP_REV_IS_B0(_cdev)   ((_cdev)->chip_rev == 1)

	u16				chip_metal;
#define CHIP_METAL_MASK                 0xff
#define CHIP_METAL_SHIFT                4

	u16				chip_bond_id;
#define CHIP_BOND_ID_MASK               0xf
#define CHIP_BOND_ID_SHIFT              0

	u8				num_engines;
	u8				num_ports_in_engines;
	u8				num_funcs_in_port;

	u8				path_id;
	enum qed_mf_mode		mf_mode;
#define IS_MF_DEFAULT(_p_hwfn)  (((_p_hwfn)->cdev)->mf_mode == QED_MF_DEFAULT)
#define IS_MF_SI(_p_hwfn)       (((_p_hwfn)->cdev)->mf_mode == QED_MF_NPAR)
#define IS_MF_SD(_p_hwfn)       (((_p_hwfn)->cdev)->mf_mode == QED_MF_OVLAN)

	int				pcie_width;
	int				pcie_speed;
	u8				ver_str[VER_SIZE];

	/* Add MF related configuration */
	u8				mcp_rev;
	u8				boot_mode;

	/* WoL related configurations */
	u8 wol_config;
	u8 wol_mac[ETH_ALEN];

	u32				int_mode;
	enum qed_coalescing_mode	int_coalescing_mode;
	u16				rx_coalesce_usecs;
	u16				tx_coalesce_usecs;

	/* Start Bar offset of first hwfn */
	void __iomem			*regview;
	void __iomem			*doorbells;
	u64				db_phys_addr;
	unsigned long			db_size;

	/* PCI */
	u8				cache_shift;

	/* Init */
	const struct iro		*iro_arr;
#define IRO (p_hwfn->cdev->iro_arr)

	/* HW functions */
	u8				num_hwfns;
	struct qed_hwfn			hwfns[MAX_HWFNS_PER_DEVICE];

	/* SRIOV */
	struct qed_hw_sriov_info *p_iov_info;
#define IS_QED_SRIOV(cdev)              (!!(cdev)->p_iov_info)

	unsigned long			tunn_mode;

	bool				b_is_vf;
	u32				drv_type;
	struct qed_eth_stats		*reset_stats;
	struct qed_fw_data		*fw_data;

	u32				mcp_nvm_resp;

	/* Linux specific here */
	struct  qede_dev		*edev;
	struct  pci_dev			*pdev;
	u32 flags;
#define QED_FLAG_STORAGE_STARTED	(BIT(0))
	int				msg_enable;

	struct pci_params		pci_params;

	struct qed_int_params		int_params;

	u8				protocol;
#define IS_QED_ETH_IF(cdev)     ((cdev)->protocol == QED_PROTOCOL_ETH)

	/* Callbacks to protocol driver */
	union {
		struct qed_common_cb_ops	*common;
		struct qed_eth_cb_ops		*eth;
		struct qed_iscsi_cb_ops		*iscsi;
	} protocol_ops;
	void				*ops_cookie;

	struct qed_dbg_params		dbg_params;

#ifdef CONFIG_QED_LL2
	struct qed_cb_ll2_info		*ll2;
	u8				ll2_mac_address[ETH_ALEN];
#endif
	DECLARE_HASHTABLE(connections, 10);
	const struct firmware		*firmware;

	u32 rdma_max_sge;
	u32 rdma_max_inline;
	u32 rdma_max_srq_sge;
};

#define NUM_OF_VFS(dev)         MAX_NUM_VFS_BB
#define NUM_OF_L2_QUEUES(dev)	MAX_NUM_L2_QUEUES_BB
#define NUM_OF_SBS(dev)         MAX_SB_PER_PATH_BB
#define NUM_OF_ENG_PFS(dev)     MAX_NUM_PFS_BB

/**
 * @brief qed_concrete_to_sw_fid - get the sw function id from
 *        the concrete value.
 *
 * @param concrete_fid
 *
 * @return inline u8
 */
static inline u8 qed_concrete_to_sw_fid(struct qed_dev *cdev,
					u32 concrete_fid)
{
	u8 vfid = GET_FIELD(concrete_fid, PXP_CONCRETE_FID_VFID);
	u8 pfid = GET_FIELD(concrete_fid, PXP_CONCRETE_FID_PFID);
	u8 vf_valid = GET_FIELD(concrete_fid,
				PXP_CONCRETE_FID_VFVALID);
	u8 sw_fid;

	if (vf_valid)
		sw_fid = vfid + MAX_NUM_PFS;
	else
		sw_fid = pfid;

	return sw_fid;
}

#define PURE_LB_TC 8
#define OOO_LB_TC 9

int qed_configure_vport_wfq(struct qed_dev *cdev, u16 vp_id, u32 rate);
void qed_configure_vp_wfq_on_link_change(struct qed_dev *cdev, u32 min_pf_rate);

void qed_clean_wfq_db(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);
#define QED_LEADING_HWFN(dev)   (&dev->hwfns[0])

/* Other Linux specific common definitions */
#define DP_NAME(cdev) ((cdev)->name)

#define REG_ADDR(cdev, offset)          (void __iomem *)((u8 __iomem *)\
						(cdev->regview) + \
							 (offset))

#define REG_RD(cdev, offset)            readl(REG_ADDR(cdev, offset))
#define REG_WR(cdev, offset, val)       writel((u32)val, REG_ADDR(cdev, offset))
#define REG_WR16(cdev, offset, val)     writew((u16)val, REG_ADDR(cdev, offset))

#define DOORBELL(cdev, db_addr, val)			 \
	writel((u32)val, (void __iomem *)((u8 __iomem *)\
					  (cdev->doorbells) + (db_addr)))

/* Prototypes */
int qed_fill_dev_info(struct qed_dev *cdev,
		      struct qed_dev_info *dev_info);
void qed_link_update(struct qed_hwfn *hwfn);
u32 qed_unzip_data(struct qed_hwfn *p_hwfn,
		   u32 input_len, u8 *input_buf,
		   u32 max_size, u8 *unzip_buf);
void qed_get_protocol_stats(struct qed_dev *cdev,
			    enum qed_mcp_protocol_type type,
			    union qed_mcp_protocol_stats *stats);
int qed_slowpath_irq_req(struct qed_hwfn *hwfn);

#endif /* _QED_H */
