/* SPDX-License-Identifier: GPL-2.0 */
/*  Marvell OcteonTx2 RPM driver
 *
 * Copyright (C) 2020 Marvell.
 */

#ifndef LMAC_COMMON_H
#define LMAC_COMMON_H

#include "rvu.h"
#include "cgx.h"
/**
 * struct lmac
 * @wq_cmd_cmplt:	waitq to keep the process blocked until cmd completion
 * @cmd_lock:		Lock to serialize the command interface
 * @resp:		command response
 * @link_info:		link related information
 * @event_cb:		callback for linkchange events
 * @event_cb_lock:	lock for serializing callback with unregister
 * @cmd_pend:		flag set before new command is started
 *			flag cleared after command response is received
 * @cgx:		parent cgx port
 * @lmac_id:		lmac port id
 * @name:		lmac port name
 */
struct lmac {
	wait_queue_head_t wq_cmd_cmplt;
	/* Lock to serialize the command interface */
	struct mutex cmd_lock;
	u64 resp;
	struct cgx_link_user_info link_info;
	struct cgx_event_cb event_cb;
	/* lock for serializing callback with unregister */
	spinlock_t event_cb_lock;
	bool cmd_pend;
	struct cgx *cgx;
	u8 lmac_id;
	char *name;
};

/* CGX & RPM has different feature set
 * update the structure fields with different one
 */
struct mac_ops {
	char		       *name;
	/* Features like RXSTAT, TXSTAT, DMAC FILTER csrs differs by fixed
	 * bar offset for example
	 * CGX DMAC_CTL0  0x1f8
	 * RPM DMAC_CTL0  0x4ff8
	 */
	u64			csr_offset;
	/* For ATF to send events to kernel, there is no dedicated interrupt
	 * defined hence CGX uses OVERFLOW bit in CMR_INT. RPM block supports
	 * SW_INT so that ATF triggers this interrupt after processing of
	 * requested command
	 */
	u64			int_register;
	u64			int_set_reg;
	/* lmac offset is different is RPM */
	u8			lmac_offset;
	u8			irq_offset;
	u8			int_ena_bit;
	u8			lmac_fwi;
	u32			fifo_len;
	bool			non_contiguous_serdes_lane;
	/* RPM & CGX differs in number of Receive/transmit stats */
	u8			rx_stats_cnt;
	u8			tx_stats_cnt;
	/* Incase of RPM get number of lmacs from RPMX_CMR_RX_LMACS[LMAC_EXIST]
	 * number of setbits in lmac_exist tells number of lmacs
	 */
	int			(*get_nr_lmacs)(void *cgx);
	u8                      (*get_lmac_type)(void *cgx, int lmac_id);
	int                     (*mac_lmac_intl_lbk)(void *cgx, int lmac_id,
						     bool enable);
	/* Register Stats related functions */
	int			(*mac_get_rx_stats)(void *cgx, int lmac_id,
						    int idx, u64 *rx_stat);
	int			(*mac_get_tx_stats)(void *cgx, int lmac_id,
						    int idx, u64 *tx_stat);

	/* Enable LMAC Pause Frame Configuration */
	void			(*mac_enadis_rx_pause_fwding)(void *cgxd,
							      int lmac_id,
							      bool enable);

	int			(*mac_get_pause_frm_status)(void *cgxd,
							    int lmac_id,
							    u8 *tx_pause,
							    u8 *rx_pause);

	int			(*mac_enadis_pause_frm)(void *cgxd,
							int lmac_id,
							u8 tx_pause,
							u8 rx_pause);

	void			(*mac_pause_frm_config)(void  *cgxd,
							int lmac_id,
							bool enable);
};

struct cgx {
	void __iomem		*reg_base;
	struct pci_dev		*pdev;
	u8			cgx_id;
	u8			lmac_count;
	struct lmac		*lmac_idmap[MAX_LMAC_PER_CGX];
	struct			work_struct cgx_cmd_work;
	struct			workqueue_struct *cgx_cmd_workq;
	struct list_head	cgx_list;
	u64			hw_features;
	struct mac_ops		*mac_ops;
	unsigned long		lmac_bmap; /* bitmap of enabled lmacs */
	/* Lock to serialize read/write of global csrs like
	 * RPMX_MTI_STAT_DATA_HI_CDC etc
	 */
	struct mutex		lock;
};

typedef struct cgx rpm_t;

/* Function Declarations */
void cgx_write(struct cgx *cgx, u64 lmac, u64 offset, u64 val);
u64 cgx_read(struct cgx *cgx, u64 lmac, u64 offset);
struct lmac *lmac_pdata(u8 lmac_id, struct cgx *cgx);
int cgx_fwi_cmd_send(u64 req, u64 *resp, struct lmac *lmac);
int cgx_fwi_cmd_generic(u64 req, u64 *resp, struct cgx *cgx, int lmac_id);
bool is_lmac_valid(struct cgx *cgx, int lmac_id);
struct mac_ops *rpm_get_mac_ops(void);

#endif /* LMAC_COMMON_H */
