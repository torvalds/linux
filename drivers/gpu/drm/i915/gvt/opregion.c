/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/acpi.h>
#include "i915_drv.h"
#include "gvt.h"

static int init_vgpu_opregion(struct intel_vgpu *vgpu, u32 gpa)
{
	void __iomem *host_va = vgpu->gvt->opregion.opregion_va;
	u8 *buf;
	int i;

	if (WARN((vgpu_opregion(vgpu)->va),
			"vgpu%d: opregion has been initialized already.\n",
			vgpu->id))
		return -EINVAL;

	vgpu_opregion(vgpu)->va = (void *)__get_free_pages(GFP_ATOMIC |
			GFP_DMA32 | __GFP_ZERO,
			INTEL_GVT_OPREGION_PORDER);

	if (!vgpu_opregion(vgpu)->va)
		return -ENOMEM;

	memcpy_fromio(vgpu_opregion(vgpu)->va, host_va,
			INTEL_GVT_OPREGION_SIZE);

	for (i = 0; i < INTEL_GVT_OPREGION_PAGES; i++)
		vgpu_opregion(vgpu)->gfn[i] = (gpa >> PAGE_SHIFT) + i;

	/* for unknown reason, the value in LID field is incorrect
	 * which block the windows guest, so workaround it by force
	 * setting it to "OPEN"
	 */
	buf = (u8 *)vgpu_opregion(vgpu)->va;
	buf[INTEL_GVT_OPREGION_CLID] = 0x3;

	return 0;
}

static int map_vgpu_opregion(struct intel_vgpu *vgpu, bool map)
{
	u64 mfn;
	int i, ret;

	for (i = 0; i < INTEL_GVT_OPREGION_PAGES; i++) {
		mfn = intel_gvt_hypervisor_virt_to_mfn(vgpu_opregion(vgpu)
			+ i * PAGE_SIZE);
		if (mfn == INTEL_GVT_INVALID_ADDR) {
			gvt_err("fail to get MFN from VA\n");
			return -EINVAL;
		}
		ret = intel_gvt_hypervisor_map_gfn_to_mfn(vgpu,
				vgpu_opregion(vgpu)->gfn[i],
				mfn, 1, map, GVT_MAP_OPREGION);
		if (ret) {
			gvt_err("fail to map GFN to MFN, errno: %d\n", ret);
			return ret;
		}
	}
	return 0;
}

/**
 * intel_vgpu_clean_opregion - clean the stuff used to emulate opregion
 * @vgpu: a vGPU
 *
 */
void intel_vgpu_clean_opregion(struct intel_vgpu *vgpu)
{
	int i;

	gvt_dbg_core("vgpu%d: clean vgpu opregion\n", vgpu->id);

	if (!vgpu_opregion(vgpu)->va)
		return;

	if (intel_gvt_host.hypervisor_type == INTEL_GVT_HYPERVISOR_KVM) {
		vunmap(vgpu_opregion(vgpu)->va);
		for (i = 0; i < INTEL_GVT_OPREGION_PAGES; i++) {
			if (vgpu_opregion(vgpu)->pages[i]) {
				put_page(vgpu_opregion(vgpu)->pages[i]);
				vgpu_opregion(vgpu)->pages[i] = NULL;
			}
		}
	} else {
		map_vgpu_opregion(vgpu, false);
		free_pages((unsigned long)vgpu_opregion(vgpu)->va,
				INTEL_GVT_OPREGION_PORDER);
	}

	vgpu_opregion(vgpu)->va = NULL;
}

/**
 * intel_vgpu_init_opregion - initialize the stuff used to emulate opregion
 * @vgpu: a vGPU
 * @gpa: guest physical address of opregion
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_vgpu_init_opregion(struct intel_vgpu *vgpu, u32 gpa)
{
	int ret;

	gvt_dbg_core("vgpu%d: init vgpu opregion\n", vgpu->id);

	if (intel_gvt_host.hypervisor_type == INTEL_GVT_HYPERVISOR_XEN) {
		gvt_dbg_core("emulate opregion from kernel\n");

		ret = init_vgpu_opregion(vgpu, gpa);
		if (ret)
			return ret;

		ret = map_vgpu_opregion(vgpu, true);
		if (ret)
			return ret;
	} else {
		gvt_dbg_core("emulate opregion from userspace\n");

		/*
		 * If opregion pages are not allocated from host kenrel,
		 * most of the params are meaningless
		 */
		ret = intel_gvt_hypervisor_map_gfn_to_mfn(vgpu,
				0, /* not used */
				0, /* not used */
				2, /* not used */
				1,
				GVT_MAP_OPREGION);
		if (ret)
			return ret;
	}
	return 0;
}

/**
 * intel_gvt_clean_opregion - clean host opergion related stuffs
 * @gvt: a GVT device
 *
 */
void intel_gvt_clean_opregion(struct intel_gvt *gvt)
{
	iounmap(gvt->opregion.opregion_va);
	gvt->opregion.opregion_va = NULL;
}

/**
 * intel_gvt_init_opregion - initialize host opergion related stuffs
 * @gvt: a GVT device
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_gvt_init_opregion(struct intel_gvt *gvt)
{
	gvt_dbg_core("init host opregion\n");

	pci_read_config_dword(gvt->dev_priv->drm.pdev, INTEL_GVT_PCI_OPREGION,
			&gvt->opregion.opregion_pa);

	gvt->opregion.opregion_va = acpi_os_ioremap(gvt->opregion.opregion_pa,
			INTEL_GVT_OPREGION_SIZE);
	if (!gvt->opregion.opregion_va) {
		gvt_err("fail to map host opregion\n");
		return -EFAULT;
	}
	return 0;
}

#define GVT_OPREGION_FUNC(scic)					\
	({							\
	 u32 __ret;						\
	 __ret = (scic & OPREGION_SCIC_FUNC_MASK) >>		\
	 OPREGION_SCIC_FUNC_SHIFT;				\
	 __ret;							\
	 })

#define GVT_OPREGION_SUBFUNC(scic)				\
	({							\
	 u32 __ret;						\
	 __ret = (scic & OPREGION_SCIC_SUBFUNC_MASK) >>		\
	 OPREGION_SCIC_SUBFUNC_SHIFT;				\
	 __ret;							\
	 })

static const char *opregion_func_name(u32 func)
{
	const char *name = NULL;

	switch (func) {
	case 0 ... 3:
	case 5:
	case 7 ... 15:
		name = "Reserved";
		break;

	case 4:
		name = "Get BIOS Data";
		break;

	case 6:
		name = "System BIOS Callbacks";
		break;

	default:
		name = "Unknown";
		break;
	}
	return name;
}

static const char *opregion_subfunc_name(u32 subfunc)
{
	const char *name = NULL;

	switch (subfunc) {
	case 0:
		name = "Supported Calls";
		break;

	case 1:
		name = "Requested Callbacks";
		break;

	case 2 ... 3:
	case 8 ... 9:
		name = "Reserved";
		break;

	case 5:
		name = "Boot Display";
		break;

	case 6:
		name = "TV-Standard/Video-Connector";
		break;

	case 7:
		name = "Internal Graphics";
		break;

	case 10:
		name = "Spread Spectrum Clocks";
		break;

	case 11:
		name = "Get AKSV";
		break;

	default:
		name = "Unknown";
		break;
	}
	return name;
};

static bool querying_capabilities(u32 scic)
{
	u32 func, subfunc;

	func = GVT_OPREGION_FUNC(scic);
	subfunc = GVT_OPREGION_SUBFUNC(scic);

	if ((func == INTEL_GVT_OPREGION_SCIC_F_GETBIOSDATA &&
		subfunc == INTEL_GVT_OPREGION_SCIC_SF_SUPPRTEDCALLS)
		|| (func == INTEL_GVT_OPREGION_SCIC_F_GETBIOSDATA &&
		 subfunc == INTEL_GVT_OPREGION_SCIC_SF_REQEUSTEDCALLBACKS)
		|| (func == INTEL_GVT_OPREGION_SCIC_F_GETBIOSCALLBACKS &&
		 subfunc == INTEL_GVT_OPREGION_SCIC_SF_SUPPRTEDCALLS)) {
		return true;
	}
	return false;
}

/**
 * intel_vgpu_emulate_opregion_request - emulating OpRegion request
 * @vgpu: a vGPU
 * @swsci: SWSCI request
 *
 * Returns:
 * Zero on success, negative error code if failed
 */
int intel_vgpu_emulate_opregion_request(struct intel_vgpu *vgpu, u32 swsci)
{
	u32 *scic, *parm;
	u32 func, subfunc;

	scic = vgpu_opregion(vgpu)->va + INTEL_GVT_OPREGION_SCIC;
	parm = vgpu_opregion(vgpu)->va + INTEL_GVT_OPREGION_PARM;

	if (!(swsci & SWSCI_SCI_SELECT)) {
		gvt_err("vgpu%d: requesting SMI service\n", vgpu->id);
		return 0;
	}
	/* ignore non 0->1 trasitions */
	if ((vgpu_cfg_space(vgpu)[INTEL_GVT_PCI_SWSCI]
				& SWSCI_SCI_TRIGGER) ||
			!(swsci & SWSCI_SCI_TRIGGER)) {
		return 0;
	}

	func = GVT_OPREGION_FUNC(*scic);
	subfunc = GVT_OPREGION_SUBFUNC(*scic);
	if (!querying_capabilities(*scic)) {
		gvt_err("vgpu%d: requesting runtime service: func \"%s\","
				" subfunc \"%s\"\n",
				vgpu->id,
				opregion_func_name(func),
				opregion_subfunc_name(subfunc));
		/*
		 * emulate exit status of function call, '0' means
		 * "failure, generic, unsupported or unknown cause"
		 */
		*scic &= ~OPREGION_SCIC_EXIT_MASK;
		return 0;
	}

	*scic = 0;
	*parm = 0;
	return 0;
}
