/*
 * rbtx4939 specific prom routines
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <asm/bootinfo.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/rbtx4939.h>

void __init rbtx4939_prom_init(void)
{
	unsigned long start, size;
	u64 win;
	int i;

	for (i = 0; i < 4; i++) {
		if (!((__u32)____raw_readq(&tx4939_ddrcptr->winen) & (1 << i)))
			continue;
		win = ____raw_readq(&tx4939_ddrcptr->win[i]);
		start = (unsigned long)(win >> 48);
		size = (((unsigned long)(win >> 32) & 0xffff) + 1) - start;
		add_memory_region(start << 20, size << 20, BOOT_MEM_RAM);
	}
	txx9_sio_putchar_init(TX4939_SIO_REG(0) & 0xfffffffffULL);
}
