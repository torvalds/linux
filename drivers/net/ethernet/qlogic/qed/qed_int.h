/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_INT_H
#define _QED_INT_H

#include <linux/types.h>
#include <linux/slab.h>
#include "qed.h"

/* Fields of IGU PF CONFIGRATION REGISTER */
#define IGU_PF_CONF_FUNC_EN       (0x1 << 0)    /* function enable        */
#define IGU_PF_CONF_MSI_MSIX_EN   (0x1 << 1)    /* MSI/MSIX enable        */
#define IGU_PF_CONF_INT_LINE_EN   (0x1 << 2)    /* INT enable             */
#define IGU_PF_CONF_ATTN_BIT_EN   (0x1 << 3)    /* attention enable       */
#define IGU_PF_CONF_SINGLE_ISR_EN (0x1 << 4)    /* single ISR mode enable */
#define IGU_PF_CONF_SIMD_MODE     (0x1 << 5)    /* simd all ones mode     */
/* Fields of IGU VF CONFIGRATION REGISTER */
#define IGU_VF_CONF_FUNC_EN        (0x1 << 0)	/* function enable        */
#define IGU_VF_CONF_MSI_MSIX_EN    (0x1 << 1)	/* MSI/MSIX enable        */
#define IGU_VF_CONF_SINGLE_ISR_EN  (0x1 << 4)	/* single ISR mode enable */
#define IGU_VF_CONF_PARENT_MASK    (0xF)	/* Parent PF              */
#define IGU_VF_CONF_PARENT_SHIFT   5		/* Parent PF              */

/* Igu control commands
 */
enum igu_ctrl_cmd {
	IGU_CTRL_CMD_TYPE_RD,
	IGU_CTRL_CMD_TYPE_WR,
	MAX_IGU_CTRL_CMD
};

/* Control register for the IGU command register
 */
struct igu_ctrl_reg {
	u32 ctrl_data;
#define IGU_CTRL_REG_FID_MASK           0xFFFF  /* Opaque_FID	 */
#define IGU_CTRL_REG_FID_SHIFT          0
#define IGU_CTRL_REG_PXP_ADDR_MASK      0xFFF   /* Command address */
#define IGU_CTRL_REG_PXP_ADDR_SHIFT     16
#define IGU_CTRL_REG_RESERVED_MASK      0x1
#define IGU_CTRL_REG_RESERVED_SHIFT     28
#define IGU_CTRL_REG_TYPE_MASK          0x1 /* use enum igu_ctrl_cmd */
#define IGU_CTRL_REG_TYPE_SHIFT         31
};

enum qed_coalescing_fsm {
	QED_COAL_RX_STATE_MACHINE,
	QED_COAL_TX_STATE_MACHINE
};

/**
 * @brief qed_int_cau_conf_pi - configure cau for a given
 *        status block
 *
 * @param p_hwfn
 * @param p_ptt
 * @param igu_sb_id
 * @param pi_index
 * @param state
 * @param timeset
 */
void qed_int_cau_conf_pi(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 u16 igu_sb_id,
			 u32 pi_index,
			 enum qed_coalescing_fsm coalescing_fsm,
			 u8 timeset);

/**
 * @brief qed_int_igu_enable_int - enable device interrupts
 *
 * @param p_hwfn
 * @param p_ptt
 * @param int_mode - interrupt mode to use
 */
void qed_int_igu_enable_int(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    enum qed_int_mode int_mode);

/**
 * @brief qed_int_igu_disable_int - disable device interrupts
 *
 * @param p_hwfn
 * @param p_ptt
 */
void qed_int_igu_disable_int(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt);

/**
 * @brief qed_int_igu_read_sisr_reg - Reads the single isr multiple dpc
 *        register from igu.
 *
 * @param p_hwfn
 *
 * @return u64
 */
u64 qed_int_igu_read_sisr_reg(struct qed_hwfn *p_hwfn);

#define QED_SP_SB_ID 0xffff
/**
 * @brief qed_int_sb_init - Initializes the sb_info structure.
 *
 * once the structure is initialized it can be passed to sb related functions.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param sb_info	points to an uninitialized (but
 *			allocated) sb_info structure
 * @param sb_virt_addr
 * @param sb_phy_addr
 * @param sb_id	the sb_id to be used (zero based in driver)
 *			should use QED_SP_SB_ID for SP Status block
 *
 * @return int
 */
int qed_int_sb_init(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    struct qed_sb_info *sb_info,
		    void *sb_virt_addr,
		    dma_addr_t sb_phy_addr,
		    u16 sb_id);
/**
 * @brief qed_int_sb_setup - Setup the sb.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param sb_info	initialized sb_info structure
 */
void qed_int_sb_setup(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      struct qed_sb_info *sb_info);

/**
 * @brief qed_int_sb_release - releases the sb_info structure.
 *
 * once the structure is released, it's memory can be freed
 *
 * @param p_hwfn
 * @param sb_info	points to an allocated sb_info structure
 * @param sb_id		the sb_id to be used (zero based in driver)
 *			should never be equal to QED_SP_SB_ID
 *			(SP Status block)
 *
 * @return int
 */
int qed_int_sb_release(struct qed_hwfn *p_hwfn,
		       struct qed_sb_info *sb_info,
		       u16 sb_id);

/**
 * @brief qed_int_sp_dpc - To be called when an interrupt is received on the
 *        default status block.
 *
 * @param p_hwfn - pointer to hwfn
 *
 */
void qed_int_sp_dpc(unsigned long hwfn_cookie);

/**
 * @brief qed_int_get_num_sbs - get the number of status
 *        blocks configured for this funciton in the igu.
 *
 * @param p_hwfn
 * @param p_sb_cnt_info
 *
 * @return int - number of status blocks configured
 */
void qed_int_get_num_sbs(struct qed_hwfn	*p_hwfn,
			 struct qed_sb_cnt_info *p_sb_cnt_info);

/**
 * @brief qed_int_disable_post_isr_release - performs the cleanup post ISR
 *        release. The API need to be called after releasing all slowpath IRQs
 *        of the device.
 *
 * @param cdev
 *
 */
void qed_int_disable_post_isr_release(struct qed_dev *cdev);

#define QED_CAU_DEF_RX_TIMER_RES 0
#define QED_CAU_DEF_TX_TIMER_RES 0

#define QED_SB_ATT_IDX  0x0001
#define QED_SB_EVENT_MASK       0x0003

#define SB_ALIGNED_SIZE(p_hwfn)	\
	ALIGNED_TYPE_SIZE(struct status_block, p_hwfn)

struct qed_igu_block {
	u8	status;
#define QED_IGU_STATUS_FREE     0x01
#define QED_IGU_STATUS_VALID    0x02
#define QED_IGU_STATUS_PF       0x04

	u8	vector_number;
	u8	function_id;
	u8	is_pf;
};

struct qed_igu_map {
	struct qed_igu_block igu_blocks[MAX_TOT_SB_PER_PATH];
};

struct qed_igu_info {
	struct qed_igu_map	igu_map;
	u16			igu_dsb_id;
	u16			igu_base_sb;
	u16			igu_base_sb_iov;
	u16			igu_sb_cnt;
	u16			igu_sb_cnt_iov;
	u16			free_blks;
};

/* TODO Names of function may change... */
void qed_int_igu_init_pure_rt(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      bool b_set,
			      bool b_slowpath);

void qed_int_igu_init_rt(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_int_igu_read_cam - Reads the IGU CAM.
 *	This function needs to be called during hardware
 *	prepare. It reads the info from igu cam to know which
 *	status block is the default / base status block etc.
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return int
 */
int qed_int_igu_read_cam(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt);

typedef int (*qed_int_comp_cb_t)(struct qed_hwfn *p_hwfn,
				 void *cookie);
/**
 * @brief qed_int_register_cb - Register callback func for
 *      slowhwfn statusblock.
 *
 *	Every protocol that uses the slowhwfn status block
 *	should register a callback function that will be called
 *	once there is an update of the sp status block.
 *
 * @param p_hwfn
 * @param comp_cb - function to be called when there is an
 *                  interrupt on the sp sb
 *
 * @param cookie  - passed to the callback function
 * @param sb_idx  - OUT parameter which gives the chosen index
 *                  for this protocol.
 * @param p_fw_cons  - pointer to the actual address of the
 *                     consumer for this protocol.
 *
 * @return int
 */
int qed_int_register_cb(struct qed_hwfn *p_hwfn,
			qed_int_comp_cb_t comp_cb,
			void *cookie,
			u8 *sb_idx,
			__le16 **p_fw_cons);

/**
 * @brief qed_int_unregister_cb - Unregisters callback
 *      function from sp sb.
 *      Partner of qed_int_register_cb -> should be called
 *      when no longer required.
 *
 * @param p_hwfn
 * @param pi
 *
 * @return int
 */
int qed_int_unregister_cb(struct qed_hwfn *p_hwfn,
			  u8 pi);

/**
 * @brief qed_int_get_sp_sb_id - Get the slowhwfn sb id.
 *
 * @param p_hwfn
 *
 * @return u16
 */
u16 qed_int_get_sp_sb_id(struct qed_hwfn *p_hwfn);

/**
 * @brief Status block cleanup. Should be called for each status
 *        block that will be used -> both PF / VF
 *
 * @param p_hwfn
 * @param p_ptt
 * @param sb_id		- igu status block id
 * @param opaque	- opaque fid of the sb owner.
 * @param b_set		- set(1) / clear(0)
 */
void qed_int_igu_init_pure_rt_single(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     u32 sb_id,
				     u16 opaque,
				     bool b_set);

/**
 * @brief qed_int_cau_conf - configure cau for a given status
 *        block
 *
 * @param p_hwfn
 * @param ptt
 * @param sb_phys
 * @param igu_sb_id
 * @param vf_number
 * @param vf_valid
 */
void qed_int_cau_conf_sb(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 dma_addr_t sb_phys,
			 u16 igu_sb_id,
			 u16 vf_number,
			 u8 vf_valid);

/**
 * @brief qed_int_alloc
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return int
 */
int qed_int_alloc(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt);

/**
 * @brief qed_int_free
 *
 * @param p_hwfn
 */
void qed_int_free(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_int_setup
 *
 * @param p_hwfn
 * @param p_ptt
 */
void qed_int_setup(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt);

/**
 * @brief - Returns an Rx queue index appropriate for usage with given SB.
 *
 * @param p_hwfn
 * @param sb_id - absolute index of SB
 *
 * @return index of Rx queue
 */
u16 qed_int_queue_id_from_sb_id(struct qed_hwfn *p_hwfn, u16 sb_id);

/**
 * @brief - Enable Interrupt & Attention for hw function
 *
 * @param p_hwfn
 * @param p_ptt
 * @param int_mode
 *
 * @return int
 */
int qed_int_igu_enable(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
		       enum qed_int_mode int_mode);

/**
 * @brief - Initialize CAU status block entry
 *
 * @param p_hwfn
 * @param p_sb_entry
 * @param pf_id
 * @param vf_number
 * @param vf_valid
 */
void qed_init_cau_sb_entry(struct qed_hwfn *p_hwfn,
			   struct cau_sb_entry *p_sb_entry,
			   u8 pf_id,
			   u16 vf_number,
			   u8 vf_valid);

#define QED_MAPPING_MEMORY_SIZE(dev)	(NUM_OF_SBS(dev))

#endif
