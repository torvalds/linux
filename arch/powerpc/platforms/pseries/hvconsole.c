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
#include <linux/export.h>
#include <linux/errno.h>
#include <asm/hvcall.h>
#include <asm/hvconsole.h>
#include <asm/plpar_wrappers.h>

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
	long ret;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	unsigned long *lbuf = (unsigned long *)buf;

	ret = plpar_hcall(H_GET_TERM_CHAR, retbuf, vtermno);
	lbuf[0] = be64_to_cpu(retbuf[1]);
	lbuf[1] = be64_to_cpu(retbuf[2]);

	if (ret == H_SUCCESS)
		return retbuf[0];

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


	/* hcall will ret H_PARAMETER if 'count' exceeds firmware max.*/
	if (count > MAX_VIO_PUT_CHARS)
		count = MAX_VIO_PUT_CHARS;

	ret = plpar_hcall_norets(H_PUT_TERM_CHAR, vtermno, count,
				 cpu_to_be64(lbuf[0]),
				 cpu_to_be64(lbuf[1]));
	if (ret == H_SUCCESS)
		return count;
	if (ret == H_BUSY)
		return -EAGAIN;
	return -EIO;
}

EXPORT_SYMBOL(hvc_put_chars);
