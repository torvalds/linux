/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <rm/rm.h>

#include <engine/disp.h>
#include <engine/disp/outp.h>

#include "nvhw/drf.h"

#include "nvrm/disp.h"

static int
r570_dmac_alloc(struct nvkm_disp *disp, u32 oclass, int inst, u32 put_offset,
		struct nvkm_gsp_object *dmac)
{
	NV50VAIO_CHANNELDMA_ALLOCATION_PARAMETERS *args;

	args = nvkm_gsp_rm_alloc_get(&disp->rm.object, (oclass << 16) | inst, oclass,
				     sizeof(*args), dmac);
	if (IS_ERR(args))
		return PTR_ERR(args);

	args->channelInstance = inst;
	args->offset = put_offset;
	args->subDeviceId = BIT(0);

	return nvkm_gsp_rm_alloc_wr(dmac, args);
}

static int
r570_disp_chan_set_pushbuf(struct nvkm_disp *disp, s32 oclass, int inst, struct nvkm_memory *memory)
{
	struct nvkm_gsp *gsp = disp->rm.objcom.client->gsp;
	NV2080_CTRL_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER_PARAMS *ctrl;

	ctrl = nvkm_gsp_rm_ctrl_get(&gsp->internal.device.subdevice,
				    NV2080_CTRL_CMD_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER,
				    sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	if (memory) {
		switch (nvkm_memory_target(memory)) {
		case NVKM_MEM_TARGET_NCOH:
			ctrl->addressSpace = ADDR_SYSMEM;
			ctrl->cacheSnoop = 0;
			ctrl->pbTargetAperture = PHYS_PCI;
			break;
		case NVKM_MEM_TARGET_HOST:
			ctrl->addressSpace = ADDR_SYSMEM;
			ctrl->cacheSnoop = 1;
			ctrl->pbTargetAperture = PHYS_PCI_COHERENT;
			break;
		case NVKM_MEM_TARGET_VRAM:
			ctrl->addressSpace = ADDR_FBMEM;
			ctrl->pbTargetAperture = PHYS_NVM;
			break;
		default:
			WARN_ON(1);
			return -EINVAL;
		}

		ctrl->physicalAddr = nvkm_memory_addr(memory);
		ctrl->limit = nvkm_memory_size(memory) - 1;
	}

	ctrl->hclass = oclass;
	ctrl->channelInstance = inst;
	ctrl->valid = ((oclass & 0xff) != 0x7a) ? 1 : 0;
	ctrl->subDeviceId = BIT(0);

	return nvkm_gsp_rm_ctrl_wr(&gsp->internal.device.subdevice, ctrl);
}

static int
r570_dp_set_indexed_link_rates(struct nvkm_outp *outp)
{
	NV0073_CTRL_CMD_DP_CONFIG_INDEXED_LINK_RATES_PARAMS *ctrl;
	struct nvkm_disp *disp = outp->disp;

	if (WARN_ON(outp->dp.rates > ARRAY_SIZE(ctrl->linkRateTbl)))
		return -EINVAL;

	ctrl = nvkm_gsp_rm_ctrl_get(&disp->rm.objcom,
				    NV0073_CTRL_CMD_DP_CONFIG_INDEXED_LINK_RATES, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->displayId = BIT(outp->index);
	for (int i = 0; i < outp->dp.rates; i++)
		ctrl->linkRateTbl[outp->dp.rate[i].dpcd] = outp->dp.rate[i].rate * 10 / 200;

	return nvkm_gsp_rm_ctrl_wr(&disp->rm.objcom, ctrl);
}

static int
r570_dp_get_caps(struct nvkm_disp *disp, int *plink_bw, bool *pmst, bool *pwm)
{
	NV0073_CTRL_CMD_DP_GET_CAPS_PARAMS *ctrl;
	int ret;

	ctrl = nvkm_gsp_rm_ctrl_get(&disp->rm.objcom,
				    NV0073_CTRL_CMD_DP_GET_CAPS, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->sorIndex = ~0;

	ret = nvkm_gsp_rm_ctrl_push(&disp->rm.objcom, &ctrl, sizeof(*ctrl));
	if (ret) {
		nvkm_gsp_rm_ctrl_done(&disp->rm.objcom, ctrl);
		return ret;
	}

	switch (NVVAL_GET(ctrl->maxLinkRate, NV0073_CTRL_CMD, DP_GET_CAPS, MAX_LINK_RATE)) {
	case NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_1_62:
		*plink_bw = 0x06;
		break;
	case NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_2_70:
		*plink_bw = 0x0a;
		break;
	case NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_5_40:
		*plink_bw = 0x14;
		break;
	case NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_8_10:
		*plink_bw = 0x1e;
		break;
	default:
		*plink_bw = 0x00;
		break;
	}

	*pmst = ctrl->bIsMultistreamSupported;
	*pwm = ctrl->bHasIncreasedWatermarkLimits;
	nvkm_gsp_rm_ctrl_done(&disp->rm.objcom, ctrl);
	return 0;
}

static int
r570_bl_ctrl(struct nvkm_disp *disp, unsigned display_id, bool set, int *pval)
{
	u32 cmd = set ? NV0073_CTRL_CMD_SPECIFIC_SET_BACKLIGHT_BRIGHTNESS :
			NV0073_CTRL_CMD_SPECIFIC_GET_BACKLIGHT_BRIGHTNESS;
	NV0073_CTRL_SPECIFIC_BACKLIGHT_BRIGHTNESS_PARAMS *ctrl;
	int ret;

	ctrl = nvkm_gsp_rm_ctrl_get(&disp->rm.objcom, cmd, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->displayId = BIT(display_id);
	ctrl->brightness = *pval;
	ctrl->brightnessType = NV0073_CTRL_SPECIFIC_BACKLIGHT_BRIGHTNESS_TYPE_PERCENT100;

	ret = nvkm_gsp_rm_ctrl_push(&disp->rm.objcom, &ctrl, sizeof(*ctrl));
	if (ret)
		return ret;

	*pval = ctrl->brightness;

	nvkm_gsp_rm_ctrl_done(&disp->rm.objcom, ctrl);
	return 0;
}

static int
r570_disp_get_active(struct nvkm_disp *disp, unsigned head, u32 *displayid)
{
	NV0073_CTRL_SYSTEM_GET_ACTIVE_PARAMS *ctrl;
	int ret;

	ctrl = nvkm_gsp_rm_ctrl_get(&disp->rm.objcom,
				    NV0073_CTRL_CMD_SYSTEM_GET_ACTIVE, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->subDeviceInstance = 0;
	ctrl->head = head;

	ret = nvkm_gsp_rm_ctrl_push(&disp->rm.objcom, &ctrl, sizeof(*ctrl));
	if (ret) {
		nvkm_gsp_rm_ctrl_done(&disp->rm.objcom, ctrl);
		return ret;
	}

	*displayid = ctrl->displayId;
	nvkm_gsp_rm_ctrl_done(&disp->rm.objcom, ctrl);
	return 0;
}
static int
r570_disp_get_connect_state(struct nvkm_disp *disp, unsigned display_id)
{
	NV0073_CTRL_SYSTEM_GET_CONNECT_STATE_PARAMS *ctrl;
	int ret;

	ctrl = nvkm_gsp_rm_ctrl_get(&disp->rm.objcom,
				    NV0073_CTRL_CMD_SYSTEM_GET_CONNECT_STATE, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	ctrl->subDeviceInstance = 0;
	ctrl->displayMask = BIT(display_id);

	ret = nvkm_gsp_rm_ctrl_push(&disp->rm.objcom, &ctrl, sizeof(*ctrl));
	if (ret == 0 && (ctrl->displayMask & BIT(display_id)))
		ret = 1;

	nvkm_gsp_rm_ctrl_done(&disp->rm.objcom, ctrl);
	return ret;
}

static int
r570_disp_get_supported(struct nvkm_disp *disp, unsigned long *pmask)
{
	NV0073_CTRL_SYSTEM_GET_SUPPORTED_PARAMS *ctrl;

	ctrl = nvkm_gsp_rm_ctrl_rd(&disp->rm.objcom,
				   NV0073_CTRL_CMD_SYSTEM_GET_SUPPORTED, sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	*pmask = ctrl->displayMask;

	nvkm_gsp_rm_ctrl_done(&disp->rm.objcom, ctrl);
	return 0;
}

static int
r570_disp_get_static_info(struct nvkm_disp *disp)
{
	NV2080_CTRL_INTERNAL_DISPLAY_GET_STATIC_INFO_PARAMS *ctrl;
	struct nvkm_gsp *gsp = disp->engine.subdev.device->gsp;

	ctrl = nvkm_gsp_rm_ctrl_rd(&gsp->internal.device.subdevice,
				   NV2080_CTRL_CMD_INTERNAL_DISPLAY_GET_STATIC_INFO,
				   sizeof(*ctrl));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	disp->wndw.mask = ctrl->windowPresentMask;
	disp->wndw.nr = fls(disp->wndw.mask);

	nvkm_gsp_rm_ctrl_done(&gsp->internal.device.subdevice, ctrl);
	return 0;
}

const struct nvkm_rm_api_disp
r570_disp = {
	.get_static_info = r570_disp_get_static_info,
	.get_supported = r570_disp_get_supported,
	.get_connect_state = r570_disp_get_connect_state,
	.get_active = r570_disp_get_active,
	.bl_ctrl = r570_bl_ctrl,
	.dp = {
		.get_caps = r570_dp_get_caps,
		.set_indexed_link_rates = r570_dp_set_indexed_link_rates,
	},
	.chan = {
		.set_pushbuf = r570_disp_chan_set_pushbuf,
		.dmac_alloc = r570_dmac_alloc,
	},
};
