/* Driver for Realtek PCI-Express card reader
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
 */

#ifndef __REALTEK_RTSX_H
#define __REALTEK_RTSX_H

#include <asm/io.h>
#include <asm/bitops.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/cdrom.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/time.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_devinfo.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>

#include "debug.h"
#include "trace.h"
#include "general.h"

#define CR_DRIVER_NAME		"rts_pstor"

#define pci_get_bus_and_slot(bus, devfn)	\
	pci_get_domain_bus_and_slot(0, (bus), (devfn))

/*
 * macros for easy use
 */
#define rtsx_writel(chip, reg, value) \
	iowrite32(value, (chip)->rtsx->remap_addr + reg)
#define rtsx_readl(chip, reg) \
	ioread32((chip)->rtsx->remap_addr + reg)
#define rtsx_writew(chip, reg, value) \
	iowrite16(value, (chip)->rtsx->remap_addr + reg)
#define rtsx_readw(chip, reg) \
	ioread16((chip)->rtsx->remap_addr + reg)
#define rtsx_writeb(chip, reg, value) \
	iowrite8(value, (chip)->rtsx->remap_addr + reg)
#define rtsx_readb(chip, reg) \
	ioread8((chip)->rtsx->remap_addr + reg)

#define rtsx_read_config_byte(chip, where, val) \
	pci_read_config_byte((chip)->rtsx->pci, where, val)

#define rtsx_write_config_byte(chip, where, val) \
	pci_write_config_byte((chip)->rtsx->pci, where, val)

#define wait_timeout_x(task_state, msecs)		\
do {							\
		set_current_state((task_state)); 	\
		schedule_timeout((msecs) * HZ / 1000);	\
} while (0)
#define wait_timeout(msecs)	wait_timeout_x(TASK_INTERRUPTIBLE, (msecs))


#define STATE_TRANS_NONE	0
#define STATE_TRANS_CMD		1
#define STATE_TRANS_BUF		2
#define STATE_TRANS_SG		3

#define TRANS_NOT_READY		0
#define TRANS_RESULT_OK		1
#define TRANS_RESULT_FAIL	2

#define SCSI_LUN(srb)		((srb)->device->lun)

typedef unsigned long DELAY_PARA_T;

struct rtsx_chip;

struct rtsx_dev {
	struct pci_dev 		*pci;

	/* pci resources */
	unsigned long 		addr;
	void __iomem 		*remap_addr;
	int 			irq;

	/* locks */
	spinlock_t 		reg_lock;

	struct task_struct	*ctl_thread;	 /* the control thread   */
	struct task_struct	*polling_thread; /* the polling thread   */

	/* mutual exclusion and synchronization structures */
	struct completion	cmnd_ready;	 /* to sleep thread on	    */
	struct completion	control_exit;	 /* control thread exit	    */
	struct completion	polling_exit;	 /* polling thread exit	    */
	struct completion	notify;		 /* thread begin/end	    */
	struct completion	scanning_done;	 /* wait for scan thread    */

	wait_queue_head_t	delay_wait;	 /* wait during scan, reset */
	struct mutex		dev_mutex;

	/* host reserved buffer */
	void 			*rtsx_resv_buf;
	dma_addr_t 		rtsx_resv_buf_addr;

	char			trans_result;
	char			trans_state;

	struct completion 	*done;
	/* Whether interrupt handler should care card cd info */
	u32 			check_card_cd;

	struct rtsx_chip 	*chip;
};

typedef struct rtsx_dev rtsx_dev_t;

/* Convert between rtsx_dev and the corresponding Scsi_Host */
static inline struct Scsi_Host *rtsx_to_host(struct rtsx_dev *dev)
{
	return container_of((void *) dev, struct Scsi_Host, hostdata);
}
static inline struct rtsx_dev *host_to_rtsx(struct Scsi_Host *host)
{
	return (struct rtsx_dev *) host->hostdata;
}

static inline void get_current_time(u8 *timeval_buf, int buf_len)
{
	struct timeval tv;

	if (!timeval_buf || (buf_len < 8))
		return;

	do_gettimeofday(&tv);

	timeval_buf[0] = (u8)(tv.tv_sec >> 24);
	timeval_buf[1] = (u8)(tv.tv_sec >> 16);
	timeval_buf[2] = (u8)(tv.tv_sec >> 8);
	timeval_buf[3] = (u8)(tv.tv_sec);
	timeval_buf[4] = (u8)(tv.tv_usec >> 24);
	timeval_buf[5] = (u8)(tv.tv_usec >> 16);
	timeval_buf[6] = (u8)(tv.tv_usec >> 8);
	timeval_buf[7] = (u8)(tv.tv_usec);
}

/* The scsi_lock() and scsi_unlock() macros protect the sm_state and the
 * single queue element srb for write access */
#define scsi_unlock(host)	spin_unlock_irq(host->host_lock)
#define scsi_lock(host)		spin_lock_irq(host->host_lock)

#define lock_state(chip)	spin_lock_irq(&((chip)->rtsx->reg_lock))
#define unlock_state(chip)	spin_unlock_irq(&((chip)->rtsx->reg_lock))

/* struct scsi_cmnd transfer buffer access utilities */
enum xfer_buf_dir	{TO_XFER_BUF, FROM_XFER_BUF};

int rtsx_read_pci_cfg_byte(u8 bus, u8 dev, u8 func, u8 offset, u8 *val);

#endif  /* __REALTEK_RTSX_H */
