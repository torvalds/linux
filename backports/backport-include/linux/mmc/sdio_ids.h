#ifndef __BACKPORT_MMC_SDIO_IDS_H
#define __BACKPORT_MMC_SDIO_IDS_H
#include <linux/version.h>
#include_next <linux/mmc/sdio_ids.h>

#ifndef SDIO_CLASS_BT_AMP
#define SDIO_CLASS_BT_AMP	0x09	/* Type-A Bluetooth AMP interface */
#endif

#ifndef SDIO_DEVICE_ID_MARVELL_8688WLAN
#define SDIO_DEVICE_ID_MARVELL_8688WLAN		0x9104
#endif

#endif /* __BACKPORT_MMC_SDIO_IDS_H */
