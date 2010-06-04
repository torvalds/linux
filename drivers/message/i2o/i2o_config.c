/*
 * I2O Configuration Interface Driver
 *
 * (C) Copyright 1999-2002  Red Hat
 *
 * Written by Alan Cox, Building Number Three Ltd
 *
 * Fixes/additions:
 *	Deepak Saxena (04/20/1999):
 *		Added basic ioctl() support
 *	Deepak Saxena (06/07/1999):
 *		Added software download ioctl (still testing)
 *	Auvo Häkkinen (09/10/1999):
 *		Changes to i2o_cfg_reply(), ioctl_parms()
 *		Added ioct_validate()
 *	Taneli Vähäkangas (09/30/1999):
 *		Fixed ioctl_swdl()
 *	Taneli Vähäkangas (10/04/1999):
 *		Changed ioctl_swdl(), implemented ioctl_swul() and ioctl_swdel()
 *	Deepak Saxena (11/18/1999):
 *		Added event managmenet support
 *	Alan Cox <alan@lxorguk.ukuu.org.uk>:
 *		2.4 rewrite ported to 2.5
 *	Markus Lidel <Markus.Lidel@shadowconnect.com>:
 *		Added pass-thru support for Adaptec's raidutils
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/miscdevice.h>
#include <linux/smp_lock.h>
#include <linux/compat.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

#include "core.h"

#define SG_TABLESIZE		30

static long i2o_cfg_ioctl(struct file *, unsigned int, unsigned long);

static spinlock_t i2o_config_lock;

#define MODINC(x,y) ((x) = ((x) + 1) % (y))

struct sg_simple_element {
	u32 flag_count;
	u32 addr_bus;
};

struct i2o_cfg_info {
	struct file *fp;
	struct fasync_struct *fasync;
	struct i2o_evt_info event_q[I2O_EVT_Q_LEN];
	u16 q_in;		// Queue head index
	u16 q_out;		// Queue tail index
	u16 q_len;		// Queue length
	u16 q_lost;		// Number of lost events
	ulong q_id;		// Event queue ID...used as tx_context
	struct i2o_cfg_info *next;
};
static struct i2o_cfg_info *open_files = NULL;
static ulong i2o_cfg_info_id = 0;

static int i2o_cfg_getiops(unsigned long arg)
{
	struct i2o_controller *c;
	u8 __user *user_iop_table = (void __user *)arg;
	u8 tmp[MAX_I2O_CONTROLLERS];
	int ret = 0;

	memset(tmp, 0, MAX_I2O_CONTROLLERS);

	list_for_each_entry(c, &i2o_controllers, list)
	    tmp[c->unit] = 1;

	if (copy_to_user(user_iop_table, tmp, MAX_I2O_CONTROLLERS))
		ret = -EFAULT;

	return ret;
};

static int i2o_cfg_gethrt(unsigned long arg)
{
	struct i2o_controller *c;
	struct i2o_cmd_hrtlct __user *cmd = (struct i2o_cmd_hrtlct __user *)arg;
	struct i2o_cmd_hrtlct kcmd;
	i2o_hrt *hrt;
	int len;
	u32 reslen;
	int ret = 0;

	if (copy_from_user(&kcmd, cmd, sizeof(struct i2o_cmd_hrtlct)))
		return -EFAULT;

	if (get_user(reslen, kcmd.reslen) < 0)
		return -EFAULT;

	if (kcmd.resbuf == NULL)
		return -EFAULT;

	c = i2o_find_iop(kcmd.iop);
	if (!c)
		return -ENXIO;

	hrt = (i2o_hrt *) c->hrt.virt;

	len = 8 + ((hrt->entry_len * hrt->num_entries) << 2);

	/* We did a get user...so assuming mem is ok...is this bad? */
	put_user(len, kcmd.reslen);
	if (len > reslen)
		ret = -ENOBUFS;
	if (copy_to_user(kcmd.resbuf, (void *)hrt, len))
		ret = -EFAULT;

	return ret;
};

static int i2o_cfg_getlct(unsigned long arg)
{
	struct i2o_controller *c;
	struct i2o_cmd_hrtlct __user *cmd = (struct i2o_cmd_hrtlct __user *)arg;
	struct i2o_cmd_hrtlct kcmd;
	i2o_lct *lct;
	int len;
	int ret = 0;
	u32 reslen;

	if (copy_from_user(&kcmd, cmd, sizeof(struct i2o_cmd_hrtlct)))
		return -EFAULT;

	if (get_user(reslen, kcmd.reslen) < 0)
		return -EFAULT;

	if (kcmd.resbuf == NULL)
		return -EFAULT;

	c = i2o_find_iop(kcmd.iop);
	if (!c)
		return -ENXIO;

	lct = (i2o_lct *) c->lct;

	len = (unsigned int)lct->table_size << 2;
	put_user(len, kcmd.reslen);
	if (len > reslen)
		ret = -ENOBUFS;
	else if (copy_to_user(kcmd.resbuf, lct, len))
		ret = -EFAULT;

	return ret;
};

static int i2o_cfg_parms(unsigned long arg, unsigned int type)
{
	int ret = 0;
	struct i2o_controller *c;
	struct i2o_device *dev;
	struct i2o_cmd_psetget __user *cmd =
	    (struct i2o_cmd_psetget __user *)arg;
	struct i2o_cmd_psetget kcmd;
	u32 reslen;
	u8 *ops;
	u8 *res;
	int len = 0;

	u32 i2o_cmd = (type == I2OPARMGET ?
		       I2O_CMD_UTIL_PARAMS_GET : I2O_CMD_UTIL_PARAMS_SET);

	if (copy_from_user(&kcmd, cmd, sizeof(struct i2o_cmd_psetget)))
		return -EFAULT;

	if (get_user(reslen, kcmd.reslen))
		return -EFAULT;

	c = i2o_find_iop(kcmd.iop);
	if (!c)
		return -ENXIO;

	dev = i2o_iop_find_device(c, kcmd.tid);
	if (!dev)
		return -ENXIO;

	ops = memdup_user(kcmd.opbuf, kcmd.oplen);
	if (IS_ERR(ops))
		return PTR_ERR(ops);

	/*
	 * It's possible to have a _very_ large table
	 * and that the user asks for all of it at once...
	 */
	res = kmalloc(65536, GFP_KERNEL);
	if (!res) {
		kfree(ops);
		return -ENOMEM;
	}

	len = i2o_parm_issue(dev, i2o_cmd, ops, kcmd.oplen, res, 65536);
	kfree(ops);

	if (len < 0) {
		kfree(res);
		return -EAGAIN;
	}

	put_user(len, kcmd.reslen);
	if (len > reslen)
		ret = -ENOBUFS;
	else if (copy_to_user(kcmd.resbuf, res, len))
		ret = -EFAULT;

	kfree(res);

	return ret;
};

static int i2o_cfg_swdl(unsigned long arg)
{
	struct i2o_sw_xfer kxfer;
	struct i2o_sw_xfer __user *pxfer = (struct i2o_sw_xfer __user *)arg;
	unsigned char maxfrag = 0, curfrag = 1;
	struct i2o_dma buffer;
	struct i2o_message *msg;
	unsigned int status = 0, swlen = 0, fragsize = 8192;
	struct i2o_controller *c;

	if (copy_from_user(&kxfer, pxfer, sizeof(struct i2o_sw_xfer)))
		return -EFAULT;

	if (get_user(swlen, kxfer.swlen) < 0)
		return -EFAULT;

	if (get_user(maxfrag, kxfer.maxfrag) < 0)
		return -EFAULT;

	if (get_user(curfrag, kxfer.curfrag) < 0)
		return -EFAULT;

	if (curfrag == maxfrag)
		fragsize = swlen - (maxfrag - 1) * 8192;

	if (!kxfer.buf || !access_ok(VERIFY_READ, kxfer.buf, fragsize))
		return -EFAULT;

	c = i2o_find_iop(kxfer.iop);
	if (!c)
		return -ENXIO;

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if (i2o_dma_alloc(&c->pdev->dev, &buffer, fragsize)) {
		i2o_msg_nop(c, msg);
		return -ENOMEM;
	}

	if (__copy_from_user(buffer.virt, kxfer.buf, fragsize)) {
		i2o_msg_nop(c, msg);
		i2o_dma_free(&c->pdev->dev, &buffer);
		return -EFAULT;
	}

	msg->u.head[0] = cpu_to_le32(NINE_WORD_MSG_SIZE | SGL_OFFSET_7);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_SW_DOWNLOAD << 24 | HOST_TID << 12 |
			ADAPTER_TID);
	msg->u.head[2] = cpu_to_le32(i2o_config_driver.context);
	msg->u.head[3] = cpu_to_le32(0);
	msg->body[0] =
	    cpu_to_le32((((u32) kxfer.flags) << 24) | (((u32) kxfer.
							sw_type) << 16) |
			(((u32) maxfrag) << 8) | (((u32) curfrag)));
	msg->body[1] = cpu_to_le32(swlen);
	msg->body[2] = cpu_to_le32(kxfer.sw_id);
	msg->body[3] = cpu_to_le32(0xD0000000 | fragsize);
	msg->body[4] = cpu_to_le32(buffer.phys);

	osm_debug("swdl frag %d/%d (size %d)\n", curfrag, maxfrag, fragsize);
	status = i2o_msg_post_wait_mem(c, msg, 60, &buffer);

	if (status != -ETIMEDOUT)
		i2o_dma_free(&c->pdev->dev, &buffer);

	if (status != I2O_POST_WAIT_OK) {
		// it fails if you try and send frags out of order
		// and for some yet unknown reasons too
		osm_info("swdl failed, DetailedStatus = %d\n", status);
		return status;
	}

	return 0;
};

static int i2o_cfg_swul(unsigned long arg)
{
	struct i2o_sw_xfer kxfer;
	struct i2o_sw_xfer __user *pxfer = (struct i2o_sw_xfer __user *)arg;
	unsigned char maxfrag = 0, curfrag = 1;
	struct i2o_dma buffer;
	struct i2o_message *msg;
	unsigned int status = 0, swlen = 0, fragsize = 8192;
	struct i2o_controller *c;
	int ret = 0;

	if (copy_from_user(&kxfer, pxfer, sizeof(struct i2o_sw_xfer)))
		return -EFAULT;

	if (get_user(swlen, kxfer.swlen) < 0)
		return -EFAULT;

	if (get_user(maxfrag, kxfer.maxfrag) < 0)
		return -EFAULT;

	if (get_user(curfrag, kxfer.curfrag) < 0)
		return -EFAULT;

	if (curfrag == maxfrag)
		fragsize = swlen - (maxfrag - 1) * 8192;

	if (!kxfer.buf)
		return -EFAULT;

	c = i2o_find_iop(kxfer.iop);
	if (!c)
		return -ENXIO;

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if (i2o_dma_alloc(&c->pdev->dev, &buffer, fragsize)) {
		i2o_msg_nop(c, msg);
		return -ENOMEM;
	}

	msg->u.head[0] = cpu_to_le32(NINE_WORD_MSG_SIZE | SGL_OFFSET_7);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_SW_UPLOAD << 24 | HOST_TID << 12 | ADAPTER_TID);
	msg->u.head[2] = cpu_to_le32(i2o_config_driver.context);
	msg->u.head[3] = cpu_to_le32(0);
	msg->body[0] =
	    cpu_to_le32((u32) kxfer.flags << 24 | (u32) kxfer.
			sw_type << 16 | (u32) maxfrag << 8 | (u32) curfrag);
	msg->body[1] = cpu_to_le32(swlen);
	msg->body[2] = cpu_to_le32(kxfer.sw_id);
	msg->body[3] = cpu_to_le32(0xD0000000 | fragsize);
	msg->body[4] = cpu_to_le32(buffer.phys);

	osm_debug("swul frag %d/%d (size %d)\n", curfrag, maxfrag, fragsize);
	status = i2o_msg_post_wait_mem(c, msg, 60, &buffer);

	if (status != I2O_POST_WAIT_OK) {
		if (status != -ETIMEDOUT)
			i2o_dma_free(&c->pdev->dev, &buffer);

		osm_info("swul failed, DetailedStatus = %d\n", status);
		return status;
	}

	if (copy_to_user(kxfer.buf, buffer.virt, fragsize))
		ret = -EFAULT;

	i2o_dma_free(&c->pdev->dev, &buffer);

	return ret;
}

static int i2o_cfg_swdel(unsigned long arg)
{
	struct i2o_controller *c;
	struct i2o_sw_xfer kxfer;
	struct i2o_sw_xfer __user *pxfer = (struct i2o_sw_xfer __user *)arg;
	struct i2o_message *msg;
	unsigned int swlen;
	int token;

	if (copy_from_user(&kxfer, pxfer, sizeof(struct i2o_sw_xfer)))
		return -EFAULT;

	if (get_user(swlen, kxfer.swlen) < 0)
		return -EFAULT;

	c = i2o_find_iop(kxfer.iop);
	if (!c)
		return -ENXIO;

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->u.head[0] = cpu_to_le32(SEVEN_WORD_MSG_SIZE | SGL_OFFSET_0);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_SW_REMOVE << 24 | HOST_TID << 12 | ADAPTER_TID);
	msg->u.head[2] = cpu_to_le32(i2o_config_driver.context);
	msg->u.head[3] = cpu_to_le32(0);
	msg->body[0] =
	    cpu_to_le32((u32) kxfer.flags << 24 | (u32) kxfer.sw_type << 16);
	msg->body[1] = cpu_to_le32(swlen);
	msg->body[2] = cpu_to_le32(kxfer.sw_id);

	token = i2o_msg_post_wait(c, msg, 10);

	if (token != I2O_POST_WAIT_OK) {
		osm_info("swdel failed, DetailedStatus = %d\n", token);
		return -ETIMEDOUT;
	}

	return 0;
};

static int i2o_cfg_validate(unsigned long arg)
{
	int token;
	int iop = (int)arg;
	struct i2o_message *msg;
	struct i2o_controller *c;

	c = i2o_find_iop(iop);
	if (!c)
		return -ENXIO;

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->u.head[0] = cpu_to_le32(FOUR_WORD_MSG_SIZE | SGL_OFFSET_0);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_CONFIG_VALIDATE << 24 | HOST_TID << 12 | iop);
	msg->u.head[2] = cpu_to_le32(i2o_config_driver.context);
	msg->u.head[3] = cpu_to_le32(0);

	token = i2o_msg_post_wait(c, msg, 10);

	if (token != I2O_POST_WAIT_OK) {
		osm_info("Can't validate configuration, ErrorStatus = %d\n",
			 token);
		return -ETIMEDOUT;
	}

	return 0;
};

static int i2o_cfg_evt_reg(unsigned long arg, struct file *fp)
{
	struct i2o_message *msg;
	struct i2o_evt_id __user *pdesc = (struct i2o_evt_id __user *)arg;
	struct i2o_evt_id kdesc;
	struct i2o_controller *c;
	struct i2o_device *d;

	if (copy_from_user(&kdesc, pdesc, sizeof(struct i2o_evt_id)))
		return -EFAULT;

	/* IOP exists? */
	c = i2o_find_iop(kdesc.iop);
	if (!c)
		return -ENXIO;

	/* Device exists? */
	d = i2o_iop_find_device(c, kdesc.tid);
	if (!d)
		return -ENODEV;

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->u.head[0] = cpu_to_le32(FOUR_WORD_MSG_SIZE | SGL_OFFSET_0);
	msg->u.head[1] =
	    cpu_to_le32(I2O_CMD_UTIL_EVT_REGISTER << 24 | HOST_TID << 12 |
			kdesc.tid);
	msg->u.head[2] = cpu_to_le32(i2o_config_driver.context);
	msg->u.head[3] = cpu_to_le32(i2o_cntxt_list_add(c, fp->private_data));
	msg->body[0] = cpu_to_le32(kdesc.evt_mask);

	i2o_msg_post(c, msg);

	return 0;
}

static int i2o_cfg_evt_get(unsigned long arg, struct file *fp)
{
	struct i2o_cfg_info *p = NULL;
	struct i2o_evt_get __user *uget = (struct i2o_evt_get __user *)arg;
	struct i2o_evt_get kget;
	unsigned long flags;

	for (p = open_files; p; p = p->next)
		if (p->q_id == (ulong) fp->private_data)
			break;

	if (!p->q_len)
		return -ENOENT;

	memcpy(&kget.info, &p->event_q[p->q_out], sizeof(struct i2o_evt_info));
	MODINC(p->q_out, I2O_EVT_Q_LEN);
	spin_lock_irqsave(&i2o_config_lock, flags);
	p->q_len--;
	kget.pending = p->q_len;
	kget.lost = p->q_lost;
	spin_unlock_irqrestore(&i2o_config_lock, flags);

	if (copy_to_user(uget, &kget, sizeof(struct i2o_evt_get)))
		return -EFAULT;
	return 0;
}

#ifdef CONFIG_COMPAT
static int i2o_cfg_passthru32(struct file *file, unsigned cmnd,
			      unsigned long arg)
{
	struct i2o_cmd_passthru32 __user *cmd;
	struct i2o_controller *c;
	u32 __user *user_msg;
	u32 *reply = NULL;
	u32 __user *user_reply = NULL;
	u32 size = 0;
	u32 reply_size = 0;
	u32 rcode = 0;
	struct i2o_dma sg_list[SG_TABLESIZE];
	u32 sg_offset = 0;
	u32 sg_count = 0;
	u32 i = 0;
	u32 sg_index = 0;
	i2o_status_block *sb;
	struct i2o_message *msg;
	unsigned int iop;

	cmd = (struct i2o_cmd_passthru32 __user *)arg;

	if (get_user(iop, &cmd->iop) || get_user(i, &cmd->msg))
		return -EFAULT;

	user_msg = compat_ptr(i);

	c = i2o_find_iop(iop);
	if (!c) {
		osm_debug("controller %d not found\n", iop);
		return -ENXIO;
	}

	sb = c->status_block.virt;

	if (get_user(size, &user_msg[0])) {
		osm_warn("unable to get size!\n");
		return -EFAULT;
	}
	size = size >> 16;

	if (size > sb->inbound_frame_size) {
		osm_warn("size of message > inbound_frame_size");
		return -EFAULT;
	}

	user_reply = &user_msg[size];

	size <<= 2;		// Convert to bytes

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	rcode = -EFAULT;
	/* Copy in the user's I2O command */
	if (copy_from_user(msg, user_msg, size)) {
		osm_warn("unable to copy user message\n");
		goto out;
	}
	i2o_dump_message(msg);

	if (get_user(reply_size, &user_reply[0]) < 0)
		goto out;

	reply_size >>= 16;
	reply_size <<= 2;

	rcode = -ENOMEM;
	reply = kzalloc(reply_size, GFP_KERNEL);
	if (!reply) {
		printk(KERN_WARNING "%s: Could not allocate reply buffer\n",
		       c->name);
		goto out;
	}

	sg_offset = (msg->u.head[0] >> 4) & 0x0f;

	memset(sg_list, 0, sizeof(sg_list[0]) * SG_TABLESIZE);
	if (sg_offset) {
		struct sg_simple_element *sg;

		if (sg_offset * 4 >= size) {
			rcode = -EFAULT;
			goto cleanup;
		}
		// TODO 64bit fix
		sg = (struct sg_simple_element *)((&msg->u.head[0]) +
						  sg_offset);
		sg_count =
		    (size - sg_offset * 4) / sizeof(struct sg_simple_element);
		if (sg_count > SG_TABLESIZE) {
			printk(KERN_DEBUG "%s:IOCTL SG List too large (%u)\n",
			       c->name, sg_count);
			rcode = -EINVAL;
			goto cleanup;
		}

		for (i = 0; i < sg_count; i++) {
			int sg_size;
			struct i2o_dma *p;

			if (!(sg[i].flag_count & 0x10000000
			      /*I2O_SGL_FLAGS_SIMPLE_ADDRESS_ELEMENT */ )) {
				printk(KERN_DEBUG
				       "%s:Bad SG element %d - not simple (%x)\n",
				       c->name, i, sg[i].flag_count);
				rcode = -EINVAL;
				goto cleanup;
			}
			sg_size = sg[i].flag_count & 0xffffff;
			p = &(sg_list[sg_index]);
			/* Allocate memory for the transfer */
			if (i2o_dma_alloc(&c->pdev->dev, p, sg_size)) {
				printk(KERN_DEBUG
				       "%s: Could not allocate SG buffer - size = %d buffer number %d of %d\n",
				       c->name, sg_size, i, sg_count);
				rcode = -ENOMEM;
				goto sg_list_cleanup;
			}
			sg_index++;
			/* Copy in the user's SG buffer if necessary */
			if (sg[i].
			    flag_count & 0x04000000 /*I2O_SGL_FLAGS_DIR */ ) {
				// TODO 64bit fix
				if (copy_from_user
				    (p->virt,
				     (void __user *)(unsigned long)sg[i].
				     addr_bus, sg_size)) {
					printk(KERN_DEBUG
					       "%s: Could not copy SG buf %d FROM user\n",
					       c->name, i);
					rcode = -EFAULT;
					goto sg_list_cleanup;
				}
			}
			//TODO 64bit fix
			sg[i].addr_bus = (u32) p->phys;
		}
	}

	rcode = i2o_msg_post_wait(c, msg, 60);
	msg = NULL;
	if (rcode) {
		reply[4] = ((u32) rcode) << 24;
		goto sg_list_cleanup;
	}

	if (sg_offset) {
		u32 rmsg[I2O_OUTBOUND_MSG_FRAME_SIZE];
		/* Copy back the Scatter Gather buffers back to user space */
		u32 j;
		// TODO 64bit fix
		struct sg_simple_element *sg;
		int sg_size;

		// re-acquire the original message to handle correctly the sg copy operation
		memset(&rmsg, 0, I2O_OUTBOUND_MSG_FRAME_SIZE * 4);
		// get user msg size in u32s
		if (get_user(size, &user_msg[0])) {
			rcode = -EFAULT;
			goto sg_list_cleanup;
		}
		size = size >> 16;
		size *= 4;
		/* Copy in the user's I2O command */
		if (copy_from_user(rmsg, user_msg, size)) {
			rcode = -EFAULT;
			goto sg_list_cleanup;
		}
		sg_count =
		    (size - sg_offset * 4) / sizeof(struct sg_simple_element);

		// TODO 64bit fix
		sg = (struct sg_simple_element *)(rmsg + sg_offset);
		for (j = 0; j < sg_count; j++) {
			/* Copy out the SG list to user's buffer if necessary */
			if (!
			    (sg[j].
			     flag_count & 0x4000000 /*I2O_SGL_FLAGS_DIR */ )) {
				sg_size = sg[j].flag_count & 0xffffff;
				// TODO 64bit fix
				if (copy_to_user
				    ((void __user *)(u64) sg[j].addr_bus,
				     sg_list[j].virt, sg_size)) {
					printk(KERN_WARNING
					       "%s: Could not copy %p TO user %x\n",
					       c->name, sg_list[j].virt,
					       sg[j].addr_bus);
					rcode = -EFAULT;
					goto sg_list_cleanup;
				}
			}
		}
	}

sg_list_cleanup:
	/* Copy back the reply to user space */
	if (reply_size) {
		// we wrote our own values for context - now restore the user supplied ones
		if (copy_from_user(reply + 2, user_msg + 2, sizeof(u32) * 2)) {
			printk(KERN_WARNING
			       "%s: Could not copy message context FROM user\n",
			       c->name);
			rcode = -EFAULT;
		}
		if (copy_to_user(user_reply, reply, reply_size)) {
			printk(KERN_WARNING
			       "%s: Could not copy reply TO user\n", c->name);
			rcode = -EFAULT;
		}
	}
	for (i = 0; i < sg_index; i++)
		i2o_dma_free(&c->pdev->dev, &sg_list[i]);

cleanup:
	kfree(reply);
out:
	if (msg)
		i2o_msg_nop(c, msg);
	return rcode;
}

static long i2o_cfg_compat_ioctl(struct file *file, unsigned cmd,
				 unsigned long arg)
{
	int ret;
	lock_kernel();
	switch (cmd) {
	case I2OGETIOPS:
		ret = i2o_cfg_ioctl(file, cmd, arg);
		break;
	case I2OPASSTHRU32:
		ret = i2o_cfg_passthru32(file, cmd, arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	unlock_kernel();
	return ret;
}

#endif

#ifdef CONFIG_I2O_EXT_ADAPTEC
static int i2o_cfg_passthru(unsigned long arg)
{
	struct i2o_cmd_passthru __user *cmd =
	    (struct i2o_cmd_passthru __user *)arg;
	struct i2o_controller *c;
	u32 __user *user_msg;
	u32 *reply = NULL;
	u32 __user *user_reply = NULL;
	u32 size = 0;
	u32 reply_size = 0;
	u32 rcode = 0;
	struct i2o_dma sg_list[SG_TABLESIZE];
	u32 sg_offset = 0;
	u32 sg_count = 0;
	int sg_index = 0;
	u32 i = 0;
	i2o_status_block *sb;
	struct i2o_message *msg;
	unsigned int iop;

	if (get_user(iop, &cmd->iop) || get_user(user_msg, &cmd->msg))
		return -EFAULT;

	c = i2o_find_iop(iop);
	if (!c) {
		osm_warn("controller %d not found\n", iop);
		return -ENXIO;
	}

	sb = c->status_block.virt;

	if (get_user(size, &user_msg[0]))
		return -EFAULT;
	size = size >> 16;

	if (size > sb->inbound_frame_size) {
		osm_warn("size of message > inbound_frame_size");
		return -EFAULT;
	}

	user_reply = &user_msg[size];

	size <<= 2;		// Convert to bytes

	msg = i2o_msg_get_wait(c, I2O_TIMEOUT_MESSAGE_GET);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	rcode = -EFAULT;
	/* Copy in the user's I2O command */
	if (copy_from_user(msg, user_msg, size))
		goto out;

	if (get_user(reply_size, &user_reply[0]) < 0)
		goto out;

	reply_size >>= 16;
	reply_size <<= 2;

	reply = kzalloc(reply_size, GFP_KERNEL);
	if (!reply) {
		printk(KERN_WARNING "%s: Could not allocate reply buffer\n",
		       c->name);
		rcode = -ENOMEM;
		goto out;
	}

	sg_offset = (msg->u.head[0] >> 4) & 0x0f;

	memset(sg_list, 0, sizeof(sg_list[0]) * SG_TABLESIZE);
	if (sg_offset) {
		struct sg_simple_element *sg;
		struct i2o_dma *p;

		if (sg_offset * 4 >= size) {
			rcode = -EFAULT;
			goto cleanup;
		}
		// TODO 64bit fix
		sg = (struct sg_simple_element *)((&msg->u.head[0]) +
						  sg_offset);
		sg_count =
		    (size - sg_offset * 4) / sizeof(struct sg_simple_element);
		if (sg_count > SG_TABLESIZE) {
			printk(KERN_DEBUG "%s:IOCTL SG List too large (%u)\n",
			       c->name, sg_count);
			rcode = -EINVAL;
			goto cleanup;
		}

		for (i = 0; i < sg_count; i++) {
			int sg_size;

			if (!(sg[i].flag_count & 0x10000000
			      /*I2O_SGL_FLAGS_SIMPLE_ADDRESS_ELEMENT */ )) {
				printk(KERN_DEBUG
				       "%s:Bad SG element %d - not simple (%x)\n",
				       c->name, i, sg[i].flag_count);
				rcode = -EINVAL;
				goto sg_list_cleanup;
			}
			sg_size = sg[i].flag_count & 0xffffff;
			p = &(sg_list[sg_index]);
			if (i2o_dma_alloc(&c->pdev->dev, p, sg_size)) {
			/* Allocate memory for the transfer */
				printk(KERN_DEBUG
				       "%s: Could not allocate SG buffer - size = %d buffer number %d of %d\n",
				       c->name, sg_size, i, sg_count);
				rcode = -ENOMEM;
				goto sg_list_cleanup;
			}
			sg_index++;
			/* Copy in the user's SG buffer if necessary */
			if (sg[i].
			    flag_count & 0x04000000 /*I2O_SGL_FLAGS_DIR */ ) {
				// TODO 64bit fix
				if (copy_from_user
				    (p->virt, (void __user *)sg[i].addr_bus,
				     sg_size)) {
					printk(KERN_DEBUG
					       "%s: Could not copy SG buf %d FROM user\n",
					       c->name, i);
					rcode = -EFAULT;
					goto sg_list_cleanup;
				}
			}
			sg[i].addr_bus = p->phys;
		}
	}

	rcode = i2o_msg_post_wait(c, msg, 60);
	msg = NULL;
	if (rcode) {
		reply[4] = ((u32) rcode) << 24;
		goto sg_list_cleanup;
	}

	if (sg_offset) {
		u32 rmsg[I2O_OUTBOUND_MSG_FRAME_SIZE];
		/* Copy back the Scatter Gather buffers back to user space */
		u32 j;
		// TODO 64bit fix
		struct sg_simple_element *sg;
		int sg_size;

		// re-acquire the original message to handle correctly the sg copy operation
		memset(&rmsg, 0, I2O_OUTBOUND_MSG_FRAME_SIZE * 4);
		// get user msg size in u32s
		if (get_user(size, &user_msg[0])) {
			rcode = -EFAULT;
			goto sg_list_cleanup;
		}
		size = size >> 16;
		size *= 4;
		/* Copy in the user's I2O command */
		if (copy_from_user(rmsg, user_msg, size)) {
			rcode = -EFAULT;
			goto sg_list_cleanup;
		}
		sg_count =
		    (size - sg_offset * 4) / sizeof(struct sg_simple_element);

		// TODO 64bit fix
		sg = (struct sg_simple_element *)(rmsg + sg_offset);
		for (j = 0; j < sg_count; j++) {
			/* Copy out the SG list to user's buffer if necessary */
			if (!
			    (sg[j].
			     flag_count & 0x4000000 /*I2O_SGL_FLAGS_DIR */ )) {
				sg_size = sg[j].flag_count & 0xffffff;
				// TODO 64bit fix
				if (copy_to_user
				    ((void __user *)sg[j].addr_bus, sg_list[j].virt,
				     sg_size)) {
					printk(KERN_WARNING
					       "%s: Could not copy %p TO user %x\n",
					       c->name, sg_list[j].virt,
					       sg[j].addr_bus);
					rcode = -EFAULT;
					goto sg_list_cleanup;
				}
			}
		}
	}

sg_list_cleanup:
	/* Copy back the reply to user space */
	if (reply_size) {
		// we wrote our own values for context - now restore the user supplied ones
		if (copy_from_user(reply + 2, user_msg + 2, sizeof(u32) * 2)) {
			printk(KERN_WARNING
			       "%s: Could not copy message context FROM user\n",
			       c->name);
			rcode = -EFAULT;
		}
		if (copy_to_user(user_reply, reply, reply_size)) {
			printk(KERN_WARNING
			       "%s: Could not copy reply TO user\n", c->name);
			rcode = -EFAULT;
		}
	}

	for (i = 0; i < sg_index; i++)
		i2o_dma_free(&c->pdev->dev, &sg_list[i]);

cleanup:
	kfree(reply);
out:
	if (msg)
		i2o_msg_nop(c, msg);
	return rcode;
}
#endif

/*
 * IOCTL Handler
 */
static long i2o_cfg_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	int ret;

	lock_kernel();
	switch (cmd) {
	case I2OGETIOPS:
		ret = i2o_cfg_getiops(arg);
		break;

	case I2OHRTGET:
		ret = i2o_cfg_gethrt(arg);
		break;

	case I2OLCTGET:
		ret = i2o_cfg_getlct(arg);
		break;

	case I2OPARMSET:
		ret = i2o_cfg_parms(arg, I2OPARMSET);
		break;

	case I2OPARMGET:
		ret = i2o_cfg_parms(arg, I2OPARMGET);
		break;

	case I2OSWDL:
		ret = i2o_cfg_swdl(arg);
		break;

	case I2OSWUL:
		ret = i2o_cfg_swul(arg);
		break;

	case I2OSWDEL:
		ret = i2o_cfg_swdel(arg);
		break;

	case I2OVALIDATE:
		ret = i2o_cfg_validate(arg);
		break;

	case I2OEVTREG:
		ret = i2o_cfg_evt_reg(arg, fp);
		break;

	case I2OEVTGET:
		ret = i2o_cfg_evt_get(arg, fp);
		break;

#ifdef CONFIG_I2O_EXT_ADAPTEC
	case I2OPASSTHRU:
		ret = i2o_cfg_passthru(arg);
		break;
#endif

	default:
		osm_debug("unknown ioctl called!\n");
		ret = -EINVAL;
	}
	unlock_kernel();
	return ret;
}

static int cfg_open(struct inode *inode, struct file *file)
{
	struct i2o_cfg_info *tmp =
	    (struct i2o_cfg_info *)kmalloc(sizeof(struct i2o_cfg_info),
					   GFP_KERNEL);
	unsigned long flags;

	if (!tmp)
		return -ENOMEM;

	lock_kernel();
	file->private_data = (void *)(i2o_cfg_info_id++);
	tmp->fp = file;
	tmp->fasync = NULL;
	tmp->q_id = (ulong) file->private_data;
	tmp->q_len = 0;
	tmp->q_in = 0;
	tmp->q_out = 0;
	tmp->q_lost = 0;
	tmp->next = open_files;

	spin_lock_irqsave(&i2o_config_lock, flags);
	open_files = tmp;
	spin_unlock_irqrestore(&i2o_config_lock, flags);
	unlock_kernel();

	return 0;
}

static int cfg_fasync(int fd, struct file *fp, int on)
{
	ulong id = (ulong) fp->private_data;
	struct i2o_cfg_info *p;
	int ret = -EBADF;

	lock_kernel();
	for (p = open_files; p; p = p->next)
		if (p->q_id == id)
			break;

	if (p)
		ret = fasync_helper(fd, fp, on, &p->fasync);
	unlock_kernel();
	return ret;
}

static int cfg_release(struct inode *inode, struct file *file)
{
	ulong id = (ulong) file->private_data;
	struct i2o_cfg_info *p, **q;
	unsigned long flags;

	lock_kernel();
	spin_lock_irqsave(&i2o_config_lock, flags);
	for (q = &open_files; (p = *q) != NULL; q = &p->next) {
		if (p->q_id == id) {
			*q = p->next;
			kfree(p);
			break;
		}
	}
	spin_unlock_irqrestore(&i2o_config_lock, flags);
	unlock_kernel();

	return 0;
}

static const struct file_operations config_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.unlocked_ioctl = i2o_cfg_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = i2o_cfg_compat_ioctl,
#endif
	.open = cfg_open,
	.release = cfg_release,
	.fasync = cfg_fasync,
};

static struct miscdevice i2o_miscdev = {
	I2O_MINOR,
	"i2octl",
	&config_fops
};

static int __init i2o_config_old_init(void)
{
	spin_lock_init(&i2o_config_lock);

	if (misc_register(&i2o_miscdev) < 0) {
		osm_err("can't register device.\n");
		return -EBUSY;
	}

	return 0;
}

static void i2o_config_old_exit(void)
{
	misc_deregister(&i2o_miscdev);
}

MODULE_AUTHOR("Red Hat Software");
