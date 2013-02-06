/* linux/drivers/media/video/samsung/tvout/s5p_tvout.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Entry file for Samsung TVOut driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/delay.h>

#if defined(CONFIG_S5P_SYSMMU_TV)
#include <plat/sysmmu.h>
#endif

#if defined(CONFIG_S5P_MEM_CMA)
#include <linux/cma.h>
#elif defined(CONFIG_S5P_MEM_BOOTMEM)
#include <plat/media.h>
#include <mach/media.h>
#endif

#if defined(CONFIG_HDMI_TX_STRENGTH) && !defined(CONFIG_USER_ALLOC_TVOUT)
#include <plat/tvout.h>
#endif

#include "s5p_tvout_common_lib.h"
#include "s5p_tvout_ctrl.h"
#include "s5p_tvout_fb.h"
#include "s5p_tvout_v4l2.h"

#define TV_CLK_GET_WITH_ERR_CHECK(clk, pdev, clk_name)			\
		do {							\
			clk = clk_get(&pdev->dev, clk_name);		\
			if (IS_ERR(clk)) {				\
				printk(KERN_ERR				\
				"failed to find clock %s\n", clk_name);	\
				return -ENOENT;				\
			}						\
		} while (0);

struct s5p_tvout_status s5ptv_status;
bool on_stop_process;
bool on_start_process;
struct s5p_tvout_vp_bufferinfo s5ptv_vp_buff;
#ifdef CONFIG_PM
static struct workqueue_struct *tvout_resume_wq;
struct work_struct tvout_resume_work;
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend s5ptv_early_suspend;
static DEFINE_MUTEX(s5p_tvout_mutex);
unsigned int suspend_status;
static void s5p_tvout_early_suspend(struct early_suspend *h);
static void s5p_tvout_late_resume(struct early_suspend *h);
#endif
bool flag_after_resume;

#ifdef CONFIG_TVOUT_DEBUG
int tvout_dbg_flag;
#endif


#ifdef CONFIG_HDMI_EARJACK_MUTE
bool hdmi_audio_ext;

/* To provide an interface fo Audio path control */
static ssize_t hdmi_set_audio_read(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int count = 0;

	printk(KERN_ERR "[HDMI]: AUDIO PATH\n");
	return count;
}

static ssize_t hdmi_set_audio_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	char *after;
	bool value = !strncmp(buf, "1", 1) ? true : false;

	printk(KERN_ERR "[HDMI] Change AUDIO PATH: %d\n", (int)value);

	if (value == hdmi_audio_ext) {
		if (value) {
			hdmi_audio_ext = 0;
			s5p_hdmi_ctrl_set_audio(1);
		} else {
			hdmi_audio_ext = 1;
			s5p_hdmi_ctrl_set_audio(0);
		}
	}

	return size;
}

static DEVICE_ATTR(hdmi_audio_set_ext, 0660,
	hdmi_set_audio_read, hdmi_set_audio_store);
#endif

static int __devinit s5p_tvout_clk_get(struct platform_device *pdev,
				       struct s5p_tvout_status *ctrl)
{
	struct clk *ext_xtal_clk, *mout_vpll_src, *fout_vpll, *mout_vpll;

	TV_CLK_GET_WITH_ERR_CHECK(ctrl->i2c_phy_clk, pdev, "i2c-hdmiphy");

	TV_CLK_GET_WITH_ERR_CHECK(ctrl->sclk_dac, pdev, "sclk_dac");
	TV_CLK_GET_WITH_ERR_CHECK(ctrl->sclk_hdmi, pdev, "sclk_hdmi");

	TV_CLK_GET_WITH_ERR_CHECK(ctrl->sclk_pixel, pdev, "sclk_pixel");
	TV_CLK_GET_WITH_ERR_CHECK(ctrl->sclk_hdmiphy, pdev, "sclk_hdmiphy");

	TV_CLK_GET_WITH_ERR_CHECK(ext_xtal_clk, pdev, "ext_xtal");
	TV_CLK_GET_WITH_ERR_CHECK(mout_vpll_src, pdev, "vpll_src");
	TV_CLK_GET_WITH_ERR_CHECK(fout_vpll, pdev, "fout_vpll");
	TV_CLK_GET_WITH_ERR_CHECK(mout_vpll, pdev, "sclk_vpll");

#ifdef CONFIG_VPLL_USE_FOR_TVENC
	if (clk_set_rate(fout_vpll, 54000000)) {
		tvout_err("%s rate change failed: %lu\n", fout_vpll->name,
			  54000000);
		return -1;
	}

	if (clk_set_parent(mout_vpll_src, ext_xtal_clk)) {
		tvout_err("unable to set parent %s of clock %s.\n",
			  ext_xtal_clk->name, mout_vpll_src->name);
		return -1;
	}

	if (clk_set_parent(mout_vpll, fout_vpll)) {
		tvout_err("unable to set parent %s of clock %s.\n",
			  fout_vpll->name, mout_vpll->name);
		return -1;
	}

	/* sclk_dac's parent is fixed as mout_vpll */
	if (clk_set_parent(ctrl->sclk_dac, mout_vpll)) {
		tvout_err("unable to set parent %s of clock %s.\n",
			  mout_vpll->name, ctrl->sclk_dac->name);
		return -1;
	}

	/* It'll be moved in the future */
	if (clk_enable(mout_vpll_src) < 0)
		return -1;

	if (clk_enable(fout_vpll) < 0)
		return -1;

	if (clk_enable(mout_vpll) < 0)
		return -1;

	clk_put(ext_xtal_clk);
	clk_put(mout_vpll_src);
	clk_put(fout_vpll);
	clk_put(mout_vpll);
#endif

	return 0;
}

#ifdef CONFIG_TVOUT_DEBUG
void show_tvout_dbg_flag(void)
{
	pr_info("hw_if/hdmi.c %s\n",
		((tvout_dbg_flag >> DBG_FLAG_HDMI) & 0x1 ? "On" : "Off"));
	pr_info("s5p_tvout_hpd.c %s\n",
		((tvout_dbg_flag >> DBG_FLAG_HPD) & 0x1 ? "On" : "Off"));
	pr_info("s5p_tvout_common_lib.h %s\n",
		((tvout_dbg_flag >> DBG_FLAG_TVOUT) & 0x1 ? "On" : "Off"));
	pr_info("hw_if/hdcp.c %s\n",
		((tvout_dbg_flag >> DBG_FLAG_HDCP) & 0x1 ? "On" : "Off"));
}

void set_flag_value(int *flag, int pos, int value)
{
	if (value == 1) {
		*flag |= (1 << pos);
	} else {		/* value is 0 */
		*flag &= ~(1 << pos);
	}
}

static ssize_t sysfs_dbg_msg_show(struct class *class,
				struct class_attribute *attr, char *buf)
{
	pr_info("sysfs_dbg_msg_show\n");
	show_tvout_dbg_flag();
	return sprintf(buf, "hw_if/hdmi.c %s\n"
		"s5p_tvout_hpd.c %s\n"
		"s5p_tvout_common_lib.h %s\n"
		"hw_if/hdcp.c %s\n",
		((tvout_dbg_flag >> DBG_FLAG_HDMI) & 0x1 ? "On" : "Off"),
		((tvout_dbg_flag >> DBG_FLAG_HPD) & 0x1 ? "On" : "Off"),
		((tvout_dbg_flag >> DBG_FLAG_TVOUT) & 0x1 ? "On" : "Off"),
		((tvout_dbg_flag >> DBG_FLAG_HDCP) & 0x1 ? "On" : "Off"));
}

static ssize_t sysfs_dbg_msg_store(struct class *class,
		struct class_attribute *attr, const char *buf, size_t size)
{
	enum tvout_dbg_flag_bit_num tvout_dbg_flag_bit;
	int value;
	int i;
	char *dest[2];
	char *buffer = (char *)buf;

	pr_info("TVOUT Debug Message setting : ");
	for (i = 0; i < 2; i++)
		dest[i] = strsep(&buffer, ":");

	if (strcmp(dest[0], "help") == 0) {
		pr_info(
			"bit3 : hw_if/hdmi.c\n"
			"bit2 : s5p_tvout_hpd.c\n"
			"bit1 : s5p_tvout_common_lib.h\n"
			"bit0 : hw_if/hdcp.c\n"
			"ex1)	echo 1010 > dbg_msg\n"
			"	hw_if/hdmi.c On\n"
			"	s5p_tvout_hpd.c Off\n"
			"	s5p_tvout_common_lib.h On\n"
			"	hw_if/hdcp.c Off\n"
			"ex2)	echo hdcp:1 > dbg_msg\n"
			"	hw_if/hdcp.c On\n"
		);
		return size;
	}

	if (strcmp(dest[0], "hdcp") == 0) {
		tvout_dbg_flag_bit = DBG_FLAG_HDCP;
	} else if (strcmp(dest[0], "tvout") == 0) {
		tvout_dbg_flag_bit = DBG_FLAG_TVOUT;
	} else if (strcmp(dest[0], "hpd") == 0) {
		tvout_dbg_flag_bit = DBG_FLAG_HPD;
	} else if (strcmp(dest[0], "hdmi") == 0) {
		tvout_dbg_flag_bit = DBG_FLAG_HDMI;
	} else if (strlen(dest[0]) == 5) {
		for (i = 0; i < 4; i++) {
			value = dest[0][i] - '0';
			if (value < 0 || 2 < value) {
				pr_info("error : setting value!\n");
				return size;
			}
			set_flag_value(&tvout_dbg_flag, 3-i, value);
		}
		show_tvout_dbg_flag();
		return size;
	} else {
		pr_info("Error : Debug Message Taget\n");
		return size;
	}

	if (strcmp(dest[1], "1\n") == 0) {
		value = 1;
	} else if (strcmp(dest[1], "0\n") == 0) {
		value = 0;
	} else {
		pr_info("Error : Setting value!\n");
		return size;
	}

	set_flag_value(&tvout_dbg_flag, tvout_dbg_flag_bit, value);
	show_tvout_dbg_flag();

	return size;
}

static CLASS_ATTR(dbg_msg, S_IRUGO | S_IWUSR,
		sysfs_dbg_msg_show, sysfs_dbg_msg_store);
#endif

#if !defined(CONFIG_CPU_EXYNOS4212) && !defined(CONFIG_CPU_EXYNOS4412)
#if defined(CONFIG_USE_TVOUT_CMA)
static inline int alloc_vp_buff(struct platform_device *pdev)
{
	/* in this case, buff will be allocated later
	   when HDMI/MHL cable is connected */
	return 1;
}
#elif defined(CONFIG_S5P_MEM_CMA)
static inline int alloc_vp_buff(struct platform_device *pdev)
{
	int i, ret;
	struct cma_info mem_info;
	unsigned int vp_buff_vir_addr;
	unsigned int vp_buff_phy_addr;

	ret = cma_info(&mem_info, &pdev->dev, 0);
	tvout_dbg("[cma_info] start_addr : 0x%x, end_addr : 0x%x, "
		  "total_size : 0x%x, free_size : 0x%x\n",
		  mem_info.lower_bound, mem_info.upper_bound,
		  mem_info.total_size, mem_info.free_size);
	if (ret) {
		tvout_err("get cma info failed\n");
		return 0;
	}
	s5ptv_vp_buff.size = mem_info.total_size;
	if (s5ptv_vp_buff.size < S5PTV_VP_BUFF_CNT * S5PTV_VP_BUFF_SIZE) {
		tvout_err("insufficient vp buffer size\n");
		return 0;
	}
	vp_buff_phy_addr = (unsigned int)cma_alloc
	    (&pdev->dev, (char *)"tvout", (size_t) s5ptv_vp_buff.size,
	     (dma_addr_t) 0);

	tvout_dbg("s5ptv_vp_buff.size = 0x%x\n", s5ptv_vp_buff.size);
	tvout_dbg("s5ptv_vp_buff phy_base = 0x%x\n", vp_buff_phy_addr);

	vp_buff_vir_addr = (unsigned int)phys_to_virt(vp_buff_phy_addr);
	tvout_dbg("s5ptv_vp_buff vir_base = 0x%x\n", vp_buff_vir_addr);

	if (!vp_buff_vir_addr) {
		tvout_err("phys_to_virt failed\n");
		cma_free(vp_buff_phy_addr);
		return 0;
	}

	for (i = 0; i < S5PTV_VP_BUFF_CNT; i++) {
		s5ptv_vp_buff.vp_buffs[i].phy_base =
		    vp_buff_phy_addr + (i * S5PTV_VP_BUFF_SIZE);
		s5ptv_vp_buff.vp_buffs[i].vir_base =
		    vp_buff_vir_addr + (i * S5PTV_VP_BUFF_SIZE);
	}

	return 1;
}
#elif defined(CONFIG_S5P_MEM_BOOTMEM)
static inline int alloc_vp_buff(struct platform_device *pdev)
{
	int i;
	int mdev_id;
	unsigned int vp_buff_vir_addr;
	unsigned int vp_buff_phy_addr;

	mdev_id = S5P_MDEV_TVOUT;
	/* alloc from bank1 as default */
	vp_buff_phy_addr = s5p_get_media_memory_bank(mdev_id, 1);
	s5ptv_vp_buff.size = s5p_get_media_memsize_bank(mdev_id, 1);
	if (s5ptv_vp_buff.size < S5PTV_VP_BUFF_CNT * S5PTV_VP_BUFF_SIZE) {
		tvout_err("insufficient vp buffer size\n");
		return 0;
	}

	tvout_dbg("s5ptv_vp_buff.size = 0x%x\n", s5ptv_vp_buff.size);
	tvout_dbg("s5ptv_vp_buff phy_base = 0x%x\n", vp_buff_phy_addr);

	vp_buff_vir_addr = (unsigned int)phys_to_virt(vp_buff_phy_addr);
	tvout_dbg("s5ptv_vp_buff vir_base = 0x%x\n", vp_buff_vir_addr);

	if (!vp_buff_vir_addr) {
		tvout_err("phys_to_virt failed\n");
		return 0;
	}

	for (i = 0; i < S5PTV_VP_BUFF_CNT; i++) {
		s5ptv_vp_buff.vp_buffs[i].phy_base =
		    vp_buff_phy_addr + (i * S5PTV_VP_BUFF_SIZE);
		s5ptv_vp_buff.vp_buffs[i].vir_base =
		    vp_buff_vir_addr + (i * S5PTV_VP_BUFF_SIZE);
	}

	return 1;
}
#endif
#else
static inline int alloc_vp_buff(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < S5PTV_VP_BUFF_CNT; i++) {
		s5ptv_vp_buff.vp_buffs[i].phy_base = 0;
		s5ptv_vp_buff.vp_buffs[i].vir_base = 0;
	}

	return 1;
}
#endif

static int __devinit s5p_tvout_probe(struct platform_device *pdev)
{
	int i;

#ifdef CONFIG_HDMI_EARJACK_MUTE
	struct class *hdmi_audio_class;
	struct device *hdmi_audio_dev;
#endif

#if defined(CONFIG_HDMI_TX_STRENGTH) && !defined(CONFIG_USER_ALLOC_TVOUT)
	struct s5p_platform_tvout *pdata;
	u8 tx_ch;
	u8 *tx_val;
#endif

#ifdef CONFIG_TVOUT_DEBUG
	struct class *sec_tvout;
	tvout_dbg_flag = 1 << DBG_FLAG_HPD;
#endif
	s5p_tvout_pm_runtime_enable(&pdev->dev);

#if defined(CONFIG_S5P_SYSMMU_TV) && defined(CONFIG_VCM)
	if (s5p_tvout_vcm_create_unified() < 0)
		goto err;

	if (s5p_tvout_vcm_init() < 0)
		goto err;
#elif defined(CONFIG_S5P_SYSMMU_TV) && defined(CONFIG_S5P_VMEM)
	s5p_sysmmu_enable(&pdev->dev);
	printk(KERN_WARNING "sysmmu on\n");
	s5p_sysmmu_set_tablebase_pgd(&pdev->dev, __pa(swapper_pg_dir));
#endif
	if (s5p_tvout_clk_get(pdev, &s5ptv_status) < 0)
		goto err;

	if (s5p_vp_ctrl_constructor(pdev) < 0)
		goto err;

	/* s5p_mixer_ctrl_constructor must be called
	   before s5p_tvif_ctrl_constructor */
	if (s5p_mixer_ctrl_constructor(pdev) < 0)
		goto err_mixer;

	if (s5p_tvif_ctrl_constructor(pdev) < 0)
		goto err_tvif;

	if (s5p_tvout_v4l2_constructor(pdev) < 0)
		goto err_v4l2;

#ifdef CONFIG_HAS_EARLYSUSPEND
	spin_lock_init(&s5ptv_status.tvout_lock);
	s5ptv_early_suspend.suspend = s5p_tvout_early_suspend;
	s5ptv_early_suspend.resume = s5p_tvout_late_resume;
	s5ptv_early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 4;
	register_early_suspend(&s5ptv_early_suspend);
	suspend_status = 0;
#endif

#ifdef CONFIG_TV_FB
#ifndef CONFIG_USER_ALLOC_TVOUT
	s5p_hdmi_phy_power(true);
	if (s5p_tvif_ctrl_start(TVOUT_720P_60, TVOUT_HDMI) < 0)
		goto err_tvif_start;
#ifdef CONFIG_HDMI_TX_STRENGTH
	pdata = to_tvout_plat(&pdev->dev);
	if (pdata && pdata->tx_tune) {
		tx_ch = pdata->tx_tune->tx_ch;
		tx_val = pdata->tx_tune->tx_val;
	}
	if (tx_ch && tx_val)
		s5p_hdmi_phy_set_tx_strength(tx_ch, tx_val);
#endif
#endif

	/* prepare memory */
	if (s5p_tvout_fb_alloc_framebuffer(&pdev->dev))
		goto err_tvif_start;

	if (s5p_tvout_fb_register_framebuffer(&pdev->dev))
		goto err_tvif_start;
#endif
	on_stop_process = false;
	on_start_process = false;

	if (!alloc_vp_buff(pdev))
		goto err_tvif_start;

	for (i = 0; i < S5PTV_VP_BUFF_CNT - 1; i++)
		s5ptv_vp_buff.copy_buff_idxs[i] = i;

	s5ptv_vp_buff.curr_copy_idx = 0;
	s5ptv_vp_buff.vp_access_buff_idx = S5PTV_VP_BUFF_CNT - 1;

#ifdef CONFIG_TVOUT_DEBUG
	tvout_dbg("Create tvout class sysfile\n");

	sec_tvout = class_create(THIS_MODULE, "tvout");
	if (IS_ERR(sec_tvout)) {
		tvout_err("Failed to create class(sec_tvout)!\n");
		goto err_class;
	}

	if (class_create_file(sec_tvout, &class_attr_dbg_msg) < 0) {
		tvout_err("failed to add sysfs entries\n");
		goto err_sysfs;
	}
#endif

	flag_after_resume = false;
#ifdef CONFIG_HDMI_EARJACK_MUTE
	hdmi_audio_class = class_create(THIS_MODULE, "hdmi_audio");
	if (IS_ERR(hdmi_audio_class))
		pr_err("Failed to create class(hdmi_audio)!\n");
	hdmi_audio_dev = device_create(hdmi_audio_class, NULL, 0, NULL,
							"hdmi_audio");
	if (IS_ERR(hdmi_audio_dev))
		pr_err("Failed to create device(hdmi_audio_dev)!\n");

	if (device_create_file(hdmi_audio_dev,
		&dev_attr_hdmi_audio_set_ext) < 0)
		printk(KERN_ERR "Failed to create device file(%s)!\n",
			dev_attr_hdmi_audio_set_ext.attr.name);

	hdmi_audio_ext = false;
#endif

	return 0;

err_sysfs:
	class_destroy(sec_tvout);
err_class:
err_tvif_start:
	s5p_tvout_v4l2_destructor();
err_v4l2:
	s5p_tvif_ctrl_destructor();
err_tvif:
	s5p_mixer_ctrl_destructor();
err_mixer:
	s5p_vp_ctrl_destructor();
err:
	return -ENODEV;
}

static int s5p_tvout_remove(struct platform_device *pdev)
{
#if defined(CONFIG_S5P_SYSMMU_TV) && defined(CONFIG_S5P_VMEM)
	s5p_sysmmu_off(&pdev->dev);
	tvout_dbg("sysmmu off\n");
#endif
	s5p_vp_ctrl_destructor();
	s5p_tvif_ctrl_destructor();
	s5p_mixer_ctrl_destructor();

	s5p_tvout_v4l2_destructor();

	clk_disable(s5ptv_status.sclk_hdmi);

	clk_put(s5ptv_status.sclk_hdmi);
	clk_put(s5ptv_status.sclk_dac);
	clk_put(s5ptv_status.sclk_pixel);
	clk_put(s5ptv_status.sclk_hdmiphy);

	s5p_tvout_pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
#ifdef CONFIG_HAS_EARLYSUSPEND
static void s5p_tvout_early_suspend(struct early_suspend *h)
{
	tvout_dbg("\n");
#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	mutex_lock(&s5p_tvout_mutex);
	/* disable vsync interrupt during early suspend */
	s5p_mixer_ctrl_disable_vsync_interrupt();
	s5p_vp_ctrl_suspend();
	s5p_mixer_ctrl_suspend();
	s5p_tvif_ctrl_suspend();
	suspend_status = 1;
	tvout_dbg("suspend_status is true\n");
	mutex_unlock(&s5p_tvout_mutex);
#else
	suspend_status = 1;
#endif

	return;
}

static void s5p_tvout_late_resume(struct early_suspend *h)
{
	tvout_dbg("\n");

#ifdef CLOCK_GATING_ON_EARLY_SUSPEND
	mutex_lock(&s5p_tvout_mutex);

#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
	if (flag_after_resume) {
		queue_work_on(0, tvout_resume_wq, &tvout_resume_work);
		flag_after_resume = false;
	}
#endif
	suspend_status = 0;
	tvout_dbg("suspend_status is false\n");

	s5p_tvif_ctrl_resume();
	s5p_mixer_ctrl_resume();
	s5p_vp_ctrl_resume();
	/* restore vsync interrupt setting */
	s5p_mixer_ctrl_set_vsync_interrupt(
		s5p_mixer_ctrl_get_vsync_interrupt());
	mutex_unlock(&s5p_tvout_mutex);
#else
	suspend_status = 0;
#endif

	return;
}

void s5p_tvout_mutex_lock()
{
	mutex_lock(&s5p_tvout_mutex);
}

void s5p_tvout_mutex_unlock()
{
	mutex_unlock(&s5p_tvout_mutex);
}
#endif

static void s5p_tvout_resume_work(void *arg)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	mutex_lock(&s5p_tvout_mutex);
#endif
	s5p_hdmi_ctrl_phy_power_resume();
#ifdef CONFIG_HAS_EARLYSUSPEND
	mutex_unlock(&s5p_tvout_mutex);
#endif
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static int s5p_tvout_suspend(struct device *dev)
{
	tvout_dbg("\n");
	return 0;
}

static int s5p_tvout_resume(struct device *dev)
{
	tvout_dbg("\n");
#if defined(CLOCK_GATING_ON_EARLY_SUSPEND)
#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
	flag_after_resume = true;
#endif
#else
	queue_work_on(0, tvout_resume_wq, &tvout_resume_work);
#endif
	return 0;
}
#else
static int s5p_tvout_suspend(struct device *dev)
{
	s5p_vp_ctrl_suspend();
	s5p_mixer_ctrl_suspend();
	s5p_tvif_ctrl_suspend();
	return 0;
}

static int s5p_tvout_resume(struct device *dev)
{
	s5p_tvif_ctrl_resume();
	s5p_mixer_ctrl_resume();
	s5p_vp_ctrl_resume();
	return 0;
}
#endif
static int s5p_tvout_runtime_suspend(struct device *dev)
{
	tvout_dbg("\n");
	return 0;
}

static int s5p_tvout_runtime_resume(struct device *dev)
{
	tvout_dbg("\n");
	return 0;
}
#else
#define s5p_tvout_suspend		NULL
#define s5p_tvout_resume		NULL
#define s5p_tvout_runtime_suspend	NULL
#define s5p_tvout_runtime_resume	NULL
#endif

static const struct dev_pm_ops s5p_tvout_pm_ops = {
	.suspend = s5p_tvout_suspend,
	.resume = s5p_tvout_resume,
	.runtime_suspend = s5p_tvout_runtime_suspend,
	.runtime_resume = s5p_tvout_runtime_resume
};

static struct platform_driver s5p_tvout_driver = {
	.probe = s5p_tvout_probe,
	.remove = s5p_tvout_remove,
	.driver = {
		   .name = "s5p-tvout",
		   .owner = THIS_MODULE,
		   .pm = &s5p_tvout_pm_ops},
};

static char banner[] __initdata =
	KERN_INFO "S5P TVOUT Driver v3.0 (c) 2010 Samsung Electronics\n";

static int __init s5p_tvout_init(void)
{
	int ret;

	printk(banner);

	ret = platform_driver_register(&s5p_tvout_driver);

	if (ret) {
		printk(KERN_ERR "Platform Device Register Failed %d\n", ret);

		return -1;
	}
#ifdef CONFIG_PM
	tvout_resume_wq = create_freezable_workqueue("tvout resume work");
	if (!tvout_resume_wq) {
		printk(KERN_ERR "Platform Device Register Failed %d\n", ret);
		platform_driver_unregister(&s5p_tvout_driver);
		return -1;
	}

	INIT_WORK(&tvout_resume_work, (work_func_t) s5p_tvout_resume_work);
#endif

	return 0;
}

static void __exit s5p_tvout_exit(void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	mutex_destroy(&s5p_tvout_mutex);
#endif
	platform_driver_unregister(&s5p_tvout_driver);
}

late_initcall(s5p_tvout_init);
module_exit(s5p_tvout_exit);

MODULE_AUTHOR("SangPil Moon");
MODULE_DESCRIPTION("S5P TVOUT driver");
MODULE_LICENSE("GPL");
