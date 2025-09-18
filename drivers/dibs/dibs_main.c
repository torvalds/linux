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
