/*
 * av7110_hw.c: av7110 low level hardware access and firmware interface
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * originally based on code by:
 * Copyright (C) 1998,1999 Christian Theiss <mistert@rz.fh-augsburg.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 * the project's page is at http://www.linuxtv.org/
 */

/* for debugging ARM communication: */
//#define COM_DEBUG

#include <stdarg.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/fs.h>

#include "av7110.h"
#include "av7110_hw.h"

#define _NOHANDSHAKE

/*
 * Max transfer size done by av7110_fw_cmd()
 *
 * The maximum size passed to this function is 6 bytes. The buffer also
 * uses two additional ones for type and size. So, 8 bytes is enough.
 */
#define MAX_XFER_SIZE  8

/****************************************************************************
 * DEBI functions
 ****************************************************************************/

/* This DEBI code is based on the Stradis driver
   by Nathan Laredo <laredo@gnu.org> */

int av7110_debiwrite(struct av7110 *av7110, u32 config,
		     int addr, u32 val, unsigned int count)
{
	struct saa7146_dev *dev = av7110->dev;

	if (count > 32764) {
		printk("%s: invalid count %d\n", __func__, count);
		return -1;
	}
	if (saa7146_wait_for_debi_done(av7110->dev, 0) < 0) {
		printk("%s: wait_for_debi_done failed\n", __func__);
		return -1;
	}
	saa7146_write(dev, DEBI_CONFIG, config);
	if (count <= 4)		/* immediate transfer */
		saa7146_write(dev, DEBI_AD, val);
	else			/* block transfer */
		saa7146_write(dev, DEBI_AD, av7110->debi_bus);
	saa7146_write(dev, DEBI_COMMAND, (count << 17) | (addr & 0xffff));
	saa7146_write(dev, MC2, (2 << 16) | 2);
	return 0;
}

u32 av7110_debiread(struct av7110 *av7110, u32 config, int addr, unsigned int count)
{
	struct saa7146_dev *dev = av7110->dev;
	u32 result = 0;

	if (count > 32764) {
		printk("%s: invalid count %d\n", __func__, count);
		return 0;
	}
	if (saa7146_wait_for_debi_done(av7110->dev, 0) < 0) {
		printk("%s: wait_for_debi_done #1 failed\n", __func__);
		return 0;
	}
	saa7146_write(dev, DEBI_AD, av7110->debi_bus);
	saa7146_write(dev, DEBI_COMMAND, (count << 17) | 0x10000 | (addr & 0xffff));

	saa7146_write(dev, DEBI_CONFIG, config);
	saa7146_write(dev, MC2, (2 << 16) | 2);
	if (count > 4)
		return count;
	if (saa7146_wait_for_debi_done(av7110->dev, 0) < 0) {
		printk("%s: wait_for_debi_done #2 failed\n", __func__);
		return 0;
	}

	result = saa7146_read(dev, DEBI_AD);
	result &= (0xffffffffUL >> ((4 - count) * 8));
	return result;
}



/* av7110 ARM core boot stuff */
#if 0
void av7110_reset_arm(struct av7110 *av7110)
{
	saa7146_setgpio(av7110->dev, RESET_LINE, SAA7146_GPIO_OUTLO);

	/* Disable DEBI and GPIO irq */
	SAA7146_IER_DISABLE(av7110->dev, MASK_19 | MASK_03);
	SAA7146_ISR_CLEAR(av7110->dev, MASK_19 | MASK_03);

	saa7146_setgpio(av7110->dev, RESET_LINE, SAA7146_GPIO_OUTHI);
	msleep(30);	/* the firmware needs some time to initialize */

	ARM_ResetMailBox(av7110);

	SAA7146_ISR_CLEAR(av7110->dev, MASK_19 | MASK_03);
	SAA7146_IER_ENABLE(av7110->dev, MASK_03);

	av7110->arm_ready = 1;
	dprintk(1, "reset ARM\n");
}
#endif  /*  0  */

static int waitdebi(struct av7110 *av7110, int adr, int state)
{
	int k;

	dprintk(4, "%p\n", av7110);

	for (k = 0; k < 100; k++) {
		if (irdebi(av7110, DEBINOSWAP, adr, 0, 2) == state)
			return 0;
		udelay(5);
	}
	return -ETIMEDOUT;
}

static int load_dram(struct av7110 *av7110, u32 *data, int len)
{
	int i;
	int blocks, rest;
	u32 base, bootblock = AV7110_BOOT_BLOCK;

	dprintk(4, "%p\n", av7110);

	blocks = len / AV7110_BOOT_MAX_SIZE;
	rest = len % AV7110_BOOT_MAX_SIZE;
	base = DRAM_START_CODE;

	for (i = 0; i < blocks; i++) {
		if (waitdebi(av7110, AV7110_BOOT_STATE, BOOTSTATE_BUFFER_EMPTY) < 0) {
			printk(KERN_ERR "dvb-ttpci: load_dram(): timeout at block %d\n", i);
			return -ETIMEDOUT;
		}
		dprintk(4, "writing DRAM block %d\n", i);
		mwdebi(av7110, DEBISWAB, bootblock,
		       ((u8 *)data) + i * AV7110_BOOT_MAX_SIZE, AV7110_BOOT_MAX_SIZE);
		bootblock ^= 0x1400;
		iwdebi(av7110, DEBISWAB, AV7110_BOOT_BASE, swab32(base), 4);
		iwdebi(av7110, DEBINOSWAP, AV7110_BOOT_SIZE, AV7110_BOOT_MAX_SIZE, 2);
		iwdebi(av7110, DEBINOSWAP, AV7110_BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
		base += AV7110_BOOT_MAX_SIZE;
	}

	if (rest > 0) {
		if (waitdebi(av7110, AV7110_BOOT_STATE, BOOTSTATE_BUFFER_EMPTY) < 0) {
			printk(KERN_ERR "dvb-ttpci: load_dram(): timeout at last block\n");
			return -ETIMEDOUT;
		}
		if (rest > 4)
			mwdebi(av7110, DEBISWAB, bootblock,
			       ((u8 *)data) + i * AV7110_BOOT_MAX_SIZE, rest);
		else
			mwdebi(av7110, DEBISWAB, bootblock,
			       ((u8 *)data) + i * AV7110_BOOT_MAX_SIZE - 4, rest + 4);

		iwdebi(av7110, DEBISWAB, AV7110_BOOT_BASE, swab32(base), 4);
		iwdebi(av7110, DEBINOSWAP, AV7110_BOOT_SIZE, rest, 2);
		iwdebi(av7110, DEBINOSWAP, AV7110_BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
	}
	if (waitdebi(av7110, AV7110_BOOT_STATE, BOOTSTATE_BUFFER_EMPTY) < 0) {
		printk(KERN_ERR "dvb-ttpci: load_dram(): timeout after last block\n");
		return -ETIMEDOUT;
	}
	iwdebi(av7110, DEBINOSWAP, AV7110_BOOT_SIZE, 0, 2);
	iwdebi(av7110, DEBINOSWAP, AV7110_BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);
	if (waitdebi(av7110, AV7110_BOOT_STATE, BOOTSTATE_AV7110_BOOT_COMPLETE) < 0) {
		printk(KERN_ERR "dvb-ttpci: load_dram(): final handshake timeout\n");
		return -ETIMEDOUT;
	}
	return 0;
}


/* we cannot write av7110 DRAM directly, so load a bootloader into
 * the DPRAM which implements a simple boot protocol */
int av7110_bootarm(struct av7110 *av7110)
{
	const struct firmware *fw;
	const char *fw_name = "av7110/bootcode.bin";
	struct saa7146_dev *dev = av7110->dev;
	u32 ret;
	int i;

	dprintk(4, "%p\n", av7110);

	av7110->arm_ready = 0;

	saa7146_setgpio(dev, RESET_LINE, SAA7146_GPIO_OUTLO);

	/* Disable DEBI and GPIO irq */
	SAA7146_IER_DISABLE(av7110->dev, MASK_03 | MASK_19);
	SAA7146_ISR_CLEAR(av7110->dev, MASK_19 | MASK_03);

	/* enable DEBI */
	saa7146_write(av7110->dev, MC1, 0x08800880);
	saa7146_write(av7110->dev, DD1_STREAM_B, 0x00000000);
	saa7146_write(av7110->dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

	/* test DEBI */
	iwdebi(av7110, DEBISWAP, DPRAM_BASE, 0x76543210, 4);
	/* FIXME: Why does Nexus CA require 2x iwdebi for first init? */
	iwdebi(av7110, DEBISWAP, DPRAM_BASE, 0x76543210, 4);

	if ((ret=irdebi(av7110, DEBINOSWAP, DPRAM_BASE, 0, 4)) != 0x10325476) {
		printk(KERN_ERR "dvb-ttpci: debi test in av7110_bootarm() failed: "
		       "%08x != %08x (check your BIOS 'Plug&Play OS' settings)\n",
		       ret, 0x10325476);
		return -1;
	}
	for (i = 0; i < 8192; i += 4)
		iwdebi(av7110, DEBISWAP, DPRAM_BASE + i, 0x00, 4);
	dprintk(2, "debi test OK\n");

	/* boot */
	dprintk(1, "load boot code\n");
	saa7146_setgpio(dev, ARM_IRQ_LINE, SAA7146_GPIO_IRQLO);
	//saa7146_setgpio(dev, DEBI_DONE_LINE, SAA7146_GPIO_INPUT);
	//saa7146_setgpio(dev, 3, SAA7146_GPIO_INPUT);

	ret = request_firmware(&fw, fw_name, &dev->pci->dev);
	if (ret) {
		printk(KERN_ERR "dvb-ttpci: Failed to load firmware \"%s\"\n",
			fw_name);
		return ret;
	}

	mwdebi(av7110, DEBISWAB, DPRAM_BASE, fw->data, fw->size);
	release_firmware(fw);
	iwdebi(av7110, DEBINOSWAP, AV7110_BOOT_STATE, BOOTSTATE_BUFFER_FULL, 2);

	if (saa7146_wait_for_debi_done(av7110->dev, 1)) {
		printk(KERN_ERR "dvb-ttpci: av7110_bootarm(): "
		       "saa7146_wait_for_debi_done() timed out\n");
		return -ETIMEDOUT;
	}
	saa7146_setgpio(dev, RESET_LINE, SAA7146_GPIO_OUTHI);
	mdelay(1);

	dprintk(1, "load dram code\n");
	if (load_dram(av7110, (u32 *)av7110->bin_root, av7110->size_root) < 0) {
		printk(KERN_ERR "dvb-ttpci: av7110_bootarm(): "
		       "load_dram() failed\n");
		return -1;
	}

	saa7146_setgpio(dev, RESET_LINE, SAA7146_GPIO_OUTLO);
	mdelay(1);

	dprintk(1, "load dpram code\n");
	mwdebi(av7110, DEBISWAB, DPRAM_BASE, av7110->bin_dpram, av7110->size_dpram);

	if (saa7146_wait_for_debi_done(av7110->dev, 1)) {
		printk(KERN_ERR "dvb-ttpci: av7110_bootarm(): "
		       "saa7146_wait_for_debi_done() timed out after loading DRAM\n");
		return -ETIMEDOUT;
	}
	saa7146_setgpio(dev, RESET_LINE, SAA7146_GPIO_OUTHI);
	msleep(30);	/* the firmware needs some time to initialize */

	//ARM_ClearIrq(av7110);
	ARM_ResetMailBox(av7110);
	SAA7146_ISR_CLEAR(av7110->dev, MASK_19 | MASK_03);
	SAA7146_IER_ENABLE(av7110->dev, MASK_03);

	av7110->arm_errors = 0;
	av7110->arm_ready = 1;
	return 0;
}
MODULE_FIRMWARE("av7110/bootcode.bin");

/****************************************************************************
 * DEBI command polling
 ****************************************************************************/

int av7110_wait_msgstate(struct av7110 *av7110, u16 flags)
{
	unsigned long start;
	u32 stat;
	int err;

	if (FW_VERSION(av7110->arm_app) <= 0x261c) {
		/* not supported by old firmware */
		msleep(50);
		return 0;
	}

	/* new firmware */
	start = jiffies;
	for (;;) {
		err = time_after(jiffies, start + ARM_WAIT_FREE);
		if (mutex_lock_interruptible(&av7110->dcomlock))
			return -ERESTARTSYS;
		stat = rdebi(av7110, DEBINOSWAP, MSGSTATE, 0, 2);
		mutex_unlock(&av7110->dcomlock);
		if ((stat & flags) == 0)
			break;
		if (err) {
			printk(KERN_ERR "%s: timeout waiting for MSGSTATE %04x\n",
				__func__, stat & flags);
			return -ETIMEDOUT;
		}
		msleep(1);
	}
	return 0;
}

static int __av7110_send_fw_cmd(struct av7110 *av7110, u16* buf, int length)
{
	int i;
	unsigned long start;
	char *type = NULL;
	u16 flags[2] = {0, 0};
	u32 stat;
	int err;

//	dprintk(4, "%p\n", av7110);

	if (!av7110->arm_ready) {
		dprintk(1, "arm not ready.\n");
		return -ENXIO;
	}

	start = jiffies;
	while (1) {
		err = time_after(jiffies, start + ARM_WAIT_FREE);
		if (rdebi(av7110, DEBINOSWAP, COMMAND, 0, 2) == 0)
			break;
		if (err) {
			printk(KERN_ERR "dvb-ttpci: %s(): timeout waiting for COMMAND idle\n", __func__);
			av7110->arm_errors++;
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	if (FW_VERSION(av7110->arm_app) <= 0x261f)
		wdebi(av7110, DEBINOSWAP, COM_IF_LOCK, 0xffff, 2);

#ifndef _NOHANDSHAKE
	start = jiffies;
	while (1) {
		err = time_after(jiffies, start + ARM_WAIT_SHAKE);
		if (rdebi(av7110, DEBINOSWAP, HANDSHAKE_REG, 0, 2) == 0)
			break;
		if (err) {
			printk(KERN_ERR "dvb-ttpci: %s(): timeout waiting for HANDSHAKE_REG\n", __func__);
			return -ETIMEDOUT;
		}
		msleep(1);
	}
#endif

	switch ((buf[0] >> 8) & 0xff) {
	case COMTYPE_PIDFILTER:
	case COMTYPE_ENCODER:
	case COMTYPE_REC_PLAY:
	case COMTYPE_MPEGDECODER:
		type = "MSG";
		flags[0] = GPMQOver;
		flags[1] = GPMQFull;
		break;
	case COMTYPE_OSD:
		type = "OSD";
		flags[0] = OSDQOver;
		flags[1] = OSDQFull;
		break;
	case COMTYPE_MISC:
		if (FW_VERSION(av7110->arm_app) >= 0x261d) {
			type = "MSG";
			flags[0] = GPMQOver;
			flags[1] = GPMQBusy;
		}
		break;
	default:
		break;
	}

	if (type != NULL) {
		/* non-immediate COMMAND type */
		start = jiffies;
		for (;;) {
			err = time_after(jiffies, start + ARM_WAIT_FREE);
			stat = rdebi(av7110, DEBINOSWAP, MSGSTATE, 0, 2);
			if (stat & flags[0]) {
				printk(KERN_ERR "%s: %s QUEUE overflow\n",
					__func__, type);
				return -1;
			}
			if ((stat & flags[1]) == 0)
				break;
			if (err) {
				printk(KERN_ERR "%s: timeout waiting on busy %s QUEUE\n",
					__func__, type);
				av7110->arm_errors++;
				return -ETIMEDOUT;
			}
			msleep(1);
		}
	}

	for (i = 2; i < length; i++)
		wdebi(av7110, DEBINOSWAP, COMMAND + 2 * i, (u32) buf[i], 2);

	if (length)
		wdebi(av7110, DEBINOSWAP, COMMAND + 2, (u32) buf[1], 2);
	else
		wdebi(av7110, DEBINOSWAP, COMMAND + 2, 0, 2);

	wdebi(av7110, DEBINOSWAP, COMMAND, (u32) buf[0], 2);

	if (FW_VERSION(av7110->arm_app) <= 0x261f)
		wdebi(av7110, DEBINOSWAP, COM_IF_LOCK, 0x0000, 2);

#ifdef COM_DEBUG
	start = jiffies;
	while (1) {
		err = time_after(jiffies, start + ARM_WAIT_FREE);
		if (rdebi(av7110, DEBINOSWAP, COMMAND, 0, 2) == 0)
			break;
		if (err) {
			printk(KERN_ERR "dvb-ttpci: %s(): timeout waiting for COMMAND %d to complete\n",
			       __func__, (buf[0] >> 8) & 0xff);
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	stat = rdebi(av7110, DEBINOSWAP, MSGSTATE, 0, 2);
	if (stat & GPMQOver) {
		printk(KERN_ERR "dvb-ttpci: %s(): GPMQOver\n", __func__);
		return -ENOSPC;
	}
	else if (stat & OSDQOver) {
		printk(KERN_ERR "dvb-ttpci: %s(): OSDQOver\n", __func__);
		return -ENOSPC;
	}
#endif

	return 0;
}

static int av7110_send_fw_cmd(struct av7110 *av7110, u16* buf, int length)
{
	int ret;

//	dprintk(4, "%p\n", av7110);

	if (!av7110->arm_ready) {
		dprintk(1, "arm not ready.\n");
		return -1;
	}
	if (mutex_lock_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;

	ret = __av7110_send_fw_cmd(av7110, buf, length);
	mutex_unlock(&av7110->dcomlock);
	if (ret && ret!=-ERESTARTSYS)
		printk(KERN_ERR "dvb-ttpci: %s(): av7110_send_fw_cmd error %d\n",
		       __func__, ret);
	return ret;
}

int av7110_fw_cmd(struct av7110 *av7110, int type, int com, int num, ...)
{
	va_list args;
	u16 buf[MAX_XFER_SIZE];
	int i, ret;

//	dprintk(4, "%p\n", av7110);

	if (2 + num > ARRAY_SIZE(buf)) {
		printk(KERN_WARNING
		       "%s: %s len=%d is too big!\n",
		       KBUILD_MODNAME, __func__, num);
		return -EINVAL;
	}

	buf[0] = ((type << 8) | com);
	buf[1] = num;

	if (num) {
		va_start(args, num);
		for (i = 0; i < num; i++)
			buf[i + 2] = va_arg(args, u32);
		va_end(args);
	}

	ret = av7110_send_fw_cmd(av7110, buf, num + 2);
	if (ret && ret != -ERESTARTSYS)
		printk(KERN_ERR "dvb-ttpci: av7110_fw_cmd error %d\n", ret);
	return ret;
}

#if 0
int av7110_send_ci_cmd(struct av7110 *av7110, u8 subcom, u8 *buf, u8 len)
{
	int i, ret;
	u16 cmd[18] = { ((COMTYPE_COMMON_IF << 8) + subcom),
		16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	dprintk(4, "%p\n", av7110);

	for(i = 0; i < len && i < 32; i++)
	{
		if(i % 2 == 0)
			cmd[(i / 2) + 2] = (u16)(buf[i]) << 8;
		else
			cmd[(i / 2) + 2] |= buf[i];
	}

	ret = av7110_send_fw_cmd(av7110, cmd, 18);
	if (ret && ret != -ERESTARTSYS)
		printk(KERN_ERR "dvb-ttpci: av7110_send_ci_cmd error %d\n", ret);
	return ret;
}
#endif  /*  0  */

int av7110_fw_request(struct av7110 *av7110, u16 *request_buf,
		      int request_buf_len, u16 *reply_buf, int reply_buf_len)
{
	int err;
	s16 i;
	unsigned long start;
#ifdef COM_DEBUG
	u32 stat;
#endif

	dprintk(4, "%p\n", av7110);

	if (!av7110->arm_ready) {
		dprintk(1, "arm not ready.\n");
		return -1;
	}

	if (mutex_lock_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;

	if ((err = __av7110_send_fw_cmd(av7110, request_buf, request_buf_len)) < 0) {
		mutex_unlock(&av7110->dcomlock);
		printk(KERN_ERR "dvb-ttpci: av7110_fw_request error %d\n", err);
		return err;
	}

	start = jiffies;
	while (1) {
		err = time_after(jiffies, start + ARM_WAIT_FREE);
		if (rdebi(av7110, DEBINOSWAP, COMMAND, 0, 2) == 0)
			break;
		if (err) {
			printk(KERN_ERR "%s: timeout waiting for COMMAND to complete\n", __func__);
			mutex_unlock(&av7110->dcomlock);
			return -ETIMEDOUT;
		}
#ifdef _NOHANDSHAKE
		msleep(1);
#endif
	}

#ifndef _NOHANDSHAKE
	start = jiffies;
	while (1) {
		err = time_after(jiffies, start + ARM_WAIT_SHAKE);
		if (rdebi(av7110, DEBINOSWAP, HANDSHAKE_REG, 0, 2) == 0)
			break;
		if (err) {
			printk(KERN_ERR "%s: timeout waiting for HANDSHAKE_REG\n", __func__);
			mutex_unlock(&av7110->dcomlock);
			return -ETIMEDOUT;
		}
		msleep(1);
	}
#endif

#ifdef COM_DEBUG
	stat = rdebi(av7110, DEBINOSWAP, MSGSTATE, 0, 2);
	if (stat & GPMQOver) {
		printk(KERN_ERR "%s: GPMQOver\n", __func__);
		mutex_unlock(&av7110->dcomlock);
		return -1;
	}
	else if (stat & OSDQOver) {
		printk(KERN_ERR "%s: OSDQOver\n", __func__);
		mutex_unlock(&av7110->dcomlock);
		return -1;
	}
#endif

	for (i = 0; i < reply_buf_len; i++)
		reply_buf[i] = rdebi(av7110, DEBINOSWAP, COM_BUFF + 2 * i, 0, 2);

	mutex_unlock(&av7110->dcomlock);
	return 0;
}

static int av7110_fw_query(struct av7110 *av7110, u16 tag, u16* buf, s16 length)
{
	int ret;
	ret = av7110_fw_request(av7110, &tag, 0, buf, length);
	if (ret)
		printk(KERN_ERR "dvb-ttpci: av7110_fw_query error %d\n", ret);
	return ret;
}


/****************************************************************************
 * Firmware commands
 ****************************************************************************/

/* get version of the firmware ROM, RTSL, video ucode and ARM application  */
int av7110_firmversion(struct av7110 *av7110)
{
	u16 buf[20];
	u16 tag = ((COMTYPE_REQUEST << 8) + ReqVersion);

	dprintk(4, "%p\n", av7110);

	if (av7110_fw_query(av7110, tag, buf, 16)) {
		printk("dvb-ttpci: failed to boot firmware @ card %d\n",
		       av7110->dvb_adapter.num);
		return -EIO;
	}

	av7110->arm_fw = (buf[0] << 16) + buf[1];
	av7110->arm_rtsl = (buf[2] << 16) + buf[3];
	av7110->arm_vid = (buf[4] << 16) + buf[5];
	av7110->arm_app = (buf[6] << 16) + buf[7];
	av7110->avtype = (buf[8] << 16) + buf[9];

	printk("dvb-ttpci: info @ card %d: firm %08x, rtsl %08x, vid %08x, app %08x\n",
	       av7110->dvb_adapter.num, av7110->arm_fw,
	       av7110->arm_rtsl, av7110->arm_vid, av7110->arm_app);

	/* print firmware capabilities */
	if (FW_CI_LL_SUPPORT(av7110->arm_app))
		printk("dvb-ttpci: firmware @ card %d supports CI link layer interface\n",
		       av7110->dvb_adapter.num);
	else
		printk("dvb-ttpci: no firmware support for CI link layer interface @ card %d\n",
		       av7110->dvb_adapter.num);

	return 0;
}


int av7110_diseqc_send(struct av7110 *av7110, int len, u8 *msg, unsigned long burst)
{
	int i, ret;
	u16 buf[18] = { ((COMTYPE_AUDIODAC << 8) + SendDiSEqC),
			16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	dprintk(4, "%p\n", av7110);

	if (len > 10)
		len = 10;

	buf[1] = len + 2;
	buf[2] = len;

	if (burst != -1)
		buf[3] = burst ? 0x01 : 0x00;
	else
		buf[3] = 0xffff;

	for (i = 0; i < len; i++)
		buf[i + 4] = msg[i];

	ret = av7110_send_fw_cmd(av7110, buf, 18);
	if (ret && ret!=-ERESTARTSYS)
		printk(KERN_ERR "dvb-ttpci: av7110_diseqc_send error %d\n", ret);
	return ret;
}


#ifdef CONFIG_DVB_AV7110_OSD

static inline int SetColorBlend(struct av7110 *av7110, u8 windownr)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, SetCBlend, 1, windownr);
}

static inline int SetBlend_(struct av7110 *av7110, u8 windownr,
		     enum av7110_osd_palette_type colordepth, u16 index, u8 blending)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, SetBlend, 4,
			     windownr, colordepth, index, blending);
}

static inline int SetColor_(struct av7110 *av7110, u8 windownr,
		     enum av7110_osd_palette_type colordepth, u16 index, u16 colorhi, u16 colorlo)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, SetColor, 5,
			     windownr, colordepth, index, colorhi, colorlo);
}

static inline int SetFont(struct av7110 *av7110, u8 windownr, u8 fontsize,
			  u16 colorfg, u16 colorbg)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, Set_Font, 4,
			     windownr, fontsize, colorfg, colorbg);
}

static int FlushText(struct av7110 *av7110)
{
	unsigned long start;
	int err;

	if (mutex_lock_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;
	start = jiffies;
	while (1) {
		err = time_after(jiffies, start + ARM_WAIT_OSD);
		if (rdebi(av7110, DEBINOSWAP, BUFF1_BASE, 0, 2) == 0)
			break;
		if (err) {
			printk(KERN_ERR "dvb-ttpci: %s(): timeout waiting for BUFF1_BASE == 0\n",
			       __func__);
			mutex_unlock(&av7110->dcomlock);
			return -ETIMEDOUT;
		}
		msleep(1);
	}
	mutex_unlock(&av7110->dcomlock);
	return 0;
}

static int WriteText(struct av7110 *av7110, u8 win, u16 x, u16 y, char *buf)
{
	int i, ret;
	unsigned long start;
	int length = strlen(buf) + 1;
	u16 cbuf[5] = { (COMTYPE_OSD << 8) + DText, 3, win, x, y };

	if (mutex_lock_interruptible(&av7110->dcomlock))
		return -ERESTARTSYS;

	start = jiffies;
	while (1) {
		ret = time_after(jiffies, start + ARM_WAIT_OSD);
		if (rdebi(av7110, DEBINOSWAP, BUFF1_BASE, 0, 2) == 0)
			break;
		if (ret) {
			printk(KERN_ERR "dvb-ttpci: %s: timeout waiting for BUFF1_BASE == 0\n",
			       __func__);
			mutex_unlock(&av7110->dcomlock);
			return -ETIMEDOUT;
		}
		msleep(1);
	}
#ifndef _NOHANDSHAKE
	start = jiffies;
	while (1) {
		ret = time_after(jiffies, start + ARM_WAIT_SHAKE);
		if (rdebi(av7110, DEBINOSWAP, HANDSHAKE_REG, 0, 2) == 0)
			break;
		if (ret) {
			printk(KERN_ERR "dvb-ttpci: %s: timeout waiting for HANDSHAKE_REG\n",
			       __func__);
			mutex_unlock(&av7110->dcomlock);
			return -ETIMEDOUT;
		}
		msleep(1);
	}
#endif
	for (i = 0; i < length / 2; i++)
		wdebi(av7110, DEBINOSWAP, BUFF1_BASE + i * 2,
		      swab16(*(u16 *)(buf + 2 * i)), 2);
	if (length & 1)
		wdebi(av7110, DEBINOSWAP, BUFF1_BASE + i * 2, 0, 2);
	ret = __av7110_send_fw_cmd(av7110, cbuf, 5);
	mutex_unlock(&av7110->dcomlock);
	if (ret && ret!=-ERESTARTSYS)
		printk(KERN_ERR "dvb-ttpci: WriteText error %d\n", ret);
	return ret;
}

static inline int DrawLine(struct av7110 *av7110, u8 windownr,
			   u16 x, u16 y, u16 dx, u16 dy, u16 color)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, DLine, 6,
			     windownr, x, y, dx, dy, color);
}

static inline int DrawBlock(struct av7110 *av7110, u8 windownr,
			    u16 x, u16 y, u16 dx, u16 dy, u16 color)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, DBox, 6,
			     windownr, x, y, dx, dy, color);
}

static inline int HideWindow(struct av7110 *av7110, u8 windownr)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, WHide, 1, windownr);
}

static inline int MoveWindowRel(struct av7110 *av7110, u8 windownr, u16 x, u16 y)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, WMoveD, 3, windownr, x, y);
}

static inline int MoveWindowAbs(struct av7110 *av7110, u8 windownr, u16 x, u16 y)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, WMoveA, 3, windownr, x, y);
}

static inline int DestroyOSDWindow(struct av7110 *av7110, u8 windownr)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, WDestroy, 1, windownr);
}

static inline int CreateOSDWindow(struct av7110 *av7110, u8 windownr,
				  osd_raw_window_t disptype,
				  u16 width, u16 height)
{
	return av7110_fw_cmd(av7110, COMTYPE_OSD, WCreate, 4,
			     windownr, disptype, width, height);
}


static enum av7110_osd_palette_type bpp2pal[8] = {
	Pal1Bit, Pal2Bit, 0, Pal4Bit, 0, 0, 0, Pal8Bit
};
static osd_raw_window_t bpp2bit[8] = {
	OSD_BITMAP1, OSD_BITMAP2, 0, OSD_BITMAP4, 0, 0, 0, OSD_BITMAP8
};

static inline int WaitUntilBmpLoaded(struct av7110 *av7110)
{
	int ret = wait_event_timeout(av7110->bmpq,
				av7110->bmp_state != BMP_LOADING, 10*HZ);
	if (ret == 0) {
		printk("dvb-ttpci: warning: timeout waiting in LoadBitmap: %d, %d\n",
		       ret, av7110->bmp_state);
		av7110->bmp_state = BMP_NONE;
		return -ETIMEDOUT;
	}
	return 0;
}

static inline int LoadBitmap(struct av7110 *av7110,
			     u16 dx, u16 dy, int inc, u8 __user * data)
{
	u16 format;
	int bpp;
	int i;
	int d, delta;
	u8 c;
	int ret;

	dprintk(4, "%p\n", av7110);

	format = bpp2bit[av7110->osdbpp[av7110->osdwin]];

	av7110->bmp_state = BMP_LOADING;
	if	(format == OSD_BITMAP8) {
		bpp=8; delta = 1;
	} else if (format == OSD_BITMAP4) {
		bpp=4; delta = 2;
	} else if (format == OSD_BITMAP2) {
		bpp=2; delta = 4;
	} else if (format == OSD_BITMAP1) {
		bpp=1; delta = 8;
	} else {
		av7110->bmp_state = BMP_NONE;
		return -EINVAL;
	}
	av7110->bmplen = ((dx * dy * bpp + 7) & ~7) / 8;
	av7110->bmpp = 0;
	if (av7110->bmplen > 32768) {
		av7110->bmp_state = BMP_NONE;
		return -EINVAL;
	}
	for (i = 0; i < dy; i++) {
		if (copy_from_user(av7110->bmpbuf + 1024 + i * dx, data + i * inc, dx)) {
			av7110->bmp_state = BMP_NONE;
			return -EINVAL;
		}
	}
	if (format != OSD_BITMAP8) {
		for (i = 0; i < dx * dy / delta; i++) {
			c = ((u8 *)av7110->bmpbuf)[1024 + i * delta + delta - 1];
			for (d = delta - 2; d >= 0; d--) {
				c |= (((u8 *)av7110->bmpbuf)[1024 + i * delta + d]
				      << ((delta - d - 1) * bpp));
				((u8 *)av7110->bmpbuf)[1024 + i] = c;
			}
		}
	}
	av7110->bmplen += 1024;
	dprintk(4, "av7110_fw_cmd: LoadBmp size %d\n", av7110->bmplen);
	ret = av7110_fw_cmd(av7110, COMTYPE_OSD, LoadBmp, 3, format, dx, dy);
	if (!ret)
		ret = WaitUntilBmpLoaded(av7110);
	return ret;
}

static int BlitBitmap(struct av7110 *av7110, u16 x, u16 y)
{
	dprintk(4, "%p\n", av7110);

	return av7110_fw_cmd(av7110, COMTYPE_OSD, BlitBmp, 4, av7110->osdwin, x, y, 0);
}

static inline int ReleaseBitmap(struct av7110 *av7110)
{
	dprintk(4, "%p\n", av7110);

	if (av7110->bmp_state != BMP_LOADED && FW_VERSION(av7110->arm_app) < 0x261e)
		return -1;
	if (av7110->bmp_state == BMP_LOADING)
		dprintk(1,"ReleaseBitmap called while BMP_LOADING\n");
	av7110->bmp_state = BMP_NONE;
	return av7110_fw_cmd(av7110, COMTYPE_OSD, ReleaseBmp, 0);
}

static u32 RGB2YUV(u16 R, u16 G, u16 B)
{
	u16 y, u, v;
	u16 Y, Cr, Cb;

	y = R * 77 + G * 150 + B * 29;	/* Luma=0.299R+0.587G+0.114B 0..65535 */
	u = 2048 + B * 8 -(y >> 5);	/* Cr 0..4095 */
	v = 2048 + R * 8 -(y >> 5);	/* Cb 0..4095 */

	Y = y / 256;
	Cb = u / 16;
	Cr = v / 16;

	return Cr | (Cb << 16) | (Y << 8);
}

static int OSDSetColor(struct av7110 *av7110, u8 color, u8 r, u8 g, u8 b, u8 blend)
{
	int ret;

	u16 ch, cl;
	u32 yuv;

	yuv = blend ? RGB2YUV(r,g,b) : 0;
	cl = (yuv & 0xffff);
	ch = ((yuv >> 16) & 0xffff);
	ret = SetColor_(av7110, av7110->osdwin, bpp2pal[av7110->osdbpp[av7110->osdwin]],
			color, ch, cl);
	if (!ret)
		ret = SetBlend_(av7110, av7110->osdwin, bpp2pal[av7110->osdbpp[av7110->osdwin]],
				color, ((blend >> 4) & 0x0f));
	return ret;
}

static int OSDSetPalette(struct av7110 *av7110, u32 __user * colors, u8 first, u8 last)
{
	int i;
	int length = last - first + 1;

	if (length * 4 > DATA_BUFF3_SIZE)
		return -EINVAL;

	for (i = 0; i < length; i++) {
		u32 color, blend, yuv;

		if (get_user(color, colors + i))
			return -EFAULT;
		blend = (color & 0xF0000000) >> 4;
		yuv = blend ? RGB2YUV(color & 0xFF, (color >> 8) & 0xFF,
				     (color >> 16) & 0xFF) | blend : 0;
		yuv = ((yuv & 0xFFFF0000) >> 16) | ((yuv & 0x0000FFFF) << 16);
		wdebi(av7110, DEBINOSWAP, DATA_BUFF3_BASE + i * 4, yuv, 4);
	}
	return av7110_fw_cmd(av7110, COMTYPE_OSD, Set_Palette, 4,
			    av7110->osdwin,
			    bpp2pal[av7110->osdbpp[av7110->osdwin]],
			    first, last);
}

static int OSDSetBlock(struct av7110 *av7110, int x0, int y0,
		       int x1, int y1, int inc, u8 __user * data)
{
	uint w, h, bpp, bpl, size, lpb, bnum, brest;
	int i;
	int rc,release_rc;

	w = x1 - x0 + 1;
	h = y1 - y0 + 1;
	if (inc <= 0)
		inc = w;
	if (w <= 0 || w > 720 || h <= 0 || h > 576)
		return -EINVAL;
	bpp = av7110->osdbpp[av7110->osdwin] + 1;
	bpl = ((w * bpp + 7) & ~7) / 8;
	size = h * bpl;
	lpb = (32 * 1024) / bpl;
	bnum = size / (lpb * bpl);
	brest = size - bnum * lpb * bpl;

	if (av7110->bmp_state == BMP_LOADING) {
		/* possible if syscall is repeated by -ERESTARTSYS and if firmware cannot abort */
		BUG_ON (FW_VERSION(av7110->arm_app) >= 0x261e);
		rc = WaitUntilBmpLoaded(av7110);
		if (rc)
			return rc;
		/* just continue. This should work for all fw versions
		 * if bnum==1 && !brest && LoadBitmap was successful
		 */
	}

	rc = 0;
	for (i = 0; i < bnum; i++) {
		rc = LoadBitmap(av7110, w, lpb, inc, data);
		if (rc)
			break;
		rc = BlitBitmap(av7110, x0, y0 + i * lpb);
		if (rc)
			break;
		data += lpb * inc;
	}
	if (!rc && brest) {
		rc = LoadBitmap(av7110, w, brest / bpl, inc, data);
		if (!rc)
			rc = BlitBitmap(av7110, x0, y0 + bnum * lpb);
	}
	release_rc = ReleaseBitmap(av7110);
	if (!rc)
		rc = release_rc;
	if (rc)
		dprintk(1,"returns %d\n",rc);
	return rc;
}

int av7110_osd_cmd(struct av7110 *av7110, osd_cmd_t *dc)
{
	int ret;

	if (mutex_lock_interruptible(&av7110->osd_mutex))
		return -ERESTARTSYS;

	switch (dc->cmd) {
	case OSD_Close:
		ret = DestroyOSDWindow(av7110, av7110->osdwin);
		break;
	case OSD_Open:
		av7110->osdbpp[av7110->osdwin] = (dc->color - 1) & 7;
		ret = CreateOSDWindow(av7110, av7110->osdwin,
				bpp2bit[av7110->osdbpp[av7110->osdwin]],
				dc->x1 - dc->x0 + 1, dc->y1 - dc->y0 + 1);
		if (ret)
			break;
		if (!dc->data) {
			ret = MoveWindowAbs(av7110, av7110->osdwin, dc->x0, dc->y0);
			if (ret)
				break;
			ret = SetColorBlend(av7110, av7110->osdwin);
		}
		break;
	case OSD_Show:
		ret = MoveWindowRel(av7110, av7110->osdwin, 0, 0);
		break;
	case OSD_Hide:
		ret = HideWindow(av7110, av7110->osdwin);
		break;
	case OSD_Clear:
		ret = DrawBlock(av7110, av7110->osdwin, 0, 0, 720, 576, 0);
		break;
	case OSD_Fill:
		ret = DrawBlock(av7110, av7110->osdwin, 0, 0, 720, 576, dc->color);
		break;
	case OSD_SetColor:
		ret = OSDSetColor(av7110, dc->color, dc->x0, dc->y0, dc->x1, dc->y1);
		break;
	case OSD_SetPalette:
		if (FW_VERSION(av7110->arm_app) >= 0x2618)
			ret = OSDSetPalette(av7110, dc->data, dc->color, dc->x0);
		else {
			int i, len = dc->x0-dc->color+1;
			u8 __user *colors = (u8 __user *)dc->data;
			u8 r, g = 0, b = 0, blend = 0;
			ret = 0;
			for (i = 0; i<len; i++) {
				if (get_user(r, colors + i * 4) ||
				    get_user(g, colors + i * 4 + 1) ||
				    get_user(b, colors + i * 4 + 2) ||
				    get_user(blend, colors + i * 4 + 3)) {
					ret = -EFAULT;
					break;
				    }
				ret = OSDSetColor(av7110, dc->color + i, r, g, b, blend);
				if (ret)
					break;
			}
		}
		break;
	case OSD_SetPixel:
		ret = DrawLine(av7110, av7110->osdwin,
			 dc->x0, dc->y0, 0, 0, dc->color);
		break;
	case OSD_SetRow:
		dc->y1 = dc->y0;
		/* fall through */
	case OSD_SetBlock:
		ret = OSDSetBlock(av7110, dc->x0, dc->y0, dc->x1, dc->y1, dc->color, dc->data);
		break;
	case OSD_FillRow:
		ret = DrawBlock(av7110, av7110->osdwin, dc->x0, dc->y0,
			  dc->x1-dc->x0+1, dc->y1, dc->color);
		break;
	case OSD_FillBlock:
		ret = DrawBlock(av7110, av7110->osdwin, dc->x0, dc->y0,
			  dc->x1 - dc->x0 + 1, dc->y1 - dc->y0 + 1, dc->color);
		break;
	case OSD_Line:
		ret = DrawLine(av7110, av7110->osdwin,
			 dc->x0, dc->y0, dc->x1 - dc->x0, dc->y1 - dc->y0, dc->color);
		break;
	case OSD_Text:
	{
		char textbuf[240];

		if (strncpy_from_user(textbuf, dc->data, 240) < 0) {
			ret = -EFAULT;
			break;
		}
		textbuf[239] = 0;
		if (dc->x1 > 3)
			dc->x1 = 3;
		ret = SetFont(av7110, av7110->osdwin, dc->x1,
			(u16) (dc->color & 0xffff), (u16) (dc->color >> 16));
		if (!ret)
			ret = FlushText(av7110);
		if (!ret)
			ret = WriteText(av7110, av7110->osdwin, dc->x0, dc->y0, textbuf);
		break;
	}
	case OSD_SetWindow:
		if (dc->x0 < 1 || dc->x0 > 7)
			ret = -EINVAL;
		else {
			av7110->osdwin = dc->x0;
			ret = 0;
		}
		break;
	case OSD_MoveWindow:
		ret = MoveWindowAbs(av7110, av7110->osdwin, dc->x0, dc->y0);
		if (!ret)
			ret = SetColorBlend(av7110, av7110->osdwin);
		break;
	case OSD_OpenRaw:
		if (dc->color < OSD_BITMAP1 || dc->color > OSD_CURSOR) {
			ret = -EINVAL;
			break;
		}
		if (dc->color >= OSD_BITMAP1 && dc->color <= OSD_BITMAP8HR)
			av7110->osdbpp[av7110->osdwin] = (1 << (dc->color & 3)) - 1;
		else
			av7110->osdbpp[av7110->osdwin] = 0;
		ret = CreateOSDWindow(av7110, av7110->osdwin, (osd_raw_window_t)dc->color,
				dc->x1 - dc->x0 + 1, dc->y1 - dc->y0 + 1);
		if (ret)
			break;
		if (!dc->data) {
			ret = MoveWindowAbs(av7110, av7110->osdwin, dc->x0, dc->y0);
			if (!ret)
				ret = SetColorBlend(av7110, av7110->osdwin);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&av7110->osd_mutex);
	if (ret==-ERESTARTSYS)
		dprintk(1, "av7110_osd_cmd(%d) returns with -ERESTARTSYS\n",dc->cmd);
	else if (ret)
		dprintk(1, "av7110_osd_cmd(%d) returns with %d\n",dc->cmd,ret);

	return ret;
}

int av7110_osd_capability(struct av7110 *av7110, osd_cap_t *cap)
{
	switch (cap->cmd) {
	case OSD_CAP_MEMSIZE:
		if (FW_4M_SDRAM(av7110->arm_app))
			cap->val = 1000000;
		else
			cap->val = 92000;
		return 0;
	default:
		return -EINVAL;
	}
}
#endif /* CONFIG_DVB_AV7110_OSD */
