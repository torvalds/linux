/*
 * rbtx4939 specific prom routines
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/rbtx4939.h>

void __init rbtx4939_prom_init(void)
{
	tx4939_add_memory_regions();
	txx9_sio_putchar_init(TX4939_SIO_REG(0) & 0xfffffffffULL);
}
