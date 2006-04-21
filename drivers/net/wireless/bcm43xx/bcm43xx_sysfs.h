#ifndef BCM43xx_SYSFS_H_
#define BCM43xx_SYSFS_H_

struct bcm43xx_private;

int bcm43xx_sysfs_register(struct bcm43xx_private *bcm);
void bcm43xx_sysfs_unregister(struct bcm43xx_private *bcm);

#endif /* BCM43xx_SYSFS_H_ */
