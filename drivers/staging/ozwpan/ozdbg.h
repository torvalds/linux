/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * ---------------------------------------------------------------------------*/

#ifndef _OZDBG_H
#define _OZDBG_H

#define OZ_WANT_DBG 0
#define OZ_WANT_VERBOSE_DBG 1

#define OZ_DBG_ON		0x0
#define OZ_DBG_STREAM		0x1
#define OZ_DBG_URB		0x2
#define OZ_DBG_CTRL_DETAIL	0x4
#define OZ_DBG_HUB		0x8
#define OZ_DBG_RX_FRAMES	0x10
#define OZ_DBG_TX_FRAMES	0x20

#define OZ_DEFAULT_DBG_MASK			\
	(					\
	/* OZ_DBG_STREAM | */			\
	/* OZ_DBG_URB | */			\
	/* OZ_DBG_CTRL_DETAIL | */		\
	OZ_DBG_HUB |				\
	/* OZ_DBG_RX_FRAMES | */		\
	/* OZ_DBG_TX_FRAMES | */		\
	0)

extern unsigned int oz_dbg_mask;

#define oz_want_dbg(mask)						\
	((OZ_WANT_DBG && (OZ_DBG_##mask == OZ_DBG_ON)) ||		\
	 (OZ_WANT_VERBOSE_DBG && (OZ_DBG_##mask & oz_dbg_mask)))

#define oz_dbg(mask, fmt, ...)						\
do {									\
	if (oz_want_dbg(mask))						\
		pr_debug(fmt, ##__VA_ARGS__);				\
} while (0)

#define oz_cdev_dbg(cdev, mask, fmt, ...)				\
do {									\
	if (oz_want_dbg(mask))						\
		netdev_dbg((cdev)->dev, fmt, ##__VA_ARGS__);		\
} while (0)

#define oz_pd_dbg(pd, mask, fmt, ...)					\
do {									\
	if (oz_want_dbg(mask))						\
		pr_debug(fmt, ##__VA_ARGS__);				\
} while (0)

#endif /* _OZDBG_H */
