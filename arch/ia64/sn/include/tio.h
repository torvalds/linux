/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2005 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef _ASM_IA64_SN_TIO_H
#define _ASM_IA64_SN_TIO_H

#define	TIO_MMR_ADDR_MOD

#define TIO_NODE_ID     TIO_MMR_ADDR_MOD(0x0000000090060e80)

#define TIO_ITTE_BASE   0xb0008800        /* base of translation table entries */
#define TIO_ITTE(bigwin)        (TIO_ITTE_BASE + 8*(bigwin))

#define TIO_ITTE_OFFSET_BITS    8       /* size of offset field */
#define TIO_ITTE_OFFSET_MASK    ((1<<TIO_ITTE_OFFSET_BITS)-1)
#define TIO_ITTE_OFFSET_SHIFT   0

#define TIO_ITTE_WIDGET_BITS    2       /* size of widget field */
#define TIO_ITTE_WIDGET_MASK    ((1<<TIO_ITTE_WIDGET_BITS)-1)
#define TIO_ITTE_WIDGET_SHIFT   12
#define TIO_ITTE_VALID_MASK	0x1
#define TIO_ITTE_VALID_SHIFT	16

#define TIO_ITTE_WIDGET(itte) \
	(((itte) >> TIO_ITTE_WIDGET_SHIFT) & TIO_ITTE_WIDGET_MASK)
#define TIO_ITTE_VALID(itte) \
	(((itte) >> TIO_ITTE_VALID_SHIFT) & TIO_ITTE_VALID_MASK)

#define TIO_ITTE_PUT(nasid, bigwin, widget, addr, valid) \
        REMOTE_HUB_S((nasid), TIO_ITTE(bigwin), \
                (((((addr) >> TIO_BWIN_SIZE_BITS) & \
                   TIO_ITTE_OFFSET_MASK) << TIO_ITTE_OFFSET_SHIFT) | \
                (((widget) & TIO_ITTE_WIDGET_MASK) << TIO_ITTE_WIDGET_SHIFT)) | \
		(( (valid) & TIO_ITTE_VALID_MASK) << TIO_ITTE_VALID_SHIFT))

#endif /*  _ASM_IA64_SN_TIO_H */
