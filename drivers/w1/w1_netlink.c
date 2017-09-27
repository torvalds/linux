/*
 * Copyright (c) 2003 Evgeniy Polyakov <zbr@ioremap.net>
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
 */

#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/connector.h>

#include "w1_internal.h"
#include "w1_netlink.h"

#if defined(CONFIG_W1_CON) && (defined(CONFIG_CONNECTOR) || (defined(CONFIG_CONNECTOR_MODULE) && defined(CONFIG_W1_MODULE)))

/* Bundle together everything required to process a request in one memory
 * allocation.
 */
struct w1_cb_block {
	atomic_t refcnt;
	u32 portid; /* Sending process port ID */
	/* maximum value for first_cn->len */
	u16 maxlen;
	/* pointers to building up the reply message */
	struct cn_msg *first_cn; /* fixed once the structure is populated */
	struct cn_msg *cn; /* advances as cn_msg is appeneded */
	struct w1_netlink_msg *msg; /* advances as w1_netlink_msg is appened */
	struct w1_netlink_cmd *cmd; /* advances as cmds are appened */
	struct w1_netlink_msg *cur_msg; /* currently message being processed */
	/* copy of the original request follows */
	struct cn_msg request_cn;
	/* followed by variable length:
	 * cn_msg, data (w1_netlink_msg and w1_netlink_cmd)
	 * one or more struct w1_cb_node
	 * reply first_cn, data (w1_netlink_msg and w1_netlink_cmd)
	 */
};
struct w1_cb_node {
	struct w1_async_cmd async;
	/* pointers within w1_cb_block and cn data */
	struct w1_cb_block *block;
	struct w1_netlink_msg *msg;
	struct w1_slave *sl;
	struct w1_master *dev;
};

/**
 * w1_reply_len() - calculate current reply length, compare to maxlen
 * @block: block to calculate
 *
 * Calculates the current message length including possible multiple
 * cn_msg and data, excludes the first sizeof(struct cn_msg).  Direclty
 * compariable to maxlen and usable to send the message.
 */
static u16 w1_reply_len(struct w1_cb_block *block)
{
	if (!block->cn)
		return 0;
	return (u8 *)block->cn - (u8 *)block->first_cn + block->cn->len;
}

static void w1_unref_block(struct w1_cb_block *block)
{
	if (atomic_sub_return(1, &block->refcnt) == 0) {
		u16 len = w1_reply_len(block);
		if (len) {
			cn_netlink_send_mult(block->first_cn, len,
				block->portid, 0, GFP_KERNEL);
		}
		kfree(block);
	}
}

/**
 * w1_reply_make_space() - send message if needed to make space
 * @block: block to make space on
 * @space: how many bytes requested
 *
 * Verify there is enough room left for the caller to add "space" bytes to the
 * message, if there isn't send the message and reset.
 */
static void w1_reply_make_space(struct w1_cb_block *block, u16 space)
{
	u16 len = w1_reply_len(block);
	if (len + space >= block->maxlen) {
		cn_netlink_send_mult(block->first_cn, len, block->portid, 0, GFP_KERNEL);
		block->first_cn->len = 0;
		block->cn = NULL;
		block->msg = NULL;
		block->cmd = NULL;
	}
}

/* Early send when replies aren't bundled. */
static void w1_netlink_check_send(struct w1_cb_block *block)
{
	if (!(block->request_cn.flags & W1_CN_BUNDLE) && block->cn)
		w1_reply_make_space(block, block->maxlen);
}

/**
 * w1_netlink_setup_msg() - prepare to write block->msg
 * @block: block to operate on
 * @ack: determines if cn can be reused
 *
 * block->cn will be setup with the correct ack, advancing if needed
 * block->cn->len does not include space for block->msg
 * block->msg advances but remains uninitialized
 */
static void w1_netlink_setup_msg(struct w1_cb_block *block, u32 ack)
{
	if (block->cn && block->cn->ack == ack) {
		block->msg = (struct w1_netlink_msg *)(block->cn->data + block->cn->len);
	} else {
		/* advance or set to data */
		if (block->cn)
			block->cn = (struct cn_msg *)(block->cn->data +
				block->cn->len);
		else
			block->cn = block->first_cn;

		memcpy(block->cn, &block->request_cn, sizeof(*block->cn));
		block->cn->len = 0;
		block->cn->ack = ack;
		block->msg = (struct w1_netlink_msg *)block->cn->data;
	}
}

/* Append cmd to msg, include cmd->data as well.  This is because
 * any following data goes with the command and in the case of a read is
 * the results.
 */
static void w1_netlink_queue_cmd(struct w1_cb_block *block,
	struct w1_netlink_cmd *cmd)
{
	u32 space;
	w1_reply_make_space(block, sizeof(struct cn_msg) +
		sizeof(struct w1_netlink_msg) + sizeof(*cmd) + cmd->len);

	/* There's a status message sent after each command, so no point
	 * in trying to bundle this cmd after an existing one, because
	 * there won't be one.  Allocate and copy over a new cn_msg.
	 */
	w1_netlink_setup_msg(block, block->request_cn.seq + 1);
	memcpy(block->msg, block->cur_msg, sizeof(*block->msg));
	block->cn->len += sizeof(*block->msg);
	block->msg->len = 0;
	block->cmd = (struct w1_netlink_cmd *)(block->msg->data);

	space = sizeof(*cmd) + cmd->len;
	if (block->cmd != cmd)
		memcpy(block->cmd, cmd, space);
	block->cn->len += space;
	block->msg->len += space;
}

/* Append req_msg and req_cmd, no other commands and no data from req_cmd are
 * copied.
 */
static void w1_netlink_queue_status(struct w1_cb_block *block,
	struct w1_netlink_msg *req_msg, struct w1_netlink_cmd *req_cmd,
	int error)
{
	u16 space = sizeof(struct cn_msg) + sizeof(*req_msg) + sizeof(*req_cmd);
	w1_reply_make_space(block, space);
	w1_netlink_setup_msg(block, block->request_cn.ack);

	memcpy(block->msg, req_msg, sizeof(*req_msg));
	block->cn->len += sizeof(*req_msg);
	block->msg->len = 0;
	block->msg->status = (u8)-error;
	if (req_cmd) {
		struct w1_netlink_cmd *cmd = (struct w1_netlink_cmd *)block->msg->data;
		memcpy(cmd, req_cmd, sizeof(*cmd));
		block->cn->len += sizeof(*cmd);
		block->msg->len += sizeof(*cmd);
		cmd->len = 0;
	}
	w1_netlink_check_send(block);
}

/**
 * w1_netlink_send_error() - sends the error message now
 * @cn: original cn_msg
 * @msg: original w1_netlink_msg
 * @portid: where to send it
 * @error: error status
 *
 * Use when a block isn't available to queue the message to and cn, msg
 * might not be contiguous.
 */
static void w1_netlink_send_error(struct cn_msg *cn, struct w1_netlink_msg *msg,
	int portid, int error)
{
	struct {
		struct cn_msg cn;
		struct w1_netlink_msg msg;
	} packet;
	memcpy(&packet.cn, cn, sizeof(packet.cn));
	memcpy(&packet.msg, msg, sizeof(packet.msg));
	packet.cn.len = sizeof(packet.msg);
	packet.msg.len = 0;
	packet.msg.status = (u8)-error;
	cn_netlink_send(&packet.cn, portid, 0, GFP_KERNEL);
}

/**
 * w1_netlink_send() - sends w1 netlink notifications
 * @dev: w1_master the even is associated with or for
 * @msg: w1_netlink_msg message to be sent
 *
 * This are notifications generated from the kernel.
 */
void w1_netlink_send(struct w1_master *dev, struct w1_netlink_msg *msg)
{
	struct {
		struct cn_msg cn;
		struct w1_netlink_msg msg;
	} packet;
	memset(&packet, 0, sizeof(packet));

	packet.cn.id.idx = CN_W1_IDX;
	packet.cn.id.val = CN_W1_VAL;

	packet.cn.seq = dev->seq++;
	packet.cn.len = sizeof(*msg);

	memcpy(&packet.msg, msg, sizeof(*msg));
	packet.msg.len = 0;

	cn_netlink_send(&packet.cn, 0, 0, GFP_KERNEL);
}

static void w1_send_slave(struct w1_master *dev, u64 rn)
{
	struct w1_cb_block *block = dev->priv;
	struct w1_netlink_cmd *cache_cmd = block->cmd;
	u64 *data;

	w1_reply_make_space(block, sizeof(*data));

	/* Add cmd back if the packet was sent */
	if (!block->cmd) {
		cache_cmd->len = 0;
		w1_netlink_queue_cmd(block, cache_cmd);
	}

	data = (u64 *)(block->cmd->data + block->cmd->len);

	*data = rn;
	block->cn->len += sizeof(*data);
	block->msg->len += sizeof(*data);
	block->cmd->len += sizeof(*data);
}

static void w1_found_send_slave(struct w1_master *dev, u64 rn)
{
	/* update kernel slave list */
	w1_slave_found(dev, rn);

	w1_send_slave(dev, rn);
}

/* Get the current slave list, or search (with or without alarm) */
static int w1_get_slaves(struct w1_master *dev, struct w1_netlink_cmd *req_cmd)
{
	struct w1_slave *sl;

	req_cmd->len = 0;
	w1_netlink_queue_cmd(dev->priv, req_cmd);

	if (req_cmd->cmd == W1_CMD_LIST_SLAVES) {
		u64 rn;
		mutex_lock(&dev->list_mutex);
		list_for_each_entry(sl, &dev->slist, w1_slave_entry) {
			memcpy(&rn, &sl->reg_num, sizeof(rn));
			w1_send_slave(dev, rn);
		}
		mutex_unlock(&dev->list_mutex);
	} else {
		w1_search_process_cb(dev, req_cmd->cmd == W1_CMD_ALARM_SEARCH ?
			W1_ALARM_SEARCH : W1_SEARCH, w1_found_send_slave);
	}

	return 0;
}

static int w1_process_command_io(struct w1_master *dev,
	struct w1_netlink_cmd *cmd)
{
	int err = 0;

	switch (cmd->cmd) {
	case W1_CMD_TOUCH:
		w1_touch_block(dev, cmd->data, cmd->len);
		w1_netlink_queue_cmd(dev->priv, cmd);
		break;
	case W1_CMD_READ:
		w1_read_block(dev, cmd->data, cmd->len);
		w1_netlink_queue_cmd(dev->priv, cmd);
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
	struct w1_netlink_cmd *cmd)
{
	struct w1_slave *sl;
	int err = 0;
	struct w1_reg_num *id;

	if (cmd->len != sizeof(*id))
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
		err = w1_get_slaves(dev, req_cmd);
		mutex_lock(&dev->bus_mutex);
		break;
	case W1_CMD_READ:
	case W1_CMD_WRITE:
	case W1_CMD_TOUCH:
		err = w1_process_command_io(dev, req_cmd);
		break;
	case W1_CMD_RESET:
		err = w1_reset_bus(dev);
		break;
	case W1_CMD_SLAVE_ADD:
	case W1_CMD_SLAVE_REMOVE:
		mutex_unlock(&dev->bus_mutex);
		mutex_lock(&dev->mutex);
		err = w1_process_command_addremove(dev, req_cmd);
		mutex_unlock(&dev->mutex);
		mutex_lock(&dev->bus_mutex);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int w1_process_command_slave(struct w1_slave *sl,
		struct w1_netlink_cmd *cmd)
{
	dev_dbg(&sl->master->dev, "%s: %02x.%012llx.%02x: cmd=%02x, len=%u.\n",
		__func__, sl->reg_num.family, (unsigned long long)sl->reg_num.id,
		sl->reg_num.crc, cmd->cmd, cmd->len);

	return w1_process_command_io(sl->master, cmd);
}

static int w1_process_command_root(struct cn_msg *req_cn, u32 portid)
{
	struct w1_master *dev;
	struct cn_msg *cn;
	struct w1_netlink_msg *msg;
	u32 *id;

	cn = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!cn)
		return -ENOMEM;

	cn->id.idx = CN_W1_IDX;
	cn->id.val = CN_W1_VAL;

	cn->seq = req_cn->seq;
	cn->ack = req_cn->seq + 1;
	cn->len = sizeof(struct w1_netlink_msg);
	msg = (struct w1_netlink_msg *)cn->data;

	msg->type = W1_LIST_MASTERS;
	msg->status = 0;
	msg->len = 0;
	id = (u32 *)msg->data;

	mutex_lock(&w1_mlock);
	list_for_each_entry(dev, &w1_masters, w1_master_entry) {
		if (cn->len + sizeof(*id) > PAGE_SIZE - sizeof(struct cn_msg)) {
			cn_netlink_send(cn, portid, 0, GFP_KERNEL);
			cn->len = sizeof(struct w1_netlink_msg);
			msg->len = 0;
			id = (u32 *)msg->data;
		}

		*id = dev->id;
		msg->len += sizeof(*id);
		cn->len += sizeof(*id);
		id++;
	}
	cn_netlink_send(cn, portid, 0, GFP_KERNEL);
	mutex_unlock(&w1_mlock);

	kfree(cn);
	return 0;
}

static void w1_process_cb(struct w1_master *dev, struct w1_async_cmd *async_cmd)
{
	struct w1_cb_node *node = container_of(async_cmd, struct w1_cb_node,
		async);
	u16 mlen = node->msg->len;
	u16 len;
	int err = 0;
	struct w1_slave *sl = node->sl;
	struct w1_netlink_cmd *cmd = (struct w1_netlink_cmd *)node->msg->data;

	mutex_lock(&dev->bus_mutex);
	dev->priv = node->block;
	if (sl && w1_reset_select_slave(sl))
		err = -ENODEV;
	node->block->cur_msg = node->msg;

	while (mlen && !err) {
		if (cmd->len + sizeof(struct w1_netlink_cmd) > mlen) {
			err = -E2BIG;
			break;
		}

		if (sl)
			err = w1_process_command_slave(sl, cmd);
		else
			err = w1_process_command_master(dev, cmd);
		w1_netlink_check_send(node->block);

		w1_netlink_queue_status(node->block, node->msg, cmd, err);
		err = 0;

		len = sizeof(*cmd) + cmd->len;
		cmd = (struct w1_netlink_cmd *)((u8 *)cmd + len);
		mlen -= len;
	}

	if (!cmd || err)
		w1_netlink_queue_status(node->block, node->msg, cmd, err);

	/* ref taken in w1_search_slave or w1_search_master_id when building
	 * the block
	 */
	if (sl)
		w1_unref_slave(sl);
	else
		atomic_dec(&dev->refcnt);
	dev->priv = NULL;
	mutex_unlock(&dev->bus_mutex);

	mutex_lock(&dev->list_mutex);
	list_del(&async_cmd->async_entry);
	mutex_unlock(&dev->list_mutex);

	w1_unref_block(node->block);
}

static void w1_list_count_cmds(struct w1_netlink_msg *msg, int *cmd_count,
	u16 *slave_len)
{
	struct w1_netlink_cmd *cmd = (struct w1_netlink_cmd *)msg->data;
	u16 mlen = msg->len;
	u16 len;
	int slave_list = 0;
	while (mlen) {
		if (cmd->len + sizeof(struct w1_netlink_cmd) > mlen)
			break;

		switch (cmd->cmd) {
		case W1_CMD_SEARCH:
		case W1_CMD_ALARM_SEARCH:
		case W1_CMD_LIST_SLAVES:
			++slave_list;
		}
		++*cmd_count;
		len = sizeof(*cmd) + cmd->len;
		cmd = (struct w1_netlink_cmd *)((u8 *)cmd + len);
		mlen -= len;
	}

	if (slave_list) {
		struct w1_master *dev = w1_search_master_id(msg->id.mst.id);
		if (dev) {
			/* Bytes, and likely an overstimate, and if it isn't
			 * the results can still be split between packets.
			 */
			*slave_len += sizeof(struct w1_reg_num) * slave_list *
				(dev->slave_count + dev->max_slave_count);
			/* search incremented it */
			atomic_dec(&dev->refcnt);
		}
	}
}

static void w1_cn_callback(struct cn_msg *cn, struct netlink_skb_parms *nsp)
{
	struct w1_netlink_msg *msg = (struct w1_netlink_msg *)(cn + 1);
	struct w1_slave *sl;
	struct w1_master *dev;
	u16 msg_len;
	u16 slave_len = 0;
	int err = 0;
	struct w1_cb_block *block = NULL;
	struct w1_cb_node *node = NULL;
	int node_count = 0;
	int cmd_count = 0;

	/* If any unknown flag is set let the application know, that way
	 * applications can detect the absence of features in kernels that
	 * don't know about them.  http://lwn.net/Articles/587527/
	 */
	if (cn->flags & ~(W1_CN_BUNDLE)) {
		w1_netlink_send_error(cn, msg, nsp->portid, -EINVAL);
		return;
	}

	/* Count the number of master or slave commands there are to allocate
	 * space for one cb_node each.
	 */
	msg_len = cn->len;
	while (msg_len && !err) {
		if (msg->len + sizeof(struct w1_netlink_msg) > msg_len) {
			err = -E2BIG;
			break;
		}

		/* count messages for nodes and allocate any additional space
		 * required for slave lists
		 */
		if (msg->type == W1_MASTER_CMD || msg->type == W1_SLAVE_CMD) {
			++node_count;
			w1_list_count_cmds(msg, &cmd_count, &slave_len);
		}

		msg_len -= sizeof(struct w1_netlink_msg) + msg->len;
		msg = (struct w1_netlink_msg *)(((u8 *)msg) +
			sizeof(struct w1_netlink_msg) + msg->len);
	}
	msg = (struct w1_netlink_msg *)(cn + 1);
	if (node_count) {
		int size;
		int reply_size = sizeof(*cn) + cn->len + slave_len;
		if (cn->flags & W1_CN_BUNDLE) {
			/* bundling duplicats some of the messages */
			reply_size += 2 * cmd_count * (sizeof(struct cn_msg) +
				sizeof(struct w1_netlink_msg) +
				sizeof(struct w1_netlink_cmd));
		}
		reply_size = min(CONNECTOR_MAX_MSG_SIZE, reply_size);

		/* allocate space for the block, a copy of the original message,
		 * one node per cmd to point into the original message,
		 * space for replies which is the original message size plus
		 * space for any list slave data and status messages
		 * cn->len doesn't include itself which is part of the block
		 * */
		size =  /* block + original message */
			sizeof(struct w1_cb_block) + sizeof(*cn) + cn->len +
			/* space for nodes */
			node_count * sizeof(struct w1_cb_node) +
			/* replies */
			sizeof(struct cn_msg) + reply_size;
		block = kzalloc(size, GFP_KERNEL);
		if (!block) {
			/* if the system is already out of memory,
			 * (A) will this work, and (B) would it be better
			 * to not try?
			 */
			w1_netlink_send_error(cn, msg, nsp->portid, -ENOMEM);
			return;
		}
		atomic_set(&block->refcnt, 1);
		block->portid = nsp->portid;
		memcpy(&block->request_cn, cn, sizeof(*cn) + cn->len);
		node = (struct w1_cb_node *)(block->request_cn.data + cn->len);

		/* Sneeky, when not bundling, reply_size is the allocated space
		 * required for the reply, cn_msg isn't part of maxlen so
		 * it should be reply_size - sizeof(struct cn_msg), however
		 * when checking if there is enough space, w1_reply_make_space
		 * is called with the full message size including cn_msg,
		 * because it isn't known at that time if an additional cn_msg
		 * will need to be allocated.  So an extra cn_msg is added
		 * above in "size".
		 */
		block->maxlen = reply_size;
		block->first_cn = (struct cn_msg *)(node + node_count);
		memset(block->first_cn, 0, sizeof(*block->first_cn));
	}

	msg_len = cn->len;
	while (msg_len && !err) {

		dev = NULL;
		sl = NULL;

		if (msg->len + sizeof(struct w1_netlink_msg) > msg_len) {
			err = -E2BIG;
			break;
		}

		/* execute on this thread, no need to process later */
		if (msg->type == W1_LIST_MASTERS) {
			err = w1_process_command_root(cn, nsp->portid);
			goto out_cont;
		}

		/* All following message types require additional data,
		 * check here before references are taken.
		 */
		if (!msg->len) {
			err = -EPROTO;
			goto out_cont;
		}

		/* both search calls take references */
		if (msg->type == W1_MASTER_CMD) {
			dev = w1_search_master_id(msg->id.mst.id);
		} else if (msg->type == W1_SLAVE_CMD) {
			sl = w1_search_slave((struct w1_reg_num *)msg->id.id);
			if (sl)
				dev = sl->master;
		} else {
			pr_notice("%s: cn: %x.%x, wrong type: %u, len: %u.\n",
				__func__, cn->id.idx, cn->id.val,
				msg->type, msg->len);
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
		node->msg = (struct w1_netlink_msg *)((u8 *)&block->request_cn +
			(size_t)((u8 *)msg - (u8 *)cn));
		node->sl = sl;
		node->dev = dev;

		mutex_lock(&dev->list_mutex);
		list_add_tail(&node->async.async_entry, &dev->async_list);
		wake_up_process(dev->thread);
		mutex_unlock(&dev->list_mutex);
		++node;

out_cont:
		/* Can't queue because that modifies block and another
		 * thread could be processing the messages by now and
		 * there isn't a lock, send directly.
		 */
		if (err)
			w1_netlink_send_error(cn, msg, nsp->portid, err);
		msg_len -= sizeof(struct w1_netlink_msg) + msg->len;
		msg = (struct w1_netlink_msg *)(((u8 *)msg) +
			sizeof(struct w1_netlink_msg) + msg->len);

		/*
		 * Let's allow requests for nonexisting devices.
		 */
		if (err == -ENODEV)
			err = 0;
	}
	if (block)
		w1_unref_block(block);
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
void w1_netlink_send(struct w1_master *dev, struct w1_netlink_msg *cn)
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
