// SPDX-License-Identifier: GPL-2.0-or-later
/*
    Copyright (c) 2001,2002 Christer Weinigel <wingel@nano-system.com>

    National Semiconductor SCx200 ACCESS.bus support
    Also supports the AMD CS5535 and AMD CS5536

    Based on i2c-keywest.c which is:
        Copyright (c) 2001 Benjamin Herrenschmidt <benh@kernel.crashing.org>
        Copyright (c) 2000 Philip Edelbrock <phil@stimpy.netroedge.com>

*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/io.h>

#include <linux/scx200.h>

MODULE_AUTHOR("Christer Weinigel <wingel@nano-system.com>");
MODULE_DESCRIPTION("NatSemi SCx200 ACCESS.bus Driver");
MODULE_ALIAS("platform:cs5535-smb");
MODULE_LICENSE("GPL");

#define MAX_DEVICES 4
static int base[MAX_DEVICES] = { 0x820, 0x840 };
module_param_hw_array(base, int, ioport, NULL, 0);
MODULE_PARM_DESC(base, "Base addresses for the ACCESS.bus controllers");

#define POLL_TIMEOUT	(HZ/5)

enum scx200_acb_state {
	state_idle,
	state_address,
	state_command,
	state_repeat_start,
	state_quick,
	state_read,
	state_write,
};

static const char *scx200_acb_state_name[] = {
	"idle",
	"address",
	"command",
	"repeat_start",
	"quick",
	"read",
	"write",
};

/* Physical interface */
struct scx200_acb_iface {
	struct scx200_acb_iface *next;
	struct i2c_adapter adapter;
	unsigned base;
	struct mutex mutex;

	/* State machine data */
	enum scx200_acb_state state;
	int result;
	u8 address_byte;
	u8 command;
	u8 *ptr;
	char needs_reset;
	unsigned len;
};

/* Register Definitions */
#define ACBSDA		(iface->base + 0)
#define ACBST		(iface->base + 1)
#define    ACBST_SDAST		0x40 /* SDA Status */
#define    ACBST_BER		0x20
#define    ACBST_NEGACK		0x10 /* Negative Acknowledge */
#define    ACBST_STASTR		0x08 /* Stall After Start */
#define    ACBST_MASTER		0x02
#define ACBCST		(iface->base + 2)
#define    ACBCST_BB		0x02
#define ACBCTL1		(iface->base + 3)
#define    ACBCTL1_STASTRE	0x80
#define    ACBCTL1_NMINTE	0x40
#define    ACBCTL1_ACK		0x10
#define    ACBCTL1_STOP		0x02
#define    ACBCTL1_START	0x01
#define ACBADDR		(iface->base + 4)
#define ACBCTL2		(iface->base + 5)
#define    ACBCTL2_ENABLE	0x01

/************************************************************************/

static void scx200_acb_machine(struct scx200_acb_iface *iface, u8 status)
{
	const char *errmsg;

	dev_dbg(&iface->adapter.dev, "state %s, status = 0x%02x\n",
		scx200_acb_state_name[iface->state], status);

	if (status & ACBST_BER) {
		errmsg = "bus error";
		goto error;
	}
	if (!(status & ACBST_MASTER)) {
		errmsg = "not master";
		goto error;
	}
	if (status & ACBST_NEGACK) {
		dev_dbg(&iface->adapter.dev, "negative ack in state %s\n",
			scx200_acb_state_name[iface->state]);

		iface->state = state_idle;
		iface->result = -ENXIO;

		outb(inb(ACBCTL1) | ACBCTL1_STOP, ACBCTL1);
		outb(ACBST_STASTR | ACBST_NEGACK, ACBST);

		/* Reset the status register */
		outb(0, ACBST);
		return;
	}

	switch (iface->state) {
	case state_idle:
		dev_warn(&iface->adapter.dev, "interrupt in idle state\n");
		break;

	case state_address:
		/* Do a pointer write first */
		outb(iface->address_byte & ~1, ACBSDA);

		iface->state = state_command;
		break;

	case state_command:
		outb(iface->command, ACBSDA);

		if (iface->address_byte & 1)
			iface->state = state_repeat_start;
		else
			iface->state = state_write;
		break;

	case state_repeat_start:
		outb(inb(ACBCTL1) | ACBCTL1_START, ACBCTL1);
		fallthrough;

	case state_quick:
		if (iface->address_byte & 1) {
			if (iface->len == 1)
				outb(inb(ACBCTL1) | ACBCTL1_ACK, ACBCTL1);
			else
				outb(inb(ACBCTL1) & ~ACBCTL1_ACK, ACBCTL1);
			outb(iface->address_byte, ACBSDA);

			iface->state = state_read;
		} else {
			outb(iface->address_byte, ACBSDA);

			iface->state = state_write;
		}
		break;

	case state_read:
		/* Set ACK if _next_ byte will be the last one */
		if (iface->len == 2)
			outb(inb(ACBCTL1) | ACBCTL1_ACK, ACBCTL1);
		else
			outb(inb(ACBCTL1) & ~ACBCTL1_ACK, ACBCTL1);

		if (iface->len == 1) {
			iface->result = 0;
			iface->state = state_idle;
			outb(inb(ACBCTL1) | ACBCTL1_STOP, ACBCTL1);
		}

		*iface->ptr++ = inb(ACBSDA);
		--iface->len;

		break;

	case state_write:
		if (iface->len == 0) {
			iface->result = 0;
			iface->state = state_idle;
			outb(inb(ACBCTL1) | ACBCTL1_STOP, ACBCTL1);
			break;
		}

		outb(*iface->ptr++, ACBSDA);
		--iface->len;

		break;
	}

	return;

 error:
	dev_err(&iface->adapter.dev,
		"%s in state %s (addr=0x%02x, len=%d, status=0x%02x)\n", errmsg,
		scx200_acb_state_name[iface->state], iface->address_byte,
		iface->len, status);

	iface->state = state_idle;
	iface->result = -EIO;
	iface->needs_reset = 1;
}

static void scx200_acb_poll(struct scx200_acb_iface *iface)
{
	u8 status;
	unsigned long timeout;

	timeout = jiffies + POLL_TIMEOUT;
	while (1) {
		status = inb(ACBST);

		/* Reset the status register to avoid the hang */
		outb(0, ACBST);

		if ((status & (ACBST_SDAST|ACBST_BER|ACBST_NEGACK)) != 0) {
			scx200_acb_machine(iface, status);
			return;
		}
		if (time_after(jiffies, timeout))
			break;
		cpu_relax();
		cond_resched();
	}

	dev_err(&iface->adapter.dev, "timeout in state %s\n",
		scx200_acb_state_name[iface->state]);

	iface->state = state_idle;
	iface->result = -EIO;
	iface->needs_reset = 1;
}

static void scx200_acb_reset(struct scx200_acb_iface *iface)
{
	/* Disable the ACCESS.bus device and Configure the SCL
	   frequency: 16 clock cycles */
	outb(0x70, ACBCTL2);
	/* Polling mode */
	outb(0, ACBCTL1);
	/* Disable slave address */
	outb(0, ACBADDR);
	/* Enable the ACCESS.bus device */
	outb(inb(ACBCTL2) | ACBCTL2_ENABLE, ACBCTL2);
	/* Free STALL after START */
	outb(inb(ACBCTL1) & ~(ACBCTL1_STASTRE | ACBCTL1_NMINTE), ACBCTL1);
	/* Send a STOP */
	outb(inb(ACBCTL1) | ACBCTL1_STOP, ACBCTL1);
	/* Clear BER, NEGACK and STASTR bits */
	outb(ACBST_BER | ACBST_NEGACK | ACBST_STASTR, ACBST);
	/* Clear BB bit */
	outb(inb(ACBCST) | ACBCST_BB, ACBCST);
}

static s32 scx200_acb_smbus_xfer(struct i2c_adapter *adapter,
				 u16 address, unsigned short flags,
				 char rw, u8 command, int size,
				 union i2c_smbus_data *data)
{
	struct scx200_acb_iface *iface = i2c_get_adapdata(adapter);
	int len;
	u8 *buffer;
	u16 cur_word;
	int rc;

	switch (size) {
	case I2C_SMBUS_QUICK:
		len = 0;
		buffer = NULL;
		break;

	case I2C_SMBUS_BYTE:
		len = 1;
		buffer = rw ? &data->byte : &command;
		break;

	case I2C_SMBUS_BYTE_DATA:
		len = 1;
		buffer = &data->byte;
		break;

	case I2C_SMBUS_WORD_DATA:
		len = 2;
		cur_word = cpu_to_le16(data->word);
		buffer = (u8 *)&cur_word;
		break;

	case I2C_SMBUS_I2C_BLOCK_DATA:
		len = data->block[0];
		if (len == 0 || len > I2C_SMBUS_BLOCK_MAX)
			return -EINVAL;
		buffer = &data->block[1];
		break;

	default:
		return -EINVAL;
	}

	dev_dbg(&adapter->dev,
		"size=%d, address=0x%x, command=0x%x, len=%d, read=%d\n",
		size, address, command, len, rw);

	if (!len && rw == I2C_SMBUS_READ) {
		dev_dbg(&adapter->dev, "zero length read\n");
		return -EINVAL;
	}

	mutex_lock(&iface->mutex);

	iface->address_byte = (address << 1) | rw;
	iface->command = command;
	iface->ptr = buffer;
	iface->len = len;
	iface->result = -EINVAL;
	iface->needs_reset = 0;

	outb(inb(ACBCTL1) | ACBCTL1_START, ACBCTL1);

	if (size == I2C_SMBUS_QUICK || size == I2C_SMBUS_BYTE)
		iface->state = state_quick;
	else
		iface->state = state_address;

	while (iface->state != state_idle)
		scx200_acb_poll(iface);

	if (iface->needs_reset)
		scx200_acb_reset(iface);

	rc = iface->result;

	mutex_unlock(&iface->mutex);

	if (rc == 0 && size == I2C_SMBUS_WORD_DATA && rw == I2C_SMBUS_READ)
		data->word = le16_to_cpu(cur_word);

#ifdef DEBUG
	dev_dbg(&adapter->dev, "transfer done, result: %d", rc);
	if (buffer) {
		int i;
		printk(" data:");
		for (i = 0; i < len; ++i)
			printk(" %02x", buffer[i]);
	}
	printk("\n");
#endif

	return rc;
}

static u32 scx200_acb_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_SMBUS_I2C_BLOCK;
}

/* For now, we only handle combined mode (smbus) */
static const struct i2c_algorithm scx200_acb_algorithm = {
	.smbus_xfer	= scx200_acb_smbus_xfer,
	.functionality	= scx200_acb_func,
};

static struct scx200_acb_iface *scx200_acb_list;
static DEFINE_MUTEX(scx200_acb_list_mutex);

static int scx200_acb_probe(struct scx200_acb_iface *iface)
{
	u8 val;

	/* Disable the ACCESS.bus device and Configure the SCL
	   frequency: 16 clock cycles */
	outb(0x70, ACBCTL2);

	if (inb(ACBCTL2) != 0x70) {
		pr_debug("ACBCTL2 readback failed\n");
		return -ENXIO;
	}

	outb(inb(ACBCTL1) | ACBCTL1_NMINTE, ACBCTL1);

	val = inb(ACBCTL1);
	if (val) {
		pr_debug("disabled, but ACBCTL1=0x%02x\n", val);
		return -ENXIO;
	}

	outb(inb(ACBCTL2) | ACBCTL2_ENABLE, ACBCTL2);

	outb(inb(ACBCTL1) | ACBCTL1_NMINTE, ACBCTL1);

	val = inb(ACBCTL1);
	if ((val & ACBCTL1_NMINTE) != ACBCTL1_NMINTE) {
		pr_debug("enabled, but NMINTE won't be set, ACBCTL1=0x%02x\n",
			 val);
		return -ENXIO;
	}

	return 0;
}

static struct scx200_acb_iface *scx200_create_iface(const char *text,
		struct device *dev, int index)
{
	struct scx200_acb_iface *iface;
	struct i2c_adapter *adapter;

	iface = kzalloc(sizeof(*iface), GFP_KERNEL);
	if (!iface)
		return NULL;

	adapter = &iface->adapter;
	i2c_set_adapdata(adapter, iface);
	snprintf(adapter->name, sizeof(adapter->name), "%s ACB%d", text, index);
	adapter->owner = THIS_MODULE;
	adapter->algo = &scx200_acb_algorithm;
	adapter->class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	adapter->dev.parent = dev;

	mutex_init(&iface->mutex);

	return iface;
}

static int scx200_acb_create(struct scx200_acb_iface *iface)
{
	struct i2c_adapter *adapter;
	int rc;

	adapter = &iface->adapter;

	rc = scx200_acb_probe(iface);
	if (rc) {
		pr_warn("probe failed\n");
		return rc;
	}

	scx200_acb_reset(iface);

	if (i2c_add_adapter(adapter) < 0) {
		pr_err("failed to register\n");
		return -ENODEV;
	}

	if (!adapter->dev.parent) {
		/* If there's no dev, we're tracking (ISA) ifaces manually */
		mutex_lock(&scx200_acb_list_mutex);
		iface->next = scx200_acb_list;
		scx200_acb_list = iface;
		mutex_unlock(&scx200_acb_list_mutex);
	}

	return 0;
}

static struct scx200_acb_iface *scx200_create_dev(const char *text,
		unsigned long base, int index, struct device *dev)
{
	struct scx200_acb_iface *iface;
	int rc;

	iface = scx200_create_iface(text, dev, index);

	if (iface == NULL)
		return NULL;

	if (!request_region(base, 8, iface->adapter.name)) {
		pr_err("can't allocate io 0x%lx-0x%lx\n", base, base + 8 - 1);
		goto errout_free;
	}

	iface->base = base;
	rc = scx200_acb_create(iface);

	if (rc == 0)
		return iface;

	release_region(base, 8);
 errout_free:
	kfree(iface);
	return NULL;
}

static int scx200_probe(struct platform_device *pdev)
{
	struct scx200_acb_iface *iface;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't fetch device resource info\n");
		return -ENODEV;
	}

	iface = scx200_create_dev("CS5535", res->start, 0, &pdev->dev);
	if (!iface)
		return -EIO;

	dev_info(&pdev->dev, "SCx200 device '%s' registered\n",
			iface->adapter.name);
	platform_set_drvdata(pdev, iface);

	return 0;
}

static void scx200_cleanup_iface(struct scx200_acb_iface *iface)
{
	i2c_del_adapter(&iface->adapter);
	release_region(iface->base, 8);
	kfree(iface);
}

static void scx200_remove(struct platform_device *pdev)
{
	struct scx200_acb_iface *iface;

	iface = platform_get_drvdata(pdev);
	scx200_cleanup_iface(iface);
}

static struct platform_driver scx200_pci_driver = {
	.driver = {
		.name = "cs5535-smb",
	},
	.probe = scx200_probe,
	.remove_new = scx200_remove,
};

static const struct pci_device_id scx200_isa[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_SCx200_BRIDGE) },
	{ PCI_DEVICE(PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_SC1100_BRIDGE) },
	{ 0, }
};

static __init void scx200_scan_isa(void)
{
	int i;

	if (!pci_dev_present(scx200_isa))
		return;

	for (i = 0; i < MAX_DEVICES; ++i) {
		if (base[i] == 0)
			continue;

		/* XXX: should we care about failures? */
		scx200_create_dev("SCx200", base[i], i, NULL);
	}
}

static int __init scx200_acb_init(void)
{
	pr_debug("NatSemi SCx200 ACCESS.bus Driver\n");

	/* First scan for ISA-based devices */
	scx200_scan_isa();	/* XXX: should we care about errors? */

	/* If at least one bus was created, init must succeed */
	if (scx200_acb_list)
		return 0;

	/* No ISA devices; register the platform driver for PCI-based devices */
	return platform_driver_register(&scx200_pci_driver);
}

static void __exit scx200_acb_cleanup(void)
{
	struct scx200_acb_iface *iface;

	platform_driver_unregister(&scx200_pci_driver);

	mutex_lock(&scx200_acb_list_mutex);
	while ((iface = scx200_acb_list) != NULL) {
		scx200_acb_list = iface->next;
		mutex_unlock(&scx200_acb_list_mutex);

		scx200_cleanup_iface(iface);

		mutex_lock(&scx200_acb_list_mutex);
	}
	mutex_unlock(&scx200_acb_list_mutex);
}

module_init(scx200_acb_init);
module_exit(scx200_acb_cleanup);
