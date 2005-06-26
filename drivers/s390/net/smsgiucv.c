/*
 * IUCV special message driver
 *
 * Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <asm/cpcmd.h>
#include <asm/ebcdic.h>

#include "iucv.h"

struct smsg_callback {
	struct list_head list;
	char *prefix;
	int len;
	void (*callback)(char *from, char *str);
};

MODULE_AUTHOR
   ("(C) 2003 IBM Corporation by Martin Schwidefsky (schwidefsky@de.ibm.com)");
MODULE_DESCRIPTION ("Linux for S/390 IUCV special message driver");

static iucv_handle_t smsg_handle;
static unsigned short smsg_pathid;
static DEFINE_SPINLOCK(smsg_list_lock);
static struct list_head smsg_list = LIST_HEAD_INIT(smsg_list);

static void
smsg_connection_complete(iucv_ConnectionComplete *eib, void *pgm_data)
{
}


static void
smsg_message_pending(iucv_MessagePending *eib, void *pgm_data)
{
	struct smsg_callback *cb;
	unsigned char *msg;
	unsigned char sender[9];
	unsigned short len;
	int rc, i;

	len = eib->ln1msg2.ipbfln1f;
	msg = kmalloc(len + 1, GFP_ATOMIC|GFP_DMA);
	if (!msg) {
		iucv_reject(eib->ippathid, eib->ipmsgid, eib->iptrgcls);
		return;
	}
	rc = iucv_receive(eib->ippathid, eib->ipmsgid, eib->iptrgcls,
			  msg, len, 0, 0, 0);
	if (rc == 0) {
		msg[len] = 0;
		EBCASC(msg, len);
		memcpy(sender, msg, 8);
		sender[8] = 0;
		/* Remove trailing whitespace from the sender name. */
		for (i = 7; i >= 0; i--) {
			if (sender[i] != ' ' && sender[i] != '\t')
				break;
			sender[i] = 0;
		}
		spin_lock(&smsg_list_lock);
		list_for_each_entry(cb, &smsg_list, list)
			if (strncmp(msg + 8, cb->prefix, cb->len) == 0) {
				cb->callback(sender, msg + 8);
				break;
			}
		spin_unlock(&smsg_list_lock);
	}
	kfree(msg);
}

static iucv_interrupt_ops_t smsg_ops = {
	.ConnectionComplete = smsg_connection_complete,
	.MessagePending     = smsg_message_pending,
};

static struct device_driver smsg_driver = {
	.name = "SMSGIUCV",
	.bus  = &iucv_bus,
};

int
smsg_register_callback(char *prefix, void (*callback)(char *from, char *str))
{
	struct smsg_callback *cb;

	cb = kmalloc(sizeof(struct smsg_callback), GFP_KERNEL);
	if (!cb)
		return -ENOMEM;
	cb->prefix = prefix;
	cb->len = strlen(prefix);
	cb->callback = callback;
	spin_lock(&smsg_list_lock);
	list_add_tail(&cb->list, &smsg_list);
	spin_unlock(&smsg_list_lock);
	return 0;
}

void
smsg_unregister_callback(char *prefix, void (*callback)(char *from, char *str))
{
	struct smsg_callback *cb, *tmp;

	spin_lock(&smsg_list_lock);
	cb = 0;
	list_for_each_entry(tmp, &smsg_list, list)
		if (tmp->callback == callback &&
		    strcmp(tmp->prefix, prefix) == 0) {
			cb = tmp;
			list_del(&cb->list);
			break;
		}
	spin_unlock(&smsg_list_lock);
	kfree(cb);
}

static void __exit
smsg_exit(void)
{
	if (smsg_handle > 0) {
		cpcmd("SET SMSG OFF", NULL, 0, NULL);
		iucv_sever(smsg_pathid, 0);
		iucv_unregister_program(smsg_handle);
		driver_unregister(&smsg_driver);
	}
	return;
}

static int __init
smsg_init(void)
{
	static unsigned char pgmmask[24] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};
	int rc;

	rc = driver_register(&smsg_driver);
	if (rc != 0) {
		printk(KERN_ERR "SMSGIUCV: failed to register driver.\n");
		return rc;
	}
	smsg_handle = iucv_register_program("SMSGIUCV        ", "*MSG    ",
					    pgmmask, &smsg_ops, 0);
	if (!smsg_handle) {
		printk(KERN_ERR "SMSGIUCV: failed to register to iucv");
		driver_unregister(&smsg_driver);
		return -EIO;	/* better errno ? */
	}
	rc = iucv_connect (&smsg_pathid, 1, 0, "*MSG    ", 0, 0, 0, 0,
			   smsg_handle, 0);
	if (rc) {
		printk(KERN_ERR "SMSGIUCV: failed to connect to *MSG");
		iucv_unregister_program(smsg_handle);
		driver_unregister(&smsg_driver);
		smsg_handle = 0;
		return -EIO;
	}
	cpcmd("SET SMSG IUCV", NULL, 0, NULL);
	return 0;
}

module_init(smsg_init);
module_exit(smsg_exit);
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(smsg_register_callback);
EXPORT_SYMBOL(smsg_unregister_callback);
