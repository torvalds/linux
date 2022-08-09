/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef _QED_INT_H
#define _QED_INT_H

#include <linux/types.h>
#include <linux/slab.h>
#include "qed.h"

/* Fields of IGU PF CONFIGURATION REGISTER */
#define IGU_PF_CONF_FUNC_EN       (0x1 << 0)    /* function enable        */
#define IGU_PF_CONF_MSI_MSIX_EN   (0x1 << 1)    /* MSI/MSIX enable        */
#define IGU_PF_CONF_INT_LINE_EN   (0x1 << 2)    /* INT enable             */
#define IGU_PF_CONF_ATTN_BIT_EN   (0x1 << 3)    /* attention enable       */
#define IGU_PF_CONF_SINGLE_ISR_EN (0x1 << 4)    /* single ISR mode enable */
#define IGU_PF_CONF_SIMD_MODE     (0x1 << 5)    /* simd all ones mode     */
/* Fields of IGU VF CONFIGURATION REGISTER */
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
 * qed_int_igu_enable_int(): Enable device interrupts.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @int_mode: Interrupt mode to use.
 *
 * Return: Void.
 */
void qed_int_igu_enable_int(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    enum qed_int_mode int_mode);

/**
 * qed_int_igu_disable_int():  Disable device interrupts.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Void.
 */
void qed_int_igu_disable_int(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt);

/**
 * qed_int_igu_read_sisr_reg(): Reads the single isr multiple dpc
 *                             register from igu.
 *
 * @p_hwfn: HW device data.
 *
 * Return: u64.
 */
u64 qed_int_igu_read_sisr_reg(struct qed_hwfn *p_hwfn);

#define QED_SP_SB_ID 0xffff
/**
 * qed_int_sb_init(): Initializes the sb_info structure.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @sb_info: points to an uninitialized (but allocated) sb_info structure
 * @sb_virt_addr: SB Virtual address.
 * @sb_phy_addr: SB Physial address.
 * @sb_id: the sb_id to be used (zero based in driver)
 *           should use QED_SP_SB_ID for SP Status block
 *
 * Return: int.
 *
 * Once the structure is initialized it can be passed to sb related functions.
 */
int qed_int_sb_init(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    struct qed_sb_info *sb_info,
		    void *sb_virt_addr,
		    dma_addr_t sb_phy_addr,
		    u16 sb_id);
/**
 * qed_int_sb_setup(): Setup the sb.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @sb_info: Initialized sb_info structure.
 *
 * Return: Void.
 */
void qed_int_sb_setup(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      struct qed_sb_info *sb_info);

/**
 * qed_int_sb_release(): Releases the sb_info structure.
 *
 * @p_hwfn: HW device data.
 * @sb_info: Points to an allocated sb_info structure.
 * @sb_id: The sb_id to be used (zero based in driver)
 *         should never be equal to QED_SP_SB_ID
 *         (SP Status block).
 *
 * Return: int.
 *
 * Once the structure is released, it's memory can be freed.
 */
int qed_int_sb_release(struct qed_hwfn *p_hwfn,
		       struct qed_sb_info *sb_info,
		       u16 sb_id);

/**
 * qed_int_sp_dpc(): To be called when an interrupt is received on the
 *                   default status block.
 *
 * @t: Tasklet.
 *
 * Return: Void.
 *
 */
void qed_int_sp_dpc(struct tasklet_struct *t);

/**
 * qed_int_get_num_sbs(): Get the number of status blocks configured
 *                        for this funciton in the igu.
 *
 * @p_hwfn: HW device data.
 * @p_sb_cnt_info: Pointer to SB count info.
 *
 * Return: Void.
 */
void qed_int_get_num_sbs(struct qed_hwfn	*p_hwfn,
			 struct qed_sb_cnt_info *p_sb_cnt_info);

/**
 * qed_int_disable_post_isr_release(): Performs the cleanup post ISR
 *        release. The API need to be called after releasing all slowpath IRQs
 *        of the device.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: Void.
 */
void qed_int_disable_post_isr_release(struct qed_dev *cdev);

/**
 * qed_int_attn_clr_enable: Sets whether the general behavior is
 *        preventing attentions from being reasserted, or following the
 *        attributes of the specific attention.
 *
 * @cdev: Qed dev pointer.
 * @clr_enable: Clear enable
 *
 * Return: Void.
 *
 */
void qed_int_attn_clr_enable(struct qed_dev *cdev, bool clr_enable);

/**
 * qed_db_rec_handler(): Doorbell Recovery handler.
 *          Run doorbell recovery in case of PF overflow (and flush DORQ if
 *          needed).
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Int.
 */
int qed_db_rec_handler(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

#define QED_CAU_DEF_RX_TIMER_RES 0
#define QED_CAU_DEF_TX_TIMER_RES 0

#define QED_SB_ATT_IDX  0x0001
#define QED_SB_EVENT_MASK       0x0003

#define SB_ALIGNED_SIZE(p_hwfn)	\
	ALIGNED_TYPE_SIZE(struct status_block, p_hwfn)

#define QED_SB_INVALID_IDX      0xffff

struct qed_igu_block {
	u8 status;
#define QED_IGU_STATUS_FREE     0x01
#define QED_IGU_STATUS_VALID    0x02
#define QED_IGU_STATUS_PF       0x04
#define QED_IGU_STATUS_DSB      0x08

	u8 vector_number;
	u8 function_id;
	u8 is_pf;

	/* Index inside IGU [meant for back reference] */
	u16 igu_sb_id;

	struct qed_sb_info *sb_info;
};

struct qed_igu_info {
	struct qed_igu_block entry[MAX_TOT_SB_PER_PATH];
	u16 igu_dsb_id;

	struct qed_sb_cnt_info usage;

	bool b_allow_pf_vf_change;
};

/**
 * qed_int_igu_reset_cam(): Make sure the IGU CAM reflects the resources
 *                          provided by MFW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Void.
 */
int qed_int_igu_reset_cam(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * qed_get_igu_sb_id(): Translate the weakly-defined client sb-id into
 *                      an IGU sb-id
 *
 * @p_hwfn: HW device data.
 * @sb_id: user provided sb_id.
 *
 * Return: An index inside IGU CAM where the SB resides.
 */
u16 qed_get_igu_sb_id(struct qed_hwfn *p_hwfn, u16 sb_id);

/**
 * qed_get_igu_free_sb(): Return a pointer to an unused valid SB
 *
 * @p_hwfn: HW device data.
 * @b_is_pf: True iff we want a SB belonging to a PF.
 *
 * Return: Point to an igu_block, NULL if none is available.
 */
struct qed_igu_block *qed_get_igu_free_sb(struct qed_hwfn *p_hwfn,
					  bool b_is_pf);

void qed_int_igu_init_pure_rt(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      bool b_set,
			      bool b_slowpath);

void qed_int_igu_init_rt(struct qed_hwfn *p_hwfn);

/**
 * qed_int_igu_read_cam():  Reads the IGU CAM.
 *	This function needs to be called during hardware
 *	prepare. It reads the info from igu cam to know which
 *	status block is the default / base status block etc.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Int.
 */
int qed_int_igu_read_cam(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt);

typedef int (*qed_int_comp_cb_t)(struct qed_hwfn *p_hwfn,
				 void *cookie);
/**
 * qed_int_register_cb(): Register callback func for slowhwfn statusblock.
 *
 * @p_hwfn: HW device data.
 * @comp_cb: Function to be called when there is an
 *           interrupt on the sp sb
 * @cookie: Passed to the callback function
 * @sb_idx: (OUT) parameter which gives the chosen index
 *           for this protocol.
 * @p_fw_cons: Pointer to the actual address of the
 *             consumer for this protocol.
 *
 * Return: Int.
 *
 * Every protocol that uses the slowhwfn status block
 * should register a callback function that will be called
 * once there is an update of the sp status block.
 */
int qed_int_register_cb(struct qed_hwfn *p_hwfn,
			qed_int_comp_cb_t comp_cb,
			void *cookie,
			u8 *sb_idx,
			__le16 **p_fw_cons);

/**
 * qed_int_unregister_cb(): Unregisters callback function from sp sb.
 *
 * @p_hwfn: HW device data.
 * @pi: Producer Index.
 *
 * Return: Int.
 *
 * Partner of qed_int_register_cb -> should be called
 * when no longer required.
 */
int qed_int_unregister_cb(struct qed_hwfn *p_hwfn,
			  u8 pi);

/**
 * qed_int_get_sp_sb_id(): Get the slowhwfn sb id.
 *
 * @p_hwfn: HW device data.
 *
 * Return: u16.
 */
u16 qed_int_get_sp_sb_id(struct qed_hwfn *p_hwfn);

/**
 * qed_int_igu_init_pure_rt_single(): Status block cleanup.
 *                                    Should be called for each status
 *                                    block that will be used -> both PF / VF.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @igu_sb_id: IGU status block id.
 * @opaque: Opaque fid of the sb owner.
 * @b_set: Set(1) / Clear(0).
 *
 * Return: Void.
 */
void qed_int_igu_init_pure_rt_single(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     u16 igu_sb_id,
				     u16 opaque,
				     bool b_set);

/**
 * qed_int_cau_conf_sb(): Configure cau for a given status block.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @sb_phys: SB Physical.
 * @igu_sb_id: IGU status block id.
 * @vf_number: VF number
 * @vf_valid: VF valid or not.
 *
 * Return: Void.
 */
void qed_int_cau_conf_sb(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 dma_addr_t sb_phys,
			 u16 igu_sb_id,
			 u16 vf_number,
			 u8 vf_valid);

/**
 * qed_int_alloc(): QED interrupt alloc.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Int.
 */
int qed_int_alloc(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt);

/**
 * qed_int_free(): QED interrupt free.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Void.
 */
void qed_int_free(struct qed_hwfn *p_hwfn);

/**
 * qed_int_setup(): QED interrupt setup.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Void.
 */
void qed_int_setup(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt);

/**
 * qed_int_igu_enable(): Enable Interrupt & Attention for hw function.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @int_mode: Interrut mode
 *
 * Return: Int.
 */
int qed_int_igu_enable(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
		       enum qed_int_mode int_mode);

/**
 * qed_init_cau_sb_entry(): Initialize CAU status block entry.
 *
 * @p_hwfn: HW device data.
 * @p_sb_entry: Pointer SB entry.
 * @pf_id: PF number
 * @vf_number: VF number
 * @vf_valid: VF valid or not.
 *
 * Return: Void.
 */
void qed_init_cau_sb_entry(struct qed_hwfn *p_hwfn,
			   struct cau_sb_entry *p_sb_entry,
			   u8 pf_id,
			   u16 vf_number,
			   u8 vf_valid);

int qed_int_set_timer_res(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			  u8 timer_res, u16 sb_id, bool tx);

#define QED_MAPPING_MEMORY_SIZE(dev)	(NUM_OF_SBS(dev))

int qed_pglueb_rbc_attn_handler(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
				bool hw_init);

#endif
