// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Intel SCU IPC mechanism
 *
 * (C) Copyright 2008-2010,2015 Intel Corporation
 * Author: Sreedhara DS (sreedhara.ds@intel.com)
 *
 * SCU running in ARC processor communicates with other entity running in IA
 * core through IPC mechanism which in turn messaging between IA core ad SCU.
 * SCU has two IPC mechanism IPC-1 and IPC-2. IPC-1 is used between IA32 and
 * SCU where IPC-2 is used between P-Unit and SCU. This driver delas with
 * IPC-1 Driver provides an API for power control unit registers (e.g. MSIC)
 * along with other APIs.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <asm/intel_scu_ipc.h>

/* IPC defines the following message types */
#define IPCMSG_PCNTRL         0xff /* Power controller unit read/write */

/* Command id associated with message IPCMSG_PCNTRL */
#define IPC_CMD_PCNTRL_W      0 /* Register write */
#define IPC_CMD_PCNTRL_R      1 /* Register read */
#define IPC_CMD_PCNTRL_M      2 /* Register read-modify-write */

/*
 * IPC register summary
 *
 * IPC register blocks are memory mapped at fixed address of PCI BAR 0.
 * To read or write information to the SCU, driver writes to IPC-1 memory
 * mapped registers. The following is the IPC mechanism
 *
 * 1. IA core cDMI interface claims this transaction and converts it to a
 *    Transaction Layer Packet (TLP) message which is sent across the cDMI.
 *
 * 2. South Complex cDMI block receives this message and writes it to
 *    the IPC-1 register block, causing an interrupt to the SCU
 *
 * 3. SCU firmware decodes this interrupt and IPC message and the appropriate
 *    message handler is called within firmware.
 */

#define IPC_WWBUF_SIZE    20		/* IPC Write buffer Size */
#define IPC_RWBUF_SIZE    20		/* IPC Read buffer Size */
#define IPC_IOC	          0x100		/* IPC command register IOC bit */

struct intel_scu_ipc_dev {
	struct device dev;
	struct resource mem;
	int irq;
	void __iomem *ipc_base;
	struct completion cmd_complete;
};

#define IPC_STATUS		0x04
#define IPC_STATUS_IRQ		BIT(2)
#define IPC_STATUS_ERR		BIT(1)
#define IPC_STATUS_BUSY		BIT(0)

/*
 * IPC Write/Read Buffers:
 * 16 byte buffer for sending and receiving data to and from SCU.
 */
#define IPC_WRITE_BUFFER	0x80
#define IPC_READ_BUFFER		0x90

/* Timeout in jiffies */
#define IPC_TIMEOUT		(3 * HZ)

static struct intel_scu_ipc_dev *ipcdev; /* Only one for now */
static DEFINE_MUTEX(ipclock); /* lock used to prevent multiple call to SCU */

static struct class intel_scu_ipc_class = {
	.name = "intel_scu_ipc",
	.owner = THIS_MODULE,
};

/*
 * Send ipc command
 * Command Register (Write Only):
 * A write to this register results in an interrupt to the SCU core processor
 * Format:
 * |rfu2(8) | size(8) | command id(4) | rfu1(3) | ioc(1) | command(8)|
 */
static inline void ipc_command(struct intel_scu_ipc_dev *scu, u32 cmd)
{
	reinit_completion(&scu->cmd_complete);
	writel(cmd | IPC_IOC, scu->ipc_base);
}

/*
 * Write ipc data
 * IPC Write Buffer (Write Only):
 * 16-byte buffer for sending data associated with IPC command to
 * SCU. Size of the data is specified in the IPC_COMMAND_REG register
 */
static inline void ipc_data_writel(struct intel_scu_ipc_dev *scu, u32 data, u32 offset)
{
	writel(data, scu->ipc_base + IPC_WRITE_BUFFER + offset);
}

/*
 * Status Register (Read Only):
 * Driver will read this register to get the ready/busy status of the IPC
 * block and error status of the IPC command that was just processed by SCU
 * Format:
 * |rfu3(8)|error code(8)|initiator id(8)|cmd id(4)|rfu1(2)|error(1)|busy(1)|
 */
static inline u8 ipc_read_status(struct intel_scu_ipc_dev *scu)
{
	return __raw_readl(scu->ipc_base + IPC_STATUS);
}

/* Read ipc byte data */
static inline u8 ipc_data_readb(struct intel_scu_ipc_dev *scu, u32 offset)
{
	return readb(scu->ipc_base + IPC_READ_BUFFER + offset);
}

/* Read ipc u32 data */
static inline u32 ipc_data_readl(struct intel_scu_ipc_dev *scu, u32 offset)
{
	return readl(scu->ipc_base + IPC_READ_BUFFER + offset);
}

/* Wait till scu status is busy */
static inline int busy_loop(struct intel_scu_ipc_dev *scu)
{
	unsigned long end = jiffies + msecs_to_jiffies(IPC_TIMEOUT);

	do {
		u32 status;

		status = ipc_read_status(scu);
		if (!(status & IPC_STATUS_BUSY))
			return (status & IPC_STATUS_ERR) ? -EIO : 0;

		usleep_range(50, 100);
	} while (time_before(jiffies, end));

	dev_err(&scu->dev, "IPC timed out");
	return -ETIMEDOUT;
}

/* Wait till ipc ioc interrupt is received or timeout in 3 HZ */
static inline int ipc_wait_for_interrupt(struct intel_scu_ipc_dev *scu)
{
	int status;

	if (!wait_for_completion_timeout(&scu->cmd_complete, IPC_TIMEOUT)) {
		dev_err(&scu->dev, "IPC timed out\n");
		return -ETIMEDOUT;
	}

	status = ipc_read_status(scu);
	if (status & IPC_STATUS_ERR)
		return -EIO;

	return 0;
}

static int intel_scu_ipc_check_status(struct intel_scu_ipc_dev *scu)
{
	return scu->irq > 0 ? ipc_wait_for_interrupt(scu) : busy_loop(scu);
}

/* Read/Write power control(PMIC in Langwell, MSIC in PenWell) registers */
static int pwr_reg_rdwr(u16 *addr, u8 *data, u32 count, u32 op, u32 id)
{
	struct intel_scu_ipc_dev *scu;
	int nc;
	u32 offset = 0;
	int err;
	u8 cbuf[IPC_WWBUF_SIZE];
	u32 *wbuf = (u32 *)&cbuf;

	memset(cbuf, 0, sizeof(cbuf));

	mutex_lock(&ipclock);
	if (!ipcdev) {
		mutex_unlock(&ipclock);
		return -ENODEV;
	}
	scu = ipcdev;

	for (nc = 0; nc < count; nc++, offset += 2) {
		cbuf[offset] = addr[nc];
		cbuf[offset + 1] = addr[nc] >> 8;
	}

	if (id == IPC_CMD_PCNTRL_R) {
		for (nc = 0, offset = 0; nc < count; nc++, offset += 4)
			ipc_data_writel(scu, wbuf[nc], offset);
		ipc_command(scu, (count * 2) << 16 | id << 12 | 0 << 8 | op);
	} else if (id == IPC_CMD_PCNTRL_W) {
		for (nc = 0; nc < count; nc++, offset += 1)
			cbuf[offset] = data[nc];
		for (nc = 0, offset = 0; nc < count; nc++, offset += 4)
			ipc_data_writel(scu, wbuf[nc], offset);
		ipc_command(scu, (count * 3) << 16 | id << 12 | 0 << 8 | op);
	} else if (id == IPC_CMD_PCNTRL_M) {
		cbuf[offset] = data[0];
		cbuf[offset + 1] = data[1];
		ipc_data_writel(scu, wbuf[0], 0); /* Write wbuff */
		ipc_command(scu, 4 << 16 | id << 12 | 0 << 8 | op);
	}

	err = intel_scu_ipc_check_status(scu);
	if (!err && id == IPC_CMD_PCNTRL_R) { /* Read rbuf */
		/* Workaround: values are read as 0 without memcpy_fromio */
		memcpy_fromio(cbuf, scu->ipc_base + 0x90, 16);
		for (nc = 0; nc < count; nc++)
			data[nc] = ipc_data_readb(scu, nc);
	}
	mutex_unlock(&ipclock);
	return err;
}

/**
 * intel_scu_ipc_ioread8		-	read a word via the SCU
 * @addr: Register on SCU
 * @data: Return pointer for read byte
 *
 * Read a single register. Returns %0 on success or an error code. All
 * locking between SCU accesses is handled for the caller.
 *
 * This function may sleep.
 */
int intel_scu_ipc_ioread8(u16 addr, u8 *data)
{
	return pwr_reg_rdwr(&addr, data, 1, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_R);
}
EXPORT_SYMBOL(intel_scu_ipc_ioread8);

/**
 * intel_scu_ipc_iowrite8		-	write a byte via the SCU
 * @addr: Register on SCU
 * @data: Byte to write
 *
 * Write a single register. Returns %0 on success or an error code. All
 * locking between SCU accesses is handled for the caller.
 *
 * This function may sleep.
 */
int intel_scu_ipc_iowrite8(u16 addr, u8 data)
{
	return pwr_reg_rdwr(&addr, &data, 1, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_W);
}
EXPORT_SYMBOL(intel_scu_ipc_iowrite8);

/**
 * intel_scu_ipc_readvv		-	read a set of registers
 * @addr: Register list
 * @data: Bytes to return
 * @len: Length of array
 *
 * Read registers. Returns %0 on success or an error code. All locking
 * between SCU accesses is handled for the caller.
 *
 * The largest array length permitted by the hardware is 5 items.
 *
 * This function may sleep.
 */
int intel_scu_ipc_readv(u16 *addr, u8 *data, int len)
{
	return pwr_reg_rdwr(addr, data, len, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_R);
}
EXPORT_SYMBOL(intel_scu_ipc_readv);

/**
 * intel_scu_ipc_writev		-	write a set of registers
 * @addr: Register list
 * @data: Bytes to write
 * @len: Length of array
 *
 * Write registers. Returns %0 on success or an error code. All locking
 * between SCU accesses is handled for the caller.
 *
 * The largest array length permitted by the hardware is 5 items.
 *
 * This function may sleep.
 */
int intel_scu_ipc_writev(u16 *addr, u8 *data, int len)
{
	return pwr_reg_rdwr(addr, data, len, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_W);
}
EXPORT_SYMBOL(intel_scu_ipc_writev);

/**
 * intel_scu_ipc_update_register	-	r/m/w a register
 * @addr: Register address
 * @bits: Bits to update
 * @mask: Mask of bits to update
 *
 * Read-modify-write power control unit register. The first data argument
 * must be register value and second is mask value mask is a bitmap that
 * indicates which bits to update. %0 = masked. Don't modify this bit, %1 =
 * modify this bit. returns %0 on success or an error code.
 *
 * This function may sleep. Locking between SCU accesses is handled
 * for the caller.
 */
int intel_scu_ipc_update_register(u16 addr, u8 bits, u8 mask)
{
	u8 data[2] = { bits, mask };
	return pwr_reg_rdwr(&addr, data, 1, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_M);
}
EXPORT_SYMBOL(intel_scu_ipc_update_register);

/**
 * intel_scu_ipc_simple_command	-	send a simple command
 * @cmd: Command
 * @sub: Sub type
 *
 * Issue a simple command to the SCU. Do not use this interface if you must
 * then access data as any data values may be overwritten by another SCU
 * access by the time this function returns.
 *
 * This function may sleep. Locking for SCU accesses is handled for the
 * caller.
 */
int intel_scu_ipc_simple_command(int cmd, int sub)
{
	struct intel_scu_ipc_dev *scu;
	int err;

	mutex_lock(&ipclock);
	if (!ipcdev) {
		mutex_unlock(&ipclock);
		return -ENODEV;
	}
	scu = ipcdev;
	ipc_command(scu, sub << 12 | cmd);
	err = intel_scu_ipc_check_status(scu);
	mutex_unlock(&ipclock);
	return err;
}
EXPORT_SYMBOL(intel_scu_ipc_simple_command);

/**
 * intel_scu_ipc_command	-	command with data
 * @cmd: Command
 * @sub: Sub type
 * @in: Input data
 * @inlen: Input length in dwords
 * @out: Output data
 * @outlen: Output length in dwords
 *
 * Issue a command to the SCU which involves data transfers. Do the
 * data copies under the lock but leave it for the caller to interpret.
 */
int intel_scu_ipc_command(int cmd, int sub, u32 *in, int inlen,
			  u32 *out, int outlen)
{
	struct intel_scu_ipc_dev *scu;
	int i, err;

	mutex_lock(&ipclock);
	if (!ipcdev) {
		mutex_unlock(&ipclock);
		return -ENODEV;
	}
	scu = ipcdev;

	for (i = 0; i < inlen; i++)
		ipc_data_writel(scu, *in++, 4 * i);

	ipc_command(scu, (inlen << 16) | (sub << 12) | cmd);
	err = intel_scu_ipc_check_status(scu);

	if (!err) {
		for (i = 0; i < outlen; i++)
			*out++ = ipc_data_readl(scu, 4 * i);
	}

	mutex_unlock(&ipclock);
	return err;
}
EXPORT_SYMBOL(intel_scu_ipc_command);

/*
 * Interrupt handler gets called when ioc bit of IPC_COMMAND_REG set to 1
 * When ioc bit is set to 1, caller api must wait for interrupt handler called
 * which in turn unlocks the caller api. Currently this is not used
 *
 * This is edge triggered so we need take no action to clear anything
 */
static irqreturn_t ioc(int irq, void *dev_id)
{
	struct intel_scu_ipc_dev *scu = dev_id;
	int status = ipc_read_status(scu);

	writel(status | IPC_STATUS_IRQ, scu->ipc_base + IPC_STATUS);
	complete(&scu->cmd_complete);

	return IRQ_HANDLED;
}

static void intel_scu_ipc_release(struct device *dev)
{
	struct intel_scu_ipc_dev *scu;

	scu = container_of(dev, struct intel_scu_ipc_dev, dev);
	if (scu->irq > 0)
		free_irq(scu->irq, scu);
	iounmap(scu->ipc_base);
	release_mem_region(scu->mem.start, resource_size(&scu->mem));
	kfree(scu);
}

/**
 * intel_scu_ipc_register() - Register SCU IPC device
 * @parent: Parent device
 * @scu_data: Data used to configure SCU IPC
 *
 * Call this function to register SCU IPC mechanism under @parent.
 * Returns pointer to the new SCU IPC device or ERR_PTR() in case of
 * failure.
 */
struct intel_scu_ipc_dev *
intel_scu_ipc_register(struct device *parent,
		       const struct intel_scu_ipc_data *scu_data)
{
	int err;
	struct intel_scu_ipc_dev *scu;
	void __iomem *ipc_base;

	mutex_lock(&ipclock);
	/* We support only one IPC */
	if (ipcdev) {
		err = -EBUSY;
		goto err_unlock;
	}

	scu = kzalloc(sizeof(*scu), GFP_KERNEL);
	if (!scu) {
		err = -ENOMEM;
		goto err_unlock;
	}

	scu->dev.parent = parent;
	scu->dev.class = &intel_scu_ipc_class;
	scu->dev.release = intel_scu_ipc_release;
	dev_set_name(&scu->dev, "intel_scu_ipc");

	if (!request_mem_region(scu_data->mem.start, resource_size(&scu_data->mem),
				"intel_scu_ipc")) {
		err = -EBUSY;
		goto err_free;
	}

	ipc_base = ioremap(scu_data->mem.start, resource_size(&scu_data->mem));
	if (!ipc_base) {
		err = -ENOMEM;
		goto err_release;
	}

	scu->ipc_base = ipc_base;
	scu->mem = scu_data->mem;
	scu->irq = scu_data->irq;
	init_completion(&scu->cmd_complete);

	if (scu->irq > 0) {
		err = request_irq(scu->irq, ioc, 0, "intel_scu_ipc", scu);
		if (err)
			goto err_unmap;
	}

	/*
	 * After this point intel_scu_ipc_release() takes care of
	 * releasing the SCU IPC resources once refcount drops to zero.
	 */
	err = device_register(&scu->dev);
	if (err) {
		put_device(&scu->dev);
		goto err_unlock;
	}

	/* Assign device at last */
	ipcdev = scu;
	mutex_unlock(&ipclock);

	return scu;

err_unmap:
	iounmap(ipc_base);
err_release:
	release_mem_region(scu_data->mem.start, resource_size(&scu_data->mem));
err_free:
	kfree(scu);
err_unlock:
	mutex_unlock(&ipclock);

	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(intel_scu_ipc_register);

static int __init intel_scu_ipc_init(void)
{
	return class_register(&intel_scu_ipc_class);
}
subsys_initcall(intel_scu_ipc_init);

static void __exit intel_scu_ipc_exit(void)
{
	class_unregister(&intel_scu_ipc_class);
}
module_exit(intel_scu_ipc_exit);
