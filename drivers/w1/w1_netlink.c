/*
 * w1_netlink.c
 *
 * Copyright (c) 2003 Evgeniy Polyakov <zbr@ioremap.net>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/connector.h>

#include "w1.h"
#include "w1_log.h"
#include "w1_netlink.h"

#if defined(CONFIG_W1_CON) && (defined(CONFIG_CONNECTOR) || (defined(CONFIG_CONNECTOR_MODULE) && defined(CONFIG_W1_MODULE)))
void w1_netlink_send(struct w1_master *dev, struct w1_netlink_msg *msg)
{
	char buf[sizeof(struct cn_msg) + sizeof(struct w1_netlink_msg)];
	struct cn_msg *m = (struct cn_msg *)buf;
	struct w1_netlink_msg *w = (struct w1_netlink_msg *)(m+1);

	memset(buf, 0, sizeof(buf));

	m->id.idx = CN_W1_IDX;
	m->id.val = CN_W1_VAL;

	m->seq = dev->seq++;
	m->len = sizeof(struct w1_netlink_msg);

	memcpy(w, msg, sizeof(struct w1_netlink_msg));

	cn_netlink_send(m, dev->portid, 0, GFP_KERNEL);
}

static void w1_send_slave(struct w1_master *dev, u64 rn)
{
	struct cn_msg *msg = dev->priv;
	struct w1_netlink_msg *hdr = (struct w1_netlink_msg *)(msg + 1);
	struct w1_netlink_cmd *cmd = (struct w1_netlink_cmd *)(hdr + 1);
	int avail;
	u64 *data;

	avail = dev->priv_size - cmd->len;

	if (avail < 8) {
		msg->ack++;
		cn_netlink_send(msg, dev->portid, 0, GFP_KERNEL);

		msg->len = sizeof(struct w1_netlink_msg) +
			sizeof(struct w1_netlink_cmd);
		hdr->len = sizeof(struct w1_netlink_cmd);
		cmd->len = 0;
	}

	data = (void *)(cmd + 1) + cmd->len;

	*data = rn;
	cmd->len += 8;
	hdr->len += 8;
	msg->len += 8;
}

static void w1_found_send_slave(struct w1_master *dev, u64 rn)
{
	/* update kernel slave list */
	w1_slave_found(dev, rn);

	w1_send_slave(dev, rn);
}

/* Get the current slave list, or search (with or without alarm) */
static int w1_get_slaves(struct w1_master *dev,
		struct cn_msg *req_msg, struct w1_netlink_msg *req_hdr,
		struct w1_netlink_cmd *req_cmd)
{
	struct cn_msg *msg;
	struct w1_netlink_msg *hdr;
	struct w1_netlink_cmd *cmd;
	struct w1_slave *sl;

	msg = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->id = req_msg->id;
	msg->seq = req_msg->seq;
	msg->ack = 0;
	msg->len = sizeof(struct w1_netlink_msg) +
		sizeof(struct w1_netlink_cmd);

	hdr = (struct w1_netlink_msg *)(msg + 1);
	cmd = (struct w1_netlink_cmd *)(hdr + 1);

	hdr->type = W1_MASTER_CMD;
	hdr->id = req_hdr->id;
	hdr->len = sizeof(struct w1_netlink_cmd);

	cmd->cmd = req_cmd->cmd;
	cmd->len = 0;

	dev->priv = msg;
	dev->priv_size = PAGE_SIZE - msg->len - sizeof(struct cn_msg);

	if (req_cmd->cmd == W1_CMD_LIST_SLAVES) {
		__u64 rn;
		mutex_lock(&dev->list_mutex);
		list_for_each_entry(sl, &dev->slist, w1_slave_entry) {
			memcpy(&rn, &sl->reg_num, sizeof(rn));
			w1_send_slave(dev, rn);
		}
		mutex_unlock(&dev->list_mutex);
	} else {
		w1_search_process_cb(dev, cmd->cmd == W1_CMD_ALARM_SEARCH ?
			W1_ALARM_SEARCH : W1_SEARCH, w1_found_send_slave);
	}

	msg->ack = 0;
	cn_netlink_send(msg, dev->portid, 0, GFP_KERNEL);

	dev->priv = NULL;
	dev->priv_size = 0;

	kfree(msg);

	return 0;
}

static int w1_send_read_reply(struct cn_msg *msg, struct w1_netlink_msg *hdr,
		struct w1_netlink_cmd *cmd, u32 portid)
{
	void *data;
	struct w1_netlink_msg *h;
	struct w1_netlink_cmd *c;
	struct cn_msg *cm;
	int err;

	data = kzalloc(sizeof(struct cn_msg) +
			sizeof(struct w1_netlink_msg) +
			sizeof(struct w1_netlink_cmd) +
			cmd->len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	cm = (struct cn_msg *)(data);
	h = (struct w1_netlink_msg *)(cm + 1);
	c = (struct w1_netlink_cmd *)(h + 1);

	memcpy(cm, msg, sizeof(struct cn_msg));
	memcpy(h, hdr, sizeof(struct w1_netlink_msg));
	memcpy(c, cmd, sizeof(struct w1_netlink_cmd));

	cm->ack = msg->seq+1;
	cm->len = sizeof(struct w1_netlink_msg) +
		sizeof(struct w1_netlink_cmd) + cmd->len;

	h->len = sizeof(struct w1_netlink_cmd) + cmd->len;

	memcpy(c->data, cmd->data, c->len);

	err = cn_netlink_send(cm, portid, 0, GFP_KERNEL);

	kfree(data);

	return err;
}

static int w1_process_command_io(struct w1_master *dev, struct cn_msg *msg,
		struct w1_netlink_msg *hdr, struct w1_netlink_cmd *cmd)
{
	int err = 0;

	switch (cmd->cmd) {
	case W1_CMD_TOUCH:
		w1_touch_block(dev, cmd->data, cmd->len);
		w1_send_read_reply(msg, hdr, cmd, dev->portid);
		break;
	case W1_CMD_READ:
		w1_read_block(dev, cmd->data, cmd->len);
		w1_send_read_reply(msg, hdr, cmd, dev->portid);
		break;
	case W1_CMD_WRITE:
		w1_write_block(dev, cmd->data, cmd->len);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int w1_process_command_addremove(struct w1_master *dev,
	struct cn_msg *msg, struct w1_netlink_msg *hdr,
	struct w1_netlink_cmd *cmd)
{
	struct w1_slave *sl;
	int err = 0;
	struct w1_reg_num *id;

	if (cmd->len != 8)
		return -EINVAL;

	id = (struct w1_reg_num *)cmd->data;

	sl = w1_slave_search_device(dev, id);
	switch (cmd->cmd) {
	case W1_CMD_SLAVE_ADD:
		if (sl)
			err = -EINVAL;
		else
			err = w1_attach_slave_device(dev, id);
		break;
	case W1_CMD_SLAVE_REMOVE:
		if (sl)
			w1_slave_detach(sl);
		else
			err = -EINVAL;
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int w1_process_command_master(struct w1_master *dev,
	struct cn_msg *req_msg, struct w1_netlink_msg *req_hdr,
	struct w1_netlink_cmd *req_cmd)
{
	int err = -EINVAL;

	/* drop bus_mutex for search (does it's own locking), and add/remove
	 * which doesn't use the bus
	 */
	switch (req_cmd->cmd) {
	case W1_CMD_SEARCH:
	case W1_CMD_ALARM_SEARCH:
	case W1_CMD_LIST_SLAVES:
		mutex_unlock(&dev->bus_mutex);
		err = w1_get_slaves(dev, req_msg, req_hdr, req_cmd);
		mutex_lock(&dev->bus_mutex);
		break;
	case W1_CMD_READ:
	case W1_CMD_WRITE:
	case W1_CMD_TOUCH:
		err = w1_process_command_io(dev, req_msg, req_hdr, req_cmd);
		break;
	case W1_CMD_RESET:
		err = w1_reset_bus(dev);
		break;
	case W1_CMD_SLAVE_ADD:
	case W1_CMD_SLAVE_REMOVE:
		mutex_unlock(&dev->bus_mutex);
		mutex_lock(&dev->mutex);
		err = w1_process_command_addremove(dev, req_msg, req_hdr,
			req_cmd);
		mutex_unlock(&dev->mutex);
		mutex_lock(&dev->bus_mutex);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int w1_process_command_slave(struct w1_slave *sl, struct cn_msg *msg,
		struct w1_netlink_msg *hdr, struct w1_netlink_cmd *cmd)
{
	dev_dbg(&sl->master->dev, "%s: %02x.%012llx.%02x: cmd=%02x, len=%u.\n",
		__func__, sl->reg_num.family, (unsigned long long)sl->reg_num.id,
		sl->reg_num.crc, cmd->cmd, cmd->len);

	return w1_process_command_io(sl->master, msg, hdr, cmd);
}

static int w1_process_command_root(struct cn_msg *msg,
	struct w1_netlink_msg *mcmd, u32 portid)
{
	struct w1_master *m;
	struct cn_msg *cn;
	struct w1_netlink_msg *w;
	u32 *id;

	cn = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!cn)
		return -ENOMEM;

	cn->id.idx = CN_W1_IDX;
	cn->id.val = CN_W1_VAL;

	cn->seq = msg->seq;
	cn->ack = 1;
	cn->len = sizeof(struct w1_netlink_msg);
	w = (struct w1_netlink_msg *)(cn + 1);

	w->type = W1_LIST_MASTERS;
	w->status = 0;
	w->len = 0;
	id = (u32 *)(w + 1);

	mutex_lock(&w1_mlock);
	list_for_each_entry(m, &w1_masters, w1_master_entry) {
		if (cn->len + sizeof(*id) > PAGE_SIZE - sizeof(struct cn_msg)) {
			cn_netlink_send(cn, portid, 0, GFP_KERNEL);
			cn->ack++;
			cn->len = sizeof(struct w1_netlink_msg);
			w->len = 0;
			id = (u32 *)(w + 1);
		}

		*id = m->id;
		w->len += sizeof(*id);
		cn->len += sizeof(*id);
		id++;
	}
	cn->ack = 0;
	cn_netlink_send(cn, portid, 0, GFP_KERNEL);
	mutex_unlock(&w1_mlock);

	kfree(cn);
	return 0;
}

static int w1_netlink_send_error(struct cn_msg *rcmsg, struct w1_netlink_msg *rmsg,
		struct w1_netlink_cmd *rcmd, int portid, int error)
{
	struct cn_msg *cmsg;
	struct w1_netlink_msg *msg;
	struct w1_netlink_cmd *cmd;

	cmsg = kzalloc(sizeof(*msg) + sizeof(*cmd) + sizeof(*cmsg), GFP_KERNEL);
	if (!cmsg)
		return -ENOMEM;

	msg = (struct w1_netlink_msg *)(cmsg + 1);
	cmd = (struct w1_netlink_cmd *)(msg + 1);

	memcpy(cmsg, rcmsg, sizeof(*cmsg));
	cmsg->len = sizeof(*msg);

	memcpy(msg, rmsg, sizeof(*msg));
	msg->len = 0;
	msg->status = (short)-error;

	if (rcmd) {
		memcpy(cmd, rcmd, sizeof(*cmd));
		cmd->len = 0;
		msg->len += sizeof(*cmd);
		cmsg->len += sizeof(*cmd);
	}

	error = cn_netlink_send(cmsg, portid, 0, GFP_KERNEL);
	kfree(cmsg);

	return error;
}

/* Bundle together a reference count, the full message, and broken out
 * commands to be executed on each w1 master kthread in one memory allocation.
 */
struct w1_cb_block {
	atomic_t refcnt;
	u32 portid; /* Sending process port ID */
	struct cn_msg msg;
	/* cn_msg data */
	/* one or more variable length struct w1_cb_node */
};
struct w1_cb_node {
	struct w1_async_cmd async;
	/* pointers within w1_cb_block and msg data */
	struct w1_cb_block *block;
	struct w1_netlink_msg *m;
	struct w1_slave *sl;
	struct w1_master *dev;
};

static void w1_process_cb(struct w1_master *dev, struct w1_async_cmd *async_cmd)
{
	struct w1_cb_node *node = container_of(async_cmd, struct w1_cb_node,
		async);
	u16 mlen = node->m->len;
	u8 *cmd_data = node->m->data;
	int err = 0;
	struct w1_slave *sl = node->sl;
	struct w1_netlink_cmd *cmd = NULL;

	mutex_lock(&dev->bus_mutex);
	dev->portid = node->block->portid;
	if (sl && w1_reset_select_slave(sl))
		err = -ENODEV;

	while (mlen && !err) {
		cmd = (struct w1_netlink_cmd *)cmd_data;

		if (cmd->len + sizeof(struct w1_netlink_cmd) > mlen) {
			err = -E2BIG;
			break;
		}

		if (sl)
			err = w1_process_command_slave(sl, &node->block->msg,
				node->m, cmd);
		else
			err = w1_process_command_master(dev, &node->block->msg,
				node->m, cmd);

		w1_netlink_send_error(&node->block->msg, node->m, cmd,
			node->block->portid, err);
		err = 0;

		cmd_data += cmd->len + sizeof(struct w1_netlink_cmd);
		mlen -= cmd->len + sizeof(struct w1_netlink_cmd);
	}

	if (!cmd || err)
		w1_netlink_send_error(&node->block->msg, node->m, cmd,
			node->block->portid, err);

	/* ref taken in w1_search_slave or w1_search_master_id when building
	 * the block
	 */
	if (sl)
		w1_unref_slave(sl);
	else
		atomic_dec(&dev->refcnt);
	dev->portid = 0;
	mutex_unlock(&dev->bus_mutex);

	mutex_lock(&dev->list_mutex);
	list_del(&async_cmd->async_entry);
	mutex_unlock(&dev->list_mutex);

	if (atomic_sub_return(1, &node->block->refcnt) == 0)
		kfree(node->block);
}

static void w1_cn_callback(struct cn_msg *msg, struct netlink_skb_parms *nsp)
{
	struct w1_netlink_msg *m = (struct w1_netlink_msg *)(msg + 1);
	struct w1_slave *sl;
	struct w1_master *dev;
	u16 msg_len;
	int err = 0;
	struct w1_cb_block *block = NULL;
	struct w1_cb_node *node = NULL;
	int node_count = 0;

	/* Count the number of master or slave commands there are to allocate
	 * space for one cb_node each.
	 */
	msg_len = msg->len;
	while (msg_len && !err) {
		if (m->len + sizeof(struct w1_netlink_msg) > msg_len) {
			err = -E2BIG;
			break;
		}

		if (m->type == W1_MASTER_CMD || m->type == W1_SLAVE_CMD)
			++node_count;

		msg_len -= sizeof(struct w1_netlink_msg) + m->len;
		m = (struct w1_netlink_msg *)(((u8 *)m) +
			sizeof(struct w1_netlink_msg) + m->len);
	}
	m = (struct w1_netlink_msg *)(msg + 1);
	if (node_count) {
		/* msg->len doesn't include itself */
		long size = sizeof(struct w1_cb_block) + msg->len +
			node_count*sizeof(struct w1_cb_node);
		block = kmalloc(size, GFP_KERNEL);
		if (!block) {
			w1_netlink_send_error(msg, m, NULL, nsp->portid,
				-ENOMEM);
			return;
		}
		atomic_set(&block->refcnt, 1);
		block->portid = nsp->portid;
		memcpy(&block->msg, msg, sizeof(*msg) + msg->len);
		node = (struct w1_cb_node *)((u8 *)block->msg.data + msg->len);
	}

	msg_len = msg->len;
	while (msg_len && !err) {

		dev = NULL;
		sl = NULL;

		if (m->len + sizeof(struct w1_netlink_msg) > msg_len) {
			err = -E2BIG;
			break;
		}

		/* execute on this thread, no need to process later */
		if (m->type == W1_LIST_MASTERS) {
			err = w1_process_command_root(msg, m, nsp->portid);
			goto out_cont;
		}

		/* All following message types require additional data,
		 * check here before references are taken.
		 */
		if (!m->len) {
			err = -EPROTO;
			goto out_cont;
		}

		/* both search calls take reference counts */
		if (m->type == W1_MASTER_CMD) {
			dev = w1_search_master_id(m->id.mst.id);
		} else if (m->type == W1_SLAVE_CMD) {
			sl = w1_search_slave((struct w1_reg_num *)m->id.id);
			if (sl)
				dev = sl->master;
		} else {
			printk(KERN_NOTICE
				"%s: msg: %x.%x, wrong type: %u, len: %u.\n",
				__func__, msg->id.idx, msg->id.val,
				m->type, m->len);
			err = -EPROTO;
			goto out_cont;
		}

		if (!dev) {
			err = -ENODEV;
			goto out_cont;
		}

		err = 0;

		atomic_inc(&block->refcnt);
		node->async.cb = w1_process_cb;
		node->block = block;
		node->m = (struct w1_netlink_msg *)((u8 *)&block->msg +
			(size_t)((u8 *)m - (u8 *)msg));
		node->sl = sl;
		node->dev = dev;

		mutex_lock(&dev->list_mutex);
		list_add_tail(&node->async.async_entry, &dev->async_list);
		wake_up_process(dev->thread);
		mutex_unlock(&dev->list_mutex);
		++node;

out_cont:
		if (err)
			w1_netlink_send_error(msg, m, NULL, nsp->portid, err);
		msg_len -= sizeof(struct w1_netlink_msg) + m->len;
		m = (struct w1_netlink_msg *)(((u8 *)m) +
			sizeof(struct w1_netlink_msg) + m->len);

		/*
		 * Let's allow requests for nonexisting devices.
		 */
		if (err == -ENODEV)
			err = 0;
	}
	if (block && atomic_sub_return(1, &block->refcnt) == 0)
		kfree(block);
}

int w1_init_netlink(void)
{
	struct cb_id w1_id = {.idx = CN_W1_IDX, .val = CN_W1_VAL};

	return cn_add_callback(&w1_id, "w1", &w1_cn_callback);
}

void w1_fini_netlink(void)
{
	struct cb_id w1_id = {.idx = CN_W1_IDX, .val = CN_W1_VAL};

	cn_del_callback(&w1_id);
}
#else
void w1_netlink_send(struct w1_master *dev, struct w1_netlink_msg *msg)
{
}

int w1_init_netlink(void)
{
	return 0;
}

void w1_fini_netlink(void)
{
}
#endif
