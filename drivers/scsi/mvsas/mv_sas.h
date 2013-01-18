/*
 * Marvell 88SE64xx/88SE94xx main function head file
 *
 * Copyright 2007 Red Hat, Inc.
 * Copyright 2008 Marvell. <kewei@marvell.com>
 * Copyright 2009-2011 Marvell. <yuxiangl@marvell.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
*/

#ifndef _MV_SAS_H_
#define _MV_SAS_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <scsi/libsas.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <scsi/sas_ata.h>
#include "mv_defs.h"

#define DRV_NAME		"mvsas"
#define DRV_VERSION		"0.8.16"
#define MVS_ID_NOT_MAPPED	0x7f
#define WIDE_PORT_MAX_PHY		4
#define mv_printk(fmt, arg ...)	\
	printk(KERN_DEBUG"%s %d:" fmt, __FILE__, __LINE__, ## arg)
#ifdef MV_DEBUG
#define mv_dprintk(format, arg...)	\
	printk(KERN_DEBUG"%s %d:" format, __FILE__, __LINE__, ## arg)
#else
#define mv_dprintk(format, arg...)
#endif
#define MV_MAX_U32			0xffffffff

extern int interrupt_coalescing;
extern struct mvs_tgt_initiator mvs_tgt;
extern struct mvs_info *tgt_mvi;
extern const struct mvs_dispatch mvs_64xx_dispatch;
extern const struct mvs_dispatch mvs_94xx_dispatch;
extern struct kmem_cache *mvs_task_list_cache;

#define DEV_IS_EXPANDER(type)	\
	((type == EDGE_DEV) || (type == FANOUT_DEV))

#define bit(n) ((u64)1 << n)

#define for_each_phy(__lseq_mask, __mc, __lseq)			\
	for ((__mc) = (__lseq_mask), (__lseq) = 0;		\
					(__mc) != 0 ;		\
					(++__lseq), (__mc) >>= 1)

#define MV_INIT_DELAYED_WORK(w, f, d)	INIT_DELAYED_WORK(w, f)
#define UNASSOC_D2H_FIS(id)		\
	((void *) mvi->rx_fis + 0x100 * id)
#define SATA_RECEIVED_FIS_LIST(reg_set)	\
	((void *) mvi->rx_fis + mvi->chip->fis_offs + 0x100 * reg_set)
#define SATA_RECEIVED_SDB_FIS(reg_set)	\
	(SATA_RECEIVED_FIS_LIST(reg_set) + 0x58)
#define SATA_RECEIVED_D2H_FIS(reg_set)	\
	(SATA_RECEIVED_FIS_LIST(reg_set) + 0x40)
#define SATA_RECEIVED_PIO_FIS(reg_set)	\
	(SATA_RECEIVED_FIS_LIST(reg_set) + 0x20)
#define SATA_RECEIVED_DMA_FIS(reg_set)	\
	(SATA_RECEIVED_FIS_LIST(reg_set) + 0x00)

enum dev_status {
	MVS_DEV_NORMAL = 0x0,
	MVS_DEV_EH	= 0x1,
};

enum dev_reset {
	MVS_SOFT_RESET	= 0,
	MVS_HARD_RESET	= 1,
	MVS_PHY_TUNE	= 2,
};

struct mvs_info;

struct mvs_dispatch {
	char *name;
	int (*chip_init)(struct mvs_info *mvi);
	int (*spi_init)(struct mvs_info *mvi);
	int (*chip_ioremap)(struct mvs_info *mvi);
	void (*chip_iounmap)(struct mvs_info *mvi);
	irqreturn_t (*isr)(struct mvs_info *mvi, int irq, u32 stat);
	u32 (*isr_status)(struct mvs_info *mvi, int irq);
	void (*interrupt_enable)(struct mvs_info *mvi);
	void (*interrupt_disable)(struct mvs_info *mvi);

	u32 (*read_phy_ctl)(struct mvs_info *mvi, u32 port);
	void (*write_phy_ctl)(struct mvs_info *mvi, u32 port, u32 val);

	u32 (*read_port_cfg_data)(struct mvs_info *mvi, u32 port);
	void (*write_port_cfg_data)(struct mvs_info *mvi, u32 port, u32 val);
	void (*write_port_cfg_addr)(struct mvs_info *mvi, u32 port, u32 addr);

	u32 (*read_port_vsr_data)(struct mvs_info *mvi, u32 port);
	void (*write_port_vsr_data)(struct mvs_info *mvi, u32 port, u32 val);
	void (*write_port_vsr_addr)(struct mvs_info *mvi, u32 port, u32 addr);

	u32 (*read_port_irq_stat)(struct mvs_info *mvi, u32 port);
	void (*write_port_irq_stat)(struct mvs_info *mvi, u32 port, u32 val);

	u32 (*read_port_irq_mask)(struct mvs_info *mvi, u32 port);
	void (*write_port_irq_mask)(struct mvs_info *mvi, u32 port, u32 val);

	void (*command_active)(struct mvs_info *mvi, u32 slot_idx);
	void (*clear_srs_irq)(struct mvs_info *mvi, u8 reg_set, u8 clear_all);
	void (*issue_stop)(struct mvs_info *mvi, enum mvs_port_type type,
				u32 tfs);
	void (*start_delivery)(struct mvs_info *mvi, u32 tx);
	u32 (*rx_update)(struct mvs_info *mvi);
	void (*int_full)(struct mvs_info *mvi);
	u8 (*assign_reg_set)(struct mvs_info *mvi, u8 *tfs);
	void (*free_reg_set)(struct mvs_info *mvi, u8 *tfs);
	u32 (*prd_size)(void);
	u32 (*prd_count)(void);
	void (*make_prd)(struct scatterlist *scatter, int nr, void *prd);
	void (*detect_porttype)(struct mvs_info *mvi, int i);
	int (*oob_done)(struct mvs_info *mvi, int i);
	void (*fix_phy_info)(struct mvs_info *mvi, int i,
				struct sas_identify_frame *id);
	void (*phy_work_around)(struct mvs_info *mvi, int i);
	void (*phy_set_link_rate)(struct mvs_info *mvi, u32 phy_id,
				struct sas_phy_linkrates *rates);
	u32 (*phy_max_link_rate)(void);
	void (*phy_disable)(struct mvs_info *mvi, u32 phy_id);
	void (*phy_enable)(struct mvs_info *mvi, u32 phy_id);
	void (*phy_reset)(struct mvs_info *mvi, u32 phy_id, int hard);
	void (*stp_reset)(struct mvs_info *mvi, u32 phy_id);
	void (*clear_active_cmds)(struct mvs_info *mvi);
	u32 (*spi_read_data)(struct mvs_info *mvi);
	void (*spi_write_data)(struct mvs_info *mvi, u32 data);
	int (*spi_buildcmd)(struct mvs_info *mvi,
						u32      *dwCmd,
						u8       cmd,
						u8       read,
						u8       length,
						u32      addr
						);
	int (*spi_issuecmd)(struct mvs_info *mvi, u32 cmd);
	int (*spi_waitdataready)(struct mvs_info *mvi, u32 timeout);
	void (*dma_fix)(struct mvs_info *mvi, u32 phy_mask,
				int buf_len, int from, void *prd);
	void (*tune_interrupt)(struct mvs_info *mvi, u32 time);
	void (*non_spec_ncq_error)(struct mvs_info *mvi);

};

struct mvs_chip_info {
	u32 		n_host;
	u32 		n_phy;
	u32 		fis_offs;
	u32 		fis_count;
	u32 		srs_sz;
	u32		sg_width;
	u32 		slot_width;
	const struct mvs_dispatch *dispatch;
};
#define MVS_MAX_SG		(1U << mvi->chip->sg_width)
#define MVS_CHIP_SLOT_SZ	(1U << mvi->chip->slot_width)
#define MVS_RX_FISL_SZ		\
	(mvi->chip->fis_offs + (mvi->chip->fis_count * 0x100))
#define MVS_CHIP_DISP		(mvi->chip->dispatch)

struct mvs_err_info {
	__le32			flags;
	__le32			flags2;
};

struct mvs_cmd_hdr {
	__le32			flags;	/* PRD tbl len; SAS, SATA ctl */
	__le32			lens;	/* cmd, max resp frame len */
	__le32			tags;	/* targ port xfer tag; tag */
	__le32			data_len;	/* data xfer len */
	__le64			cmd_tbl;  	/* command table address */
	__le64			open_frame;	/* open addr frame address */
	__le64			status_buf;	/* status buffer address */
	__le64			prd_tbl;		/* PRD tbl address */
	__le32			reserved[4];
};

struct mvs_port {
	struct asd_sas_port	sas_port;
	u8			port_attached;
	u8			wide_port_phymap;
	struct list_head	list;
};

struct mvs_phy {
	struct mvs_info 		*mvi;
	struct mvs_port		*port;
	struct asd_sas_phy	sas_phy;
	struct sas_identify	identify;
	struct scsi_device	*sdev;
	struct timer_list timer;
	u64		dev_sas_addr;
	u64		att_dev_sas_addr;
	u32		att_dev_info;
	u32		dev_info;
	u32		phy_type;
	u32		phy_status;
	u32		irq_status;
	u32		frame_rcvd_size;
	u8		frame_rcvd[32];
	u8		phy_attached;
	u8		phy_mode;
	u8		reserved[2];
	u32		phy_event;
	enum sas_linkrate	minimum_linkrate;
	enum sas_linkrate	maximum_linkrate;
};

struct mvs_device {
	struct list_head		dev_entry;
	enum sas_dev_type dev_type;
	struct mvs_info *mvi_info;
	struct domain_device *sas_device;
	struct timer_list timer;
	u32 attached_phy;
	u32 device_id;
	u32 running_req;
	u8 taskfileset;
	u8 dev_status;
	u16 reserved;
};

/* Generate  PHY tunning parameters */
struct phy_tuning {
	/* 1 bit,  transmitter emphasis enable	*/
	u8	trans_emp_en:1;
	/* 4 bits, transmitter emphasis amplitude */
	u8	trans_emp_amp:4;
	/* 3 bits, reserved space */
	u8	Reserved_2bit_1:3;
	/* 5 bits, transmitter amplitude */
	u8	trans_amp:5;
	/* 2 bits, transmitter amplitude adjust */
	u8	trans_amp_adj:2;
	/* 1 bit, reserved space */
	u8	resv_2bit_2:1;
	/* 2 bytes, reserved space */
	u8	reserved[2];
};

struct ffe_control {
	/* 4 bits,  FFE Capacitor Select  (value range 0~F)  */
	u8 ffe_cap_sel:4;
	/* 3 bits,  FFE Resistor Select (value range 0~7) */
	u8 ffe_rss_sel:3;
	/* 1 bit reserve*/
	u8 reserved:1;
};

/*
 * HBA_Info_Page is saved in Flash/NVRAM, total 256 bytes.
 * The data area is valid only Signature="MRVL".
 * If any member fills with 0xFF, the member is invalid.
 */
struct hba_info_page {
	/* Dword 0 */
	/* 4 bytes, structure signature,should be "MRVL" at first initial */
	u8 signature[4];

	/* Dword 1-13 */
	u32 reserved1[13];

	/* Dword 14-29 */
	/* 64 bytes, SAS address for each port */
	u64 sas_addr[8];

	/* Dword 30-31 */
	/* 8 bytes for vanir 8 port PHY FFE seeting
	 * BIT 0~3 : FFE Capacitor select(value range 0~F)
	 * BIT 4~6 : FFE Resistor select(value range 0~7)
	 * BIT 7: reserve.
	 */

	struct ffe_control  ffe_ctl[8];
	/* Dword 32 -43 */
	u32 reserved2[12];

	/* Dword 44-45 */
	/* 8 bytes,  0:  1.5G, 1: 3.0G, should be 0x01 at first initial */
	u8 phy_rate[8];

	/* Dword 46-53 */
	/* 32 bytes, PHY tuning parameters for each PHY*/
	struct phy_tuning   phy_tuning[8];

	/* Dword 54-63 */
	u32 reserved3[10];
};	/* total 256 bytes */

struct mvs_slot_info {
	struct list_head entry;
	union {
		struct sas_task *task;
		void *tdata;
	};
	u32 n_elem;
	u32 tx;
	u32 slot_tag;

	/* DMA buffer for storing cmd tbl, open addr frame, status buffer,
	 * and PRD table
	 */
	void *buf;
	dma_addr_t buf_dma;
	void *response;
	struct mvs_port *port;
	struct mvs_device	*device;
	void *open_frame;
};

struct mvs_info {
	unsigned long flags;

	/* host-wide lock */
	spinlock_t lock;

	/* our device */
	struct pci_dev *pdev;
	struct device *dev;

	/* enhanced mode registers */
	void __iomem *regs;

	/* peripheral or soc registers */
	void __iomem *regs_ex;
	u8 sas_addr[SAS_ADDR_SIZE];

	/* SCSI/SAS glue */
	struct sas_ha_struct *sas;
	struct Scsi_Host *shost;

	/* TX (delivery) DMA ring */
	__le32 *tx;
	dma_addr_t tx_dma;

	/* cached next-producer idx */
	u32 tx_prod;

	/* RX (completion) DMA ring */
	__le32	*rx;
	dma_addr_t rx_dma;

	/* RX consumer idx */
	u32 rx_cons;

	/* RX'd FIS area */
	__le32 *rx_fis;
	dma_addr_t rx_fis_dma;

	/* DMA command header slots */
	struct mvs_cmd_hdr *slot;
	dma_addr_t slot_dma;

	u32 chip_id;
	const struct mvs_chip_info *chip;

	int tags_num;
	unsigned long *tags;
	/* further per-slot information */
	struct mvs_phy phy[MVS_MAX_PHYS];
	struct mvs_port port[MVS_MAX_PHYS];
	u32 id;
	u64 sata_reg_set;
	struct list_head *hba_list;
	struct list_head soc_entry;
	struct list_head wq_list;
	unsigned long instance;
	u16 flashid;
	u32 flashsize;
	u32 flashsectSize;

	void *addon;
	struct hba_info_page hba_info_param;
	struct mvs_device	devices[MVS_MAX_DEVICES];
	void *bulk_buffer;
	dma_addr_t bulk_buffer_dma;
	void *bulk_buffer1;
	dma_addr_t bulk_buffer_dma1;
#define TRASH_BUCKET_SIZE    	0x20000
	void *dma_pool;
	struct mvs_slot_info slot_info[0];
};

struct mvs_prv_info{
	u8 n_host;
	u8 n_phy;
	u8 scan_finished;
	u8 reserve;
	struct mvs_info *mvi[2];
	struct tasklet_struct mv_tasklet;
};

struct mvs_wq {
	struct delayed_work work_q;
	struct mvs_info *mvi;
	void *data;
	int handler;
	struct list_head entry;
};

struct mvs_task_exec_info {
	struct sas_task *task;
	struct mvs_cmd_hdr *hdr;
	struct mvs_port *port;
	u32 tag;
	int n_elem;
};

struct mvs_task_list {
	struct sas_task *task;
	struct list_head list;
};


/******************** function prototype *********************/
void mvs_get_sas_addr(void *buf, u32 buflen);
void mvs_tag_clear(struct mvs_info *mvi, u32 tag);
void mvs_tag_free(struct mvs_info *mvi, u32 tag);
void mvs_tag_set(struct mvs_info *mvi, unsigned int tag);
int mvs_tag_alloc(struct mvs_info *mvi, u32 *tag_out);
void mvs_tag_init(struct mvs_info *mvi);
void mvs_iounmap(void __iomem *regs);
int mvs_ioremap(struct mvs_info *mvi, int bar, int bar_ex);
void mvs_phys_reset(struct mvs_info *mvi, u32 phy_mask, int hard);
int mvs_phy_control(struct asd_sas_phy *sas_phy, enum phy_func func,
			void *funcdata);
void mvs_set_sas_addr(struct mvs_info *mvi, int port_id, u32 off_lo,
		      u32 off_hi, u64 sas_addr);
void mvs_scan_start(struct Scsi_Host *shost);
int mvs_scan_finished(struct Scsi_Host *shost, unsigned long time);
int mvs_queue_command(struct sas_task *task, const int num,
			gfp_t gfp_flags);
int mvs_abort_task(struct sas_task *task);
int mvs_abort_task_set(struct domain_device *dev, u8 *lun);
int mvs_clear_aca(struct domain_device *dev, u8 *lun);
int mvs_clear_task_set(struct domain_device *dev, u8 * lun);
void mvs_port_formed(struct asd_sas_phy *sas_phy);
void mvs_port_deformed(struct asd_sas_phy *sas_phy);
int mvs_dev_found(struct domain_device *dev);
void mvs_dev_gone(struct domain_device *dev);
int mvs_lu_reset(struct domain_device *dev, u8 *lun);
int mvs_slot_complete(struct mvs_info *mvi, u32 rx_desc, u32 flags);
int mvs_I_T_nexus_reset(struct domain_device *dev);
int mvs_query_task(struct sas_task *task);
void mvs_release_task(struct mvs_info *mvi,
			struct domain_device *dev);
void mvs_do_release_task(struct mvs_info *mvi, int phy_no,
			struct domain_device *dev);
void mvs_int_port(struct mvs_info *mvi, int phy_no, u32 events);
void mvs_update_phyinfo(struct mvs_info *mvi, int i, int get_st);
int mvs_int_rx(struct mvs_info *mvi, bool self_clear);
struct mvs_device *mvs_find_dev_by_reg_set(struct mvs_info *mvi, u8 reg_set);
#endif

