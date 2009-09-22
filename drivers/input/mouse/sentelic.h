/*-
 * Finger Sensing Pad PS/2 mouse driver.
 *
 * Copyright (C) 2005-2007 Asia Vital Components Co., Ltd.
 * Copyright (C) 2005-2009 Tai-hwa Liang, Sentelic Corporation.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2
 *   of the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef	__SENTELIC_H
#define	__SENTELIC_H

/* Finger-sensing Pad information registers */
#define	FSP_REG_DEVICE_ID	0x00
#define	FSP_REG_VERSION		0x01
#define	FSP_REG_REVISION	0x04
#define	FSP_REG_TMOD_STATUS1	0x0B
#define	FSP_BIT_NO_ROTATION	BIT(3)
#define	FSP_REG_PAGE_CTRL	0x0F

/* Finger-sensing Pad control registers */
#define	FSP_REG_SYSCTL1		0x10
#define	FSP_BIT_EN_REG_CLK	BIT(5)
#define	FSP_REG_OPC_QDOWN	0x31
#define	FSP_BIT_EN_OPC_TAG	BIT(7)
#define	FSP_REG_OPTZ_XLO	0x34
#define	FSP_REG_OPTZ_XHI	0x35
#define	FSP_REG_OPTZ_YLO	0x36
#define	FSP_REG_OPTZ_YHI	0x37
#define	FSP_REG_SYSCTL5		0x40
#define	FSP_BIT_90_DEGREE	BIT(0)
#define	FSP_BIT_EN_MSID6	BIT(1)
#define	FSP_BIT_EN_MSID7	BIT(2)
#define	FSP_BIT_EN_MSID8	BIT(3)
#define	FSP_BIT_EN_AUTO_MSID8	BIT(5)
#define	FSP_BIT_EN_PKT_G0	BIT(6)

#define	FSP_REG_ONPAD_CTL	0x43
#define	FSP_BIT_ONPAD_ENABLE	BIT(0)
#define	FSP_BIT_ONPAD_FBBB	BIT(1)
#define	FSP_BIT_FIX_VSCR	BIT(3)
#define	FSP_BIT_FIX_HSCR	BIT(5)
#define	FSP_BIT_DRAG_LOCK	BIT(6)

/* Finger-sensing Pad packet formating related definitions */

/* absolute packet type */
#define	FSP_PKT_TYPE_NORMAL	(0x00)
#define	FSP_PKT_TYPE_ABS	(0x01)
#define	FSP_PKT_TYPE_NOTIFY	(0x02)
#define	FSP_PKT_TYPE_NORMAL_OPC	(0x03)
#define	FSP_PKT_TYPE_SHIFT	(6)

#ifdef __KERNEL__

struct fsp_data {
	unsigned char	ver;		/* hardware version */
	unsigned char	rev;		/* hardware revison */
	unsigned char	buttons;	/* Number of buttons */
	unsigned int	flags;
#define	FSPDRV_FLAG_EN_OPC	(0x001)	/* enable on-pad clicking */

	bool		vscroll;	/* Vertical scroll zone enabled */
	bool		hscroll;	/* Horizontal scroll zone enabled */

	unsigned char	last_reg;	/* Last register we requested read from */
	unsigned char	last_val;
};

#ifdef CONFIG_MOUSE_PS2_SENTELIC
extern int fsp_detect(struct psmouse *psmouse, bool set_properties);
extern int fsp_init(struct psmouse *psmouse);
#else
inline int fsp_detect(struct psmouse *psmouse, bool set_properties)
{
	return -ENOSYS;
}
inline int fsp_init(struct psmouse *psmouse)
{
	return -ENOSYS;
}
#endif

#endif	/* __KERNEL__ */

#endif	/* !__SENTELIC_H */
