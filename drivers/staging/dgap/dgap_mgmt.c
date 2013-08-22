/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *	NOTE TO LINUX KERNEL HACKERS:  DO NOT REFORMAT THIS CODE! 
 *
 *	This is shared code between Digi's CVS archive and the
 *	Linux Kernel sources.
 *	Changing the source just for reformatting needlessly breaks
 *	our CVS diff history.
 *
 *	Send any bug fixes/changes to:  Eng.Linux at digi dot com. 
 *	Thank you. 
 *
 */

/************************************************************************
 * 
 * This file implements the mgmt functionality for the
 * FEP5 based product lines.
 * 
 ************************************************************************
 * $Id: dgap_mgmt.c,v 1.2 2010/12/13 19:38:04 markh Exp $
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/ctype.h>
#include <linux/sched.h>	/* For jiffies, task states */
#include <linux/interrupt.h>	/* For tasklet and interrupt structs/defines */
#include <linux/pci.h>
#include <linux/serial_reg.h>
#include <linux/termios.h>
#include <asm/uaccess.h>	/* For copy_from_user/copy_to_user */
#include <asm/io.h>		/* For read[bwl]/write[bwl] */

#include "dgap_driver.h"
#include "dgap_kcompat.h"	/* Kernel 2.4/2.6 compat includes */
#include "dgap_fep5.h"
#include "dgap_parse.h"
#include "dgap_mgmt.h"
#include "dgap_downld.h"
#include "dgap_tty.h"
#include "dgap_proc.h"
#include "dgap_sysfs.h"


/* This holds the status of the KME buffer */
static int 		dgap_kmebusy = 0;

/* Our "in use" variables, to enforce 1 open only */
static int		dgap_mgmt_in_use = 0;
static int		dgap_downld_in_use = 0;


/*
 * dgap_mgmt_open()  
 *
 * Open the mgmt/downld/dpa device
 */  
int dgap_mgmt_open(struct inode *inode, struct file *file)
{
	unsigned long lock_flags;
	unsigned int minor = iminor(inode);

	DPR_MGMT(("dgap_mgmt_open start.\n"));

	DGAP_LOCK(dgap_global_lock, lock_flags); 

	/* mgmt device */
	if (minor == MGMT_MGMT) {
		/* Only allow 1 open at a time on mgmt device */
		if (dgap_mgmt_in_use) {
			DGAP_UNLOCK(dgap_global_lock, lock_flags); 
			return (-EBUSY);
		}
		dgap_mgmt_in_use++;
	}
	/* downld device */
	else if (minor == MGMT_DOWNLD) {
		/* Only allow 1 open at a time on downld device */
		if (dgap_downld_in_use) {
			DGAP_UNLOCK(dgap_global_lock, lock_flags); 
			return (-EBUSY);
		}
		dgap_downld_in_use++;
	}
	else {
		DGAP_UNLOCK(dgap_global_lock, lock_flags); 
		return (-ENXIO);
	}

	DGAP_UNLOCK(dgap_global_lock, lock_flags); 

	DPR_MGMT(("dgap_mgmt_open finish.\n"));

	return 0;
}

/*
 * dgap_mgmt_close()
 *
 * Open the mgmt/dpa device
 */  
int dgap_mgmt_close(struct inode *inode, struct file *file)
{
	unsigned long lock_flags;
	unsigned int minor = iminor(inode);

	DPR_MGMT(("dgap_mgmt_close start.\n"));

	DGAP_LOCK(dgap_global_lock, lock_flags); 

	/* mgmt device */
	if (minor == MGMT_MGMT) {
		if (dgap_mgmt_in_use) {
			dgap_mgmt_in_use = 0;
		}
	}
	/* downld device */
	else if (minor == MGMT_DOWNLD) {
		if (dgap_downld_in_use) {
			dgap_downld_in_use = 0;
		}
	}

	DGAP_UNLOCK(dgap_global_lock, lock_flags); 

	DPR_MGMT(("dgap_mgmt_close finish.\n"));

	return 0;
}


/*
 * dgap_mgmt_ioctl()
 *
 * ioctl the mgmt/dpa device
 */  
#ifdef HAVE_UNLOCKED_IOCTL
long dgap_mgmt_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
        struct inode *inode = file->f_dentry->d_inode;
#else
int dgap_mgmt_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
#endif
	unsigned long lock_flags;
	int error = 0;
	int i = 0;
	int j = 0;
	struct board_t *brd;
	struct channel_t *ch;
	static struct downldio dlio;
	void __user *uarg = (void __user *) arg;

	DPR_MGMT(("dgap_mgmt_ioctl start.\n"));

	switch (cmd) {

	/* HiJack the usage of SEDELAY to turn on/off debugging. */
	case DIGI_SEDELAY:
	{
		unsigned int value = 0;

		if (copy_from_user((unsigned int *) &value, uarg, sizeof(unsigned int))) {
			return (-EFAULT);
		}

		printk("Setting debug of value: %x\n", value);
		dgap_debug = value;
		return 0;
	}

	case DIGI_DLREQ_GET:
	{

get_service:

		DGAP_LOCK(dgap_global_lock, lock_flags);

		if (dgap_driver_state == DRIVER_NEED_CONFIG_LOAD) {

			dgap_driver_state = DRIVER_REQUESTED_CONFIG;

			dlio.req_type = DLREQ_CONFIG;
			dlio.bdid = 0;
			dlio.image.fi.type = 0;

			DGAP_UNLOCK(dgap_global_lock, lock_flags);

			if (copy_to_user(uarg, &dlio, sizeof(struct downldio))) {
				DGAP_LOCK(dgap_global_lock, lock_flags);
				dgap_driver_state = DRIVER_NEED_CONFIG_LOAD;
				DGAP_UNLOCK(dgap_global_lock, lock_flags);
				return(-EFAULT);
			}

			return(0);
		}

		DGAP_UNLOCK(dgap_global_lock, lock_flags);

		/*
		 * Loop thru each board.
		 * Check state, force state machine to start running.
		 */
		for (i = 0; i < dgap_NumBoards; i++ ) {

			brd = dgap_Board[i];

			DGAP_LOCK(brd->bd_lock, lock_flags);

			switch (brd->state) {

			case NEED_DEVICE_CREATION:

				/*
				 * Let go of lock, tty_register() (and us also)
				 * does a non-atomic malloc, so it would be
				 * possible to deadlock the system if the
				 * malloc went to sleep.
				 */
				DGAP_UNLOCK(brd->bd_lock, lock_flags);
				dgap_tty_register(brd);
				dgap_finalize_board_init(brd);
				DGAP_LOCK(brd->bd_lock, lock_flags);

				brd->state = REQUESTED_DEVICE_CREATION;

				dlio.req_type = DLREQ_DEVCREATE;
				dlio.bdid = i;
				dlio.image.fi.type = brd->dpatype;

				DGAP_UNLOCK(brd->bd_lock, lock_flags);

				if (copy_to_user(uarg, &dlio, sizeof(struct downldio))) {
					DGAP_LOCK(brd->bd_lock, lock_flags);
					brd->state = NEED_DEVICE_CREATION;
					DGAP_UNLOCK(brd->bd_lock, lock_flags);
					return(-EFAULT);
				}

				return(0);

			case NEED_BIOS_LOAD:

				brd->state = REQUESTED_BIOS;

				dlio.req_type = DLREQ_BIOS;
				dlio.bdid = i;
				dlio.image.fi.type = brd->dpatype;

				DGAP_UNLOCK(brd->bd_lock, lock_flags);
				if (copy_to_user(uarg, &dlio, sizeof(struct downldio))) {
					DGAP_LOCK(brd->bd_lock, lock_flags);
					brd->state = NEED_BIOS_LOAD;
					DGAP_UNLOCK(brd->bd_lock, lock_flags);
					return(-EFAULT);
				}

				return(0);

			case NEED_FEP_LOAD:
				brd->state = REQUESTED_FEP;

				dlio.req_type = DLREQ_FEP; 
				dlio.bdid = i;
				dlio.image.fi.type = brd->dpatype;

				DGAP_UNLOCK(brd->bd_lock, lock_flags);

				if (copy_to_user(uarg, &dlio, sizeof(struct downldio))) {
					DGAP_LOCK(brd->bd_lock, lock_flags);
					brd->state = NEED_FEP_LOAD;
					DGAP_UNLOCK(brd->bd_lock, lock_flags);
					return(-EFAULT);
				}
				return(0);

			case NEED_PROC_CREATION:

				DGAP_UNLOCK(brd->bd_lock, lock_flags);
				//dgap_proc_register_channel_postscan(brd->boardnum);

				ch = brd->channels[0];
				for (j = 0; j < brd->nasync; j++, ch = brd->channels[j]) {
					struct device *classp;
					classp = 
						tty_register_device(brd->SerialDriver, j, 
						&(ch->ch_bd->pdev->dev));
					ch->ch_tun.un_sysfs = classp;
					dgap_create_tty_sysfs(&ch->ch_tun, classp);

					classp = 
						tty_register_device(brd->PrintDriver, j, 
						&(ch->ch_bd->pdev->dev));
					ch->ch_pun.un_sysfs = classp;
					dgap_create_tty_sysfs(&ch->ch_pun, classp);
				}
				dgap_create_ports_sysfiles(brd);

				DGAP_LOCK(brd->bd_lock, lock_flags);
				brd->state = FINISHED_PROC_CREATION;
				DGAP_UNLOCK(brd->bd_lock, lock_flags);
				break;

			default:
				DGAP_UNLOCK(brd->bd_lock, lock_flags);
				break;

			}

			DGAP_LOCK(brd->bd_lock, lock_flags);

			switch (brd->conc_dl_status) {

			case NEED_CONCENTRATOR:
			{
				u16 offset = 0;
				char *vaddr;
				struct downld_t *to_dp;

				vaddr = brd->re_map_membase;
				if (!vaddr) {
					brd->conc_dl_status = NO_PENDING_CONCENTRATOR_REQUESTS;
					DGAP_UNLOCK(brd->bd_lock, lock_flags);
					break;
				}

				dlio.req_type = DLREQ_CONC;
				dlio.bdid = i;

				offset = readw((u16 *) (vaddr + DOWNREQ));
				to_dp = (struct downld_t *) (vaddr + (int) offset);

				if (!to_dp) {
					brd->conc_dl_status = NO_PENDING_CONCENTRATOR_REQUESTS;
					DGAP_UNLOCK(brd->bd_lock, lock_flags);
					break;
				}

				memcpy(&dlio.image.dl, to_dp, sizeof(struct downld_t));

				brd->conc_dl_status = REQUESTED_CONCENTRATOR;

				DGAP_UNLOCK(brd->bd_lock, lock_flags);

				if (copy_to_user(uarg, &dlio, sizeof(struct downldio))) {
					DGAP_LOCK(brd->bd_lock, lock_flags);
					brd->conc_dl_status = NEED_CONCENTRATOR;
					DGAP_UNLOCK(brd->bd_lock, lock_flags);
					return(-EFAULT);
				}

				return(0);
			}

			default:
				DGAP_UNLOCK(brd->bd_lock, lock_flags);
				break;
			}
		}

		/*
		 * Go to sleep waiting for the driver to signal an event to us.
		 */
		error = wait_event_interruptible(dgap_dl_wait, (dgap_dl_action));

		DGAP_LOCK(dgap_dl_lock, lock_flags);
		dgap_dl_action = 0;
		DGAP_UNLOCK(dgap_dl_lock, lock_flags);

		/* Break out of ioctl if user cancelled us */
		if (error)
			break;

		goto get_service;

	}

	case DIGI_DLREQ_SET:
	{
		uchar __user *uaddr = NULL;
		uchar *uaddr2 = NULL;

		if (copy_from_user((char *) &dlio, uarg, sizeof(struct downldio))) {
			return (-EFAULT);
		}

		if (dlio.req_type == DLREQ_CONFIG) {

			uaddr = uarg +
				(int) ( ((struct downldio *)0)->image.fi.fepimage);

			dgap_do_config_load(uaddr, dlio.image.fi.len);
			dgap_after_config_loaded();

			DGAP_LOCK(dgap_global_lock, lock_flags);
			dgap_driver_state = DRIVER_READY;
			DGAP_UNLOCK(dgap_global_lock, lock_flags);

			break;

		}

		if (dlio.bdid < 0 || dlio.bdid > dgap_NumBoards) {
			return(-ENXIO);
                }

		brd = dgap_Board[dlio.bdid];

		switch(dlio.req_type) {

		case DLREQ_BIOS:
			if (brd->state == BOARD_FAILED || brd->state == BOARD_READY) {
				break;
			}

			if (dlio.image.fi.type == -1) {
				DGAP_LOCK(brd->bd_lock, lock_flags);
				brd->state = BOARD_FAILED;
				brd->dpastatus = BD_NOBIOS;
				DGAP_UNLOCK(brd->bd_lock, lock_flags);
				break;
			}


			uaddr = uarg +
				(int) ( ((struct downldio *)0)->image.fi.fepimage);

			dgap_do_bios_load(brd, uaddr, dlio.image.fi.len);

			break;
		case DLREQ_FEP:
			if (brd->state == BOARD_FAILED || brd->state == BOARD_READY) {
				 break;
			}

			if (dlio.image.fi.type == -1) {
				DGAP_LOCK(brd->bd_lock, lock_flags);
				brd->state = BOARD_FAILED;
				brd->dpastatus = BD_NOBIOS;
				DGAP_UNLOCK(brd->bd_lock, lock_flags);
				break;
			}

			uaddr = uarg +
				(int) ( ((struct downldio *)0)->image.fi.fepimage);

			dgap_do_fep_load(brd, uaddr, dlio.image.fi.len);

			break;

		case DLREQ_CONC:

			if (brd->state == BOARD_FAILED) {
				 break;
			}

			if (dlio.image.fi.type == -1) {
				break;
			}

			uaddr2 = (char *) &dlio.image.dl;
			dgap_do_conc_load(brd, uaddr2, sizeof(struct downld_t));

			break;

		case DLREQ_DEVCREATE:
			if (brd->state == BOARD_FAILED || brd->state == BOARD_READY) {
				 break;
			}

			DGAP_LOCK(brd->bd_lock, lock_flags);
			brd->state = FINISHED_DEVICE_CREATION;
			DGAP_UNLOCK(brd->bd_lock, lock_flags);

			break;

		}

		break;
	}


	case DIGI_GETDD:
	{
		/*
		 * This returns the total number of boards
		 * in the system, as well as driver version
		 * and has space for a reserved entry
		 */
		struct digi_dinfo ddi;

		DGAP_LOCK(dgap_global_lock, lock_flags); 

		ddi.dinfo_nboards = dgap_NumBoards;
		sprintf(ddi.dinfo_version, "%s", DG_PART);

		DGAP_UNLOCK(dgap_global_lock, lock_flags); 

		DPR_MGMT(("DIGI_GETDD returning numboards: %lu version: %s\n",
			ddi.dinfo_nboards, ddi.dinfo_version));

		if (copy_to_user(uarg, &ddi, sizeof(ddi)))
			return(-EFAULT);

		break;
	}

	case DIGI_GETBD:
	{
		int brd2;
		struct digi_info di;

		if (copy_from_user(&brd2, uarg, sizeof(int))) {
			return(-EFAULT);
		}

		DPR_MGMT(("DIGI_GETBD asking about board: %d\n", brd2));

		if ((brd2 < 0) || (brd2 > dgap_NumBoards) || (dgap_NumBoards == 0))
			return (-ENODEV);

		memset(&di, 0, sizeof(di));

		di.info_bdnum = brd2;

		DGAP_LOCK(dgap_Board[brd2]->bd_lock, lock_flags); 

		di.info_bdtype = dgap_Board[brd2]->dpatype;
		di.info_bdstate = dgap_Board[brd2]->dpastatus;
		di.info_ioport = (ulong) dgap_Board[brd2]->port;
		di.info_physaddr = (ulong) dgap_Board[brd2]->membase;
		di.info_physsize = (ulong) dgap_Board[brd2]->membase - dgap_Board[brd2]->membase_end;
		if (dgap_Board[brd2]->state != BOARD_FAILED)
			di.info_nports = dgap_Board[brd2]->nasync;
		else
			di.info_nports = 0;

		DGAP_UNLOCK(dgap_Board[brd2]->bd_lock, lock_flags); 

		DPR_MGMT(("DIGI_GETBD returning type: %x state: %x ports: %x size: %lx\n",
			di.info_bdtype, di.info_bdstate, di.info_nports, di.info_physsize));

		if (copy_to_user(uarg, &di, sizeof (di)))
			return(-EFAULT);

		break;
	}

	case DIGI_KME:
	{
		int itmp, jtmp;
		unchar *memaddr = NULL;           
		struct rw_t kme;
		struct rw_t *mp = NULL;
		int brd2 = 0;
		struct board_t *bd;

		/* This ioctl takes an argument of type 'rw_t'
		 * and uses it to interact with the KME struct
		 * located on the digiboard itself.
		 */
		if (copy_from_user(&kme, uarg, sizeof(kme)))
			return(-EFAULT);

		if (kme.rw_size > 128)
			kme.rw_size = 128;

		brd2 = kme.rw_board;

		DPR_MGMT(("dgap_mgmt: DIGI_KME: %s asked for board %d\n", current->comm, brd2));

		/* Sanity Checking... */
		if ((brd2 < 0) || (brd2 > dgap_NumBoards) || (dgap_NumBoards == 0))
			return (-ENODEV);

		bd = dgap_Board[brd2];

		DGAP_LOCK(dgap_Board[brd2]->bd_lock, lock_flags); 

		if (bd->state != BOARD_READY) {
			DGAP_UNLOCK(dgap_Board[brd2]->bd_lock, lock_flags); 
			return(-ENODEV);
		}

		memaddr = bd->re_map_membase;

		DGAP_UNLOCK(dgap_Board[brd2]->bd_lock, lock_flags); 

		/* If the concentrator number is 0... */
		if (kme.rw_conc == 0 && kme.rw_addr < 0x100000) {
			int addr = kme.rw_addr;
			int size = kme.rw_size;
			caddr_t data = (caddr_t) kme.rw_data; 

			while ((itmp = size)) {

				switch (kme.rw_req) {
				case RW_READ: 
				{
					register caddr_t cp1 = (char *)memaddr + addr;
					register caddr_t cp2 = kme.rw_data;

					DPR_MGMT(("RW_READ CARDMEM - page=%d rw_addr=0x%lx  rw_size=%x\n",
						page, kme.rw_addr, kme.rw_size));

					for (jtmp = 0; jtmp < itmp; jtmp++) {
						*cp2++ = readb(cp1++);
					}
				}

				break;

				case RW_WRITE:
				{
					register caddr_t cp1 = memaddr + addr;
					register caddr_t cp2 = data;

					DPR_MGMT(("RW_WRITE CARDMEM - page=%d rw_addr=0x%lx rw_size=%d\n",
						page, kme.rw_addr, kme.rw_size));

					for (jtmp = 0; jtmp < itmp; jtmp++) {
						writeb(*cp2++, cp1++);
					}
				}
				break;
				}

				addr += itmp;
				data += itmp;
				size -= itmp;

			}

		} 
		else {                        

			/*
			 * Read/Write memory in a REMOTE CONCENTRATOR..
			 * There is only 1 buffer, so do mutual
			 * exclusion to make sure only one KME
			 * request is pending...
			 */

			mp = (struct rw_t *) (memaddr + KMEMEM);

			while (dgap_kmebusy != 0) {
				dgap_kmebusy = 2;      
				error = wait_event_interruptible(bd->kme_wait, (!dgap_kmebusy));
				if (error)
					goto endkme;
			}

			dgap_kmebusy = 1;

			/* Copy KME request to the board.. */

			mp->rw_board = kme.rw_board;
			mp->rw_conc  = kme.rw_conc;
			mp->rw_reserved = kme.rw_reserved;
			memcpy(&mp->rw_addr, &kme.rw_addr, sizeof(int));
			memcpy(&mp->rw_size, &kme.rw_size, sizeof(short));

			if(kme.rw_req == RW_WRITE) {
				register caddr_t cp1 = (caddr_t) mp->rw_data;
				register caddr_t cp2 = (caddr_t) kme.rw_data;

				DPR_MGMT(("RW_WRITE CONCMEM - rw_addr=0x%lx  rw_size=%d\n",
					kme.rw_addr, kme.rw_size));

				for (jtmp = 0; jtmp < (int) kme.rw_size; jtmp++) {
					writeb(*cp2++, cp1++);
				}
			}

			/* EXECUTE REQUEST */

			mp->rw_req = kme.rw_req;

			/*
			 * Wait for the board to process the
			 * request, but limit the wait to 2 secs
			 */

			for (itmp = jiffies + (2 * HZ); mp->rw_req;) {
				if(jiffies >= itmp) {     
					error = ENXIO;
					/* Set request back to 0.. */
					mp->rw_req = 0;
					goto endkme;
				}

				schedule_timeout(HZ / 10);
			}

			/* 
			 * Since this portion of code is looksee
			 * ported from the HPUX EMUX code, i'm
			 * leaving OUT a portion of that code where
			 * the HP/UX code actually puts the process
			 * to sleep for some reason
			 */
			if (mp->rw_size < kme.rw_size)
				memcpy(&kme.rw_size, &mp->rw_size, sizeof(short));

			/* Copy the READ data back to the source buffer... */
			if (kme.rw_req == RW_READ) {
				register caddr_t cp1 = (caddr_t) mp->rw_data;
				register caddr_t cp2 = (caddr_t) kme.rw_data;

				DPR_MGMT(("RW_READ CONCMEM - rw_addr=0x%lx  rw_size=%d\n",
					kme.rw_addr, kme.rw_size));

				for (jtmp = 0; jtmp < (int) kme.rw_size; jtmp++) {
					*cp2++ = readb(cp1++);
				}
			}       

			/*
			 * Common exit point for code sharing the
			 * kme buffer. Before exiting, always wake
			 * another process waiting for the buffer
			 */

		endkme:

			if (dgap_kmebusy != 1)
				wake_up_interruptible(&bd->kme_wait);
			dgap_kmebusy = 0;
			if (error == ENXIO)
				return(-EINVAL);
		}        

		/* Copy the whole (Possibly Modified) mess */
		/* back out to user space...               */

		if (!error) {
			if (copy_to_user(uarg, &kme, sizeof(kme)))
				return (-EFAULT);

			return(0);
		}
	}
	}

	DPR_MGMT(("dgap_mgmt_ioctl finish.\n"));

	return 0;
}
