/* Common header for Virtio crypto device.
 *
 * Copyright 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _VIRTIO_CRYPTO_COMMON_H
#define _VIRTIO_CRYPTO_COMMON_H

#include <linux/virtio.h>
#include <linux/crypto.h>
#include <linux/spinlock.h>
#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/engine.h>


/* Internal representation of a data virtqueue */
struct data_queue {
	/* Virtqueue associated with this send _queue */
	struct virtqueue *vq;

	/* To protect the vq operations for the dataq */
	spinlock_t lock;

	/* Name of the tx queue: dataq.$index */
	char name[32];

	struct crypto_engine *engine;
};

struct virtio_crypto {
	struct virtio_device *vdev;
	struct virtqueue *ctrl_vq;
	struct data_queue *data_vq;

	/* To protect the vq operations for the controlq */
	spinlock_t ctrl_lock;

	/* Maximum of data queues supported by the device */
	u32 max_data_queues;

	/* Number of queue currently used by the driver */
	u32 curr_queue;

	/*
	 * Specifies the services mask which the device support,
	 * see VIRTIO_CRYPTO_SERVICE_*
	 */
	u32 crypto_services;

	/* Detailed algorithms mask */
	u32 cipher_algo_l;
	u32 cipher_algo_h;
	u32 hash_algo;
	u32 mac_algo_l;
	u32 mac_algo_h;
	u32 aead_algo;

	/* Maximum length of cipher key */
	u32 max_cipher_key_len;
	/* Maximum length of authenticated key */
	u32 max_auth_key_len;
	/* Maximum size of per request */
	u64 max_size;

	/* Control VQ buffers: protected by the ctrl_lock */
	struct virtio_crypto_op_ctrl_req ctrl;
	struct virtio_crypto_session_input input;
	struct virtio_crypto_inhdr ctrl_status;

	unsigned long status;
	atomic_t ref_count;
	struct list_head list;
	struct module *owner;
	uint8_t dev_id;

	/* Does the affinity hint is set for virtqueues? */
	bool affinity_hint_set;
};

struct virtio_crypto_sym_session_info {
	/* Backend session id, which come from the host side */
	__u64 session_id;
};

struct virtio_crypto_request;
typedef void (*virtio_crypto_data_callback)
		(struct virtio_crypto_request *vc_req, int len);

struct virtio_crypto_request {
	uint8_t status;
	struct virtio_crypto_op_data_req *req_data;
	struct scatterlist **sgs;
	struct data_queue *dataq;
	virtio_crypto_data_callback alg_cb;
};

int virtcrypto_devmgr_add_dev(struct virtio_crypto *vcrypto_dev);
struct list_head *virtcrypto_devmgr_get_head(void);
void virtcrypto_devmgr_rm_dev(struct virtio_crypto *vcrypto_dev);
struct virtio_crypto *virtcrypto_devmgr_get_first(void);
int virtcrypto_dev_in_use(struct virtio_crypto *vcrypto_dev);
int virtcrypto_dev_get(struct virtio_crypto *vcrypto_dev);
void virtcrypto_dev_put(struct virtio_crypto *vcrypto_dev);
int virtcrypto_dev_started(struct virtio_crypto *vcrypto_dev);
bool virtcrypto_algo_is_supported(struct virtio_crypto *vcrypto_dev,
				  uint32_t service,
				  uint32_t algo);
struct virtio_crypto *virtcrypto_get_dev_node(int node,
					      uint32_t service,
					      uint32_t algo);
int virtcrypto_dev_start(struct virtio_crypto *vcrypto);
void virtcrypto_dev_stop(struct virtio_crypto *vcrypto);
int virtio_crypto_ablkcipher_crypt_req(
	struct crypto_engine *engine, void *vreq);

void
virtcrypto_clear_request(struct virtio_crypto_request *vc_req);

static inline int virtio_crypto_get_current_node(void)
{
	int cpu, node;

	cpu = get_cpu();
	node = topology_physical_package_id(cpu);
	put_cpu();

	return node;
}

int virtio_crypto_algs_register(struct virtio_crypto *vcrypto);
void virtio_crypto_algs_unregister(struct virtio_crypto *vcrypto);

#endif /* _VIRTIO_CRYPTO_COMMON_H */
