/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000, 2003 Ralf Baechle
 * Copyright (C) 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_SN_IO_H
#define _ASM_SN_IO_H

#if defined(CONFIG_SGI_IP27)
#include <asm/sn/sn0/hubio.h>
#endif


#define IIO_ITTE_BASE		0x400160 /* base of translation table entries */
#define IIO_ITTE(bigwin)	(IIO_ITTE_BASE + 8*(bigwin))

#define IIO_ITTE_OFFSET_BITS	5	/* size of offset field */
#define IIO_ITTE_OFFSET_MASK	((1<<IIO_ITTE_OFFSET_BITS)-1)
#define IIO_ITTE_OFFSET_SHIFT	0

#define IIO_ITTE_WIDGET_BITS	4	/* size of widget field */
#define IIO_ITTE_WIDGET_MASK	((1<<IIO_ITTE_WIDGET_BITS)-1)
#define IIO_ITTE_WIDGET_SHIFT	8

#define IIO_ITTE_IOSP		1	/* I/O Space bit */
#define IIO_ITTE_IOSP_MASK	1
#define IIO_ITTE_IOSP_SHIFT	12
#define HUB_PIO_MAP_TO_MEM	0
#define HUB_PIO_MAP_TO_IO	1

#define IIO_ITTE_INVALID_WIDGET	3	/* an invalid widget  */

#define IIO_ITTE_PUT(nasid, bigwin, io_or_mem, widget, addr) \
	REMOTE_HUB_S((nasid), IIO_ITTE(bigwin), \
		(((((addr) >> BWIN_SIZE_BITS) & \
		   IIO_ITTE_OFFSET_MASK) << IIO_ITTE_OFFSET_SHIFT) | \
		(io_or_mem << IIO_ITTE_IOSP_SHIFT) | \
		(((widget) & IIO_ITTE_WIDGET_MASK) << IIO_ITTE_WIDGET_SHIFT)))

#define IIO_ITTE_DISABLE(nasid, bigwin) \
	IIO_ITTE_PUT((nasid), HUB_PIO_MAP_TO_MEM, \
		     (bigwin), IIO_ITTE_INVALID_WIDGET, 0)

#define IIO_ITTE_GET(nasid, bigwin) REMOTE_HUB_ADDR((nasid), IIO_ITTE(bigwin))

/*
 * Macro which takes the widget number, and returns the
 * IO PRB address of that widget.
 * value _x is expected to be a widget number in the range
 * 0, 8 - 0xF
 */
#define	IIO_IOPRB(_x)	(IIO_IOPRB_0 + ( ( (_x) < HUB_WIDGET_ID_MIN ? \
			(_x) : \
			(_x) - (HUB_WIDGET_ID_MIN-1)) << 3) )

#endif /* _ASM_SN_IO_H */
