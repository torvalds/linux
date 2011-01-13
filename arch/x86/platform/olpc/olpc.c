/*
 * Support for the OLPC DCON and OLPC EC access
 *
 * Copyright © 2006  Advanced Micro Devices, Inc.
 * Copyright © 2007-2008  Andres Salomon <dilinger@debian.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/platform_device.h>

#include <asm/geode.h>
#include <asm/setup.h>
#include <asm/olpc.h>
#include <asm/olpc_ofw.h>

struct olpc_platform_t olpc_platform_info;
EXPORT_SYMBOL_GPL(olpc_platform_info);

static DEFINE_SPINLOCK(ec_lock);

/* what the timeout *should* be (in ms) */
#define EC_BASE_TIMEOUT 20

/* the timeout that bugs in the EC might force us to actually use */
static int ec_timeout = EC_BASE_TIMEOUT;

static int __init olpc_ec_timeout_set(char *str)
{
	if (get_option(&str, &ec_timeout) != 1) {
		ec_timeout = EC_BASE_TIMEOUT;
		printk(KERN_ERR "olpc-ec:  invalid argument to "
				"'olpc_ec_timeout=', ignoring!\n");
	}
	printk(KERN_DEBUG "olpc-ec:  using %d ms delay for EC commands.\n",
			ec_timeout);
	return 1;
}
__setup("olpc_ec_timeout=", olpc_ec_timeout_set);

/*
 * These {i,o}bf_status functions return whether the buffers are full or not.
 */

static inline unsigned int ibf_status(unsigned int port)
{
	return !!(inb(port) & 0x02);
}

static inline unsigned int obf_status(unsigned int port)
{
	return inb(port) & 0x01;
}

#define wait_on_ibf(p, d) __wait_on_ibf(__LINE__, (p), (d))
static int __wait_on_ibf(unsigned int line, unsigned int port, int desired)
{
	unsigned int timeo;
	int state = ibf_status(port);

	for (timeo = ec_timeout; state != desired && timeo; timeo--) {
		mdelay(1);
		state = ibf_status(port);
	}

	if ((state == desired) && (ec_timeout > EC_BASE_TIMEOUT) &&
			timeo < (ec_timeout - EC_BASE_TIMEOUT)) {
		printk(KERN_WARNING "olpc-ec:  %d: waited %u ms for IBF!\n",
				line, ec_timeout - timeo);
	}

	return !(state == desired);
}

#define wait_on_obf(p, d) __wait_on_obf(__LINE__, (p), (d))
static int __wait_on_obf(unsigned int line, unsigned int port, int desired)
{
	unsigned int timeo;
	int state = obf_status(port);

	for (timeo = ec_timeout; state != desired && timeo; timeo--) {
		mdelay(1);
		state = obf_status(port);
	}

	if ((state == desired) && (ec_timeout > EC_BASE_TIMEOUT) &&
			timeo < (ec_timeout - EC_BASE_TIMEOUT)) {
		printk(KERN_WARNING "olpc-ec:  %d: waited %u ms for OBF!\n",
				line, ec_timeout - timeo);
	}

	return !(state == desired);
}

/*
 * This allows the kernel to run Embedded Controller commands.  The EC is
 * documented at <http://wiki.laptop.org/go/Embedded_controller>, and the
 * available EC commands are here:
 * <http://wiki.laptop.org/go/Ec_specification>.  Unfortunately, while
 * OpenFirmware's source is available, the EC's is not.
 */
int olpc_ec_cmd(unsigned char cmd, unsigned char *inbuf, size_t inlen,
		unsigned char *outbuf,  size_t outlen)
{
	unsigned long flags;
	int ret = -EIO;
	int i;
	int restarts = 0;

	spin_lock_irqsave(&ec_lock, flags);

	/* Clear OBF */
	for (i = 0; i < 10 && (obf_status(0x6c) == 1); i++)
		inb(0x68);
	if (i == 10) {
		printk(KERN_ERR "olpc-ec:  timeout while attempting to "
				"clear OBF flag!\n");
		goto err;
	}

	if (wait_on_ibf(0x6c, 0)) {
		printk(KERN_ERR "olpc-ec:  timeout waiting for EC to "
				"quiesce!\n");
		goto err;
	}

restart:
	/*
	 * Note that if we time out during any IBF checks, that's a failure;
	 * we have to return.  There's no way for the kernel to clear that.
	 *
	 * If we time out during an OBF check, we can restart the command;
	 * reissuing it will clear the OBF flag, and we should be alright.
	 * The OBF flag will sometimes misbehave due to what we believe
	 * is a hardware quirk..
	 */
	pr_devel("olpc-ec:  running cmd 0x%x\n", cmd);
	outb(cmd, 0x6c);

	if (wait_on_ibf(0x6c, 0)) {
		printk(KERN_ERR "olpc-ec:  timeout waiting for EC to read "
				"command!\n");
		goto err;
	}

	if (inbuf && inlen) {
		/* write data to EC */
		for (i = 0; i < inlen; i++) {
			if (wait_on_ibf(0x6c, 0)) {
				printk(KERN_ERR "olpc-ec:  timeout waiting for"
						" EC accept data!\n");
				goto err;
			}
			pr_devel("olpc-ec:  sending cmd arg 0x%x\n", inbuf[i]);
			outb(inbuf[i], 0x68);
		}
	}
	if (outbuf && outlen) {
		/* read data from EC */
		for (i = 0; i < outlen; i++) {
			if (wait_on_obf(0x6c, 1)) {
				printk(KERN_ERR "olpc-ec:  timeout waiting for"
						" EC to provide data!\n");
				if (restarts++ < 10)
					goto restart;
				goto err;
			}
			outbuf[i] = inb(0x68);
			pr_devel("olpc-ec:  received 0x%x\n", outbuf[i]);
		}
	}

	ret = 0;
err:
	spin_unlock_irqrestore(&ec_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(olpc_ec_cmd);

static bool __init check_ofw_architecture(void)
{
	size_t propsize;
	char olpc_arch[5];
	const void *args[] = { NULL, "architecture", olpc_arch, (void *)5 };
	void *res[] = { &propsize };

	if (olpc_ofw("getprop", args, res)) {
		printk(KERN_ERR "ofw: getprop call failed!\n");
		return false;
	}
	return propsize == 5 && strncmp("OLPC", olpc_arch, 5) == 0;
}

static u32 __init get_board_revision(void)
{
	size_t propsize;
	__be32 rev;
	const void *args[] = { NULL, "board-revision-int", &rev, (void *)4 };
	void *res[] = { &propsize };

	if (olpc_ofw("getprop", args, res) || propsize != 4) {
		printk(KERN_ERR "ofw: getprop call failed!\n");
		return cpu_to_be32(0);
	}
	return be32_to_cpu(rev);
}

static bool __init platform_detect(void)
{
	if (!check_ofw_architecture())
		return false;
	olpc_platform_info.flags |= OLPC_F_PRESENT;
	olpc_platform_info.boardrev = get_board_revision();
	return true;
}

static int __init add_xo1_platform_devices(void)
{
	struct platform_device *pdev;

	pdev = platform_device_register_simple("xo1-rfkill", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	pdev = platform_device_register_simple("olpc-xo1", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	return 0;
}

static int __init olpc_init(void)
{
	int r = 0;

	if (!olpc_ofw_present() || !platform_detect())
		return 0;

	spin_lock_init(&ec_lock);

	/* assume B1 and above models always have a DCON */
	if (olpc_board_at_least(olpc_board(0xb1)))
		olpc_platform_info.flags |= OLPC_F_DCON;

	/* get the EC revision */
	olpc_ec_cmd(EC_FIRMWARE_REV, NULL, 0,
			(unsigned char *) &olpc_platform_info.ecver, 1);

#ifdef CONFIG_PCI_OLPC
	/* If the VSA exists let it emulate PCI, if not emulate in kernel.
	 * XO-1 only. */
	if (olpc_platform_info.boardrev < olpc_board_pre(0xd0) &&
			!cs5535_has_vsa2())
		x86_init.pci.arch_init = pci_olpc_init;
#endif

	printk(KERN_INFO "OLPC board revision %s%X (EC=%x)\n",
			((olpc_platform_info.boardrev & 0xf) < 8) ? "pre" : "",
			olpc_platform_info.boardrev >> 4,
			olpc_platform_info.ecver);

	if (olpc_platform_info.boardrev < olpc_board_pre(0xd0)) { /* XO-1 */
		r = add_xo1_platform_devices();
		if (r)
			return r;
	}

	return 0;
}

postcore_initcall(olpc_init);
