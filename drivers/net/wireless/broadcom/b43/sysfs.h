#ifndef B43_SYSFS_H_
#define B43_SYSFS_H_

struct b43_wldev;

int b43_sysfs_register(struct b43_wldev *dev);
void b43_sysfs_unregister(struct b43_wldev *dev);

#endif /* B43_SYSFS_H_ */
