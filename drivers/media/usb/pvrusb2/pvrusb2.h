/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *  Copyright (C) 2004 Aurelien Alleaume <slts@free.fr>
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
