#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/cpufreq.h>
#include <linux/debugfs.h>
#include <linux/io.h>

#include <mach/clock.h>
#include "./csp_ccmu/ccmu/ccm_i.h"
#include "./csp_ccmu/csp_ccm_para.h"
#include "./csp_ccmu/csp_ccm_ops.h"


#define AW_CCMU_USED 1
#define AW_CCMU_FREE 0

#define AW_CCMU_CLK_ON  1
#define AW_CCMU_CLK_OFF 0

#define AW_AHB_CLOCK_LIMIT (240*1000*1000)

#define AW_CCMU_DIV_2   2
#define AW_CCMU_DIV_8   8
#define AW_CCMU_DIV_16  16
#define AW_CCMU_DIV_64  64

// declearation
static int aw_mod_clk_enable(struct clk *clk);
static void aw_mod_clk_disable(struct clk *clk);
static int aw_mod_clk_set_rate(struct clk *clk, unsigned long rate);
static unsigned long aw_mod_clk_get_rate(struct clk *clk);
static struct clk *aw_mod_clk_get_parent(struct clk *clk);
static int aw_mod_clk_set_parent(struct clk *clk, struct clk *parent);
static int aw_mod_clk_reset(struct clk *clk, int reset);

static int aw_sys_clk_enable(struct clk *clk);
static void aw_sys_clk_disable(struct clk *clk);
static int aw_sys_clk_set_freq(struct clk *clk, unsigned long freq);
static unsigned long aw_sys_clk_get_freq(struct clk *clk);
static struct clk *aw_sys_clk_get_parent(struct clk *clk);
static int aw_sys_clk_set_parent(struct clk *clk, struct clk *parent);



// system clock
static struct clk clk_hosc = {
    .clk_id         = CSP_CCM_SYS_CLK_HOSC,
    .parent_id      = -1,
    .usrcnt         = 0,
    .parent         = NULL,
    .freq           = 24000000,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_ON,
    .enable         = NULL,
    .disable        = NULL,
    .set_parent     = NULL,
    .get_parent     = NULL,
    .set_rate       = NULL,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_losc  = {
    .clk_id         = CSP_CCM_SYS_CLK_LOSC,
    .parent_id      = -1,
    .usrcnt         = 0,
    .parent         = NULL,
    .freq           = 32000,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_ON,
    .enable         = NULL,
    .disable        = NULL,
    .set_parent     = NULL,
    .get_parent     = NULL,
    .set_rate       = NULL,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_core_pll  = {
    .clk_id         = CSP_CCM_SYS_CLK_CORE_PLL,
    .parent_id      = CSP_CCM_SYS_CLK_HOSC,
    .usrcnt         = 0,
    .parent         = &clk_hosc,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_ve_pll  = {
    .clk_id         = CSP_CCM_SYS_CLK_VE_PLL,
    .parent_id      = CSP_CCM_SYS_CLK_HOSC,
    .usrcnt         = 0,
    .parent         = &clk_hosc,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_sdram_pll  = {
    .clk_id         = CSP_CCM_SYS_CLK_SDRAM_PLL,
    .parent_id      = CSP_CCM_SYS_CLK_HOSC,

    .usrcnt         = 0,
    .parent         = &clk_hosc,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_audio_pll_1x  = {
    .clk_id         = CSP_CCM_SYS_CLK_AUDIO_PLL,
    .parent_id      = -1,

    .usrcnt         = 0,
    .parent         = NULL,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_video_pll0_1x  = {
    .clk_id         = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .parent_id      = -1,

    .usrcnt         = 0,
    .parent         = NULL,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_video_pll1_1x  = {
    .clk_id         = CSP_CCM_SYS_CLK_VIDEO_PLL1,
    .parent_id      = -1,

    .usrcnt         = 0,
    .parent         = NULL,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_audio_pll_4x  = {
    .clk_id         = CSP_CCM_SYS_CLK_AUDIO_PLL_4X,
    .parent_id      = -1,

    .usrcnt         = 0,
    .parent         = NULL,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_audio_pll_8x  = {
    .clk_id         = CSP_CCM_SYS_CLK_AUDIO_PLL_8X,
    .parent_id      = -1,

    .usrcnt         = 0,
    .parent         = NULL,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_video_pll0_2x  = {
    .clk_id         = CSP_CCM_SYS_CLK_VIDEO_PLL0_2X,
    .parent_id      = -1,
    .usrcnt         = 0,
    .parent         = NULL,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_video_pll1_2x  = {
    .clk_id         = CSP_CCM_SYS_CLK_VIDEO_PLL1_2X,
    .parent_id      = -1,

    .usrcnt         = 0,
    .parent         = NULL,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_cpu  = {
    .clk_id         = CSP_CCM_SYS_CLK_CPU,
    .parent_id      = CSP_CCM_SYS_CLK_HOSC,

    .usrcnt         = 0,
    .parent         = &clk_hosc,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_ahb  = {
    .clk_id         = CSP_CCM_SYS_CLK_AHB,
    .parent_id      = CSP_CCM_SYS_CLK_CPU,

    .usrcnt         = 0,
    .parent         = &clk_cpu,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_apb  = {
    .clk_id         = CSP_CCM_SYS_CLK_APB,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_ON,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_sdram  = {
    .clk_id         = CSP_CCM_SYS_CLK_SDRAM,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM_PLL,
    .usrcnt         = 0,
    .parent         = &clk_sdram_pll,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_tvenc0  = {
    .clk_id         = CSP_CCM_SYS_CLK_TVENC_0,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};

static struct clk clk_tvenc1  = {
    .clk_id         = CSP_CCM_SYS_CLK_TVENC_1,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll1_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_sys_clk_enable,
    .disable        = aw_sys_clk_disable,
    .set_parent     = aw_sys_clk_set_parent,
    .get_parent     = aw_sys_clk_get_parent,
    .set_rate       = aw_sys_clk_set_freq,
    .get_rate       = aw_sys_clk_get_freq,
    .mod_reset      = NULL,
};


// module clock
static struct clk clk_nfc  = {
	.clk_id         = CSP_CCM_MOD_CLK_NFC,
	.parent_id      = CSP_CCM_SYS_CLK_SDRAM_PLL,
    .usrcnt         = 0,
    .parent         = &clk_sdram_pll,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_16,
};

static struct clk clk_msc  = {
    .clk_id         = CSP_CCM_MOD_CLK_MSC,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_16,
};

static struct clk clk_sdc0  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDC0,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_16,
};

static struct clk clk_sdc1  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDC1,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_16,
};

static struct clk clk_sdc2  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDC2,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_16,
};

static struct clk clk_sdc3  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDC3,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_16,
};

static struct clk clk_de_image1  = {
    .clk_id         = CSP_CCM_MOD_CLK_DE_IMAGE1,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = aw_mod_clk_reset,
    .div_max		= AW_CCMU_DIV_8,
};

static struct clk clk_de_image0  = {
    .clk_id         = CSP_CCM_MOD_CLK_DE_IMAGE0,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = aw_mod_clk_reset,
    .div_max		= AW_CCMU_DIV_8,
};

static struct clk clk_de_scale1  = {
    .clk_id         = CSP_CCM_MOD_CLK_DE_SCALE1,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = aw_mod_clk_reset,
    .div_max		= AW_CCMU_DIV_8,
};

static struct clk clk_de_scale0  = {
    .clk_id         = CSP_CCM_MOD_CLK_DE_SCALE0,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = aw_mod_clk_reset,
    .div_max		= AW_CCMU_DIV_8,
};

static struct clk clk_ve  = {
    .clk_id         = CSP_CCM_MOD_CLK_VE,
    .parent_id      = CSP_CCM_SYS_CLK_VE_PLL,
    .usrcnt         = 0,
    .parent         = &clk_ve_pll,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = aw_mod_clk_reset,
    .div_max		= 0,
};

static struct clk clk_csi1  = {
    .clk_id         = CSP_CCM_MOD_CLK_CSI1,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_16,
};

static struct clk clk_csi0  = {
    .clk_id         = CSP_CCM_MOD_CLK_CSI0,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_16,
};

static struct clk clk_ir  = {
    .clk_id         = CSP_CCM_MOD_CLK_IR,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_64,
};

static struct clk clk_ac97  = {
    .clk_id         = CSP_CCM_MOD_CLK_AC97,
    .parent_id      = CSP_CCM_SYS_CLK_AUDIO_PLL,
    .usrcnt         = 0,
    .parent         = &clk_audio_pll_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_i2s  = {
    .clk_id         = CSP_CCM_MOD_CLK_I2S,
    .parent_id      = CSP_CCM_SYS_CLK_AUDIO_PLL_8X,
    .usrcnt         = 0,
    .parent         = &clk_audio_pll_8x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_spdif  = {
    .clk_id         = CSP_CCM_MOD_CLK_SPDIF,
    .parent_id      = CSP_CCM_SYS_CLK_AUDIO_PLL_4X,
    .usrcnt         = 0,
    .parent         = &clk_audio_pll_4x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_audio_codec  = {
    .clk_id         = CSP_CCM_MOD_CLK_AUDIO_CODEC,
    .parent_id      = CSP_CCM_SYS_CLK_AUDIO_PLL,
    .usrcnt         = 0,
    .parent         = &clk_audio_pll_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ace  = {
    .clk_id         = CSP_CCM_MOD_CLK_ACE,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = aw_mod_clk_reset,
    .div_max		= AW_CCMU_DIV_8,
};

static struct clk clk_ss  = {
    .clk_id         = CSP_CCM_MOD_CLK_SS,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_16,
};

static struct clk clk_ts  = {
    .clk_id         = CSP_CCM_MOD_CLK_TS,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_16,
};

static struct clk clk_usb0  = {
    .clk_id         = CSP_CCM_MOD_CLK_USB_PHY0,
    .parent_id      = CSP_CCM_SYS_CLK_HOSC,
    .usrcnt         = 0,
    .parent         = &clk_hosc,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = aw_mod_clk_reset,
};

static struct clk clk_usb1  = {
    .clk_id         = CSP_CCM_MOD_CLK_USB_PHY1,
    .parent_id      = CSP_CCM_SYS_CLK_HOSC,
    .usrcnt         = 0,
    .parent         = &clk_hosc,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = aw_mod_clk_reset,
};

static struct clk clk_usb2  = {
    .clk_id         = CSP_CCM_MOD_CLK_USB_PHY2,
    .parent_id      = CSP_CCM_SYS_CLK_HOSC,
    .usrcnt         = 0,
    .parent         = &clk_hosc,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = aw_mod_clk_reset,
};

static struct clk clk_avs  = {
    .clk_id         = CSP_CCM_MOD_CLK_AVS,
    .parent_id      = CSP_CCM_SYS_CLK_HOSC,
    .usrcnt         = 0,
    .parent         = &clk_hosc,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ata  = {
    .clk_id         = CSP_CCM_MOD_CLK_ATA,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,

    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_16,
};

static struct clk clk_de_mix  = {
    .clk_id         = CSP_CCM_MOD_CLK_DE_MIX,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_8,
};

static struct clk clk_keypad  = {
    .clk_id         = CSP_CCM_MOD_CLK_KEY_PAD,
    .parent_id      = CSP_CCM_SYS_CLK_LOSC,
    .usrcnt         = 0,
    .parent         = &clk_losc,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_com  = {
    .clk_id         = CSP_CCM_MOD_CLK_COM,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_hosc,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = aw_mod_clk_reset,
};

static struct clk clk_tvenc_1x  = {
    .clk_id         = CSP_CCM_MOD_CLK_TVENC_1X,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_2,
};

static struct clk clk_tvenc_2x  = {
    .clk_id         = CSP_CCM_MOD_CLK_TVENC_2X,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_tcon0_0  = {
    .clk_id         = CSP_CCM_MOD_CLK_TCON0_0,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_tcon0_1  = {
    .clk_id         = CSP_CCM_MOD_CLK_TCON0_1,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_2,
};

static struct clk clk_tcon1_0  = {
    .clk_id         = CSP_CCM_MOD_CLK_TCON1_0,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_tcon1_1  = {
    .clk_id         = CSP_CCM_MOD_CLK_TCON1_1,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_1x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
    .div_max		= AW_CCMU_DIV_2,
};

static struct clk clk_lvds  = {
    .clk_id         = CSP_CCM_MOD_CLK_LVDS,
    .parent_id      = CSP_CCM_SYS_CLK_VIDEO_PLL0_2X,
    .usrcnt         = 0,
    .parent         = &clk_video_pll0_2x,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
// AHB CLOCK
static struct clk clk_dmac  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_DMAC,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_bist  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_BIST,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_emac  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_EMAC,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_spi0  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_SPI0,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_spi1  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_SPI1,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_spi2  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_SPI2,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
// APB CLOCK
static struct clk clk_twi2  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_TWI2,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_twi0  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_TWI0,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_twi1  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_TWI1,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_gpio  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_PIO,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_uart0  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_UART0,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_uart1  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_UART1,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_uart2  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_UART2,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_uart3  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_UART3,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_uart4  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_UART4,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_uart5  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_UART5,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_uart6  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_UART6,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_uart7  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_UART7,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};


static struct clk clk_ps0  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_PS0,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ps1  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_PS1,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_can  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_CAN,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_smc  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_SMC,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_apb_keypad  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_KEY_PAD,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_apb_audio_codec  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_AUDIO_CODEC,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_apb_ir  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_IR,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_apb_i2s  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_I2S,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_apb_spdif  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_SPDIF,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_apb_ac97  = {
    .clk_id         = CSP_CCM_MOD_CLK_APB_AC97,
    .parent_id      = CSP_CCM_SYS_CLK_APB,
    .usrcnt         = 0,
    .parent         = &clk_apb,
    .freq           = 0,
    .div            = 0,
    .onoff          = 0,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};


// AHB_XXX CLOCK
static struct clk clk_ahb_tvenc  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_TVENC,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ahb_csi0  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_CSI0,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ahb_csi1  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_CSI1,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ahb_tcon0  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_TCON0,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ahb_tcon1  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_TCON1,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ahb_ve  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_VE,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ahb_ts  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_TS,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_com  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_COM,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ahb_ace  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_ACE,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ahb_de_scale0  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_DE_SCALE0,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_de_scale1  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_DE_SCALE1,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_de_image1  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_DE_IMAGE1,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_de_image0  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_DE_IMAGE0,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_de_mix  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_DE_MIX,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_usb0  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_USB0,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ahb_usb1  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_USB1,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_usb2  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_USB2,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ahb_ss  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_SS,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_ata  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_ATA,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_sdc0  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_SDC0,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_sdc1  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_SDC1,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_sdc2  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_SDC2,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ahb_sdc3  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_SDC3,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_msc  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_MSC,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_ahb_nfc  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_NFC,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_ahb_sdramc  = {
    .clk_id         = CSP_CCM_MOD_CLK_AHB_SDRAMC,
    .parent_id      = CSP_CCM_SYS_CLK_AHB,
    .usrcnt         = 0,
    .parent         = &clk_ahb,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};





// SDRAM XXX CLOCK
static struct clk clk_sdram_ouput  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDRAM_OUTPUT,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_sdram,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};


static struct clk clk_sdram_de_scale0  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDRAM_DE_SCALE0,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_sdram,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_sdram_de_scale1  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDRAM_DE_SCALE1,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_sdram,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_sdram_de_image0  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDRAM_DE_IMAGE0,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_sdram,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_sdram_de_image1  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDRAM_DE_IMAGE1,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_sdram,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_sdram_csi0  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDRAM_CSI0,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_sdram,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_sdram_csi1  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDRAM_CSI1,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_sdram,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_sdram_de_mix  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDRAM_DE_MIX,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_sdram,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_sdram_ve  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDRAM_VE,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_sdram,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_sdram_ace  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDRAM_ACE,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_sdram,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};
static struct clk clk_sdram_ts  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDRAM_TS,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_sdram,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

static struct clk clk_sdram_com  = {
    .clk_id         = CSP_CCM_MOD_CLK_SDRAM_COM_ENGINE,
    .parent_id      = CSP_CCM_SYS_CLK_SDRAM,
    .usrcnt         = 0,
    .parent         = &clk_sdram,
    .freq           = 0,
    .div            = 0,
    .onoff          = AW_CCMU_CLK_OFF,
    .enable         = aw_mod_clk_enable,
    .disable        = aw_mod_clk_disable,
    .set_parent     = aw_mod_clk_set_parent,
    .get_parent     = aw_mod_clk_get_parent,
    .set_rate       = aw_mod_clk_set_rate,
    .get_rate       = aw_mod_clk_get_rate,
    .mod_reset      = NULL,
};

// must consistent with csp_ccmu
static struct clk * aw16xx_clk[] = {
    // sys clock start
    &clk_hosc,      // 0
    &clk_losc,

    &clk_core_pll,
    &clk_ve_pll,
    &clk_sdram_pll,
    &clk_audio_pll_1x,
    &clk_video_pll0_1x,
    &clk_video_pll1_1x,

    &clk_audio_pll_4x,
    &clk_audio_pll_8x,
    &clk_video_pll0_2x,
    &clk_video_pll1_2x,

    &clk_cpu,
    &clk_ahb,
    &clk_apb,
    &clk_sdram,

    &clk_tvenc0,
    &clk_tvenc1,     // 17
    // sys clock end

    // module clock
    &clk_nfc,        // 18
    &clk_msc,
    &clk_sdc0,
    &clk_sdc1,
    &clk_sdc2,
    &clk_sdc3,
    &clk_de_image1,
    &clk_de_image0,
    &clk_de_scale1,
    &clk_de_scale0,

    &clk_ve,
    &clk_csi1,
    &clk_csi0,
    &clk_ir,

    &clk_ac97,
    &clk_i2s,
    &clk_spdif,
    &clk_audio_codec,
    &clk_ace,

    &clk_ss,
    &clk_ts,

    &clk_usb0,
    &clk_usb1,
    &clk_usb2,
    &clk_avs,

    &clk_ata,
    &clk_de_mix,
    &clk_keypad,
    &clk_com,
    &clk_tvenc_1x, // TVE_CLK_1x, can source form TVE_CLK0 or TVE_CLK1
    &clk_tvenc_2x, // TVE_CLK_2x,

    &clk_tcon0_0,
    &clk_tcon0_1,
    &clk_tcon1_0,
    &clk_tcon1_1,
    &clk_lvds,

    // ahb source, ignore usb0-csi0,sdc0-3,csi0-1,sdram, etc...
    &clk_ahb_usb0,// usb0
    &clk_ahb_usb1,// usb1
    &clk_ahb_ss,// SS
    &clk_ahb_ata,// ATA
    &clk_ahb_tvenc,// TVENC
    &clk_ahb_csi0,// CSI0
    &clk_dmac,
    &clk_ahb_sdc0,// SDC0
    &clk_ahb_sdc1,// SDC1
    &clk_ahb_sdc2,// SDC2
    &clk_ahb_sdc3,// SDC3
    &clk_ahb_msc,// MSC
    &clk_ahb_nfc,// NFC
    &clk_ahb_sdramc,// SDRAMC
    &clk_ahb_tcon0,// TCON0
    &clk_ahb_ve,// VE
    &clk_bist,  // bist ???
    &clk_emac,
    &clk_ahb_ts,// TS
    &clk_spi0,
    &clk_spi1,
    &clk_spi2,
    &clk_ahb_usb2,// USB2
    &clk_ahb_csi1,// CSI1
    &clk_ahb_com,// COM
    &clk_ahb_ace,// ACE
    &clk_ahb_de_scale0,// DE_SCALE0
    &clk_ahb_de_image0,// DE_IMAGE0
    &clk_ahb_de_mix,// DE_MIX
    &clk_ahb_de_scale1,// DE_SCALE1
    &clk_ahb_de_image1,// DE_IMAGE1
    &clk_ahb_tcon1,// TCON1

    // apb source, ignore keypad,ir,audio, etc...
    &clk_apb_keypad,// keypad
    &clk_twi2,
    &clk_twi0,
    &clk_twi1,
    &clk_gpio,
    &clk_uart0,
    &clk_uart1,
    &clk_uart2,
    &clk_uart3,
    &clk_apb_audio_codec,// AUDIO CODEC
    &clk_apb_ir,//IR
    &clk_apb_i2s,// I2S
    &clk_apb_spdif,// SPDIF
    &clk_apb_ac97,// AC97
    &clk_ps0,
    &clk_ps1,
    &clk_uart4,
    &clk_uart5,
    &clk_uart6,
    &clk_uart7,
    &clk_can,
    &clk_smc,  // smart card controller

    &clk_sdram_ouput, // dram ouput
    &clk_sdram_de_scale0,
    &clk_sdram_de_scale1,
    &clk_sdram_de_image0,
    &clk_sdram_de_image1,
    &clk_sdram_csi0,
    &clk_sdram_csi1,
    &clk_sdram_de_mix,
    &clk_sdram_ve,
    &clk_sdram_ace,
    &clk_sdram_ts,
    &clk_sdram_com,
    NULL,
};


static u32 aw16xx_freq2div(struct clk *clk, unsigned long rate)
{
    u32 ret = 0;
    unsigned long parent_clk = 0;

    parent_clk = CSP_CCM_get_sys_clk_freq((CSP_CCM_sysClkNo_t)clk->parent_id); // get the latest system clock

    if(rate > parent_clk) {
        pr_debug("set freq = %d out range of parent clock.", (int)rate);
        return ret;
    }

    if(clk->div_max == 16) {
        ret = parent_clk/rate;
        if(ret > 16){
            pr_debug("divider = %d is out range[1-16].", ret);
            return 0;
        }
        return ret;
    }
    else if(clk->div_max == 8) {
        ret = parent_clk/rate;
        if(ret > 8){
            pr_debug("divider = %d is out range[1-8].", ret);
            return 0;
        }
        return ret;
    }
    else if(clk->div_max == 64) {
        ret = parent_clk/rate;
        if(ret > 64){
            pr_debug("divider = %d is out range[1-64].", ret);
            return 0;
        }
        return ret;
    }
    else if(clk->clk_id == CSP_CCM_MOD_CLK_I2S){
        ret = parent_clk/rate;
        if(ret > 8){
            pr_debug("divider = %d is out range[2,4,8].", ret);
            return 0;
        }
        if(ret < 3){ // 1,2
            ret = 2;
        } else if(ret < 6){ // 3,4,5
            ret = 4;
        } else { // 6,7,8
            ret = 8;
        }
        return ret;
    }
    else if(clk->div_max == 2){ // tvenc_1x
        ret = parent_clk/rate;
        if(ret > 2){
            pr_debug("divider = %d is out range[1,2].", ret);
            return 0;
        }
        return ret;
    }
    else if(clk->clk_id == CSP_CCM_MOD_CLK_KEY_PAD){
        ret = parent_clk/rate;
        if(ret > 256){
            pr_debug("divider = %d is out range[1,64,128,256].", ret);
            return 0;
        }
        if(ret < 32){ // 1-31
            ret = 1;
        } else if(ret < 95){ // 32- 94
            ret = 64;
        } else if (ret < 192) { // 95-191
            ret = 128;
        }else { // 192-256
            ret = 256;
        }
        return ret;
    }

    return 0;
}
// 1. get the parent clock
// 2. get the divider
// 3. divide
static unsigned long aw16xx_div2freq(struct clk *clk)
{
    unsigned long ret = 0;
    u32 div = 0;
    CSP_CCM_modClkInfo_t    tmpMclk;
    CSP_CCM_err_t           tmpErr;
    unsigned long parent_clk = CSP_CCM_get_sys_clk_freq((CSP_CCM_sysClkNo_t)clk->parent_id); // get the latest system clock

    if(parent_clk == FREQ_0){
        return ret;
    }

    tmpErr = CSP_CCM_get_mod_clk_info((CSP_CCM_modClkNo_t)clk->clk_id, &tmpMclk);
    if(tmpErr == CSP_CCM_ERR_NONE) {

        div  = tmpMclk.divideRatio;
        if(div != 0){
            ret  = parent_clk/div; //frequncey, save in the return
        }
        return ret;
    }
    else {
        return clk->freq; // return the old value
    }
}



// module clock operation

// module clock enable
// 1. check parent clock of module is on or off,if off then on;
// 2. enable ahb/apb gating bit and maybe enable sdram gating bit if parent clock is sdram;
// 3. enable module clock and set default divide ratio.
static int aw_mod_clk_enable(struct clk *clk)
{
    //CSP_CCM_modClkPara_t   tmpMclkPara;

    if(clk->onoff == AW_CCMU_CLK_ON) {
        pr_debug("module clock is on!\n");
	  //  return 0;
    }

    if(clk->enable) {
        if(clk->parent){
            if(clk->parent->onoff == AW_CCMU_CLK_OFF) {
                //on source clock
                CSP_CCM_set_sys_clk_status((CSP_CCM_sysClkNo_t)clk->parent_id, 1);
                clk->parent->onoff = AW_CCMU_CLK_ON;
            }
            //increase source clock user count
            clk->parent->usrcnt++;
        }
        // enable clock
        if(CSP_CCM_set_mod_clk_status((CSP_CCM_modClkNo_t)clk->clk_id, 1) != AW_CCMU_OK) {
                pr_debug("enable clock fail!\n");
                return -1;
        }
        clk->onoff = AW_CCMU_CLK_ON;
        return 0;
    }
    return -1;
}


// 1. if module is off, return immediately;
// 2. close the module clock, disabe ahb/apb gating;
// 3. if any, disable sdram gating;
// 4. if parent has no children, then disable parent clock.
static void aw_mod_clk_disable(struct clk *clk)
{
    if(clk->onoff == AW_CCMU_CLK_OFF) {
        pr_debug("module clock is off!\n");
      //  return;
    }
    if(clk->parent){
        // off module clock
        CSP_CCM_set_mod_clk_status((CSP_CCM_modClkNo_t)clk->clk_id, 0);

        // decrease source clock user count
        clk->parent->usrcnt--;
        // if parent is losc or hosc,etc...,can't set.
        if( clk->parent->usrcnt == 0) {
            //off source clock , power management
          //  CSP_CCM_set_sys_clk_status((CSP_CCM_sysClkNo_t)clk->parent_id, 0);
          //  clk->parent->onoff = AW_CCMU_CLK_OFF;
        }
        clk->onoff = AW_CCMU_CLK_OFF;// add this
    }
    return;
}


// 1. if off immediately return.
// 2. if module parent is ahb/apb,no need to set rate return.
// 3. if equal return.
// 4. rate :unit is HZ.
static int aw_mod_clk_set_rate(struct clk *clk, unsigned long rate)
{
    //check if module clock is on
    if(clk->onoff == AW_CCMU_CLK_OFF) {
        //clock is off, just modify the clock source
        // clk->div = rate;
          pr_debug("module clock is off!\n");
       // return -1;
    }

    if((clk->parent_id == CSP_CCM_SYS_CLK_AHB) ||
              (clk->parent_id == CSP_CCM_SYS_CLK_APB)||
                    (clk->parent_id == CSP_CCM_SYS_CLK_SDRAM) ) {
              pr_debug("AHB/APB/SDRAM clock,no need to set rate!\n");
        return 0;
    }

    //check if the clock source is changed,default divider is 1.
   // if(clk->freq !=  rate) {
   {
        CSP_CCM_modClkPara_t    tmpMclkPara;
        u16 is_on = 0;
        u16 div = aw16xx_freq2div(clk, rate);
        if(div == 0){
            pr_debug("set frequncy out of range!\n");
            return -1;
        }
        // keep the original status
        is_on = CSP_CCM_get_mod_clk_status((CSP_CCM_modClkNo_t)clk->clk_id) ?  1 : 0;
        tmpMclkPara.clkNo           = (CSP_CCM_modClkNo_t)clk->clk_id;
        tmpMclkPara.srcClk          = (CSP_CCM_sysClkNo_t)clk->parent_id;
        tmpMclkPara.isOn            = is_on;
        tmpMclkPara.divideRatio     = div; // can't be set to 1
        tmpMclkPara.resetIsValid    = 0;

        //set module clock frequency
        if(CSP_CCM_set_mod_clk_freq(&tmpMclkPara) != CSP_CCM_ERR_NONE){
            pr_debug("Try to set module clock frequency failed!\n");
            return -1;
        }
        clk->freq = rate;
        clk->div  = div;
    }

    return 0;
}



static unsigned long aw_mod_clk_get_rate(struct clk *clk)
{
    unsigned long freq = 0;
    //check if module clock is on
    if(clk->onoff == AW_CCMU_CLK_OFF){
        pr_debug("clock is off!\n");
        //clock is off, return error
    }
    //when source clock is off, return 0
    if(0 == CSP_CCM_get_mod_clk_status((CSP_CCM_modClkNo_t)clk->clk_id)){
        return freq;
    }
    // ahb,apb,sdram
    if((clk->parent_id == CSP_CCM_SYS_CLK_AHB) ||
              (clk->parent_id == CSP_CCM_SYS_CLK_APB)||
                    (clk->parent_id == CSP_CCM_SYS_CLK_SDRAM)){
        clk->freq = CSP_CCM_get_sys_clk_freq((CSP_CCM_sysClkNo_t)clk->parent_id); // get the latest system clock
        return clk->freq;
    }

    // module clock
    if(clk->clk_id <= CSP_CCM_MOD_CLK_LVDS){
        freq = aw16xx_div2freq(clk);
        if(freq != 0) {
            clk->freq = freq; // update
        }
    }
    return clk->freq; //
}




static struct clk *aw_mod_clk_get_parent(struct clk *clk)
{
    return clk->parent;
}

static int aw_mod_clk_set_parent(struct clk *clk, struct clk *parent)
{
    int ret   = 0;
    u16 is_on = 0;
    CSP_CCM_err_t           tmpErr;
    CSP_CCM_modClkInfo_t    tmpMclk;
    CSP_CCM_modClkPara_t    tmpMclkPara;

    //check if module clock is on
    if(clk->onoff == AW_CCMU_CLK_OFF) {
        //clock is off, just modify the clock source
      //  clk->parent_id  = parent->clk_id;
        pr_debug("clock is off,set parent!\n");
     //   return -1;
    }

    //check if the clock source is changed
    if(clk->parent_id != parent->clk_id) {
        tmpErr = CSP_CCM_get_mod_clk_info((CSP_CCM_modClkNo_t)clk->clk_id, &tmpMclk);
        if(tmpErr != CSP_CCM_ERR_NONE) {
            pr_debug("set parent: get mod info error!\n");
            return -1;
        }
        // keep the original status
        is_on = CSP_CCM_get_mod_clk_status((CSP_CCM_modClkNo_t)clk->clk_id) ?  1 : 0;

        tmpMclkPara.clkNo           = (CSP_CCM_modClkNo_t)clk->clk_id;
        tmpMclkPara.srcClk          = (CSP_CCM_sysClkNo_t)parent->clk_id;
        tmpMclkPara.isOn            = is_on; //  keep the original value
        tmpMclkPara.divideRatio     = tmpMclk.divideRatio;; // keep the original value
        tmpMclkPara.resetIsValid    = 0;

        //set module clock frequency
        if(CSP_CCM_set_mod_clk_freq(&tmpMclkPara) != CSP_CCM_ERR_NONE){
            pr_debug("Try to set module parent failed!\n");
            // restore the original value
            tmpMclkPara.clkNo           = (CSP_CCM_modClkNo_t)clk->clk_id;
            tmpMclkPara.srcClk          = (CSP_CCM_sysClkNo_t)clk->parent_id;
            tmpMclkPara.isOn            = is_on; //  restore the original value
            tmpMclkPara.divideRatio     = tmpMclk.divideRatio;; // restore the original value
            tmpMclkPara.resetIsValid    = 0;
            CSP_CCM_set_mod_clk_freq(&tmpMclkPara);
            return -1;
        }
        // change the parent info
        clk->parent_id = parent->clk_id;
        clk->parent    = parent;
    }

    return ret;
}

static int aw_mod_clk_reset(struct clk *clk, int reset)
{
    if(clk->mod_reset){
        if(CSP_CCM_ERR_NONE != CSP_CCM_mod_clk_reset_control((CSP_CCM_modClkNo_t)clk->clk_id, reset)){
            return -1;
        }
    }
    return 0;
}


// system clock operation

// system clock enable
static int aw_sys_clk_enable(struct clk *clk)
{
    if(clk->onoff == AW_CCMU_CLK_ON){
        pr_debug("system clock is on!\n");
      //  return 0;
    }

    if(clk->enable) {
        if(clk->parent){
            if(clk->parent->onoff == AW_CCMU_CLK_OFF) {
                //on source clock
                CSP_CCM_set_sys_clk_status((CSP_CCM_sysClkNo_t)clk->parent_id, 1);
                clk->parent->onoff = AW_CCMU_CLK_ON;
            }
            //increase source clock user count
            clk->parent->usrcnt++;
        }
        //set sys clock on
        if(CSP_CCM_set_sys_clk_status((CSP_CCM_sysClkNo_t)clk->clk_id, 1) != AW_CCMU_OK) {
            pr_debug("enable system clock fail!\n");
            return -1;
        }
        clk->onoff = AW_CCMU_CLK_ON;
        return 0;
    }
    return -1;
}


// system clock disable
static void aw_sys_clk_disable(struct clk *clk)
{
    if(clk->onoff == AW_CCMU_CLK_OFF) {
        pr_debug("system clock is off!\n");
      //  return;
    }
    // off system clock
    CSP_CCM_set_sys_clk_status((CSP_CCM_sysClkNo_t)clk->clk_id, 0);

    if(clk->parent){
        // decrease source clock user count
        clk->parent->usrcnt--;
        // power managment ,if parent is losc or hosc,can't set.
/*
        if( (clk->parent->usrcnt == 0)
                && (clk->parent->clk_id != CSP_CCM_SYS_CLK_LOSC)
                    && (clk->parent->clk_id != CSP_CCM_SYS_CLK_HOSC)
                        &&(clk->parent->clk_id != CSP_CCM_SYS_CLK_SDRAM_PLL))
        {
            //off source clock
            CSP_CCM_set_sys_clk_status((CSP_CCM_sysClkNo_t)clk->parent_id, 0);
            clk->parent->onoff = AW_CCMU_CLK_OFF;
        }
*/
    }
    clk->onoff = AW_CCMU_CLK_OFF;
    return;
}

// system clock get parent ,return NULL means no parent.
struct clk *aw_sys_clk_get_parent(struct clk *clk)
{
    return clk->parent;
}

// system has no set parent operation , set freq is okay.
int aw_sys_clk_set_parent(struct clk *clk, struct clk *parent)
{
    int ret = 0;

    return ret;
}


static unsigned long aw_sys_clk_get_freq(struct clk *clk)
{
    if(clk->clk_id < CSP_CCM_SYS_CLK_TOTAL_NUM) {
       clk->freq = CSP_CCM_get_sys_clk_freq((CSP_CCM_sysClkNo_t)clk->clk_id);
       return clk->freq;
    }
    return 0;
}

static int aw_sys_clk_set_freq(struct clk *clk, unsigned long freq)
{
    s32       ret = 0;

    //check if the clock frequency need be change
    if(clk->freq == freq) {
        //clock frequency need not be change
        pr_debug("equal with work frequency,needn't adjust!\n");
        return ret;
    }

    switch((CSP_CCM_sysClkNo_t)clk->clk_id) {
        //set core pll
       case CSP_CCM_SYS_CLK_CORE_PLL:
       {
#if 0
            //save the old frequency
            u32   tmpPllFreq = CSP_CCM_get_sys_clk_freq(CSP_CCM_SYS_CLK_CORE_PLL);
            u32   tmpCpuFreq = CSP_CCM_get_sys_clk_freq(CSP_CCM_SYS_CLK_CPU);
            u32   tmpAhbFreq = CSP_CCM_get_sys_clk_freq(CSP_CCM_SYS_CLK_AHB);
            u32   tmpApbFreq = CSP_CCM_get_sys_clk_freq(CSP_CCM_SYS_CLK_APB);

            //calculate new frequency for cpu/ahb/apb
            u32   tmpNewCpu = ((__u64)freq * tmpCpuFreq) / tmpPllFreq;
            u32   tmpNewAhb = ((__u64)tmpNewCpu * tmpAhbFreq) / tmpCpuFreq;
            u32   tmpNewApb = ((__u64)tmpNewAhb * tmpApbFreq) / tmpAhbFreq;

            //process some hardware limit
            if(tmpNewAhb > AW_AHB_CLOCK_LIMIT) {
                tmpNewAhb = tmpNewAhb>>1;
                tmpNewApb = tmpNewApb>>1;
            }

            //set ahb clock to 1/2 cpu clock, cpu clock to 1/2 core clock
            if(    (CSP_CCM_set_sys_clk_freq(CSP_CCM_SYS_CLK_AHB, tmpCpuFreq>>1) == CSP_CCM_ERR_NONE)
                && (CSP_CCM_set_sys_clk_freq(CSP_CCM_SYS_CLK_CPU, tmpPllFreq>>1) == CSP_CCM_ERR_NONE)
                && (CSP_CCM_set_sys_clk_freq(CSP_CCM_SYS_CLK_CORE_PLL, freq) == CSP_CCM_ERR_NONE)
                && (CSP_CCM_set_sys_clk_freq(CSP_CCM_SYS_CLK_CPU, tmpNewCpu) == CSP_CCM_ERR_NONE)
                && (CSP_CCM_set_sys_clk_freq(CSP_CCM_SYS_CLK_AHB, tmpNewAhb) == CSP_CCM_ERR_NONE)
                && (CSP_CCM_set_sys_clk_freq(CSP_CCM_SYS_CLK_APB, tmpNewApb) == CSP_CCM_ERR_NONE))
            {
                ret = 0;
            }
            else {
                CSP_CCM_set_sys_clk_freq(CSP_CCM_SYS_CLK_CORE_PLL, tmpPllFreq);
                CSP_CCM_set_sys_clk_freq(CSP_CCM_SYS_CLK_CPU, tmpCpuFreq);
                CSP_CCM_set_sys_clk_freq(CSP_CCM_SYS_CLK_AHB, tmpAhbFreq);
                CSP_CCM_set_sys_clk_freq(CSP_CCM_SYS_CLK_APB, tmpApbFreq);

                ret = -1;
            }
#endif
            break;
        }

        //set dram pll
        case CSP_CCM_SYS_CLK_SDRAM_PLL:
        {
            // unknown how to deal with dram change...
            // all the data in the dram may be destroyed...
            // lock the read/write, d-cache,i-cache...
            //------------------------------------------------------------------
            //how to process tlb ???
            //------------------------------------------------------------------
            //directly try to adjsut dram pll
            if(CSP_CCM_set_sys_clk_freq(CSP_CCM_SYS_CLK_SDRAM_PLL, freq) == CSP_CCM_ERR_NONE)
            {
                ret = 0;
            }
            else {
                ret = -1;
            }
            break;
        }

        //set other source clock
        default:
        {
            if(CSP_CCM_set_sys_clk_freq((CSP_CCM_sysClkNo_t)clk->clk_id, freq) == CSP_CCM_ERR_NONE)
            {
                ret = 0;
            }
            else {
                ret = -1;
            }

            break;
        }
    }

    return ret;
}



// clock initial

static unsigned int sys_clk_num = 0;
static unsigned int mod_clk_num = 0;

int aw16xx_clk_init(void)
{
    s32 i = 0;
    CSP_CCM_err_t           tmpErr;
    CSP_CCM_sysClkInfo_t    tmpSclk;
    CSP_CCM_modClkInfo_t    tmpMclk;
    struct clk **pclk = aw16xx_clk;
    struct clk *clk   = *pclk;

    //initialise clock controller unit
    if(AW_CCMU_FAIL == CSP_CCM_init()){
        pr_debug("csp ccmu initial failed!\n");
        return -1;
    }

    sys_clk_num = CSP_CCM_get_sys_clk_total_num();
    mod_clk_num = CSP_CCM_get_mod_clk_total_num();

    pr_debug("\n\n *** get system clock info *** \n");

    //build the clock configuration table

    // 1. fill source clock configuration table
    for(i = 0; i < sys_clk_num; i++)
    {
        if(*pclk == NULL) {
            pr_debug("source clock is NULL!");
            pclk++;
            continue;
        }
        tmpErr = CSP_CCM_get_sys_clk_info((CSP_CCM_sysClkNo_t)i, &tmpSclk);
        if(tmpErr == CSP_CCM_ERR_NONE){
            clk = *pclk;
            if(tmpSclk.pName) {
                clk->name = tmpSclk.pName;
            }

            clk->clk_id     = tmpSclk.clkId;
            clk->freq       = tmpSclk.freq;
            clk->usrcnt     = 0;
            clk->used       = AW_CCMU_FREE;
             // has parent or parent has change
            if( (tmpSclk.srcClkId != clk->parent_id) && (clk->parent != NULL) ){
                if(tmpSclk.srcClkId < CSP_CCM_SYS_CLK_TOTAL_NUM){// ignore no parent
                    clk->parent_id  = tmpSclk.srcClkId;
                    clk->parent     = aw16xx_clk[clk->parent_id]; // set parent
                }
            }
            //dram has been used by dram module already
            if(tmpSclk.clkId == CSP_CCM_SYS_CLK_SDRAM_PLL) {
                clk->usrcnt++;
            }
            pr_debug("clk_id = %d, parent_id = %d, freq = %d, name = %s,  parent name = %s \n",
               clk->clk_id, clk->parent_id, clk->freq, clk->name,(clk->parent  == NULL ? "NO parent" : clk->parent->name));
        }
        pclk++;// next source
    }

    pr_debug("\n\n *** get moudle clock info *** \n");

    // module clock start from eighteen( 18, nfc )
    // 2. file moudle clock configuration table
    for(i = 0 ; i < mod_clk_num; i++)
    {
        if( *pclk == NULL){
            pclk++;
            continue;
        }
        tmpErr = CSP_CCM_get_mod_clk_info((CSP_CCM_modClkNo_t)i, &tmpMclk);
        if(tmpErr == CSP_CCM_ERR_NONE) {
            clk = *pclk;
            if(tmpMclk.pName) {
                clk->name = tmpMclk.pName;
            }

            clk->clk_id     = tmpMclk.clkId;
            clk->div        = tmpMclk.divideRatio;
            clk->onoff      = AW_CCMU_CLK_OFF;
            clk->used       = AW_CCMU_FREE;
            clk->freq       = aw16xx_div2freq(clk);
            if( (clk->parent != NULL) && (clk->parent_id != tmpMclk.srcClkId) ){
                if(tmpMclk.srcClkId < CSP_CCM_SYS_CLK_TOTAL_NUM){// ignore no parent
                    clk->parent_id  = tmpMclk.srcClkId;
                    clk->parent     = aw16xx_clk[tmpMclk.srcClkId]; // set parent
                    clk->freq       = aw16xx_div2freq(clk);
                }
            }
            pr_debug("clk_id = %d, parent_id = %d, div = %d, freq = %d, name = %s,  parent name = %s \n",
              clk->clk_id, clk->parent_id, clk->div,  clk->freq,  clk->name, clk->parent->name);
        }
        pclk++;
    }
	return 0;
}
fs_initcall(aw16xx_clk_init);

// export symbol function
static LIST_HEAD(clocks); // temporary not used
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);

struct clk * clk_get(struct device *dev, const char *id)
{
	struct clk **pclk, *clk = NULL;
	s32 i = 0;
	u32 total = sys_clk_num + mod_clk_num;

	pr_debug("[%s] id=%s\n", __FUNCTION__, id);
	if(!id) {
		return NULL;
	}

	mutex_lock(&clocks_mutex);
	pclk = aw16xx_clk;
	for(i=0; i < total; i++) {
		if(*pclk == NULL) {
			pclk++;
			continue;
		}
		clk = *pclk;
		if (strcmp(id, clk->name) == 0 ) {
			pr_debug("clk_get: clock name = %s \n", clk->name);
			break;
		}
		pclk++;
	}
	// module clk: check if used by someone,locked;
	// system clk: used as counter,clk_get_rate may be called from lots of users.
	if(clk){
		if(clk->used == AW_CCMU_USED){
			clk = NULL;
		}else{
			clk->used = AW_CCMU_USED;
		}
	}
	mutex_unlock(&clocks_mutex);

	return clk;
}
EXPORT_SYMBOL(clk_get);



int clk_enable(struct clk *clk)
{
	unsigned long flags;
	int ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	if(clk->used == AW_CCMU_FREE){
	    pr_debug("clk ,used, para error!\n");
	    return -1;
	}

	spin_lock_irqsave(&clockfw_lock, flags);
	if (clk->enable){
		ret = clk->enable(clk);
	}
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_enable);



void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	if(clk->used == AW_CCMU_FREE){
	    pr_debug("clk ,used, para error!\n");
	    return;
	}

	spin_lock_irqsave(&clockfw_lock, flags);
	if (clk->disable){
		clk->disable(clk);
	}
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_disable);



unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long flags;
	unsigned long ret = 0;

	pr_debug("[%s] clk=%s\n", __FUNCTION__, clk->name);

	if (clk == NULL || IS_ERR(clk)){
		return 0;
	}

	spin_lock_irqsave(&clockfw_lock, flags);
	if(clk->get_rate){
	    ret = clk->get_rate(clk);
	}
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_get_rate);



int clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	int ret = 0;

	pr_debug("[%s] clk=%s, rate=%lu\n", __FUNCTION__, clk->name, rate);

	if (clk == NULL || IS_ERR(clk))
		return -1;

	if(clk->used == AW_CCMU_FREE){
	    pr_debug("clk ,used, para error!\n");
	    return -1;
	}

	spin_lock_irqsave(&clockfw_lock, flags);
	if(clk->set_rate){
	    ret = clk->set_rate(clk, rate);
	}
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);


struct clk *clk_get_parent(struct clk *clk)
{
	unsigned long flags;
	struct clk *parent = NULL;

	pr_debug("[%s] clk=%s\n", __FUNCTION__, clk->name);

	if (clk == NULL || IS_ERR(clk))
		return NULL;

	spin_lock_irqsave(&clockfw_lock, flags);
	if(clk->get_parent){
	    parent = clk->get_parent(clk);
	}
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return parent;
}
EXPORT_SYMBOL(clk_get_parent);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	unsigned long flags;
	int ret = -1;

	pr_debug("[%s] clk=%s\n", __FUNCTION__, clk->name);

	if (clk == NULL || clk->parent == NULL){
		return ret;
	}
	if (parent == NULL || IS_ERR(parent)){
		return ret;
	}

	if(clk->used == AW_CCMU_FREE){
	    pr_debug("clk ,used, para error!\n");
	    return ret;
	}

	spin_lock_irqsave(&clockfw_lock, flags);
	if(clk->set_parent){
		ret = clk->set_parent(clk, parent);
	}
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_set_parent);


void clk_put(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	if(clk->used == AW_CCMU_FREE){
	    pr_debug("clk ,used, para error!\n");
	    return;
	}

	spin_lock_irqsave(&clockfw_lock, flags);
	clk->used = AW_CCMU_FREE;
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_put);


int clk_reset(struct clk *clk, int reset)
{
	unsigned long flags;
	int ret = 0;

	pr_debug("[%s] clk=%s\n", __FUNCTION__, clk->name);

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	if(clk->used == AW_CCMU_FREE){
	    pr_debug("clk ,used, para error!\n");
	    return -1;
	}

	spin_lock_irqsave(&clockfw_lock, flags);
	if(clk->mod_reset){
		ret = clk->mod_reset(clk, reset);
	}
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_reset);
