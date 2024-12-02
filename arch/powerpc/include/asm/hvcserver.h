/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * hvcserver.h
 * Copyright (C) 2004 Ryan S Arnold, IBM Corporation
 *
 * PPC64 virtual I/O console server support.
 */

#ifndef _PPC64_HVCSERVER_H
#define _PPC64_HVCSERVER_H
#ifdef __KERNEL__

#include <linux/list.h>

/* Converged Location Code length */
#define HVCS_CLC_LENGTH	79

/**
 * hvcs_partner_info - an element in a list of partner info
 * @node: list_head denoting this partner_info struct's position in the list of
 *	partner info.
 * @unit_address: The partner unit address of this entry.
 * @partition_ID: The partner partition ID of this entry.
 * @location_code: The converged location code of this entry + 1 char for the
 *	null-term.
 *
 * This structure outlines the format that partner info is presented to a caller
 * of the hvcs partner info fetching functions.  These are strung together into
 * a list using linux kernel lists.
 */
struct hvcs_partner_info {
	struct list_head node;
	uint32_t unit_address;
	uint32_t partition_ID;
	char location_code[HVCS_CLC_LENGTH + 1]; /* CLC + 1 null-term char */
};

extern int hvcs_free_partner_info(struct list_head *head);
extern int hvcs_get_partner_info(uint32_t unit_address,
		struct list_head *head, unsigned long *pi_buff);
extern int hvcs_register_connection(uint32_t unit_address,
		uint32_t p_partition_ID, uint32_t p_unit_address);
extern int hvcs_free_connection(uint32_t unit_address);

#endif /* __KERNEL__ */
#endif /* _PPC64_HVCSERVER_H */
