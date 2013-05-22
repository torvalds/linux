#include <linux/vhost.h>

enum {
	VHOST_BLK_FEATURES = (1ULL << VIRTIO_RING_F_INDIRECT_DESC) |
			     (1ULL << VIRTIO_RING_F_EVENT_IDX),
};
/* VHOST_BLK specific defines */
#define VHOST_BLK_SET_BACKEND _IOW(VHOST_VIRTIO, 0x50, struct vhost_vring_file)
