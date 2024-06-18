/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022, Linaro Ltd.
 *
 */

#ifndef _MHI_EP_INTERNAL_
#define _MHI_EP_INTERNAL_

#include <linux/bitfield.h>

#include "../common.h"

extern struct bus_type mhi_ep_bus_type;

#define MHI_REG_OFFSET				0x100
#define BHI_REG_OFFSET				0x200

/* MHI registers */
#define EP_MHIREGLEN				(MHI_REG_OFFSET + MHIREGLEN)
#define EP_MHIVER				(MHI_REG_OFFSET + MHIVER)
#define EP_MHICFG				(MHI_REG_OFFSET + MHICFG)
#define EP_CHDBOFF				(MHI_REG_OFFSET + CHDBOFF)
#define EP_ERDBOFF				(MHI_REG_OFFSET + ERDBOFF)
#define EP_BHIOFF				(MHI_REG_OFFSET + BHIOFF)
#define EP_BHIEOFF				(MHI_REG_OFFSET + BHIEOFF)
#define EP_DEBUGOFF				(MHI_REG_OFFSET + DEBUGOFF)
#define EP_MHICTRL				(MHI_REG_OFFSET + MHICTRL)
#define EP_MHISTATUS				(MHI_REG_OFFSET + MHISTATUS)
#define EP_CCABAP_LOWER				(MHI_REG_OFFSET + CCABAP_LOWER)
#define EP_CCABAP_HIGHER			(MHI_REG_OFFSET + CCABAP_HIGHER)
#define EP_ECABAP_LOWER				(MHI_REG_OFFSET + ECABAP_LOWER)
#define EP_ECABAP_HIGHER			(MHI_REG_OFFSET + ECABAP_HIGHER)
#define EP_CRCBAP_LOWER				(MHI_REG_OFFSET + CRCBAP_LOWER)
#define EP_CRCBAP_HIGHER			(MHI_REG_OFFSET + CRCBAP_HIGHER)
#define EP_CRDB_LOWER				(MHI_REG_OFFSET + CRDB_LOWER)
#define EP_CRDB_HIGHER				(MHI_REG_OFFSET + CRDB_HIGHER)
#define EP_MHICTRLBASE_LOWER			(MHI_REG_OFFSET + MHICTRLBASE_LOWER)
#define EP_MHICTRLBASE_HIGHER			(MHI_REG_OFFSET + MHICTRLBASE_HIGHER)
#define EP_MHICTRLLIMIT_LOWER			(MHI_REG_OFFSET + MHICTRLLIMIT_LOWER)
#define EP_MHICTRLLIMIT_HIGHER			(MHI_REG_OFFSET + MHICTRLLIMIT_HIGHER)
#define EP_MHIDATABASE_LOWER			(MHI_REG_OFFSET + MHIDATABASE_LOWER)
#define EP_MHIDATABASE_HIGHER			(MHI_REG_OFFSET + MHIDATABASE_HIGHER)
#define EP_MHIDATALIMIT_LOWER			(MHI_REG_OFFSET + MHIDATALIMIT_LOWER)
#define EP_MHIDATALIMIT_HIGHER			(MHI_REG_OFFSET + MHIDATALIMIT_HIGHER)

/* MHI BHI registers */
#define EP_BHI_INTVEC				(BHI_REG_OFFSET + BHI_INTVEC)
#define EP_BHI_EXECENV				(BHI_REG_OFFSET + BHI_EXECENV)

/* MHI Doorbell registers */
#define CHDB_LOWER_n(n)				(0x400 + 0x8 * (n))
#define CHDB_HIGHER_n(n)			(0x404 + 0x8 * (n))
#define ERDB_LOWER_n(n)				(0x800 + 0x8 * (n))
#define ERDB_HIGHER_n(n)			(0x804 + 0x8 * (n))

#define MHI_CTRL_INT_STATUS			0x4
#define MHI_CTRL_INT_STATUS_MSK			BIT(0)
#define MHI_CTRL_INT_STATUS_CRDB_MSK		BIT(1)
#define MHI_CHDB_INT_STATUS_n(n)		(0x28 + 0x4 * (n))
#define MHI_ERDB_INT_STATUS_n(n)		(0x38 + 0x4 * (n))

#define MHI_CTRL_INT_CLEAR			0x4c
#define MHI_CTRL_INT_MMIO_WR_CLEAR		BIT(2)
#define MHI_CTRL_INT_CRDB_CLEAR			BIT(1)
#define MHI_CTRL_INT_CRDB_MHICTRL_CLEAR		BIT(0)

#define MHI_CHDB_INT_CLEAR_n(n)			(0x70 + 0x4 * (n))
#define MHI_CHDB_INT_CLEAR_n_CLEAR_ALL		GENMASK(31, 0)
#define MHI_ERDB_INT_CLEAR_n(n)			(0x80 + 0x4 * (n))
#define MHI_ERDB_INT_CLEAR_n_CLEAR_ALL		GENMASK(31, 0)

/*
 * Unlike the usual "masking" convention, writing "1" to a bit in this register
 * enables the interrupt and writing "0" will disable it..
 */
#define MHI_CTRL_INT_MASK			0x94
#define MHI_CTRL_INT_MASK_MASK			GENMASK(1, 0)
#define MHI_CTRL_MHICTRL_MASK			BIT(0)
#define MHI_CTRL_CRDB_MASK			BIT(1)

#define MHI_CHDB_INT_MASK_n(n)			(0xb8 + 0x4 * (n))
#define MHI_CHDB_INT_MASK_n_EN_ALL		GENMASK(31, 0)
#define MHI_ERDB_INT_MASK_n(n)			(0xc8 + 0x4 * (n))
#define MHI_ERDB_INT_MASK_n_EN_ALL		GENMASK(31, 0)

#define NR_OF_CMD_RINGS				1
#define MHI_MASK_ROWS_CH_DB			4
#define MHI_MASK_ROWS_EV_DB			4
#define MHI_MASK_CH_LEN				32
#define MHI_MASK_EV_LEN				32

/* Generic context */
struct mhi_generic_ctx {
	__le32 reserved0;
	__le32 reserved1;
	__le32 reserved2;

	__le64 rbase __packed __aligned(4);
	__le64 rlen __packed __aligned(4);
	__le64 rp __packed __aligned(4);
	__le64 wp __packed __aligned(4);
};

enum mhi_ep_ring_type {
	RING_TYPE_CMD,
	RING_TYPE_ER,
	RING_TYPE_CH,
};

/* Ring element */
union mhi_ep_ring_ctx {
	struct mhi_cmd_ctxt cmd;
	struct mhi_event_ctxt ev;
	struct mhi_chan_ctxt ch;
	struct mhi_generic_ctx generic;
};

struct mhi_ep_ring_item {
	struct list_head node;
	struct mhi_ep_ring *ring;
};

struct mhi_ep_ring {
	struct mhi_ep_cntrl *mhi_cntrl;
	union mhi_ep_ring_ctx *ring_ctx;
	struct mhi_ring_element *ring_cache;
	enum mhi_ep_ring_type type;
	struct delayed_work intmodt_work;
	u64 rbase;
	size_t rd_offset;
	size_t wr_offset;
	size_t ring_size;
	u32 db_offset_h;
	u32 db_offset_l;
	u32 ch_id;
	u32 er_index;
	u32 irq_vector;
	u32 intmodt;
	bool started;
	bool irq_pending;
};

struct mhi_ep_cmd {
	struct mhi_ep_ring ring;
};

struct mhi_ep_event {
	struct mhi_ep_ring ring;
};

struct mhi_ep_state_transition {
	struct list_head node;
	enum mhi_state state;
};

struct mhi_ep_chan {
	char *name;
	struct mhi_ep_device *mhi_dev;
	struct mhi_ep_ring ring;
	struct mutex lock;
	void (*xfer_cb)(struct mhi_ep_device *mhi_dev, struct mhi_result *result);
	enum mhi_ch_state state;
	enum dma_data_direction dir;
	size_t rd_offset;
	u64 tre_loc;
	u32 tre_size;
	u32 tre_bytes_left;
	u32 chan;
	bool skip_td;
};

/* MHI Ring related functions */
void mhi_ep_ring_init(struct mhi_ep_ring *ring, enum mhi_ep_ring_type type, u32 id);
void mhi_ep_ring_reset(struct mhi_ep_cntrl *mhi_cntrl, struct mhi_ep_ring *ring);
int mhi_ep_ring_start(struct mhi_ep_cntrl *mhi_cntrl, struct mhi_ep_ring *ring,
		      union mhi_ep_ring_ctx *ctx);
size_t mhi_ep_ring_addr2offset(struct mhi_ep_ring *ring, u64 ptr);
int mhi_ep_ring_add_element(struct mhi_ep_ring *ring, struct mhi_ring_element *element);
void mhi_ep_ring_inc_index(struct mhi_ep_ring *ring);
int mhi_ep_update_wr_offset(struct mhi_ep_ring *ring);

/* MMIO related functions */
u32 mhi_ep_mmio_read(struct mhi_ep_cntrl *mhi_cntrl, u32 offset);
void mhi_ep_mmio_write(struct mhi_ep_cntrl *mhi_cntrl, u32 offset, u32 val);
void mhi_ep_mmio_masked_write(struct mhi_ep_cntrl *mhi_cntrl, u32 offset, u32 mask, u32 val);
u32 mhi_ep_mmio_masked_read(struct mhi_ep_cntrl *dev, u32 offset, u32 mask);
void mhi_ep_mmio_enable_ctrl_interrupt(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_mmio_disable_ctrl_interrupt(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_mmio_enable_cmdb_interrupt(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_mmio_disable_cmdb_interrupt(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_mmio_enable_chdb(struct mhi_ep_cntrl *mhi_cntrl, u32 ch_id);
void mhi_ep_mmio_disable_chdb(struct mhi_ep_cntrl *mhi_cntrl, u32 ch_id);
void mhi_ep_mmio_enable_chdb_interrupts(struct mhi_ep_cntrl *mhi_cntrl);
bool mhi_ep_mmio_read_chdb_status_interrupts(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_mmio_mask_interrupts(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_mmio_get_chc_base(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_mmio_get_erc_base(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_mmio_get_crc_base(struct mhi_ep_cntrl *mhi_cntrl);
u64 mhi_ep_mmio_get_db(struct mhi_ep_ring *ring);
void mhi_ep_mmio_set_env(struct mhi_ep_cntrl *mhi_cntrl, u32 value);
void mhi_ep_mmio_clear_reset(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_mmio_reset(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_mmio_get_mhi_state(struct mhi_ep_cntrl *mhi_cntrl, enum mhi_state *state,
			       bool *mhi_reset);
void mhi_ep_mmio_init(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_mmio_update_ner(struct mhi_ep_cntrl *mhi_cntrl);

/* MHI EP core functions */
int mhi_ep_send_state_change_event(struct mhi_ep_cntrl *mhi_cntrl, enum mhi_state state);
int mhi_ep_send_ee_event(struct mhi_ep_cntrl *mhi_cntrl, enum mhi_ee_type exec_env);
bool mhi_ep_check_mhi_state(struct mhi_ep_cntrl *mhi_cntrl, enum mhi_state cur_mhi_state,
			    enum mhi_state mhi_state);
int mhi_ep_set_mhi_state(struct mhi_ep_cntrl *mhi_cntrl, enum mhi_state mhi_state);
int mhi_ep_set_m0_state(struct mhi_ep_cntrl *mhi_cntrl);
int mhi_ep_set_m3_state(struct mhi_ep_cntrl *mhi_cntrl);
int mhi_ep_set_ready_state(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_handle_syserr(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_resume_channels(struct mhi_ep_cntrl *mhi_cntrl);
void mhi_ep_suspend_channels(struct mhi_ep_cntrl *mhi_cntrl);

#endif
