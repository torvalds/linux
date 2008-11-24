/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __IFACE_H__
#define __IFACE_H__

#define Kb  (1024)
#define Mb  (Kb*Kb)

#define VIA_K800_BRIDGE_VID         0x1106
#define VIA_K800_BRIDGE_DID         0x3204

#define VIA_K800_SYSTEM_MEMORY_REG  0x47
#define VIA_K800_VIDEO_MEMORY_REG   0xA1

extern int viafb_memsize;
unsigned int viafb_get_memsize(void);
unsigned long viafb_get_videobuf_addr(void);

#endif /* __IFACE_H__ */
