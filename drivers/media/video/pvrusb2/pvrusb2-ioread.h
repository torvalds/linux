/*
 *
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
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
#ifndef __PVRUSB2_IOREAD_H
#define __PVRUSB2_IOREAD_H

#include "pvrusb2-io.h"

struct pvr2_ioread;

struct pvr2_ioread *pvr2_ioread_create(void);
void pvr2_ioread_destroy(struct pvr2_ioread *);
int pvr2_ioread_setup(struct pvr2_ioread *,struct pvr2_stream *);
struct pvr2_stream *pvr2_ioread_get_stream(struct pvr2_ioread *);
void pvr2_ioread_set_sync_key(struct pvr2_ioread *,
			      const char *sync_key_ptr,
			      unsigned int sync_key_len);
int pvr2_ioread_set_enabled(struct pvr2_ioread *,int fl);
int pvr2_ioread_read(struct pvr2_ioread *,void __user *buf,unsigned int cnt);
int pvr2_ioread_avail(struct pvr2_ioread *);

#endif /* __PVRUSB2_IOREAD_H */

/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 75 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */
