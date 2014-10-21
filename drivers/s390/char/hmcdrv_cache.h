/*
 *    SE/HMC Drive (Read) Cache Functions
 *
 *    Copyright IBM Corp. 2013
 *    Author(s): Ralf Hoppe (rhoppe@de.ibm.com)
 */

#ifndef __HMCDRV_CACHE_H__
#define __HMCDRV_CACHE_H__

#include <linux/mmzone.h>
#include "hmcdrv_ftp.h"

#define HMCDRV_CACHE_SIZE_DFLT	(MAX_ORDER_NR_PAGES * PAGE_SIZE / 2UL)

typedef ssize_t (*hmcdrv_cache_ftpfunc)(const struct hmcdrv_ftp_cmdspec *ftp,
					size_t *fsize);

ssize_t hmcdrv_cache_cmd(const struct hmcdrv_ftp_cmdspec *ftp,
			 hmcdrv_cache_ftpfunc func);
int hmcdrv_cache_startup(size_t cachesize);
void hmcdrv_cache_shutdown(void);

#endif	 /* __HMCDRV_CACHE_H__ */
