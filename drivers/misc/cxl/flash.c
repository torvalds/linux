#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/rtas.h>

#include "cxl.h"
#include "hcalls.h"

#define DOWNLOAD_IMAGE 1
#define VALIDATE_IMAGE 2

struct ai_header {
	u16 version;
	u8  reserved0[6];
	u16 vendor;
	u16 device;
	u16 subsystem_vendor;
	u16 subsystem;
	u64 image_offset;
	u64 image_length;
	u8  reserved1[96];
};

static struct semaphore sem;
static unsigned long *buffer[CXL_AI_MAX_ENTRIES];
static struct sg_list *le;
static u64 continue_token;
static unsigned int transfer;

struct update_props_workarea {
	__be32 phandle;
	__be32 state;
	__be64 reserved;
	__be32 nprops;
} __packed;

struct update_nodes_workarea {
	__be32 state;
	__be64 unit_address;
	__be32 reserved;
} __packed;

#define DEVICE_SCOPE 3
#define NODE_ACTION_MASK	0xff000000
#define NODE_COUNT_MASK		0x00ffffff
#define OPCODE_DELETE	0x01000000
#define OPCODE_UPDATE	0x02000000
#define OPCODE_ADD	0x03000000

static int rcall(int token, char *buf, s32 scope)
{
	int rc;

	spin_lock(&rtas_data_buf_lock);

	memcpy(rtas_data_buf, buf, RTAS_DATA_BUF_SIZE);
	rc = rtas_call(token, 2, 1, NULL, rtas_data_buf, scope);
	memcpy(buf, rtas_data_buf, RTAS_DATA_BUF_SIZE);

	spin_unlock(&rtas_data_buf_lock);
	return rc;
}

static int update_property(struct device_node *dn, const char *name,
			   u32 vd, char *value)
{
	struct property *new_prop;
	u32 *val;
	int rc;

	new_prop = kzalloc(sizeof(*new_prop), GFP_KERNEL);
	if (!new_prop)
		return -ENOMEM;

	new_prop->name = kstrdup(name, GFP_KERNEL);
	if (!new_prop->name) {
		kfree(new_prop);
		return -ENOMEM;
	}

	new_prop->length = vd;
	new_prop->value = kzalloc(new_prop->length, GFP_KERNEL);
	if (!new_prop->value) {
		kfree(new_prop->name);
		kfree(new_prop);
		return -ENOMEM;
	}
	memcpy(new_prop->value, value, vd);

	val = (u32 *)new_prop->value;
	rc = cxl_update_properties(dn, new_prop);
	pr_devel("%s: update property (%s, length: %i, value: %#x)\n",
		  dn->name, name, vd, be32_to_cpu(*val));

	if (rc) {
		kfree(new_prop->name);
		kfree(new_prop->value);
		kfree(new_prop);
	}
	return rc;
}

static int update_node(__be32 phandle, s32 scope)
{
	struct update_props_workarea *upwa;
	struct device_node *dn;
	int i, rc, ret;
	char *prop_data;
	char *buf;
	int token;
	u32 nprops;
	u32 vd;

	token = rtas_token("ibm,update-properties");
	if (token == RTAS_UNKNOWN_SERVICE)
		return -EINVAL;

	buf = kzalloc(RTAS_DATA_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	dn = of_find_node_by_phandle(be32_to_cpu(phandle));
	if (!dn) {
		kfree(buf);
		return -ENOENT;
	}

	upwa = (struct update_props_workarea *)&buf[0];
	upwa->phandle = phandle;
	do {
		rc = rcall(token, buf, scope);
		if (rc < 0)
			break;

		prop_data = buf + sizeof(*upwa);
		nprops = be32_to_cpu(upwa->nprops);

		if (*prop_data == 0) {
			prop_data++;
			vd = be32_to_cpu(*(__be32 *)prop_data);
			prop_data += vd + sizeof(vd);
			nprops--;
		}

		for (i = 0; i < nprops; i++) {
			char *prop_name;

			prop_name = prop_data;
			prop_data += strlen(prop_name) + 1;
			vd = be32_to_cpu(*(__be32 *)prop_data);
			prop_data += sizeof(vd);

			if ((vd != 0x00000000) && (vd != 0x80000000)) {
				ret = update_property(dn, prop_name, vd,
						prop_data);
				if (ret)
					pr_err("cxl: Could not update property %s - %i\n",
					       prop_name, ret);

				prop_data += vd;
			}
		}
	} while (rc == 1);

	of_node_put(dn);
	kfree(buf);
	return rc;
}

static int update_devicetree(struct cxl *adapter, s32 scope)
{
	struct update_nodes_workarea *unwa;
	u32 action, node_count;
	int token, rc, i;
	__be32 *data, drc_index, phandle;
	char *buf;

	token = rtas_token("ibm,update-nodes");
	if (token == RTAS_UNKNOWN_SERVICE)
		return -EINVAL;

	buf = kzalloc(RTAS_DATA_BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	unwa = (struct update_nodes_workarea *)&buf[0];
	unwa->unit_address = cpu_to_be64(adapter->guest->handle);
	do {
		rc = rcall(token, buf, scope);
		if (rc && rc != 1)
			break;

		data = (__be32 *)buf + 4;
		while (be32_to_cpu(*data) & NODE_ACTION_MASK) {
			action = be32_to_cpu(*data) & NODE_ACTION_MASK;
			node_count = be32_to_cpu(*data) & NODE_COUNT_MASK;
			pr_devel("device reconfiguration - action: %#x, nodes: %#x\n",
				 action, node_count);
			data++;

			for (i = 0; i < node_count; i++) {
				phandle = *data++;

				switch (action) {
				case OPCODE_DELETE:
					/* nothing to do */
					break;
				case OPCODE_UPDATE:
					update_node(phandle, scope);
					break;
				case OPCODE_ADD:
					/* nothing to do, just move pointer */
					drc_index = *data++;
					break;
				}
			}
		}
	} while (rc == 1);

	kfree(buf);
	return 0;
}

static int handle_image(struct cxl *adapter, int operation,
			long (*fct)(u64, u64, u64, u64 *),
			struct cxl_adapter_image *ai)
{
	size_t mod, s_copy, len_chunk = 0;
	struct ai_header *header = NULL;
	unsigned int entries = 0, i;
	void *dest, *from;
	int rc = 0, need_header;

	/* base adapter image header */
	need_header = (ai->flags & CXL_AI_NEED_HEADER);
	if (need_header) {
		header = kzalloc(sizeof(struct ai_header), GFP_KERNEL);
		if (!header)
			return -ENOMEM;
		header->version = cpu_to_be16(1);
		header->vendor = cpu_to_be16(adapter->guest->vendor);
		header->device = cpu_to_be16(adapter->guest->device);
		header->subsystem_vendor = cpu_to_be16(adapter->guest->subsystem_vendor);
		header->subsystem = cpu_to_be16(adapter->guest->subsystem);
		header->image_offset = cpu_to_be64(CXL_AI_HEADER_SIZE);
		header->image_length = cpu_to_be64(ai->len_image);
	}

	/* number of entries in the list */
	len_chunk = ai->len_data;
	if (need_header)
		len_chunk += CXL_AI_HEADER_SIZE;

	entries = len_chunk / CXL_AI_BUFFER_SIZE;
	mod = len_chunk % CXL_AI_BUFFER_SIZE;
	if (mod)
		entries++;

	if (entries > CXL_AI_MAX_ENTRIES) {
		rc = -EINVAL;
		goto err;
	}

	/*          < -- MAX_CHUNK_SIZE = 4096 * 256 = 1048576 bytes -->
	 * chunk 0  ----------------------------------------------------
	 *          | header   |  data                                 |
	 *          ----------------------------------------------------
	 * chunk 1  ----------------------------------------------------
	 *          | data                                             |
	 *          ----------------------------------------------------
	 * ....
	 * chunk n  ----------------------------------------------------
	 *          | data                                             |
	 *          ----------------------------------------------------
	 */
	from = (void *) ai->data;
	for (i = 0; i < entries; i++) {
		dest = buffer[i];
		s_copy = CXL_AI_BUFFER_SIZE;

		if ((need_header) && (i == 0)) {
			/* add adapter image header */
			memcpy(buffer[i], header, sizeof(struct ai_header));
			s_copy = CXL_AI_BUFFER_SIZE - CXL_AI_HEADER_SIZE;
			dest += CXL_AI_HEADER_SIZE; /* image offset */
		}
		if ((i == (entries - 1)) && mod)
			s_copy = mod;

		/* copy data */
		if (copy_from_user(dest, from, s_copy))
			goto err;

		/* fill in the list */
		le[i].phys_addr = cpu_to_be64(virt_to_phys(buffer[i]));
		le[i].len = cpu_to_be64(CXL_AI_BUFFER_SIZE);
		if ((i == (entries - 1)) && mod)
			le[i].len = cpu_to_be64(mod);
		from += s_copy;
	}
	pr_devel("%s (op: %i, need header: %i, entries: %i, token: %#llx)\n",
		 __func__, operation, need_header, entries, continue_token);

	/*
	 * download/validate the adapter image to the coherent
	 * platform facility
	 */
	rc = fct(adapter->guest->handle, virt_to_phys(le), entries,
		&continue_token);
	if (rc == 0) /* success of download/validation operation */
		continue_token = 0;

err:
	kfree(header);

	return rc;
}

static int transfer_image(struct cxl *adapter, int operation,
			struct cxl_adapter_image *ai)
{
	int rc = 0;
	int afu;

	switch (operation) {
	case DOWNLOAD_IMAGE:
		rc = handle_image(adapter, operation,
				&cxl_h_download_adapter_image, ai);
		if (rc < 0) {
			pr_devel("resetting adapter\n");
			cxl_h_reset_adapter(adapter->guest->handle);
		}
		return rc;

	case VALIDATE_IMAGE:
		rc = handle_image(adapter, operation,
				&cxl_h_validate_adapter_image, ai);
		if (rc < 0) {
			pr_devel("resetting adapter\n");
			cxl_h_reset_adapter(adapter->guest->handle);
			return rc;
		}
		if (rc == 0) {
			pr_devel("remove current afu\n");
			for (afu = 0; afu < adapter->slices; afu++)
				cxl_guest_remove_afu(adapter->afu[afu]);

			pr_devel("resetting adapter\n");
			cxl_h_reset_adapter(adapter->guest->handle);

			/* The entire image has now been
			 * downloaded and the validation has
			 * been successfully performed.
			 * After that, the partition should call
			 * ibm,update-nodes and
			 * ibm,update-properties to receive the
			 * current configuration
			 */
			rc = update_devicetree(adapter, DEVICE_SCOPE);
			transfer = 1;
		}
		return rc;
	}

	return -EINVAL;
}

static long ioctl_transfer_image(struct cxl *adapter, int operation,
				struct cxl_adapter_image __user *uai)
{
	struct cxl_adapter_image ai;

	pr_devel("%s\n", __func__);

	if (copy_from_user(&ai, uai, sizeof(struct cxl_adapter_image)))
		return -EFAULT;

	/*
	 * Make sure reserved fields and bits are set to 0
	 */
	if (ai.reserved1 || ai.reserved2 || ai.reserved3 || ai.reserved4 ||
		(ai.flags & ~CXL_AI_ALL))
		return -EINVAL;

	return transfer_image(adapter, operation, &ai);
}

static int device_open(struct inode *inode, struct file *file)
{
	int adapter_num = CXL_DEVT_ADAPTER(inode->i_rdev);
	struct cxl *adapter;
	int rc = 0, i;

	pr_devel("in %s\n", __func__);

	BUG_ON(sizeof(struct ai_header) != CXL_AI_HEADER_SIZE);

	/* Allows one process to open the device by using a semaphore */
	if (down_interruptible(&sem) != 0)
		return -EPERM;

	if (!(adapter = get_cxl_adapter(adapter_num)))
		return -ENODEV;

	file->private_data = adapter;
	continue_token = 0;
	transfer = 0;

	for (i = 0; i < CXL_AI_MAX_ENTRIES; i++)
		buffer[i] = NULL;

	/* aligned buffer containing list entries which describes up to
	 * 1 megabyte of data (256 entries of 4096 bytes each)
	 *  Logical real address of buffer 0  -  Buffer 0 length in bytes
	 *  Logical real address of buffer 1  -  Buffer 1 length in bytes
	 *  Logical real address of buffer 2  -  Buffer 2 length in bytes
	 *  ....
	 *  ....
	 *  Logical real address of buffer N  -  Buffer N length in bytes
	 */
	le = (struct sg_list *)get_zeroed_page(GFP_KERNEL);
	if (!le) {
		rc = -ENOMEM;
		goto err;
	}

	for (i = 0; i < CXL_AI_MAX_ENTRIES; i++) {
		buffer[i] = (unsigned long *)get_zeroed_page(GFP_KERNEL);
		if (!buffer[i]) {
			rc = -ENOMEM;
			goto err1;
		}
	}

	return 0;

err1:
	for (i = 0; i < CXL_AI_MAX_ENTRIES; i++) {
		if (buffer[i])
			free_page((unsigned long) buffer[i]);
	}

	if (le)
		free_page((unsigned long) le);
err:
	put_device(&adapter->dev);

	return rc;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct cxl *adapter = file->private_data;

	pr_devel("in %s\n", __func__);

	if (cmd == CXL_IOCTL_DOWNLOAD_IMAGE)
		return ioctl_transfer_image(adapter,
					DOWNLOAD_IMAGE,
					(struct cxl_adapter_image __user *)arg);
	else if (cmd == CXL_IOCTL_VALIDATE_IMAGE)
		return ioctl_transfer_image(adapter,
					VALIDATE_IMAGE,
					(struct cxl_adapter_image __user *)arg);
	else
		return -EINVAL;
}

static long device_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	return device_ioctl(file, cmd, arg);
}

static int device_close(struct inode *inode, struct file *file)
{
	struct cxl *adapter = file->private_data;
	int i;

	pr_devel("in %s\n", __func__);

	for (i = 0; i < CXL_AI_MAX_ENTRIES; i++) {
		if (buffer[i])
			free_page((unsigned long) buffer[i]);
	}

	if (le)
		free_page((unsigned long) le);

	up(&sem);
	put_device(&adapter->dev);
	continue_token = 0;

	/* reload the module */
	if (transfer)
		cxl_guest_reload_module(adapter);
	else {
		pr_devel("resetting adapter\n");
		cxl_h_reset_adapter(adapter->guest->handle);
	}

	transfer = 0;
	return 0;
}

static const struct file_operations fops = {
	.owner		= THIS_MODULE,
	.open		= device_open,
	.unlocked_ioctl	= device_ioctl,
	.compat_ioctl	= device_compat_ioctl,
	.release	= device_close,
};

void cxl_guest_remove_chardev(struct cxl *adapter)
{
	cdev_del(&adapter->guest->cdev);
}

int cxl_guest_add_chardev(struct cxl *adapter)
{
	dev_t devt;
	int rc;

	devt = MKDEV(MAJOR(cxl_get_dev()), CXL_CARD_MINOR(adapter));
	cdev_init(&adapter->guest->cdev, &fops);
	if ((rc = cdev_add(&adapter->guest->cdev, devt, 1))) {
		dev_err(&adapter->dev,
			"Unable to add chardev on adapter (card%i): %i\n",
			adapter->adapter_num, rc);
		goto err;
	}
	adapter->dev.devt = devt;
	sema_init(&sem, 1);
err:
	return rc;
}
