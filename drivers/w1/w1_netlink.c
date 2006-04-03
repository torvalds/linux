/*
 * w1_netlink.c
 *
 * Copyright (c) 2003 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
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

	cn_netlink_send(m, 0, GFP_KERNEL);
}

static int w1_process_command_master(struct w1_master *dev, struct cn_msg *msg,
		struct w1_netlink_msg *hdr, struct w1_netlink_cmd *cmd)
{
	dev_dbg(&dev->dev, "%s: %s: cmd=%02x, len=%u.\n",
		__func__, dev->name, cmd->cmd, cmd->len);

	if (cmd->cmd != W1_CMD_SEARCH && cmd->cmd != W1_CMD_ALARM_SEARCH)
		return -EINVAL;

	w1_search_process(dev, (cmd->cmd == W1_CMD_ALARM_SEARCH)?W1_ALARM_SEARCH:W1_SEARCH);
	return 0;
}

static int w1_send_read_reply(struct w1_slave *sl, struct cn_msg *msg,
		struct w1_netlink_msg *hdr, struct w1_netlink_cmd *cmd)
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
	cm->len = sizeof(struct w1_netlink_msg) + sizeof(struct w1_netlink_cmd) + cmd->len;

	h->len = sizeof(struct w1_netlink_cmd) + cmd->len;

	memcpy(c->data, cmd->data, c->len);

	err = cn_netlink_send(cm, 0, GFP_KERNEL);

	kfree(data);

	return err;
}

static int w1_process_command_slave(struct w1_slave *sl, struct cn_msg *msg,
		struct w1_netlink_msg *hdr, struct w1_netlink_cmd *cmd)
{
	int err = 0;

	dev_dbg(&sl->master->dev, "%s: %02x.%012llx.%02x: cmd=%02x, len=%u.\n",
		__func__, sl->reg_num.family, (unsigned long long)sl->reg_num.id, sl->reg_num.crc,
		cmd->cmd, cmd->len);

	switch (cmd->cmd) {
		case W1_CMD_READ:
			w1_read_block(sl->master, cmd->data, cmd->len);
			w1_send_read_reply(sl, msg, hdr, cmd);
			break;
		case W1_CMD_WRITE:
			w1_write_block(sl->master, cmd->data, cmd->len);
			break;
		case W1_CMD_SEARCH:
		case W1_CMD_ALARM_SEARCH:
			w1_search_process(sl->master,
					(cmd->cmd == W1_CMD_ALARM_SEARCH)?W1_ALARM_SEARCH:W1_SEARCH);
			break;
		default:
			err = -1;
			break;
	}

	return err;
}

static void w1_cn_callback(void *data)
{
	struct cn_msg *msg = data;
	struct w1_netlink_msg *m = (struct w1_netlink_msg *)(msg + 1);
	struct w1_netlink_cmd *cmd;
	struct w1_slave *sl;
	struct w1_master *dev;
	int err = 0;

	while (msg->len && !err) {
		struct w1_reg_num id;
		u16 mlen = m->len;
		u8 *cmd_data = m->data;

		dev = NULL;
		sl = NULL;

		memcpy(&id, m->id.id, sizeof(id));
#if 0
		printk("%s: %02x.%012llx.%02x: type=%02x, len=%u.\n",
				__func__, id.family, (unsigned long long)id.id, id.crc, m->type, m->len);
#endif
		if (m->len + sizeof(struct w1_netlink_msg) > msg->len) {
			err = -E2BIG;
			break;
		}

		if (!mlen)
			goto out_cont;

		if (m->type == W1_MASTER_CMD) {
			dev = w1_search_master_id(m->id.mst.id);
		} else if (m->type == W1_SLAVE_CMD) {
			sl = w1_search_slave(&id);
			if (sl)
				dev = sl->master;
		}

		if (!dev) {
			err = -ENODEV;
			goto out_cont;
		}

		down(&dev->mutex);

		if (sl && w1_reset_select_slave(sl)) {
			err = -ENODEV;
			goto out_up;
		}

		while (mlen) {
			cmd = (struct w1_netlink_cmd *)cmd_data;

			if (cmd->len + sizeof(struct w1_netlink_cmd) > mlen) {
				err = -E2BIG;
				break;
			}

			if (sl)
				w1_process_command_slave(sl, msg, m, cmd);
			else
				w1_process_command_master(dev, msg, m, cmd);

			cmd_data += cmd->len + sizeof(struct w1_netlink_cmd);
			mlen -= cmd->len + sizeof(struct w1_netlink_cmd);
		}
out_up:
		atomic_dec(&dev->refcnt);
		if (sl)
			atomic_dec(&sl->refcnt);
		up(&dev->mutex);
out_cont:
		msg->len -= sizeof(struct w1_netlink_msg) + m->len;
		m = (struct w1_netlink_msg *)(((u8 *)m) + sizeof(struct w1_netlink_msg) + m->len);

		/*
		 * Let's allow requests for nonexisting devices.
		 */
		if (err == -ENODEV)
			err = 0;
	}
#if 0
	if (err) {
		printk("%s: malformed message. Dropping.\n", __func__);
	}
#endif
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
