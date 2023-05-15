// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
// Copyright(c) 2023 Intel Corporation. All rights reserved.

/*
 * Soundwire Intel ops for LunarLake
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include <sound/hda-mlink.h>
#include "cadence_master.h"
#include "bus.h"
#include "intel.h"

/*
 * shim vendor-specific (vs) ops
 */

static void intel_shim_vs_init(struct sdw_intel *sdw)
{
	void __iomem *shim_vs = sdw->link_res->shim_vs;
	u16 act = 0;

	u16p_replace_bits(&act, 0x1, SDW_SHIM2_INTEL_VS_ACTMCTL_DOAIS);
	act |= SDW_SHIM2_INTEL_VS_ACTMCTL_DACTQE;
	act |=  SDW_SHIM2_INTEL_VS_ACTMCTL_DODS;
	intel_writew(shim_vs, SDW_SHIM2_INTEL_VS_ACTMCTL, act);
	usleep_range(10, 15);
}

static int intel_shim_check_wake(struct sdw_intel *sdw)
{
	void __iomem *shim_vs;
	u16 wake_sts;

	shim_vs = sdw->link_res->shim_vs;
	wake_sts = intel_readw(shim_vs, SDW_SHIM2_INTEL_VS_WAKESTS);

	return wake_sts & SDW_SHIM2_INTEL_VS_WAKEEN_PWS;
}

static void intel_shim_wake(struct sdw_intel *sdw, bool wake_enable)
{
	void __iomem *shim_vs = sdw->link_res->shim_vs;
	u16 wake_en;
	u16 wake_sts;

	wake_en = intel_readw(shim_vs, SDW_SHIM2_INTEL_VS_WAKEEN);

	if (wake_enable) {
		/* Enable the wakeup */
		wake_en |= SDW_SHIM2_INTEL_VS_WAKEEN_PWE;
		intel_writew(shim_vs, SDW_SHIM2_INTEL_VS_WAKEEN, wake_en);
	} else {
		/* Disable the wake up interrupt */
		wake_en &= ~SDW_SHIM2_INTEL_VS_WAKEEN_PWE;
		intel_writew(shim_vs, SDW_SHIM2_INTEL_VS_WAKEEN, wake_en);

		/* Clear wake status (W1C) */
		wake_sts = intel_readw(shim_vs, SDW_SHIM2_INTEL_VS_WAKESTS);
		wake_sts |= SDW_SHIM2_INTEL_VS_WAKEEN_PWS;
		intel_writew(shim_vs, SDW_SHIM2_INTEL_VS_WAKESTS, wake_sts);
	}
}

static int intel_link_power_up(struct sdw_intel *sdw)
{
	struct sdw_bus *bus = &sdw->cdns.bus;
	struct sdw_master_prop *prop = &bus->prop;
	u32 *shim_mask = sdw->link_res->shim_mask;
	unsigned int link_id = sdw->instance;
	u32 syncprd;
	int ret;

	mutex_lock(sdw->link_res->shim_lock);

	if (!*shim_mask) {
		/* we first need to program the SyncPRD/CPU registers */
		dev_dbg(sdw->cdns.dev, "first link up, programming SYNCPRD\n");

		if (prop->mclk_freq % 6000000)
			syncprd = SDW_SHIM_SYNC_SYNCPRD_VAL_38_4;
		else
			syncprd = SDW_SHIM_SYNC_SYNCPRD_VAL_24;

		ret =  hdac_bus_eml_sdw_set_syncprd_unlocked(sdw->link_res->hbus, syncprd);
		if (ret < 0) {
			dev_err(sdw->cdns.dev, "%s: hdac_bus_eml_sdw_set_syncprd failed: %d\n",
				__func__, ret);
			goto out;
		}
	}

	ret = hdac_bus_eml_sdw_power_up_unlocked(sdw->link_res->hbus, link_id);
	if (ret < 0) {
		dev_err(sdw->cdns.dev, "%s: hdac_bus_eml_sdw_power_up failed: %d\n",
			__func__, ret);
		goto out;
	}

	if (!*shim_mask) {
		/* SYNCPU will change once link is active */
		ret =  hdac_bus_eml_sdw_wait_syncpu_unlocked(sdw->link_res->hbus);
		if (ret < 0) {
			dev_err(sdw->cdns.dev, "%s: hdac_bus_eml_sdw_wait_syncpu failed: %d\n",
				__func__, ret);
			goto out;
		}
	}

	*shim_mask |= BIT(link_id);

	sdw->cdns.link_up = true;

	intel_shim_vs_init(sdw);

out:
	mutex_unlock(sdw->link_res->shim_lock);

	return ret;
}

static int intel_link_power_down(struct sdw_intel *sdw)
{
	u32 *shim_mask = sdw->link_res->shim_mask;
	unsigned int link_id = sdw->instance;
	int ret;

	mutex_lock(sdw->link_res->shim_lock);

	sdw->cdns.link_up = false;

	*shim_mask &= ~BIT(link_id);

	ret = hdac_bus_eml_sdw_power_down_unlocked(sdw->link_res->hbus, link_id);
	if (ret < 0) {
		dev_err(sdw->cdns.dev, "%s: hdac_bus_eml_sdw_power_down failed: %d\n",
			__func__, ret);

		/*
		 * we leave the sdw->cdns.link_up flag as false since we've disabled
		 * the link at this point and cannot handle interrupts any longer.
		 */
	}

	mutex_unlock(sdw->link_res->shim_lock);

	return ret;
}

static void intel_sync_arm(struct sdw_intel *sdw)
{
	unsigned int link_id = sdw->instance;

	mutex_lock(sdw->link_res->shim_lock);

	hdac_bus_eml_sdw_sync_arm_unlocked(sdw->link_res->hbus, link_id);

	mutex_unlock(sdw->link_res->shim_lock);
}

static int intel_sync_go_unlocked(struct sdw_intel *sdw)
{
	int ret;

	ret = hdac_bus_eml_sdw_sync_go_unlocked(sdw->link_res->hbus);
	if (ret < 0)
		dev_err(sdw->cdns.dev, "%s: SyncGO clear failed: %d\n", __func__, ret);

	return ret;
}

static int intel_sync_go(struct sdw_intel *sdw)
{
	int ret;

	mutex_lock(sdw->link_res->shim_lock);

	ret = intel_sync_go_unlocked(sdw);

	mutex_unlock(sdw->link_res->shim_lock);

	return ret;
}

/*
 * DAI operations
 */
static const struct snd_soc_dai_ops intel_pcm_dai_ops = {
};

static const struct snd_soc_component_driver dai_component = {
	.name			= "soundwire",
};

/*
 * PDI routines
 */
static void intel_pdi_init(struct sdw_intel *sdw,
			   struct sdw_cdns_stream_config *config)
{
	void __iomem *shim = sdw->link_res->shim;
	int pcm_cap;

	/* PCM Stream Capability */
	pcm_cap = intel_readw(shim, SDW_SHIM2_PCMSCAP);

	config->pcm_bd = FIELD_GET(SDW_SHIM2_PCMSCAP_BSS, pcm_cap);
	config->pcm_in = FIELD_GET(SDW_SHIM2_PCMSCAP_ISS, pcm_cap);
	config->pcm_out = FIELD_GET(SDW_SHIM2_PCMSCAP_ISS, pcm_cap);

	dev_dbg(sdw->cdns.dev, "PCM cap bd:%d in:%d out:%d\n",
		config->pcm_bd, config->pcm_in, config->pcm_out);
}

static int
intel_pdi_get_ch_cap(struct sdw_intel *sdw, unsigned int pdi_num)
{
	void __iomem *shim = sdw->link_res->shim;

	/* zero based values for channel count in register */
	return intel_readw(shim, SDW_SHIM2_PCMSYCHC(pdi_num)) + 1;
}

static void intel_pdi_get_ch_update(struct sdw_intel *sdw,
				    struct sdw_cdns_pdi *pdi,
				    unsigned int num_pdi,
				    unsigned int *num_ch)
{
	int ch_count = 0;
	int i;

	for (i = 0; i < num_pdi; i++) {
		pdi->ch_count = intel_pdi_get_ch_cap(sdw, pdi->num);
		ch_count += pdi->ch_count;
		pdi++;
	}

	*num_ch = ch_count;
}

static void intel_pdi_stream_ch_update(struct sdw_intel *sdw,
				       struct sdw_cdns_streams *stream)
{
	intel_pdi_get_ch_update(sdw, stream->bd, stream->num_bd,
				&stream->num_ch_bd);

	intel_pdi_get_ch_update(sdw, stream->in, stream->num_in,
				&stream->num_ch_in);

	intel_pdi_get_ch_update(sdw, stream->out, stream->num_out,
				&stream->num_ch_out);
}

static int intel_create_dai(struct sdw_cdns *cdns,
			    struct snd_soc_dai_driver *dais,
			    enum intel_pdi_type type,
			    u32 num, u32 off, u32 max_ch)
{
	int i;

	if (!num)
		return 0;

	for (i = off; i < (off + num); i++) {
		dais[i].name = devm_kasprintf(cdns->dev, GFP_KERNEL,
					      "SDW%d Pin%d",
					      cdns->instance, i);
		if (!dais[i].name)
			return -ENOMEM;

		if (type == INTEL_PDI_BD || type == INTEL_PDI_OUT) {
			dais[i].playback.channels_min = 1;
			dais[i].playback.channels_max = max_ch;
		}

		if (type == INTEL_PDI_BD || type == INTEL_PDI_IN) {
			dais[i].capture.channels_min = 1;
			dais[i].capture.channels_max = max_ch;
		}

		dais[i].ops = &intel_pcm_dai_ops;
	}

	return 0;
}

static int intel_register_dai(struct sdw_intel *sdw)
{
	struct sdw_cdns_dai_runtime **dai_runtime_array;
	struct sdw_cdns_stream_config config;
	struct sdw_cdns *cdns = &sdw->cdns;
	struct sdw_cdns_streams *stream;
	struct snd_soc_dai_driver *dais;
	int num_dai;
	int ret;
	int off = 0;

	/* Read the PDI config and initialize cadence PDI */
	intel_pdi_init(sdw, &config);
	ret = sdw_cdns_pdi_init(cdns, config);
	if (ret)
		return ret;

	intel_pdi_stream_ch_update(sdw, &sdw->cdns.pcm);

	/* DAIs are created based on total number of PDIs supported */
	num_dai = cdns->pcm.num_pdi;

	dai_runtime_array = devm_kcalloc(cdns->dev, num_dai,
					 sizeof(struct sdw_cdns_dai_runtime *),
					 GFP_KERNEL);
	if (!dai_runtime_array)
		return -ENOMEM;
	cdns->dai_runtime_array = dai_runtime_array;

	dais = devm_kcalloc(cdns->dev, num_dai, sizeof(*dais), GFP_KERNEL);
	if (!dais)
		return -ENOMEM;

	/* Create PCM DAIs */
	stream = &cdns->pcm;

	ret = intel_create_dai(cdns, dais, INTEL_PDI_IN, cdns->pcm.num_in,
			       off, stream->num_ch_in);
	if (ret)
		return ret;

	off += cdns->pcm.num_in;
	ret = intel_create_dai(cdns, dais, INTEL_PDI_OUT, cdns->pcm.num_out,
			       off, stream->num_ch_out);
	if (ret)
		return ret;

	off += cdns->pcm.num_out;
	ret = intel_create_dai(cdns, dais, INTEL_PDI_BD, cdns->pcm.num_bd,
			       off, stream->num_ch_bd);
	if (ret)
		return ret;

	return devm_snd_soc_register_component(cdns->dev, &dai_component,
					       dais, num_dai);
}

const struct sdw_intel_hw_ops sdw_intel_lnl_hw_ops = {
	.debugfs_init = intel_ace2x_debugfs_init,
	.debugfs_exit = intel_ace2x_debugfs_exit,

	.register_dai = intel_register_dai,

	.check_clock_stop = intel_check_clock_stop,
	.start_bus = intel_start_bus,
	.start_bus_after_reset = intel_start_bus_after_reset,
	.start_bus_after_clock_stop = intel_start_bus_after_clock_stop,
	.stop_bus = intel_stop_bus,

	.link_power_up = intel_link_power_up,
	.link_power_down = intel_link_power_down,

	.shim_check_wake = intel_shim_check_wake,
	.shim_wake = intel_shim_wake,

	.sync_arm = intel_sync_arm,
	.sync_go_unlocked = intel_sync_go_unlocked,
	.sync_go = intel_sync_go,
};
EXPORT_SYMBOL_NS(sdw_intel_lnl_hw_ops, SOUNDWIRE_INTEL);

MODULE_IMPORT_NS(SND_SOC_SOF_HDA_MLINK);
