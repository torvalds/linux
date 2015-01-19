#ifndef  _OSD_RDMA_H 
#define _OSD_RDMA_H

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <mach/am_regs.h>
#include <mach/power_gate.h>

#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/amlogic/vout/vout_notify.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/clk.h>
#include <linux/amlogic/logo/logo.h>

typedef  struct{
	u32  addr;
	u32  val;
}rdma_table_item_t;

#define TABLE_SIZE	 PAGE_SIZE
#define MAX_TABLE_ITEM	 (TABLE_SIZE/sizeof(rdma_table_item_t))
#define RDMA_CHANNEL_INDEX	3  //auto  1,2,3   manual is 0
#define START_ADDR		(P_RDMA_AHB_START_ADDR_MAN+(RDMA_CHANNEL_INDEX<<3))
#define END_ADDR		(P_RDMA_AHB_END_ADDR_MAN+(RDMA_CHANNEL_INDEX<<3))

#endif
