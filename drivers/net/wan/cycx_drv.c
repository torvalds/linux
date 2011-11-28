/*
* cycx_drv.c	Cyclom 2X Support Module.
*
*		This module is a library of common hardware specific
*		functions used by the Cyclades Cyclom 2X sync card.
*
* Author:	Arnaldo Carvalho de Melo <acme@conectiva.com.br>
*
* Copyright:	(c) 1998-2003 Arnaldo Carvalho de Melo
*
* Based on sdladrv.c by Gene Kozin <genek@compuserve.com>
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* 1999/11/11	acme		set_current_state(TASK_INTERRUPTIBLE), code
*				cleanup
* 1999/11/08	acme		init_cyc2x deleted, doing nothing
* 1999/11/06	acme		back to read[bw], write[bw] and memcpy_to and
*				fromio to use dpmbase ioremaped
* 1999/10/26	acme		use isa_read[bw], isa_write[bw] & isa_memcpy_to
*				& fromio
* 1999/10/23	acme		cleanup to only supports cyclom2x: all the other
*				boards are no longer manufactured by cyclades,
*				if someone wants to support them... be my guest!
* 1999/05/28    acme		cycx_intack & cycx_intde gone for good
* 1999/05/18	acme		lots of unlogged work, submitting to Linus...
* 1999/01/03	acme		more judicious use of data types
* 1999/01/03	acme		judicious use of data types :>
*				cycx_inten trying to reset pending interrupts
*				from cyclom 2x - I think this isn't the way to
*				go, but for now...
* 1999/01/02	acme		cycx_intack ok, I think there's nothing to do
*				to ack an int in cycx_drv.c, only handle it in
*				cyx_isr (or in the other protocols: cyp_isr,
*				cyf_isr, when they get implemented.
* Dec 31, 1998	acme		cycx_data_boot & cycx_code_boot fixed, crossing
*				fingers to see x25_configure in cycx_x25.c
*				work... :)
* Dec 26, 1998	acme		load implementation fixed, seems to work! :)
*				cycx_2x_dpmbase_options with all the possible
*				DPM addresses (20).
*				cycx_intr implemented (test this!)
*				general code cleanup
* Dec  8, 1998	Ivan Passos	Cyclom-2X firmware load implementation.
* Aug  8, 1998	acme		Initial version.
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>		/* __init */
#include <linux/module.h>
#include <linux/kernel.h>	/* printk(), and other useful stuff */
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/cycx_drv.h>	/* API definitions */
#include <linux/cycx_cfm.h>	/* CYCX firmware module definitions */
#include <linux/delay.h>	/* udelay, msleep_interruptible */
#include <asm/io.h>		/* read[wl], write[wl], ioremap, iounmap */

#define	MOD_VERSION	0
#define	MOD_RELEASE	6

MODULE_AUTHOR("Arnaldo Carvalho de Melo");
MODULE_DESCRIPTION("Cyclom 2x Sync Card Driver");
MODULE_LICENSE("GPL");

/* Hardware-specific functions */
static int load_cyc2x(struct cycx_hw *hw, struct cycx_firmware *cfm, u32 len);
static void cycx_bootcfg(struct cycx_hw *hw);

static int reset_cyc2x(void __iomem *addr);
static int detect_cyc2x(void __iomem *addr);

/* Miscellaneous functions */
static int get_option_index(const long *optlist, long optval);
static u16 checksum(u8 *buf, u32 len);

#define wait_cyc(addr) cycx_exec(addr + CMD_OFFSET)

/* Global Data */

/* private data */
static const char fullname[] = "Cyclom 2X Support Module";
static const char copyright[] =
	"(c) 1998-2003 Arnaldo Carvalho de Melo <acme@conectiva.com.br>";

/* Hardware configuration options.
 * These are arrays of configuration options used by verification routines.
 * The first element of each array is its size (i.e. number of options).
 */
static const long cyc2x_dpmbase_options[] = {
	20,
	0xA0000, 0xA4000, 0xA8000, 0xAC000, 0xB0000, 0xB4000, 0xB8000,
	0xBC000, 0xC0000, 0xC4000, 0xC8000, 0xCC000, 0xD0000, 0xD4000,
	0xD8000, 0xDC000, 0xE0000, 0xE4000, 0xE8000, 0xEC000
};

static const long cycx_2x_irq_options[]  = { 7, 3, 5, 9, 10, 11, 12, 15 };

/* Kernel Loadable Module Entry Points */
/* Module 'insert' entry point.
 * o print announcement
 * o initialize static data
 *
 * Return:	0	Ok
 *		< 0	error.
 * Context:	process */

static int __init cycx_drv_init(void)
{
	pr_info("%s v%u.%u %s\n",
		fullname, MOD_VERSION, MOD_RELEASE, copyright);

	return 0;
}

/* Module 'remove' entry point.
 * o release all remaining system resources */
static void cycx_drv_cleanup(void)
{
}

/* Kernel APIs */
/* Set up adapter.
 * o detect adapter type
 * o verify hardware configuration options
 * o check for hardware conflicts
 * o set up adapter shared memory
 * o test adapter memory
 * o load firmware
 * Return:	0	ok.
 *		< 0	error */
EXPORT_SYMBOL(cycx_setup);
int cycx_setup(struct cycx_hw *hw, void *cfm, u32 len, unsigned long dpmbase)
{
	int err;

	/* Verify IRQ configuration options */
	if (!get_option_index(cycx_2x_irq_options, hw->irq)) {
		pr_err("IRQ %d is invalid!\n", hw->irq);
		return -EINVAL;
	}

	/* Setup adapter dual-port memory window and test memory */
	if (!dpmbase) {
		pr_err("you must specify the dpm address!\n");
 		return -EINVAL;
	} else if (!get_option_index(cyc2x_dpmbase_options, dpmbase)) {
		pr_err("memory address 0x%lX is invalid!\n", dpmbase);
		return -EINVAL;
	}

	hw->dpmbase = ioremap(dpmbase, CYCX_WINDOWSIZE);
	hw->dpmsize = CYCX_WINDOWSIZE;

	if (!detect_cyc2x(hw->dpmbase)) {
		pr_err("adapter Cyclom 2X not found at address 0x%lX!\n",
		       dpmbase);
		return -EINVAL;
	}

	pr_info("found Cyclom 2X card at address 0x%lX\n", dpmbase);

	/* Load firmware. If loader fails then shut down adapter */
	err = load_cyc2x(hw, cfm, len);

	if (err)
		cycx_down(hw);         /* shutdown adapter */

	return err;
}

EXPORT_SYMBOL(cycx_down);
int cycx_down(struct cycx_hw *hw)
{
	iounmap(hw->dpmbase);
	return 0;
}

/* Enable interrupt generation.  */
static void cycx_inten(struct cycx_hw *hw)
{
	writeb(0, hw->dpmbase);
}

/* Generate an interrupt to adapter's CPU. */
EXPORT_SYMBOL(cycx_intr);
void cycx_intr(struct cycx_hw *hw)
{
	writew(0, hw->dpmbase + GEN_CYCX_INTR);
}

/* Execute Adapter Command.
 * o Set exec flag.
 * o Busy-wait until flag is reset. */
EXPORT_SYMBOL(cycx_exec);
int cycx_exec(void __iomem *addr)
{
	u16 i = 0;
	/* wait till addr content is zeroed */

	while (readw(addr)) {
		udelay(1000);

		if (++i > 50)
			return -1;
	}

	return 0;
}

/* Read absolute adapter memory.
 * Transfer data from adapter's memory to data buffer. */
EXPORT_SYMBOL(cycx_peek);
int cycx_peek(struct cycx_hw *hw, u32 addr, void *buf, u32 len)
{
	if (len == 1)
		*(u8*)buf = readb(hw->dpmbase + addr);
	else
		memcpy_fromio(buf, hw->dpmbase + addr, len);

	return 0;
}

/* Write Absolute Adapter Memory.
 * Transfer data from data buffer to adapter's memory. */
EXPORT_SYMBOL(cycx_poke);
int cycx_poke(struct cycx_hw *hw, u32 addr, void *buf, u32 len)
{
	if (len == 1)
		writeb(*(u8*)buf, hw->dpmbase + addr);
	else
		memcpy_toio(hw->dpmbase + addr, buf, len);

	return 0;
}

/* Hardware-Specific Functions */

/* Load Aux Routines */
/* Reset board hardware.
   return 1 if memory exists at addr and 0 if not. */
static int memory_exists(void __iomem *addr)
{
	int tries = 0;

	for (; tries < 3 ; tries++) {
		writew(TEST_PATTERN, addr + 0x10);

		if (readw(addr + 0x10) == TEST_PATTERN)
			if (readw(addr + 0x10) == TEST_PATTERN)
				return 1;

		msleep_interruptible(1 * 1000);
	}

	return 0;
}

/* Load reset code. */
static void reset_load(void __iomem *addr, u8 *buffer, u32 cnt)
{
	void __iomem *pt_code = addr + RESET_OFFSET;
	u16 i; /*, j; */

	for (i = 0 ; i < cnt ; i++) {
/*		for (j = 0 ; j < 50 ; j++); Delay - FIXME busy waiting... */
		writeb(*buffer++, pt_code++);
	}
}

/* Load buffer using boot interface.
 * o copy data from buffer to Cyclom-X memory
 * o wait for reset code to copy it to right portion of memory */
static int buffer_load(void __iomem *addr, u8 *buffer, u32 cnt)
{
	memcpy_toio(addr + DATA_OFFSET, buffer, cnt);
	writew(GEN_BOOT_DAT, addr + CMD_OFFSET);

	return wait_cyc(addr);
}

/* Set up entry point and kick start Cyclom-X CPU. */
static void cycx_start(void __iomem *addr)
{
	/* put in 0x30 offset the jump instruction to the code entry point */
	writeb(0xea, addr + 0x30);
	writeb(0x00, addr + 0x31);
	writeb(0xc4, addr + 0x32);
	writeb(0x00, addr + 0x33);
	writeb(0x00, addr + 0x34);

	/* cmd to start executing code */
	writew(GEN_START, addr + CMD_OFFSET);
}

/* Load and boot reset code. */
static void cycx_reset_boot(void __iomem *addr, u8 *code, u32 len)
{
	void __iomem *pt_start = addr + START_OFFSET;

	writeb(0xea, pt_start++); /* jmp to f000:3f00 */
	writeb(0x00, pt_start++);
	writeb(0xfc, pt_start++);
	writeb(0x00, pt_start++);
	writeb(0xf0, pt_start);
	reset_load(addr, code, len);

	/* 80186 was in hold, go */
	writeb(0, addr + START_CPU);
	msleep_interruptible(1 * 1000);
}

/* Load data.bin file through boot (reset) interface. */
static int cycx_data_boot(void __iomem *addr, u8 *code, u32 len)
{
	void __iomem *pt_boot_cmd = addr + CMD_OFFSET;
	u32 i;

	/* boot buffer length */
	writew(CFM_LOAD_BUFSZ, pt_boot_cmd + sizeof(u16));
	writew(GEN_DEFPAR, pt_boot_cmd);

	if (wait_cyc(addr) < 0)
		return -1;

	writew(0, pt_boot_cmd + sizeof(u16));
	writew(0x4000, pt_boot_cmd + 2 * sizeof(u16));
	writew(GEN_SET_SEG, pt_boot_cmd);

	if (wait_cyc(addr) < 0)
		return -1;

	for (i = 0 ; i < len ; i += CFM_LOAD_BUFSZ)
		if (buffer_load(addr, code + i,
				min_t(u32, CFM_LOAD_BUFSZ, (len - i))) < 0) {
			pr_err("Error !!\n");
			return -1;
		}

	return 0;
}


/* Load code.bin file through boot (reset) interface. */
static int cycx_code_boot(void __iomem *addr, u8 *code, u32 len)
{
	void __iomem *pt_boot_cmd = addr + CMD_OFFSET;
	u32 i;

	/* boot buffer length */
	writew(CFM_LOAD_BUFSZ, pt_boot_cmd + sizeof(u16));
	writew(GEN_DEFPAR, pt_boot_cmd);

	if (wait_cyc(addr) < 0)
		return -1;

	writew(0x0000, pt_boot_cmd + sizeof(u16));
	writew(0xc400, pt_boot_cmd + 2 * sizeof(u16));
	writew(GEN_SET_SEG, pt_boot_cmd);

	if (wait_cyc(addr) < 0)
		return -1;

	for (i = 0 ; i < len ; i += CFM_LOAD_BUFSZ)
		if (buffer_load(addr, code + i,
				min_t(u32, CFM_LOAD_BUFSZ, (len - i)))) {
			pr_err("Error !!\n");
			return -1;
		}

	return 0;
}

/* Load adapter from the memory image of the CYCX firmware module.
 * o verify firmware integrity and compatibility
 * o start adapter up */
static int load_cyc2x(struct cycx_hw *hw, struct cycx_firmware *cfm, u32 len)
{
	int i, j;
	struct cycx_fw_header *img_hdr;
	u8 *reset_image,
	   *data_image,
	   *code_image;
	void __iomem *pt_cycld = hw->dpmbase + 0x400;
	u16 cksum;

	/* Announce */
	pr_info("firmware signature=\"%s\"\n", cfm->signature);

	/* Verify firmware signature */
	if (strcmp(cfm->signature, CFM_SIGNATURE)) {
		pr_err("load_cyc2x: not Cyclom-2X firmware!\n");
		return -EINVAL;
	}

	pr_info("firmware version=%u\n", cfm->version);

	/* Verify firmware module format version */
	if (cfm->version != CFM_VERSION) {
		pr_err("%s: firmware format %u rejected! Expecting %u.\n",
		       __func__, cfm->version, CFM_VERSION);
		return -EINVAL;
	}

	/* Verify firmware module length and checksum */
	cksum = checksum((u8*)&cfm->info, sizeof(struct cycx_fw_info) +
					  cfm->info.codesize);
/*
	FIXME cfm->info.codesize is off by 2
	if (((len - sizeof(struct cycx_firmware) - 1) != cfm->info.codesize) ||
*/
	if (cksum != cfm->checksum) {
		pr_err("%s: firmware corrupted!\n", __func__);
		pr_err(" cdsize = 0x%x (expected 0x%lx)\n",
		       len - (int)sizeof(struct cycx_firmware) - 1,
		       cfm->info.codesize);
		pr_err(" chksum = 0x%x (expected 0x%x)\n",
		       cksum, cfm->checksum);
		return -EINVAL;
	}

	/* If everything is ok, set reset, data and code pointers */
	img_hdr = (struct cycx_fw_header *)&cfm->image;
#ifdef FIRMWARE_DEBUG
	pr_info("%s: image sizes\n", __func__);
	pr_info(" reset=%lu\n", img_hdr->reset_size);
	pr_info("  data=%lu\n", img_hdr->data_size);
	pr_info("  code=%lu\n", img_hdr->code_size);
#endif
	reset_image = ((u8 *)img_hdr) + sizeof(struct cycx_fw_header);
	data_image = reset_image + img_hdr->reset_size;
	code_image = data_image + img_hdr->data_size;

	/*---- Start load ----*/
	/* Announce */
	pr_info("loading firmware %s (ID=%u)...\n",
		cfm->descr[0] ? cfm->descr : "unknown firmware",
		cfm->info.codeid);

	for (i = 0 ; i < 5 ; i++) {
		/* Reset Cyclom hardware */
		if (!reset_cyc2x(hw->dpmbase)) {
			pr_err("dpm problem or board not found\n");
			return -EINVAL;
		}

		/* Load reset.bin */
		cycx_reset_boot(hw->dpmbase, reset_image, img_hdr->reset_size);
		/* reset is waiting for boot */
		writew(GEN_POWER_ON, pt_cycld);
		msleep_interruptible(1 * 1000);

		for (j = 0 ; j < 3 ; j++)
			if (!readw(pt_cycld))
				goto reset_loaded;
			else
				msleep_interruptible(1 * 1000);
	}

	pr_err("reset not started\n");
	return -EINVAL;

reset_loaded:
	/* Load data.bin */
	if (cycx_data_boot(hw->dpmbase, data_image, img_hdr->data_size)) {
		pr_err("cannot load data file\n");
		return -EINVAL;
	}

	/* Load code.bin */
	if (cycx_code_boot(hw->dpmbase, code_image, img_hdr->code_size)) {
		pr_err("cannot load code file\n");
		return -EINVAL;
	}

	/* Prepare boot-time configuration data */
	cycx_bootcfg(hw);

	/* kick-off CPU */
	cycx_start(hw->dpmbase);

	/* Arthur Ganzert's tip: wait a while after the firmware loading...
	   seg abr 26 17:17:12 EST 1999 - acme */
	msleep_interruptible(7 * 1000);
	pr_info("firmware loaded!\n");

	/* enable interrupts */
	cycx_inten(hw);

	return 0;
}

/* Prepare boot-time firmware configuration data.
 * o initialize configuration data area
   From async.doc - V_3.4.0 - 07/18/1994
   - As of now, only static buffers are available to the user.
     So, the bit VD_RXDIRC must be set in 'valid'. That means that user
     wants to use the static transmission and reception buffers. */
static void cycx_bootcfg(struct cycx_hw *hw)
{
	/* use fixed buffers */
	writeb(FIXED_BUFFERS, hw->dpmbase + CONF_OFFSET);
}

/* Detect Cyclom 2x adapter.
 *	Following tests are used to detect Cyclom 2x adapter:
 *       to be completed based on the tests done below
 *	Return 1 if detected o.k. or 0 if failed.
 *	Note:	This test is destructive! Adapter will be left in shutdown
 *		state after the test. */
static int detect_cyc2x(void __iomem *addr)
{
	reset_cyc2x(addr);

	return memory_exists(addr);
}

/* Miscellaneous */
/* Get option's index into the options list.
 *	Return option's index (1 .. N) or zero if option is invalid. */
static int get_option_index(const long *optlist, long optval)
{
	int i = 1;

	for (; i <= optlist[0]; ++i)
		if (optlist[i] == optval)
			return i;

	return 0;
}

/* Reset adapter's CPU. */
static int reset_cyc2x(void __iomem *addr)
{
	writeb(0, addr + RST_ENABLE);
	msleep_interruptible(2 * 1000);
	writeb(0, addr + RST_DISABLE);
	msleep_interruptible(2 * 1000);

	return memory_exists(addr);
}

/* Calculate 16-bit CRC using CCITT polynomial. */
static u16 checksum(u8 *buf, u32 len)
{
	u16 crc = 0;
	u16 mask, flag;

	for (; len; --len, ++buf)
		for (mask = 0x80; mask; mask >>= 1) {
			flag = (crc & 0x8000);
			crc <<= 1;
			crc |= ((*buf & mask) ? 1 : 0);

			if (flag)
				crc ^= 0x1021;
		}

	return crc;
}

module_init(cycx_drv_init);
module_exit(cycx_drv_cleanup);

/* End */
