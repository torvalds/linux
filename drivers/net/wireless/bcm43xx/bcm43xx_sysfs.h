#ifndef BCM43xx_SYSFS_H_
#define BCM43xx_SYSFS_H_

#include <linux/device.h>


struct bcm43xx_sysfs {
	struct device_attribute attr_sprom;
	struct device_attribute attr_interfmode;
	struct device_attribute attr_preamble;
};

#define devattr_to_bcm(attr, attr_name)	({				\
	struct bcm43xx_sysfs *__s; struct bcm43xx_private *__p;		\
	__s = container_of((attr), struct bcm43xx_sysfs, attr_name);	\
	__p = container_of(__s, struct bcm43xx_private, sysfs);		\
	__p;								\
					})

struct bcm43xx_private;

int bcm43xx_sysfs_register(struct bcm43xx_private *bcm);
void bcm43xx_sysfs_unregister(struct bcm43xx_private *bcm);

#endif /* BCM43xx_SYSFS_H_ */
