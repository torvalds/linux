/*
 * arch/ppc/boot/simple/openbios.c
 *
 * 2005 (c) SYSGO AG - g.jaeger@sysgo.com
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without
 * any warranty of any kind, whether express or implied.
 *
 * Derived from arch/ppc/boot/simple/pibs.c (from MontaVista)
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/string.h>
#include <asm/ppcboot.h>
#include <platforms/4xx/ebony.h>

extern unsigned long decompress_kernel(unsigned long load_addr, int num_words,
				       unsigned long cksum);

/* We need to make sure that this is before the images to ensure
 * that it's in a mapped location. */
bd_t hold_resid_buf __attribute__ ((__section__ (".data.boot")));
bd_t *hold_residual = &hold_resid_buf;

void *
load_kernel(unsigned long load_addr, int num_words, unsigned long cksum,
		void *ign1, void *ign2)
{
	decompress_kernel(load_addr, num_words, cksum);

	/* simply copy the MAC addresses */
	memcpy(hold_residual->bi_enetaddr,  (char *)EBONY_OPENBIOS_MAC_BASE, 6);
	memcpy(hold_residual->bi_enet1addr, (char *)(EBONY_OPENBIOS_MAC_BASE+EBONY_OPENBIOS_MAC_OFFSET), 6);

	return (void *)hold_residual;
}
