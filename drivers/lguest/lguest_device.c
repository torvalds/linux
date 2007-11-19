/*P:050 Lguest guests use a very simple method to describe devices.  It's a
 * series of device descriptors contained just above the top of normal
 * memory.
 *
 * We use the standard "virtio" device infrastructure, which provides us with a
 * console, a network and a block driver.  Each one expects some configuration
 * information and a "virtqueue" mechanism to send and receive data. :*/
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/lguest_launcher.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/interrupt.h>
#include <linux/virtio_ring.h>
#include <linux/err.h>
#include <asm/io.h>
#include <asm/paravirt.h>
#include <asm/lguest_hcall.h>

/* The pointer to our (page) of device descriptions. */
static void *lguest_devices;

/* Unique numbering for lguest devices. */
static unsigned int dev_index;

/* For Guests, device memory can be used as normal memory, so we cast away the
 * __iomem to quieten sparse. */
static inline void *lguest_map(unsigned long phys_addr, unsigned long pages)
{
	return (__force void *)ioremap(phys_addr, PAGE_SIZE*pages);
}

static inline void lguest_unmap(void *addr)
{
	iounmap((__force void __iomem *)addr);
}

/*D:100 Each lguest device is just a virtio device plus a pointer to its entry
 * in the lguest_devices page. */
struct lguest_device {
	struct virtio_device vdev;

	/* The entry in the lguest_devices page for this device. */
	struct lguest_device_desc *desc;
};

/* Since the virtio infrastructure hands us a pointer to the virtio_device all
 * the time, it helps to have a curt macro to get a pointer to the struct
 * lguest_device it's enclosed in.  */
#define to_lgdev(vdev) container_of(vdev, struct lguest_device, vdev)

/*D:130
 * Device configurations
 *
 * The configuration information for a device consists of a series of fields.
 * We don't really care what they are: the Launcher set them up, and the driver
 * will look at them during setup.
 *
 * For us these fields come immediately after that device's descriptor in the
 * lguest_devices page.
 *
 * Each field starts with a "type" byte, a "length" byte, then that number of
 * bytes of configuration information.  The device descriptor tells us the
 * total configuration length so we know when we've reached the last field. */

/* type + length bytes */
#define FHDR_LEN 2

/* This finds the first field of a given type for a device's configuration. */
static void *lg_find(struct virtio_device *vdev, u8 type, unsigned int *len)
{
	struct lguest_device_desc *desc = to_lgdev(vdev)->desc;
	int i;

	for (i = 0; i < desc->config_len; i += FHDR_LEN + desc->config[i+1]) {
		if (desc->config[i] == type) {
			/* Mark it used, so Host can know we looked at it, and
			 * also so we won't find the same one twice. */
			desc->config[i] |= 0x80;
			/* Remember, the second byte is the length. */
			*len = desc->config[i+1];
			/* We return a pointer to the field header. */
			return desc->config + i;
		}
	}

	/* Not found: return NULL for failure. */
	return NULL;
}

/* Once they've found a field, getting a copy of it is easy. */
static void lg_get(struct virtio_device *vdev, void *token,
		   void *buf, unsigned len)
{
	/* Check they didn't ask for more than the length of the field! */
	BUG_ON(len > ((u8 *)token)[1]);
	memcpy(buf, token + FHDR_LEN, len);
}

/* Setting the contents is also trivial. */
static void lg_set(struct virtio_device *vdev, void *token,
		   const void *buf, unsigned len)
{
	BUG_ON(len > ((u8 *)token)[1]);
	memcpy(token + FHDR_LEN, buf, len);
}

/* The operations to get and set the status word just access the status field
 * of the device descriptor. */
static u8 lg_get_status(struct virtio_device *vdev)
{
	return to_lgdev(vdev)->desc->status;
}

static void lg_set_status(struct virtio_device *vdev, u8 status)
{
	to_lgdev(vdev)->desc->status = status;
}

/*
 * Virtqueues
 *
 * The other piece of infrastructure virtio needs is a "virtqueue": a way of
 * the Guest device registering buffers for the other side to read from or
 * write into (ie. send and receive buffers).  Each device can have multiple
 * virtqueues: for example the console driver uses one queue for sending and
 * another for receiving.
 *
 * Fortunately for us, a very fast shared-memory-plus-descriptors virtqueue
 * already exists in virtio_ring.c.  We just need to connect it up.
 *
 * We start with the information we need to keep about each virtqueue.
 */

/*D:140 This is the information we remember about each virtqueue. */
struct lguest_vq_info
{
	/* A copy of the information contained in the device config. */
	struct lguest_vqconfig config;

	/* The address where we mapped the virtio ring, so we can unmap it. */
	void *pages;
};

/* When the virtio_ring code wants to prod the Host, it calls us here and we
 * make a hypercall.  We hand the page number of the virtqueue so the Host
 * knows which virtqueue we're talking about. */
static void lg_notify(struct virtqueue *vq)
{
	/* We store our virtqueue information in the "priv" pointer of the
	 * virtqueue structure. */
	struct lguest_vq_info *lvq = vq->priv;

	hcall(LHCALL_NOTIFY, lvq->config.pfn << PAGE_SHIFT, 0, 0);
}

/* This routine finds the first virtqueue described in the configuration of
 * this device and sets it up.
 *
 * This is kind of an ugly duckling.  It'd be nicer to have a standard
 * representation of a virtqueue in the configuration space, but it seems that
 * everyone wants to do it differently.  The KVM coders want the Guest to
 * allocate its own pages and tell the Host where they are, but for lguest it's
 * simpler for the Host to simply tell us where the pages are.
 *
 * So we provide devices with a "find virtqueue and set it up" function. */
static struct virtqueue *lg_find_vq(struct virtio_device *vdev,
				    bool (*callback)(struct virtqueue *vq))
{
	struct lguest_vq_info *lvq;
	struct virtqueue *vq;
	unsigned int len;
	void *token;
	int err;

	/* Look for a field of the correct type to mark a virtqueue.  Note that
	 * if this succeeds, then the type will be changed so it won't be found
	 * again, and future lg_find_vq() calls will find the next
	 * virtqueue (if any). */
	token = vdev->config->find(vdev, VIRTIO_CONFIG_F_VIRTQUEUE, &len);
	if (!token)
		return ERR_PTR(-ENOENT);

	lvq = kmalloc(sizeof(*lvq), GFP_KERNEL);
	if (!lvq)
		return ERR_PTR(-ENOMEM);

	/* Note: we could use a configuration space inside here, just like we
	 * do for the device.  This would allow expansion in future, because
	 * our configuration system is designed to be expansible.  But this is
	 * way easier. */
	if (len != sizeof(lvq->config)) {
		dev_err(&vdev->dev, "Unexpected virtio config len %u\n", len);
		err = -EIO;
		goto free_lvq;
	}
	/* Make a copy of the "struct lguest_vqconfig" field.  We need a copy
	 * because the config space might not be aligned correctly. */
	vdev->config->get(vdev, token, &lvq->config, sizeof(lvq->config));

	/* Figure out how many pages the ring will take, and map that memory */
	lvq->pages = lguest_map((unsigned long)lvq->config.pfn << PAGE_SHIFT,
				DIV_ROUND_UP(vring_size(lvq->config.num,
							PAGE_SIZE),
					     PAGE_SIZE));
	if (!lvq->pages) {
		err = -ENOMEM;
		goto free_lvq;
	}

	/* OK, tell virtio_ring.c to set up a virtqueue now we know its size
	 * and we've got a pointer to its pages. */
	vq = vring_new_virtqueue(lvq->config.num, vdev, lvq->pages,
				 lg_notify, callback);
	if (!vq) {
		err = -ENOMEM;
		goto unmap;
	}

	/* Tell the interrupt for this virtqueue to go to the virtio_ring
	 * interrupt handler. */
	/* FIXME: We used to have a flag for the Host to tell us we could use
	 * the interrupt as a source of randomness: it'd be nice to have that
	 * back.. */
	err = request_irq(lvq->config.irq, vring_interrupt, IRQF_SHARED,
			  vdev->dev.bus_id, vq);
	if (err)
		goto destroy_vring;

	/* Last of all we hook up our 'struct lguest_vq_info" to the
	 * virtqueue's priv pointer. */
	vq->priv = lvq;
	return vq;

destroy_vring:
	vring_del_virtqueue(vq);
unmap:
	lguest_unmap(lvq->pages);
free_lvq:
	kfree(lvq);
	return ERR_PTR(err);
}
/*:*/

/* Cleaning up a virtqueue is easy */
static void lg_del_vq(struct virtqueue *vq)
{
	struct lguest_vq_info *lvq = vq->priv;

	/* Release the interrupt */
	free_irq(lvq->config.irq, vq);
	/* Tell virtio_ring.c to free the virtqueue. */
	vring_del_virtqueue(vq);
	/* Unmap the pages containing the ring. */
	lguest_unmap(lvq->pages);
	/* Free our own queue information. */
	kfree(lvq);
}

/* The ops structure which hooks everything together. */
static struct virtio_config_ops lguest_config_ops = {
	.find = lg_find,
	.get = lg_get,
	.set = lg_set,
	.get_status = lg_get_status,
	.set_status = lg_set_status,
	.find_vq = lg_find_vq,
	.del_vq = lg_del_vq,
};

/* The root device for the lguest virtio devices.  This makes them appear as
 * /sys/devices/lguest/0,1,2 not /sys/devices/0,1,2. */
static struct device lguest_root = {
	.parent = NULL,
	.bus_id = "lguest",
};

/*D:120 This is the core of the lguest bus: actually adding a new device.
 * It's a separate function because it's neater that way, and because an
 * earlier version of the code supported hotplug and unplug.  They were removed
 * early on because they were never used.
 *
 * As Andrew Tridgell says, "Untested code is buggy code".
 *
 * It's worth reading this carefully: we start with a pointer to the new device
 * descriptor in the "lguest_devices" page. */
static void add_lguest_device(struct lguest_device_desc *d)
{
	struct lguest_device *ldev;

	/* Start with zeroed memory; Linux's device layer seems to count on
	 * it. */
	ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
	if (!ldev) {
		printk(KERN_EMERG "Cannot allocate lguest dev %u\n",
		       dev_index++);
		return;
	}

	/* This devices' parent is the lguest/ dir. */
	ldev->vdev.dev.parent = &lguest_root;
	/* We have a unique device index thanks to the dev_index counter. */
	ldev->vdev.index = dev_index++;
	/* The device type comes straight from the descriptor.  There's also a
	 * device vendor field in the virtio_device struct, which we leave as
	 * 0. */
	ldev->vdev.id.device = d->type;
	/* We have a simple set of routines for querying the device's
	 * configuration information and setting its status. */
	ldev->vdev.config = &lguest_config_ops;
	/* And we remember the device's descriptor for lguest_config_ops. */
	ldev->desc = d;

	/* register_virtio_device() sets up the generic fields for the struct
	 * virtio_device and calls device_register().  This makes the bus
	 * infrastructure look for a matching driver. */
	if (register_virtio_device(&ldev->vdev) != 0) {
		printk(KERN_ERR "Failed to register lguest device %u\n",
		       ldev->vdev.index);
		kfree(ldev);
	}
}

/*D:110 scan_devices() simply iterates through the device page.  The type 0 is
 * reserved to mean "end of devices". */
static void scan_devices(void)
{
	unsigned int i;
	struct lguest_device_desc *d;

	/* We start at the page beginning, and skip over each entry. */
	for (i = 0; i < PAGE_SIZE; i += sizeof(*d) + d->config_len) {
		d = lguest_devices + i;

		/* Once we hit a zero, stop. */
		if (d->type == 0)
			break;

		add_lguest_device(d);
	}
}

/*D:105 Fairly early in boot, lguest_devices_init() is called to set up the
 * lguest device infrastructure.  We check that we are a Guest by checking
 * pv_info.name: there are other ways of checking, but this seems most
 * obvious to me.
 *
 * So we can access the "struct lguest_device_desc"s easily, we map that memory
 * and store the pointer in the global "lguest_devices".  Then we register a
 * root device from which all our devices will hang (this seems to be the
 * correct sysfs incantation).
 *
 * Finally we call scan_devices() which adds all the devices found in the
 * lguest_devices page. */
static int __init lguest_devices_init(void)
{
	if (strcmp(pv_info.name, "lguest") != 0)
		return 0;

	if (device_register(&lguest_root) != 0)
		panic("Could not register lguest root");

	/* Devices are in a single page above top of "normal" mem */
	lguest_devices = lguest_map(max_pfn<<PAGE_SHIFT, 1);

	scan_devices();
	return 0;
}
/* We do this after core stuff, but before the drivers. */
postcore_initcall(lguest_devices_init);

/*D:150 At this point in the journey we used to now wade through the lguest
 * devices themselves: net, block and console.  Since they're all now virtio
 * devices rather than lguest-specific, I've decided to ignore them.  Mostly,
 * they're kind of boring.  But this does mean you'll never experience the
 * thrill of reading the forbidden love scene buried deep in the block driver.
 *
 * "make Launcher" beckons, where we answer questions like "Where do Guests
 * come from?", and "What do you do when someone asks for optimization?". */
