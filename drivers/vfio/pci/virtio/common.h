/* SPDX-License-Identifier: GPL-2.0 */

#ifndef VIRTIO_VFIO_COMMON_H
#define VIRTIO_VFIO_COMMON_H

#include <linux/kernel.h>
#include <linux/virtio.h>
#include <linux/vfio_pci_core.h>
#include <linux/virtio_pci.h>

enum virtiovf_migf_state {
	VIRTIOVF_MIGF_STATE_ERROR = 1,
	VIRTIOVF_MIGF_STATE_PRECOPY = 2,
	VIRTIOVF_MIGF_STATE_COMPLETE = 3,
};

enum virtiovf_load_state {
	VIRTIOVF_LOAD_STATE_READ_HEADER,
	VIRTIOVF_LOAD_STATE_PREP_HEADER_DATA,
	VIRTIOVF_LOAD_STATE_READ_HEADER_DATA,
	VIRTIOVF_LOAD_STATE_PREP_CHUNK,
	VIRTIOVF_LOAD_STATE_READ_CHUNK,
	VIRTIOVF_LOAD_STATE_LOAD_CHUNK,
};

struct virtiovf_data_buffer {
	struct sg_append_table table;
	loff_t start_pos;
	u64 length;
	u64 allocated_length;
	struct list_head buf_elm;
	u8 include_header_object:1;
	struct virtiovf_migration_file *migf;
	/* Optimize virtiovf_get_migration_page() for sequential access */
	struct scatterlist *last_offset_sg;
	unsigned int sg_last_entry;
	unsigned long last_offset;
};

enum virtiovf_migf_header_flags {
	VIRTIOVF_MIGF_HEADER_FLAGS_TAG_MANDATORY = 0,
	VIRTIOVF_MIGF_HEADER_FLAGS_TAG_OPTIONAL = 1 << 0,
};

enum virtiovf_migf_header_tag {
	VIRTIOVF_MIGF_HEADER_TAG_DEVICE_DATA = 0,
};

struct virtiovf_migration_header {
	__le64 record_size;
	/* For future use in case we may need to change the kernel protocol */
	__le32 flags; /* Use virtiovf_migf_header_flags */
	__le32 tag; /* Use virtiovf_migf_header_tag */
	__u8 data[]; /* Its size is given in the record_size */
};

struct virtiovf_migration_file {
	struct file *filp;
	/* synchronize access to the file state */
	struct mutex lock;
	loff_t max_pos;
	u64 pre_copy_initial_bytes;
	struct ratelimit_state pre_copy_rl_state;
	u64 record_size;
	u32 record_tag;
	u8 has_obj_id:1;
	u32 obj_id;
	enum virtiovf_migf_state state;
	enum virtiovf_load_state load_state;
	/* synchronize access to the lists */
	spinlock_t list_lock;
	struct list_head buf_list;
	struct list_head avail_list;
	struct virtiovf_data_buffer *buf;
	struct virtiovf_data_buffer *buf_header;
	struct virtiovf_pci_core_device *virtvdev;
};

struct virtiovf_pci_core_device {
	struct vfio_pci_core_device core_device;
#ifdef CONFIG_VIRTIO_VFIO_PCI_ADMIN_LEGACY
	u8 *bar0_virtual_buf;
	/* synchronize access to the virtual buf */
	struct mutex bar_mutex;
	void __iomem *notify_addr;
	u64 notify_offset;
	__le32 pci_base_addr_0;
	__le16 pci_cmd;
	u8 bar0_virtual_buf_size;
	u8 notify_bar;
#endif

	/* LM related */
	u8 migrate_cap:1;
	u8 deferred_reset:1;
	/* protect migration state */
	struct mutex state_mutex;
	enum vfio_device_mig_state mig_state;
	/* protect the reset_done flow */
	spinlock_t reset_lock;
	struct virtiovf_migration_file *resuming_migf;
	struct virtiovf_migration_file *saving_migf;
};

void virtiovf_set_migratable(struct virtiovf_pci_core_device *virtvdev);
void virtiovf_open_migration(struct virtiovf_pci_core_device *virtvdev);
void virtiovf_close_migration(struct virtiovf_pci_core_device *virtvdev);
void virtiovf_migration_reset_done(struct pci_dev *pdev);

#ifdef CONFIG_VIRTIO_VFIO_PCI_ADMIN_LEGACY
int virtiovf_open_legacy_io(struct virtiovf_pci_core_device *virtvdev);
long virtiovf_vfio_pci_core_ioctl(struct vfio_device *core_vdev,
				  unsigned int cmd, unsigned long arg);
int virtiovf_pci_ioctl_get_region_info(struct vfio_device *core_vdev,
				       unsigned int cmd, unsigned long arg);
ssize_t virtiovf_pci_core_write(struct vfio_device *core_vdev,
				const char __user *buf, size_t count,
				loff_t *ppos);
ssize_t virtiovf_pci_core_read(struct vfio_device *core_vdev, char __user *buf,
			       size_t count, loff_t *ppos);
bool virtiovf_support_legacy_io(struct pci_dev *pdev);
int virtiovf_init_legacy_io(struct virtiovf_pci_core_device *virtvdev);
void virtiovf_release_legacy_io(struct virtiovf_pci_core_device *virtvdev);
void virtiovf_legacy_io_reset_done(struct pci_dev *pdev);
#endif

#endif /* VIRTIO_VFIO_COMMON_H */
