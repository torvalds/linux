/* mga_warp.c -- Matrox G200/G400 WARP engine management -*- linux-c -*-
 * Created: Thu Jan 11 21:29:32 2001 by gareth@valinux.com
 *
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Gareth Hughes <gareth@valinux.com>
 */

#include <linux/firmware.h>
#include <linux/ihex.h>
#include <linux/platform_device.h>

#include "drmP.h"
#include "drm.h"
#include "mga_drm.h"
#include "mga_drv.h"

#define FIRMWARE_G200 "matrox/g200_warp.fw"
#define FIRMWARE_G400 "matrox/g400_warp.fw"

MODULE_FIRMWARE(FIRMWARE_G200);
MODULE_FIRMWARE(FIRMWARE_G400);

#define MGA_WARP_CODE_ALIGN		256	/* in bytes */

#define WARP_UCODE_SIZE(size)		ALIGN(size, MGA_WARP_CODE_ALIGN)

int mga_warp_install_microcode(drm_mga_private_t * dev_priv)
{
	unsigned char *vcbase = dev_priv->warp->handle;
	unsigned long pcbase = dev_priv->warp->offset;
	const char *firmware_name;
	struct platform_device *pdev;
	const struct firmware *fw = NULL;
	const struct ihex_binrec *rec;
	unsigned int size;
	int n_pipes, where;
	int rc = 0;

	switch (dev_priv->chipset) {
	case MGA_CARD_TYPE_G400:
	case MGA_CARD_TYPE_G550:
		firmware_name = FIRMWARE_G400;
		n_pipes = MGA_MAX_G400_PIPES;
		break;
	case MGA_CARD_TYPE_G200:
		firmware_name = FIRMWARE_G200;
		n_pipes = MGA_MAX_G200_PIPES;
		break;
	default:
		return -EINVAL;
	}

	pdev = platform_device_register_simple("mga_warp", 0, NULL, 0);
	if (IS_ERR(pdev)) {
		DRM_ERROR("mga: Failed to register microcode\n");
		return PTR_ERR(pdev);
	}
	rc = request_ihex_firmware(&fw, firmware_name, &pdev->dev);
	platform_device_unregister(pdev);
	if (rc) {
		DRM_ERROR("mga: Failed to load microcode \"%s\"\n",
			  firmware_name);
		return rc;
	}

	size = 0;
	where = 0;
	for (rec = (const struct ihex_binrec *)fw->data;
	     rec;
	     rec = ihex_next_binrec(rec)) {
		size += WARP_UCODE_SIZE(be16_to_cpu(rec->len));
		where++;
	}

	if (where != n_pipes) {
		DRM_ERROR("mga: Invalid microcode \"%s\"\n", firmware_name);
		rc = -EINVAL;
		goto out;
	}
	size = PAGE_ALIGN(size);
	DRM_DEBUG("MGA ucode size = %d bytes\n", size);
	if (size > dev_priv->warp->size) {
		DRM_ERROR("microcode too large! (%u > %lu)\n",
			  size, dev_priv->warp->size);
		rc = -ENOMEM;
		goto out;
	}

	memset(dev_priv->warp_pipe_phys, 0, sizeof(dev_priv->warp_pipe_phys));

	where = 0;
	for (rec = (const struct ihex_binrec *)fw->data;
	     rec;
	     rec = ihex_next_binrec(rec)) {
		unsigned int src_size, dst_size;

		DRM_DEBUG(" pcbase = 0x%08lx  vcbase = %p\n", pcbase, vcbase);
		dev_priv->warp_pipe_phys[where] = pcbase;
		src_size = be16_to_cpu(rec->len);
		dst_size = WARP_UCODE_SIZE(src_size);
		memcpy(vcbase, rec->data, src_size);
		pcbase += dst_size;
		vcbase += dst_size;
		where++;
	}

out:
	release_firmware(fw);
	return rc;
}

#define WMISC_EXPECTED		(MGA_WUCODECACHE_ENABLE | MGA_WMASTER_ENABLE)

int mga_warp_init(drm_mga_private_t * dev_priv)
{
	u32 wmisc;

	/* FIXME: Get rid of these damned magic numbers...
	 */
	switch (dev_priv->chipset) {
	case MGA_CARD_TYPE_G400:
	case MGA_CARD_TYPE_G550:
		MGA_WRITE(MGA_WIADDR2, MGA_WMODE_SUSPEND);
		MGA_WRITE(MGA_WGETMSB, 0x00000E00);
		MGA_WRITE(MGA_WVRTXSZ, 0x00001807);
		MGA_WRITE(MGA_WACCEPTSEQ, 0x18000000);
		break;
	case MGA_CARD_TYPE_G200:
		MGA_WRITE(MGA_WIADDR, MGA_WMODE_SUSPEND);
		MGA_WRITE(MGA_WGETMSB, 0x1606);
		MGA_WRITE(MGA_WVRTXSZ, 7);
		break;
	default:
		return -EINVAL;
	}

	MGA_WRITE(MGA_WMISC, (MGA_WUCODECACHE_ENABLE |
			      MGA_WMASTER_ENABLE | MGA_WCACHEFLUSH_ENABLE));
	wmisc = MGA_READ(MGA_WMISC);
	if (wmisc != WMISC_EXPECTED) {
		DRM_ERROR("WARP engine config failed! 0x%x != 0x%x\n",
			  wmisc, WMISC_EXPECTED);
		return -EINVAL;
	}

	return 0;
}
