// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

/*
 * Soundwire Intel Master Driver
 */

#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/auxiliary_bus.h>
#include <sound/pcm_params.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include "cadence_master.h"
#include "bus.h"
#include "intel.h"

#define INTEL_MASTER_SUSPEND_DELAY_MS	3000
#define INTEL_MASTER_RESET_ITERATIONS	10

/*
 * debug/config flags for the Intel SoundWire Master.
 *
 * Since we may have multiple masters active, we can have up to 8
 * flags reused in each byte, with master0 using the ls-byte, etc.
 */

#define SDW_INTEL_MASTER_DISABLE_PM_RUNTIME		BIT(0)
#define SDW_INTEL_MASTER_DISABLE_CLOCK_STOP		BIT(1)
#define SDW_INTEL_MASTER_DISABLE_PM_RUNTIME_IDLE	BIT(2)
#define SDW_INTEL_MASTER_DISABLE_MULTI_LINK		BIT(3)

static int md_flags;
module_param_named(sdw_md_flags, md_flags, int, 0444);
MODULE_PARM_DESC(sdw_md_flags, "SoundWire Intel Master device flags (0x0 all off)");

enum intel_pdi_type {
	INTEL_PDI_IN = 0,
	INTEL_PDI_OUT = 1,
	INTEL_PDI_BD = 2,
};

#define cdns_to_intel(_cdns) container_of(_cdns, struct sdw_intel, cdns)

/*
 * Read, write helpers for HW registers
 */
static inline int intel_readl(void __iomem *base, int offset)
{
	return readl(base + offset);
}

static inline void intel_writel(void __iomem *base, int offset, int value)
{
	writel(value, base + offset);
}

static inline u16 intel_readw(void __iomem *base, int offset)
{
	return readw(base + offset);
}

static inline void intel_writew(void __iomem *base, int offset, u16 value)
{
	writew(value, base + offset);
}

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

	links = intel_readl(s, SDW_SHIM_LCAP) & GENMASK(2, 0);

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
		ret += scnprintf(buf + ret, RD_BUF - ret, "\n PDMSCAP, IOCTL, CTMCTL\n");

		ret += intel_sprintf(s, false, buf, ret, SDW_SHIM_PDMSCAP(i));
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
		dev_dbg(sdw->cdns.dev, "%s: powering up all links\n", __func__);

		/* we first need to program the SyncPRD/CPU registers */
		dev_dbg(sdw->cdns.dev,
			"%s: first link up, programming SYNCPRD\n", __func__);

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
out:
	mutex_unlock(sdw->link_res->shim_lock);

	return ret;
}

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

static int intel_shim_init(struct sdw_intel *sdw, bool clock_stop)
{
	void __iomem *shim = sdw->link_res->shim;
	unsigned int link_id = sdw->instance;
	int ret = 0;
	u16 ioctl = 0, act = 0;

	mutex_lock(sdw->link_res->shim_lock);

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

	mutex_unlock(sdw->link_res->shim_lock);

	return ret;
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

		dev_dbg(sdw->cdns.dev, "%s: powering down all links\n", __func__);

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
	int ret;

	/* Read SYNC register */
	sync_reg = intel_readl(shim, SDW_SHIM_SYNC);

	/*
	 * Set SyncGO bit to synchronously trigger a bank switch for
	 * all the masters. A write to SYNCGO bit clears CMDSYNC bit for all
	 * the Masters.
	 */
	sync_reg |= SDW_SHIM_SYNC_SYNCGO;

	ret = intel_clear_bit(shim, SDW_SHIM_SYNC, sync_reg,
			      SDW_SHIM_SYNC_SYNCGO);

	if (ret < 0)
		dev_err(sdw->cdns.dev, "SyncGO clear failed: %d\n", ret);

	return ret;
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

static int intel_pdi_ch_update(struct sdw_intel *sdw)
{
	intel_pdi_stream_ch_update(sdw, &sdw->cdns.pcm);

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
 * bank switch routines
 */

static int intel_pre_bank_switch(struct sdw_bus *bus)
{
	struct sdw_cdns *cdns = bus_to_cdns(bus);
	struct sdw_intel *sdw = cdns_to_intel(cdns);

	/* Write to register only for multi-link */
	if (!bus->multi_link)
		return 0;

	intel_shim_sync_arm(sdw);

	return 0;
}

static int intel_post_bank_switch(struct sdw_bus *bus)
{
	struct sdw_cdns *cdns = bus_to_cdns(bus);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	void __iomem *shim = sdw->link_res->shim;
	int sync_reg, ret;

	/* Write to register only for multi-link */
	if (!bus->multi_link)
		return 0;

	mutex_lock(sdw->link_res->shim_lock);

	/* Read SYNC register */
	sync_reg = intel_readl(shim, SDW_SHIM_SYNC);

	/*
	 * post_bank_switch() ops is called from the bus in loop for
	 * all the Masters in the steam with the expectation that
	 * we trigger the bankswitch for the only first Master in the list
	 * and do nothing for the other Masters
	 *
	 * So, set the SYNCGO bit only if CMDSYNC bit is set for any Master.
	 */
	if (!(sync_reg & SDW_SHIM_SYNC_CMDSYNC_MASK)) {
		ret = 0;
		goto unlock;
	}

	ret = intel_shim_sync_go_unlocked(sdw);
unlock:
	mutex_unlock(sdw->link_res->shim_lock);

	if (ret < 0)
		dev_err(sdw->cdns.dev, "Post bank switch failed: %d\n", ret);

	return ret;
}

/*
 * DAI routines
 */

static int intel_startup(struct snd_pcm_substream *substream,
			 struct snd_soc_dai *dai)
{
	struct sdw_cdns *cdns = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = pm_runtime_resume_and_get(cdns->dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(cdns->dev,
				    "pm_runtime_resume_and_get failed in %s, ret %d\n",
				    __func__, ret);
		return ret;
	}
	return 0;
}

static int intel_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct sdw_cdns *cdns = snd_soc_dai_get_drvdata(dai);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_cdns_dma_data *dma;
	struct sdw_cdns_pdi *pdi;
	struct sdw_stream_config sconfig;
	struct sdw_port_config *pconfig;
	int ch, dir;
	int ret;

	dma = snd_soc_dai_get_dma_data(dai, substream);
	if (!dma)
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
	dma->paused = false;
	dma->suspended = false;
	dma->pdi = pdi;
	dma->hw_params = params;

	/* Inform DSP about PDI stream number */
	ret = intel_params_stream(sdw, substream->stream, dai, params,
				  sdw->instance,
				  pdi->intel_alh_id);
	if (ret)
		goto error;

	sconfig.direction = dir;
	sconfig.ch_count = ch;
	sconfig.frame_rate = params_rate(params);
	sconfig.type = dma->stream_type;

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
				    pconfig, 1, dma->stream);
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
	struct sdw_cdns_dma_data *dma;
	int ch, dir;
	int ret = 0;

	dma = snd_soc_dai_get_dma_data(dai, substream);
	if (!dma) {
		dev_err(dai->dev, "failed to get dma data in %s\n",
			__func__);
		return -EIO;
	}

	if (dma->suspended) {
		dma->suspended = false;

		/*
		 * .prepare() is called after system resume, where we
		 * need to reinitialize the SHIM/ALH/Cadence IP.
		 * .prepare() is also called to deal with underflows,
		 * but in those cases we cannot touch ALH/SHIM
		 * registers
		 */

		/* configure stream */
		ch = params_channels(dma->hw_params);
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			dir = SDW_DATA_DIR_RX;
		else
			dir = SDW_DATA_DIR_TX;

		intel_pdi_shim_configure(sdw, dma->pdi);
		intel_pdi_alh_configure(sdw, dma->pdi);
		sdw_cdns_config_stream(cdns, ch, dir, dma->pdi);

		/* Inform DSP about PDI stream number */
		ret = intel_params_stream(sdw, substream->stream, dai,
					  dma->hw_params,
					  sdw->instance,
					  dma->pdi->intel_alh_id);
	}

	return ret;
}

static int
intel_hw_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct sdw_cdns *cdns = snd_soc_dai_get_drvdata(dai);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_cdns_dma_data *dma;
	int ret;

	dma = snd_soc_dai_get_dma_data(dai, substream);
	if (!dma)
		return -EIO;

	/*
	 * The sdw stream state will transition to RELEASED when stream->
	 * master_list is empty. So the stream state will transition to
	 * DEPREPARED for the first cpu-dai and to RELEASED for the last
	 * cpu-dai.
	 */
	ret = sdw_stream_remove_master(&cdns->bus, dma->stream);
	if (ret < 0) {
		dev_err(dai->dev, "remove master from stream %s failed: %d\n",
			dma->stream->name, ret);
		return ret;
	}

	ret = intel_free_stream(sdw, substream->stream, dai, sdw->instance);
	if (ret < 0) {
		dev_err(dai->dev, "intel_free_stream: failed %d\n", ret);
		return ret;
	}

	dma->hw_params = NULL;
	dma->pdi = NULL;

	return 0;
}

static void intel_shutdown(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct sdw_cdns *cdns = snd_soc_dai_get_drvdata(dai);

	pm_runtime_mark_last_busy(cdns->dev);
	pm_runtime_put_autosuspend(cdns->dev);
}

static int intel_pcm_set_sdw_stream(struct snd_soc_dai *dai,
				    void *stream, int direction)
{
	return cdns_set_sdw_stream(dai, stream, direction);
}

static void *intel_get_sdw_stream(struct snd_soc_dai *dai,
				  int direction)
{
	struct sdw_cdns_dma_data *dma;

	if (direction == SNDRV_PCM_STREAM_PLAYBACK)
		dma = dai->playback_dma_data;
	else
		dma = dai->capture_dma_data;

	if (!dma)
		return ERR_PTR(-EINVAL);

	return dma->stream;
}

static int intel_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
	struct sdw_cdns *cdns = snd_soc_dai_get_drvdata(dai);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_intel_link_res *res = sdw->link_res;
	struct sdw_cdns_dma_data *dma;
	int ret = 0;

	/*
	 * The .trigger callback is used to send required IPC to audio
	 * firmware. The .free_stream callback will still be called
	 * by intel_free_stream() in the TRIGGER_SUSPEND case.
	 */
	if (res->ops && res->ops->trigger)
		res->ops->trigger(dai, cmd, substream->stream);

	dma = snd_soc_dai_get_dma_data(dai, substream);
	if (!dma) {
		dev_err(dai->dev, "failed to get dma data in %s\n",
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

		dma->suspended = true;

		ret = intel_free_stream(sdw, substream->stream, dai, sdw->instance);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dma->paused = true;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dma->paused = false;
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
		struct sdw_cdns_dma_data *dma;
		int stream;
		int ret;

		dma = dai->playback_dma_data;
		stream = SNDRV_PCM_STREAM_PLAYBACK;
		if (!dma) {
			dma = dai->capture_dma_data;
			stream = SNDRV_PCM_STREAM_CAPTURE;
		}

		if (!dma)
			continue;

		if (dma->suspended)
			continue;

		if (dma->paused) {
			dma->suspended = true;

			ret = intel_free_stream(sdw, stream, dai, sdw->instance);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static const struct snd_soc_dai_ops intel_pcm_dai_ops = {
	.startup = intel_startup,
	.hw_params = intel_hw_params,
	.prepare = intel_prepare,
	.hw_free = intel_hw_free,
	.trigger = intel_trigger,
	.shutdown = intel_shutdown,
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

	 /* TODO: Read supported rates/formats from hardware */
	for (i = off; i < (off + num); i++) {
		dais[i].name = devm_kasprintf(cdns->dev, GFP_KERNEL,
					      "SDW%d Pin%d",
					      cdns->instance, i);
		if (!dais[i].name)
			return -ENOMEM;

		if (type == INTEL_PDI_BD || type == INTEL_PDI_OUT) {
			dais[i].playback.channels_min = 1;
			dais[i].playback.channels_max = max_ch;
			dais[i].playback.rates = SNDRV_PCM_RATE_48000;
			dais[i].playback.formats = SNDRV_PCM_FMTBIT_S16_LE;
		}

		if (type == INTEL_PDI_BD || type == INTEL_PDI_IN) {
			dais[i].capture.channels_min = 1;
			dais[i].capture.channels_max = max_ch;
			dais[i].capture.rates = SNDRV_PCM_RATE_48000;
			dais[i].capture.formats = SNDRV_PCM_FMTBIT_S16_LE;
		}

		dais[i].ops = &intel_pcm_dai_ops;
	}

	return 0;
}

static int intel_register_dai(struct sdw_intel *sdw)
{
	struct sdw_cdns *cdns = &sdw->cdns;
	struct sdw_cdns_streams *stream;
	struct snd_soc_dai_driver *dais;
	int num_dai, ret, off = 0;

	/* DAIs are created based on total number of PDIs supported */
	num_dai = cdns->pcm.num_pdi;

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

	return snd_soc_register_component(cdns->dev, &dai_component,
					  dais, num_dai);
}

static int sdw_master_read_intel_prop(struct sdw_bus *bus)
{
	struct sdw_master_prop *prop = &bus->prop;
	struct fwnode_handle *link;
	char name[32];
	u32 quirk_mask;

	/* Find master handle */
	snprintf(name, sizeof(name),
		 "mipi-sdw-link-%d-subproperties", bus->link_id);

	link = device_get_named_child_node(bus->dev, name);
	if (!link) {
		dev_err(bus->dev, "Master node %s not found\n", name);
		return -EIO;
	}

	fwnode_property_read_u32(link,
				 "intel-sdw-ip-clock",
				 &prop->mclk_freq);

	/* the values reported by BIOS are the 2x clock, not the bus clock */
	prop->mclk_freq /= 2;

	fwnode_property_read_u32(link,
				 "intel-quirk-mask",
				 &quirk_mask);

	if (quirk_mask & SDW_INTEL_QUIRK_MASK_BUS_DISABLE)
		prop->hw_disabled = true;

	prop->quirks = SDW_MASTER_QUIRKS_CLEAR_INITIAL_CLASH |
		SDW_MASTER_QUIRKS_CLEAR_INITIAL_PARITY;

	return 0;
}

static int intel_prop_read(struct sdw_bus *bus)
{
	/* Initialize with default handler to read all DisCo properties */
	sdw_master_read_prop(bus);

	/* read Intel-specific properties */
	sdw_master_read_intel_prop(bus);

	return 0;
}

static struct sdw_master_ops sdw_intel_ops = {
	.read_prop = sdw_master_read_prop,
	.override_adr = sdw_dmi_override_adr,
	.xfer_msg = cdns_xfer_msg,
	.xfer_msg_defer = cdns_xfer_msg_defer,
	.reset_page_addr = cdns_reset_page_addr,
	.set_bus_conf = cdns_bus_conf,
	.pre_bank_switch = intel_pre_bank_switch,
	.post_bank_switch = intel_post_bank_switch,
};

static int intel_init(struct sdw_intel *sdw)
{
	bool clock_stop;

	/* Initialize shim and controller */
	intel_link_power_up(sdw);

	clock_stop = sdw_cdns_is_clock_stop(&sdw->cdns);

	intel_shim_init(sdw, clock_stop);

	return 0;
}

/*
 * probe and init (aux_dev_id argument is required by function prototype but not used)
 */
static int intel_link_probe(struct auxiliary_device *auxdev,
			    const struct auxiliary_device_id *aux_dev_id)

{
	struct device *dev = &auxdev->dev;
	struct sdw_intel_link_dev *ldev = auxiliary_dev_to_sdw_intel_link_dev(auxdev);
	struct sdw_intel *sdw;
	struct sdw_cdns *cdns;
	struct sdw_bus *bus;
	int ret;

	sdw = devm_kzalloc(dev, sizeof(*sdw), GFP_KERNEL);
	if (!sdw)
		return -ENOMEM;

	cdns = &sdw->cdns;
	bus = &cdns->bus;

	sdw->instance = auxdev->id;
	sdw->link_res = &ldev->link_res;
	cdns->dev = dev;
	cdns->registers = sdw->link_res->registers;
	cdns->instance = sdw->instance;
	cdns->msg_count = 0;

	bus->link_id = auxdev->id;

	sdw_cdns_probe(cdns);

	/* Set property read ops */
	sdw_intel_ops.read_prop = intel_prop_read;
	bus->ops = &sdw_intel_ops;

	/* set driver data, accessed by snd_soc_dai_get_drvdata() */
	auxiliary_set_drvdata(auxdev, cdns);

	/* use generic bandwidth allocation algorithm */
	sdw->cdns.bus.compute_params = sdw_compute_params;

	/* avoid resuming from pm_runtime suspend if it's not required */
	dev_pm_set_driver_flags(dev, DPM_FLAG_SMART_SUSPEND);

	ret = sdw_bus_master_add(bus, dev, dev->fwnode);
	if (ret) {
		dev_err(dev, "sdw_bus_master_add fail: %d\n", ret);
		return ret;
	}

	if (bus->prop.hw_disabled)
		dev_info(dev,
			 "SoundWire master %d is disabled, will be ignored\n",
			 bus->link_id);
	/*
	 * Ignore BIOS err_threshold, it's a really bad idea when dealing
	 * with multiple hardware synchronized links
	 */
	bus->prop.err_threshold = 0;

	return 0;
}

int intel_link_startup(struct auxiliary_device *auxdev)
{
	struct sdw_cdns_stream_config config;
	struct device *dev = &auxdev->dev;
	struct sdw_cdns *cdns = auxiliary_get_drvdata(auxdev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;
	int link_flags;
	bool multi_link;
	u32 clock_stop_quirks;
	int ret;

	if (bus->prop.hw_disabled) {
		dev_info(dev,
			 "SoundWire master %d is disabled, ignoring\n",
			 sdw->instance);
		return 0;
	}

	link_flags = md_flags >> (bus->link_id * 8);
	multi_link = !(link_flags & SDW_INTEL_MASTER_DISABLE_MULTI_LINK);
	if (!multi_link) {
		dev_dbg(dev, "Multi-link is disabled\n");
		bus->multi_link = false;
	} else {
		/*
		 * hardware-based synchronization is required regardless
		 * of the number of segments used by a stream: SSP-based
		 * synchronization is gated by gsync when the multi-master
		 * mode is set.
		 */
		bus->multi_link = true;
		bus->hw_sync_min_links = 1;
	}

	/* Initialize shim, controller */
	ret = intel_init(sdw);
	if (ret)
		goto err_init;

	/* Read the PDI config and initialize cadence PDI */
	intel_pdi_init(sdw, &config);
	ret = sdw_cdns_pdi_init(cdns, config);
	if (ret)
		goto err_init;

	intel_pdi_ch_update(sdw);

	ret = sdw_cdns_enable_interrupt(cdns, true);
	if (ret < 0) {
		dev_err(dev, "cannot enable interrupts\n");
		goto err_init;
	}

	/*
	 * follow recommended programming flows to avoid timeouts when
	 * gsync is enabled
	 */
	if (multi_link)
		intel_shim_sync_arm(sdw);

	ret = sdw_cdns_init(cdns);
	if (ret < 0) {
		dev_err(dev, "unable to initialize Cadence IP\n");
		goto err_interrupt;
	}

	ret = sdw_cdns_exit_reset(cdns);
	if (ret < 0) {
		dev_err(dev, "unable to exit bus reset sequence\n");
		goto err_interrupt;
	}

	if (multi_link) {
		ret = intel_shim_sync_go(sdw);
		if (ret < 0) {
			dev_err(dev, "sync go failed: %d\n", ret);
			goto err_interrupt;
		}
	}
	sdw_cdns_check_self_clearing_bits(cdns, __func__,
					  true, INTEL_MASTER_RESET_ITERATIONS);

	/* Register DAIs */
	ret = intel_register_dai(sdw);
	if (ret) {
		dev_err(dev, "DAI registration failed: %d\n", ret);
		snd_soc_unregister_component(dev);
		goto err_interrupt;
	}

	intel_debugfs_init(sdw);

	/* Enable runtime PM */
	if (!(link_flags & SDW_INTEL_MASTER_DISABLE_PM_RUNTIME)) {
		pm_runtime_set_autosuspend_delay(dev,
						 INTEL_MASTER_SUSPEND_DELAY_MS);
		pm_runtime_use_autosuspend(dev);
		pm_runtime_mark_last_busy(dev);

		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	clock_stop_quirks = sdw->link_res->clock_stop_quirks;
	if (clock_stop_quirks & SDW_INTEL_CLK_STOP_NOT_ALLOWED) {
		/*
		 * To keep the clock running we need to prevent
		 * pm_runtime suspend from happening by increasing the
		 * reference count.
		 * This quirk is specified by the parent PCI device in
		 * case of specific latency requirements. It will have
		 * no effect if pm_runtime is disabled by the user via
		 * a module parameter for testing purposes.
		 */
		pm_runtime_get_noresume(dev);
	}

	/*
	 * The runtime PM status of Slave devices is "Unsupported"
	 * until they report as ATTACHED. If they don't, e.g. because
	 * there are no Slave devices populated or if the power-on is
	 * delayed or dependent on a power switch, the Master will
	 * remain active and prevent its parent from suspending.
	 *
	 * Conditionally force the pm_runtime core to re-evaluate the
	 * Master status in the absence of any Slave activity. A quirk
	 * is provided to e.g. deal with Slaves that may be powered on
	 * with a delay. A more complete solution would require the
	 * definition of Master properties.
	 */
	if (!(link_flags & SDW_INTEL_MASTER_DISABLE_PM_RUNTIME_IDLE))
		pm_runtime_idle(dev);

	sdw->startup_done = true;
	return 0;

err_interrupt:
	sdw_cdns_enable_interrupt(cdns, false);
err_init:
	return ret;
}

static void intel_link_remove(struct auxiliary_device *auxdev)
{
	struct device *dev = &auxdev->dev;
	struct sdw_cdns *cdns = auxiliary_get_drvdata(auxdev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;

	/*
	 * Since pm_runtime is already disabled, we don't decrease
	 * the refcount when the clock_stop_quirk is
	 * SDW_INTEL_CLK_STOP_NOT_ALLOWED
	 */
	if (!bus->prop.hw_disabled) {
		intel_debugfs_exit(sdw);
		sdw_cdns_enable_interrupt(cdns, false);
		snd_soc_unregister_component(dev);
	}
	sdw_bus_master_delete(bus);
}

int intel_link_process_wakeen_event(struct auxiliary_device *auxdev)
{
	struct device *dev = &auxdev->dev;
	struct sdw_intel *sdw;
	struct sdw_bus *bus;
	void __iomem *shim;
	u16 wake_sts;

	sdw = auxiliary_get_drvdata(auxdev);
	bus = &sdw->cdns.bus;

	if (bus->prop.hw_disabled || !sdw->startup_done) {
		dev_dbg(dev, "SoundWire master %d is disabled or not-started, ignoring\n",
			bus->link_id);
		return 0;
	}

	shim = sdw->link_res->shim;
	wake_sts = intel_readw(shim, SDW_SHIM_WAKESTS);

	if (!(wake_sts & BIT(sdw->instance)))
		return 0;

	/* disable WAKEEN interrupt ASAP to prevent interrupt flood */
	intel_shim_wake(sdw, false);

	/*
	 * resume the Master, which will generate a bus reset and result in
	 * Slaves re-attaching and be re-enumerated. The SoundWire physical
	 * device which generated the wake will trigger an interrupt, which
	 * will in turn cause the corresponding Linux Slave device to be
	 * resumed and the Slave codec driver to check the status.
	 */
	pm_request_resume(dev);

	return 0;
}

/*
 * PM calls
 */

static int intel_resume_child_device(struct device *dev, void *data)
{
	int ret;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);

	if (!slave->probed) {
		dev_dbg(dev, "%s: skipping device, no probed driver\n", __func__);
		return 0;
	}
	if (!slave->dev_num_sticky) {
		dev_dbg(dev, "%s: skipping device, never detected on bus\n", __func__);
		return 0;
	}

	ret = pm_request_resume(dev);
	if (ret < 0)
		dev_err(dev, "%s: pm_request_resume failed: %d\n", __func__, ret);

	return ret;
}

static int __maybe_unused intel_pm_prepare(struct device *dev)
{
	struct sdw_cdns *cdns = dev_get_drvdata(dev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;
	u32 clock_stop_quirks;
	int ret;

	if (bus->prop.hw_disabled || !sdw->startup_done) {
		dev_dbg(dev, "SoundWire master %d is disabled or not-started, ignoring\n",
			bus->link_id);
		return 0;
	}

	clock_stop_quirks = sdw->link_res->clock_stop_quirks;

	if (pm_runtime_suspended(dev) &&
	    pm_runtime_suspended(dev->parent) &&
	    ((clock_stop_quirks & SDW_INTEL_CLK_STOP_BUS_RESET) ||
	     !clock_stop_quirks)) {
		/*
		 * if we've enabled clock stop, and the parent is suspended, the SHIM registers
		 * are not accessible and the shim wake cannot be disabled.
		 * The only solution is to resume the entire bus to full power
		 */

		/*
		 * If any operation in this block fails, we keep going since we don't want
		 * to prevent system suspend from happening and errors should be recoverable
		 * on resume.
		 */

		/*
		 * first resume the device for this link. This will also by construction
		 * resume the PCI parent device.
		 */
		ret = pm_request_resume(dev);
		if (ret < 0) {
			dev_err(dev, "%s: pm_request_resume failed: %d\n", __func__, ret);
			return 0;
		}

		/*
		 * Continue resuming the entire bus (parent + child devices) to exit
		 * the clock stop mode. If there are no devices connected on this link
		 * this is a no-op.
		 * The resume to full power could have been implemented with a .prepare
		 * step in SoundWire codec drivers. This would however require a lot
		 * of code to handle an Intel-specific corner case. It is simpler in
		 * practice to add a loop at the link level.
		 */
		ret = device_for_each_child(bus->dev, NULL, intel_resume_child_device);

		if (ret < 0)
			dev_err(dev, "%s: intel_resume_child_device failed: %d\n", __func__, ret);
	}

	return 0;
}

static int __maybe_unused intel_suspend(struct device *dev)
{
	struct sdw_cdns *cdns = dev_get_drvdata(dev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;
	u32 clock_stop_quirks;
	int ret;

	if (bus->prop.hw_disabled || !sdw->startup_done) {
		dev_dbg(dev, "SoundWire master %d is disabled or not-started, ignoring\n",
			bus->link_id);
		return 0;
	}

	if (pm_runtime_suspended(dev)) {
		dev_dbg(dev, "%s: pm_runtime status: suspended\n", __func__);

		clock_stop_quirks = sdw->link_res->clock_stop_quirks;

		if ((clock_stop_quirks & SDW_INTEL_CLK_STOP_BUS_RESET) ||
		    !clock_stop_quirks) {

			if (pm_runtime_suspended(dev->parent)) {
				/*
				 * paranoia check: this should not happen with the .prepare
				 * resume to full power
				 */
				dev_err(dev, "%s: invalid config: parent is suspended\n", __func__);
			} else {
				intel_shim_wake(sdw, false);
			}
		}

		return 0;
	}

	ret = sdw_cdns_enable_interrupt(cdns, false);
	if (ret < 0) {
		dev_err(dev, "cannot disable interrupts on suspend\n");
		return ret;
	}

	ret = intel_link_power_down(sdw);
	if (ret) {
		dev_err(dev, "Link power down failed: %d\n", ret);
		return ret;
	}

	intel_shim_wake(sdw, false);

	return 0;
}

static int __maybe_unused intel_suspend_runtime(struct device *dev)
{
	struct sdw_cdns *cdns = dev_get_drvdata(dev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;
	u32 clock_stop_quirks;
	int ret;

	if (bus->prop.hw_disabled || !sdw->startup_done) {
		dev_dbg(dev, "SoundWire master %d is disabled or not-started, ignoring\n",
			bus->link_id);
		return 0;
	}

	clock_stop_quirks = sdw->link_res->clock_stop_quirks;

	if (clock_stop_quirks & SDW_INTEL_CLK_STOP_TEARDOWN) {

		ret = sdw_cdns_enable_interrupt(cdns, false);
		if (ret < 0) {
			dev_err(dev, "cannot disable interrupts on suspend\n");
			return ret;
		}

		ret = intel_link_power_down(sdw);
		if (ret) {
			dev_err(dev, "Link power down failed: %d\n", ret);
			return ret;
		}

		intel_shim_wake(sdw, false);

	} else if (clock_stop_quirks & SDW_INTEL_CLK_STOP_BUS_RESET ||
		   !clock_stop_quirks) {
		bool wake_enable = true;

		ret = sdw_cdns_clock_stop(cdns, true);
		if (ret < 0) {
			dev_err(dev, "cannot enable clock stop on suspend\n");
			wake_enable = false;
		}

		ret = sdw_cdns_enable_interrupt(cdns, false);
		if (ret < 0) {
			dev_err(dev, "cannot disable interrupts on suspend\n");
			return ret;
		}

		ret = intel_link_power_down(sdw);
		if (ret) {
			dev_err(dev, "Link power down failed: %d\n", ret);
			return ret;
		}

		intel_shim_wake(sdw, wake_enable);
	} else {
		dev_err(dev, "%s clock_stop_quirks %x unsupported\n",
			__func__, clock_stop_quirks);
		ret = -EINVAL;
	}

	return ret;
}

static int __maybe_unused intel_resume(struct device *dev)
{
	struct sdw_cdns *cdns = dev_get_drvdata(dev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;
	int link_flags;
	bool multi_link;
	int ret;

	if (bus->prop.hw_disabled || !sdw->startup_done) {
		dev_dbg(dev, "SoundWire master %d is disabled or not-started, ignoring\n",
			bus->link_id);
		return 0;
	}

	link_flags = md_flags >> (bus->link_id * 8);
	multi_link = !(link_flags & SDW_INTEL_MASTER_DISABLE_MULTI_LINK);

	if (pm_runtime_suspended(dev)) {
		dev_dbg(dev, "%s: pm_runtime status was suspended, forcing active\n", __func__);

		/* follow required sequence from runtime_pm.rst */
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_mark_last_busy(dev);
		pm_runtime_enable(dev);

		link_flags = md_flags >> (bus->link_id * 8);

		if (!(link_flags & SDW_INTEL_MASTER_DISABLE_PM_RUNTIME_IDLE))
			pm_runtime_idle(dev);
	}

	ret = intel_init(sdw);
	if (ret) {
		dev_err(dev, "%s failed: %d\n", __func__, ret);
		return ret;
	}

	/*
	 * make sure all Slaves are tagged as UNATTACHED and provide
	 * reason for reinitialization
	 */
	sdw_clear_slave_status(bus, SDW_UNATTACH_REQUEST_MASTER_RESET);

	ret = sdw_cdns_enable_interrupt(cdns, true);
	if (ret < 0) {
		dev_err(dev, "cannot enable interrupts during resume\n");
		return ret;
	}

	/*
	 * follow recommended programming flows to avoid timeouts when
	 * gsync is enabled
	 */
	if (multi_link)
		intel_shim_sync_arm(sdw);

	ret = sdw_cdns_init(&sdw->cdns);
	if (ret < 0) {
		dev_err(dev, "unable to initialize Cadence IP during resume\n");
		return ret;
	}

	ret = sdw_cdns_exit_reset(cdns);
	if (ret < 0) {
		dev_err(dev, "unable to exit bus reset sequence during resume\n");
		return ret;
	}

	if (multi_link) {
		ret = intel_shim_sync_go(sdw);
		if (ret < 0) {
			dev_err(dev, "sync go failed during resume\n");
			return ret;
		}
	}
	sdw_cdns_check_self_clearing_bits(cdns, __func__,
					  true, INTEL_MASTER_RESET_ITERATIONS);

	/*
	 * after system resume, the pm_runtime suspend() may kick in
	 * during the enumeration, before any children device force the
	 * master device to remain active.  Using pm_runtime_get()
	 * routines is not really possible, since it'd prevent the
	 * master from suspending.
	 * A reasonable compromise is to update the pm_runtime
	 * counters and delay the pm_runtime suspend by several
	 * seconds, by when all enumeration should be complete.
	 */
	pm_runtime_mark_last_busy(dev);

	return ret;
}

static int __maybe_unused intel_resume_runtime(struct device *dev)
{
	struct sdw_cdns *cdns = dev_get_drvdata(dev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;
	u32 clock_stop_quirks;
	bool clock_stop0;
	int link_flags;
	bool multi_link;
	int status;
	int ret;

	if (bus->prop.hw_disabled || !sdw->startup_done) {
		dev_dbg(dev, "SoundWire master %d is disabled or not-started, ignoring\n",
			bus->link_id);
		return 0;
	}

	/* unconditionally disable WAKEEN interrupt */
	intel_shim_wake(sdw, false);

	link_flags = md_flags >> (bus->link_id * 8);
	multi_link = !(link_flags & SDW_INTEL_MASTER_DISABLE_MULTI_LINK);

	clock_stop_quirks = sdw->link_res->clock_stop_quirks;

	if (clock_stop_quirks & SDW_INTEL_CLK_STOP_TEARDOWN) {
		ret = intel_init(sdw);
		if (ret) {
			dev_err(dev, "%s failed: %d\n", __func__, ret);
			return ret;
		}

		/*
		 * make sure all Slaves are tagged as UNATTACHED and provide
		 * reason for reinitialization
		 */
		sdw_clear_slave_status(bus, SDW_UNATTACH_REQUEST_MASTER_RESET);

		ret = sdw_cdns_enable_interrupt(cdns, true);
		if (ret < 0) {
			dev_err(dev, "cannot enable interrupts during resume\n");
			return ret;
		}

		/*
		 * follow recommended programming flows to avoid
		 * timeouts when gsync is enabled
		 */
		if (multi_link)
			intel_shim_sync_arm(sdw);

		ret = sdw_cdns_init(&sdw->cdns);
		if (ret < 0) {
			dev_err(dev, "unable to initialize Cadence IP during resume\n");
			return ret;
		}

		ret = sdw_cdns_exit_reset(cdns);
		if (ret < 0) {
			dev_err(dev, "unable to exit bus reset sequence during resume\n");
			return ret;
		}

		if (multi_link) {
			ret = intel_shim_sync_go(sdw);
			if (ret < 0) {
				dev_err(dev, "sync go failed during resume\n");
				return ret;
			}
		}
		sdw_cdns_check_self_clearing_bits(cdns, "intel_resume_runtime TEARDOWN",
						  true, INTEL_MASTER_RESET_ITERATIONS);

	} else if (clock_stop_quirks & SDW_INTEL_CLK_STOP_BUS_RESET) {
		ret = intel_init(sdw);
		if (ret) {
			dev_err(dev, "%s failed: %d\n", __func__, ret);
			return ret;
		}

		/*
		 * An exception condition occurs for the CLK_STOP_BUS_RESET
		 * case if one or more masters remain active. In this condition,
		 * all the masters are powered on for they are in the same power
		 * domain. Master can preserve its context for clock stop0, so
		 * there is no need to clear slave status and reset bus.
		 */
		clock_stop0 = sdw_cdns_is_clock_stop(&sdw->cdns);

		if (!clock_stop0) {

			/*
			 * make sure all Slaves are tagged as UNATTACHED and
			 * provide reason for reinitialization
			 */

			status = SDW_UNATTACH_REQUEST_MASTER_RESET;
			sdw_clear_slave_status(bus, status);

			ret = sdw_cdns_enable_interrupt(cdns, true);
			if (ret < 0) {
				dev_err(dev, "cannot enable interrupts during resume\n");
				return ret;
			}

			/*
			 * follow recommended programming flows to avoid
			 * timeouts when gsync is enabled
			 */
			if (multi_link)
				intel_shim_sync_arm(sdw);

			/*
			 * Re-initialize the IP since it was powered-off
			 */
			sdw_cdns_init(&sdw->cdns);

		} else {
			ret = sdw_cdns_enable_interrupt(cdns, true);
			if (ret < 0) {
				dev_err(dev, "cannot enable interrupts during resume\n");
				return ret;
			}
		}

		ret = sdw_cdns_clock_restart(cdns, !clock_stop0);
		if (ret < 0) {
			dev_err(dev, "unable to restart clock during resume\n");
			return ret;
		}

		if (!clock_stop0) {
			ret = sdw_cdns_exit_reset(cdns);
			if (ret < 0) {
				dev_err(dev, "unable to exit bus reset sequence during resume\n");
				return ret;
			}

			if (multi_link) {
				ret = intel_shim_sync_go(sdw);
				if (ret < 0) {
					dev_err(sdw->cdns.dev, "sync go failed during resume\n");
					return ret;
				}
			}
		}
		sdw_cdns_check_self_clearing_bits(cdns, "intel_resume_runtime BUS_RESET",
						  true, INTEL_MASTER_RESET_ITERATIONS);

	} else if (!clock_stop_quirks) {

		clock_stop0 = sdw_cdns_is_clock_stop(&sdw->cdns);
		if (!clock_stop0)
			dev_err(dev, "%s invalid configuration, clock was not stopped", __func__);

		ret = intel_init(sdw);
		if (ret) {
			dev_err(dev, "%s failed: %d\n", __func__, ret);
			return ret;
		}

		ret = sdw_cdns_enable_interrupt(cdns, true);
		if (ret < 0) {
			dev_err(dev, "cannot enable interrupts during resume\n");
			return ret;
		}

		ret = sdw_cdns_clock_restart(cdns, false);
		if (ret < 0) {
			dev_err(dev, "unable to resume master during resume\n");
			return ret;
		}

		sdw_cdns_check_self_clearing_bits(cdns, "intel_resume_runtime no_quirks",
						  true, INTEL_MASTER_RESET_ITERATIONS);
	} else {
		dev_err(dev, "%s clock_stop_quirks %x unsupported\n",
			__func__, clock_stop_quirks);
		ret = -EINVAL;
	}

	return ret;
}

static const struct dev_pm_ops intel_pm = {
	.prepare = intel_pm_prepare,
	SET_SYSTEM_SLEEP_PM_OPS(intel_suspend, intel_resume)
	SET_RUNTIME_PM_OPS(intel_suspend_runtime, intel_resume_runtime, NULL)
};

static const struct auxiliary_device_id intel_link_id_table[] = {
	{ .name = "soundwire_intel.link" },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, intel_link_id_table);

static struct auxiliary_driver sdw_intel_drv = {
	.probe = intel_link_probe,
	.remove = intel_link_remove,
	.driver = {
		/* auxiliary_driver_register() sets .name to be the modname */
		.pm = &intel_pm,
	},
	.id_table = intel_link_id_table
};
module_auxiliary_driver(sdw_intel_drv);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Intel Soundwire Link Driver");
