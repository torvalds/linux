#ifndef __BACKPORT_MMC_SDIO_FUNC_H
#define __BACKPORT_MMC_SDIO_FUNC_H
#include <linux/version.h>
#include_next <linux/mmc/sdio_func.h>

#ifndef dev_to_sdio_func
#define dev_to_sdio_func(d)	container_of(d, struct sdio_func, dev)
#endif

#endif /* __BACKPORT_MMC_SDIO_FUNC_H */
