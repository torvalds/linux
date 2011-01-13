/*
 * Copyright(c) 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#ifndef _FC_LIBFC_H_
#define _FC_LIBFC_H_

#define FC_LIBFC_LOGGING 0x01 /* General logging, not categorized */
#define FC_LPORT_LOGGING 0x02 /* lport layer logging */
#define FC_DISC_LOGGING	 0x04 /* discovery layer logging */
#define FC_RPORT_LOGGING 0x08 /* rport layer logging */
#define FC_FCP_LOGGING	 0x10 /* I/O path logging */
#define FC_EM_LOGGING	 0x20 /* Exchange Manager logging */
#define FC_EXCH_LOGGING	 0x40 /* Exchange/Sequence logging */
#define FC_SCSI_LOGGING	 0x80 /* SCSI logging (mostly error handling) */

extern unsigned int fc_debug_logging;

#define FC_CHECK_LOGGING(LEVEL, CMD)			\
	do {						\
		if (unlikely(fc_debug_logging & LEVEL))	\
			do {				\
				CMD;			\
			} while (0);			\
	} while (0)

#define FC_LIBFC_DBG(fmt, args...)					\
	FC_CHECK_LOGGING(FC_LIBFC_LOGGING,				\
			 printk(KERN_INFO "libfc: " fmt, ##args))

#define FC_LPORT_DBG(lport, fmt, args...)				\
	FC_CHECK_LOGGING(FC_LPORT_LOGGING,				\
			 printk(KERN_INFO "host%u: lport %6.6x: " fmt,	\
				(lport)->host->host_no,			\
				(lport)->port_id, ##args))

#define FC_DISC_DBG(disc, fmt, args...)				\
	FC_CHECK_LOGGING(FC_DISC_LOGGING,			\
			 printk(KERN_INFO "host%u: disc: " fmt,	\
				fc_disc_lport(disc)->host->host_no,	\
				##args))

#define FC_RPORT_ID_DBG(lport, port_id, fmt, args...)			\
	FC_CHECK_LOGGING(FC_RPORT_LOGGING,				\
			 printk(KERN_INFO "host%u: rport %6.6x: " fmt,	\
				(lport)->host->host_no,			\
				(port_id), ##args))

#define FC_RPORT_DBG(rdata, fmt, args...)				\
	FC_RPORT_ID_DBG((rdata)->local_port, (rdata)->ids.port_id, fmt, ##args)

#define FC_FCP_DBG(pkt, fmt, args...)					\
	FC_CHECK_LOGGING(FC_FCP_LOGGING,				\
	{								\
		if ((pkt)->seq_ptr) {					\
			struct fc_exch *_ep = NULL;			\
			_ep = fc_seq_exch((pkt)->seq_ptr);		\
			printk(KERN_INFO "host%u: fcp: %6.6x: "		\
				"xid %04x-%04x: " fmt,			\
				(pkt)->lp->host->host_no,		\
				(pkt)->rport->port_id,			\
				(_ep)->oxid, (_ep)->rxid, ##args);	\
		} else {						\
			printk(KERN_INFO "host%u: fcp: %6.6x: " fmt,	\
				(pkt)->lp->host->host_no,		\
				(pkt)->rport->port_id, ##args);		\
		}							\
	})

#define FC_EXCH_DBG(exch, fmt, args...)					\
	FC_CHECK_LOGGING(FC_EXCH_LOGGING,				\
			 printk(KERN_INFO "host%u: xid %4x: " fmt,	\
				(exch)->lp->host->host_no,		\
				exch->xid, ##args))

#define FC_SCSI_DBG(lport, fmt, args...)				\
	FC_CHECK_LOGGING(FC_SCSI_LOGGING,				\
			 printk(KERN_INFO "host%u: scsi: " fmt,		\
				(lport)->host->host_no,	##args))

/*
 * Set up direct-data placement for this I/O request
 */
void fc_fcp_ddp_setup(struct fc_fcp_pkt *fsp, u16 xid);

/*
 * Module setup functions
 */
int fc_setup_exch_mgr(void);
void fc_destroy_exch_mgr(void);
int fc_setup_rport(void);
void fc_destroy_rport(void);
int fc_setup_fcp(void);
void fc_destroy_fcp(void);

/*
 * Internal libfc functions
 */
const char *fc_els_resp_type(struct fc_frame *);

/*
 * Copies a buffer into an sg list
 */
u32 fc_copy_buffer_to_sglist(void *buf, size_t len,
			     struct scatterlist *sg,
			     u32 *nents, size_t *offset,
			     enum km_type km_type, u32 *crc);

#endif /* _FC_LIBFC_H_ */
