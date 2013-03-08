/*
 * Randomness driver for virtio
 *  Copyright (C) 2007, 2008 Rusty Russell IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/virtio.h>
#include <linux/virtio_rng.h>
#include <linux/module.h>

static struct virtqueue *vq;
static unsigned int data_avail;
static DECLARE_COMPLETION(have_data);
static bool busy;

static void random_recv_done(struct virtqueue *vq)
{
	/* We can get spurious callbacks, e.g. shared IRQs + virtio_pci. */
	if (!virtqueue_get_buf(vq, &data_avail))
		return;

	complete(&have_data);
}

/* The host will fill any buffer we give it with sweet, sweet randomness. */
static void register_buffer(u8 *buf, size_t size)
{
	struct scatterlist sg;

	sg_init_one(&sg, buf, size);

	/* There should always be room for one buffer. */
	if (virtqueue_add_buf(vq, &sg, 0, 1, buf, GFP_KERNEL) < 0)
		BUG();

	virtqueue_kick(vq);
}

static int virtio_read(struct hwrng *rng, void *buf, size_t size, bool wait)
{

	if (!busy) {
		busy = true;
		init_completion(&have_data);
		register_buffer(buf, size);
	}

	if (!wait)
		return 0;

	wait_for_completion(&have_data);

	busy = false;

	return data_avail;
}

static void virtio_cleanup(struct hwrng *rng)
{
	if (busy)
		wait_for_completion(&have_data);
}


static struct hwrng virtio_hwrng = {
	.name		= "virtio",
	.cleanup	= virtio_cleanup,
	.read		= virtio_read,
};

static int virtrng_probe(struct virtio_device *vdev)
{
	int err;

	if (vq) {
		/* We only support one device for now */
		return -EBUSY;
	}
	/* We expect a single virtqueue. */
	vq = virtio_find_single_vq(vdev, random_recv_done, "input");
	if (IS_ERR(vq)) {
		err = PTR_ERR(vq);
		vq = NULL;
		return err;
	}

	err = hwrng_register(&virtio_hwrng);
	if (err) {
		vdev->config->del_vqs(vdev);
		vq = NULL;
		return err;
	}

	return 0;
}

static void __devexit virtrng_remove(struct virtio_device *vdev)
{
	vdev->config->reset(vdev);
	hwrng_unregister(&virtio_hwrng);
	vdev->config->del_vqs(vdev);
	vq = NULL;
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_RNG, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_rng_driver = {
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtrng_probe,
	.remove =	__devexit_p(virtrng_remove),
};

static int __init init(void)
{
	return register_virtio_driver(&virtio_rng_driver);
}

static void __exit fini(void)
{
	unregister_virtio_driver(&virtio_rng_driver);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio random number driver");
MODULE_LICENSE("GPL");
