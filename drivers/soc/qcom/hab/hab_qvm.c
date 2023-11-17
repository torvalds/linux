// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab.h"
#include "hab_qvm.h"

/*
 * 1. The entry values are only for platform without probe features. For those
 * platforms with probe feature, correct address and irq will be updated in it.
 * 2. Length of the array decides the maximum number of successful probe attempts.
 * Excessive probe attempts will fail and return -ENODEV.
 * 3. The number of vdev-shmem devices for hab in linux-lv/la.config should be
 * the same as the entry number of the below array to successfully probe and only
 * probe those vdev-shmem devices for HAB if other vdev-shmem users exist.
 */
static struct shmem_irq_config pchan_factory_settings[] = {
	{0x1b000000, 7},
	{0x1b001000, 8},
	{0x1b002000, 9},
	{0x1b003000, 10},
	{0x1b004000, 11},
	{0x1b005000, 12},
	{0x1b006000, 13},
	{0x1b007000, 14},
	{0x1b008000, 15},
	{0x1b009000, 16},
	{0x1b00a000, 17},
	{0x1b00b000, 18},
	{0x1b00c000, 19},
	{0x1b00d000, 20},
	{0x1b00e000, 21},
	{0x1b00f000, 22},
	{0x1b010000, 23},
	{0x1b011000, 24},
	{0x1b012000, 25},
	{0x1b013000, 26},
	{0x1b014000, 27},
	{0x1b015000, 28},
	{0x1b016000, 29},
	{0x1b017000, 30},
	{0x1b018000, 31},
	{0x1b019000, 32},
};

struct qvm_plugin_info qvm_priv_info = {
	pchan_factory_settings,
	ARRAY_SIZE(pchan_factory_settings),
	0,
	ARRAY_SIZE(pchan_factory_settings)
};

/*
 * this is common but only for guest
 */
uint64_t get_guest_ctrl_paddr(struct qvm_channel *dev,
	unsigned long factory_addr, int irq, const char *name, uint32_t pages)
{
	int i;
	unsigned long factory_va;

	pr_debug("name = %s, factory paddr = 0x%lx, irq %d, pages %d\n",
		name, factory_addr, irq, pages);

	/* get guest factory's va */
	factory_va = hab_shmem_factory_va(factory_addr);
	dev->guest_factory = (struct guest_shm_factory *)factory_va;

	if (dev->guest_factory->signature != GUEST_SHM_SIGNATURE) {
		pr_err("signature error: %ld != %llu, factory addr %lx\n",
			GUEST_SHM_SIGNATURE, dev->guest_factory->signature,
			factory_addr);
		iounmap(dev->guest_factory);
		return 0;
	}

	dev->guest_intr = dev->guest_factory->vector;

	/*
	 * Set the name field on the factory page to identify the shared memory
	 * region
	 */
	for (i = 0; i < strlen(name) && i < GUEST_SHM_MAX_NAME - 1; i++)
		dev->guest_factory->name[i] = name[i];
	dev->guest_factory->name[i] = (char) 0;

	guest_shm_create(dev->guest_factory, pages);

	/* See if we successfully created/attached to the region. */
	if (dev->guest_factory->status != GSS_OK) {
		pr_err("create failed: %d\n", dev->guest_factory->status);
		iounmap(dev->guest_factory);
		return 0;
	}

	pr_debug("shm creation size %x, paddr=%llx, vector %d, dev %pK\n",
		dev->guest_factory->size,
		dev->guest_factory->shmem,
		dev->guest_intr,
		dev);

	dev->factory_addr = factory_addr;
	dev->irq = irq;

	return dev->guest_factory->shmem;
}

void hab_pipe_reset(struct physical_channel *pchan)
{
	struct hab_pipe_endpoint *pipe_ep;
	struct qvm_channel *dev  = (struct qvm_channel *)pchan->hyp_data;

	pipe_ep = hab_pipe_init(dev->pipe, &dev->tx_buf,
				&dev->rx_buf, &dev->dbg_itms, PIPE_SHMEM_SIZE,
				pchan->is_be ? 0 : 1);
	if (dev->pipe_ep != pipe_ep)
		pr_warn("The pipe endpoint must not change\n");
}

/*
 * allocate hypervisor plug-in specific resource for pchan, and call hab pchan
 * alloc common function. hab driver struct is directly accessed.
 * commdev: pointer to store the pchan address
 * id: index to hab_device (mmids)
 * is_be: pchan local endpoint role
 * name: pchan name
 * return: status 0: success, otherwise: failures
 */
int habhyp_commdev_alloc(void **commdev, int is_be, char *name,
		int vmid_remote, struct hab_device *mmid_device)
{
	struct qvm_channel *dev = NULL;
	struct qvm_channel_os *dev_os = NULL;
	struct physical_channel **pchan = (struct physical_channel **)commdev;
	int ret = 0;
	char *shmdata;
	uint32_t pipe_alloc_size =
		hab_pipe_calc_required_bytes(PIPE_SHMEM_SIZE);
	uint32_t pipe_alloc_pages =
		(pipe_alloc_size + PAGE_SIZE - 1) / PAGE_SIZE;

	pr_debug("%s: pipe_alloc_size is %d\n", __func__, pipe_alloc_size);

	/* allocate common part for the commdev */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	/* allocate the os-specific data for the commdev */
	dev_os = kzalloc(sizeof(*dev_os), GFP_KERNEL);
	if (!dev_os) {
		ret = -ENOMEM;
		goto err;
	}

	dev->os_data = dev_os;

	spin_lock_init(&dev->io_lock);

	/*
	 * create/attach to the shmem region, and get back the
	 * shmem data vaddr
	 */
	shmdata = hab_shmem_attach(dev, name, pipe_alloc_pages);

	if (IS_ERR(shmdata)) {
		ret = PTR_ERR(shmdata);
		goto err;
	}

	dev->pipe = (struct hab_pipe *)shmdata;
	pr_debug("\"%s\": pipesize %d, addr 0x%pK, be %d\n", name,
				 pipe_alloc_size, dev->pipe, is_be);
	dev->pipe_ep = hab_pipe_init(dev->pipe, &dev->tx_buf, &dev->rx_buf,
		&dev->dbg_itms, PIPE_SHMEM_SIZE, is_be ? 0 : 1);
	/* newly created pchan is added to mmid device list */
	*pchan = hab_pchan_alloc(mmid_device, vmid_remote);
	if (!(*pchan)) {
		ret = -ENOMEM;
		goto err;
	}

	(*pchan)->closed = 0;
	(*pchan)->hyp_data = (void *)dev;
	strscpy((*pchan)->name, name, MAX_VMID_NAME_SIZE);
	(*pchan)->is_be = is_be;

	ret = habhyp_commdev_create_dispatcher(*pchan);
	if (ret < 0)
		goto err;

	return ret;

err:
	pr_err("%s failed\n", __func__);

	if (*commdev)
		habhyp_commdev_dealloc(*commdev);

	return ret;
}

int habhyp_commdev_dealloc(void *commdev)
{
	struct physical_channel *pchan = (struct physical_channel *)commdev;
	struct qvm_channel *dev = pchan->hyp_data;

	/* os specific deallocation for this commdev */
	habhyp_commdev_dealloc_os(commdev);

	if (get_refcnt(pchan->refcount) > 1) {
		pr_warn("potential leak pchan %s vchans %d refcnt %d\n",
				pchan->name, pchan->vcnt,
				get_refcnt(pchan->refcount));
	}

	kfree(dev->os_data);
	kfree(dev);

	hab_pchan_put(pchan);

	return 0;
}

int hab_hypervisor_register(void)
{
	int ret = 0;

	/* os-specific registration work */
	ret = hab_hypervisor_register_os();

	if (ret)
		goto done;

	pr_info("initializing for %s VM\n", hab_driver.b_server_dom ?
		"host" : "guest");

	hab_driver.hyp_priv = &qvm_priv_info;

done:
	return ret;
}

void hab_hypervisor_unregister(void)
{
	hab_hypervisor_unregister_os();
}

int hab_hypervisor_register_post(void) { return 0; }
