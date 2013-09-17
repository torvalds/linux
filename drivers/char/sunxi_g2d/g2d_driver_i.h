/* g2d_driver_i.h
 *
 * Copyright (c)	2011 xxxx Electronics
 *					2011 Yupu Tang
 *
 * @ F23 G2D driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA02111-1307USA
 */

#ifndef __G2D_DRIVER_I_H
#define __G2D_DRIVER_I_H

#include"g2d_bsp.h"

#define G2D_DEBUG	1
#ifdef	G2D_DEBUG
#define	DBG(format, args...) printk(KERN_DEBUG "%s: " format, "G2D", ## args)
#else
#define	DBG(format, args...)
#endif
#define ERR(format, args...) printk(KERN_ERR "%s: " format, "G2D", ## args)
#define WARNING(format, args...) printk(KERN_WARNING "%s: " format, "G2D", ## args)
#define INFO(format, args...) printk(KERN_INFO "%s: " format, "G2D", ## args)

#define MAX_G2D_MEM_INDEX	1000
#define	INTC_IRQNO_DE_MIX	SW_INT_IRQNO_MP

struct info_mem
{
	unsigned long	 phy_addr;
	void			*virt_addr;
    __u32			 b_used;
	__u32			 mem_len;
};

typedef struct
{
	struct device		*dev;
	struct resource		*mem;
	void __iomem		*io;
	__u32				 irq;
	struct mutex		 mutex;

}__g2d_info_t;

typedef struct
{
    __u32				 mid;
    __u32				 used;
    __u32				 status;
    struct semaphore	*g2d_finished_sem;
    struct semaphore	*event_sem;
	wait_queue_head_t	 queue;
	__u32				 finish_flag;

}__g2d_drv_t;

struct g2d_alloc_struct
{
	__u32	address;
	__u32	size;
	__u32	u_size;
	struct	g2d_alloc_struct *next;
};

#endif	/* __G2D_DRIVER_I_H */
