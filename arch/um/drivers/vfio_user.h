/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_VFIO_USER_H
#define __UM_VFIO_USER_H

struct uml_vfio_user_device {
	int device;

	struct {
		uint64_t size;
		uint64_t offset;
	} *region;
	int num_regions;

	int32_t *irqfd;
	int irq_count;
};

int uml_vfio_user_open_container(void);
int uml_vfio_user_setup_iommu(int container);

int uml_vfio_user_get_group_id(const char *device);
int uml_vfio_user_open_group(int group_id);
int uml_vfio_user_set_container(int container, int group);
int uml_vfio_user_unset_container(int container, int group);

int uml_vfio_user_setup_device(struct uml_vfio_user_device *dev,
			       int group, const char *device);
void uml_vfio_user_teardown_device(struct uml_vfio_user_device *dev);

int uml_vfio_user_activate_irq(struct uml_vfio_user_device *dev, int index);
void uml_vfio_user_deactivate_irq(struct uml_vfio_user_device *dev, int index);
int uml_vfio_user_update_irqs(struct uml_vfio_user_device *dev);

int uml_vfio_user_cfgspace_read(struct uml_vfio_user_device *dev,
				unsigned int offset, void *buf, int size);
int uml_vfio_user_cfgspace_write(struct uml_vfio_user_device *dev,
				 unsigned int offset, const void *buf, int size);

int uml_vfio_user_bar_read(struct uml_vfio_user_device *dev, int bar,
			   unsigned int offset, void *buf, int size);
int uml_vfio_user_bar_write(struct uml_vfio_user_device *dev, int bar,
			    unsigned int offset, const void *buf, int size);

#endif /* __UM_VFIO_USER_H */
