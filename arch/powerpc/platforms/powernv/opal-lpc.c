// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PowerNV LPC bus handling.
 *
 * Copyright 2013 IBM Corp.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/bug.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/debugfs.h>

#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/opal.h>
#include <asm/prom.h>
#include <linux/uaccess.h>
#include <asm/isa-bridge.h>

static int opal_lpc_chip_id = -1;

static u8 opal_lpc_inb(unsigned long port)
{
	int64_t rc;
	__be32 data;

	if (opal_lpc_chip_id < 0 || port > 0xffff)
		return 0xff;
	rc = opal_lpc_read(opal_lpc_chip_id, OPAL_LPC_IO, port, &data, 1);
	return rc ? 0xff : be32_to_cpu(data);
}

static __le16 __opal_lpc_inw(unsigned long port)
{
	int64_t rc;
	__be32 data;

	if (opal_lpc_chip_id < 0 || port > 0xfffe)
		return 0xffff;
	if (port & 1)
		return (__le16)opal_lpc_inb(port) << 8 | opal_lpc_inb(port + 1);
	rc = opal_lpc_read(opal_lpc_chip_id, OPAL_LPC_IO, port, &data, 2);
	return rc ? 0xffff : be32_to_cpu(data);
}
static u16 opal_lpc_inw(unsigned long port)
{
	return le16_to_cpu(__opal_lpc_inw(port));
}

static __le32 __opal_lpc_inl(unsigned long port)
{
	int64_t rc;
	__be32 data;

	if (opal_lpc_chip_id < 0 || port > 0xfffc)
		return 0xffffffff;
	if (port & 3)
		return (__le32)opal_lpc_inb(port    ) << 24 |
		       (__le32)opal_lpc_inb(port + 1) << 16 |
		       (__le32)opal_lpc_inb(port + 2) <<  8 |
			       opal_lpc_inb(port + 3);
	rc = opal_lpc_read(opal_lpc_chip_id, OPAL_LPC_IO, port, &data, 4);
	return rc ? 0xffffffff : be32_to_cpu(data);
}

static u32 opal_lpc_inl(unsigned long port)
{
	return le32_to_cpu(__opal_lpc_inl(port));
}

static void opal_lpc_outb(u8 val, unsigned long port)
{
	if (opal_lpc_chip_id < 0 || port > 0xffff)
		return;
	opal_lpc_write(opal_lpc_chip_id, OPAL_LPC_IO, port, val, 1);
}

static void __opal_lpc_outw(__le16 val, unsigned long port)
{
	if (opal_lpc_chip_id < 0 || port > 0xfffe)
		return;
	if (port & 1) {
		opal_lpc_outb(val >> 8, port);
		opal_lpc_outb(val     , port + 1);
		return;
	}
	opal_lpc_write(opal_lpc_chip_id, OPAL_LPC_IO, port, val, 2);
}

static void opal_lpc_outw(u16 val, unsigned long port)
{
	__opal_lpc_outw(cpu_to_le16(val), port);
}

static void __opal_lpc_outl(__le32 val, unsigned long port)
{
	if (opal_lpc_chip_id < 0 || port > 0xfffc)
		return;
	if (port & 3) {
		opal_lpc_outb(val >> 24, port);
		opal_lpc_outb(val >> 16, port + 1);
		opal_lpc_outb(val >>  8, port + 2);
		opal_lpc_outb(val      , port + 3);
		return;
	}
	opal_lpc_write(opal_lpc_chip_id, OPAL_LPC_IO, port, val, 4);
}

static void opal_lpc_outl(u32 val, unsigned long port)
{
	__opal_lpc_outl(cpu_to_le32(val), port);
}

static void opal_lpc_insb(unsigned long p, void *b, unsigned long c)
{
	u8 *ptr = b;

	while(c--)
		*(ptr++) = opal_lpc_inb(p);
}

static void opal_lpc_insw(unsigned long p, void *b, unsigned long c)
{
	__le16 *ptr = b;

	while(c--)
		*(ptr++) = __opal_lpc_inw(p);
}

static void opal_lpc_insl(unsigned long p, void *b, unsigned long c)
{
	__le32 *ptr = b;

	while(c--)
		*(ptr++) = __opal_lpc_inl(p);
}

static void opal_lpc_outsb(unsigned long p, const void *b, unsigned long c)
{
	const u8 *ptr = b;

	while(c--)
		opal_lpc_outb(*(ptr++), p);
}

static void opal_lpc_outsw(unsigned long p, const void *b, unsigned long c)
{
	const __le16 *ptr = b;

	while(c--)
		__opal_lpc_outw(*(ptr++), p);
}

static void opal_lpc_outsl(unsigned long p, const void *b, unsigned long c)
{
	const __le32 *ptr = b;

	while(c--)
		__opal_lpc_outl(*(ptr++), p);
}

static const struct ppc_pci_io opal_lpc_io = {
	.inb	= opal_lpc_inb,
	.inw	= opal_lpc_inw,
	.inl	= opal_lpc_inl,
	.outb	= opal_lpc_outb,
	.outw	= opal_lpc_outw,
	.outl	= opal_lpc_outl,
	.insb	= opal_lpc_insb,
	.insw	= opal_lpc_insw,
	.insl	= opal_lpc_insl,
	.outsb	= opal_lpc_outsb,
	.outsw	= opal_lpc_outsw,
	.outsl	= opal_lpc_outsl,
};

#ifdef CONFIG_DEBUG_FS
struct lpc_debugfs_entry {
	enum OpalLPCAddressType lpc_type;
};

static ssize_t lpc_debug_read(struct file *filp, char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	struct lpc_debugfs_entry *lpc = filp->private_data;
	u32 data, pos, len, todo;
	int rc;

	if (!access_ok(ubuf, count))
		return -EFAULT;

	todo = count;
	while (todo) {
		pos = *ppos;

		/*
		 * Select access size based on count and alignment and
		 * access type. IO and MEM only support byte accesses,
		 * FW supports all 3.
		 */
		len = 1;
		if (lpc->lpc_type == OPAL_LPC_FW) {
			if (todo > 3 && (pos & 3) == 0)
				len = 4;
			else if (todo > 1 && (pos & 1) == 0)
				len = 2;
		}
		rc = opal_lpc_read(opal_lpc_chip_id, lpc->lpc_type, pos,
				   &data, len);
		if (rc)
			return -ENXIO;

		/*
		 * Now there is some trickery with the data returned by OPAL
		 * as it's the desired data right justified in a 32-bit BE
		 * word.
		 *
		 * This is a very bad interface and I'm to blame for it :-(
		 *
		 * So we can't just apply a 32-bit swap to what comes from OPAL,
		 * because user space expects the *bytes* to be in their proper
		 * respective positions (ie, LPC position).
		 *
		 * So what we really want to do here is to shift data right
		 * appropriately on a LE kernel.
		 *
		 * IE. If the LPC transaction has bytes B0, B1, B2 and B3 in that
		 * order, we have in memory written to by OPAL at the "data"
		 * pointer:
		 *
		 *               Bytes:      OPAL "data"   LE "data"
		 *   32-bit:   B0 B1 B2 B3   B0B1B2B3      B3B2B1B0
		 *   16-bit:   B0 B1         0000B0B1      B1B00000
		 *    8-bit:   B0            000000B0      B0000000
		 *
		 * So a BE kernel will have the leftmost of the above in the MSB
		 * and rightmost in the LSB and can just then "cast" the u32 "data"
		 * down to the appropriate quantity and write it.
		 *
		 * However, an LE kernel can't. It doesn't need to swap because a
		 * load from data followed by a store to user are going to preserve
		 * the byte ordering which is the wire byte order which is what the
		 * user wants, but in order to "crop" to the right size, we need to
		 * shift right first.
		 */
		switch(len) {
		case 4:
			rc = __put_user((u32)data, (u32 __user *)ubuf);
			break;
		case 2:
#ifdef __LITTLE_ENDIAN__
			data >>= 16;
#endif
			rc = __put_user((u16)data, (u16 __user *)ubuf);
			break;
		default:
#ifdef __LITTLE_ENDIAN__
			data >>= 24;
#endif
			rc = __put_user((u8)data, (u8 __user *)ubuf);
			break;
		}
		if (rc)
			return -EFAULT;
		*ppos += len;
		ubuf += len;
		todo -= len;
	}

	return count;
}

static ssize_t lpc_debug_write(struct file *filp, const char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	struct lpc_debugfs_entry *lpc = filp->private_data;
	u32 data, pos, len, todo;
	int rc;

	if (!access_ok(ubuf, count))
		return -EFAULT;

	todo = count;
	while (todo) {
		pos = *ppos;

		/*
		 * Select access size based on count and alignment and
		 * access type. IO and MEM only support byte acceses,
		 * FW supports all 3.
		 */
		len = 1;
		if (lpc->lpc_type == OPAL_LPC_FW) {
			if (todo > 3 && (pos & 3) == 0)
				len = 4;
			else if (todo > 1 && (pos & 1) == 0)
				len = 2;
		}

		/*
		 * Similarly to the read case, we have some trickery here but
		 * it's different to handle. We need to pass the value to OPAL in
		 * a register whose layout depends on the access size. We want
		 * to reproduce the memory layout of the user, however we aren't
		 * doing a load from user and a store to another memory location
		 * which would achieve that. Here we pass the value to OPAL via
		 * a register which is expected to contain the "BE" interpretation
		 * of the byte sequence. IE: for a 32-bit access, byte 0 should be
		 * in the MSB. So here we *do* need to byteswap on LE.
		 *
		 *           User bytes:    LE "data"  OPAL "data"
		 *  32-bit:  B0 B1 B2 B3    B3B2B1B0   B0B1B2B3
		 *  16-bit:  B0 B1          0000B1B0   0000B0B1
		 *   8-bit:  B0             000000B0   000000B0
		 */
		switch(len) {
		case 4:
			rc = __get_user(data, (u32 __user *)ubuf);
			data = cpu_to_be32(data);
			break;
		case 2:
			rc = __get_user(data, (u16 __user *)ubuf);
			data = cpu_to_be16(data);
			break;
		default:
			rc = __get_user(data, (u8 __user *)ubuf);
			break;
		}
		if (rc)
			return -EFAULT;

		rc = opal_lpc_write(opal_lpc_chip_id, lpc->lpc_type, pos,
				    data, len);
		if (rc)
			return -ENXIO;
		*ppos += len;
		ubuf += len;
		todo -= len;
	}

	return count;
}

static const struct file_operations lpc_fops = {
	.read =		lpc_debug_read,
	.write =	lpc_debug_write,
	.open =		simple_open,
	.llseek =	default_llseek,
};

static int opal_lpc_debugfs_create_type(struct dentry *folder,
					const char *fname,
					enum OpalLPCAddressType type)
{
	struct lpc_debugfs_entry *entry;
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	entry->lpc_type = type;
	debugfs_create_file(fname, 0600, folder, entry, &lpc_fops);
	return 0;
}

static int opal_lpc_init_debugfs(void)
{
	struct dentry *root;
	int rc = 0;

	if (opal_lpc_chip_id < 0)
		return -ENODEV;

	root = debugfs_create_dir("lpc", arch_debugfs_dir);

	rc |= opal_lpc_debugfs_create_type(root, "io", OPAL_LPC_IO);
	rc |= opal_lpc_debugfs_create_type(root, "mem", OPAL_LPC_MEM);
	rc |= opal_lpc_debugfs_create_type(root, "fw", OPAL_LPC_FW);
	return rc;
}
machine_device_initcall(powernv, opal_lpc_init_debugfs);
#endif  /* CONFIG_DEBUG_FS */

void __init opal_lpc_init(void)
{
	struct device_node *np;

	/*
	 * Look for a Power8 LPC bus tagged as "primary",
	 * we currently support only one though the OPAL APIs
	 * support any number.
	 */
	for_each_compatible_node(np, NULL, "ibm,power8-lpc") {
		if (!of_device_is_available(np))
			continue;
		if (!of_property_present(np, "primary"))
			continue;
		opal_lpc_chip_id = of_get_ibm_chip_id(np);
		of_node_put(np);
		break;
	}
	if (opal_lpc_chip_id < 0)
		return;

	/* Does it support direct mapping ? */
	if (of_property_present(np, "ranges")) {
		pr_info("OPAL: Found memory mapped LPC bus on chip %d\n",
			opal_lpc_chip_id);
		isa_bridge_init_non_pci(np);
	} else {
		pr_info("OPAL: Found non-mapped LPC bus on chip %d\n",
			opal_lpc_chip_id);

		/* Setup special IO ops */
		ppc_pci_io = opal_lpc_io;
		isa_io_special = true;
	}
}
