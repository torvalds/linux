/*
 * linux/arch/mips/tx4938/toshiba_rbtx4938/spi_eeprom.c
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <asm/tx4938/spi.h>
#include <asm/tx4938/tx4938.h>

/* ATMEL 250x0 instructions */
#define	ATMEL_WREN	0x06
#define	ATMEL_WRDI	0x04
#define ATMEL_RDSR	0x05
#define ATMEL_WRSR	0x01
#define	ATMEL_READ	0x03
#define	ATMEL_WRITE	0x02

#define ATMEL_SR_BSY	0x01
#define ATMEL_SR_WEN	0x02
#define ATMEL_SR_BP0	0x04
#define ATMEL_SR_BP1	0x08

DEFINE_SPINLOCK(spi_eeprom_lock);

static struct spi_dev_desc seeprom_dev_desc = {
	.baud 		= 1500000,	/* 1.5Mbps */
	.tcss		= 1,
	.tcsh		= 1,
	.tcsr		= 1,
	.byteorder	= 1,		/* MSB-First */
	.polarity	= 0,		/* High-Active */
	.phase		= 0,		/* Sample-Then-Shift */

};
static inline int
spi_eeprom_io(int chipid,
	      unsigned char **inbufs, unsigned int *incounts,
	      unsigned char **outbufs, unsigned int *outcounts)
{
	return txx9_spi_io(chipid, &seeprom_dev_desc,
			   inbufs, incounts, outbufs, outcounts, 0);
}

int spi_eeprom_write_enable(int chipid, int enable)
{
	unsigned char inbuf[1];
	unsigned char *inbufs[1];
	unsigned int incounts[2];
	unsigned long flags;
	int stat;
	inbuf[0] = enable ? ATMEL_WREN : ATMEL_WRDI;
	inbufs[0] = inbuf;
	incounts[0] = sizeof(inbuf);
	incounts[1] = 0;
	spin_lock_irqsave(&spi_eeprom_lock, flags);
	stat = spi_eeprom_io(chipid, inbufs, incounts, NULL, NULL);
	spin_unlock_irqrestore(&spi_eeprom_lock, flags);
	return stat;
}

static int spi_eeprom_read_status_nolock(int chipid)
{
	unsigned char inbuf[2], outbuf[2];
	unsigned char *inbufs[1], *outbufs[1];
	unsigned int incounts[2], outcounts[2];
	int stat;
	inbuf[0] = ATMEL_RDSR;
	inbuf[1] = 0;
	inbufs[0] = inbuf;
	incounts[0] = sizeof(inbuf);
	incounts[1] = 0;
	outbufs[0] = outbuf;
	outcounts[0] = sizeof(outbuf);
	outcounts[1] = 0;
	stat = spi_eeprom_io(chipid, inbufs, incounts, outbufs, outcounts);
	if (stat < 0)
		return stat;
	return outbuf[1];
}

int spi_eeprom_read_status(int chipid)
{
	unsigned long flags;
	int stat;
	spin_lock_irqsave(&spi_eeprom_lock, flags);
	stat = spi_eeprom_read_status_nolock(chipid);
	spin_unlock_irqrestore(&spi_eeprom_lock, flags);
	return stat;
}

int spi_eeprom_read(int chipid, int address, unsigned char *buf, int len)
{
	unsigned char inbuf[2];
	unsigned char *inbufs[2], *outbufs[2];
	unsigned int incounts[2], outcounts[3];
	unsigned long flags;
	int stat;
	inbuf[0] = ATMEL_READ;
	inbuf[1] = address;
	inbufs[0] = inbuf;
	inbufs[1] = NULL;
	incounts[0] = sizeof(inbuf);
	incounts[1] = 0;
	outbufs[0] = NULL;
	outbufs[1] = buf;
	outcounts[0] = 2;
	outcounts[1] = len;
	outcounts[2] = 0;
	spin_lock_irqsave(&spi_eeprom_lock, flags);
	stat = spi_eeprom_io(chipid, inbufs, incounts, outbufs, outcounts);
	spin_unlock_irqrestore(&spi_eeprom_lock, flags);
	return stat;
}

int spi_eeprom_write(int chipid, int address, unsigned char *buf, int len)
{
	unsigned char inbuf[2];
	unsigned char *inbufs[2];
	unsigned int incounts[3];
	unsigned long flags;
	int i, stat;

	if (address / 8 != (address + len - 1) / 8)
		return -EINVAL;
	stat = spi_eeprom_write_enable(chipid, 1);
	if (stat < 0)
		return stat;
	stat = spi_eeprom_read_status(chipid);
	if (stat < 0)
		return stat;
	if (!(stat & ATMEL_SR_WEN))
		return -EPERM;

	inbuf[0] = ATMEL_WRITE;
	inbuf[1] = address;
	inbufs[0] = inbuf;
	inbufs[1] = buf;
	incounts[0] = sizeof(inbuf);
	incounts[1] = len;
	incounts[2] = 0;
	spin_lock_irqsave(&spi_eeprom_lock, flags);
	stat = spi_eeprom_io(chipid, inbufs, incounts, NULL, NULL);
	if (stat < 0)
		goto unlock_return;

	/* write start.  max 10ms */
	for (i = 10; i > 0; i--) {
		int stat = spi_eeprom_read_status_nolock(chipid);
		if (stat < 0)
			goto unlock_return;
		if (!(stat & ATMEL_SR_BSY))
			break;
		mdelay(1);
	}
	spin_unlock_irqrestore(&spi_eeprom_lock, flags);
	if (i == 0)
		return -EIO;
	return len;
 unlock_return:
	spin_unlock_irqrestore(&spi_eeprom_lock, flags);
	return stat;
}

#ifdef CONFIG_PROC_FS
#define MAX_SIZE	0x80	/* for ATMEL 25010 */
static int spi_eeprom_read_proc(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	unsigned int size = MAX_SIZE;
	if (spi_eeprom_read((int)data, 0, (unsigned char *)page, size) < 0)
		size = 0;
	return size;
}

static int spi_eeprom_write_proc(struct file *file, const char *buffer,
				 unsigned long count, void *data)
{
	unsigned int size = MAX_SIZE;
	int i;
	if (file->f_pos >= size)
		return -EIO;
	if (file->f_pos + count > size)
		count = size - file->f_pos;
	for (i = 0; i < count; i += 8) {
		int len = count - i < 8 ? count - i : 8;
		if (spi_eeprom_write((int)data, file->f_pos,
				     (unsigned char *)buffer, len) < 0) {
			count = -EIO;
			break;
		}
		buffer += len;
		file->f_pos += len;
	}
	return count;
}

__init void spi_eeprom_proc_create(struct proc_dir_entry *dir, int chipid)
{
	struct proc_dir_entry *entry;
	char name[128];
	sprintf(name, "seeprom-%d", chipid);
	entry = create_proc_entry(name, 0600, dir);
	if (entry) {
		entry->read_proc = spi_eeprom_read_proc;
		entry->write_proc = spi_eeprom_write_proc;
		entry->data = (void *)chipid;
	}
}
#endif /* CONFIG_PROC_FS */
