/* arch/arm/mach-rk2818/proc_comm.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <mach/rk2818_iomap.h>
#include <mach/system.h>

#include "proc_comm.h"

//#define MSM_A2M_INT(n) (MSM_CSR_BASE + 0x400 + (n) * 4)

static inline void notify_other_proc_comm(void)
{
	//writel(1, MSM_A2M_INT(6));
}

#define APP_COMMAND 0x00
#define APP_STATUS  0x04
#define APP_DATA1   0x08
#define APP_DATA2   0x0C

#define MDM_COMMAND 0x10
#define MDM_STATUS  0x14
#define MDM_DATA1   0x18
#define MDM_DATA2   0x1C

static DEFINE_SPINLOCK(proc_comm_lock);

/* The higher level SMD support will install this to
 * provide a way to check for and handle modem restart.
 */
int (*rk2818_check_for_modem_crash)(void);

/* Poll for a state change, checking for possible
 * modem crashes along the way (so we don't wait
 * forever while the ARM9 is blowing up).
 *
 * Return an error in the event of a modem crash and
 * restart so the msm_proc_comm() routine can restart
 * the operation from the beginning.
 */
static int proc_comm_wait_for(void __iomem *addr, unsigned value)
{
	for (;;) {
		if (readl(addr) == value)
			return 0;

		if (rk2818_check_for_modem_crash)
			if (rk2818_check_for_modem_crash())
				return -EAGAIN;
	}
}

int rk2818_proc_comm(unsigned cmd, unsigned *data1, unsigned *data2)
{

	//void __iomem *base = MSM_SHARED_RAM_BASE;
	unsigned long flags;
	int ret=0;

	spin_lock_irqsave(&proc_comm_lock, flags);
	#if 0
	for (;;) {
		if (proc_comm_wait_for(base + MDM_STATUS, PCOM_READY))
			continue;

		writel(cmd, base + APP_COMMAND);
		writel(data1 ? *data1 : 0, base + APP_DATA1);
		writel(data2 ? *data2 : 0, base + APP_DATA2);

		notify_other_proc_comm();

		if (proc_comm_wait_for(base + APP_COMMAND, PCOM_CMD_DONE))
			continue;

		if (readl(base + APP_STATUS) != PCOM_CMD_FAIL) {
			if (data1)
				*data1 = readl(base + APP_DATA1);
			if (data2)
				*data2 = readl(base + APP_DATA2);
			ret = 0;
		} else {
			ret = -EIO;
		}
		break;
	}

	writel(PCOM_CMD_IDLE, base + APP_COMMAND);
	#endif
	spin_unlock_irqrestore(&proc_comm_lock, flags);

	return ret;
}


