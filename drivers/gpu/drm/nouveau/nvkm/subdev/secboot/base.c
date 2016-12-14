/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Secure boot is the process by which NVIDIA-signed firmware is loaded into
 * some of the falcons of a GPU. For production devices this is the only way
 * for the firmware to access useful (but sensitive) registers.
 *
 * A Falcon microprocessor supporting advanced security modes can run in one of
 * three modes:
 *
 * - Non-secure (NS). In this mode, functionality is similar to Falcon
 *   architectures before security modes were introduced (pre-Maxwell), but
 *   capability is restricted. In particular, certain registers may be
 *   inaccessible for reads and/or writes, and physical memory access may be
 *   disabled (on certain Falcon instances). This is the only possible mode that
 *   can be used if you don't have microcode cryptographically signed by NVIDIA.
 *
 * - Heavy Secure (HS). In this mode, the microprocessor is a black box - it's
 *   not possible to read or write any Falcon internal state or Falcon registers
 *   from outside the Falcon (for example, from the host system). The only way
 *   to enable this mode is by loading microcode that has been signed by NVIDIA.
 *   (The loading process involves tagging the IMEM block as secure, writing the
 *   signature into a Falcon register, and starting execution. The hardware will
 *   validate the signature, and if valid, grant HS privileges.)
 *
 * - Light Secure (LS). In this mode, the microprocessor has more privileges
 *   than NS but fewer than HS. Some of the microprocessor state is visible to
 *   host software to ease debugging. The only way to enable this mode is by HS
 *   microcode enabling LS mode. Some privileges available to HS mode are not
 *   available here. LS mode is introduced in GM20x.
 *
 * Secure boot consists in temporarily switching a HS-capable falcon (typically
 * PMU) into HS mode in order to validate the LS firmwares of managed falcons,
 * load them, and switch managed falcons into LS mode. Once secure boot
 * completes, no falcon remains in HS mode.
 *
 * Secure boot requires a write-protected memory region (WPR) which can only be
 * written by the secure falcon. On dGPU, the driver sets up the WPR region in
 * video memory. On Tegra, it is set up by the bootloader and its location and
 * size written into memory controller registers.
 *
 * The secure boot process takes place as follows:
 *
 * 1) A LS blob is constructed that contains all the LS firmwares we want to
 *    load, along with their signatures and bootloaders.
 *
 * 2) A HS blob (also called ACR) is created that contains the signed HS
 *    firmware in charge of loading the LS firmwares into their respective
 *    falcons.
 *
 * 3) The HS blob is loaded (via its own bootloader) and executed on the
 *    HS-capable falcon. It authenticates itself, switches the secure falcon to
 *    HS mode and setup the WPR region around the LS blob (dGPU) or copies the
 *    LS blob into the WPR region (Tegra).
 *
 * 4) The LS blob is now secure from all external tampering. The HS falcon
 *    checks the signatures of the LS firmwares and, if valid, switches the
 *    managed falcons to LS mode and makes them ready to run the LS firmware.
 *
 * 5) The managed falcons remain in LS mode and can be started.
 *
 */

#include "priv.h"
#include "acr.h"

#include <subdev/mc.h>
#include <subdev/timer.h>
#include <subdev/pmu.h>

static const char *
managed_falcons_names[] = {
	[NVKM_SECBOOT_FALCON_PMU] = "PMU",
	[NVKM_SECBOOT_FALCON_RESERVED] = "<reserved>",
	[NVKM_SECBOOT_FALCON_FECS] = "FECS",
	[NVKM_SECBOOT_FALCON_GPCCS] = "GPCCS",
	[NVKM_SECBOOT_FALCON_END] = "<invalid>",
};
/**
 * nvkm_secboot_reset() - reset specified falcon
 */
int
nvkm_secboot_reset(struct nvkm_secboot *sb, enum nvkm_secboot_falcon falcon)
{
	/* Unmanaged falcon? */
	if (!(BIT(falcon) & sb->acr->managed_falcons)) {
		nvkm_error(&sb->subdev, "cannot reset unmanaged falcon!\n");
		return -EINVAL;
	}

	return sb->acr->func->reset(sb->acr, sb, falcon);
}

/**
 * nvkm_secboot_is_managed() - check whether a given falcon is securely-managed
 */
bool
nvkm_secboot_is_managed(struct nvkm_secboot *sb, enum nvkm_secboot_falcon fid)
{
	if (!sb)
		return false;

	return sb->acr->managed_falcons & BIT(fid);
}

static int
nvkm_secboot_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_secboot *sb = nvkm_secboot(subdev);
	int ret = 0;

	switch (sb->acr->boot_falcon) {
	case NVKM_SECBOOT_FALCON_PMU:
		sb->boot_falcon = subdev->device->pmu->falcon;
		break;
	default:
		nvkm_error(subdev, "Unmanaged boot falcon %s!\n",
			   managed_falcons_names[sb->acr->boot_falcon]);
		return -EINVAL;
	}

	/* Call chip-specific init function */
	if (sb->func->oneinit)
		ret = sb->func->oneinit(sb);
	if (ret) {
		nvkm_error(subdev, "Secure Boot initialization failed: %d\n",
			   ret);
		return ret;
	}

	return 0;
}

static int
nvkm_secboot_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_secboot *sb = nvkm_secboot(subdev);
	int ret = 0;

	if (sb->func->fini)
		ret = sb->func->fini(sb, suspend);

	return ret;
}

static void *
nvkm_secboot_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_secboot *sb = nvkm_secboot(subdev);
	void *ret = NULL;

	if (sb->func->dtor)
		ret = sb->func->dtor(sb);

	return ret;
}

static const struct nvkm_subdev_func
nvkm_secboot = {
	.oneinit = nvkm_secboot_oneinit,
	.fini = nvkm_secboot_fini,
	.dtor = nvkm_secboot_dtor,
};

int
nvkm_secboot_ctor(const struct nvkm_secboot_func *func, struct nvkm_acr *acr,
		  struct nvkm_device *device, int index,
		  struct nvkm_secboot *sb)
{
	unsigned long fid;

	nvkm_subdev_ctor(&nvkm_secboot, device, index, &sb->subdev);
	sb->func = func;
	sb->acr = acr;
	acr->subdev = &sb->subdev;

	nvkm_debug(&sb->subdev, "securely managed falcons:\n");
	for_each_set_bit(fid, &sb->acr->managed_falcons,
			 NVKM_SECBOOT_FALCON_END)
		nvkm_debug(&sb->subdev, "- %s\n", managed_falcons_names[fid]);

	return 0;
}
