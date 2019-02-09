/*
 *
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *  Copyright (C) 2004 Aurelien Alleaume <slts@free.fr>
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
 */

#ifndef __PVRUSB2_H
#define __PVRUSB2_H

/* Maximum number of pvrusb2 instances we can track at once.  You
   might want to increase this - however the driver operation will not
   be impaired if it is too small.  Instead additional units just
   won't have an ID assigned and it might not be possible to specify
   module parameters for those extra units. */
#define PVR_NUM 20

#endif /* __PVRUSB2_H */
