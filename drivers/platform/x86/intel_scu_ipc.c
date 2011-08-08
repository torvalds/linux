/*
 * intel_scu_ipc.c: Driver for the Intel SCU IPC mechanism
 *
 * (C) Copyright 2008-2010 Intel Corporation
 * Author: Sreedhara DS (sreedhara.ds@intel.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * SCU running in ARC processor communicates with other entity running in IA
 * core through IPC mechanism which in turn messaging between IA core ad SCU.
 * SCU has two IPC mechanism IPC-1 and IPC-2. IPC-1 is used between IA32 and
 * SCU where IPC-2 is used between P-Unit and SCU. This driver delas with
 * IPC-1 Driver provides an API for power control unit registers (e.g. MSIC)
 * along with other APIs.
 */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/sysdev.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/sfi.h>
#include <asm/mrst.h>
#include <asm/intel_scu_ipc.h>

/* IPC defines the following message types */
#define IPCMSG_WATCHDOG_TIMER 0xF8 /* Set Kernel Watchdog Threshold */
#define IPCMSG_BATTERY        0xEF /* Coulomb Counter Accumulator */
#define IPCMSG_FW_UPDATE      0xFE /* Firmware update */
#define IPCMSG_PCNTRL         0xFF /* Power controller unit read/write */
#define IPCMSG_FW_REVISION    0xF4 /* Get firmware revision */

/* Command id associated with message IPCMSG_PCNTRL */
#define IPC_CMD_PCNTRL_W      0 /* Register write */
#define IPC_CMD_PCNTRL_R      1 /* Register read */
#define IPC_CMD_PCNTRL_M      2 /* Register read-modify-write */

/*
 * IPC register summary
 *
 * IPC register blocks are memory mapped at fixed address of 0xFF11C000
 * To read or write information to the SCU, driver writes to IPC-1 memory
 * mapped registers (base address 0xFF11C000). The following is the IPC
 * mechanism
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

#define IPC_BASE_ADDR     0xFF11C000	/* IPC1 base register address */
#define IPC_MAX_ADDR      0x100		/* Maximum IPC regisers */
#define IPC_WWBUF_SIZE    20		/* IPC Write buffer Size */
#define IPC_RWBUF_SIZE    20		/* IPC Read buffer Size */
#define IPC_I2C_BASE      0xFF12B000	/* I2C control register base address */
#define IPC_I2C_MAX_ADDR  0x10		/* Maximum I2C regisers */

static int ipc_probe(struct pci_dev *dev, const struct pci_device_id *id);
static void ipc_remove(struct pci_dev *pdev);

struct intel_scu_ipc_dev {
	struct pci_dev *pdev;
	void __iomem *ipc_base;
	void __iomem *i2c_base;
};

static struct intel_scu_ipc_dev  ipcdev; /* Only one for now */

static int platform;		/* Platform type */

/*
 * IPC Read Buffer (Read Only):
 * 16 byte buffer for receiving data from SCU, if IPC command
 * processing results in response data
 */
#define IPC_READ_BUFFER		0x90

#define IPC_I2C_CNTRL_ADDR	0
#define I2C_DATA_ADDR		0x04

static DEFINE_MUTEX(ipclock); /* lock used to prevent multiple call to SCU */

/*
 * Command Register (Write Only):
 * A write to this register results in an interrupt to the SCU core processor
 * Format:
 * |rfu2(8) | size(8) | command id(4) | rfu1(3) | ioc(1) | command(8)|
 */
static inline void ipc_command(u32 cmd) /* Send ipc command */
{
	writel(cmd, ipcdev.ipc_base);
}

/*
 * IPC Write Buffer (Write Only):
 * 16-byte buffer for sending data associated with IPC command to
 * SCU. Size of the data is specified in the IPC_COMMAND_REG register
 */
static inline void ipc_data_writel(u32 data, u32 offset) /* Write ipc data */
{
	writel(data, ipcdev.ipc_base + 0x80 + offset);
}

/*
 * Status Register (Read Only):
 * Driver will read this register to get the ready/busy status of the IPC
 * block and error status of the IPC command that was just processed by SCU
 * Format:
 * |rfu3(8)|error code(8)|initiator id(8)|cmd id(4)|rfu1(2)|error(1)|busy(1)|
 */

static inline u8 ipc_read_status(void)
{
	return __raw_readl(ipcdev.ipc_base + 0x04);
}

static inline u8 ipc_data_readb(u32 offset) /* Read ipc byte data */
{
	return readb(ipcdev.ipc_base + IPC_READ_BUFFER + offset);
}

static inline u32 ipc_data_readl(u32 offset) /* Read ipc u32 data */
{
	return readl(ipcdev.ipc_base + IPC_READ_BUFFER + offset);
}

static inline int busy_loop(void) /* Wait till scu status is busy */
{
	u32 status = 0;
	u32 loop_count = 0;

	status = ipc_read_status();
	while (status & 1) {
		udelay(1); /* scu processing time is in few u secods */
		status = ipc_read_status();
		loop_count++;
		/* break if scu doesn't reset busy bit after huge retry */
		if (loop_count > 100000) {
			dev_err(&ipcdev.pdev->dev, "IPC timed out");
			return -ETIMEDOUT;
		}
	}
	if ((status >> 1) & 1)
		return -EIO;

	return 0;
}

/* Read/Write power control(PMIC in Langwell, MSIC in PenWell) registers */
static int pwr_reg_rdwr(u16 *addr, u8 *data, u32 count, u32 op, u32 id)
{
	int i, nc, bytes, d;
	u32 offset = 0;
	int err;
	u8 cbuf[IPC_WWBUF_SIZE] = { };
	u32 *wbuf = (u32 *)&cbuf;

	mutex_lock(&ipclock);

	memset(cbuf, 0, sizeof(cbuf));

	if (ipcdev.pdev == NULL) {
		mutex_unlock(&ipclock);
		return -ENODEV;
	}

	if (platform != MRST_CPU_CHIP_PENWELL) {
		bytes = 0;
		d = 0;
		for (i = 0; i < count; i++) {
			cbuf[bytes++] = addr[i];
			cbuf[bytes++] = addr[i] >> 8;
			if (id != IPC_CMD_PCNTRL_R)
				cbuf[bytes++] = data[d++];
			if (id == IPC_CMD_PCNTRL_M)
				cbuf[bytes++] = data[d++];
		}
		for (i = 0; i < bytes; i += 4)
			ipc_data_writel(wbuf[i/4], i);
		ipc_command(bytes << 16 |  id << 12 | 0 << 8 | op);
	} else {
		for (nc = 0; nc < count; nc++, offset += 2) {
			cbuf[offset] = addr[nc];
			cbuf[offset + 1] = addr[nc] >> 8;
		}

		if (id == IPC_CMD_PCNTRL_R) {
			for (nc = 0, offset = 0; nc < count; nc++, offset += 4)
				ipc_data_writel(wbuf[nc], offset);
			ipc_command((count*2) << 16 |  id << 12 | 0 << 8 | op);
		} else if (id == IPC_CMD_PCNTRL_W) {
			for (nc = 0; nc < count; nc++, offset += 1)
				cbuf[offset] = data[nc];
			for (nc = 0, offset = 0; nc < count; nc++, offset += 4)
				ipc_data_writel(wbuf[nc], offset);
			ipc_command((count*3) << 16 |  id << 12 | 0 << 8 | op);
		} else if (id == IPC_CMD_PCNTRL_M) {
			cbuf[offset] = data[0];
			cbuf[offset + 1] = data[1];
			ipc_data_writel(wbuf[0], 0); /* Write wbuff */
			ipc_command(4 << 16 |  id << 12 | 0 << 8 | op);
		}
	}

	err = busy_loop();
	if (id == IPC_CMD_PCNTRL_R) { /* Read rbuf */
		/* Workaround: values are read as 0 without memcpy_fromio */
		memcpy_fromio(cbuf, ipcdev.ipc_base + 0x90, 16);
		if (platform != MRST_CPU_CHIP_PENWELL) {
			for (nc = 0, offset = 2; nc < count; nc++, offset += 3)
				data[nc] = ipc_data_readb(offset);
		} else {
			for (nc = 0; nc < count; nc++)
				data[nc] = ipc_data_readb(nc);
		}
	}
	mutex_unlock(&ipclock);
	return err;
}

/**
 *	intel_scu_ipc_ioread8		-	read a word via the SCU
 *	@addr: register on SCU
 *	@data: return pointer for read byte
 *
 *	Read a single register. Returns 0 on success or an error code. All
 *	locking between SCU accesses is handled for the caller.
 *
 *	This function may sleep.
 */
int intel_scu_ipc_ioread8(u16 addr, u8 *data)
{
	return pwr_reg_rdwr(&addr, data, 1, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_R);
}
EXPORT_SYMBOL(intel_scu_ipc_ioread8);

/**
 *	intel_scu_ipc_ioread16		-	read a word via the SCU
 *	@addr: register on SCU
 *	@data: return pointer for read word
 *
 *	Read a register pair. Returns 0 on success or an error code. All
 *	locking between SCU accesses is handled for the caller.
 *
 *	This function may sleep.
 */
int intel_scu_ipc_ioread16(u16 addr, u16 *data)
{
	u16 x[2] = {addr, addr + 1 };
	return pwr_reg_rdwr(x, (u8 *)data, 2, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_R);
}
EXPORT_SYMBOL(intel_scu_ipc_ioread16);

/**
 *	intel_scu_ipc_ioread32		-	read a dword via the SCU
 *	@addr: register on SCU
 *	@data: return pointer for read dword
 *
 *	Read four registers. Returns 0 on success or an error code. All
 *	locking between SCU accesses is handled for the caller.
 *
 *	This function may sleep.
 */
int intel_scu_ipc_ioread32(u16 addr, u32 *data)
{
	u16 x[4] = {addr, addr + 1, addr + 2, addr + 3};
	return pwr_reg_rdwr(x, (u8 *)data, 4, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_R);
}
EXPORT_SYMBOL(intel_scu_ipc_ioread32);

/**
 *	intel_scu_ipc_iowrite8		-	write a byte via the SCU
 *	@addr: register on SCU
 *	@data: byte to write
 *
 *	Write a single register. Returns 0 on success or an error code. All
 *	locking between SCU accesses is handled for the caller.
 *
 *	This function may sleep.
 */
int intel_scu_ipc_iowrite8(u16 addr, u8 data)
{
	return pwr_reg_rdwr(&addr, &data, 1, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_W);
}
EXPORT_SYMBOL(intel_scu_ipc_iowrite8);

/**
 *	intel_scu_ipc_iowrite16		-	write a word via the SCU
 *	@addr: register on SCU
 *	@data: word to write
 *
 *	Write two registers. Returns 0 on success or an error code. All
 *	locking between SCU accesses is handled for the caller.
 *
 *	This function may sleep.
 */
int intel_scu_ipc_iowrite16(u16 addr, u16 data)
{
	u16 x[2] = {addr, addr + 1 };
	return pwr_reg_rdwr(x, (u8 *)&data, 2, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_W);
}
EXPORT_SYMBOL(intel_scu_ipc_iowrite16);

/**
 *	intel_scu_ipc_iowrite32		-	write a dword via the SCU
 *	@addr: register on SCU
 *	@data: dword to write
 *
 *	Write four registers. Returns 0 on success or an error code. All
 *	locking between SCU accesses is handled for the caller.
 *
 *	This function may sleep.
 */
int intel_scu_ipc_iowrite32(u16 addr, u32 data)
{
	u16 x[4] = {addr, addr + 1, addr + 2, addr + 3};
	return pwr_reg_rdwr(x, (u8 *)&data, 4, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_W);
}
EXPORT_SYMBOL(intel_scu_ipc_iowrite32);

/**
 *	intel_scu_ipc_readvv		-	read a set of registers
 *	@addr: register list
 *	@data: bytes to return
 *	@len: length of array
 *
 *	Read registers. Returns 0 on success or an error code. All
 *	locking between SCU accesses is handled for the caller.
 *
 *	The largest array length permitted by the hardware is 5 items.
 *
 *	This function may sleep.
 */
int intel_scu_ipc_readv(u16 *addr, u8 *data, int len)
{
	return pwr_reg_rdwr(addr, data, len, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_R);
}
EXPORT_SYMBOL(intel_scu_ipc_readv);

/**
 *	intel_scu_ipc_writev		-	write a set of registers
 *	@addr: register list
 *	@data: bytes to write
 *	@len: length of array
 *
 *	Write registers. Returns 0 on success or an error code. All
 *	locking between SCU accesses is handled for the caller.
 *
 *	The largest array length permitted by the hardware is 5 items.
 *
 *	This function may sleep.
 *
 */
int intel_scu_ipc_writev(u16 *addr, u8 *data, int len)
{
	return pwr_reg_rdwr(addr, data, len, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_W);
}
EXPORT_SYMBOL(intel_scu_ipc_writev);


/**
 *	intel_scu_ipc_update_register	-	r/m/w a register
 *	@addr: register address
 *	@bits: bits to update
 *	@mask: mask of bits to update
 *
 *	Read-modify-write power control unit register. The first data argument
 *	must be register value and second is mask value
 *	mask is a bitmap that indicates which bits to update.
 *	0 = masked. Don't modify this bit, 1 = modify this bit.
 *	returns 0 on success or an error code.
 *
 *	This function may sleep. Locking between SCU accesses is handled
 *	for the caller.
 */
int intel_scu_ipc_update_register(u16 addr, u8 bits, u8 mask)
{
	u8 data[2] = { bits, mask };
	return pwr_reg_rdwr(&addr, data, 1, IPCMSG_PCNTRL, IPC_CMD_PCNTRL_M);
}
EXPORT_SYMBOL(intel_scu_ipc_update_register);

/**
 *	intel_scu_ipc_simple_command	-	send a simple command
 *	@cmd: command
 *	@sub: sub type
 *
 *	Issue a simple command to the SCU. Do not use this interface if
 *	you must then access data as any data values may be overwritten
 *	by another SCU access by the time this function returns.
 *
 *	This function may sleep. Locking for SCU accesses is handled for
 *	the caller.
 */
int intel_scu_ipc_simple_command(int cmd, int sub)
{
	int err;

	mutex_lock(&ipclock);
	if (ipcdev.pdev == NULL) {
		mutex_unlock(&ipclock);
		return -ENODEV;
	}
	ipc_command(sub << 12 | cmd);
	err = busy_loop();
	mutex_unlock(&ipclock);
	return err;
}
EXPORT_SYMBOL(intel_scu_ipc_simple_command);

/**
 *	intel_scu_ipc_command	-	command with data
 *	@cmd: command
 *	@sub: sub type
 *	@in: input data
 *	@inlen: input length in dwords
 *	@out: output data
 *	@outlein: output length in dwords
 *
 *	Issue a command to the SCU which involves data transfers. Do the
 *	data copies under the lock but leave it for the caller to interpret
 */

int intel_scu_ipc_command(int cmd, int sub, u32 *in, int inlen,
							u32 *out, int outlen)
{
	int i, err;

	mutex_lock(&ipclock);
	if (ipcdev.pdev == NULL) {
		mutex_unlock(&ipclock);
		return -ENODEV;
	}

	for (i = 0; i < inlen; i++)
		ipc_data_writel(*in++, 4 * i);

	ipc_command((inlen << 16) | (sub << 12) | cmd);
	err = busy_loop();

	for (i = 0; i < outlen; i++)
		*out++ = ipc_data_readl(4 * i);

	mutex_unlock(&ipclock);
	return err;
}
EXPORT_SYMBOL(intel_scu_ipc_command);

/*I2C commands */
#define IPC_I2C_WRITE 1 /* I2C Write command */
#define IPC_I2C_READ  2 /* I2C Read command */

/**
 *	intel_scu_ipc_i2c_cntrl		-	I2C read/write operations
 *	@addr: I2C address + command bits
 *	@data: data to read/write
 *
 *	Perform an an I2C read/write operation via the SCU. All locking is
 *	handled for the caller. This function may sleep.
 *
 *	Returns an error code or 0 on success.
 *
 *	This has to be in the IPC driver for the locking.
 */
int intel_scu_ipc_i2c_cntrl(u32 addr, u32 *data)
{
	u32 cmd = 0;

	mutex_lock(&ipclock);
	if (ipcdev.pdev == NULL) {
		mutex_unlock(&ipclock);
		return -ENODEV;
	}
	cmd = (addr >> 24) & 0xFF;
	if (cmd == IPC_I2C_READ) {
		writel(addr, ipcdev.i2c_base + IPC_I2C_CNTRL_ADDR);
		/* Write not getting updated without delay */
		mdelay(1);
		*data = readl(ipcdev.i2c_base + I2C_DATA_ADDR);
	} else if (cmd == IPC_I2C_WRITE) {
		writel(*data, ipcdev.i2c_base + I2C_DATA_ADDR);
		mdelay(1);
		writel(addr, ipcdev.i2c_base + IPC_I2C_CNTRL_ADDR);
	} else {
		dev_err(&ipcdev.pdev->dev,
			"intel_scu_ipc: I2C INVALID_CMD = 0x%x\n", cmd);

		mutex_unlock(&ipclock);
		return -EIO;
	}
	mutex_unlock(&ipclock);
	return 0;
}
EXPORT_SYMBOL(intel_scu_ipc_i2c_cntrl);

#define IPC_FW_LOAD_ADDR 0xFFFC0000 /* Storage location for FW image */
#define IPC_FW_UPDATE_MBOX_ADDR 0xFFFFDFF4 /* Mailbox between ipc and scu */
#define IPC_MAX_FW_SIZE 262144 /* 256K storage size for loading the FW image */
#define IPC_FW_MIP_HEADER_SIZE 2048 /* Firmware MIP header size */
/* IPC inform SCU to get ready for update process */
#define IPC_CMD_FW_UPDATE_READY  0x10FE
/* IPC inform SCU to go for update process */
#define IPC_CMD_FW_UPDATE_GO     0x20FE
/* Status code for fw update */
#define IPC_FW_UPDATE_SUCCESS	0x444f4e45 /* Status code 'DONE' */
#define IPC_FW_UPDATE_BADN	0x4241444E /* Status code 'BADN' */
#define IPC_FW_TXHIGH		0x54784849 /* Status code 'IPC_FW_TXHIGH' */
#define IPC_FW_TXLOW		0x54784c4f /* Status code 'IPC_FW_TXLOW' */

struct fw_update_mailbox {
	u32    status;
	u32    scu_flag;
	u32    driver_flag;
};


/**
 *	intel_scu_ipc_fw_update	-	 Firmware update utility
 *	@buffer: firmware buffer
 *	@length: size of firmware buffer
 *
 *	This function provides an interface to load the firmware into
 *	the SCU. Returns 0 on success or -1 on failure
 */
int intel_scu_ipc_fw_update(u8 *buffer, u32 length)
{
	void __iomem *fw_update_base;
	struct fw_update_mailbox __iomem *mailbox = NULL;
	int retry_cnt = 0;
	u32 status;

	mutex_lock(&ipclock);
	fw_update_base = ioremap_nocache(IPC_FW_LOAD_ADDR, (128*1024));
	if (fw_update_base == NULL) {
		mutex_unlock(&ipclock);
		return -ENOMEM;
	}
	mailbox = ioremap_nocache(IPC_FW_UPDATE_MBOX_ADDR,
					sizeof(struct fw_update_mailbox));
	if (mailbox == NULL) {
		iounmap(fw_update_base);
		mutex_unlock(&ipclock);
		return -ENOMEM;
	}

	ipc_command(IPC_CMD_FW_UPDATE_READY);

	/* Intitialize mailbox */
	writel(0, &mailbox->status);
	writel(0, &mailbox->scu_flag);
	writel(0, &mailbox->driver_flag);

	/* Driver copies the 2KB MIP header to SRAM at 0xFFFC0000*/
	memcpy_toio(fw_update_base, buffer, 0x800);

	/* Driver sends "FW Update" IPC command (CMD_ID 0xFE; MSG_ID 0x02).
	* Upon receiving this command, SCU will write the 2K MIP header
	* from 0xFFFC0000 into NAND.
	* SCU will write a status code into the Mailbox, and then set scu_flag.
	*/

	ipc_command(IPC_CMD_FW_UPDATE_GO);

	/*Driver stalls until scu_flag is set */
	while (readl(&mailbox->scu_flag) != 1) {
		rmb();
		mdelay(1);
	}

	/* Driver checks Mailbox status.
	 * If the status is 'BADN', then abort (bad NAND).
	 * If the status is 'IPC_FW_TXLOW', then continue.
	 */
	while (readl(&mailbox->status) != IPC_FW_TXLOW) {
		rmb();
		mdelay(10);
	}
	mdelay(10);

update_retry:
	if (retry_cnt > 5)
		goto update_end;

	if (readl(&mailbox->status) != IPC_FW_TXLOW)
		goto update_end;
	buffer = buffer + 0x800;
	memcpy_toio(fw_update_base, buffer, 0x20000);
	writel(1, &mailbox->driver_flag);
	while (readl(&mailbox->scu_flag) == 1) {
		rmb();
		mdelay(1);
	}

	/* check for 'BADN' */
	if (readl(&mailbox->status) == IPC_FW_UPDATE_BADN)
		goto update_end;

	while (readl(&mailbox->status) != IPC_FW_TXHIGH) {
		rmb();
		mdelay(10);
	}
	mdelay(10);

	if (readl(&mailbox->status) != IPC_FW_TXHIGH)
		goto update_end;

	buffer = buffer + 0x20000;
	memcpy_toio(fw_update_base, buffer, 0x20000);
	writel(0, &mailbox->driver_flag);

	while (mailbox->scu_flag == 0) {
		rmb();
		mdelay(1);
	}

	/* check for 'BADN' */
	if (readl(&mailbox->status) == IPC_FW_UPDATE_BADN)
		goto update_end;

	if (readl(&mailbox->status) == IPC_FW_TXLOW) {
		++retry_cnt;
		goto update_retry;
	}

update_end:
	status = readl(&mailbox->status);

	iounmap(fw_update_base);
	iounmap(mailbox);
	mutex_unlock(&ipclock);

	if (status == IPC_FW_UPDATE_SUCCESS)
		return 0;
	return -EIO;
}
EXPORT_SYMBOL(intel_scu_ipc_fw_update);

/*
 * Interrupt handler gets called when ioc bit of IPC_COMMAND_REG set to 1
 * When ioc bit is set to 1, caller api must wait for interrupt handler called
 * which in turn unlocks the caller api. Currently this is not used
 *
 * This is edge triggered so we need take no action to clear anything
 */
static irqreturn_t ioc(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

/**
 *	ipc_probe	-	probe an Intel SCU IPC
 *	@dev: the PCI device matching
 *	@id: entry in the match table
 *
 *	Enable and install an intel SCU IPC. This appears in the PCI space
 *	but uses some hard coded addresses as well.
 */
static int ipc_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int err;
	resource_size_t pci_resource;

	if (ipcdev.pdev)		/* We support only one SCU */
		return -EBUSY;

	ipcdev.pdev = pci_dev_get(dev);

	err = pci_enable_device(dev);
	if (err)
		return err;

	err = pci_request_regions(dev, "intel_scu_ipc");
	if (err)
		return err;

	pci_resource = pci_resource_start(dev, 0);
	if (!pci_resource)
		return -ENOMEM;

	if (request_irq(dev->irq, ioc, 0, "intel_scu_ipc", &ipcdev))
		return -EBUSY;

	ipcdev.ipc_base = ioremap_nocache(IPC_BASE_ADDR, IPC_MAX_ADDR);
	if (!ipcdev.ipc_base)
		return -ENOMEM;

	ipcdev.i2c_base = ioremap_nocache(IPC_I2C_BASE, IPC_I2C_MAX_ADDR);
	if (!ipcdev.i2c_base) {
		iounmap(ipcdev.ipc_base);
		return -ENOMEM;
	}

	intel_scu_devices_create();

	return 0;
}

/**
 *	ipc_remove	-	remove a bound IPC device
 *	@pdev: PCI device
 *
 *	In practice the SCU is not removable but this function is also
 *	called for each device on a module unload or cleanup which is the
 *	path that will get used.
 *
 *	Free up the mappings and release the PCI resources
 */
static void ipc_remove(struct pci_dev *pdev)
{
	free_irq(pdev->irq, &ipcdev);
	pci_release_regions(pdev);
	pci_dev_put(ipcdev.pdev);
	iounmap(ipcdev.ipc_base);
	iounmap(ipcdev.i2c_base);
	ipcdev.pdev = NULL;
	intel_scu_devices_destroy();
}

static DEFINE_PCI_DEVICE_TABLE(pci_ids) = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x080e)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x082a)},
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, pci_ids);

static struct pci_driver ipc_driver = {
	.name = "intel_scu_ipc",
	.id_table = pci_ids,
	.probe = ipc_probe,
	.remove = ipc_remove,
};


static int __init intel_scu_ipc_init(void)
{
	platform = mrst_identify_cpu();
	if (platform == 0)
		return -ENODEV;
	return  pci_register_driver(&ipc_driver);
}

static void __exit intel_scu_ipc_exit(void)
{
	pci_unregister_driver(&ipc_driver);
}

MODULE_AUTHOR("Sreedhara DS <sreedhara.ds@intel.com>");
MODULE_DESCRIPTION("Intel SCU IPC driver");
MODULE_LICENSE("GPL");

module_init(intel_scu_ipc_init);
module_exit(intel_scu_ipc_exit);
