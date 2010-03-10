/* $Id: capifs.h,v 1.1.2.2 2004/01/16 21:09:26 keil Exp $
 * 
 * Copyright 2000 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/dcache.h>

#if defined(CONFIG_ISDN_CAPI_CAPIFS) || defined(CONFIG_ISDN_CAPI_CAPIFS_MODULE)

struct dentry *capifs_new_ncci(unsigned int num, dev_t device);
void capifs_free_ncci(struct dentry *dentry);

#else

static inline struct dentry *capifs_new_ncci(unsigned int num, dev_t device)
{
	return NULL;
}

static inline void capifs_free_ncci(struct dentry *dentry)
{
}

#endif
