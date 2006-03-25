/*
 * Modified in order to keep it compatible both with new and old videotext IOCTLs by
 * Michael Geng <linux@MichaelGeng.de>
 *
 *	Cleaned up to use existing videodev interface and allow the idea
 *	of multiple teletext decoders on the video4linux iface. Changed i2c
 *	to cover addressing clashes on device busses. It's also rebuilt so
 *	you can add arbitary multiple teletext devices to Linux video4linux
 *	now (well 32 anyway).
 *
 *	Alan Cox <Alan.Cox@linux.org>
 *
 *	The original driver was heavily modified to match the i2c interface
 *	It was truncated to use the WinTV boards, too.
 *
 *	Copyright (c) 1998 Richard Guenther <richard.guenther@student.uni-tuebingen.de>
 *
 * $Id: saa5249.c,v 1.1 1998/03/30 22:23:23 alan Exp $
 *
 *	Derived From
 *
 * vtx.c:
 * This is a loadable character-device-driver for videotext-interfaces
 * (aka teletext). Please check the Makefile/README for a list of supported
 * interfaces.
 *
 * Copyright (c) 1994-97 Martin Buck  <martin-2.buck@student.uni-ulm.de>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <stdarg.h>
#include <linux/i2c.h>
#include <linux/videotext.h>
#include <linux/videodev.h>
#include <linux/mutex.h>


#include <asm/io.h>
#include <asm/uaccess.h>

#define VTX_VER_MAJ 1
#define VTX_VER_MIN 8



#define NUM_DAUS 4
#define NUM_BUFS 8
#define IF_NAME "SAA5249"

static const int disp_modes[8][3] =
{
	{ 0x46, 0x03, 0x03 },	/* DISPOFF */
	{ 0x46, 0xcc, 0xcc },	/* DISPNORM */
	{ 0x44, 0x0f, 0x0f },	/* DISPTRANS */
	{ 0x46, 0xcc, 0x46 },	/* DISPINS */
	{ 0x44, 0x03, 0x03 },	/* DISPOFF, interlaced */
	{ 0x44, 0xcc, 0xcc },	/* DISPNORM, interlaced */
	{ 0x44, 0x0f, 0x0f },	/* DISPTRANS, interlaced */
	{ 0x44, 0xcc, 0x46 }	/* DISPINS, interlaced */
};



#define PAGE_WAIT (300*HZ/1000)			/* Time between requesting page and */
						/* checking status bits */
#define PGBUF_EXPIRE (15*HZ)			/* Time to wait before retransmitting */
						/* page regardless of infobits */
typedef struct {
	u8 pgbuf[VTX_VIRTUALSIZE];		/* Page-buffer */
	u8 laststat[10];			/* Last value of infobits for DAU */
	u8 sregs[7];				/* Page-request registers */
	unsigned long expire;			/* Time when page will be expired */
	unsigned clrfound : 1;			/* VTXIOCCLRFOUND has been called */
	unsigned stopped : 1;			/* VTXIOCSTOPDAU has been called */
} vdau_t;

struct saa5249_device
{
	vdau_t vdau[NUM_DAUS];			/* Data for virtual DAUs (the 5249 only has one */
						/* real DAU, so we have to simulate some more) */
	int vtx_use_count;
	int is_searching[NUM_DAUS];
	int disp_mode;
	int virtual_mode;
	struct i2c_client *client;
	struct mutex lock;
};


#define CCTWR 34		/* I²C write/read-address of vtx-chip */
#define CCTRD 35
#define NOACK_REPEAT 10		/* Retry access this many times on failure */
#define CLEAR_DELAY (HZ/20)	/* Time required to clear a page */
#define READY_TIMEOUT (30*HZ/1000)	/* Time to wait for ready signal of I²C-bus interface */
#define INIT_DELAY 500		/* Time in usec to wait at initialization of CEA interface */
#define START_DELAY 10		/* Time in usec to wait before starting write-cycle (CEA) */

#define VTX_DEV_MINOR 0

/* General defines and debugging support */

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

#define RESCHED do { cond_resched(); } while(0)

static struct video_device saa_template;	/* Declared near bottom */

/* Addresses to scan */
static unsigned short normal_i2c[] = {34>>1,I2C_CLIENT_END};
I2C_CLIENT_INSMOD;

static struct i2c_client client_template;

static int saa5249_attach(struct i2c_adapter *adap, int addr, int kind)
{
	int pgbuf;
	int err;
	struct i2c_client *client;
	struct video_device *vd;
	struct saa5249_device *t;

	printk(KERN_INFO "saa5249: teletext chip found.\n");
	client=kmalloc(sizeof(*client), GFP_KERNEL);
	if(client==NULL)
		return -ENOMEM;
	client_template.adapter = adap;
	client_template.addr = addr;
	memcpy(client, &client_template, sizeof(*client));
	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if(t==NULL)
	{
		kfree(client);
		return -ENOMEM;
	}
	strlcpy(client->name, IF_NAME, I2C_NAME_SIZE);
	mutex_init(&t->lock);

	/*
	 *	Now create a video4linux device
	 */

	vd = kmalloc(sizeof(struct video_device), GFP_KERNEL);
	if(vd==NULL)
	{
		kfree(t);
		kfree(client);
		return -ENOMEM;
	}
	i2c_set_clientdata(client, vd);
	memcpy(vd, &saa_template, sizeof(*vd));

	for (pgbuf = 0; pgbuf < NUM_DAUS; pgbuf++)
	{
		memset(t->vdau[pgbuf].pgbuf, ' ', sizeof(t->vdau[0].pgbuf));
		memset(t->vdau[pgbuf].sregs, 0, sizeof(t->vdau[0].sregs));
		memset(t->vdau[pgbuf].laststat, 0, sizeof(t->vdau[0].laststat));
		t->vdau[pgbuf].expire = 0;
		t->vdau[pgbuf].clrfound = TRUE;
		t->vdau[pgbuf].stopped = TRUE;
		t->is_searching[pgbuf] = FALSE;
	}
	vd->priv=t;


	/*
	 *	Register it
	 */

	if((err=video_register_device(vd, VFL_TYPE_VTX,-1))<0)
	{
		kfree(t);
		kfree(vd);
		kfree(client);
		return err;
	}
	t->client = client;
	i2c_attach_client(client);
	return 0;
}

/*
 *	We do most of the hard work when we become a device on the i2c.
 */

static int saa5249_probe(struct i2c_adapter *adap)
{
	if (adap->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adap, &addr_data, saa5249_attach);
	return 0;
}

static int saa5249_detach(struct i2c_client *client)
{
	struct video_device *vd = i2c_get_clientdata(client);
	i2c_detach_client(client);
	video_unregister_device(vd);
	kfree(vd->priv);
	kfree(vd);
	kfree(client);
	return 0;
}

/* new I2C driver support */

static struct i2c_driver i2c_driver_videotext =
{
	.driver = {
		.name 	= IF_NAME,		/* name */
	},
	.id 		= I2C_DRIVERID_SAA5249, /* in i2c.h */
	.attach_adapter = saa5249_probe,
	.detach_client  = saa5249_detach,
};

static struct i2c_client client_template = {
	.driver		= &i2c_driver_videotext,
	.name		= "(unset)",
};

/*
 *	Wait the given number of jiffies (10ms). This calls the scheduler, so the actual
 *	delay may be longer.
 */

static void jdelay(unsigned long delay)
{
	sigset_t oldblocked = current->blocked;

	spin_lock_irq(&current->sighand->siglock);
	sigfillset(&current->blocked);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	msleep_interruptible(jiffies_to_msecs(delay));

	spin_lock_irq(&current->sighand->siglock);
	current->blocked = oldblocked;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
}


/*
 *	I2C interfaces
 */

static int i2c_sendbuf(struct saa5249_device *t, int reg, int count, u8 *data)
{
	char buf[64];

	buf[0] = reg;
	memcpy(buf+1, data, count);

	if(i2c_master_send(t->client, buf, count+1)==count+1)
		return 0;
	return -1;
}

static int i2c_senddata(struct saa5249_device *t, ...)
{
	unsigned char buf[64];
	int v;
	int ct=0;
	va_list argp;
	va_start(argp,t);

	while((v=va_arg(argp,int))!=-1)
		buf[ct++]=v;
	return i2c_sendbuf(t, buf[0], ct-1, buf+1);
}

/* Get count number of bytes from I²C-device at address adr, store them in buf. Start & stop
 * handshaking is done by this routine, ack will be sent after the last byte to inhibit further
 * sending of data. If uaccess is TRUE, data is written to user-space with put_user.
 * Returns -1 if I²C-device didn't send acknowledge, 0 otherwise
 */

static int i2c_getdata(struct saa5249_device *t, int count, u8 *buf)
{
	if(i2c_master_recv(t->client, buf, count)!=count)
		return -1;
	return 0;
}


/*
 *	Standard character-device-driver functions
 */

static int do_saa5249_ioctl(struct inode *inode, struct file *file,
			    unsigned int cmd, void *arg)
{
	static int virtual_mode = FALSE;
	struct video_device *vd = video_devdata(file);
	struct saa5249_device *t=vd->priv;

	switch(cmd)
	{
		case VTXIOCGETINFO:
		{
			vtx_info_t *info = arg;
			info->version_major = VTX_VER_MAJ;
			info->version_minor = VTX_VER_MIN;
			info->numpages = NUM_DAUS;
			/*info->cct_type = CCT_TYPE;*/
			return 0;
		}

		case VTXIOCCLRPAGE:
		{
			vtx_pagereq_t *req = arg;

			if (req->pgbuf < 0 || req->pgbuf >= NUM_DAUS)
				return -EINVAL;
			memset(t->vdau[req->pgbuf].pgbuf, ' ', sizeof(t->vdau[0].pgbuf));
			t->vdau[req->pgbuf].clrfound = TRUE;
			return 0;
		}

		case VTXIOCCLRFOUND:
		{
			vtx_pagereq_t *req = arg;

			if (req->pgbuf < 0 || req->pgbuf >= NUM_DAUS)
				return -EINVAL;
			t->vdau[req->pgbuf].clrfound = TRUE;
			return 0;
		}

		case VTXIOCPAGEREQ:
		{
			vtx_pagereq_t *req = arg;
			if (!(req->pagemask & PGMASK_PAGE))
				req->page = 0;
			if (!(req->pagemask & PGMASK_HOUR))
				req->hour = 0;
			if (!(req->pagemask & PGMASK_MINUTE))
				req->minute = 0;
			if (req->page < 0 || req->page > 0x8ff) /* 7FF ?? */
				return -EINVAL;
			req->page &= 0x7ff;
			if (req->hour < 0 || req->hour > 0x3f || req->minute < 0 || req->minute > 0x7f ||
				req->pagemask < 0 || req->pagemask >= PGMASK_MAX || req->pgbuf < 0 || req->pgbuf >= NUM_DAUS)
				return -EINVAL;
			t->vdau[req->pgbuf].sregs[0] = (req->pagemask & PG_HUND ? 0x10 : 0) | (req->page / 0x100);
			t->vdau[req->pgbuf].sregs[1] = (req->pagemask & PG_TEN ? 0x10 : 0) | ((req->page / 0x10) & 0xf);
			t->vdau[req->pgbuf].sregs[2] = (req->pagemask & PG_UNIT ? 0x10 : 0) | (req->page & 0xf);
			t->vdau[req->pgbuf].sregs[3] = (req->pagemask & HR_TEN ? 0x10 : 0) | (req->hour / 0x10);
			t->vdau[req->pgbuf].sregs[4] = (req->pagemask & HR_UNIT ? 0x10 : 0) | (req->hour & 0xf);
			t->vdau[req->pgbuf].sregs[5] = (req->pagemask & MIN_TEN ? 0x10 : 0) | (req->minute / 0x10);
			t->vdau[req->pgbuf].sregs[6] = (req->pagemask & MIN_UNIT ? 0x10 : 0) | (req->minute & 0xf);
			t->vdau[req->pgbuf].stopped = FALSE;
			t->vdau[req->pgbuf].clrfound = TRUE;
			t->is_searching[req->pgbuf] = TRUE;
			return 0;
		}

		case VTXIOCGETSTAT:
		{
			vtx_pagereq_t *req = arg;
			u8 infobits[10];
			vtx_pageinfo_t info;
			int a;

			if (req->pgbuf < 0 || req->pgbuf >= NUM_DAUS)
				return -EINVAL;
			if (!t->vdau[req->pgbuf].stopped)
			{
				if (i2c_senddata(t, 2, 0, -1) ||
					i2c_sendbuf(t, 3, sizeof(t->vdau[0].sregs), t->vdau[req->pgbuf].sregs) ||
					i2c_senddata(t, 8, 0, 25, 0, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', -1) ||
					i2c_senddata(t, 2, 0, t->vdau[req->pgbuf].sregs[0] | 8, -1) ||
					i2c_senddata(t, 8, 0, 25, 0, -1))
					return -EIO;
				jdelay(PAGE_WAIT);
				if (i2c_getdata(t, 10, infobits))
					return -EIO;

				if (!(infobits[8] & 0x10) && !(infobits[7] & 0xf0) &&	/* check FOUND-bit */
					(memcmp(infobits, t->vdau[req->pgbuf].laststat, sizeof(infobits)) ||
					time_after_eq(jiffies, t->vdau[req->pgbuf].expire)))
				{		/* check if new page arrived */
					if (i2c_senddata(t, 8, 0, 0, 0, -1) ||
						i2c_getdata(t, VTX_PAGESIZE, t->vdau[req->pgbuf].pgbuf))
						return -EIO;
					t->vdau[req->pgbuf].expire = jiffies + PGBUF_EXPIRE;
					memset(t->vdau[req->pgbuf].pgbuf + VTX_PAGESIZE, ' ', VTX_VIRTUALSIZE - VTX_PAGESIZE);
					if (t->virtual_mode)
					{
						/* Packet X/24 */
						if (i2c_senddata(t, 8, 0, 0x20, 0, -1) ||
							i2c_getdata(t, 40, t->vdau[req->pgbuf].pgbuf + VTX_PAGESIZE + 20 * 40))
							return -EIO;
						/* Packet X/27/0 */
						if (i2c_senddata(t, 8, 0, 0x21, 0, -1) ||
							i2c_getdata(t, 40, t->vdau[req->pgbuf].pgbuf + VTX_PAGESIZE + 16 * 40))
							return -EIO;
						/* Packet 8/30/0...8/30/15
						 * FIXME: AFAIK, the 5249 does hamming-decoding for some bytes in packet 8/30,
						 *        so we should undo this here.
						 */
						if (i2c_senddata(t, 8, 0, 0x22, 0, -1) ||
							i2c_getdata(t, 40, t->vdau[req->pgbuf].pgbuf + VTX_PAGESIZE + 23 * 40))
							return -EIO;
					}
					t->vdau[req->pgbuf].clrfound = FALSE;
					memcpy(t->vdau[req->pgbuf].laststat, infobits, sizeof(infobits));
				}
				else
				{
					memcpy(infobits, t->vdau[req->pgbuf].laststat, sizeof(infobits));
				}
			}
			else
			{
				memcpy(infobits, t->vdau[req->pgbuf].laststat, sizeof(infobits));
			}

			info.pagenum = ((infobits[8] << 8) & 0x700) | ((infobits[1] << 4) & 0xf0) | (infobits[0] & 0x0f);
			if (info.pagenum < 0x100)
				info.pagenum += 0x800;
			info.hour = ((infobits[5] << 4) & 0x30) | (infobits[4] & 0x0f);
			info.minute = ((infobits[3] << 4) & 0x70) | (infobits[2] & 0x0f);
			info.charset = ((infobits[7] >> 1) & 7);
			info.delete = !!(infobits[3] & 8);
			info.headline = !!(infobits[5] & 4);
			info.subtitle = !!(infobits[5] & 8);
			info.supp_header = !!(infobits[6] & 1);
			info.update = !!(infobits[6] & 2);
			info.inter_seq = !!(infobits[6] & 4);
			info.dis_disp = !!(infobits[6] & 8);
			info.serial = !!(infobits[7] & 1);
			info.notfound = !!(infobits[8] & 0x10);
			info.pblf = !!(infobits[9] & 0x20);
			info.hamming = 0;
			for (a = 0; a <= 7; a++)
			{
				if (infobits[a] & 0xf0)
				{
					info.hamming = 1;
					break;
				}
			}
			if (t->vdau[req->pgbuf].clrfound)
				info.notfound = 1;
			if(copy_to_user(req->buffer, &info, sizeof(vtx_pageinfo_t)))
				return -EFAULT;
			if (!info.hamming && !info.notfound)
			{
				t->is_searching[req->pgbuf] = FALSE;
			}
			return 0;
		}

		case VTXIOCGETPAGE:
		{
			vtx_pagereq_t *req = arg;
			int start, end;

			if (req->pgbuf < 0 || req->pgbuf >= NUM_DAUS || req->start < 0 ||
				req->start > req->end || req->end >= (virtual_mode ? VTX_VIRTUALSIZE : VTX_PAGESIZE))
				return -EINVAL;
			if(copy_to_user(req->buffer, &t->vdau[req->pgbuf].pgbuf[req->start], req->end - req->start + 1))
				return -EFAULT;

			 /*
			  *	Always read the time directly from SAA5249
			  */

			if (req->start <= 39 && req->end >= 32)
			{
				int len;
				char buf[16];
				start = max(req->start, 32);
				end = min(req->end, 39);
				len=end-start+1;
				if (i2c_senddata(t, 8, 0, 0, start, -1) ||
					i2c_getdata(t, len, buf))
					return -EIO;
				if(copy_to_user(req->buffer+start-req->start, buf, len))
					return -EFAULT;
			}
			/* Insert the current header if DAU is still searching for a page */
			if (req->start <= 31 && req->end >= 7 && t->is_searching[req->pgbuf])
			{
				char buf[32];
				int len;
				start = max(req->start, 7);
				end = min(req->end, 31);
				len=end-start+1;
				if (i2c_senddata(t, 8, 0, 0, start, -1) ||
					i2c_getdata(t, len, buf))
					return -EIO;
				if(copy_to_user(req->buffer+start-req->start, buf, len))
					return -EFAULT;
			}
			return 0;
		}

		case VTXIOCSTOPDAU:
		{
			vtx_pagereq_t *req = arg;

			if (req->pgbuf < 0 || req->pgbuf >= NUM_DAUS)
				return -EINVAL;
			t->vdau[req->pgbuf].stopped = TRUE;
			t->is_searching[req->pgbuf] = FALSE;
			return 0;
		}

		case VTXIOCPUTPAGE:
		case VTXIOCSETDISP:
		case VTXIOCPUTSTAT:
			return 0;

		case VTXIOCCLRCACHE:
		{
			if (i2c_senddata(t, 0, NUM_DAUS, 0, 8, -1) || i2c_senddata(t, 11,
				' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
				' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ', -1))
				return -EIO;
			if (i2c_senddata(t, 3, 0x20, -1))
				return -EIO;
			jdelay(10 * CLEAR_DELAY);			/* I have no idea how long we have to wait here */
			return 0;
		}

		case VTXIOCSETVIRT:
		{
			/* The SAA5249 has virtual-row reception turned on always */
			t->virtual_mode = (int)(long)arg;
			return 0;
		}
	}
	return -EINVAL;
}

/*
 * Translates old vtx IOCTLs to new ones
 *
 * This keeps new kernel versions compatible with old userspace programs.
 */
static inline unsigned int vtx_fix_command(unsigned int cmd)
{
	switch (cmd) {
	case VTXIOCGETINFO_OLD:
		cmd = VTXIOCGETINFO;
		break;
	case VTXIOCCLRPAGE_OLD:
		cmd = VTXIOCCLRPAGE;
		break;
	case VTXIOCCLRFOUND_OLD:
		cmd = VTXIOCCLRFOUND;
		break;
	case VTXIOCPAGEREQ_OLD:
		cmd = VTXIOCPAGEREQ;
		break;
	case VTXIOCGETSTAT_OLD:
		cmd = VTXIOCGETSTAT;
		break;
	case VTXIOCGETPAGE_OLD:
		cmd = VTXIOCGETPAGE;
		break;
	case VTXIOCSTOPDAU_OLD:
		cmd = VTXIOCSTOPDAU;
		break;
	case VTXIOCPUTPAGE_OLD:
		cmd = VTXIOCPUTPAGE;
		break;
	case VTXIOCSETDISP_OLD:
		cmd = VTXIOCSETDISP;
		break;
	case VTXIOCPUTSTAT_OLD:
		cmd = VTXIOCPUTSTAT;
		break;
	case VTXIOCCLRCACHE_OLD:
		cmd = VTXIOCCLRCACHE;
		break;
	case VTXIOCSETVIRT_OLD:
		cmd = VTXIOCSETVIRT;
		break;
	}
	return cmd;
}

/*
 *	Handle the locking
 */

static int saa5249_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct video_device *vd = video_devdata(file);
	struct saa5249_device *t=vd->priv;
	int err;

	cmd = vtx_fix_command(cmd);
	mutex_lock(&t->lock);
	err = video_usercopy(inode,file,cmd,arg,do_saa5249_ioctl);
	mutex_unlock(&t->lock);
	return err;
}

static int saa5249_open(struct inode *inode, struct file *file)
{
	struct video_device *vd = video_devdata(file);
	struct saa5249_device *t=vd->priv;
	int err,pgbuf;

	err = video_exclusive_open(inode,file);
	if (err < 0)
		return err;

	if (t->client==NULL) {
		err = -ENODEV;
		goto fail;
	}

	if (i2c_senddata(t, 0, 0, -1) ||		/* Select R11 */
						/* Turn off parity checks (we do this ourselves) */
		i2c_senddata(t, 1, disp_modes[t->disp_mode][0], 0, -1) ||
						/* Display TV-picture, no virtual rows */
		i2c_senddata(t, 4, NUM_DAUS, disp_modes[t->disp_mode][1], disp_modes[t->disp_mode][2], 7, -1)) /* Set display to page 4 */

	{
		err = -EIO;
		goto fail;
	}

	for (pgbuf = 0; pgbuf < NUM_DAUS; pgbuf++)
	{
		memset(t->vdau[pgbuf].pgbuf, ' ', sizeof(t->vdau[0].pgbuf));
		memset(t->vdau[pgbuf].sregs, 0, sizeof(t->vdau[0].sregs));
		memset(t->vdau[pgbuf].laststat, 0, sizeof(t->vdau[0].laststat));
		t->vdau[pgbuf].expire = 0;
		t->vdau[pgbuf].clrfound = TRUE;
		t->vdau[pgbuf].stopped = TRUE;
		t->is_searching[pgbuf] = FALSE;
	}
	t->virtual_mode=FALSE;
	return 0;

 fail:
	video_exclusive_release(inode,file);
	return err;
}



static int saa5249_release(struct inode *inode, struct file *file)
{
	struct video_device *vd = video_devdata(file);
	struct saa5249_device *t=vd->priv;
	i2c_senddata(t, 1, 0x20, -1);		/* Turn off CCT */
	i2c_senddata(t, 5, 3, 3, -1);		/* Turn off TV-display */
	video_exclusive_release(inode,file);
	return 0;
}

static int __init init_saa_5249 (void)
{
	printk(KERN_INFO "SAA5249 driver (" IF_NAME " interface) for VideoText version %d.%d\n",
			VTX_VER_MAJ, VTX_VER_MIN);
	return i2c_add_driver(&i2c_driver_videotext);
}

static void __exit cleanup_saa_5249 (void)
{
	i2c_del_driver(&i2c_driver_videotext);
}

module_init(init_saa_5249);
module_exit(cleanup_saa_5249);

static struct file_operations saa_fops = {
	.owner		= THIS_MODULE,
	.open		= saa5249_open,
	.release       	= saa5249_release,
	.ioctl          = saa5249_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek         = no_llseek,
};

static struct video_device saa_template =
{
	.owner		= THIS_MODULE,
	.name		= IF_NAME,
	.type		= VID_TYPE_TELETEXT,	/*| VID_TYPE_TUNER ?? */
	.hardware	= VID_HARDWARE_SAA5249,
	.fops           = &saa_fops,
};

MODULE_LICENSE("GPL");
