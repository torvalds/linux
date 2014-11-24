/*
 * Copyright (c) 2014 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/uaccess.h>

#include "wil6210.h"
#include <uapi/linux/wil6210_uapi.h>

#define wil_hex_dump_ioctl(prefix_str, buf, len) \
	print_hex_dump_debug("DBG[IOC ]" prefix_str, \
			     DUMP_PREFIX_OFFSET, 16, 1, buf, len, true)
#define wil_dbg_ioctl(wil, fmt, arg...) wil_dbg(wil, "DBG[IOC ]" fmt, ##arg)

static void __iomem *wil_ioc_addr(struct wil6210_priv *wil, uint32_t addr,
				  uint32_t size, enum wil_memio_op op)
{
	void __iomem *a;
	u32 off;

	switch (op & wil_mmio_addr_mask) {
	case wil_mmio_addr_linker:
		a = wmi_buffer(wil, cpu_to_le32(addr));
		break;
	case wil_mmio_addr_ahb:
		a = wmi_addr(wil, addr);
		break;
	case wil_mmio_addr_bar:
		a = wmi_addr(wil, addr + WIL6210_FW_HOST_OFF);
		break;
	default:
		wil_err(wil, "Unsupported address mode, op = 0x%08x\n", op);
		return NULL;
	}

	off = a - wil->csr;
	if (size >= WIL6210_MEM_SIZE - off) {
		wil_err(wil, "Requested block does not fit into memory: "
			"off = 0x%08x size = 0x%08x\n", off, size);
		return NULL;
	}

	return a;
}

static int wil_ioc_memio_dword(struct wil6210_priv *wil, void __user *data)
{
	struct wil_memio io;
	void __iomem *a;
	bool need_copy = false;

	if (copy_from_user(&io, data, sizeof(io)))
		return -EFAULT;

	wil_dbg_ioctl(wil, "IO: addr = 0x%08x val = 0x%08x op = 0x%08x\n",
		      io.addr, io.val, io.op);

	a = wil_ioc_addr(wil, io.addr, sizeof(u32), io.op);
	if (!a) {
		wil_err(wil, "invalid address 0x%08x, op = 0x%08x\n", io.addr,
			io.op);
		return -EINVAL;
	}
	/* operation */
	switch (io.op & wil_mmio_op_mask) {
	case wil_mmio_read:
		io.val = ioread32(a);
		need_copy = true;
		break;
	case wil_mmio_write:
		iowrite32(io.val, a);
		wmb(); /* make sure write propagated to HW */
		break;
	default:
		wil_err(wil, "Unsupported operation, op = 0x%08x\n", io.op);
		return -EINVAL;
	}

	if (need_copy) {
		wil_dbg_ioctl(wil, "IO done: addr = 0x%08x"
			      " val = 0x%08x op = 0x%08x\n",
			      io.addr, io.val, io.op);
		if (copy_to_user(data, &io, sizeof(io)))
			return -EFAULT;
	}

	return 0;
}

static int wil_ioc_memio_block(struct wil6210_priv *wil, void __user *data)
{
	struct wil_memio_block io;
	void *block;
	void __iomem *a;
	int rc = 0;

	if (copy_from_user(&io, data, sizeof(io)))
		return -EFAULT;

	wil_dbg_ioctl(wil, "IO: addr = 0x%08x size = 0x%08x op = 0x%08x\n",
		      io.addr, io.size, io.op);

	/* size */
	if (io.size % 4) {
		wil_err(wil, "size is not multiple of 4:  0x%08x\n", io.size);
		return -EINVAL;
	}

	a = wil_ioc_addr(wil, io.addr, io.size, io.op);
	if (!a) {
		wil_err(wil, "invalid address 0x%08x, op = 0x%08x\n", io.addr,
			io.op);
		return -EINVAL;
	}

	block = kmalloc(io.size, GFP_USER);
	if (!block)
		return -ENOMEM;

	/* operation */
	switch (io.op & wil_mmio_op_mask) {
	case wil_mmio_read:
		wil_memcpy_fromio_32(block, a, io.size);
		wil_hex_dump_ioctl("Read  ", block, io.size);
		if (copy_to_user(io.block, block, io.size)) {
			rc = -EFAULT;
			goto out_free;
		}
		break;
	case wil_mmio_write:
		if (copy_from_user(block, io.block, io.size)) {
			rc = -EFAULT;
			goto out_free;
		}
		wil_memcpy_toio_32(a, block, io.size);
		wmb(); /* make sure write propagated to HW */
		wil_hex_dump_ioctl("Write ", block, io.size);
		break;
	default:
		wil_err(wil, "Unsupported operation, op = 0x%08x\n", io.op);
		rc = -EINVAL;
		break;
	}

out_free:
	kfree(block);
	return rc;
}

int wil_ioctl(struct wil6210_priv *wil, void __user *data, int cmd)
{
	switch (cmd) {
	case WIL_IOCTL_MEMIO:
		return wil_ioc_memio_dword(wil, data);
	case WIL_IOCTL_MEMIO_BLOCK:
		return wil_ioc_memio_block(wil, data);
	default:
		wil_dbg_ioctl(wil, "Unsupported IOCTL 0x%04x\n", cmd);
		return -ENOIOCTLCMD;
	}
}
