/*
 *  libata.h - helper library for ATA
 *
 *  Copyright 2003-2004 Red Hat, Inc.  All rights reserved.
 *  Copyright 2003-2004 Jeff Garzik
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/DocBook/libata.*
 *
 */

#ifndef __LIBATA_H__
#define __LIBATA_H__

#define DRV_NAME	"libata"

struct ata_scsi_args {
	struct ata_device	*dev;
	u16			*id;
	struct scsi_cmnd	*cmd;
	void			(*done)(struct scsi_cmnd *);
};

/* libata-core.c */
enum {
	/* flags for ata_dev_read_id() */
	ATA_READID_POSTRESET	= (1 << 0), /* reading ID after reset */

	/* selector for ata_down_xfermask_limit() */
	ATA_DNXFER_PIO		= 0,	/* speed down PIO */
	ATA_DNXFER_DMA		= 1,	/* speed down DMA */
	ATA_DNXFER_40C		= 2,	/* apply 40c cable limit */
	ATA_DNXFER_FORCE_PIO	= 3,	/* force PIO */
	ATA_DNXFER_FORCE_PIO0	= 4,	/* force PIO0 */

	ATA_DNXFER_QUIET	= (1 << 31),
};

extern unsigned int ata_print_id;
extern struct workqueue_struct *ata_aux_wq;
extern int atapi_enabled;
extern int atapi_dmadir;
extern int libata_fua;
extern int libata_noacpi;
extern int ata_spindown_compat;
extern struct ata_queued_cmd *ata_qc_new_init(struct ata_device *dev);
extern int ata_build_rw_tf(struct ata_taskfile *tf, struct ata_device *dev,
			   u64 block, u32 n_block, unsigned int tf_flags,
			   unsigned int tag);
extern u64 ata_tf_read_block(struct ata_taskfile *tf, struct ata_device *dev);
extern void ata_dev_disable(struct ata_device *dev);
extern void ata_port_flush_task(struct ata_port *ap);
extern unsigned ata_exec_internal(struct ata_device *dev,
				  struct ata_taskfile *tf, const u8 *cdb,
				  int dma_dir, void *buf, unsigned int buflen);
extern unsigned ata_exec_internal_sg(struct ata_device *dev,
				     struct ata_taskfile *tf, const u8 *cdb,
				     int dma_dir, struct scatterlist *sg,
				     unsigned int n_elem);
extern unsigned int ata_do_simple_cmd(struct ata_device *dev, u8 cmd);
extern int ata_dev_read_id(struct ata_device *dev, unsigned int *p_class,
			   unsigned int flags, u16 *id);
extern int ata_dev_revalidate(struct ata_device *dev, unsigned int flags);
extern int ata_dev_configure(struct ata_device *dev);
extern int sata_down_spd_limit(struct ata_port *ap);
extern int sata_set_spd_needed(struct ata_port *ap);
extern int ata_down_xfermask_limit(struct ata_device *dev, unsigned int sel);
extern int ata_set_mode(struct ata_port *ap, struct ata_device **r_failed_dev);
extern void ata_sg_clean(struct ata_queued_cmd *qc);
extern void ata_qc_free(struct ata_queued_cmd *qc);
extern void ata_qc_issue(struct ata_queued_cmd *qc);
extern void __ata_qc_complete(struct ata_queued_cmd *qc);
extern int ata_check_atapi_dma(struct ata_queued_cmd *qc);
extern void ata_dev_select(struct ata_port *ap, unsigned int device,
                           unsigned int wait, unsigned int can_sleep);
extern void swap_buf_le16(u16 *buf, unsigned int buf_words);
extern int ata_flush_cache(struct ata_device *dev);
extern void ata_dev_init(struct ata_device *dev);
extern int ata_task_ioctl(struct scsi_device *scsidev, void __user *arg);
extern int ata_cmd_ioctl(struct scsi_device *scsidev, void __user *arg);
extern struct ata_port *ata_port_alloc(struct ata_host *host);

/* libata-acpi.c */
#ifdef CONFIG_ATA_ACPI
extern int ata_acpi_exec_tfs(struct ata_port *ap);
extern int ata_acpi_push_id(struct ata_device *dev);
#else
static inline int ata_acpi_exec_tfs(struct ata_port *ap)
{
	return 0;
}
static inline int ata_acpi_push_id(struct ata_device *dev)
{
	return 0;
}
#endif

/* libata-scsi.c */
extern int ata_scsi_add_hosts(struct ata_host *host,
			      struct scsi_host_template *sht);
extern void ata_scsi_scan_host(struct ata_port *ap);
extern int ata_scsi_offline_dev(struct ata_device *dev);
extern void ata_scsi_hotplug(struct work_struct *work);
extern unsigned int ata_scsiop_inq_std(struct ata_scsi_args *args, u8 *rbuf,
			       unsigned int buflen);

extern unsigned int ata_scsiop_inq_00(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen);

extern unsigned int ata_scsiop_inq_80(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen);
extern unsigned int ata_scsiop_inq_83(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen);
extern unsigned int ata_scsiop_noop(struct ata_scsi_args *args, u8 *rbuf,
			    unsigned int buflen);
extern unsigned int ata_scsiop_sync_cache(struct ata_scsi_args *args, u8 *rbuf,
				  unsigned int buflen);
extern unsigned int ata_scsiop_mode_sense(struct ata_scsi_args *args, u8 *rbuf,
				  unsigned int buflen);
extern unsigned int ata_scsiop_read_cap(struct ata_scsi_args *args, u8 *rbuf,
			        unsigned int buflen);
extern unsigned int ata_scsiop_report_luns(struct ata_scsi_args *args, u8 *rbuf,
				   unsigned int buflen);
extern void ata_scsi_badcmd(struct scsi_cmnd *cmd,
			    void (*done)(struct scsi_cmnd *),
			    u8 asc, u8 ascq);
extern void ata_scsi_set_sense(struct scsi_cmnd *cmd,
			       u8 sk, u8 asc, u8 ascq);
extern void ata_scsi_rbuf_fill(struct ata_scsi_args *args,
                        unsigned int (*actor) (struct ata_scsi_args *args,
                                           u8 *rbuf, unsigned int buflen));
extern void ata_schedule_scsi_eh(struct Scsi_Host *shost);
extern void ata_scsi_dev_rescan(struct work_struct *work);
extern int ata_bus_probe(struct ata_port *ap);

/* libata-eh.c */
extern enum scsi_eh_timer_return ata_scsi_timed_out(struct scsi_cmnd *cmd);
extern void ata_scsi_error(struct Scsi_Host *host);
extern void ata_port_wait_eh(struct ata_port *ap);
extern void ata_qc_schedule_eh(struct ata_queued_cmd *qc);

/* libata-sff.c */
extern u8 ata_irq_on(struct ata_port *ap);


#endif /* __LIBATA_H__ */
