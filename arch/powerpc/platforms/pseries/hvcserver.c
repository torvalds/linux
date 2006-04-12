/*
 * hvcserver.c
 * Copyright (C) 2004 Ryan S Arnold, IBM Corporation
 *
 * PPC64 virtual I/O console server support.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <asm/hvcall.h>
#include <asm/hvcserver.h>
#include <asm/io.h>

#define HVCS_ARCH_VERSION "1.0.0"

MODULE_AUTHOR("Ryan S. Arnold <rsa@us.ibm.com>");
MODULE_DESCRIPTION("IBM hvcs ppc64 API");
MODULE_LICENSE("GPL");
MODULE_VERSION(HVCS_ARCH_VERSION);

/*
 * Convert arch specific return codes into relevant errnos.  The hvcs
 * functions aren't performance sensitive, so this conversion isn't an
 * issue.
 */
static int hvcs_convert(long to_convert)
{
	switch (to_convert) {
		case H_SUCCESS:
			return 0;
		case H_PARAMETER:
			return -EINVAL;
		case H_HARDWARE:
			return -EIO;
		case H_BUSY:
		case H_LONG_BUSY_ORDER_1_MSEC:
		case H_LONG_BUSY_ORDER_10_MSEC:
		case H_LONG_BUSY_ORDER_100_MSEC:
		case H_LONG_BUSY_ORDER_1_SEC:
		case H_LONG_BUSY_ORDER_10_SEC:
		case H_LONG_BUSY_ORDER_100_SEC:
			return -EBUSY;
		case H_FUNCTION: /* fall through */
		default:
			return -EPERM;
	}
}

/**
 * hvcs_free_partner_info - free pi allocated by hvcs_get_partner_info
 * @head: list_head pointer for an allocated list of partner info structs to
 *	free.
 *
 * This function is used to free the partner info list that was returned by
 * calling hvcs_get_partner_info().
 */
int hvcs_free_partner_info(struct list_head *head)
{
	struct hvcs_partner_info *pi;
	struct list_head *element;

	if (!head)
		return -EINVAL;

	while (!list_empty(head)) {
		element = head->next;
		pi = list_entry(element, struct hvcs_partner_info, node);
		list_del(element);
		kfree(pi);
	}

	return 0;
}
EXPORT_SYMBOL(hvcs_free_partner_info);

/* Helper function for hvcs_get_partner_info */
static int hvcs_next_partner(uint32_t unit_address,
		unsigned long last_p_partition_ID,
		unsigned long last_p_unit_address, unsigned long *pi_buff)

{
	long retval;
	retval = plpar_hcall_norets(H_VTERM_PARTNER_INFO, unit_address,
			last_p_partition_ID,
				last_p_unit_address, virt_to_phys(pi_buff));
	return hvcs_convert(retval);
}

/**
 * hvcs_get_partner_info - Get all of the partner info for a vty-server adapter
 * @unit_address: The unit_address of the vty-server adapter for which this
 *	function is fetching partner info.
 * @head: An initialized list_head pointer to an empty list to use to return the
 *	list of partner info fetched from the hypervisor to the caller.
 * @pi_buff: A page sized buffer pre-allocated prior to calling this function
 *	that is to be used to be used by firmware as an iterator to keep track
 *	of the partner info retrieval.
 *
 * This function returns non-zero on success, or if there is no partner info.
 *
 * The pi_buff is pre-allocated prior to calling this function because this
 * function may be called with a spin_lock held and kmalloc of a page is not
 * recommended as GFP_ATOMIC.
 *
 * The first long of this buffer is used to store a partner unit address.  The
 * second long is used to store a partner partition ID and starting at
 * pi_buff[2] is the 79 character Converged Location Code (diff size than the
 * unsigned longs, hence the casting mumbo jumbo you see later).
 *
 * Invocation of this function should always be followed by an invocation of
 * hvcs_free_partner_info() using a pointer to the SAME list head instance
 * that was passed as a parameter to this function.
 */
int hvcs_get_partner_info(uint32_t unit_address, struct list_head *head,
		unsigned long *pi_buff)
{
	/*
	 * Dealt with as longs because of the hcall interface even though the
	 * values are uint32_t.
	 */
	unsigned long	last_p_partition_ID;
	unsigned long	last_p_unit_address;
	struct hvcs_partner_info *next_partner_info = NULL;
	int more = 1;
	int retval;

	memset(pi_buff, 0x00, PAGE_SIZE);
	/* invalid parameters */
	if (!head || !pi_buff)
		return -EINVAL;

	last_p_partition_ID = last_p_unit_address = ~0UL;
	INIT_LIST_HEAD(head);

	do {
		retval = hvcs_next_partner(unit_address, last_p_partition_ID,
				last_p_unit_address, pi_buff);
		if (retval) {
			/*
			 * Don't indicate that we've failed if we have
			 * any list elements.
			 */
			if (!list_empty(head))
				return 0;
			return retval;
		}

		last_p_partition_ID = pi_buff[0];
		last_p_unit_address = pi_buff[1];

		/* This indicates that there are no further partners */
		if (last_p_partition_ID == ~0UL
				&& last_p_unit_address == ~0UL)
			break;

		/* This is a very small struct and will be freed soon in
		 * hvcs_free_partner_info(). */
		next_partner_info = kmalloc(sizeof(struct hvcs_partner_info),
				GFP_ATOMIC);

		if (!next_partner_info) {
			printk(KERN_WARNING "HVCONSOLE: kmalloc() failed to"
				" allocate partner info struct.\n");
			hvcs_free_partner_info(head);
			return -ENOMEM;
		}

		next_partner_info->unit_address
			= (unsigned int)last_p_unit_address;
		next_partner_info->partition_ID
			= (unsigned int)last_p_partition_ID;

		/* copy the Null-term char too */
		strncpy(&next_partner_info->location_code[0],
			(char *)&pi_buff[2],
			strlen((char *)&pi_buff[2]) + 1);

		list_add_tail(&(next_partner_info->node), head);
		next_partner_info = NULL;

	} while (more);

	return 0;
}
EXPORT_SYMBOL(hvcs_get_partner_info);

/**
 * hvcs_register_connection - establish a connection between this vty-server and
 *	a vty.
 * @unit_address: The unit address of the vty-server adapter that is to be
 *	establish a connection.
 * @p_partition_ID: The partition ID of the vty adapter that is to be connected.
 * @p_unit_address: The unit address of the vty adapter to which the vty-server
 *	is to be connected.
 *
 * If this function is called once and -EINVAL is returned it may
 * indicate that the partner info needs to be refreshed for the
 * target unit address at which point the caller must invoke
 * hvcs_get_partner_info() and then call this function again.  If,
 * for a second time, -EINVAL is returned then it indicates that
 * there is probably already a partner connection registered to a
 * different vty-server adapter.  It is also possible that a second
 * -EINVAL may indicate that one of the parms is not valid, for
 * instance if the link was removed between the vty-server adapter
 * and the vty adapter that you are trying to open.  Don't shoot the
 * messenger.  Firmware implemented it this way.
 */
int hvcs_register_connection( uint32_t unit_address,
		uint32_t p_partition_ID, uint32_t p_unit_address)
{
	long retval;
	retval = plpar_hcall_norets(H_REGISTER_VTERM, unit_address,
				p_partition_ID, p_unit_address);
	return hvcs_convert(retval);
}
EXPORT_SYMBOL(hvcs_register_connection);

/**
 * hvcs_free_connection - free the connection between a vty-server and vty
 * @unit_address: The unit address of the vty-server that is to have its
 *	connection severed.
 *
 * This function is used to free the partner connection between a vty-server
 * adapter and a vty adapter.
 *
 * If -EBUSY is returned continue to call this function until 0 is returned.
 */
int hvcs_free_connection(uint32_t unit_address)
{
	long retval;
	retval = plpar_hcall_norets(H_FREE_VTERM, unit_address);
	return hvcs_convert(retval);
}
EXPORT_SYMBOL(hvcs_free_connection);
