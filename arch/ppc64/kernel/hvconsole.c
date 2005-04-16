/*
 * hvconsole.c
 * Copyright (C) 2004 Hollis Blanchard, IBM Corporation
 * Copyright (C) 2004 IBM Corporation
 *
 * Additional Author(s):
 *  Ryan S. Arnold <rsa@us.ibm.com>
 *
 * LPAR console support.
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
#include <linux/module.h>
#include <asm/hvcall.h>
#include <asm/hvconsole.h>
#include <asm/prom.h>

/**
 * hvc_get_chars - retrieve characters from firmware for denoted vterm adatper
 * @vtermno: The vtermno or unit_address of the adapter from which to fetch the
 *	data.
 * @buf: The character buffer into which to put the character data fetched from
 *	firmware.
 * @count: not used?
 */
int hvc_get_chars(uint32_t vtermno, char *buf, int count)
{
	unsigned long got;

	if (plpar_hcall(H_GET_TERM_CHAR, vtermno, 0, 0, 0, &got,
		(unsigned long *)buf, (unsigned long *)buf+1) == H_Success) {
		/*
		 * Work around a HV bug where it gives us a null
		 * after every \r.  -- paulus
		 */
		if (got > 0) {
			int i;
			for (i = 1; i < got; ++i) {
				if (buf[i] == 0 && buf[i-1] == '\r') {
					--got;
					if (i < got)
						memmove(&buf[i], &buf[i+1],
							got - i);
				}
			}
		}
		return got;
	}
	return 0;
}

EXPORT_SYMBOL(hvc_get_chars);

/**
 * hvc_put_chars: send characters to firmware for denoted vterm adapter
 * @vtermno: The vtermno or unit_address of the adapter from which the data
 *	originated.
 * @buf: The character buffer that contains the character data to send to
 *	firmware.
 * @count: Send this number of characters.
 */
int hvc_put_chars(uint32_t vtermno, const char *buf, int count)
{
	unsigned long *lbuf = (unsigned long *) buf;
	long ret;

	ret = plpar_hcall_norets(H_PUT_TERM_CHAR, vtermno, count, lbuf[0],
				 lbuf[1]);
	if (ret == H_Success)
		return count;
	if (ret == H_Busy)
		return 0;
	return -EIO;
}

EXPORT_SYMBOL(hvc_put_chars);

/*
 * We hope/assume that the first vty found corresponds to the first console
 * device.
 */
int hvc_find_vtys(void)
{
	struct device_node *vty;
	int num_found = 0;

	for (vty = of_find_node_by_name(NULL, "vty"); vty != NULL;
			vty = of_find_node_by_name(vty, "vty")) {
		uint32_t *vtermno;

		/* We have statically defined space for only a certain number of
		 * console adapters. */
		if (num_found >= MAX_NR_HVC_CONSOLES)
			break;

		vtermno = (uint32_t *)get_property(vty, "reg", NULL);
		if (!vtermno)
			continue;

		if (device_is_compatible(vty, "hvterm1")) {
			hvc_instantiate(*vtermno, num_found);
			++num_found;
		}
	}

	return num_found;
}
