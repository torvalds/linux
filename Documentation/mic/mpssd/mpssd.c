/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC User Space Tools.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <fcntl.h>
#include <getopt.h>
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <poll.h>
#include <features.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_net.h>
#include <linux/virtio_console.h>
#include <linux/virtio_blk.h>
#include <linux/version.h>
#include "mpssd.h"
#include <linux/mic_ioctl.h>
#include <linux/mic_common.h>

static void init_mic(struct mic_info *mic);

static FILE *logfp;
static struct mic_info mic_list;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define min_t(type, x, y) ({				\
		type __min1 = (x);                      \
		type __min2 = (y);                      \
		__min1 < __min2 ? __min1 : __min2; })

/* align addr on a size boundary - adjust address up/down if needed */
#define _ALIGN_DOWN(addr, size)  ((addr)&(~((size)-1)))
#define _ALIGN_UP(addr, size)    _ALIGN_DOWN(addr + size - 1, size)

/* align addr on a size boundary - adjust address up if needed */
#define _ALIGN(addr, size)     _ALIGN_UP(addr, size)

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)        _ALIGN(addr, PAGE_SIZE)

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

#define GSO_ENABLED		1
#define MAX_GSO_SIZE		(64 * 1024)
#define ETH_H_LEN		14
#define MAX_NET_PKT_SIZE	(_ALIGN_UP(MAX_GSO_SIZE + ETH_H_LEN, 64))
#define MIC_DEVICE_PAGE_END	0x1000

#ifndef VIRTIO_NET_HDR_F_DATA_VALID
#define VIRTIO_NET_HDR_F_DATA_VALID	2	/* Csum is valid */
#endif

static struct {
	struct mic_device_desc dd;
	struct mic_vqconfig vqconfig[2];
	__u32 host_features, guest_acknowledgements;
	struct virtio_console_config cons_config;
} virtcons_dev_page = {
	.dd = {
		.type = VIRTIO_ID_CONSOLE,
		.num_vq = ARRAY_SIZE(virtcons_dev_page.vqconfig),
		.feature_len = sizeof(virtcons_dev_page.host_features),
		.config_len = sizeof(virtcons_dev_page.cons_config),
	},
	.vqconfig[0] = {
		.num = htole16(MIC_VRING_ENTRIES),
	},
	.vqconfig[1] = {
		.num = htole16(MIC_VRING_ENTRIES),
	},
};

static struct {
	struct mic_device_desc dd;
	struct mic_vqconfig vqconfig[2];
	__u32 host_features, guest_acknowledgements;
	struct virtio_net_config net_config;
} virtnet_dev_page = {
	.dd = {
		.type = VIRTIO_ID_NET,
		.num_vq = ARRAY_SIZE(virtnet_dev_page.vqconfig),
		.feature_len = sizeof(virtnet_dev_page.host_features),
		.config_len = sizeof(virtnet_dev_page.net_config),
	},
	.vqconfig[0] = {
		.num = htole16(MIC_VRING_ENTRIES),
	},
	.vqconfig[1] = {
		.num = htole16(MIC_VRING_ENTRIES),
	},
#if GSO_ENABLED
		.host_features = htole32(
		1 << VIRTIO_NET_F_CSUM |
		1 << VIRTIO_NET_F_GSO |
		1 << VIRTIO_NET_F_GUEST_TSO4 |
		1 << VIRTIO_NET_F_GUEST_TSO6 |
		1 << VIRTIO_NET_F_GUEST_ECN |
		1 << VIRTIO_NET_F_GUEST_UFO),
#else
		.host_features = 0,
#endif
};

static const char *mic_config_dir = "/etc/sysconfig/mic";
static const char *virtblk_backend = "VIRTBLK_BACKEND";
static struct {
	struct mic_device_desc dd;
	struct mic_vqconfig vqconfig[1];
	__u32 host_features, guest_acknowledgements;
	struct virtio_blk_config blk_config;
} virtblk_dev_page = {
	.dd = {
		.type = VIRTIO_ID_BLOCK,
		.num_vq = ARRAY_SIZE(virtblk_dev_page.vqconfig),
		.feature_len = sizeof(virtblk_dev_page.host_features),
		.config_len = sizeof(virtblk_dev_page.blk_config),
	},
	.vqconfig[0] = {
		.num = htole16(MIC_VRING_ENTRIES),
	},
	.host_features =
		htole32(1<<VIRTIO_BLK_F_SEG_MAX),
	.blk_config = {
		.seg_max = htole32(MIC_VRING_ENTRIES - 2),
		.capacity = htole64(0),
	 }
};

static char *myname;

static int
tap_configure(struct mic_info *mic, char *dev)
{
	pid_t pid;
	char *ifargv[7];
	char ipaddr[IFNAMSIZ];
	int ret = 0;

	pid = fork();
	if (pid == 0) {
		ifargv[0] = "ip";
		ifargv[1] = "link";
		ifargv[2] = "set";
		ifargv[3] = dev;
		ifargv[4] = "up";
		ifargv[5] = NULL;
		mpsslog("Configuring %s\n", dev);
		ret = execvp("ip", ifargv);
		if (ret < 0) {
			mpsslog("%s execvp failed errno %s\n",
				mic->name, strerror(errno));
			return ret;
		}
	}
	if (pid < 0) {
		mpsslog("%s fork failed errno %s\n",
			mic->name, strerror(errno));
		return ret;
	}

	ret = waitpid(pid, NULL, 0);
	if (ret < 0) {
		mpsslog("%s waitpid failed errno %s\n",
			mic->name, strerror(errno));
		return ret;
	}

	snprintf(ipaddr, IFNAMSIZ, "172.31.%d.254/24", mic->id);

	pid = fork();
	if (pid == 0) {
		ifargv[0] = "ip";
		ifargv[1] = "addr";
		ifargv[2] = "add";
		ifargv[3] = ipaddr;
		ifargv[4] = "dev";
		ifargv[5] = dev;
		ifargv[6] = NULL;
		mpsslog("Configuring %s ipaddr %s\n", dev, ipaddr);
		ret = execvp("ip", ifargv);
		if (ret < 0) {
			mpsslog("%s execvp failed errno %s\n",
				mic->name, strerror(errno));
			return ret;
		}
	}
	if (pid < 0) {
		mpsslog("%s fork failed errno %s\n",
			mic->name, strerror(errno));
		return ret;
	}

	ret = waitpid(pid, NULL, 0);
	if (ret < 0) {
		mpsslog("%s waitpid failed errno %s\n",
			mic->name, strerror(errno));
		return ret;
	}
	mpsslog("MIC name %s %s %d DONE!\n",
		mic->name, __func__, __LINE__);
	return 0;
}

static int tun_alloc(struct mic_info *mic, char *dev)
{
	struct ifreq ifr;
	int fd, err;
#if GSO_ENABLED
	unsigned offload;
#endif
	fd = open("/dev/net/tun", O_RDWR);
	if (fd < 0) {
		mpsslog("Could not open /dev/net/tun %s\n", strerror(errno));
		goto done;
	}

	memset(&ifr, 0, sizeof(ifr));

	ifr.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_VNET_HDR;
	if (*dev)
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);

	err = ioctl(fd, TUNSETIFF, (void *)&ifr);
	if (err < 0) {
		mpsslog("%s %s %d TUNSETIFF failed %s\n",
			mic->name, __func__, __LINE__, strerror(errno));
		close(fd);
		return err;
	}
#if GSO_ENABLED
	offload = TUN_F_CSUM | TUN_F_TSO4 | TUN_F_TSO6 |
		TUN_F_TSO_ECN | TUN_F_UFO;

	err = ioctl(fd, TUNSETOFFLOAD, offload);
	if (err < 0) {
		mpsslog("%s %s %d TUNSETOFFLOAD failed %s\n",
			mic->name, __func__, __LINE__, strerror(errno));
		close(fd);
		return err;
	}
#endif
	strcpy(dev, ifr.ifr_name);
	mpsslog("Created TAP %s\n", dev);
done:
	return fd;
}

#define NET_FD_VIRTIO_NET 0
#define NET_FD_TUN 1
#define MAX_NET_FD 2

static void set_dp(struct mic_info *mic, int type, void *dp)
{
	switch (type) {
	case VIRTIO_ID_CONSOLE:
		mic->mic_console.console_dp = dp;
		return;
	case VIRTIO_ID_NET:
		mic->mic_net.net_dp = dp;
		return;
	case VIRTIO_ID_BLOCK:
		mic->mic_virtblk.block_dp = dp;
		return;
	}
	mpsslog("%s %s %d not found\n", mic->name, __func__, type);
	assert(0);
}

static void *get_dp(struct mic_info *mic, int type)
{
	switch (type) {
	case VIRTIO_ID_CONSOLE:
		return mic->mic_console.console_dp;
	case VIRTIO_ID_NET:
		return mic->mic_net.net_dp;
	case VIRTIO_ID_BLOCK:
		return mic->mic_virtblk.block_dp;
	}
	mpsslog("%s %s %d not found\n", mic->name, __func__, type);
	assert(0);
	return NULL;
}

static struct mic_device_desc *get_device_desc(struct mic_info *mic, int type)
{
	struct mic_device_desc *d;
	int i;
	void *dp = get_dp(mic, type);

	for (i = sizeof(struct mic_bootparam); i < PAGE_SIZE;
		i += mic_total_desc_size(d)) {
		d = dp + i;

		/* End of list */
		if (d->type == 0)
			break;

		if (d->type == -1)
			continue;

		mpsslog("%s %s d-> type %d d %p\n",
			mic->name, __func__, d->type, d);

		if (d->type == (__u8)type)
			return d;
	}
	mpsslog("%s %s %d not found\n", mic->name, __func__, type);
	assert(0);
	return NULL;
}

/* See comments in vhost.c for explanation of next_desc() */
static unsigned next_desc(struct vring_desc *desc)
{
	unsigned int next;

	if (!(le16toh(desc->flags) & VRING_DESC_F_NEXT))
		return -1U;
	next = le16toh(desc->next);
	return next;
}

/* Sum up all the IOVEC length */
static ssize_t
sum_iovec_len(struct mic_copy_desc *copy)
{
	ssize_t sum = 0;
	int i;

	for (i = 0; i < copy->iovcnt; i++)
		sum += copy->iov[i].iov_len;
	return sum;
}

static inline void verify_out_len(struct mic_info *mic,
	struct mic_copy_desc *copy)
{
	if (copy->out_len != sum_iovec_len(copy)) {
		mpsslog("%s %s %d BUG copy->out_len 0x%x len 0x%zx\n",
			mic->name, __func__, __LINE__,
			copy->out_len, sum_iovec_len(copy));
		assert(copy->out_len == sum_iovec_len(copy));
	}
}

/* Display an iovec */
static void
disp_iovec(struct mic_info *mic, struct mic_copy_desc *copy,
	   const char *s, int line)
{
	int i;

	for (i = 0; i < copy->iovcnt; i++)
		mpsslog("%s %s %d copy->iov[%d] addr %p len 0x%zx\n",
			mic->name, s, line, i,
			copy->iov[i].iov_base, copy->iov[i].iov_len);
}

static inline __u16 read_avail_idx(struct mic_vring *vr)
{
	return ACCESS_ONCE(vr->info->avail_idx);
}

static inline void txrx_prepare(int type, bool tx, struct mic_vring *vr,
				struct mic_copy_desc *copy, ssize_t len)
{
	copy->vr_idx = tx ? 0 : 1;
	copy->update_used = true;
	if (type == VIRTIO_ID_NET)
		copy->iov[1].iov_len = len - sizeof(struct virtio_net_hdr);
	else
		copy->iov[0].iov_len = len;
}

/* Central API which triggers the copies */
static int
mic_virtio_copy(struct mic_info *mic, int fd,
		struct mic_vring *vr, struct mic_copy_desc *copy)
{
	int ret;

	ret = ioctl(fd, MIC_VIRTIO_COPY_DESC, copy);
	if (ret) {
		mpsslog("%s %s %d errno %s ret %d\n",
			mic->name, __func__, __LINE__,
			strerror(errno), ret);
	}
	return ret;
}

/*
 * This initialization routine requires at least one
 * vring i.e. vr0. vr1 is optional.
 */
static void *
init_vr(struct mic_info *mic, int fd, int type,
	struct mic_vring *vr0, struct mic_vring *vr1, int num_vq)
{
	int vr_size;
	char *va;

	vr_size = PAGE_ALIGN(vring_size(MIC_VRING_ENTRIES,
		MIC_VIRTIO_RING_ALIGN) + sizeof(struct _mic_vring_info));
	va = mmap(NULL, MIC_DEVICE_PAGE_END + vr_size * num_vq,
		PROT_READ, MAP_SHARED, fd, 0);
	if (MAP_FAILED == va) {
		mpsslog("%s %s %d mmap failed errno %s\n",
			mic->name, __func__, __LINE__,
			strerror(errno));
		goto done;
	}
	set_dp(mic, type, va);
	vr0->va = (struct mic_vring *)&va[MIC_DEVICE_PAGE_END];
	vr0->info = vr0->va +
		vring_size(MIC_VRING_ENTRIES, MIC_VIRTIO_RING_ALIGN);
	vring_init(&vr0->vr,
		   MIC_VRING_ENTRIES, vr0->va, MIC_VIRTIO_RING_ALIGN);
	mpsslog("%s %s vr0 %p vr0->info %p vr_size 0x%x vring 0x%x ",
		__func__, mic->name, vr0->va, vr0->info, vr_size,
		vring_size(MIC_VRING_ENTRIES, MIC_VIRTIO_RING_ALIGN));
	mpsslog("magic 0x%x expected 0x%x\n",
		le32toh(vr0->info->magic), MIC_MAGIC + type);
	assert(le32toh(vr0->info->magic) == MIC_MAGIC + type);
	if (vr1) {
		vr1->va = (struct mic_vring *)
			&va[MIC_DEVICE_PAGE_END + vr_size];
		vr1->info = vr1->va + vring_size(MIC_VRING_ENTRIES,
			MIC_VIRTIO_RING_ALIGN);
		vring_init(&vr1->vr,
			   MIC_VRING_ENTRIES, vr1->va, MIC_VIRTIO_RING_ALIGN);
		mpsslog("%s %s vr1 %p vr1->info %p vr_size 0x%x vring 0x%x ",
			__func__, mic->name, vr1->va, vr1->info, vr_size,
			vring_size(MIC_VRING_ENTRIES, MIC_VIRTIO_RING_ALIGN));
		mpsslog("magic 0x%x expected 0x%x\n",
			le32toh(vr1->info->magic), MIC_MAGIC + type + 1);
		assert(le32toh(vr1->info->magic) == MIC_MAGIC + type + 1);
	}
done:
	return va;
}

static void
wait_for_card_driver(struct mic_info *mic, int fd, int type)
{
	struct pollfd pollfd;
	int err;
	struct mic_device_desc *desc = get_device_desc(mic, type);

	pollfd.fd = fd;
	mpsslog("%s %s Waiting .... desc-> type %d status 0x%x\n",
		mic->name, __func__, type, desc->status);
	while (1) {
		pollfd.events = POLLIN;
		pollfd.revents = 0;
		err = poll(&pollfd, 1, -1);
		if (err < 0) {
			mpsslog("%s %s poll failed %s\n",
				mic->name, __func__, strerror(errno));
			continue;
		}

		if (pollfd.revents) {
			mpsslog("%s %s Waiting... desc-> type %d status 0x%x\n",
				mic->name, __func__, type, desc->status);
			if (desc->status & VIRTIO_CONFIG_S_DRIVER_OK) {
				mpsslog("%s %s poll.revents %d\n",
					mic->name, __func__, pollfd.revents);
				mpsslog("%s %s desc-> type %d status 0x%x\n",
					mic->name, __func__, type,
					desc->status);
				break;
			}
		}
	}
}

/* Spin till we have some descriptors */
static void
spin_for_descriptors(struct mic_info *mic, struct mic_vring *vr)
{
	__u16 avail_idx = read_avail_idx(vr);

	while (avail_idx == le16toh(ACCESS_ONCE(vr->vr.avail->idx))) {
#ifdef DEBUG
		mpsslog("%s %s waiting for desc avail %d info_avail %d\n",
			mic->name, __func__,
			le16toh(vr->vr.avail->idx), vr->info->avail_idx);
#endif
		sched_yield();
	}
}

static void *
virtio_net(void *arg)
{
	static __u8 vnet_hdr[2][sizeof(struct virtio_net_hdr)];
	static __u8 vnet_buf[2][MAX_NET_PKT_SIZE] __attribute__ ((aligned(64)));
	struct iovec vnet_iov[2][2] = {
		{ { .iov_base = vnet_hdr[0], .iov_len = sizeof(vnet_hdr[0]) },
		  { .iov_base = vnet_buf[0], .iov_len = sizeof(vnet_buf[0]) } },
		{ { .iov_base = vnet_hdr[1], .iov_len = sizeof(vnet_hdr[1]) },
		  { .iov_base = vnet_buf[1], .iov_len = sizeof(vnet_buf[1]) } },
	};
	struct iovec *iov0 = vnet_iov[0], *iov1 = vnet_iov[1];
	struct mic_info *mic = (struct mic_info *)arg;
	char if_name[IFNAMSIZ];
	struct pollfd net_poll[MAX_NET_FD];
	struct mic_vring tx_vr, rx_vr;
	struct mic_copy_desc copy;
	struct mic_device_desc *desc;
	int err;

	snprintf(if_name, IFNAMSIZ, "mic%d", mic->id);
	mic->mic_net.tap_fd = tun_alloc(mic, if_name);
	if (mic->mic_net.tap_fd < 0)
		goto done;

	if (tap_configure(mic, if_name))
		goto done;
	mpsslog("MIC name %s id %d\n", mic->name, mic->id);

	net_poll[NET_FD_VIRTIO_NET].fd = mic->mic_net.virtio_net_fd;
	net_poll[NET_FD_VIRTIO_NET].events = POLLIN;
	net_poll[NET_FD_TUN].fd = mic->mic_net.tap_fd;
	net_poll[NET_FD_TUN].events = POLLIN;

	if (MAP_FAILED == init_vr(mic, mic->mic_net.virtio_net_fd,
				  VIRTIO_ID_NET, &tx_vr, &rx_vr,
		virtnet_dev_page.dd.num_vq)) {
		mpsslog("%s init_vr failed %s\n",
			mic->name, strerror(errno));
		goto done;
	}

	copy.iovcnt = 2;
	desc = get_device_desc(mic, VIRTIO_ID_NET);

	while (1) {
		ssize_t len;

		net_poll[NET_FD_VIRTIO_NET].revents = 0;
		net_poll[NET_FD_TUN].revents = 0;

		/* Start polling for data from tap and virtio net */
		err = poll(net_poll, 2, -1);
		if (err < 0) {
			mpsslog("%s poll failed %s\n",
				__func__, strerror(errno));
			continue;
		}
		if (!(desc->status & VIRTIO_CONFIG_S_DRIVER_OK))
			wait_for_card_driver(mic, mic->mic_net.virtio_net_fd,
					     VIRTIO_ID_NET);
		/*
		 * Check if there is data to be read from TUN and write to
		 * virtio net fd if there is.
		 */
		if (net_poll[NET_FD_TUN].revents & POLLIN) {
			copy.iov = iov0;
			len = readv(net_poll[NET_FD_TUN].fd,
				copy.iov, copy.iovcnt);
			if (len > 0) {
				struct virtio_net_hdr *hdr
					= (struct virtio_net_hdr *)vnet_hdr[0];

				/* Disable checksums on the card since we are on
				   a reliable PCIe link */
				hdr->flags |= VIRTIO_NET_HDR_F_DATA_VALID;
#ifdef DEBUG
				mpsslog("%s %s %d hdr->flags 0x%x ", mic->name,
					__func__, __LINE__, hdr->flags);
				mpsslog("copy.out_len %d hdr->gso_type 0x%x\n",
					copy.out_len, hdr->gso_type);
#endif
#ifdef DEBUG
				disp_iovec(mic, copy, __func__, __LINE__);
				mpsslog("%s %s %d read from tap 0x%lx\n",
					mic->name, __func__, __LINE__,
					len);
#endif
				spin_for_descriptors(mic, &tx_vr);
				txrx_prepare(VIRTIO_ID_NET, 1, &tx_vr, &copy,
					     len);

				err = mic_virtio_copy(mic,
					mic->mic_net.virtio_net_fd, &tx_vr,
					&copy);
				if (err < 0) {
					mpsslog("%s %s %d mic_virtio_copy %s\n",
						mic->name, __func__, __LINE__,
						strerror(errno));
				}
				if (!err)
					verify_out_len(mic, &copy);
#ifdef DEBUG
				disp_iovec(mic, copy, __func__, __LINE__);
				mpsslog("%s %s %d wrote to net 0x%lx\n",
					mic->name, __func__, __LINE__,
					sum_iovec_len(&copy));
#endif
				/* Reinitialize IOV for next run */
				iov0[1].iov_len = MAX_NET_PKT_SIZE;
			} else if (len < 0) {
				disp_iovec(mic, &copy, __func__, __LINE__);
				mpsslog("%s %s %d read failed %s ", mic->name,
					__func__, __LINE__, strerror(errno));
				mpsslog("cnt %d sum %zd\n",
					copy.iovcnt, sum_iovec_len(&copy));
			}
		}

		/*
		 * Check if there is data to be read from virtio net and
		 * write to TUN if there is.
		 */
		if (net_poll[NET_FD_VIRTIO_NET].revents & POLLIN) {
			while (rx_vr.info->avail_idx !=
				le16toh(rx_vr.vr.avail->idx)) {
				copy.iov = iov1;
				txrx_prepare(VIRTIO_ID_NET, 0, &rx_vr, &copy,
					     MAX_NET_PKT_SIZE
					+ sizeof(struct virtio_net_hdr));

				err = mic_virtio_copy(mic,
					mic->mic_net.virtio_net_fd, &rx_vr,
					&copy);
				if (!err) {
#ifdef DEBUG
					struct virtio_net_hdr *hdr
						= (struct virtio_net_hdr *)
							vnet_hdr[1];

					mpsslog("%s %s %d hdr->flags 0x%x, ",
						mic->name, __func__, __LINE__,
						hdr->flags);
					mpsslog("out_len %d gso_type 0x%x\n",
						copy.out_len,
						hdr->gso_type);
#endif
					/* Set the correct output iov_len */
					iov1[1].iov_len = copy.out_len -
						sizeof(struct virtio_net_hdr);
					verify_out_len(mic, &copy);
#ifdef DEBUG
					disp_iovec(mic, copy, __func__,
						   __LINE__);
					mpsslog("%s %s %d ",
						mic->name, __func__, __LINE__);
					mpsslog("read from net 0x%lx\n",
						sum_iovec_len(copy));
#endif
					len = writev(net_poll[NET_FD_TUN].fd,
						copy.iov, copy.iovcnt);
					if (len != sum_iovec_len(&copy)) {
						mpsslog("Tun write failed %s ",
							strerror(errno));
						mpsslog("len 0x%zx ", len);
						mpsslog("read_len 0x%zx\n",
							sum_iovec_len(&copy));
					} else {
#ifdef DEBUG
						disp_iovec(mic, &copy, __func__,
							   __LINE__);
						mpsslog("%s %s %d ",
							mic->name, __func__,
							__LINE__);
						mpsslog("wrote to tap 0x%lx\n",
							len);
#endif
					}
				} else {
					mpsslog("%s %s %d mic_virtio_copy %s\n",
						mic->name, __func__, __LINE__,
						strerror(errno));
					break;
				}
			}
		}
		if (net_poll[NET_FD_VIRTIO_NET].revents & POLLERR)
			mpsslog("%s: %s: POLLERR\n", __func__, mic->name);
	}
done:
	pthread_exit(NULL);
}

/* virtio_console */
#define VIRTIO_CONSOLE_FD 0
#define MONITOR_FD (VIRTIO_CONSOLE_FD + 1)
#define MAX_CONSOLE_FD (MONITOR_FD + 1)  /* must be the last one + 1 */
#define MAX_BUFFER_SIZE PAGE_SIZE

static void *
virtio_console(void *arg)
{
	static __u8 vcons_buf[2][PAGE_SIZE];
	struct iovec vcons_iov[2] = {
		{ .iov_base = vcons_buf[0], .iov_len = sizeof(vcons_buf[0]) },
		{ .iov_base = vcons_buf[1], .iov_len = sizeof(vcons_buf[1]) },
	};
	struct iovec *iov0 = &vcons_iov[0], *iov1 = &vcons_iov[1];
	struct mic_info *mic = (struct mic_info *)arg;
	int err;
	struct pollfd console_poll[MAX_CONSOLE_FD];
	int pty_fd;
	char *pts_name;
	ssize_t len;
	struct mic_vring tx_vr, rx_vr;
	struct mic_copy_desc copy;
	struct mic_device_desc *desc;

	pty_fd = posix_openpt(O_RDWR);
	if (pty_fd < 0) {
		mpsslog("can't open a pseudoterminal master device: %s\n",
			strerror(errno));
		goto _return;
	}
	pts_name = ptsname(pty_fd);
	if (pts_name == NULL) {
		mpsslog("can't get pts name\n");
		goto _close_pty;
	}
	printf("%s console message goes to %s\n", mic->name, pts_name);
	mpsslog("%s console message goes to %s\n", mic->name, pts_name);
	err = grantpt(pty_fd);
	if (err < 0) {
		mpsslog("can't grant access: %s %s\n",
			pts_name, strerror(errno));
		goto _close_pty;
	}
	err = unlockpt(pty_fd);
	if (err < 0) {
		mpsslog("can't unlock a pseudoterminal: %s %s\n",
			pts_name, strerror(errno));
		goto _close_pty;
	}
	console_poll[MONITOR_FD].fd = pty_fd;
	console_poll[MONITOR_FD].events = POLLIN;

	console_poll[VIRTIO_CONSOLE_FD].fd = mic->mic_console.virtio_console_fd;
	console_poll[VIRTIO_CONSOLE_FD].events = POLLIN;

	if (MAP_FAILED == init_vr(mic, mic->mic_console.virtio_console_fd,
				  VIRTIO_ID_CONSOLE, &tx_vr, &rx_vr,
		virtcons_dev_page.dd.num_vq)) {
		mpsslog("%s init_vr failed %s\n",
			mic->name, strerror(errno));
		goto _close_pty;
	}

	copy.iovcnt = 1;
	desc = get_device_desc(mic, VIRTIO_ID_CONSOLE);

	for (;;) {
		console_poll[MONITOR_FD].revents = 0;
		console_poll[VIRTIO_CONSOLE_FD].revents = 0;
		err = poll(console_poll, MAX_CONSOLE_FD, -1);
		if (err < 0) {
			mpsslog("%s %d: poll failed: %s\n", __func__, __LINE__,
				strerror(errno));
			continue;
		}
		if (!(desc->status & VIRTIO_CONFIG_S_DRIVER_OK))
			wait_for_card_driver(mic,
					     mic->mic_console.virtio_console_fd,
				VIRTIO_ID_CONSOLE);

		if (console_poll[MONITOR_FD].revents & POLLIN) {
			copy.iov = iov0;
			len = readv(pty_fd, copy.iov, copy.iovcnt);
			if (len > 0) {
#ifdef DEBUG
				disp_iovec(mic, copy, __func__, __LINE__);
				mpsslog("%s %s %d read from tap 0x%lx\n",
					mic->name, __func__, __LINE__,
					len);
#endif
				spin_for_descriptors(mic, &tx_vr);
				txrx_prepare(VIRTIO_ID_CONSOLE, 1, &tx_vr,
					     &copy, len);

				err = mic_virtio_copy(mic,
					mic->mic_console.virtio_console_fd,
					&tx_vr, &copy);
				if (err < 0) {
					mpsslog("%s %s %d mic_virtio_copy %s\n",
						mic->name, __func__, __LINE__,
						strerror(errno));
				}
				if (!err)
					verify_out_len(mic, &copy);
#ifdef DEBUG
				disp_iovec(mic, copy, __func__, __LINE__);
				mpsslog("%s %s %d wrote to net 0x%lx\n",
					mic->name, __func__, __LINE__,
					sum_iovec_len(copy));
#endif
				/* Reinitialize IOV for next run */
				iov0->iov_len = PAGE_SIZE;
			} else if (len < 0) {
				disp_iovec(mic, &copy, __func__, __LINE__);
				mpsslog("%s %s %d read failed %s ",
					mic->name, __func__, __LINE__,
					strerror(errno));
				mpsslog("cnt %d sum %zd\n",
					copy.iovcnt, sum_iovec_len(&copy));
			}
		}

		if (console_poll[VIRTIO_CONSOLE_FD].revents & POLLIN) {
			while (rx_vr.info->avail_idx !=
				le16toh(rx_vr.vr.avail->idx)) {
				copy.iov = iov1;
				txrx_prepare(VIRTIO_ID_CONSOLE, 0, &rx_vr,
					     &copy, PAGE_SIZE);

				err = mic_virtio_copy(mic,
					mic->mic_console.virtio_console_fd,
					&rx_vr, &copy);
				if (!err) {
					/* Set the correct output iov_len */
					iov1->iov_len = copy.out_len;
					verify_out_len(mic, &copy);
#ifdef DEBUG
					disp_iovec(mic, copy, __func__,
						   __LINE__);
					mpsslog("%s %s %d ",
						mic->name, __func__, __LINE__);
					mpsslog("read from net 0x%lx\n",
						sum_iovec_len(copy));
#endif
					len = writev(pty_fd,
						copy.iov, copy.iovcnt);
					if (len != sum_iovec_len(&copy)) {
						mpsslog("Tun write failed %s ",
							strerror(errno));
						mpsslog("len 0x%zx ", len);
						mpsslog("read_len 0x%zx\n",
							sum_iovec_len(&copy));
					} else {
#ifdef DEBUG
						disp_iovec(mic, copy, __func__,
							   __LINE__);
						mpsslog("%s %s %d ",
							mic->name, __func__,
							__LINE__);
						mpsslog("wrote to tap 0x%lx\n",
							len);
#endif
					}
				} else {
					mpsslog("%s %s %d mic_virtio_copy %s\n",
						mic->name, __func__, __LINE__,
						strerror(errno));
					break;
				}
			}
		}
		if (console_poll[NET_FD_VIRTIO_NET].revents & POLLERR)
			mpsslog("%s: %s: POLLERR\n", __func__, mic->name);
	}
_close_pty:
	close(pty_fd);
_return:
	pthread_exit(NULL);
}

static void
add_virtio_device(struct mic_info *mic, struct mic_device_desc *dd)
{
	char path[PATH_MAX];
	int fd, err;

	snprintf(path, PATH_MAX, "/dev/mic%d", mic->id);
	fd = open(path, O_RDWR);
	if (fd < 0) {
		mpsslog("Could not open %s %s\n", path, strerror(errno));
		return;
	}

	err = ioctl(fd, MIC_VIRTIO_ADD_DEVICE, dd);
	if (err < 0) {
		mpsslog("Could not add %d %s\n", dd->type, strerror(errno));
		close(fd);
		return;
	}
	switch (dd->type) {
	case VIRTIO_ID_NET:
		mic->mic_net.virtio_net_fd = fd;
		mpsslog("Added VIRTIO_ID_NET for %s\n", mic->name);
		break;
	case VIRTIO_ID_CONSOLE:
		mic->mic_console.virtio_console_fd = fd;
		mpsslog("Added VIRTIO_ID_CONSOLE for %s\n", mic->name);
		break;
	case VIRTIO_ID_BLOCK:
		mic->mic_virtblk.virtio_block_fd = fd;
		mpsslog("Added VIRTIO_ID_BLOCK for %s\n", mic->name);
		break;
	}
}

static bool
set_backend_file(struct mic_info *mic)
{
	FILE *config;
	char buff[PATH_MAX], *line, *evv, *p;

	snprintf(buff, PATH_MAX, "%s/mpssd%03d.conf", mic_config_dir, mic->id);
	config = fopen(buff, "r");
	if (config == NULL)
		return false;
	do {  /* look for "virtblk_backend=XXXX" */
		line = fgets(buff, PATH_MAX, config);
		if (line == NULL)
			break;
		if (*line == '#')
			continue;
		p = strchr(line, '\n');
		if (p)
			*p = '\0';
	} while (strncmp(line, virtblk_backend, strlen(virtblk_backend)) != 0);
	fclose(config);
	if (line == NULL)
		return false;
	evv = strchr(line, '=');
	if (evv == NULL)
		return false;
	mic->mic_virtblk.backend_file = malloc(strlen(evv) + 1);
	if (mic->mic_virtblk.backend_file == NULL) {
		mpsslog("%s %d can't allocate memory\n", mic->name, mic->id);
		return false;
	}
	strcpy(mic->mic_virtblk.backend_file, evv + 1);
	return true;
}

#define SECTOR_SIZE 512
static bool
set_backend_size(struct mic_info *mic)
{
	mic->mic_virtblk.backend_size = lseek(mic->mic_virtblk.backend, 0,
		SEEK_END);
	if (mic->mic_virtblk.backend_size < 0) {
		mpsslog("%s: can't seek: %s\n",
			mic->name, mic->mic_virtblk.backend_file);
		return false;
	}
	virtblk_dev_page.blk_config.capacity =
		mic->mic_virtblk.backend_size / SECTOR_SIZE;
	if ((mic->mic_virtblk.backend_size % SECTOR_SIZE) != 0)
		virtblk_dev_page.blk_config.capacity++;

	virtblk_dev_page.blk_config.capacity =
		htole64(virtblk_dev_page.blk_config.capacity);

	return true;
}

static bool
open_backend(struct mic_info *mic)
{
	if (!set_backend_file(mic))
		goto _error_exit;
	mic->mic_virtblk.backend = open(mic->mic_virtblk.backend_file, O_RDWR);
	if (mic->mic_virtblk.backend < 0) {
		mpsslog("%s: can't open: %s\n", mic->name,
			mic->mic_virtblk.backend_file);
		goto _error_free;
	}
	if (!set_backend_size(mic))
		goto _error_close;
	mic->mic_virtblk.backend_addr = mmap(NULL,
		mic->mic_virtblk.backend_size,
		PROT_READ|PROT_WRITE, MAP_SHARED,
		mic->mic_virtblk.backend, 0L);
	if (mic->mic_virtblk.backend_addr == MAP_FAILED) {
		mpsslog("%s: can't map: %s %s\n",
			mic->name, mic->mic_virtblk.backend_file,
			strerror(errno));
		goto _error_close;
	}
	return true;

 _error_close:
	close(mic->mic_virtblk.backend);
 _error_free:
	free(mic->mic_virtblk.backend_file);
 _error_exit:
	return false;
}

static void
close_backend(struct mic_info *mic)
{
	munmap(mic->mic_virtblk.backend_addr, mic->mic_virtblk.backend_size);
	close(mic->mic_virtblk.backend);
	free(mic->mic_virtblk.backend_file);
}

static bool
start_virtblk(struct mic_info *mic, struct mic_vring *vring)
{
	if (((unsigned long)&virtblk_dev_page.blk_config % 8) != 0) {
		mpsslog("%s: blk_config is not 8 byte aligned.\n",
			mic->name);
		return false;
	}
	add_virtio_device(mic, &virtblk_dev_page.dd);
	if (MAP_FAILED == init_vr(mic, mic->mic_virtblk.virtio_block_fd,
				  VIRTIO_ID_BLOCK, vring, NULL,
				  virtblk_dev_page.dd.num_vq)) {
		mpsslog("%s init_vr failed %s\n",
			mic->name, strerror(errno));
		return false;
	}
	return true;
}

static void
stop_virtblk(struct mic_info *mic)
{
	int vr_size, ret;

	vr_size = PAGE_ALIGN(vring_size(MIC_VRING_ENTRIES,
		MIC_VIRTIO_RING_ALIGN) + sizeof(struct _mic_vring_info));
	ret = munmap(mic->mic_virtblk.block_dp,
		MIC_DEVICE_PAGE_END + vr_size * virtblk_dev_page.dd.num_vq);
	if (ret < 0)
		mpsslog("%s munmap errno %d\n", mic->name, errno);
	close(mic->mic_virtblk.virtio_block_fd);
}

static __u8
header_error_check(struct vring_desc *desc)
{
	if (le32toh(desc->len) != sizeof(struct virtio_blk_outhdr)) {
		mpsslog("%s() %d: length is not sizeof(virtio_blk_outhd)\n",
			__func__, __LINE__);
		return -EIO;
	}
	if (!(le16toh(desc->flags) & VRING_DESC_F_NEXT)) {
		mpsslog("%s() %d: alone\n",
			__func__, __LINE__);
		return -EIO;
	}
	if (le16toh(desc->flags) & VRING_DESC_F_WRITE) {
		mpsslog("%s() %d: not read\n",
			__func__, __LINE__);
		return -EIO;
	}
	return 0;
}

static int
read_header(int fd, struct virtio_blk_outhdr *hdr, __u32 desc_idx)
{
	struct iovec iovec;
	struct mic_copy_desc copy;

	iovec.iov_len = sizeof(*hdr);
	iovec.iov_base = hdr;
	copy.iov = &iovec;
	copy.iovcnt = 1;
	copy.vr_idx = 0;  /* only one vring on virtio_block */
	copy.update_used = false;  /* do not update used index */
	return ioctl(fd, MIC_VIRTIO_COPY_DESC, &copy);
}

static int
transfer_blocks(int fd, struct iovec *iovec, __u32 iovcnt)
{
	struct mic_copy_desc copy;

	copy.iov = iovec;
	copy.iovcnt = iovcnt;
	copy.vr_idx = 0;  /* only one vring on virtio_block */
	copy.update_used = false;  /* do not update used index */
	return ioctl(fd, MIC_VIRTIO_COPY_DESC, &copy);
}

static __u8
status_error_check(struct vring_desc *desc)
{
	if (le32toh(desc->len) != sizeof(__u8)) {
		mpsslog("%s() %d: length is not sizeof(status)\n",
			__func__, __LINE__);
		return -EIO;
	}
	return 0;
}

static int
write_status(int fd, __u8 *status)
{
	struct iovec iovec;
	struct mic_copy_desc copy;

	iovec.iov_base = status;
	iovec.iov_len = sizeof(*status);
	copy.iov = &iovec;
	copy.iovcnt = 1;
	copy.vr_idx = 0;  /* only one vring on virtio_block */
	copy.update_used = true; /* Update used index */
	return ioctl(fd, MIC_VIRTIO_COPY_DESC, &copy);
}

static void *
virtio_block(void *arg)
{
	struct mic_info *mic = (struct mic_info *)arg;
	int ret;
	struct pollfd block_poll;
	struct mic_vring vring;
	__u16 avail_idx;
	__u32 desc_idx;
	struct vring_desc *desc;
	struct iovec *iovec, *piov;
	__u8 status;
	__u32 buffer_desc_idx;
	struct virtio_blk_outhdr hdr;
	void *fos;

	for (;;) {  /* forever */
		if (!open_backend(mic)) { /* No virtblk */
			for (mic->mic_virtblk.signaled = 0;
				!mic->mic_virtblk.signaled;)
				sleep(1);
			continue;
		}

		/* backend file is specified. */
		if (!start_virtblk(mic, &vring))
			goto _close_backend;
		iovec = malloc(sizeof(*iovec) *
			le32toh(virtblk_dev_page.blk_config.seg_max));
		if (!iovec) {
			mpsslog("%s: can't alloc iovec: %s\n",
				mic->name, strerror(ENOMEM));
			goto _stop_virtblk;
		}

		block_poll.fd = mic->mic_virtblk.virtio_block_fd;
		block_poll.events = POLLIN;
		for (mic->mic_virtblk.signaled = 0;
		     !mic->mic_virtblk.signaled;) {
			block_poll.revents = 0;
					/* timeout in 1 sec to see signaled */
			ret = poll(&block_poll, 1, 1000);
			if (ret < 0) {
				mpsslog("%s %d: poll failed: %s\n",
					__func__, __LINE__,
					strerror(errno));
				continue;
			}

			if (!(block_poll.revents & POLLIN)) {
#ifdef DEBUG
				mpsslog("%s %d: block_poll.revents=0x%x\n",
					__func__, __LINE__, block_poll.revents);
#endif
				continue;
			}

			/* POLLIN */
			while (vring.info->avail_idx !=
				le16toh(vring.vr.avail->idx)) {
				/* read header element */
				avail_idx =
					vring.info->avail_idx &
					(vring.vr.num - 1);
				desc_idx = le16toh(
					vring.vr.avail->ring[avail_idx]);
				desc = &vring.vr.desc[desc_idx];
#ifdef DEBUG
				mpsslog("%s() %d: avail_idx=%d ",
					__func__, __LINE__,
					vring.info->avail_idx);
				mpsslog("vring.vr.num=%d desc=%p\n",
					vring.vr.num, desc);
#endif
				status = header_error_check(desc);
				ret = read_header(
					mic->mic_virtblk.virtio_block_fd,
					&hdr, desc_idx);
				if (ret < 0) {
					mpsslog("%s() %d %s: ret=%d %s\n",
						__func__, __LINE__,
						mic->name, ret,
						strerror(errno));
					break;
				}
				/* buffer element */
				piov = iovec;
				status = 0;
				fos = mic->mic_virtblk.backend_addr +
					(hdr.sector * SECTOR_SIZE);
				buffer_desc_idx = next_desc(desc);
				desc_idx = buffer_desc_idx;
				for (desc = &vring.vr.desc[buffer_desc_idx];
				     desc->flags & VRING_DESC_F_NEXT;
				     desc_idx = next_desc(desc),
					     desc = &vring.vr.desc[desc_idx]) {
					piov->iov_len = desc->len;
					piov->iov_base = fos;
					piov++;
					fos += desc->len;
				}
				/* Returning NULLs for VIRTIO_BLK_T_GET_ID. */
				if (hdr.type & ~(VIRTIO_BLK_T_OUT |
					VIRTIO_BLK_T_GET_ID)) {
					/*
					  VIRTIO_BLK_T_IN - does not do
					  anything. Probably for documenting.
					  VIRTIO_BLK_T_SCSI_CMD - for
					  virtio_scsi.
					  VIRTIO_BLK_T_FLUSH - turned off in
					  config space.
					  VIRTIO_BLK_T_BARRIER - defined but not
					  used in anywhere.
					*/
					mpsslog("%s() %d: type %x ",
						__func__, __LINE__,
						hdr.type);
					mpsslog("is not supported\n");
					status = -ENOTSUP;

				} else {
					ret = transfer_blocks(
					mic->mic_virtblk.virtio_block_fd,
						iovec,
						piov - iovec);
					if (ret < 0 &&
					    status != 0)
						status = ret;
				}
				/* write status and update used pointer */
				if (status != 0)
					status = status_error_check(desc);
				ret = write_status(
					mic->mic_virtblk.virtio_block_fd,
					&status);
#ifdef DEBUG
				mpsslog("%s() %d: write status=%d on desc=%p\n",
					__func__, __LINE__,
					status, desc);
#endif
			}
		}
		free(iovec);
_stop_virtblk:
		stop_virtblk(mic);
_close_backend:
		close_backend(mic);
	}  /* forever */

	pthread_exit(NULL);
}

static void
reset(struct mic_info *mic)
{
#define RESET_TIMEOUT 120
	int i = RESET_TIMEOUT;
	setsysfs(mic->name, "state", "reset");
	while (i) {
		char *state;
		state = readsysfs(mic->name, "state");
		if (!state)
			goto retry;
		mpsslog("%s: %s %d state %s\n",
			mic->name, __func__, __LINE__, state);

		/*
		 * If the shutdown was initiated by OSPM, the state stays
		 * in "suspended" which is also a valid condition for reset.
		 */
		if ((!strcmp(state, "offline")) ||
		    (!strcmp(state, "suspended"))) {
			free(state);
			break;
		}
		free(state);
retry:
		sleep(1);
		i--;
	}
}

static int
get_mic_shutdown_status(struct mic_info *mic, char *shutdown_status)
{
	if (!strcmp(shutdown_status, "nop"))
		return MIC_NOP;
	if (!strcmp(shutdown_status, "crashed"))
		return MIC_CRASHED;
	if (!strcmp(shutdown_status, "halted"))
		return MIC_HALTED;
	if (!strcmp(shutdown_status, "poweroff"))
		return MIC_POWER_OFF;
	if (!strcmp(shutdown_status, "restart"))
		return MIC_RESTART;
	mpsslog("%s: BUG invalid status %s\n", mic->name, shutdown_status);
	/* Invalid state */
	assert(0);
};

static int get_mic_state(struct mic_info *mic, char *state)
{
	if (!strcmp(state, "offline"))
		return MIC_OFFLINE;
	if (!strcmp(state, "online"))
		return MIC_ONLINE;
	if (!strcmp(state, "shutting_down"))
		return MIC_SHUTTING_DOWN;
	if (!strcmp(state, "reset_failed"))
		return MIC_RESET_FAILED;
	if (!strcmp(state, "suspending"))
		return MIC_SUSPENDING;
	if (!strcmp(state, "suspended"))
		return MIC_SUSPENDED;
	mpsslog("%s: BUG invalid state %s\n", mic->name, state);
	/* Invalid state */
	assert(0);
};

static void mic_handle_shutdown(struct mic_info *mic)
{
#define SHUTDOWN_TIMEOUT 60
	int i = SHUTDOWN_TIMEOUT, ret, stat = 0;
	char *shutdown_status;
	while (i) {
		shutdown_status = readsysfs(mic->name, "shutdown_status");
		if (!shutdown_status)
			continue;
		mpsslog("%s: %s %d shutdown_status %s\n",
			mic->name, __func__, __LINE__, shutdown_status);
		switch (get_mic_shutdown_status(mic, shutdown_status)) {
		case MIC_RESTART:
			mic->restart = 1;
		case MIC_HALTED:
		case MIC_POWER_OFF:
		case MIC_CRASHED:
			free(shutdown_status);
			goto reset;
		default:
			break;
		}
		free(shutdown_status);
		sleep(1);
		i--;
	}
reset:
	ret = kill(mic->pid, SIGTERM);
	mpsslog("%s: %s %d kill pid %d ret %d\n",
		mic->name, __func__, __LINE__,
		mic->pid, ret);
	if (!ret) {
		ret = waitpid(mic->pid, &stat,
			WIFSIGNALED(stat));
		mpsslog("%s: %s %d waitpid ret %d pid %d\n",
			mic->name, __func__, __LINE__,
			ret, mic->pid);
	}
	if (ret == mic->pid)
		reset(mic);
}

static void *
mic_config(void *arg)
{
	struct mic_info *mic = (struct mic_info *)arg;
	char *state = NULL;
	char pathname[PATH_MAX];
	int fd, ret;
	struct pollfd ufds[1];
	char value[4096];

	snprintf(pathname, PATH_MAX - 1, "%s/%s/%s",
		 MICSYSFSDIR, mic->name, "state");

	fd = open(pathname, O_RDONLY);
	if (fd < 0) {
		mpsslog("%s: opening file %s failed %s\n",
			mic->name, pathname, strerror(errno));
		goto error;
	}

	do {
		ret = lseek(fd, 0, SEEK_SET);
		if (ret < 0) {
			mpsslog("%s: Failed to seek to file start '%s': %s\n",
				mic->name, pathname, strerror(errno));
			goto close_error1;
		}
		ret = read(fd, value, sizeof(value));
		if (ret < 0) {
			mpsslog("%s: Failed to read sysfs entry '%s': %s\n",
				mic->name, pathname, strerror(errno));
			goto close_error1;
		}
retry:
		state = readsysfs(mic->name, "state");
		if (!state)
			goto retry;
		mpsslog("%s: %s %d state %s\n",
			mic->name, __func__, __LINE__, state);
		switch (get_mic_state(mic, state)) {
		case MIC_SHUTTING_DOWN:
			mic_handle_shutdown(mic);
			goto close_error;
		case MIC_SUSPENDING:
			mic->boot_on_resume = 1;
			setsysfs(mic->name, "state", "suspend");
			mic_handle_shutdown(mic);
			goto close_error;
		case MIC_OFFLINE:
			if (mic->boot_on_resume) {
				setsysfs(mic->name, "state", "boot");
				mic->boot_on_resume = 0;
			}
			break;
		default:
			break;
		}
		free(state);

		ufds[0].fd = fd;
		ufds[0].events = POLLERR | POLLPRI;
		ret = poll(ufds, 1, -1);
		if (ret < 0) {
			mpsslog("%s: poll failed %s\n",
				mic->name, strerror(errno));
			goto close_error1;
		}
	} while (1);
close_error:
	free(state);
close_error1:
	close(fd);
error:
	init_mic(mic);
	pthread_exit(NULL);
}

static void
set_cmdline(struct mic_info *mic)
{
	char buffer[PATH_MAX];
	int len;

	len = snprintf(buffer, PATH_MAX,
		"clocksource=tsc highres=off nohz=off ");
	len += snprintf(buffer + len, PATH_MAX - len,
		"cpufreq_on;corec6_off;pc3_off;pc6_off ");
	len += snprintf(buffer + len, PATH_MAX - len,
		"ifcfg=static;address,172.31.%d.1;netmask,255.255.255.0",
		mic->id);

	setsysfs(mic->name, "cmdline", buffer);
	mpsslog("%s: Command line: \"%s\"\n", mic->name, buffer);
	snprintf(buffer, PATH_MAX, "172.31.%d.1", mic->id);
	mpsslog("%s: IPADDR: \"%s\"\n", mic->name, buffer);
}

static void
set_log_buf_info(struct mic_info *mic)
{
	int fd;
	off_t len;
	char system_map[] = "/lib/firmware/mic/System.map";
	char *map, *temp, log_buf[17] = {'\0'};

	fd = open(system_map, O_RDONLY);
	if (fd < 0) {
		mpsslog("%s: Opening System.map failed: %d\n",
			mic->name, errno);
		return;
	}
	len = lseek(fd, 0, SEEK_END);
	if (len < 0) {
		mpsslog("%s: Reading System.map size failed: %d\n",
			mic->name, errno);
		close(fd);
		return;
	}
	map = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map == MAP_FAILED) {
		mpsslog("%s: mmap of System.map failed: %d\n",
			mic->name, errno);
		close(fd);
		return;
	}
	temp = strstr(map, "__log_buf");
	if (!temp) {
		mpsslog("%s: __log_buf not found: %d\n", mic->name, errno);
		munmap(map, len);
		close(fd);
		return;
	}
	strncpy(log_buf, temp - 19, 16);
	setsysfs(mic->name, "log_buf_addr", log_buf);
	mpsslog("%s: log_buf_addr: %s\n", mic->name, log_buf);
	temp = strstr(map, "log_buf_len");
	if (!temp) {
		mpsslog("%s: log_buf_len not found: %d\n", mic->name, errno);
		munmap(map, len);
		close(fd);
		return;
	}
	strncpy(log_buf, temp - 19, 16);
	setsysfs(mic->name, "log_buf_len", log_buf);
	mpsslog("%s: log_buf_len: %s\n", mic->name, log_buf);
	munmap(map, len);
	close(fd);
}

static void init_mic(struct mic_info *mic);

static void
change_virtblk_backend(int x, siginfo_t *siginfo, void *p)
{
	struct mic_info *mic;

	for (mic = mic_list.next; mic != NULL; mic = mic->next)
		mic->mic_virtblk.signaled = 1/* true */;
}

static void
init_mic(struct mic_info *mic)
{
	struct sigaction ignore = {
		.sa_flags = 0,
		.sa_handler = SIG_IGN
	};
	struct sigaction act = {
		.sa_flags = SA_SIGINFO,
		.sa_sigaction = change_virtblk_backend,
	};
	char buffer[PATH_MAX];
	int err;

	/*
	 * Currently, one virtio block device is supported for each MIC card
	 * at a time. Any user (or test) can send a SIGUSR1 to the MIC daemon.
	 * The signal informs the virtio block backend about a change in the
	 * configuration file which specifies the virtio backend file name on
	 * the host. Virtio block backend then re-reads the configuration file
	 * and switches to the new block device. This signalling mechanism may
	 * not be required once multiple virtio block devices are supported by
	 * the MIC daemon.
	 */
	sigaction(SIGUSR1, &ignore, NULL);

	mic->pid = fork();
	switch (mic->pid) {
	case 0:
		set_log_buf_info(mic);
		set_cmdline(mic);
		add_virtio_device(mic, &virtcons_dev_page.dd);
		add_virtio_device(mic, &virtnet_dev_page.dd);
		err = pthread_create(&mic->mic_console.console_thread, NULL,
			virtio_console, mic);
		if (err)
			mpsslog("%s virtcons pthread_create failed %s\n",
				mic->name, strerror(err));
		err = pthread_create(&mic->mic_net.net_thread, NULL,
			virtio_net, mic);
		if (err)
			mpsslog("%s virtnet pthread_create failed %s\n",
				mic->name, strerror(err));
		err = pthread_create(&mic->mic_virtblk.block_thread, NULL,
			virtio_block, mic);
		if (err)
			mpsslog("%s virtblk pthread_create failed %s\n",
				mic->name, strerror(err));
		sigemptyset(&act.sa_mask);
		err = sigaction(SIGUSR1, &act, NULL);
		if (err)
			mpsslog("%s sigaction SIGUSR1 failed %s\n",
				mic->name, strerror(errno));
		while (1)
			sleep(60);
	case -1:
		mpsslog("fork failed MIC name %s id %d errno %d\n",
			mic->name, mic->id, errno);
		break;
	default:
		if (mic->restart) {
			snprintf(buffer, PATH_MAX, "boot");
			setsysfs(mic->name, "state", buffer);
			mpsslog("%s restarting mic %d\n",
				mic->name, mic->restart);
			mic->restart = 0;
		}
		pthread_create(&mic->config_thread, NULL, mic_config, mic);
	}
}

static void
start_daemon(void)
{
	struct mic_info *mic;

	for (mic = mic_list.next; mic != NULL; mic = mic->next)
		init_mic(mic);

	while (1)
		sleep(60);
}

static int
init_mic_list(void)
{
	struct mic_info *mic = &mic_list;
	struct dirent *file;
	DIR *dp;
	int cnt = 0;

	dp = opendir(MICSYSFSDIR);
	if (!dp)
		return 0;

	while ((file = readdir(dp)) != NULL) {
		if (!strncmp(file->d_name, "mic", 3)) {
			mic->next = calloc(1, sizeof(struct mic_info));
			if (mic->next) {
				mic = mic->next;
				mic->id = atoi(&file->d_name[3]);
				mic->name = malloc(strlen(file->d_name) + 16);
				if (mic->name)
					strcpy(mic->name, file->d_name);
				mpsslog("MIC name %s id %d\n", mic->name,
					mic->id);
				cnt++;
			}
		}
	}

	closedir(dp);
	return cnt;
}

void
mpsslog(char *format, ...)
{
	va_list args;
	char buffer[4096];
	char ts[52], *ts1;
	time_t t;

	if (logfp == NULL)
		return;

	va_start(args, format);
	vsprintf(buffer, format, args);
	va_end(args);

	time(&t);
	ts1 = ctime_r(&t, ts);
	ts1[strlen(ts1) - 1] = '\0';
	fprintf(logfp, "%s: %s", ts1, buffer);

	fflush(logfp);
}

int
main(int argc, char *argv[])
{
	int cnt;
	pid_t pid;

	myname = argv[0];

	logfp = fopen(LOGFILE_NAME, "a+");
	if (!logfp) {
		fprintf(stderr, "cannot open logfile '%s'\n", LOGFILE_NAME);
		exit(1);
	}
	pid = fork();
	switch (pid) {
	case 0:
		break;
	case -1:
		exit(2);
	default:
		exit(0);
	}

	mpsslog("MIC Daemon start\n");

	cnt = init_mic_list();
	if (cnt == 0) {
		mpsslog("MIC module not loaded\n");
		exit(3);
	}
	mpsslog("MIC found %d devices\n", cnt);

	start_daemon();

	exit(0);
}
