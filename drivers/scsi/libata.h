/*
   libata.h - helper library for ATA

   Copyright 2003-2004 Red Hat, Inc.  All rights reserved.
   Copyright 2003-2004 Jeff Garzik

   The contents of this file are subject to the Open
   Software License version 1.1 that can be found at
   http://www.opensource.org/licenses/osl-1.1.txt and is included herein
   by reference.

   Alternatively, the contents of this file may be used under the terms
   of the GNU General Public License version 2 (the "GPL") as distributed
   in the kernel source COPYING file, in which case the provisions of
   the GPL are applicable instead of the above.  If you wish to allow
   the use of your version of this file only under the terms of the
   GPL and not to allow others to use your version of this file under
   the OSL, indicate your decision by deleting the provisions above and
   replace them with the notice and other provisions required by the GPL.
   If you do not delete the provisions above, a recipient may use your
   version of this file under either the OSL or the GPL.

 */

#ifndef __LIBATA_H__
#define __LIBATA_H__

#define DRV_NAME	"libata"
#define DRV_VERSION	"1.10"	/* must be exactly four chars */

struct ata_scsi_args {
	u16			*id;
	struct scsi_cmnd	*cmd;
	void			(*done)(struct scsi_cmnd *);
};

/* libata-core.c */
extern struct ata_queued_cmd *ata_qc_new_init(struct ata_port *ap,
				      struct ata_device *dev);
extern void ata_qc_free(struct ata_queued_cmd *qc);
extern int ata_qc_issue(struct ata_queued_cmd *qc);
extern int ata_check_atapi_dma(struct ata_queued_cmd *qc);
extern void ata_dev_select(struct ata_port *ap, unsigned int device,
                           unsigned int wait, unsigned int can_sleep);
extern void ata_tf_to_host_nolock(struct ata_port *ap, struct ata_taskfile *tf);
extern void swap_buf_le16(u16 *buf, unsigned int buf_words);


/* libata-scsi.c */
extern void ata_to_sense_error(struct ata_queued_cmd *qc, u8 drv_stat);
extern int ata_scsi_error(struct Scsi_Host *host);
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
extern void ata_scsi_rbuf_fill(struct ata_scsi_args *args, 
                        unsigned int (*actor) (struct ata_scsi_args *args,
                                           u8 *rbuf, unsigned int buflen));

static inline void ata_bad_scsiop(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	ata_scsi_badcmd(cmd, done, 0x20, 0x00);
}

static inline void ata_bad_cdb(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	ata_scsi_badcmd(cmd, done, 0x24, 0x00);
}

#endif /* __LIBATA_H__ */
