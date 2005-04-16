/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 * 
 * Copyright (C) 2001-2003 Silicon Graphics, Inc. All rights reserved.
 *
 */
#include <linux/module.h>
#include <asm/pgalloc.h>

/**
 * sn_flush_all_caches - flush a range of address from all caches (incl. L4)
 * @flush_addr: identity mapped region 7 address to start flushing
 * @bytes: number of bytes to flush
 *
 * Flush a range of addresses from all caches including L4. 
 * All addresses fully or partially contained within 
 * @flush_addr to @flush_addr + @bytes are flushed
 * from the all caches.
 */
void
sn_flush_all_caches(long flush_addr, long bytes)
{
	flush_icache_range(flush_addr, flush_addr+bytes);
	/*
	 * The last call may have returned before the caches
	 * were actually flushed, so we call it again to make
	 * sure.
	 */
	flush_icache_range(flush_addr, flush_addr+bytes);
	mb();
}
EXPORT_SYMBOL(sn_flush_all_caches);
