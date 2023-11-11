// SPDX-License-Identifier: GPL-2.0-only
/*
 *   Copyright (C) 2016 T-Platforms. All Rights Reserved.
 *
 * IDT PCIe-switch NTB Linux driver
 *
 * Contact Information:
 * Serge Semin <fancer.lancer@gmail.com>, <Sergey.Semin@t-platforms.ru>
 */
/*
 *           NOTE of the IDT 89HPESx SMBus-slave interface driver
 *    This driver primarily is developed to have an access to EEPROM device of
 * IDT PCIe-switches. IDT provides a simple SMBus interface to perform IO-
 * operations from/to EEPROM, which is located at private (so called Master)
 * SMBus of switches. Using that interface this the driver creates a simple
 * binary sysfs-file in the device directory:
 * /sys/bus/i2c/devices/<bus>-<devaddr>/eeprom
 * In case if read-only flag is specified in the dts-node of device desription,
 * User-space applications won't be able to write to the EEPROM sysfs-node.
 *    Additionally IDT 89HPESx SMBus interface has an ability to write/read
 * data of device CSRs. This driver exposes debugf-file to perform simple IO
 * operations using that ability for just basic debug purpose. Particularly
 * next file is created in the specific debugfs-directory:
 * /sys/kernel/debug/idt_csr/
 * Format of the debugfs-node is:
 * $ cat /sys/kernel/debug/idt_csr/<bus>-<devaddr>/<devname>;
 * <CSR address>:<CSR value>
 * So reading the content of the file gives current CSR address and it value.
 * If User-space application wishes to change current CSR address,
 * it can just write a proper value to the sysfs-file:
 * $ echo "<CSR address>" > /sys/kernel/debug/idt_csr/<bus>-<devaddr>/<devname>
 * If it wants to change the CSR value as well, the format of the write
 * operation is:
 * $ echo "<CSR address>:<CSR value>" > \
 *        /sys/kernel/debug/idt_csr/<bus>-<devaddr>/<devname>;
 * CSR address and value can be any of hexadecimal, decimal or octal format.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/debugfs.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/i2c.h>
#include <linux/pci_ids.h>
#include <linux/delay.h>

#define IDT_NAME		"89hpesx"
#define IDT_89HPESX_DESC	"IDT 89HPESx SMBus-slave interface driver"
#define IDT_89HPESX_VER		"1.0"

MODULE_DESCRIPTION(IDT_89HPESX_DESC);
MODULE_VERSION(IDT_89HPESX_VER);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("T-platforms");

/*
 * csr_dbgdir - CSR read/write operations Debugfs directory
 */
static struct dentry *csr_dbgdir;

/*
 * struct idt_89hpesx_dev - IDT 89HPESx device data structure
 * @eesize:	Size of EEPROM in bytes (calculated from "idt,eecompatible")
 * @eero:	EEPROM Read-only flag
 * @eeaddr:	EEPROM custom address
 *
 * @inieecmd:	Initial cmd value for EEPROM read/write operations
 * @inicsrcmd:	Initial cmd value for CSR read/write operations
 * @iniccode:	Initialial command code value for IO-operations
 *
 * @csr:	CSR address to perform read operation
 *
 * @smb_write:	SMBus write method
 * @smb_read:	SMBus read method
 * @smb_mtx:	SMBus mutex
 *
 * @client:	i2c client used to perform IO operations
 *
 * @ee_file:	EEPROM read/write sysfs-file
 */
struct idt_smb_seq;
struct idt_89hpesx_dev {
	u32 eesize;
	bool eero;
	u8 eeaddr;

	u8 inieecmd;
	u8 inicsrcmd;
	u8 iniccode;

	u16 csr;

	int (*smb_write)(struct idt_89hpesx_dev *, const struct idt_smb_seq *);
	int (*smb_read)(struct idt_89hpesx_dev *, struct idt_smb_seq *);
	struct mutex smb_mtx;

	struct i2c_client *client;

	struct bin_attribute *ee_file;
	struct dentry *csr_dir;
};

/*
 * struct idt_smb_seq - sequence of data to be read/written from/to IDT 89HPESx
 * @ccode:	SMBus command code
 * @bytecnt:	Byte count of operation
 * @data:	Data to by written
 */
struct idt_smb_seq {
	u8 ccode;
	u8 bytecnt;
	u8 *data;
};

/*
 * struct idt_eeprom_seq - sequence of data to be read/written from/to EEPROM
 * @cmd:	Transaction CMD
 * @eeaddr:	EEPROM custom address
 * @memaddr:	Internal memory address of EEPROM
 * @data:	Data to be written at the memory address
 */
struct idt_eeprom_seq {
	u8 cmd;
	u8 eeaddr;
	u16 memaddr;
	u8 data;
} __packed;

/*
 * struct idt_csr_seq - sequence of data to be read/written from/to CSR
 * @cmd:	Transaction CMD
 * @csraddr:	Internal IDT device CSR address
 * @data:	Data to be read/written from/to the CSR address
 */
struct idt_csr_seq {
	u8 cmd;
	u16 csraddr;
	u32 data;
} __packed;

/*
 * SMBus command code macros
 * @CCODE_END:		Indicates the end of transaction
 * @CCODE_START:	Indicates the start of transaction
 * @CCODE_CSR:		CSR read/write transaction
 * @CCODE_EEPROM:	EEPROM read/write transaction
 * @CCODE_BYTE:		Supplied data has BYTE length
 * @CCODE_WORD:		Supplied data has WORD length
 * @CCODE_BLOCK:	Supplied data has variable length passed in bytecnt
 *			byte right following CCODE byte
 */
#define CCODE_END	((u8)0x01)
#define CCODE_START	((u8)0x02)
#define CCODE_CSR	((u8)0x00)
#define CCODE_EEPROM	((u8)0x04)
#define CCODE_BYTE	((u8)0x00)
#define CCODE_WORD	((u8)0x20)
#define CCODE_BLOCK	((u8)0x40)
#define CCODE_PEC	((u8)0x80)

/*
 * EEPROM command macros
 * @EEPROM_OP_WRITE:	EEPROM write operation
 * @EEPROM_OP_READ:	EEPROM read operation
 * @EEPROM_USA:		Use specified address of EEPROM
 * @EEPROM_NAERR:	EEPROM device is not ready to respond
 * @EEPROM_LAERR:	EEPROM arbitration loss error
 * @EEPROM_MSS:		EEPROM misplace start & stop bits error
 * @EEPROM_WR_CNT:	Bytes count to perform write operation
 * @EEPROM_WRRD_CNT:	Bytes count to write before reading
 * @EEPROM_RD_CNT:	Bytes count to perform read operation
 * @EEPROM_DEF_SIZE:	Fall back size of EEPROM
 * @EEPROM_DEF_ADDR:	Defatul EEPROM address
 * @EEPROM_TOUT:	Timeout before retry read operation if eeprom is busy
 */
#define EEPROM_OP_WRITE	((u8)0x00)
#define EEPROM_OP_READ	((u8)0x01)
#define EEPROM_USA	((u8)0x02)
#define EEPROM_NAERR	((u8)0x08)
#define EEPROM_LAERR    ((u8)0x10)
#define EEPROM_MSS	((u8)0x20)
#define EEPROM_WR_CNT	((u8)5)
#define EEPROM_WRRD_CNT	((u8)4)
#define EEPROM_RD_CNT	((u8)5)
#define EEPROM_DEF_SIZE	((u16)4096)
#define EEPROM_DEF_ADDR	((u8)0x50)
#define EEPROM_TOUT	(100)

/*
 * CSR command macros
 * @CSR_DWE:		Enable all four bytes of the operation
 * @CSR_OP_WRITE:	CSR write operation
 * @CSR_OP_READ:	CSR read operation
 * @CSR_RERR:		Read operation error
 * @CSR_WERR:		Write operation error
 * @CSR_WR_CNT:		Bytes count to perform write operation
 * @CSR_WRRD_CNT:	Bytes count to write before reading
 * @CSR_RD_CNT:		Bytes count to perform read operation
 * @CSR_MAX:		Maximum CSR address
 * @CSR_DEF:		Default CSR address
 * @CSR_REAL_ADDR:	CSR real unshifted address
 */
#define CSR_DWE			((u8)0x0F)
#define CSR_OP_WRITE		((u8)0x00)
#define CSR_OP_READ		((u8)0x10)
#define CSR_RERR		((u8)0x40)
#define CSR_WERR		((u8)0x80)
#define CSR_WR_CNT		((u8)7)
#define CSR_WRRD_CNT		((u8)3)
#define CSR_RD_CNT		((u8)7)
#define CSR_MAX			((u32)0x3FFFF)
#define CSR_DEF			((u16)0x0000)
#define CSR_REAL_ADDR(val)	((unsigned int)val << 2)

/*
 * IDT 89HPESx basic register
 * @IDT_VIDDID_CSR:	PCIe VID and DID of IDT 89HPESx
 * @IDT_VID_MASK:	Mask of VID
 */
#define IDT_VIDDID_CSR	((u32)0x0000)
#define IDT_VID_MASK	((u32)0xFFFF)

/*
 * IDT 89HPESx can send NACK when new command is sent before previous one
 * fininshed execution. In this case driver retries operation
 * certain times.
 * @RETRY_CNT:		Number of retries before giving up and fail
 * @idt_smb_safe:	Generate a retry loop on corresponding SMBus method
 */
#define RETRY_CNT (128)
#define idt_smb_safe(ops, args...) ({ \
	int __retry = RETRY_CNT; \
	s32 __sts; \
	do { \
		__sts = i2c_smbus_ ## ops ## _data(args); \
	} while (__retry-- && __sts < 0); \
	__sts; \
})

/*===========================================================================
 *                         i2c bus level IO-operations
 *===========================================================================
 */

/*
 * idt_smb_write_byte() - SMBus write method when I2C_SMBUS_BYTE_DATA operation
 *                        is only available
 * @pdev:	Pointer to the driver data
 * @seq:	Sequence of data to be written
 */
static int idt_smb_write_byte(struct idt_89hpesx_dev *pdev,
			      const struct idt_smb_seq *seq)
{
	s32 sts;
	u8 ccode;
	int idx;

	/* Loop over the supplied data sending byte one-by-one */
	for (idx = 0; idx < seq->bytecnt; idx++) {
		/* Collect the command code byte */
		ccode = seq->ccode | CCODE_BYTE;
		if (idx == 0)
			ccode |= CCODE_START;
		if (idx == seq->bytecnt - 1)
			ccode |= CCODE_END;

		/* Send data to the device */
		sts = idt_smb_safe(write_byte, pdev->client, ccode,
			seq->data[idx]);
		if (sts != 0)
			return (int)sts;
	}

	return 0;
}

/*
 * idt_smb_read_byte() - SMBus read method when I2C_SMBUS_BYTE_DATA operation
 *                        is only available
 * @pdev:	Pointer to the driver data
 * @seq:	Buffer to read data to
 */
static int idt_smb_read_byte(struct idt_89hpesx_dev *pdev,
			     struct idt_smb_seq *seq)
{
	s32 sts;
	u8 ccode;
	int idx;

	/* Loop over the supplied buffer receiving byte one-by-one */
	for (idx = 0; idx < seq->bytecnt; idx++) {
		/* Collect the command code byte */
		ccode = seq->ccode | CCODE_BYTE;
		if (idx == 0)
			ccode |= CCODE_START;
		if (idx == seq->bytecnt - 1)
			ccode |= CCODE_END;

		/* Read data from the device */
		sts = idt_smb_safe(read_byte, pdev->client, ccode);
		if (sts < 0)
			return (int)sts;

		seq->data[idx] = (u8)sts;
	}

	return 0;
}

/*
 * idt_smb_write_word() - SMBus write method when I2C_SMBUS_BYTE_DATA and
 *                        I2C_FUNC_SMBUS_WORD_DATA operations are available
 * @pdev:	Pointer to the driver data
 * @seq:	Sequence of data to be written
 */
static int idt_smb_write_word(struct idt_89hpesx_dev *pdev,
			      const struct idt_smb_seq *seq)
{
	s32 sts;
	u8 ccode;
	int idx, evencnt;

	/* Calculate the even count of data to send */
	evencnt = seq->bytecnt - (seq->bytecnt % 2);

	/* Loop over the supplied data sending two bytes at a time */
	for (idx = 0; idx < evencnt; idx += 2) {
		/* Collect the command code byte */
		ccode = seq->ccode | CCODE_WORD;
		if (idx == 0)
			ccode |= CCODE_START;
		if (idx == evencnt - 2)
			ccode |= CCODE_END;

		/* Send word data to the device */
		sts = idt_smb_safe(write_word, pdev->client, ccode,
			*(u16 *)&seq->data[idx]);
		if (sts != 0)
			return (int)sts;
	}

	/* If there is odd number of bytes then send just one last byte */
	if (seq->bytecnt != evencnt) {
		/* Collect the command code byte */
		ccode = seq->ccode | CCODE_BYTE | CCODE_END;
		if (idx == 0)
			ccode |= CCODE_START;

		/* Send byte data to the device */
		sts = idt_smb_safe(write_byte, pdev->client, ccode,
			seq->data[idx]);
		if (sts != 0)
			return (int)sts;
	}

	return 0;
}

/*
 * idt_smb_read_word() - SMBus read method when I2C_SMBUS_BYTE_DATA and
 *                       I2C_FUNC_SMBUS_WORD_DATA operations are available
 * @pdev:	Pointer to the driver data
 * @seq:	Buffer to read data to
 */
static int idt_smb_read_word(struct idt_89hpesx_dev *pdev,
			     struct idt_smb_seq *seq)
{
	s32 sts;
	u8 ccode;
	int idx, evencnt;

	/* Calculate the even count of data to send */
	evencnt = seq->bytecnt - (seq->bytecnt % 2);

	/* Loop over the supplied data reading two bytes at a time */
	for (idx = 0; idx < evencnt; idx += 2) {
		/* Collect the command code byte */
		ccode = seq->ccode | CCODE_WORD;
		if (idx == 0)
			ccode |= CCODE_START;
		if (idx == evencnt - 2)
			ccode |= CCODE_END;

		/* Read word data from the device */
		sts = idt_smb_safe(read_word, pdev->client, ccode);
		if (sts < 0)
			return (int)sts;

		*(u16 *)&seq->data[idx] = (u16)sts;
	}

	/* If there is odd number of bytes then receive just one last byte */
	if (seq->bytecnt != evencnt) {
		/* Collect the command code byte */
		ccode = seq->ccode | CCODE_BYTE | CCODE_END;
		if (idx == 0)
			ccode |= CCODE_START;

		/* Read last data byte from the device */
		sts = idt_smb_safe(read_byte, pdev->client, ccode);
		if (sts < 0)
			return (int)sts;

		seq->data[idx] = (u8)sts;
	}

	return 0;
}

/*
 * idt_smb_write_block() - SMBus write method when I2C_SMBUS_BLOCK_DATA
 *                         operation is available
 * @pdev:	Pointer to the driver data
 * @seq:	Sequence of data to be written
 */
static int idt_smb_write_block(struct idt_89hpesx_dev *pdev,
			       const struct idt_smb_seq *seq)
{
	u8 ccode;

	/* Return error if too much data passed to send */
	if (seq->bytecnt > I2C_SMBUS_BLOCK_MAX)
		return -EINVAL;

	/* Collect the command code byte */
	ccode = seq->ccode | CCODE_BLOCK | CCODE_START | CCODE_END;

	/* Send block of data to the device */
	return idt_smb_safe(write_block, pdev->client, ccode, seq->bytecnt,
		seq->data);
}

/*
 * idt_smb_read_block() - SMBus read method when I2C_SMBUS_BLOCK_DATA
 *                        operation is available
 * @pdev:	Pointer to the driver data
 * @seq:	Buffer to read data to
 */
static int idt_smb_read_block(struct idt_89hpesx_dev *pdev,
			      struct idt_smb_seq *seq)
{
	s32 sts;
	u8 ccode;

	/* Return error if too much data passed to send */
	if (seq->bytecnt > I2C_SMBUS_BLOCK_MAX)
		return -EINVAL;

	/* Collect the command code byte */
	ccode = seq->ccode | CCODE_BLOCK | CCODE_START | CCODE_END;

	/* Read block of data from the device */
	sts = idt_smb_safe(read_block, pdev->client, ccode, seq->data);
	if (sts != seq->bytecnt)
		return (sts < 0 ? sts : -ENODATA);

	return 0;
}

/*
 * idt_smb_write_i2c_block() - SMBus write method when I2C_SMBUS_I2C_BLOCK_DATA
 *                             operation is available
 * @pdev:	Pointer to the driver data
 * @seq:	Sequence of data to be written
 *
 * NOTE It's usual SMBus write block operation, except the actual data length is
 * sent as first byte of data
 */
static int idt_smb_write_i2c_block(struct idt_89hpesx_dev *pdev,
				   const struct idt_smb_seq *seq)
{
	u8 ccode, buf[I2C_SMBUS_BLOCK_MAX + 1];

	/* Return error if too much data passed to send */
	if (seq->bytecnt > I2C_SMBUS_BLOCK_MAX)
		return -EINVAL;

	/* Collect the data to send. Length byte must be added prior the data */
	buf[0] = seq->bytecnt;
	memcpy(&buf[1], seq->data, seq->bytecnt);

	/* Collect the command code byte */
	ccode = seq->ccode | CCODE_BLOCK | CCODE_START | CCODE_END;

	/* Send length and block of data to the device */
	return idt_smb_safe(write_i2c_block, pdev->client, ccode,
		seq->bytecnt + 1, buf);
}

/*
 * idt_smb_read_i2c_block() - SMBus read method when I2C_SMBUS_I2C_BLOCK_DATA
 *                            operation is available
 * @pdev:	Pointer to the driver data
 * @seq:	Buffer to read data to
 *
 * NOTE It's usual SMBus read block operation, except the actual data length is
 * retrieved as first byte of data
 */
static int idt_smb_read_i2c_block(struct idt_89hpesx_dev *pdev,
				  struct idt_smb_seq *seq)
{
	u8 ccode, buf[I2C_SMBUS_BLOCK_MAX + 1];
	s32 sts;

	/* Return error if too much data passed to send */
	if (seq->bytecnt > I2C_SMBUS_BLOCK_MAX)
		return -EINVAL;

	/* Collect the command code byte */
	ccode = seq->ccode | CCODE_BLOCK | CCODE_START | CCODE_END;

	/* Read length and block of data from the device */
	sts = idt_smb_safe(read_i2c_block, pdev->client, ccode,
		seq->bytecnt + 1, buf);
	if (sts != seq->bytecnt + 1)
		return (sts < 0 ? sts : -ENODATA);
	if (buf[0] != seq->bytecnt)
		return -ENODATA;

	/* Copy retrieved data to the output data buffer */
	memcpy(seq->data, &buf[1], seq->bytecnt);

	return 0;
}

/*===========================================================================
 *                          EEPROM IO-operations
 *===========================================================================
 */

/*
 * idt_eeprom_read_byte() - read just one byte from EEPROM
 * @pdev:	Pointer to the driver data
 * @memaddr:	Start EEPROM memory address
 * @data:	Data to be written to EEPROM
 */
static int idt_eeprom_read_byte(struct idt_89hpesx_dev *pdev, u16 memaddr,
				u8 *data)
{
	struct device *dev = &pdev->client->dev;
	struct idt_eeprom_seq eeseq;
	struct idt_smb_seq smbseq;
	int ret, retry;

	/* Initialize SMBus sequence fields */
	smbseq.ccode = pdev->iniccode | CCODE_EEPROM;
	smbseq.data = (u8 *)&eeseq;

	/*
	 * Sometimes EEPROM may respond with NACK if it's busy with previous
	 * operation, so we need to perform a few attempts of read cycle
	 */
	retry = RETRY_CNT;
	do {
		/* Send EEPROM memory address to read data from */
		smbseq.bytecnt = EEPROM_WRRD_CNT;
		eeseq.cmd = pdev->inieecmd | EEPROM_OP_READ;
		eeseq.eeaddr = pdev->eeaddr;
		eeseq.memaddr = cpu_to_le16(memaddr);
		ret = pdev->smb_write(pdev, &smbseq);
		if (ret != 0) {
			dev_err(dev, "Failed to init eeprom addr 0x%02x",
				memaddr);
			break;
		}

		/* Perform read operation */
		smbseq.bytecnt = EEPROM_RD_CNT;
		ret = pdev->smb_read(pdev, &smbseq);
		if (ret != 0) {
			dev_err(dev, "Failed to read eeprom data 0x%02x",
				memaddr);
			break;
		}

		/* Restart read operation if the device is busy */
		if (retry && (eeseq.cmd & EEPROM_NAERR)) {
			dev_dbg(dev, "EEPROM busy, retry reading after %d ms",
				EEPROM_TOUT);
			msleep(EEPROM_TOUT);
			continue;
		}

		/* Check whether IDT successfully read data from EEPROM */
		if (eeseq.cmd & (EEPROM_NAERR | EEPROM_LAERR | EEPROM_MSS)) {
			dev_err(dev,
				"Communication with eeprom failed, cmd 0x%hhx",
				eeseq.cmd);
			ret = -EREMOTEIO;
			break;
		}

		/* Save retrieved data and exit the loop */
		*data = eeseq.data;
		break;
	} while (retry--);

	/* Return the status of operation */
	return ret;
}

/*
 * idt_eeprom_write() - EEPROM write operation
 * @pdev:	Pointer to the driver data
 * @memaddr:	Start EEPROM memory address
 * @len:	Length of data to be written
 * @data:	Data to be written to EEPROM
 */
static int idt_eeprom_write(struct idt_89hpesx_dev *pdev, u16 memaddr, u16 len,
			    const u8 *data)
{
	struct device *dev = &pdev->client->dev;
	struct idt_eeprom_seq eeseq;
	struct idt_smb_seq smbseq;
	int ret;
	u16 idx;

	/* Initialize SMBus sequence fields */
	smbseq.ccode = pdev->iniccode | CCODE_EEPROM;
	smbseq.data = (u8 *)&eeseq;

	/* Send data byte-by-byte, checking if it is successfully written */
	for (idx = 0; idx < len; idx++, memaddr++) {
		/* Lock IDT SMBus device */
		mutex_lock(&pdev->smb_mtx);

		/* Perform write operation */
		smbseq.bytecnt = EEPROM_WR_CNT;
		eeseq.cmd = pdev->inieecmd | EEPROM_OP_WRITE;
		eeseq.eeaddr = pdev->eeaddr;
		eeseq.memaddr = cpu_to_le16(memaddr);
		eeseq.data = data[idx];
		ret = pdev->smb_write(pdev, &smbseq);
		if (ret != 0) {
			dev_err(dev,
				"Failed to write 0x%04hx:0x%02hhx to eeprom",
				memaddr, data[idx]);
			goto err_mutex_unlock;
		}

		/*
		 * Check whether the data is successfully written by reading
		 * from the same EEPROM memory address.
		 */
		eeseq.data = ~data[idx];
		ret = idt_eeprom_read_byte(pdev, memaddr, &eeseq.data);
		if (ret != 0)
			goto err_mutex_unlock;

		/* Check whether the read byte is the same as written one */
		if (eeseq.data != data[idx]) {
			dev_err(dev, "Values don't match 0x%02hhx != 0x%02hhx",
				eeseq.data, data[idx]);
			ret = -EREMOTEIO;
			goto err_mutex_unlock;
		}

		/* Unlock IDT SMBus device */
err_mutex_unlock:
		mutex_unlock(&pdev->smb_mtx);
		if (ret != 0)
			return ret;
	}

	return 0;
}

/*
 * idt_eeprom_read() - EEPROM read operation
 * @pdev:	Pointer to the driver data
 * @memaddr:	Start EEPROM memory address
 * @len:	Length of data to read
 * @buf:	Buffer to read data to
 */
static int idt_eeprom_read(struct idt_89hpesx_dev *pdev, u16 memaddr, u16 len,
			   u8 *buf)
{
	int ret;
	u16 idx;

	/* Read data byte-by-byte, retrying if it wasn't successful */
	for (idx = 0; idx < len; idx++, memaddr++) {
		/* Lock IDT SMBus device */
		mutex_lock(&pdev->smb_mtx);

		/* Just read the byte to the buffer */
		ret = idt_eeprom_read_byte(pdev, memaddr, &buf[idx]);

		/* Unlock IDT SMBus device */
		mutex_unlock(&pdev->smb_mtx);

		/* Return error if read operation failed */
		if (ret != 0)
			return ret;
	}

	return 0;
}

/*===========================================================================
 *                          CSR IO-operations
 *===========================================================================
 */

/*
 * idt_csr_write() - CSR write operation
 * @pdev:	Pointer to the driver data
 * @csraddr:	CSR address (with no two LS bits)
 * @data:	Data to be written to CSR
 */
static int idt_csr_write(struct idt_89hpesx_dev *pdev, u16 csraddr,
			 const u32 data)
{
	struct device *dev = &pdev->client->dev;
	struct idt_csr_seq csrseq;
	struct idt_smb_seq smbseq;
	int ret;

	/* Initialize SMBus sequence fields */
	smbseq.ccode = pdev->iniccode | CCODE_CSR;
	smbseq.data = (u8 *)&csrseq;

	/* Lock IDT SMBus device */
	mutex_lock(&pdev->smb_mtx);

	/* Perform write operation */
	smbseq.bytecnt = CSR_WR_CNT;
	csrseq.cmd = pdev->inicsrcmd | CSR_OP_WRITE;
	csrseq.csraddr = cpu_to_le16(csraddr);
	csrseq.data = cpu_to_le32(data);
	ret = pdev->smb_write(pdev, &smbseq);
	if (ret != 0) {
		dev_err(dev, "Failed to write 0x%04x: 0x%04x to csr",
			CSR_REAL_ADDR(csraddr), data);
		goto err_mutex_unlock;
	}

	/* Send CSR address to read data from */
	smbseq.bytecnt = CSR_WRRD_CNT;
	csrseq.cmd = pdev->inicsrcmd | CSR_OP_READ;
	ret = pdev->smb_write(pdev, &smbseq);
	if (ret != 0) {
		dev_err(dev, "Failed to init csr address 0x%04x",
			CSR_REAL_ADDR(csraddr));
		goto err_mutex_unlock;
	}

	/* Perform read operation */
	smbseq.bytecnt = CSR_RD_CNT;
	ret = pdev->smb_read(pdev, &smbseq);
	if (ret != 0) {
		dev_err(dev, "Failed to read csr 0x%04x",
			CSR_REAL_ADDR(csraddr));
		goto err_mutex_unlock;
	}

	/* Check whether IDT successfully retrieved CSR data */
	if (csrseq.cmd & (CSR_RERR | CSR_WERR)) {
		dev_err(dev, "IDT failed to perform CSR r/w");
		ret = -EREMOTEIO;
		goto err_mutex_unlock;
	}

	/* Unlock IDT SMBus device */
err_mutex_unlock:
	mutex_unlock(&pdev->smb_mtx);

	return ret;
}

/*
 * idt_csr_read() - CSR read operation
 * @pdev:	Pointer to the driver data
 * @csraddr:	CSR address (with no two LS bits)
 * @data:	Data to be written to CSR
 */
static int idt_csr_read(struct idt_89hpesx_dev *pdev, u16 csraddr, u32 *data)
{
	struct device *dev = &pdev->client->dev;
	struct idt_csr_seq csrseq;
	struct idt_smb_seq smbseq;
	int ret;

	/* Initialize SMBus sequence fields */
	smbseq.ccode = pdev->iniccode | CCODE_CSR;
	smbseq.data = (u8 *)&csrseq;

	/* Lock IDT SMBus device */
	mutex_lock(&pdev->smb_mtx);

	/* Send CSR register address before reading it */
	smbseq.bytecnt = CSR_WRRD_CNT;
	csrseq.cmd = pdev->inicsrcmd | CSR_OP_READ;
	csrseq.csraddr = cpu_to_le16(csraddr);
	ret = pdev->smb_write(pdev, &smbseq);
	if (ret != 0) {
		dev_err(dev, "Failed to init csr address 0x%04x",
			CSR_REAL_ADDR(csraddr));
		goto err_mutex_unlock;
	}

	/* Perform read operation */
	smbseq.bytecnt = CSR_RD_CNT;
	ret = pdev->smb_read(pdev, &smbseq);
	if (ret != 0) {
		dev_err(dev, "Failed to read csr 0x%04x",
			CSR_REAL_ADDR(csraddr));
		goto err_mutex_unlock;
	}

	/* Check whether IDT successfully retrieved CSR data */
	if (csrseq.cmd & (CSR_RERR | CSR_WERR)) {
		dev_err(dev, "IDT failed to perform CSR r/w");
		ret = -EREMOTEIO;
		goto err_mutex_unlock;
	}

	/* Save data retrieved from IDT */
	*data = le32_to_cpu(csrseq.data);

	/* Unlock IDT SMBus device */
err_mutex_unlock:
	mutex_unlock(&pdev->smb_mtx);

	return ret;
}

/*===========================================================================
 *                          Sysfs/debugfs-nodes IO-operations
 *===========================================================================
 */

/*
 * eeprom_write() - EEPROM sysfs-node write callback
 * @filep:	Pointer to the file system node
 * @kobj:	Pointer to the kernel object related to the sysfs-node
 * @attr:	Attributes of the file
 * @buf:	Buffer to write data to
 * @off:	Offset at which data should be written to
 * @count:	Number of bytes to write
 */
static ssize_t eeprom_write(struct file *filp, struct kobject *kobj,
			    struct bin_attribute *attr,
			    char *buf, loff_t off, size_t count)
{
	struct idt_89hpesx_dev *pdev;
	int ret;

	/* Retrieve driver data */
	pdev = dev_get_drvdata(kobj_to_dev(kobj));

	/* Perform EEPROM write operation */
	ret = idt_eeprom_write(pdev, (u16)off, (u16)count, (u8 *)buf);
	return (ret != 0 ? ret : count);
}

/*
 * eeprom_read() - EEPROM sysfs-node read callback
 * @filep:	Pointer to the file system node
 * @kobj:	Pointer to the kernel object related to the sysfs-node
 * @attr:	Attributes of the file
 * @buf:	Buffer to write data to
 * @off:	Offset at which data should be written to
 * @count:	Number of bytes to write
 */
static ssize_t eeprom_read(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *attr,
			   char *buf, loff_t off, size_t count)
{
	struct idt_89hpesx_dev *pdev;
	int ret;

	/* Retrieve driver data */
	pdev = dev_get_drvdata(kobj_to_dev(kobj));

	/* Perform EEPROM read operation */
	ret = idt_eeprom_read(pdev, (u16)off, (u16)count, (u8 *)buf);
	return (ret != 0 ? ret : count);
}

/*
 * idt_dbgfs_csr_write() - CSR debugfs-node write callback
 * @filep:	Pointer to the file system file descriptor
 * @buf:	Buffer to read data from
 * @count:	Size of the buffer
 * @offp:	Offset within the file
 *
 * It accepts either "0x<reg addr>:0x<value>" for saving register address
 * and writing value to specified DWORD register or "0x<reg addr>" for
 * just saving register address in order to perform next read operation.
 *
 * WARNING No spaces are allowed. Incoming string must be strictly formated as:
 * "<reg addr>:<value>". Register address must be aligned within 4 bytes
 * (one DWORD).
 */
static ssize_t idt_dbgfs_csr_write(struct file *filep, const char __user *ubuf,
				   size_t count, loff_t *offp)
{
	struct idt_89hpesx_dev *pdev = filep->private_data;
	char *colon_ch, *csraddr_str, *csrval_str;
	int ret, csraddr_len;
	u32 csraddr, csrval;
	char *buf;

	if (*offp)
		return 0;

	/* Copy data from User-space */
	buf = kmalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, count)) {
		ret = -EFAULT;
		goto free_buf;
	}
	buf[count] = 0;

	/* Find position of colon in the buffer */
	colon_ch = strnchr(buf, count, ':');

	/*
	 * If there is colon passed then new CSR value should be parsed as
	 * well, so allocate buffer for CSR address substring.
	 * If no colon is found, then string must have just one number with
	 * no new CSR value
	 */
	if (colon_ch != NULL) {
		csraddr_len = colon_ch - buf;
		csraddr_str =
			kmalloc(csraddr_len + 1, GFP_KERNEL);
		if (csraddr_str == NULL) {
			ret = -ENOMEM;
			goto free_buf;
		}
		/* Copy the register address to the substring buffer */
		strncpy(csraddr_str, buf, csraddr_len);
		csraddr_str[csraddr_len] = '\0';
		/* Register value must follow the colon */
		csrval_str = colon_ch + 1;
	} else /* if (str_colon == NULL) */ {
		csraddr_str = (char *)buf; /* Just to shut warning up */
		csraddr_len = strnlen(csraddr_str, count);
		csrval_str = NULL;
	}

	/* Convert CSR address to u32 value */
	ret = kstrtou32(csraddr_str, 0, &csraddr);
	if (ret != 0)
		goto free_csraddr_str;

	/* Check whether passed register address is valid */
	if (csraddr > CSR_MAX || !IS_ALIGNED(csraddr, SZ_4)) {
		ret = -EINVAL;
		goto free_csraddr_str;
	}

	/* Shift register address to the right so to have u16 address */
	pdev->csr = (csraddr >> 2);

	/* Parse new CSR value and send it to IDT, if colon has been found */
	if (colon_ch != NULL) {
		ret = kstrtou32(csrval_str, 0, &csrval);
		if (ret != 0)
			goto free_csraddr_str;

		ret = idt_csr_write(pdev, pdev->csr, csrval);
		if (ret != 0)
			goto free_csraddr_str;
	}

	/* Free memory only if colon has been found */
free_csraddr_str:
	if (colon_ch != NULL)
		kfree(csraddr_str);

	/* Free buffer allocated for data retrieved from User-space */
free_buf:
	kfree(buf);

	return (ret != 0 ? ret : count);
}

/*
 * idt_dbgfs_csr_read() - CSR debugfs-node read callback
 * @filep:	Pointer to the file system file descriptor
 * @buf:	Buffer to write data to
 * @count:	Size of the buffer
 * @offp:	Offset within the file
 *
 * It just prints the pair "0x<reg addr>:0x<value>" to passed buffer.
 */
#define CSRBUF_SIZE	((size_t)32)
static ssize_t idt_dbgfs_csr_read(struct file *filep, char __user *ubuf,
				  size_t count, loff_t *offp)
{
	struct idt_89hpesx_dev *pdev = filep->private_data;
	u32 csraddr, csrval;
	char buf[CSRBUF_SIZE];
	int ret, size;

	/* Perform CSR read operation */
	ret = idt_csr_read(pdev, pdev->csr, &csrval);
	if (ret != 0)
		return ret;

	/* Shift register address to the left so to have real address */
	csraddr = ((u32)pdev->csr << 2);

	/* Print the "0x<reg addr>:0x<value>" to buffer */
	size = snprintf(buf, CSRBUF_SIZE, "0x%05x:0x%08x\n",
		(unsigned int)csraddr, (unsigned int)csrval);

	/* Copy data to User-space */
	return simple_read_from_buffer(ubuf, count, offp, buf, size);
}

/*
 * eeprom_attribute - EEPROM sysfs-node attributes
 *
 * NOTE Size will be changed in compliance with OF node. EEPROM attribute will
 * be read-only as well if the corresponding flag is specified in OF node.
 */
static BIN_ATTR_RW(eeprom, EEPROM_DEF_SIZE);

/*
 * csr_dbgfs_ops - CSR debugfs-node read/write operations
 */
static const struct file_operations csr_dbgfs_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = idt_dbgfs_csr_write,
	.read = idt_dbgfs_csr_read
};

/*===========================================================================
 *                       Driver init/deinit methods
 *===========================================================================
 */

/*
 * idt_set_defval() - disable EEPROM access by default
 * @pdev:	Pointer to the driver data
 */
static void idt_set_defval(struct idt_89hpesx_dev *pdev)
{
	/* If OF info is missing then use next values */
	pdev->eesize = 0;
	pdev->eero = true;
	pdev->inieecmd = 0;
	pdev->eeaddr = 0;
}

static const struct i2c_device_id ee_ids[];

/*
 * idt_ee_match_id() - check whether the node belongs to compatible EEPROMs
 */
static const struct i2c_device_id *idt_ee_match_id(struct fwnode_handle *fwnode)
{
	const struct i2c_device_id *id = ee_ids;
	const char *compatible, *p;
	char devname[I2C_NAME_SIZE];
	int ret;

	ret = fwnode_property_read_string(fwnode, "compatible", &compatible);
	if (ret)
		return NULL;

	p = strchr(compatible, ',');
	strscpy(devname, p ? p + 1 : compatible, sizeof(devname));
	/* Search through the device name */
	while (id->name[0]) {
		if (strcmp(devname, id->name) == 0)
			return id;
		id++;
	}
	return NULL;
}

/*
 * idt_get_fw_data() - get IDT i2c-device parameters from device tree
 * @pdev:	Pointer to the driver data
 */
static void idt_get_fw_data(struct idt_89hpesx_dev *pdev)
{
	struct device *dev = &pdev->client->dev;
	struct fwnode_handle *fwnode;
	const struct i2c_device_id *ee_id = NULL;
	u32 eeprom_addr;
	int ret;

	device_for_each_child_node(dev, fwnode) {
		ee_id = idt_ee_match_id(fwnode);
		if (ee_id)
			break;

		dev_warn(dev, "Skip unsupported EEPROM device %pfw\n", fwnode);
	}

	/* If there is no fwnode EEPROM device, then set zero size */
	if (!ee_id) {
		dev_warn(dev, "No fwnode, EEPROM access disabled");
		idt_set_defval(pdev);
		return;
	}

	/* Retrieve EEPROM size */
	pdev->eesize = (u32)ee_id->driver_data;

	/* Get custom EEPROM address from 'reg' attribute */
	ret = fwnode_property_read_u32(fwnode, "reg", &eeprom_addr);
	if (ret || (eeprom_addr == 0)) {
		dev_warn(dev, "No EEPROM reg found, use default address 0x%x",
			 EEPROM_DEF_ADDR);
		pdev->inieecmd = 0;
		pdev->eeaddr = EEPROM_DEF_ADDR << 1;
	} else {
		pdev->inieecmd = EEPROM_USA;
		pdev->eeaddr = eeprom_addr << 1;
	}

	/* Check EEPROM 'read-only' flag */
	if (fwnode_property_read_bool(fwnode, "read-only"))
		pdev->eero = true;
	else /* if (!fwnode_property_read_bool(node, "read-only")) */
		pdev->eero = false;

	fwnode_handle_put(fwnode);
	dev_info(dev, "EEPROM of %d bytes found by 0x%x",
		pdev->eesize, pdev->eeaddr);
}

/*
 * idt_create_pdev() - create and init data structure of the driver
 * @client:	i2c client of IDT PCIe-switch device
 */
static struct idt_89hpesx_dev *idt_create_pdev(struct i2c_client *client)
{
	struct idt_89hpesx_dev *pdev;

	/* Allocate memory for driver data */
	pdev = devm_kmalloc(&client->dev, sizeof(struct idt_89hpesx_dev),
		GFP_KERNEL);
	if (pdev == NULL)
		return ERR_PTR(-ENOMEM);

	/* Initialize basic fields of the data */
	pdev->client = client;
	i2c_set_clientdata(client, pdev);

	/* Read firmware nodes information */
	idt_get_fw_data(pdev);

	/* Initialize basic CSR CMD field - use full DWORD-sized r/w ops */
	pdev->inicsrcmd = CSR_DWE;
	pdev->csr = CSR_DEF;

	/* Enable Packet Error Checking if it's supported by adapter */
	if (i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_PEC)) {
		pdev->iniccode = CCODE_PEC;
		client->flags |= I2C_CLIENT_PEC;
	} else /* PEC is unsupported */ {
		pdev->iniccode = 0;
	}

	return pdev;
}

/*
 * idt_free_pdev() - free data structure of the driver
 * @pdev:	Pointer to the driver data
 */
static void idt_free_pdev(struct idt_89hpesx_dev *pdev)
{
	/* Clear driver data from device private field */
	i2c_set_clientdata(pdev->client, NULL);
}

/*
 * idt_set_smbus_ops() - set supported SMBus operations
 * @pdev:	Pointer to the driver data
 * Return status of smbus check operations
 */
static int idt_set_smbus_ops(struct idt_89hpesx_dev *pdev)
{
	struct i2c_adapter *adapter = pdev->client->adapter;
	struct device *dev = &pdev->client->dev;

	/* Check i2c adapter read functionality */
	if (i2c_check_functionality(adapter,
				    I2C_FUNC_SMBUS_READ_BLOCK_DATA)) {
		pdev->smb_read = idt_smb_read_block;
		dev_dbg(dev, "SMBus block-read op chosen");
	} else if (i2c_check_functionality(adapter,
					   I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
		pdev->smb_read = idt_smb_read_i2c_block;
		dev_dbg(dev, "SMBus i2c-block-read op chosen");
	} else if (i2c_check_functionality(adapter,
					   I2C_FUNC_SMBUS_READ_WORD_DATA) &&
		   i2c_check_functionality(adapter,
					   I2C_FUNC_SMBUS_READ_BYTE_DATA)) {
		pdev->smb_read = idt_smb_read_word;
		dev_warn(dev, "Use slow word/byte SMBus read ops");
	} else if (i2c_check_functionality(adapter,
					   I2C_FUNC_SMBUS_READ_BYTE_DATA)) {
		pdev->smb_read = idt_smb_read_byte;
		dev_warn(dev, "Use slow byte SMBus read op");
	} else /* no supported smbus read operations */ {
		dev_err(dev, "No supported SMBus read op");
		return -EPFNOSUPPORT;
	}

	/* Check i2c adapter write functionality */
	if (i2c_check_functionality(adapter,
				    I2C_FUNC_SMBUS_WRITE_BLOCK_DATA)) {
		pdev->smb_write = idt_smb_write_block;
		dev_dbg(dev, "SMBus block-write op chosen");
	} else if (i2c_check_functionality(adapter,
					   I2C_FUNC_SMBUS_WRITE_I2C_BLOCK)) {
		pdev->smb_write = idt_smb_write_i2c_block;
		dev_dbg(dev, "SMBus i2c-block-write op chosen");
	} else if (i2c_check_functionality(adapter,
					   I2C_FUNC_SMBUS_WRITE_WORD_DATA) &&
		   i2c_check_functionality(adapter,
					   I2C_FUNC_SMBUS_WRITE_BYTE_DATA)) {
		pdev->smb_write = idt_smb_write_word;
		dev_warn(dev, "Use slow word/byte SMBus write op");
	} else if (i2c_check_functionality(adapter,
					   I2C_FUNC_SMBUS_WRITE_BYTE_DATA)) {
		pdev->smb_write = idt_smb_write_byte;
		dev_warn(dev, "Use slow byte SMBus write op");
	} else /* no supported smbus write operations */ {
		dev_err(dev, "No supported SMBus write op");
		return -EPFNOSUPPORT;
	}

	/* Initialize IDT SMBus slave interface mutex */
	mutex_init(&pdev->smb_mtx);

	return 0;
}

/*
 * idt_check_dev() - check whether it's really IDT 89HPESx device
 * @pdev:	Pointer to the driver data
 * Return status of i2c adapter check operation
 */
static int idt_check_dev(struct idt_89hpesx_dev *pdev)
{
	struct device *dev = &pdev->client->dev;
	u32 viddid;
	int ret;

	/* Read VID and DID directly from IDT memory space */
	ret = idt_csr_read(pdev, IDT_VIDDID_CSR, &viddid);
	if (ret != 0) {
		dev_err(dev, "Failed to read VID/DID");
		return ret;
	}

	/* Check whether it's IDT device */
	if ((viddid & IDT_VID_MASK) != PCI_VENDOR_ID_IDT) {
		dev_err(dev, "Got unsupported VID/DID: 0x%08x", viddid);
		return -ENODEV;
	}

	dev_info(dev, "Found IDT 89HPES device VID:0x%04x, DID:0x%04x",
		(viddid & IDT_VID_MASK), (viddid >> 16));

	return 0;
}

/*
 * idt_create_sysfs_files() - create sysfs attribute files
 * @pdev:	Pointer to the driver data
 * Return status of operation
 */
static int idt_create_sysfs_files(struct idt_89hpesx_dev *pdev)
{
	struct device *dev = &pdev->client->dev;
	int ret;

	/* Don't do anything if EEPROM isn't accessible */
	if (pdev->eesize == 0) {
		dev_dbg(dev, "Skip creating sysfs-files");
		return 0;
	}

	/* Allocate memory for attribute file */
	pdev->ee_file = devm_kmalloc(dev, sizeof(*pdev->ee_file), GFP_KERNEL);
	if (!pdev->ee_file)
		return -ENOMEM;

	/* Copy the declared EEPROM attr structure to change some of fields */
	memcpy(pdev->ee_file, &bin_attr_eeprom, sizeof(*pdev->ee_file));

	/* In case of read-only EEPROM get rid of write ability */
	if (pdev->eero) {
		pdev->ee_file->attr.mode &= ~0200;
		pdev->ee_file->write = NULL;
	}
	/* Create EEPROM sysfs file */
	pdev->ee_file->size = pdev->eesize;
	ret = sysfs_create_bin_file(&dev->kobj, pdev->ee_file);
	if (ret != 0) {
		dev_err(dev, "Failed to create EEPROM sysfs-node");
		return ret;
	}

	return 0;
}

/*
 * idt_remove_sysfs_files() - remove sysfs attribute files
 * @pdev:	Pointer to the driver data
 */
static void idt_remove_sysfs_files(struct idt_89hpesx_dev *pdev)
{
	struct device *dev = &pdev->client->dev;

	/* Don't do anything if EEPROM wasn't accessible */
	if (pdev->eesize == 0)
		return;

	/* Remove EEPROM sysfs file */
	sysfs_remove_bin_file(&dev->kobj, pdev->ee_file);
}

/*
 * idt_create_dbgfs_files() - create debugfs files
 * @pdev:	Pointer to the driver data
 */
#define CSRNAME_LEN	((size_t)32)
static void idt_create_dbgfs_files(struct idt_89hpesx_dev *pdev)
{
	struct i2c_client *cli = pdev->client;
	char fname[CSRNAME_LEN];

	/* Create Debugfs directory for CSR file */
	snprintf(fname, CSRNAME_LEN, "%d-%04hx", cli->adapter->nr, cli->addr);
	pdev->csr_dir = debugfs_create_dir(fname, csr_dbgdir);

	/* Create Debugfs file for CSR read/write operations */
	debugfs_create_file(cli->name, 0600, pdev->csr_dir, pdev,
			    &csr_dbgfs_ops);
}

/*
 * idt_remove_dbgfs_files() - remove debugfs files
 * @pdev:	Pointer to the driver data
 */
static void idt_remove_dbgfs_files(struct idt_89hpesx_dev *pdev)
{
	/* Remove CSR directory and it sysfs-node */
	debugfs_remove_recursive(pdev->csr_dir);
}

/*
 * idt_probe() - IDT 89HPESx driver probe() callback method
 */
static int idt_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct idt_89hpesx_dev *pdev;
	int ret;

	/* Create driver data */
	pdev = idt_create_pdev(client);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	/* Set SMBus operations */
	ret = idt_set_smbus_ops(pdev);
	if (ret != 0)
		goto err_free_pdev;

	/* Check whether it is truly IDT 89HPESx device */
	ret = idt_check_dev(pdev);
	if (ret != 0)
		goto err_free_pdev;

	/* Create sysfs files */
	ret = idt_create_sysfs_files(pdev);
	if (ret != 0)
		goto err_free_pdev;

	/* Create debugfs files */
	idt_create_dbgfs_files(pdev);

	return 0;

err_free_pdev:
	idt_free_pdev(pdev);

	return ret;
}

/*
 * idt_remove() - IDT 89HPESx driver remove() callback method
 */
static void idt_remove(struct i2c_client *client)
{
	struct idt_89hpesx_dev *pdev = i2c_get_clientdata(client);

	/* Remove debugfs files first */
	idt_remove_dbgfs_files(pdev);

	/* Remove sysfs files */
	idt_remove_sysfs_files(pdev);

	/* Discard driver data structure */
	idt_free_pdev(pdev);
}

/*
 * ee_ids - array of supported EEPROMs
 */
static const struct i2c_device_id ee_ids[] = {
	{ "24c32",  4096},
	{ "24c64",  8192},
	{ "24c128", 16384},
	{ "24c256", 32768},
	{ "24c512", 65536},
	{}
};
MODULE_DEVICE_TABLE(i2c, ee_ids);

/*
 * idt_ids - supported IDT 89HPESx devices
 */
static const struct i2c_device_id idt_ids[] = {
	{ "89hpes8nt2", 0 },
	{ "89hpes12nt3", 0 },

	{ "89hpes24nt6ag2", 0 },
	{ "89hpes32nt8ag2", 0 },
	{ "89hpes32nt8bg2", 0 },
	{ "89hpes12nt12g2", 0 },
	{ "89hpes16nt16g2", 0 },
	{ "89hpes24nt24g2", 0 },
	{ "89hpes32nt24ag2", 0 },
	{ "89hpes32nt24bg2", 0 },

	{ "89hpes12n3", 0 },
	{ "89hpes12n3a", 0 },
	{ "89hpes24n3", 0 },
	{ "89hpes24n3a", 0 },

	{ "89hpes32h8", 0 },
	{ "89hpes32h8g2", 0 },
	{ "89hpes48h12", 0 },
	{ "89hpes48h12g2", 0 },
	{ "89hpes48h12ag2", 0 },
	{ "89hpes16h16", 0 },
	{ "89hpes22h16", 0 },
	{ "89hpes22h16g2", 0 },
	{ "89hpes34h16", 0 },
	{ "89hpes34h16g2", 0 },
	{ "89hpes64h16", 0 },
	{ "89hpes64h16g2", 0 },
	{ "89hpes64h16ag2", 0 },

	/* { "89hpes3t3", 0 }, // No SMBus-slave iface */
	{ "89hpes12t3g2", 0 },
	{ "89hpes24t3g2", 0 },
	/* { "89hpes4t4", 0 }, // No SMBus-slave iface */
	{ "89hpes16t4", 0 },
	{ "89hpes4t4g2", 0 },
	{ "89hpes10t4g2", 0 },
	{ "89hpes16t4g2", 0 },
	{ "89hpes16t4ag2", 0 },
	{ "89hpes5t5", 0 },
	{ "89hpes6t5", 0 },
	{ "89hpes8t5", 0 },
	{ "89hpes8t5a", 0 },
	{ "89hpes24t6", 0 },
	{ "89hpes6t6g2", 0 },
	{ "89hpes24t6g2", 0 },
	{ "89hpes16t7", 0 },
	{ "89hpes32t8", 0 },
	{ "89hpes32t8g2", 0 },
	{ "89hpes48t12", 0 },
	{ "89hpes48t12g2", 0 },
	{ /* END OF LIST */ }
};
MODULE_DEVICE_TABLE(i2c, idt_ids);

static const struct of_device_id idt_of_match[] = {
	{ .compatible = "idt,89hpes8nt2", },
	{ .compatible = "idt,89hpes12nt3", },

	{ .compatible = "idt,89hpes24nt6ag2", },
	{ .compatible = "idt,89hpes32nt8ag2", },
	{ .compatible = "idt,89hpes32nt8bg2", },
	{ .compatible = "idt,89hpes12nt12g2", },
	{ .compatible = "idt,89hpes16nt16g2", },
	{ .compatible = "idt,89hpes24nt24g2", },
	{ .compatible = "idt,89hpes32nt24ag2", },
	{ .compatible = "idt,89hpes32nt24bg2", },

	{ .compatible = "idt,89hpes12n3", },
	{ .compatible = "idt,89hpes12n3a", },
	{ .compatible = "idt,89hpes24n3", },
	{ .compatible = "idt,89hpes24n3a", },

	{ .compatible = "idt,89hpes32h8", },
	{ .compatible = "idt,89hpes32h8g2", },
	{ .compatible = "idt,89hpes48h12", },
	{ .compatible = "idt,89hpes48h12g2", },
	{ .compatible = "idt,89hpes48h12ag2", },
	{ .compatible = "idt,89hpes16h16", },
	{ .compatible = "idt,89hpes22h16", },
	{ .compatible = "idt,89hpes22h16g2", },
	{ .compatible = "idt,89hpes34h16", },
	{ .compatible = "idt,89hpes34h16g2", },
	{ .compatible = "idt,89hpes64h16", },
	{ .compatible = "idt,89hpes64h16g2", },
	{ .compatible = "idt,89hpes64h16ag2", },

	{ .compatible = "idt,89hpes12t3g2", },
	{ .compatible = "idt,89hpes24t3g2", },

	{ .compatible = "idt,89hpes16t4", },
	{ .compatible = "idt,89hpes4t4g2", },
	{ .compatible = "idt,89hpes10t4g2", },
	{ .compatible = "idt,89hpes16t4g2", },
	{ .compatible = "idt,89hpes16t4ag2", },
	{ .compatible = "idt,89hpes5t5", },
	{ .compatible = "idt,89hpes6t5", },
	{ .compatible = "idt,89hpes8t5", },
	{ .compatible = "idt,89hpes8t5a", },
	{ .compatible = "idt,89hpes24t6", },
	{ .compatible = "idt,89hpes6t6g2", },
	{ .compatible = "idt,89hpes24t6g2", },
	{ .compatible = "idt,89hpes16t7", },
	{ .compatible = "idt,89hpes32t8", },
	{ .compatible = "idt,89hpes32t8g2", },
	{ .compatible = "idt,89hpes48t12", },
	{ .compatible = "idt,89hpes48t12g2", },
	{ },
};
MODULE_DEVICE_TABLE(of, idt_of_match);

/*
 * idt_driver - IDT 89HPESx driver structure
 */
static struct i2c_driver idt_driver = {
	.driver = {
		.name = IDT_NAME,
		.of_match_table = idt_of_match,
	},
	.probe = idt_probe,
	.remove = idt_remove,
	.id_table = idt_ids,
};

/*
 * idt_init() - IDT 89HPESx driver init() callback method
 */
static int __init idt_init(void)
{
	int ret;

	/* Create Debugfs directory first */
	if (debugfs_initialized())
		csr_dbgdir = debugfs_create_dir("idt_csr", NULL);

	/* Add new i2c-device driver */
	ret = i2c_add_driver(&idt_driver);
	if (ret) {
		debugfs_remove_recursive(csr_dbgdir);
		return ret;
	}

	return 0;
}
module_init(idt_init);

/*
 * idt_exit() - IDT 89HPESx driver exit() callback method
 */
static void __exit idt_exit(void)
{
	/* Discard debugfs directory and all files if any */
	debugfs_remove_recursive(csr_dbgdir);

	/* Unregister i2c-device driver */
	i2c_del_driver(&idt_driver);
}
module_exit(idt_exit);
