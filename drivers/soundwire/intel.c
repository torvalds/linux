// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

/*
 * Soundwire Intel Master Driver
 */

#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <sound/pcm_params.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include "cadence_master.h"
#include "bus.h"
#include "intel.h"

static int intel_wait_bit(void __iomem *base, int offset, u32 mask, u32 target)
{
	int timeout = 10;
	u32 reg_read;

	do {
		reg_read = readl(base + offset);
		if ((reg_read & mask) == target)
			return 0;

		timeout--;
		usleep_range(50, 100);
	} while (timeout != 0);

	return -EAGAIN;
}

static int intel_clear_bit(void __iomem *base, int offset, u32 value, u32 mask)
{
	writel(value, base + offset);
	return intel_wait_bit(base, offset, mask, 0);
}

static int intel_set_bit(void __iomem *base, int offset, u32 value, u32 mask)
{
	writel(value, base + offset);
	return intel_wait_bit(base, offset, mask, mask);
}

/*
 * debugfs
 */
#ifdef CONFIG_DEBUG_FS

#define RD_BUF (2 * PAGE_SIZE)

static ssize_t intel_sprintf(void __iomem *mem, bool l,
			     char *buf, size_t pos, unsigned int reg)
{
	int value;

	if (l)
		value = intel_readl(mem, reg);
	else
		value = intel_readw(mem, reg);

	return scnprintf(buf + pos, RD_BUF - pos, "%4x\t%4x\n", reg, value);
}

static int intel_reg_show(struct seq_file *s_file, void *data)
{
	struct sdw_intel *sdw = s_file->private;
	void __iomem *s = sdw->link_res->shim;
	void __iomem *a = sdw->link_res->alh;
	char *buf;
	ssize_t ret;
	int i, j;
	unsigned int links, reg;

	buf = kzalloc(RD_BUF, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	links = intel_readl(s, SDW_SHIM_LCAP) & SDW_SHIM_LCAP_LCOUNT_MASK;

	ret = scnprintf(buf, RD_BUF, "Register  Value\n");
	ret += scnprintf(buf + ret, RD_BUF - ret, "\nShim\n");

	for (i = 0; i < links; i++) {
		reg = SDW_SHIM_LCAP + i * 4;
		ret += intel_sprintf(s, true, buf, ret, reg);
	}

	for (i = 0; i < links; i++) {
		ret += scnprintf(buf + ret, RD_BUF - ret, "\nLink%d\n", i);
		ret += intel_sprintf(s, false, buf, ret, SDW_SHIM_CTLSCAP(i));
		ret += intel_sprintf(s, false, buf, ret, SDW_SHIM_CTLS0CM(i));
		ret += intel_sprintf(s, false, buf, ret, SDW_SHIM_CTLS1CM(i));
		ret += intel_sprintf(s, false, buf, ret, SDW_SHIM_CTLS2CM(i));
		ret += intel_sprintf(s, false, buf, ret, SDW_SHIM_CTLS3CM(i));
		ret += intel_sprintf(s, false, buf, ret, SDW_SHIM_PCMSCAP(i));

		ret += scnprintf(buf + ret, RD_BUF - ret, "\n PCMSyCH registers\n");

		/*
		 * the value 10 is the number of PDIs. We will need a
		 * cleanup to remove hard-coded Intel configurations
		 * from cadence_master.c
		 */
		for (j = 0; j < 10; j++) {
			ret += intel_sprintf(s, false, buf, ret,
					SDW_SHIM_PCMSYCHM(i, j));
			ret += intel_sprintf(s, false, buf, ret,
					SDW_SHIM_PCMSYCHC(i, j));
		}
		ret += scnprintf(buf + ret, RD_BUF - ret, "\n IOCTL, CTMCTL\n");

		ret += intel_sprintf(s, false, buf, ret, SDW_SHIM_IOCTL(i));
		ret += intel_sprintf(s, false, buf, ret, SDW_SHIM_CTMCTL(i));
	}

	ret += scnprintf(buf + ret, RD_BUF - ret, "\nWake registers\n");
	ret += intel_sprintf(s, false, buf, ret, SDW_SHIM_WAKEEN);
	ret += intel_sprintf(s, false, buf, ret, SDW_SHIM_WAKESTS);

	ret += scnprintf(buf + ret, RD_BUF - ret, "\nALH STRMzCFG\n");
	for (i = 0; i < SDW_ALH_NUM_STREAMS; i++)
		ret += intel_sprintf(a, true, buf, ret, SDW_ALH_STRMZCFG(i));

	seq_printf(s_file, "%s", buf);
	kfree(buf);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(intel_reg);

static int intel_set_m_datamode(void *data, u64 value)
{
	struct sdw_intel *sdw = data;
	struct sdw_bus *bus = &sdw->cdns.bus;

	if (value > SDW_PORT_DATA_MODE_STATIC_1)
		return -EINVAL;

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	bus->params.m_data_mode = value;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(intel_set_m_datamode_fops, NULL,
			 intel_set_m_datamode, "%llu\n");

static int intel_set_s_datamode(void *data, u64 value)
{
	struct sdw_intel *sdw = data;
	struct sdw_bus *bus = &sdw->cdns.bus;

	if (value > SDW_PORT_DATA_MODE_STATIC_1)
		return -EINVAL;

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	bus->params.s_data_mode = value;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(intel_set_s_datamode_fops, NULL,
			 intel_set_s_datamode, "%llu\n");

static void intel_debugfs_init(struct sdw_intel *sdw)
{
	struct dentry *root = sdw->cdns.bus.debugfs;

	if (!root)
		return;

	sdw->debugfs = debugfs_create_dir("intel-sdw", root);

	debugfs_create_file("intel-registers", 0400, sdw->debugfs, sdw,
			    &intel_reg_fops);

	debugfs_create_file("intel-m-datamode", 0200, sdw->debugfs, sdw,
			    &intel_set_m_datamode_fops);

	debugfs_create_file("intel-s-datamode", 0200, sdw->debugfs, sdw,
			    &intel_set_s_datamode_fops);

	sdw_cdns_debugfs_init(&sdw->cdns, sdw->debugfs);
}

static void intel_debugfs_exit(struct sdw_intel *sdw)
{
	debugfs_remove_recursive(sdw->debugfs);
}
#else
static void intel_debugfs_init(struct sdw_intel *sdw) {}
static void intel_debugfs_exit(struct sdw_intel *sdw) {}
#endif /* CONFIG_DEBUG_FS */

/*
 * shim ops
 */
/* this needs to be called with shim_lock */
static void intel_shim_glue_to_master_ip(struct sdw_intel *sdw)
{
	void __iomem *shim = sdw->link_res->shim;
	unsigned int link_id = sdw->instance;
	u16 ioctl;

	/* Switch to MIP from Glue logic */
	ioctl = intel_readw(shim,  SDW_SHIM_IOCTL(link_id));

	ioctl &= ~(SDW_SHIM_IOCTL_DOE);
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);
	usleep_range(10, 15);

	ioctl &= ~(SDW_SHIM_IOCTL_DO);
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);
	usleep_range(10, 15);

	ioctl |= (SDW_SHIM_IOCTL_MIF);
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);
	usleep_range(10, 15);

	ioctl &= ~(SDW_SHIM_IOCTL_BKE);
	ioctl &= ~(SDW_SHIM_IOCTL_COE);
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);
	usleep_range(10, 15);

	/* at this point Master IP has full control of the I/Os */
}

/* this needs to be called with shim_lock */
static void intel_shim_master_ip_to_glue(struct sdw_intel *sdw)
{
	unsigned int link_id = sdw->instance;
	void __iomem *shim = sdw->link_res->shim;
	u16 ioctl;

	/* Glue logic */
	ioctl = intel_readw(shim, SDW_SHIM_IOCTL(link_id));
	ioctl |= SDW_SHIM_IOCTL_BKE;
	ioctl |= SDW_SHIM_IOCTL_COE;
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);
	usleep_range(10, 15);

	ioctl &= ~(SDW_SHIM_IOCTL_MIF);
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);
	usleep_range(10, 15);

	/* at this point Integration Glue has full control of the I/Os */
}

/* this needs to be called with shim_lock */
static void intel_shim_init(struct sdw_intel *sdw)
{
	void __iomem *shim = sdw->link_res->shim;
	unsigned int link_id = sdw->instance;
	u16 ioctl = 0, act = 0;

	/* Initialize Shim */
	ioctl |= SDW_SHIM_IOCTL_BKE;
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);
	usleep_range(10, 15);

	ioctl |= SDW_SHIM_IOCTL_WPDD;
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);
	usleep_range(10, 15);

	ioctl |= SDW_SHIM_IOCTL_DO;
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);
	usleep_range(10, 15);

	ioctl |= SDW_SHIM_IOCTL_DOE;
	intel_writew(shim, SDW_SHIM_IOCTL(link_id), ioctl);
	usleep_range(10, 15);

	intel_shim_glue_to_master_ip(sdw);

	u16p_replace_bits(&act, 0x1, SDW_SHIM_CTMCTL_DOAIS);
	act |= SDW_SHIM_CTMCTL_DACTQE;
	act |= SDW_SHIM_CTMCTL_DODS;
	intel_writew(shim, SDW_SHIM_CTMCTL(link_id), act);
	usleep_range(10, 15);
}

static int intel_shim_check_wake(struct sdw_intel *sdw)
{
	void __iomem *shim;
	u16 wake_sts;

	shim = sdw->link_res->shim;
	wake_sts = intel_readw(shim, SDW_SHIM_WAKESTS);

	return wake_sts & BIT(sdw->instance);
}

static void intel_shim_wake(struct sdw_intel *sdw, bool wake_enable)
{
	void __iomem *shim = sdw->link_res->shim;
	unsigned int link_id = sdw->instance;
	u16 wake_en, wake_sts;

	mutex_lock(sdw->link_res->shim_lock);
	wake_en = intel_readw(shim, SDW_SHIM_WAKEEN);

	if (wake_enable) {
		/* Enable the wakeup */
		wake_en |= (SDW_SHIM_WAKEEN_ENABLE << link_id);
		intel_writew(shim, SDW_SHIM_WAKEEN, wake_en);
	} else {
		/* Disable the wake up interrupt */
		wake_en &= ~(SDW_SHIM_WAKEEN_ENABLE << link_id);
		intel_writew(shim, SDW_SHIM_WAKEEN, wake_en);

		/* Clear wake status */
		wake_sts = intel_readw(shim, SDW_SHIM_WAKESTS);
		wake_sts |= (SDW_SHIM_WAKESTS_STATUS << link_id);
		intel_writew(shim, SDW_SHIM_WAKESTS, wake_sts);
	}
	mutex_unlock(sdw->link_res->shim_lock);
}

static bool intel_check_cmdsync_unlocked(struct sdw_intel *sdw)
{
	void __iomem *shim = sdw->link_res->shim;
	int sync_reg;

	sync_reg = intel_readl(shim, SDW_SHIM_SYNC);
	return !!(sync_reg & SDW_SHIM_SYNC_CMDSYNC_MASK);
}

static int intel_link_power_up(struct sdw_intel *sdw)
{
	unsigned int link_id = sdw->instance;
	void __iomem *shim = sdw->link_res->shim;
	u32 *shim_mask = sdw->link_res->shim_mask;
	struct sdw_bus *bus = &sdw->cdns.bus;
	struct sdw_master_prop *prop = &bus->prop;
	u32 spa_mask, cpa_mask;
	u32 link_control;
	int ret = 0;
	u32 syncprd;
	u32 sync_reg;

	mutex_lock(sdw->link_res->shim_lock);

	/*
	 * The hardware relies on an internal counter, typically 4kHz,
	 * to generate the SoundWire SSP - which defines a 'safe'
	 * synchronization point between commands and audio transport
	 * and allows for multi link synchronization. The SYNCPRD value
	 * is only dependent on the oscillator clock provided to
	 * the IP, so adjust based on _DSD properties reported in DSDT
	 * tables. The values reported are based on either 24MHz
	 * (CNL/CML) or 38.4 MHz (ICL/TGL+).
	 */
	if (prop->mclk_freq % 6000000)
		syncprd = SDW_SHIM_SYNC_SYNCPRD_VAL_38_4;
	else
		syncprd = SDW_SHIM_SYNC_SYNCPRD_VAL_24;

	if (!*shim_mask) {
		dev_dbg(sdw->cdns.dev, "powering up all links\n");

		/* we first need to program the SyncPRD/CPU registers */
		dev_dbg(sdw->cdns.dev,
			"first link up, programming SYNCPRD\n");

		/* set SyncPRD period */
		sync_reg = intel_readl(shim, SDW_SHIM_SYNC);
		u32p_replace_bits(&sync_reg, syncprd, SDW_SHIM_SYNC_SYNCPRD);

		/* Set SyncCPU bit */
		sync_reg |= SDW_SHIM_SYNC_SYNCCPU;
		intel_writel(shim, SDW_SHIM_SYNC, sync_reg);

		/* Link power up sequence */
		link_control = intel_readl(shim, SDW_SHIM_LCTL);

		/* only power-up enabled links */
		spa_mask = FIELD_PREP(SDW_SHIM_LCTL_SPA_MASK, sdw->link_res->link_mask);
		cpa_mask = FIELD_PREP(SDW_SHIM_LCTL_CPA_MASK, sdw->link_res->link_mask);

		link_control |=  spa_mask;

		ret = intel_set_bit(shim, SDW_SHIM_LCTL, link_control, cpa_mask);
		if (ret < 0) {
			dev_err(sdw->cdns.dev, "Failed to power up link: %d\n", ret);
			goto out;
		}

		/* SyncCPU will change once link is active */
		ret = intel_wait_bit(shim, SDW_SHIM_SYNC,
				     SDW_SHIM_SYNC_SYNCCPU, 0);
		if (ret < 0) {
			dev_err(sdw->cdns.dev,
				"Failed to set SHIM_SYNC: %d\n", ret);
			goto out;
		}
	}

	*shim_mask |= BIT(link_id);

	sdw->cdns.link_up = true;

	intel_shim_init(sdw);

out:
	mutex_unlock(sdw->link_res->shim_lock);

	return ret;
}

static int intel_link_power_down(struct sdw_intel *sdw)
{
	u32 link_control, spa_mask, cpa_mask;
	unsigned int link_id = sdw->instance;
	void __iomem *shim = sdw->link_res->shim;
	u32 *shim_mask = sdw->link_res->shim_mask;
	int ret = 0;

	mutex_lock(sdw->link_res->shim_lock);

	if (!(*shim_mask & BIT(link_id)))
		dev_err(sdw->cdns.dev,
			"%s: Unbalanced power-up/down calls\n", __func__);

	sdw->cdns.link_up = false;

	intel_shim_master_ip_to_glue(sdw);

	*shim_mask &= ~BIT(link_id);

	if (!*shim_mask) {

		dev_dbg(sdw->cdns.dev, "powering down all links\n");

		/* Link power down sequence */
		link_control = intel_readl(shim, SDW_SHIM_LCTL);

		/* only power-down enabled links */
		spa_mask = FIELD_PREP(SDW_SHIM_LCTL_SPA_MASK, ~sdw->link_res->link_mask);
		cpa_mask = FIELD_PREP(SDW_SHIM_LCTL_CPA_MASK, sdw->link_res->link_mask);

		link_control &=  spa_mask;

		ret = intel_clear_bit(shim, SDW_SHIM_LCTL, link_control, cpa_mask);
		if (ret < 0) {
			dev_err(sdw->cdns.dev, "%s: could not power down link\n", __func__);

			/*
			 * we leave the sdw->cdns.link_up flag as false since we've disabled
			 * the link at this point and cannot handle interrupts any longer.
			 */
		}
	}

	mutex_unlock(sdw->link_res->shim_lock);

	return ret;
}

static void intel_shim_sync_arm(struct sdw_intel *sdw)
{
	void __iomem *shim = sdw->link_res->shim;
	u32 sync_reg;

	mutex_lock(sdw->link_res->shim_lock);

	/* update SYNC register */
	sync_reg = intel_readl(shim, SDW_SHIM_SYNC);
	sync_reg |= (SDW_SHIM_SYNC_CMDSYNC << sdw->instance);
	intel_writel(shim, SDW_SHIM_SYNC, sync_reg);

	mutex_unlock(sdw->link_res->shim_lock);
}

static int intel_shim_sync_go_unlocked(struct sdw_intel *sdw)
{
	void __iomem *shim = sdw->link_res->shim;
	u32 sync_reg;

	/* Read SYNC register */
	sync_reg = intel_readl(shim, SDW_SHIM_SYNC);

	/*
	 * Set SyncGO bit to synchronously trigger a bank switch for
	 * all the masters. A write to SYNCGO bit clears CMDSYNC bit for all
	 * the Masters.
	 */
	sync_reg |= SDW_SHIM_SYNC_SYNCGO;

	intel_writel(shim, SDW_SHIM_SYNC, sync_reg);

	return 0;
}

static int intel_shim_sync_go(struct sdw_intel *sdw)
{
	int ret;

	mutex_lock(sdw->link_res->shim_lock);

	ret = intel_shim_sync_go_unlocked(sdw);

	mutex_unlock(sdw->link_res->shim_lock);

	return ret;
}

/*
 * PDI routines
 */
static void intel_pdi_init(struct sdw_intel *sdw,
			   struct sdw_cdns_stream_config *config)
{
	void __iomem *shim = sdw->link_res->shim;
	unsigned int link_id = sdw->instance;
	int pcm_cap;

	/* PCM Stream Capability */
	pcm_cap = intel_readw(shim, SDW_SHIM_PCMSCAP(link_id));

	config->pcm_bd = FIELD_GET(SDW_SHIM_PCMSCAP_BSS, pcm_cap);
	config->pcm_in = FIELD_GET(SDW_SHIM_PCMSCAP_ISS, pcm_cap);
	config->pcm_out = FIELD_GET(SDW_SHIM_PCMSCAP_OSS, pcm_cap);

	dev_dbg(sdw->cdns.dev, "PCM cap bd:%d in:%d out:%d\n",
		config->pcm_bd, config->pcm_in, config->pcm_out);
}

static int
intel_pdi_get_ch_cap(struct sdw_intel *sdw, unsigned int pdi_num)
{
	void __iomem *shim = sdw->link_res->shim;
	unsigned int link_id = sdw->instance;
	int count;

	count = intel_readw(shim, SDW_SHIM_PCMSYCHC(link_id, pdi_num));

	/*
	 * WORKAROUND: on all existing Intel controllers, pdi
	 * number 2 reports channel count as 1 even though it
	 * supports 8 channels. Performing hardcoding for pdi
	 * number 2.
	 */
	if (pdi_num == 2)
		count = 7;

	/* zero based values for channel count in register */
	count++;

	return count;
}

static int intel_pdi_get_ch_update(struct sdw_intel *sdw,
				   struct sdw_cdns_pdi *pdi,
				   unsigned int num_pdi,
				   unsigned int *num_ch)
{
	int i, ch_count = 0;

	for (i = 0; i < num_pdi; i++) {
		pdi->ch_count = intel_pdi_get_ch_cap(sdw, pdi->num);
		ch_count += pdi->ch_count;
		pdi++;
	}

	*num_ch = ch_count;
	return 0;
}

static int intel_pdi_stream_ch_update(struct sdw_intel *sdw,
				      struct sdw_cdns_streams *stream)
{
	intel_pdi_get_ch_update(sdw, stream->bd, stream->num_bd,
				&stream->num_ch_bd);

	intel_pdi_get_ch_update(sdw, stream->in, stream->num_in,
				&stream->num_ch_in);

	intel_pdi_get_ch_update(sdw, stream->out, stream->num_out,
				&stream->num_ch_out);

	return 0;
}

static void
intel_pdi_shim_configure(struct sdw_intel *sdw, struct sdw_cdns_pdi *pdi)
{
	void __iomem *shim = sdw->link_res->shim;
	unsigned int link_id = sdw->instance;
	int pdi_conf = 0;

	/* the Bulk and PCM streams are not contiguous */
	pdi->intel_alh_id = (link_id * 16) + pdi->num + 3;
	if (pdi->num >= 2)
		pdi->intel_alh_id += 2;

	/*
	 * Program stream parameters to stream SHIM register
	 * This is applicable for PCM stream only.
	 */
	if (pdi->type != SDW_STREAM_PCM)
		return;

	if (pdi->dir == SDW_DATA_DIR_RX)
		pdi_conf |= SDW_SHIM_PCMSYCM_DIR;
	else
		pdi_conf &= ~(SDW_SHIM_PCMSYCM_DIR);

	u32p_replace_bits(&pdi_conf, pdi->intel_alh_id, SDW_SHIM_PCMSYCM_STREAM);
	u32p_replace_bits(&pdi_conf, pdi->l_ch_num, SDW_SHIM_PCMSYCM_LCHN);
	u32p_replace_bits(&pdi_conf, pdi->h_ch_num, SDW_SHIM_PCMSYCM_HCHN);

	intel_writew(shim, SDW_SHIM_PCMSYCHM(link_id, pdi->num), pdi_conf);
}

static void
intel_pdi_alh_configure(struct sdw_intel *sdw, struct sdw_cdns_pdi *pdi)
{
	void __iomem *alh = sdw->link_res->alh;
	unsigned int link_id = sdw->instance;
	unsigned int conf;

	/* the Bulk and PCM streams are not contiguous */
	pdi->intel_alh_id = (link_id * 16) + pdi->num + 3;
	if (pdi->num >= 2)
		pdi->intel_alh_id += 2;

	/* Program Stream config ALH register */
	conf = intel_readl(alh, SDW_ALH_STRMZCFG(pdi->intel_alh_id));

	u32p_replace_bits(&conf, SDW_ALH_STRMZCFG_DMAT_VAL, SDW_ALH_STRMZCFG_DMAT);
	u32p_replace_bits(&conf, pdi->ch_count - 1, SDW_ALH_STRMZCFG_CHN);

	intel_writel(alh, SDW_ALH_STRMZCFG(pdi->intel_alh_id), conf);
}

static int intel_params_stream(struct sdw_intel *sdw,
			       int stream,
			       struct snd_soc_dai *dai,
			       struct snd_pcm_hw_params *hw_params,
			       int link_id, int alh_stream_id)
{
	struct sdw_intel_link_res *res = sdw->link_res;
	struct sdw_intel_stream_params_data params_data;

	params_data.stream = stream; /* direction */
	params_data.dai = dai;
	params_data.hw_params = hw_params;
	params_data.link_id = link_id;
	params_data.alh_stream_id = alh_stream_id;

	if (res->ops && res->ops->params_stream && res->dev)
		return res->ops->params_stream(res->dev,
					       &params_data);
	return -EIO;
}

static int intel_free_stream(struct sdw_intel *sdw,
			     int stream,
			     struct snd_soc_dai *dai,
			     int link_id)
{
	struct sdw_intel_link_res *res = sdw->link_res;
	struct sdw_intel_stream_free_data free_data;

	free_data.stream = stream; /* direction */
	free_data.dai = dai;
	free_data.link_id = link_id;

	if (res->ops && res->ops->free_stream && res->dev)
		return res->ops->free_stream(res->dev,
					     &free_data);

	return 0;
}

/*
 * DAI routines
 */

static int intel_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct sdw_cdns *cdns = snd_soc_dai_get_drvdata(dai);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_cdns_dai_runtime *dai_runtime;
	struct sdw_cdns_pdi *pdi;
	struct sdw_stream_config sconfig;
	struct sdw_port_config *pconfig;
	int ch, dir;
	int ret;

	dai_runtime = cdns->dai_runtime_array[dai->id];
	if (!dai_runtime)
		return -EIO;

	ch = params_channels(params);
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		dir = SDW_DATA_DIR_RX;
	else
		dir = SDW_DATA_DIR_TX;

	pdi = sdw_cdns_alloc_pdi(cdns, &cdns->pcm, ch, dir, dai->id);

	if (!pdi) {
		ret = -EINVAL;
		goto error;
	}

	/* do run-time configurations for SHIM, ALH and PDI/PORT */
	intel_pdi_shim_configure(sdw, pdi);
	intel_pdi_alh_configure(sdw, pdi);
	sdw_cdns_config_stream(cdns, ch, dir, pdi);

	/* store pdi and hw_params, may be needed in prepare step */
	dai_runtime->paused = false;
	dai_runtime->suspended = false;
	dai_runtime->pdi = pdi;

	/* Inform DSP about PDI stream number */
	ret = intel_params_stream(sdw, substream->stream, dai, params,
				  sdw->instance,
				  pdi->intel_alh_id);
	if (ret)
		goto error;

	sconfig.direction = dir;
	sconfig.ch_count = ch;
	sconfig.frame_rate = params_rate(params);
	sconfig.type = dai_runtime->stream_type;

	sconfig.bps = snd_pcm_format_width(params_format(params));

	/* Port configuration */
	pconfig = kzalloc(sizeof(*pconfig), GFP_KERNEL);
	if (!pconfig) {
		ret =  -ENOMEM;
		goto error;
	}

	pconfig->num = pdi->num;
	pconfig->ch_mask = (1 << ch) - 1;

	ret = sdw_stream_add_master(&cdns->bus, &sconfig,
				    pconfig, 1, dai_runtime->stream);
	if (ret)
		dev_err(cdns->dev, "add master to stream failed:%d\n", ret);

	kfree(pconfig);
error:
	return ret;
}

static int intel_prepare(struct snd_pcm_substream *substream,
			 struct snd_soc_dai *dai)
{
	struct sdw_cdns *cdns = snd_soc_dai_get_drvdata(dai);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_cdns_dai_runtime *dai_runtime;
	int ch, dir;
	int ret = 0;

	dai_runtime = cdns->dai_runtime_array[dai->id];
	if (!dai_runtime) {
		dev_err(dai->dev, "failed to get dai runtime in %s\n",
			__func__);
		return -EIO;
	}

	if (dai_runtime->suspended) {
		struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
		struct snd_pcm_hw_params *hw_params;

		hw_params = &rtd->dpcm[substream->stream].hw_params;

		dai_runtime->suspended = false;

		/*
		 * .prepare() is called after system resume, where we
		 * need to reinitialize the SHIM/ALH/Cadence IP.
		 * .prepare() is also called to deal with underflows,
		 * but in those cases we cannot touch ALH/SHIM
		 * registers
		 */

		/* configure stream */
		ch = params_channels(hw_params);
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			dir = SDW_DATA_DIR_RX;
		else
			dir = SDW_DATA_DIR_TX;

		intel_pdi_shim_configure(sdw, dai_runtime->pdi);
		intel_pdi_alh_configure(sdw, dai_runtime->pdi);
		sdw_cdns_config_stream(cdns, ch, dir, dai_runtime->pdi);

		/* Inform DSP about PDI stream number */
		ret = intel_params_stream(sdw, substream->stream, dai,
					  hw_params,
					  sdw->instance,
					  dai_runtime->pdi->intel_alh_id);
	}

	return ret;
}

static int
intel_hw_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct sdw_cdns *cdns = snd_soc_dai_get_drvdata(dai);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_cdns_dai_runtime *dai_runtime;
	int ret;

	dai_runtime = cdns->dai_runtime_array[dai->id];
	if (!dai_runtime)
		return -EIO;

	/*
	 * The sdw stream state will transition to RELEASED when stream->
	 * master_list is empty. So the stream state will transition to
	 * DEPREPARED for the first cpu-dai and to RELEASED for the last
	 * cpu-dai.
	 */
	ret = sdw_stream_remove_master(&cdns->bus, dai_runtime->stream);
	if (ret < 0) {
		dev_err(dai->dev, "remove master from stream %s failed: %d\n",
			dai_runtime->stream->name, ret);
		return ret;
	}

	ret = intel_free_stream(sdw, substream->stream, dai, sdw->instance);
	if (ret < 0) {
		dev_err(dai->dev, "intel_free_stream: failed %d\n", ret);
		return ret;
	}

	dai_runtime->pdi = NULL;

	return 0;
}

static int intel_pcm_set_sdw_stream(struct snd_soc_dai *dai,
				    void *stream, int direction)
{
	return cdns_set_sdw_stream(dai, stream, direction);
}

static void *intel_get_sdw_stream(struct snd_soc_dai *dai,
				  int direction)
{
	struct sdw_cdns *cdns = snd_soc_dai_get_drvdata(dai);
	struct sdw_cdns_dai_runtime *dai_runtime;

	dai_runtime = cdns->dai_runtime_array[dai->id];
	if (!dai_runtime)
		return ERR_PTR(-EINVAL);

	return dai_runtime->stream;
}

static int intel_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
	struct sdw_cdns *cdns = snd_soc_dai_get_drvdata(dai);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_intel_link_res *res = sdw->link_res;
	struct sdw_cdns_dai_runtime *dai_runtime;
	int ret = 0;

	/*
	 * The .trigger callback is used to send required IPC to audio
	 * firmware. The .free_stream callback will still be called
	 * by intel_free_stream() in the TRIGGER_SUSPEND case.
	 */
	if (res->ops && res->ops->trigger)
		res->ops->trigger(dai, cmd, substream->stream);

	dai_runtime = cdns->dai_runtime_array[dai->id];
	if (!dai_runtime) {
		dev_err(dai->dev, "failed to get dai runtime in %s\n",
			__func__);
		return -EIO;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_SUSPEND:

		/*
		 * The .prepare callback is used to deal with xruns and resume operations.
		 * In the case of xruns, the DMAs and SHIM registers cannot be touched,
		 * but for resume operations the DMAs and SHIM registers need to be initialized.
		 * the .trigger callback is used to track the suspend case only.
		 */

		dai_runtime->suspended = true;

		ret = intel_free_stream(sdw, substream->stream, dai, sdw->instance);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dai_runtime->paused = true;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dai_runtime->paused = false;
		break;
	default:
		break;
	}

	return ret;
}

static int intel_component_probe(struct snd_soc_component *component)
{
	int ret;

	/*
	 * make sure the device is pm_runtime_active before initiating
	 * bus transactions during the card registration.
	 * We use pm_runtime_resume() here, without taking a reference
	 * and releasing it immediately.
	 */
	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	return 0;
}

static int intel_component_dais_suspend(struct snd_soc_component *component)
{
	struct snd_soc_dai *dai;

	/*
	 * In the corner case where a SUSPEND happens during a PAUSE, the ALSA core
	 * does not throw the TRIGGER_SUSPEND. This leaves the DAIs in an unbalanced state.
	 * Since the component suspend is called last, we can trap this corner case
	 * and force the DAIs to release their resources.
	 */
	for_each_component_dais(component, dai) {
		struct sdw_cdns *cdns = snd_soc_dai_get_drvdata(dai);
		struct sdw_intel *sdw = cdns_to_intel(cdns);
		struct sdw_cdns_dai_runtime *dai_runtime;
		int ret;

		dai_runtime = cdns->dai_runtime_array[dai->id];

		if (!dai_runtime)
			continue;

		if (dai_runtime->suspended)
			continue;

		if (dai_runtime->paused) {
			dai_runtime->suspended = true;

			ret = intel_free_stream(sdw, dai_runtime->direction, dai, sdw->instance);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static const struct snd_soc_dai_ops intel_pcm_dai_ops = {
	.hw_params = intel_hw_params,
	.prepare = intel_prepare,
	.hw_free = intel_hw_free,
	.trigger = intel_trigger,
	.set_stream = intel_pcm_set_sdw_stream,
	.get_stream = intel_get_sdw_stream,
};

static const struct snd_soc_component_driver dai_component = {
	.name			= "soundwire",
	.probe			= intel_component_probe,
	.suspend		= intel_component_dais_suspend,
	.legacy_dai_naming	= 1,
};

static int intel_create_dai(struct sdw_cdns *cdns,
			    struct snd_soc_dai_driver *dais,
			    enum intel_pdi_type type,
			    u32 num, u32 off, u32 max_ch)
{
	int i;

	if (num == 0)
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
	int num_dai, ret, off = 0;

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


const struct sdw_intel_hw_ops sdw_intel_cnl_hw_ops = {
	.debugfs_init = intel_debugfs_init,
	.debugfs_exit = intel_debugfs_exit,

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

	.pre_bank_switch = intel_pre_bank_switch,
	.post_bank_switch = intel_post_bank_switch,

	.sync_arm = intel_shim_sync_arm,
	.sync_go_unlocked = intel_shim_sync_go_unlocked,
	.sync_go = intel_shim_sync_go,
	.sync_check_cmdsync_unlocked = intel_check_cmdsync_unlocked,
};
EXPORT_SYMBOL_NS(sdw_intel_cnl_hw_ops, SOUNDWIRE_INTEL);

