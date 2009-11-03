/*
 * 2007+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
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

#include <linux/kernel.h>
#include <linux/connector.h>
#include <linux/crypto.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/in.h>

#include "netfs.h"

/*
 * Global configuration list.
 * Each client can be asked to get one of them.
 *
 * Allows to provide remote server address (ipv4/v6/whatever), port
 * and so on via kernel connector.
 */

static struct cb_id pohmelfs_cn_id = {.idx = POHMELFS_CN_IDX, .val = POHMELFS_CN_VAL};
static LIST_HEAD(pohmelfs_config_list);
static DEFINE_MUTEX(pohmelfs_config_lock);

static inline int pohmelfs_config_eql(struct pohmelfs_ctl *sc, struct pohmelfs_ctl *ctl)
{
	if (sc->idx == ctl->idx && sc->type == ctl->type &&
			sc->proto == ctl->proto &&
			sc->addrlen == ctl->addrlen &&
			!memcmp(&sc->addr, &ctl->addr, ctl->addrlen))
		return 1;

	return 0;
}

static struct pohmelfs_config_group *pohmelfs_find_config_group(unsigned int idx)
{
	struct pohmelfs_config_group *g, *group = NULL;

	list_for_each_entry(g, &pohmelfs_config_list, group_entry) {
		if (g->idx == idx) {
			group = g;
			break;
		}
	}

	return group;
}

static struct pohmelfs_config_group *pohmelfs_find_create_config_group(unsigned int idx)
{
	struct pohmelfs_config_group *g;

	g = pohmelfs_find_config_group(idx);
	if (g)
		return g;

	g = kzalloc(sizeof(struct pohmelfs_config_group), GFP_KERNEL);
	if (!g)
		return NULL;

	INIT_LIST_HEAD(&g->config_list);
	g->idx = idx;
	g->num_entry = 0;

	list_add_tail(&g->group_entry, &pohmelfs_config_list);

	return g;
}

static inline void pohmelfs_insert_config_entry(struct pohmelfs_sb *psb, struct pohmelfs_config *dst)
{
	struct pohmelfs_config *tmp;

	INIT_LIST_HEAD(&dst->config_entry);

	list_for_each_entry(tmp, &psb->state_list, config_entry) {
		if (dst->state.ctl.prio > tmp->state.ctl.prio)
			list_add_tail(&dst->config_entry, &tmp->config_entry);
	}
	if (list_empty(&dst->config_entry))
		list_add_tail(&dst->config_entry, &psb->state_list);
}

static int pohmelfs_move_config_entry(struct pohmelfs_sb *psb,
		struct pohmelfs_config *dst, struct pohmelfs_config *new)
{
	if ((dst->state.ctl.prio == new->state.ctl.prio) &&
		(dst->state.ctl.perm == new->state.ctl.perm))
		return 0;

	dprintk("%s: dst: prio: %d, perm: %x, new: prio: %d, perm: %d.\n",
			__func__, dst->state.ctl.prio, dst->state.ctl.perm,
			new->state.ctl.prio, new->state.ctl.perm);
	dst->state.ctl.prio = new->state.ctl.prio;
	dst->state.ctl.perm = new->state.ctl.perm;

	list_del_init(&dst->config_entry);
	pohmelfs_insert_config_entry(psb, dst);
	return 0;
}

/*
 * pohmelfs_copy_config() is used to copy new state configs from the
 * config group (controlled by the netlink messages) into the superblock.
 * This happens either at startup time where no transactions can access
 * the list of the configs (and thus list of the network states), or at
 * run-time, where it is protected by the psb->state_lock.
 */
int pohmelfs_copy_config(struct pohmelfs_sb *psb)
{
	struct pohmelfs_config_group *g;
	struct pohmelfs_config *c, *dst;
	int err = -ENODEV;

	mutex_lock(&pohmelfs_config_lock);

	g = pohmelfs_find_config_group(psb->idx);
	if (!g)
		goto out_unlock;

	/*
	 * Run over all entries in given config group and try to crate and
	 * initialize those, which do not exist in superblock list.
	 * Skip all existing entries.
	 */

	list_for_each_entry(c, &g->config_list, config_entry) {
		err = 0;
		list_for_each_entry(dst, &psb->state_list, config_entry) {
			if (pohmelfs_config_eql(&dst->state.ctl, &c->state.ctl)) {
				err = pohmelfs_move_config_entry(psb, dst, c);
				if (!err)
					err = -EEXIST;
				break;
			}
		}

		if (err)
			continue;

		dst = kzalloc(sizeof(struct pohmelfs_config), GFP_KERNEL);
		if (!dst) {
			err = -ENOMEM;
			break;
		}

		memcpy(&dst->state.ctl, &c->state.ctl, sizeof(struct pohmelfs_ctl));

		pohmelfs_insert_config_entry(psb, dst);

		err = pohmelfs_state_init_one(psb, dst);
		if (err) {
			list_del(&dst->config_entry);
			kfree(dst);
		}

		err = 0;
	}

out_unlock:
	mutex_unlock(&pohmelfs_config_lock);

	return err;
}

int pohmelfs_copy_crypto(struct pohmelfs_sb *psb)
{
	struct pohmelfs_config_group *g;
	int err = -ENOENT;

	mutex_lock(&pohmelfs_config_lock);
	g = pohmelfs_find_config_group(psb->idx);
	if (!g)
		goto err_out_exit;

	if (g->hash_string) {
		err = -ENOMEM;
		psb->hash_string = kstrdup(g->hash_string, GFP_KERNEL);
		if (!psb->hash_string)
			goto err_out_exit;
		psb->hash_strlen = g->hash_strlen;
	}

	if (g->cipher_string) {
		psb->cipher_string = kstrdup(g->cipher_string, GFP_KERNEL);
		if (!psb->cipher_string)
			goto err_out_free_hash_string;
		psb->cipher_strlen = g->cipher_strlen;
	}

	if (g->hash_keysize) {
		psb->hash_key = kmalloc(g->hash_keysize, GFP_KERNEL);
		if (!psb->hash_key)
			goto err_out_free_cipher_string;
		memcpy(psb->hash_key, g->hash_key, g->hash_keysize);
		psb->hash_keysize = g->hash_keysize;
	}

	if (g->cipher_keysize) {
		psb->cipher_key = kmalloc(g->cipher_keysize, GFP_KERNEL);
		if (!psb->cipher_key)
			goto err_out_free_hash;
		memcpy(psb->cipher_key, g->cipher_key, g->cipher_keysize);
		psb->cipher_keysize = g->cipher_keysize;
	}

	mutex_unlock(&pohmelfs_config_lock);

	return 0;

err_out_free_hash:
	kfree(psb->hash_key);
err_out_free_cipher_string:
	kfree(psb->cipher_string);
err_out_free_hash_string:
	kfree(psb->hash_string);
err_out_exit:
	mutex_unlock(&pohmelfs_config_lock);
	return err;
}

static int pohmelfs_send_reply(int err, int msg_num, int action, struct cn_msg *msg, struct pohmelfs_ctl *ctl)
{
	struct pohmelfs_cn_ack *ack;

	ack = kmalloc(sizeof(struct pohmelfs_cn_ack), GFP_KERNEL);
	if (!ack)
		return -ENOMEM;

	memset(ack, 0, sizeof(struct pohmelfs_cn_ack));
	memcpy(&ack->msg, msg, sizeof(struct cn_msg));

	if (action == POHMELFS_CTLINFO_ACK)
		memcpy(&ack->ctl, ctl, sizeof(struct pohmelfs_ctl));

	ack->msg.len = sizeof(struct pohmelfs_cn_ack) - sizeof(struct cn_msg);
	ack->msg.ack = msg->ack + 1;
	ack->error = err;
	ack->msg_num = msg_num;

	cn_netlink_send(&ack->msg, 0, GFP_KERNEL);
	kfree(ack);
	return 0;
}

static int pohmelfs_cn_disp(struct cn_msg *msg)
{
	struct pohmelfs_config_group *g;
	struct pohmelfs_ctl *ctl = (struct pohmelfs_ctl *)msg->data;
	struct pohmelfs_config *c, *tmp;
	int err = 0, i = 1;

	if (msg->len != sizeof(struct pohmelfs_ctl))
		return -EBADMSG;

	mutex_lock(&pohmelfs_config_lock);

	g = pohmelfs_find_config_group(ctl->idx);
	if (!g) {
		pohmelfs_send_reply(err, 0, POHMELFS_NOINFO_ACK, msg, NULL);
		goto out_unlock;
	}

	list_for_each_entry_safe(c, tmp, &g->config_list, config_entry) {
		struct pohmelfs_ctl *sc = &c->state.ctl;
		if (pohmelfs_send_reply(err, g->num_entry - i, POHMELFS_CTLINFO_ACK, msg, sc)) {
			err = -ENOMEM;
			goto out_unlock;
		}
		i += 1;
	}

 out_unlock:
	mutex_unlock(&pohmelfs_config_lock);
	return err;
}

static int pohmelfs_cn_dump(struct cn_msg *msg)
{
	struct pohmelfs_config_group *g;
	struct pohmelfs_config *c, *tmp;
	int err = 0, i = 1;
	int total_msg = 0;

	if (msg->len != sizeof(struct pohmelfs_ctl))
		return -EBADMSG;

	mutex_lock(&pohmelfs_config_lock);

	list_for_each_entry(g, &pohmelfs_config_list, group_entry) {
		if (g)
			total_msg += g->num_entry;
	}
	if (total_msg == 0) {
		if (pohmelfs_send_reply(err, 0, POHMELFS_NOINFO_ACK, msg, NULL))
			err = -ENOMEM;
		goto out_unlock;
	}

	list_for_each_entry(g, &pohmelfs_config_list, group_entry) {
		if (g) {
			list_for_each_entry_safe(c, tmp, &g->config_list, config_entry) {
				struct pohmelfs_ctl *sc = &c->state.ctl;
				if (pohmelfs_send_reply(err, total_msg - i, POHMELFS_CTLINFO_ACK, msg, sc)) {
					err = -ENOMEM;
					goto out_unlock;
				}
				i += 1;
			}
		}
	}

out_unlock:
	mutex_unlock(&pohmelfs_config_lock);
	return err;
}

static int pohmelfs_cn_flush(struct cn_msg *msg)
{
	struct pohmelfs_config_group *g;
	struct pohmelfs_ctl *ctl = (struct pohmelfs_ctl *)msg->data;
	struct pohmelfs_config *c, *tmp;
	int err = 0;

	if (msg->len != sizeof(struct pohmelfs_ctl))
		return -EBADMSG;

	mutex_lock(&pohmelfs_config_lock);

	if (ctl->idx != POHMELFS_NULL_IDX) {
		g = pohmelfs_find_config_group(ctl->idx);

		if (!g)
			goto out_unlock;

		list_for_each_entry_safe(c, tmp, &g->config_list, config_entry) {
			list_del(&c->config_entry);
			g->num_entry--;
			kfree(c);
		}
	} else {
		list_for_each_entry(g, &pohmelfs_config_list, group_entry) {
			if (g) {
				list_for_each_entry_safe(c, tmp, &g->config_list, config_entry) {
					list_del(&c->config_entry);
					g->num_entry--;
					kfree(c);
				}
			}
		}
	}

out_unlock:
	mutex_unlock(&pohmelfs_config_lock);
	pohmelfs_cn_dump(msg);

	return err;
}

static int pohmelfs_modify_config(struct pohmelfs_ctl *old, struct pohmelfs_ctl *new)
{
	old->perm = new->perm;
	old->prio = new->prio;
	return 0;
}

static int pohmelfs_cn_ctl(struct cn_msg *msg, int action)
{
	struct pohmelfs_config_group *g;
	struct pohmelfs_ctl *ctl = (struct pohmelfs_ctl *)msg->data;
	struct pohmelfs_config *c, *tmp;
	int err = 0;

	if (msg->len != sizeof(struct pohmelfs_ctl))
		return -EBADMSG;

	mutex_lock(&pohmelfs_config_lock);

	g = pohmelfs_find_create_config_group(ctl->idx);
	if (!g) {
		err = -ENOMEM;
		goto out_unlock;
	}

	list_for_each_entry_safe(c, tmp, &g->config_list, config_entry) {
		struct pohmelfs_ctl *sc = &c->state.ctl;

		if (pohmelfs_config_eql(sc, ctl)) {
			if (action == POHMELFS_FLAGS_ADD) {
				err = -EEXIST;
				goto out_unlock;
			} else if (action == POHMELFS_FLAGS_DEL) {
				list_del(&c->config_entry);
				g->num_entry--;
				kfree(c);
				goto out_unlock;
			} else if (action == POHMELFS_FLAGS_MODIFY) {
				err = pohmelfs_modify_config(sc, ctl);
				goto out_unlock;
			} else {
				err = -EEXIST;
				goto out_unlock;
			}
		}
	}
	if (action == POHMELFS_FLAGS_DEL) {
		err = -EBADMSG;
		goto out_unlock;
	}

	c = kzalloc(sizeof(struct pohmelfs_config), GFP_KERNEL);
	if (!c) {
		err = -ENOMEM;
		goto out_unlock;
	}
	memcpy(&c->state.ctl, ctl, sizeof(struct pohmelfs_ctl));
	g->num_entry++;

	list_add_tail(&c->config_entry, &g->config_list);

 out_unlock:
	mutex_unlock(&pohmelfs_config_lock);
	if (pohmelfs_send_reply(err, 0, POHMELFS_NOINFO_ACK, msg, NULL))
		err = -ENOMEM;

	return err;
}

static int pohmelfs_crypto_hash_init(struct pohmelfs_config_group *g, struct pohmelfs_crypto *c)
{
	char *algo = (char *)c->data;
	u8 *key = (u8 *)(algo + c->strlen);

	if (g->hash_string)
		return -EEXIST;

	g->hash_string = kstrdup(algo, GFP_KERNEL);
	if (!g->hash_string)
		return -ENOMEM;
	g->hash_strlen = c->strlen;
	g->hash_keysize = c->keysize;

	g->hash_key = kmalloc(c->keysize, GFP_KERNEL);
	if (!g->hash_key) {
		kfree(g->hash_string);
		return -ENOMEM;
	}

	memcpy(g->hash_key, key, c->keysize);

	return 0;
}

static int pohmelfs_crypto_cipher_init(struct pohmelfs_config_group *g, struct pohmelfs_crypto *c)
{
	char *algo = (char *)c->data;
	u8 *key = (u8 *)(algo + c->strlen);

	if (g->cipher_string)
		return -EEXIST;

	g->cipher_string = kstrdup(algo, GFP_KERNEL);
	if (!g->cipher_string)
		return -ENOMEM;
	g->cipher_strlen = c->strlen;
	g->cipher_keysize = c->keysize;

	g->cipher_key = kmalloc(c->keysize, GFP_KERNEL);
	if (!g->cipher_key) {
		kfree(g->cipher_string);
		return -ENOMEM;
	}

	memcpy(g->cipher_key, key, c->keysize);

	return 0;
}

static int pohmelfs_cn_crypto(struct cn_msg *msg)
{
	struct pohmelfs_crypto *crypto = (struct pohmelfs_crypto *)msg->data;
	struct pohmelfs_config_group *g;
	int err = 0;

	dprintk("%s: idx: %u, strlen: %u, type: %u, keysize: %u, algo: %s.\n",
			__func__, crypto->idx, crypto->strlen, crypto->type,
			crypto->keysize, (char *)crypto->data);

	mutex_lock(&pohmelfs_config_lock);
	g = pohmelfs_find_create_config_group(crypto->idx);
	if (!g) {
		err = -ENOMEM;
		goto out_unlock;
	}

	switch (crypto->type) {
		case POHMELFS_CRYPTO_HASH:
			err = pohmelfs_crypto_hash_init(g, crypto);
			break;
		case POHMELFS_CRYPTO_CIPHER:
			err = pohmelfs_crypto_cipher_init(g, crypto);
			break;
		default:
			err = -ENOTSUPP;
			break;
	}

out_unlock:
	mutex_unlock(&pohmelfs_config_lock);
	if (pohmelfs_send_reply(err, 0, POHMELFS_NOINFO_ACK, msg, NULL))
		err = -ENOMEM;

	return err;
}

static void pohmelfs_cn_callback(struct cn_msg *msg, struct netlink_skb_parms *nsp)
{
	int err;

	if (!cap_raised(nsp->eff_cap, CAP_SYS_ADMIN))
		return;

	switch (msg->flags) {
		case POHMELFS_FLAGS_ADD:
		case POHMELFS_FLAGS_DEL:
		case POHMELFS_FLAGS_MODIFY:
			err = pohmelfs_cn_ctl(msg, msg->flags);
			break;
		case POHMELFS_FLAGS_FLUSH:
			err = pohmelfs_cn_flush(msg);
			break;
		case POHMELFS_FLAGS_SHOW:
			err = pohmelfs_cn_disp(msg);
			break;
		case POHMELFS_FLAGS_DUMP:
			err = pohmelfs_cn_dump(msg);
			break;
		case POHMELFS_FLAGS_CRYPTO:
			err = pohmelfs_cn_crypto(msg);
			break;
		default:
			err = -ENOSYS;
			break;
	}
}

int pohmelfs_config_check(struct pohmelfs_config *config, int idx)
{
	struct pohmelfs_ctl *ctl = &config->state.ctl;
	struct pohmelfs_config *tmp;
	int err = -ENOENT;
	struct pohmelfs_ctl *sc;
	struct pohmelfs_config_group *g;

	mutex_lock(&pohmelfs_config_lock);

	g = pohmelfs_find_config_group(ctl->idx);
	if (g) {
		list_for_each_entry(tmp, &g->config_list, config_entry) {
			sc = &tmp->state.ctl;

			if (pohmelfs_config_eql(sc, ctl)) {
				err = 0;
				break;
			}
		}
	}

	mutex_unlock(&pohmelfs_config_lock);

	return err;
}

int __init pohmelfs_config_init(void)
{
	/* XXX remove (void *) cast when vanilla connector got synced */
	return cn_add_callback(&pohmelfs_cn_id, "pohmelfs", (void *)pohmelfs_cn_callback);
}

void pohmelfs_config_exit(void)
{
	struct pohmelfs_config *c, *tmp;
	struct pohmelfs_config_group *g, *gtmp;

	cn_del_callback(&pohmelfs_cn_id);

	mutex_lock(&pohmelfs_config_lock);
	list_for_each_entry_safe(g, gtmp, &pohmelfs_config_list, group_entry) {
		list_for_each_entry_safe(c, tmp, &g->config_list, config_entry) {
			list_del(&c->config_entry);
			kfree(c);
		}

		list_del(&g->group_entry);

		if (g->hash_string)
			kfree(g->hash_string);

		if (g->cipher_string)
			kfree(g->cipher_string);

		kfree(g);
	}
	mutex_unlock(&pohmelfs_config_lock);
}
