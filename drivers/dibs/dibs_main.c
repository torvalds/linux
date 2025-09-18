// SPDX-License-Identifier: GPL-2.0
/*
 *  DIBS - Direct Internal Buffer Sharing
 *
 *  Implementation of the DIBS class module
 *
 *  Copyright IBM Corp. 2025
 */
#define KMSG_COMPONENT "dibs"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/dibs.h>

MODULE_DESCRIPTION("Direct Internal Buffer Sharing class");
MODULE_LICENSE("GPL");

/* use an array rather a list for fast mapping: */
static struct dibs_client *clients[MAX_DIBS_CLIENTS];
static u8 max_client;
static DEFINE_MUTEX(clients_lock);

int dibs_register_client(struct dibs_client *client)
{
	int i, rc = -ENOSPC;

	mutex_lock(&clients_lock);
	for (i = 0; i < MAX_DIBS_CLIENTS; ++i) {
		if (!clients[i]) {
			clients[i] = client;
			client->id = i;
			if (i == max_client)
				max_client++;
			rc = 0;
			break;
		}
	}
	mutex_unlock(&clients_lock);

	return rc;
}
EXPORT_SYMBOL_GPL(dibs_register_client);

int dibs_unregister_client(struct dibs_client *client)
{
	int rc = 0;

	mutex_lock(&clients_lock);
	clients[client->id] = NULL;
	if (client->id + 1 == max_client)
		max_client--;
	mutex_unlock(&clients_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(dibs_unregister_client);

static int __init dibs_init(void)
{
	memset(clients, 0, sizeof(clients));
	max_client = 0;

	return 0;
}

static void __exit dibs_exit(void)
{
}

module_init(dibs_init);
module_exit(dibs_exit);
