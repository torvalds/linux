/*  
 *  (C) 2004 Margit Schubert-While <margitsw@t-online.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*  
 *	Compatibility header file to aid support of different kernel versions
 */

#ifdef PRISM54_COMPAT24
#include "prismcompat24.h"
#else	/* PRISM54_COMPAT24 */

#ifndef _PRISM_COMPAT_H
#define _PRISM_COMPAT_H

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/config.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>
#include <linux/compiler.h>

#ifndef __iomem
#define __iomem
#endif

#define PRISM_FW_PDEV		&priv->pdev->dev

#endif				/* _PRISM_COMPAT_H */
#endif				/* PRISM54_COMPAT24 */
