// SPDX-License-Identifier: GPL-2.0
/*
 * virtio_pmem.c: Virtio pmem Driver
 *
 * Discovers persistent memory range information
 * from host and registers the virtual pmem device
 * with libnvdimm core.
 */
#include "virtio_pmem.h"
#include "nd.h"

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_PMEM, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

 /* Initialize virt queue */
static int init_vq(struct virtio_pmem *vpmem)
{
	/* single vq */
	vpmem->req_vq = virtio_find_single_vq(vpmem->vdev,
					virtio_pmem_host_ack, "flush_queue");
	if (IS_ERR(vpmem->req_vq))
		return PTR_ERR(vpmem->req_vq);

	spin_lock_init(&vpmem->pmem_lock);
	INIT_LIST_HEAD(&vpmem->req_list);

	return 0;
};

static int virtio_pmem_probe(struct virtio_device *vdev)
{
	struct nd_region_desc ndr_desc = {};
	int nid = dev_to_node(&vdev->dev);
	struct nd_region *nd_region;
	struct virtio_pmem *vpmem;
	struct resource res;
	int err = 0;

	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	vpmem = devm_kzalloc(&vdev->dev, sizeof(*vpmem), GFP_KERNEL);
	if (!vpmem) {
		err = -ENOMEM;
		goto out_err;
	}

	vpmem->vdev = vdev;
	vdev->priv = vpmem;
	err = init_vq(vpmem);
	if (err) {
		dev_err(&vdev->dev, "failed to initialize virtio pmem vq's\n");
		goto out_err;
	}

	virtio_cread_le(vpmem->vdev, struct virtio_pmem_config,
			start, &vpmem->start);
	virtio_cread_le(vpmem->vdev, struct virtio_pmem_config,
			size, &vpmem->size);

	res.start = vpmem->start;
	res.end   = vpmem->start + vpmem->size - 1;
	vpmem->nd_desc.provider_name = "virtio-pmem";
	vpmem->nd_desc.module = THIS_MODULE;

	vpmem->nvdimm_bus = nvdimm_bus_register(&vdev->dev,
						&vpmem->nd_desc);
	if (!vpmem->nvdimm_bus) {
		dev_err(&vdev->dev, "failed to register device with nvdimm_bus\n");
		err = -ENXIO;
		goto out_vq;
	}

	dev_set_drvdata(&vdev->dev, vpmem->nvdimm_bus);

	ndr_desc.res = &res;
	ndr_desc.numa_node = nid;
	ndr_desc.flush = async_pmem_flush;
	ndr_desc.provider_data = vdev;
	set_bit(ND_REGION_PAGEMAP, &ndr_desc.flags);
	set_bit(ND_REGION_ASYNC, &ndr_desc.flags);
	/*
	 * The NVDIMM region could be available before the
	 * virtio_device_ready() that is called by
	 * virtio_dev_probe(), so we set device ready here.
	 */
	virtio_device_ready(vdev);
	nd_region = nvdimm_pmem_region_create(vpmem->nvdimm_bus, &ndr_desc);
	if (!nd_region) {
		dev_err(&vdev->dev, "failed to create nvdimm region\n");
		err = -ENXIO;
		goto out_nd;
	}
	return 0;
out_nd:
	virtio_reset_device(vdev);
	nvdimm_bus_unregister(vpmem->nvdimm_bus);
out_vq:
	vdev->config->del_vqs(vdev);
out_err:
	return err;
}

static void virtio_pmem_remove(struct virtio_device *vdev)
{
	struct nvdimm_bus *nvdimm_bus = dev_get_drvdata(&vdev->dev);

	nvdimm_bus_unregister(nvdimm_bus);
	vdev->config->del_vqs(vdev);
	virtio_reset_device(vdev);
}

static struct virtio_driver virtio_pmem_driver = {
	.driver.name		= KBUILD_MODNAME,
	.driver.owner		= THIS_MODULE,
	.id_table		= id_table,
	.probe			= virtio_pmem_probe,
	.remove			= virtio_pmem_remove,
};

module_virtio_driver(virtio_pmem_driver);
MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio pmem driver");
MODULE_LICENSE("GPL");
