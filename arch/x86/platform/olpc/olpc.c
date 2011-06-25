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
#include <linux/of.h>
#include <linux/syscore_ops.h>

#include <asm/geode.h>
#include <asm/setup.h>
#include <asm/olpc.h>
#include <asm/olpc_ofw.h>

struct olpc_platform_t olpc_platform_info;
EXPORT_SYMBOL_GPL(olpc_platform_info);

static DEFINE_SPINLOCK(ec_lock);

/* EC event mask to be applied during suspend (defining wakeup sources). */
static u16 ec_wakeup_mask;

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

void olpc_ec_wakeup_set(u16 value)
{
	ec_wakeup_mask |= value;
}
EXPORT_SYMBOL_GPL(olpc_ec_wakeup_set);

void olpc_ec_wakeup_clear(u16 value)
{
	ec_wakeup_mask &= ~value;
}
EXPORT_SYMBOL_GPL(olpc_ec_wakeup_clear);

/*
 * Returns true if the compile and runtime configurations allow for EC events
 * to wake the system.
 */
bool olpc_ec_wakeup_available(void)
{
	if (!machine_is_olpc())
		return false;

	/*
	 * XO-1 EC wakeups are available when olpc-xo1-sci driver is
	 * compiled in
	 */
#ifdef CONFIG_OLPC_XO1_SCI
	if (olpc_platform_info.boardrev < olpc_board_pre(0xd0)) /* XO-1 */
		return true;
#endif

	/*
	 * XO-1.5 EC wakeups are available when olpc-xo15-sci driver is
	 * compiled in
	 */
#ifdef CONFIG_OLPC_XO15_SCI
	if (olpc_platform_info.boardrev >= olpc_board_pre(0xd0)) /* XO-1.5 */
		return true;
#endif

	return false;
}
EXPORT_SYMBOL_GPL(olpc_ec_wakeup_available);

int olpc_ec_mask_write(u16 bits)
{
	if (olpc_platform_info.flags & OLPC_F_EC_WIDE_SCI) {
		__be16 ec_word = cpu_to_be16(bits);
		return olpc_ec_cmd(EC_WRITE_EXT_SCI_MASK, (void *) &ec_word, 2,
				   NULL, 0);
	} else {
		unsigned char ec_byte = bits & 0xff;
		return olpc_ec_cmd(EC_WRITE_SCI_MASK, &ec_byte, 1, NULL, 0);
	}
}
EXPORT_SYMBOL_GPL(olpc_ec_mask_write);

int olpc_ec_sci_query(u16 *sci_value)
{
	int ret;

	if (olpc_platform_info.flags & OLPC_F_EC_WIDE_SCI) {
		__be16 ec_word;
		ret = olpc_ec_cmd(EC_EXT_SCI_QUERY,
			NULL, 0, (void *) &ec_word, 2);
		if (ret == 0)
			*sci_value = be16_to_cpu(ec_word);
	} else {
		unsigned char ec_byte;
		ret = olpc_ec_cmd(EC_SCI_QUERY, NULL, 0, &ec_byte, 1);
		if (ret == 0)
			*sci_value = ec_byte;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(olpc_ec_sci_query);

static int olpc_ec_suspend(void)
{
	return olpc_ec_mask_write(ec_wakeup_mask);
}

static bool __init check_ofw_architecture(struct device_node *root)
{
	const char *olpc_arch;
	int propsize;

	olpc_arch = of_get_property(root, "architecture", &propsize);
	return propsize == 5 && strncmp("OLPC", olpc_arch, 5) == 0;
}

static u32 __init get_board_revision(struct device_node *root)
{
	int propsize;
	const __be32 *rev;

	rev = of_get_property(root, "board-revision-int", &propsize);
	if (propsize != 4)
		return 0;

	return be32_to_cpu(*rev);
}

static bool __init platform_detect(void)
{
	struct device_node *root = of_find_node_by_path("/");
	bool success;

	if (!root)
		return false;

	success = check_ofw_architecture(root);
	if (success) {
		olpc_platform_info.boardrev = get_board_revision(root);
		olpc_platform_info.flags |= OLPC_F_PRESENT;
	}

	of_node_put(root);
	return success;
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

static struct syscore_ops olpc_syscore_ops = {
	.suspend = olpc_ec_suspend,
};

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
	/* EC version 0x5f adds support for wide SCI mask */
	if (olpc_platform_info.ecver >= 0x5f)
		olpc_platform_info.flags |= OLPC_F_EC_WIDE_SCI;

	printk(KERN_INFO "OLPC board revision %s%X (EC=%x)\n",
			((olpc_platform_info.boardrev & 0xf) < 8) ? "pre" : "",
			olpc_platform_info.boardrev >> 4,
			olpc_platform_info.ecver);

	if (olpc_platform_info.boardrev < olpc_board_pre(0xd0)) { /* XO-1 */
		r = add_xo1_platform_devices();
		if (r)
			return r;
	}

	register_syscore_ops(&olpc_syscore_ops);

	return 0;
}

postcore_initcall(olpc_init);
