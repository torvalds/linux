/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2012 Chelsio Communications, Inc. All rights reserved.
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

#ifndef __CSIO_HW_H__
#define __CSIO_HW_H__

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/compiler.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/mempool.h>
#include <linux/io.h>
#include <linux/spinlock_types.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport_fc.h>

#include "t4_hw.h"
#include "csio_hw_chip.h"
#include "csio_wr.h"
#include "csio_mb.h"
#include "csio_scsi.h"
#include "csio_defs.h"
#include "t4_regs.h"
#include "t4_msg.h"

/*
 * An error value used by host. Should not clash with FW defined return values.
 */
#define	FW_HOSTERROR			255

#define CSIO_HW_NAME		"Chelsio FCoE Adapter"
#define CSIO_MAX_PFN		8
#define CSIO_MAX_PPORTS		4

#define CSIO_MAX_LUN		0xFFFF
#define CSIO_MAX_QUEUE		2048
#define CSIO_MAX_CMD_PER_LUN	32
#define CSIO_MAX_DDP_BUF_SIZE	(1024 * 1024)
#define CSIO_MAX_SECTOR_SIZE	128

/* Interrupts */
#define CSIO_EXTRA_MSI_IQS	2	/* Extra iqs for INTX/MSI mode
					 * (Forward intr iq + fw iq) */
#define CSIO_EXTRA_VECS		2	/* non-data + FW evt */
#define CSIO_MAX_SCSI_CPU	128
#define CSIO_MAX_SCSI_QSETS	(CSIO_MAX_SCSI_CPU * CSIO_MAX_PPORTS)
#define CSIO_MAX_MSIX_VECS	(CSIO_MAX_SCSI_QSETS + CSIO_EXTRA_VECS)

/* Queues */
enum {
	CSIO_INTR_WRSIZE = 128,
	CSIO_INTR_IQSIZE = ((CSIO_MAX_MSIX_VECS + 1) * CSIO_INTR_WRSIZE),
	CSIO_FWEVT_WRSIZE = 128,
	CSIO_FWEVT_IQLEN = 128,
	CSIO_FWEVT_FLBUFS = 64,
	CSIO_FWEVT_IQSIZE = (CSIO_FWEVT_WRSIZE * CSIO_FWEVT_IQLEN),
	CSIO_HW_NIQ = 1,
	CSIO_HW_NFLQ = 1,
	CSIO_HW_NEQ = 1,
	CSIO_HW_NINTXQ = 1,
};

struct csio_msix_entries {
	unsigned short	vector;		/* Assigned MSI-X vector */
	void		*dev_id;	/* Priv object associated w/ this msix*/
	char		desc[24];	/* Description of this vector */
};

struct csio_scsi_qset {
	int		iq_idx;		/* Ingress index */
	int		eq_idx;		/* Egress index */
	uint32_t	intr_idx;	/* MSIX Vector index */
};

struct csio_scsi_cpu_info {
	int16_t	max_cpus;
};

extern int csio_dbg_level;
extern unsigned int csio_port_mask;
extern int csio_msi;

#define CSIO_VENDOR_ID				0x1425
#define CSIO_ASIC_DEVID_PROTO_MASK		0xFF00
#define CSIO_ASIC_DEVID_TYPE_MASK		0x00FF

#define CSIO_GLBL_INTR_MASK	(CIM_F | MPS_F | PL_F | PCIE_F | MC_F | \
				 EDC0_F | EDC1_F | LE_F | TP_F | MA_F | \
				 PM_TX_F | PM_RX_F | ULP_RX_F | \
				 CPL_SWITCH_F | SGE_F | ULP_TX_F | SF_F)

/*
 * Hard parameters used to initialize the card in the absence of a
 * configuration file.
 */
enum {
	/* General */
	CSIO_SGE_DBFIFO_INT_THRESH	= 10,

	CSIO_SGE_RX_DMA_OFFSET		= 2,

	CSIO_SGE_FLBUF_SIZE1		= 65536,
	CSIO_SGE_FLBUF_SIZE2		= 1536,
	CSIO_SGE_FLBUF_SIZE3		= 9024,
	CSIO_SGE_FLBUF_SIZE4		= 9216,
	CSIO_SGE_FLBUF_SIZE5		= 2048,
	CSIO_SGE_FLBUF_SIZE6		= 128,
	CSIO_SGE_FLBUF_SIZE7		= 8192,
	CSIO_SGE_FLBUF_SIZE8		= 16384,

	CSIO_SGE_TIMER_VAL_0		= 5,
	CSIO_SGE_TIMER_VAL_1		= 10,
	CSIO_SGE_TIMER_VAL_2		= 20,
	CSIO_SGE_TIMER_VAL_3		= 50,
	CSIO_SGE_TIMER_VAL_4		= 100,
	CSIO_SGE_TIMER_VAL_5		= 200,

	CSIO_SGE_INT_CNT_VAL_0		= 1,
	CSIO_SGE_INT_CNT_VAL_1		= 4,
	CSIO_SGE_INT_CNT_VAL_2		= 8,
	CSIO_SGE_INT_CNT_VAL_3		= 16,
};

/* Slowpath events */
enum csio_evt {
	CSIO_EVT_FW  = 0,	/* FW event */
	CSIO_EVT_MBX,		/* MBX event */
	CSIO_EVT_SCN,		/* State change notification */
	CSIO_EVT_DEV_LOSS,	/* Device loss event */
	CSIO_EVT_MAX,		/* Max supported event */
};

#define CSIO_EVT_MSG_SIZE	512
#define CSIO_EVTQ_SIZE		512

/* Event msg  */
struct csio_evt_msg {
	struct list_head	list;	/* evt queue*/
	enum csio_evt		type;
	uint8_t			data[CSIO_EVT_MSG_SIZE];
};

enum {
	SERNUM_LEN     = 16,    /* Serial # length */
	EC_LEN         = 16,    /* E/C length */
	ID_LEN         = 16,    /* ID length */
};

enum {
	SF_SIZE = SF_SEC_SIZE * 16,   /* serial flash size */
};

/* serial flash and firmware constants */
enum {
	SF_ATTEMPTS = 10,             /* max retries for SF operations */

	/* flash command opcodes */
	SF_PROG_PAGE    = 2,          /* program page */
	SF_WR_DISABLE   = 4,          /* disable writes */
	SF_RD_STATUS    = 5,          /* read status register */
	SF_WR_ENABLE    = 6,          /* enable writes */
	SF_RD_DATA_FAST = 0xb,        /* read flash */
	SF_RD_ID	= 0x9f,	      /* read ID */
	SF_ERASE_SECTOR = 0xd8,       /* erase sector */
};

/* Management module */
enum {
	CSIO_MGMT_EQ_WRSIZE = 512,
	CSIO_MGMT_IQ_WRSIZE = 128,
	CSIO_MGMT_EQLEN = 64,
	CSIO_MGMT_IQLEN = 64,
};

#define CSIO_MGMT_EQSIZE	(CSIO_MGMT_EQLEN * CSIO_MGMT_EQ_WRSIZE)
#define CSIO_MGMT_IQSIZE	(CSIO_MGMT_IQLEN * CSIO_MGMT_IQ_WRSIZE)

/* mgmt module stats */
struct csio_mgmtm_stats {
	uint32_t	n_abort_req;		/* Total abort request */
	uint32_t	n_abort_rsp;		/* Total abort response */
	uint32_t	n_close_req;		/* Total close request */
	uint32_t	n_close_rsp;		/* Total close response */
	uint32_t	n_err;			/* Total Errors */
	uint32_t	n_drop;			/* Total request dropped */
	uint32_t	n_active;		/* Count of active_q */
	uint32_t	n_cbfn;			/* Count of cbfn_q */
};

/* MGMT module */
struct csio_mgmtm {
	struct	csio_hw		*hw;		/* Pointer to HW moduel */
	int			eq_idx;		/* Egress queue index */
	int			iq_idx;		/* Ingress queue index */
	int			msi_vec;	/* MSI vector */
	struct list_head	active_q;	/* Outstanding ELS/CT */
	struct list_head	abort_q;	/* Outstanding abort req */
	struct list_head	cbfn_q;		/* Completion queue */
	struct list_head	mgmt_req_freelist; /* Free poll of reqs */
						/* ELSCT request freelist*/
	struct timer_list	mgmt_timer;	/* MGMT timer */
	struct csio_mgmtm_stats stats;		/* ELS/CT stats */
};

struct csio_adap_desc {
	char model_no[16];
	char description[32];
};

struct pci_params {
	uint16_t   vendor_id;
	uint16_t   device_id;
	int        vpd_cap_addr;
	uint16_t   speed;
	uint8_t    width;
};

/* User configurable hw parameters */
struct csio_hw_params {
	uint32_t		sf_size;		/* serial flash
							 * size in bytes
							 */
	uint32_t		sf_nsec;		/* # of flash sectors */
	struct pci_params	pci;
	uint32_t		log_level;		/* Module-level for
							 * debug log.
							 */
};

struct csio_vpd {
	uint32_t cclk;
	uint8_t ec[EC_LEN + 1];
	uint8_t sn[SERNUM_LEN + 1];
	uint8_t id[ID_LEN + 1];
};

struct csio_pport {
	uint16_t	pcap;
	uint8_t		portid;
	uint8_t		link_status;
	uint16_t	link_speed;
	uint8_t		mac[6];
	uint8_t		mod_type;
	uint8_t		rsvd1;
	uint8_t		rsvd2;
	uint8_t		rsvd3;
};

/* fcoe resource information */
struct csio_fcoe_res_info {
	uint16_t	e_d_tov;
	uint16_t	r_a_tov_seq;
	uint16_t	r_a_tov_els;
	uint16_t	r_r_tov;
	uint32_t	max_xchgs;
	uint32_t	max_ssns;
	uint32_t	used_xchgs;
	uint32_t	used_ssns;
	uint32_t	max_fcfs;
	uint32_t	max_vnps;
	uint32_t	used_fcfs;
	uint32_t	used_vnps;
};

/* HW State machine Events */
enum csio_hw_ev {
	CSIO_HWE_CFG = (uint32_t)1, /* Starts off the State machine */
	CSIO_HWE_INIT,	         /* Config done, start Init      */
	CSIO_HWE_INIT_DONE,      /* Init Mailboxes sent, HW ready */
	CSIO_HWE_FATAL,		 /* Fatal error during initialization */
	CSIO_HWE_PCIERR_DETECTED,/* PCI error recovery detetced */
	CSIO_HWE_PCIERR_SLOT_RESET, /* Slot reset after PCI recoviery */
	CSIO_HWE_PCIERR_RESUME,  /* Resume after PCI error recovery */
	CSIO_HWE_QUIESCED,	 /* HBA quiesced */
	CSIO_HWE_HBA_RESET,      /* HBA reset requested */
	CSIO_HWE_HBA_RESET_DONE, /* HBA reset completed */
	CSIO_HWE_FW_DLOAD,       /* FW download requested */
	CSIO_HWE_PCI_REMOVE,     /* PCI de-instantiation */
	CSIO_HWE_SUSPEND,        /* HW suspend for Online(hot) replacement */
	CSIO_HWE_RESUME,         /* HW resume for Online(hot) replacement */
	CSIO_HWE_MAX,		 /* Max HW event */
};

/* hw stats */
struct csio_hw_stats {
	uint32_t	n_evt_activeq;	/* Number of event in active Q */
	uint32_t	n_evt_freeq;	/* Number of event in free Q */
	uint32_t	n_evt_drop;	/* Number of event droped */
	uint32_t	n_evt_unexp;	/* Number of unexpected events */
	uint32_t	n_pcich_offline;/* Number of pci channel offline */
	uint32_t	n_lnlkup_miss;  /* Number of lnode lookup miss */
	uint32_t	n_cpl_fw6_msg;	/* Number of cpl fw6 message*/
	uint32_t	n_cpl_fw6_pld;	/* Number of cpl fw6 payload*/
	uint32_t	n_cpl_unexp;	/* Number of unexpected cpl */
	uint32_t	n_mbint_unexp;	/* Number of unexpected mbox */
					/* interrupt */
	uint32_t	n_plint_unexp;	/* Number of unexpected PL */
					/* interrupt */
	uint32_t	n_plint_cnt;	/* Number of PL interrupt */
	uint32_t	n_int_stray;	/* Number of stray interrupt */
	uint32_t	n_err;		/* Number of hw errors */
	uint32_t	n_err_fatal;	/* Number of fatal errors */
	uint32_t	n_err_nomem;	/* Number of memory alloc failure */
	uint32_t	n_err_io;	/* Number of IO failure */
	enum csio_hw_ev	n_evt_sm[CSIO_HWE_MAX];	/* Number of sm events */
	uint64_t	n_reset_start;  /* Start time after the reset */
	uint32_t	rsvd1;
};

/* Defines for hw->flags */
#define CSIO_HWF_MASTER			0x00000001	/* This is the Master
							 * function for the
							 * card.
							 */
#define	CSIO_HWF_HW_INTR_ENABLED	0x00000002	/* Are HW Interrupt
							 * enable bit set?
							 */
#define	CSIO_HWF_FWEVT_PENDING		0x00000004	/* FW events pending */
#define	CSIO_HWF_Q_MEM_ALLOCED		0x00000008	/* Queues have been
							 * allocated memory.
							 */
#define	CSIO_HWF_Q_FW_ALLOCED		0x00000010	/* Queues have been
							 * allocated in FW.
							 */
#define CSIO_HWF_VPD_VALID		0x00000020	/* Valid VPD copied */
#define CSIO_HWF_DEVID_CACHED		0X00000040	/* PCI vendor & device
							 * id cached */
#define	CSIO_HWF_FWEVT_STOP		0x00000080	/* Stop processing
							 * FW events
							 */
#define CSIO_HWF_USING_SOFT_PARAMS	0x00000100      /* Using FW config
							 * params
							 */
#define	CSIO_HWF_HOST_INTR_ENABLED	0x00000200	/* Are host interrupts
							 * enabled?
							 */

#define csio_is_hw_intr_enabled(__hw)	\
				((__hw)->flags & CSIO_HWF_HW_INTR_ENABLED)
#define csio_is_host_intr_enabled(__hw)	\
				((__hw)->flags & CSIO_HWF_HOST_INTR_ENABLED)
#define csio_is_hw_master(__hw)		((__hw)->flags & CSIO_HWF_MASTER)
#define csio_is_valid_vpd(__hw)		((__hw)->flags & CSIO_HWF_VPD_VALID)
#define csio_is_dev_id_cached(__hw)	((__hw)->flags & CSIO_HWF_DEVID_CACHED)
#define csio_valid_vpd_copied(__hw)	((__hw)->flags |= CSIO_HWF_VPD_VALID)
#define csio_dev_id_cached(__hw)	((__hw)->flags |= CSIO_HWF_DEVID_CACHED)

/* Defines for intr_mode */
enum csio_intr_mode {
	CSIO_IM_NONE = 0,
	CSIO_IM_INTX = 1,
	CSIO_IM_MSI  = 2,
	CSIO_IM_MSIX = 3,
};

/* Master HW structure: One per function */
struct csio_hw {
	struct csio_sm		sm;			/* State machine: should
							 * be the 1st member.
							 */
	spinlock_t		lock;			/* Lock for hw */

	struct csio_scsim	scsim;			/* SCSI module*/
	struct csio_wrm		wrm;			/* Work request module*/
	struct pci_dev		*pdev;			/* PCI device */

	void __iomem		*regstart;		/* Virtual address of
							 * register map
							 */
	/* SCSI queue sets */
	uint32_t		num_sqsets;		/* Number of SCSI
							 * queue sets */
	uint32_t		num_scsi_msix_cpus;	/* Number of CPUs that
							 * will be used
							 * for ingress
							 * processing.
							 */

	struct csio_scsi_qset	sqset[CSIO_MAX_PPORTS][CSIO_MAX_SCSI_CPU];
	struct csio_scsi_cpu_info scsi_cpu_info[CSIO_MAX_PPORTS];

	uint32_t		evtflag;		/* Event flag  */
	uint32_t		flags;			/* HW flags */

	struct csio_mgmtm	mgmtm;			/* management module */
	struct csio_mbm		mbm;			/* Mailbox module */

	/* Lnodes */
	uint32_t		num_lns;		/* Number of lnodes */
	struct csio_lnode	*rln;			/* Root lnode */
	struct list_head	sln_head;		/* Sibling node list
							 * list
							 */
	int			intr_iq_idx;		/* Forward interrupt
							 * queue.
							 */
	int			fwevt_iq_idx;		/* FW evt queue */
	struct work_struct	evtq_work;		/* Worker thread for
							 * HW events.
							 */
	struct list_head	evt_free_q;		/* freelist of evt
							 * elements
							 */
	struct list_head	evt_active_q;		/* active evt queue*/

	/* board related info */
	char			name[32];
	char			hw_ver[16];
	char			model_desc[32];
	char			drv_version[32];
	char			fwrev_str[32];
	uint32_t		optrom_ver;
	uint32_t		fwrev;
	uint32_t		tp_vers;
	char			chip_ver;
	uint16_t		chip_id;		/* Tells T4/T5 chip */
	enum csio_dev_state	fw_state;
	struct csio_vpd		vpd;

	uint8_t			pfn;			/* Physical Function
							 * number
							 */
	uint32_t		port_vec;		/* Port vector */
	uint8_t			num_pports;		/* Number of physical
							 * ports.
							 */
	uint8_t			rst_retries;		/* Reset retries */
	uint8_t			cur_evt;		/* current s/m evt */
	uint8_t			prev_evt;		/* Previous s/m evt */
	uint32_t		dev_num;		/* device number */
	struct csio_pport	pport[CSIO_MAX_PPORTS];	/* Ports (XGMACs) */
	struct csio_hw_params	params;			/* Hw parameters */

	struct pci_pool		*scsi_pci_pool;		/* PCI pool for SCSI */
	mempool_t		*mb_mempool;		/* Mailbox memory pool*/
	mempool_t		*rnode_mempool;		/* rnode memory pool */

	/* Interrupt */
	enum csio_intr_mode	intr_mode;		/* INTx, MSI, MSIX */
	uint32_t		fwevt_intr_idx;		/* FW evt MSIX/interrupt
							 * index
							 */
	uint32_t		nondata_intr_idx;	/* nondata MSIX/intr
							 * idx
							 */

	uint8_t			cfg_neq;		/* FW configured no of
							 * egress queues
							 */
	uint8_t			cfg_niq;		/* FW configured no of
							 * iq queues.
							 */

	struct csio_fcoe_res_info  fres_info;		/* Fcoe resource info */
	struct csio_hw_chip_ops	*chip_ops;		/* T4/T5 Chip specific
							 * Operations
							 */

	/* MSIX vectors */
	struct csio_msix_entries msix_entries[CSIO_MAX_MSIX_VECS];

	struct dentry		*debugfs_root;		/* Debug FS */
	struct csio_hw_stats	stats;			/* Hw statistics */
};

/* Register access macros */
#define csio_reg(_b, _r)		((_b) + (_r))

#define	csio_rd_reg8(_h, _r)		readb(csio_reg((_h)->regstart, (_r)))
#define	csio_rd_reg16(_h, _r)		readw(csio_reg((_h)->regstart, (_r)))
#define	csio_rd_reg32(_h, _r)		readl(csio_reg((_h)->regstart, (_r)))
#define	csio_rd_reg64(_h, _r)		readq(csio_reg((_h)->regstart, (_r)))

#define	csio_wr_reg8(_h, _v, _r)	writeb((_v), \
						csio_reg((_h)->regstart, (_r)))
#define	csio_wr_reg16(_h, _v, _r)	writew((_v), \
						csio_reg((_h)->regstart, (_r)))
#define	csio_wr_reg32(_h, _v, _r)	writel((_v), \
						csio_reg((_h)->regstart, (_r)))
#define	csio_wr_reg64(_h, _v, _r)	writeq((_v), \
						csio_reg((_h)->regstart, (_r)))

void csio_set_reg_field(struct csio_hw *, uint32_t, uint32_t, uint32_t);

/* Core clocks <==> uSecs */
static inline uint32_t
csio_core_ticks_to_us(struct csio_hw *hw, uint32_t ticks)
{
	/* add Core Clock / 2 to round ticks to nearest uS */
	return (ticks * 1000 + hw->vpd.cclk/2) / hw->vpd.cclk;
}

static inline uint32_t
csio_us_to_core_ticks(struct csio_hw *hw, uint32_t us)
{
	return (us * hw->vpd.cclk) / 1000;
}

/* Easy access macros */
#define csio_hw_to_wrm(hw)		((struct csio_wrm *)(&(hw)->wrm))
#define csio_hw_to_mbm(hw)		((struct csio_mbm *)(&(hw)->mbm))
#define csio_hw_to_scsim(hw)		((struct csio_scsim *)(&(hw)->scsim))
#define csio_hw_to_mgmtm(hw)		((struct csio_mgmtm *)(&(hw)->mgmtm))

#define CSIO_PCI_BUS(hw)		((hw)->pdev->bus->number)
#define CSIO_PCI_DEV(hw)		(PCI_SLOT((hw)->pdev->devfn))
#define CSIO_PCI_FUNC(hw)		(PCI_FUNC((hw)->pdev->devfn))

#define csio_set_fwevt_intr_idx(_h, _i)		((_h)->fwevt_intr_idx = (_i))
#define csio_get_fwevt_intr_idx(_h)		((_h)->fwevt_intr_idx)
#define csio_set_nondata_intr_idx(_h, _i)	((_h)->nondata_intr_idx = (_i))
#define csio_get_nondata_intr_idx(_h)		((_h)->nondata_intr_idx)

/* Printing/logging */
#define CSIO_DEVID(__dev)		((__dev)->dev_num)
#define CSIO_DEVID_LO(__dev)		(CSIO_DEVID((__dev)) & 0xFFFF)
#define CSIO_DEVID_HI(__dev)		((CSIO_DEVID((__dev)) >> 16) & 0xFFFF)

#define csio_info(__hw, __fmt, ...)					\
			dev_info(&(__hw)->pdev->dev, __fmt, ##__VA_ARGS__)

#define csio_fatal(__hw, __fmt, ...)					\
			dev_crit(&(__hw)->pdev->dev, __fmt, ##__VA_ARGS__)

#define csio_err(__hw, __fmt, ...)					\
			dev_err(&(__hw)->pdev->dev, __fmt, ##__VA_ARGS__)

#define csio_warn(__hw, __fmt, ...)					\
			dev_warn(&(__hw)->pdev->dev, __fmt, ##__VA_ARGS__)

#ifdef __CSIO_DEBUG__
#define csio_dbg(__hw, __fmt, ...)					\
			csio_info((__hw), __fmt, ##__VA_ARGS__);
#else
#define csio_dbg(__hw, __fmt, ...)
#endif

int csio_hw_wait_op_done_val(struct csio_hw *, int, uint32_t, int,
			     int, int, uint32_t *);
void csio_hw_tp_wr_bits_indirect(struct csio_hw *, unsigned int,
				 unsigned int, unsigned int);
int csio_mgmt_req_lookup(struct csio_mgmtm *, struct csio_ioreq *);
void csio_hw_intr_disable(struct csio_hw *);
int csio_hw_slow_intr_handler(struct csio_hw *);
int csio_handle_intr_status(struct csio_hw *, unsigned int,
			    const struct intr_info *);

int csio_hw_start(struct csio_hw *);
int csio_hw_stop(struct csio_hw *);
int csio_hw_reset(struct csio_hw *);
int csio_is_hw_ready(struct csio_hw *);
int csio_is_hw_removing(struct csio_hw *);

int csio_fwevtq_handler(struct csio_hw *);
void csio_evtq_worker(struct work_struct *);
int csio_enqueue_evt(struct csio_hw *, enum csio_evt, void *, uint16_t);
void csio_evtq_flush(struct csio_hw *hw);

int csio_request_irqs(struct csio_hw *);
void csio_intr_enable(struct csio_hw *);
void csio_intr_disable(struct csio_hw *, bool);
void csio_hw_fatal_err(struct csio_hw *);

struct csio_lnode *csio_lnode_alloc(struct csio_hw *);
int csio_config_queues(struct csio_hw *);

int csio_hw_init(struct csio_hw *);
void csio_hw_exit(struct csio_hw *);
#endif /* ifndef __CSIO_HW_H__ */
