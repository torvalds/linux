/*
 * arch/arm/mach-netx/xc.c
 *
 * Copyright (c) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/netx-regs.h>

#include <mach/xc.h>

static DEFINE_MUTEX(xc_lock);

static int xc_in_use = 0;

struct fw_desc {
	unsigned int ofs;
	unsigned int size;
	unsigned int patch_ofs;
	unsigned int patch_entries;
};

struct fw_header {
	unsigned int magic;
	unsigned int type;
	unsigned int version;
	unsigned int reserved[5];
	struct fw_desc fw_desc[3];
} __attribute__ ((packed));

int xc_stop(struct xc *x)
{
	writel(RPU_HOLD_PC, x->xmac_base + NETX_XMAC_RPU_HOLD_PC_OFS);
	writel(TPU_HOLD_PC, x->xmac_base + NETX_XMAC_TPU_HOLD_PC_OFS);
	writel(XPU_HOLD_PC, x->xpec_base + NETX_XPEC_XPU_HOLD_PC_OFS);
	return 0;
}

int xc_start(struct xc *x)
{
	writel(0, x->xmac_base + NETX_XMAC_RPU_HOLD_PC_OFS);
	writel(0, x->xmac_base + NETX_XMAC_TPU_HOLD_PC_OFS);
	writel(0, x->xpec_base + NETX_XPEC_XPU_HOLD_PC_OFS);
	return 0;
}

int xc_running(struct xc *x)
{
	return (readl(x->xmac_base + NETX_XMAC_RPU_HOLD_PC_OFS) & RPU_HOLD_PC)
	    || (readl(x->xmac_base + NETX_XMAC_TPU_HOLD_PC_OFS) & TPU_HOLD_PC)
	    || (readl(x->xpec_base + NETX_XPEC_XPU_HOLD_PC_OFS) & XPU_HOLD_PC) ?
		0 : 1;
}

int xc_reset(struct xc *x)
{
	writel(0, x->xpec_base + NETX_XPEC_PC_OFS);
	return 0;
}

static int xc_check_ptr(struct xc *x, unsigned long adr, unsigned int size)
{
	if (adr >= NETX_PA_XMAC(x->no) &&
	    adr + size < NETX_PA_XMAC(x->no) + XMAC_MEM_SIZE)
		return 0;

	if (adr >= NETX_PA_XPEC(x->no) &&
	    adr + size < NETX_PA_XPEC(x->no) + XPEC_MEM_SIZE)
		return 0;

	dev_err(x->dev, "Illegal pointer in firmware found. aborting\n");

	return -1;
}

static int xc_patch(struct xc *x, const void *patch, int count)
{
	unsigned int val, adr;
	const unsigned int *data = patch;

	int i;
	for (i = 0; i < count; i++) {
		adr = *data++;
		val = *data++;
		if (xc_check_ptr(x, adr, 4) < 0)
			return -EINVAL;

		writel(val, (void __iomem *)io_p2v(adr));
	}
	return 0;
}

int xc_request_firmware(struct xc *x)
{
	int ret;
	char name[16];
	const struct firmware *fw;
	struct fw_header *head;
	unsigned int size;
	int i;
	const void *src;
	unsigned long dst;

	sprintf(name, "xc%d.bin", x->no);

	ret = request_firmware(&fw, name, x->dev);

	if (ret < 0) {
		dev_err(x->dev, "request_firmware failed\n");
		return ret;
	}

	head = (struct fw_header *)fw->data;
	if (head->magic != 0x4e657458) {
		if (head->magic == 0x5874654e) {
			dev_err(x->dev,
			    "firmware magic is 'XteN'. Endianess problems?\n");
			ret = -ENODEV;
			goto exit_release_firmware;
		}
		dev_err(x->dev, "unrecognized firmware magic 0x%08x\n",
			head->magic);
		ret = -ENODEV;
		goto exit_release_firmware;
	}

	x->type = head->type;
	x->version = head->version;

	ret = -EINVAL;

	for (i = 0; i < 3; i++) {
		src = fw->data + head->fw_desc[i].ofs;
		dst = *(unsigned int *)src;
		src += sizeof (unsigned int);
		size = head->fw_desc[i].size - sizeof (unsigned int);

		if (xc_check_ptr(x, dst, size))
			goto exit_release_firmware;

		memcpy((void *)io_p2v(dst), src, size);

		src = fw->data + head->fw_desc[i].patch_ofs;
		size = head->fw_desc[i].patch_entries;
		ret = xc_patch(x, src, size);
		if (ret < 0)
			goto exit_release_firmware;
	}

	ret = 0;

      exit_release_firmware:
	release_firmware(fw);

	return ret;
}

struct xc *request_xc(int xcno, struct device *dev)
{
	struct xc *x = NULL;

	mutex_lock(&xc_lock);

	if (xcno > 3)
		goto exit;
	if (xc_in_use & (1 << xcno))
		goto exit;

	x = kmalloc(sizeof (struct xc), GFP_KERNEL);
	if (!x)
		goto exit;

	if (!request_mem_region
	    (NETX_PA_XPEC(xcno), XPEC_MEM_SIZE, kobject_name(&dev->kobj)))
		goto exit_free;

	if (!request_mem_region
	    (NETX_PA_XMAC(xcno), XMAC_MEM_SIZE, kobject_name(&dev->kobj)))
		goto exit_release_1;

	if (!request_mem_region
	    (SRAM_INTERNAL_PHYS(xcno), SRAM_MEM_SIZE, kobject_name(&dev->kobj)))
		goto exit_release_2;

	x->xpec_base = (void * __iomem)io_p2v(NETX_PA_XPEC(xcno));
	x->xmac_base = (void * __iomem)io_p2v(NETX_PA_XMAC(xcno));
	x->sram_base = ioremap(SRAM_INTERNAL_PHYS(xcno), SRAM_MEM_SIZE);
	if (!x->sram_base)
		goto exit_release_3;

	x->irq = NETX_IRQ_XPEC(xcno);

	x->no = xcno;
	x->dev = dev;

	xc_in_use |= (1 << xcno);

	goto exit;

      exit_release_3:
	release_mem_region(SRAM_INTERNAL_PHYS(xcno), SRAM_MEM_SIZE);
      exit_release_2:
	release_mem_region(NETX_PA_XMAC(xcno), XMAC_MEM_SIZE);
      exit_release_1:
	release_mem_region(NETX_PA_XPEC(xcno), XPEC_MEM_SIZE);
      exit_free:
	kfree(x);
	x = NULL;
      exit:
	mutex_unlock(&xc_lock);
	return x;
}

void free_xc(struct xc *x)
{
	int xcno = x->no;

	mutex_lock(&xc_lock);

	iounmap(x->sram_base);
	release_mem_region(SRAM_INTERNAL_PHYS(xcno), SRAM_MEM_SIZE);
	release_mem_region(NETX_PA_XMAC(xcno), XMAC_MEM_SIZE);
	release_mem_region(NETX_PA_XPEC(xcno), XPEC_MEM_SIZE);
	xc_in_use &= ~(1 << x->no);
	kfree(x);

	mutex_unlock(&xc_lock);
}

EXPORT_SYMBOL(free_xc);
EXPORT_SYMBOL(request_xc);
EXPORT_SYMBOL(xc_request_firmware);
EXPORT_SYMBOL(xc_reset);
EXPORT_SYMBOL(xc_running);
EXPORT_SYMBOL(xc_start);
EXPORT_SYMBOL(xc_stop);
