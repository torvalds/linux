/* drivers/message/fusion/linux_compat.h */

#ifndef FUSION_LINUX_COMPAT_H
#define FUSION_LINUX_COMPAT_H

#include <linux/version.h>
#include <scsi/scsi_device.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,6))
static int inline scsi_device_online(struct scsi_device *sdev)
{
	return sdev->online;
}
#endif


/*}-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#endif /* _LINUX_COMPAT_H */
