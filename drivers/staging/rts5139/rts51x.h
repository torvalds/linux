/* Driver for Realtek RTS51xx USB card reader
 * Header file
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   wwang (wei_wang@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 * Maintainer:
 *   Edwin Rong (edwin_rong@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#ifndef __RTS51X_H
#define __RTS51X_H

#include <linux/usb.h>
#include <linux/usb_usual.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/cdrom.h>
#include <linux/kernel.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_devinfo.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>

#define DRIVER_VERSION		"v1.04"

#define RTS51X_DESC		"Realtek RTS5139/29 USB card reader driver"
#define RTS51X_NAME		"rts5139"
#define RTS51X_CTL_THREAD	"rts5139-control"
#define RTS51X_POLLING_THREAD	"rts5139-polling"

#define POLLING_IN_THREAD
#define SUPPORT_FILE_OP

#define wait_timeout_x(task_state, msecs)	\
do {						\
	set_current_state((task_state));	\
	schedule_timeout((msecs) * HZ / 1000);	\
} while (0)

#define wait_timeout(msecs)	wait_timeout_x(TASK_INTERRUPTIBLE, (msecs))

#define SCSI_LUN(srb)		((srb)->device->lun)

/* Size of the DMA-mapped I/O buffer */
#define RTS51X_IOBUF_SIZE	1024

/* Dynamic bitflag definitions (dflags): used in set_bit() etc. */
#define FLIDX_URB_ACTIVE	0	/* current_urb is in use    */
#define FLIDX_SG_ACTIVE		1	/* current_sg is in use     */
#define FLIDX_ABORTING		2	/* abort is in progress     */
#define FLIDX_DISCONNECTING	3	/* disconnect in progress   */
#define FLIDX_RESETTING		4	/* device reset in progress */
#define FLIDX_TIMED_OUT		5	/* SCSI midlayer timed out  */

struct rts51x_chip;

struct rts51x_usb {
	/* The device we're working with
	 * It's important to note:
	 *    (o) you must hold dev_mutex to change pusb_dev
	 */
	struct mutex dev_mutex;	/* protect pusb_dev */
	struct usb_device *pusb_dev;	/* this usb_device */
	struct usb_interface *pusb_intf;	/* this interface */

	unsigned long dflags;	/* dynamic atomic bitflags */

	unsigned int send_bulk_pipe;	/* cached pipe values */
	unsigned int recv_bulk_pipe;
	unsigned int send_ctrl_pipe;
	unsigned int recv_ctrl_pipe;
	unsigned int recv_intr_pipe;

	u8 ifnum;		/* interface number   */
	u8 ep_bInterval;	/* interrupt interval */

	/* control and bulk communications data */
	struct urb *current_urb;	/* USB requests         */
	struct urb *intr_urb;	/* Interrupt USB request */
	struct usb_ctrlrequest *cr;	/* control requests     */
	struct usb_sg_request current_sg;	/* scatter-gather req.  */
	unsigned char *iobuf;	/* I/O buffer           */
	dma_addr_t cr_dma;	/* buffer DMA addresses */
	dma_addr_t iobuf_dma;
	struct task_struct *ctl_thread;	/* the control thread   */
	struct task_struct *polling_thread;	/* the polling thread   */

	/* mutual exclusion and synchronization structures */
	struct completion cmnd_ready;	/* to sleep thread on      */
	struct completion control_exit;	/* control thread exit     */
	struct completion polling_exit;	/* polling thread exit     */
	struct completion notify;	/* thread begin/end        */
};

extern struct usb_driver rts51x_driver;

static inline void get_current_time(u8 *timeval_buf, int buf_len)
{
	struct timeval tv;

	if (!timeval_buf || (buf_len < 8))
		return;

	do_gettimeofday(&tv);

	timeval_buf[0] = (u8) (tv.tv_sec >> 24);
	timeval_buf[1] = (u8) (tv.tv_sec >> 16);
	timeval_buf[2] = (u8) (tv.tv_sec >> 8);
	timeval_buf[3] = (u8) (tv.tv_sec);
	timeval_buf[4] = (u8) (tv.tv_usec >> 24);
	timeval_buf[5] = (u8) (tv.tv_usec >> 16);
	timeval_buf[6] = (u8) (tv.tv_usec >> 8);
	timeval_buf[7] = (u8) (tv.tv_usec);
}

#define SND_CTRL_PIPE(chip)	((chip)->usb->send_ctrl_pipe)
#define RCV_CTRL_PIPE(chip)	((chip)->usb->recv_ctrl_pipe)
#define SND_BULK_PIPE(chip)	((chip)->usb->send_bulk_pipe)
#define RCV_BULK_PIPE(chip)	((chip)->usb->recv_bulk_pipe)
#define RCV_INTR_PIPE(chip)	((chip)->usb->recv_intr_pipe)

/* The scsi_lock() and scsi_unlock() macros protect the sm_state and the
 * single queue element srb for write access */
#define scsi_unlock(host)	spin_unlock_irq(host->host_lock)
#define scsi_lock(host)		spin_lock_irq(host->host_lock)

#define GET_PM_USAGE_CNT(chip)	\
	atomic_read(&((chip)->usb->pusb_intf->pm_usage_cnt))
#define SET_PM_USAGE_CNT(chip, cnt)	\
	atomic_set(&((chip)->usb->pusb_intf->pm_usage_cnt), (cnt))

/* Compatible macros while we switch over */
static inline void *usb_buffer_alloc(struct usb_device *dev, size_t size,
				     gfp_t mem_flags, dma_addr_t *dma)
{
	return usb_alloc_coherent(dev, size, mem_flags, dma);
}

static inline void usb_buffer_free(struct usb_device *dev, size_t size,
				   void *addr, dma_addr_t dma)
{
	return usb_free_coherent(dev, size, addr, dma);
}

/* Convert between us_data and the corresponding Scsi_Host */
static inline struct Scsi_Host *rts51x_to_host(struct rts51x_chip *chip)
{
	return container_of((void *)chip, struct Scsi_Host, hostdata);
}

static inline struct rts51x_chip *host_to_rts51x(struct Scsi_Host *host)
{
	return (struct rts51x_chip *)(host->hostdata);
}

/* struct scsi_cmnd transfer buffer access utilities */
enum xfer_buf_dir { TO_XFER_BUF, FROM_XFER_BUF };

/* General routines provided by the usb-storage standard core */
#ifdef CONFIG_PM
void rts51x_try_to_exit_ss(struct rts51x_chip *chip);
int rts51x_suspend(struct usb_interface *iface, pm_message_t message);
int rts51x_resume(struct usb_interface *iface);
int rts51x_reset_resume(struct usb_interface *iface);
#else
#define rts51x_suspend		NULL
#define rts51x_resume		NULL
#define rts51x_reset_resume	NULL
#endif

extern struct scsi_host_template rts51x_host_template;

#endif /* __RTS51X_H */
