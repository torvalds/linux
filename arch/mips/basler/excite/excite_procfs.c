/*
 *  Copyright (C) 2004, 2005 by Basler Vision Technologies AG
 *  Author: Thomas Koeller <thomas.koeller@baslerweb.com>
 *
 *  Procfs support for Basler eXcite
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/rm9k-ocd.h>

#include <excite.h>

static int excite_get_unit_id(char *buf, char **addr, off_t offs, int size)
{
	const int len = snprintf(buf, PAGE_SIZE, "%06x", unit_id);
	const int w = len - offs;
	*addr = buf + offs;
	return w < size ? w : size;
}

static int
excite_bootrom_read(char *page, char **start, off_t off, int count,
		  int *eof, void *data)
{
	void __iomem * src;

	if (off >= EXCITE_SIZE_BOOTROM) {
		*eof = 1;
		return 0;
	}

	if ((off + count) > EXCITE_SIZE_BOOTROM)
		count = EXCITE_SIZE_BOOTROM - off;

	src = ioremap(EXCITE_PHYS_BOOTROM + off, count);
	if (src) {
		memcpy_fromio(page, src, count);
		iounmap(src);
		*start = page;
	} else {
		count = -ENOMEM;
	}

	return count;
}

void excite_procfs_init(void)
{
	/* Create & populate /proc/excite */
	struct proc_dir_entry * const pdir = proc_mkdir("excite", &proc_root);
	if (pdir) {
		struct proc_dir_entry * e;

		e = create_proc_info_entry("unit_id", S_IRUGO, pdir,
					   excite_get_unit_id);
		if (e) e->size = 6;

		e = create_proc_read_entry("bootrom", S_IRUGO, pdir,
					   excite_bootrom_read, NULL);
		if (e) e->size = EXCITE_SIZE_BOOTROM;
	}
}
