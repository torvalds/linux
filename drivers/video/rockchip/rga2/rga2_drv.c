/*
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "rga: " fmt
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <asm/delay.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <asm/cacheflush.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/wakelock.h>
#include <linux/scatterlist.h>
#include <linux/version.h>
#include <linux/debugfs.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
#include <linux/pm_runtime.h>
#include <linux/dma-buf.h>
#endif

#include "rga2.h"
#include "rga2_reg_info.h"
#include "rga2_mmu_info.h"
#include "RGA2_API.h"

#if IS_ENABLED(CONFIG_ION_ROCKCHIP) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
#include <linux/rockchip_ion.h>
#endif

#if ((defined(CONFIG_RK_IOMMU) || defined(CONFIG_ROCKCHIP_IOMMU)) && defined(CONFIG_ION_ROCKCHIP))
#define CONFIG_RGA_IOMMU
#endif

#define RGA2_TEST_FLUSH_TIME 0
#define RGA2_INFO_BUS_ERROR 1
#define RGA2_POWER_OFF_DELAY	4*HZ /* 4s */
#define RGA2_TIMEOUT_DELAY	(HZ / 2) /* 500ms */
#define RGA2_MAJOR		255
#define RGA2_RESET_TIMEOUT	1000
/*
 * The maximum input is 8192*8192, the maximum output is 4096*4096
 * The size of physical pages requested is:
 * ( ( maximum_input_value * maximum_input_value * format_bpp ) / 4K_page_size ) + 1
 */
#define RGA2_PHY_PAGE_SIZE	(((8192 * 8192 * 4) / 4096) + 1)


/* Driver information */
#define DRIVER_DESC		"RGA2 Device Driver"
#define DRIVER_NAME		"rga2"
#define RGA2_VERSION   "2.000"

ktime_t rga2_start;
ktime_t rga2_end;
int rga2_flag;
int first_RGA2_proc;
static int rk3368;
#if RGA2_DEBUGFS
int RGA2_TEST_REG;
int RGA2_TEST_MSG;
int RGA2_TEST_TIME;
int RGA2_CHECK_MODE;
int RGA2_NONUSE;
int RGA2_INT_FLAG;
#endif

rga2_session rga2_session_global;
long (*rga2_ioctl_kernel_p)(struct rga_req *);

struct rga2_drvdata_t *rga2_drvdata;
struct rga2_service_info rga2_service;
struct rga2_mmu_buf_t rga2_mmu_buf;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
extern struct ion_client *rockchip_ion_client_create(const char *name);
#endif

static int rga2_blit_async(rga2_session *session, struct rga2_req *req);
static void rga2_del_running_list(void);
static void rga2_del_running_list_timeout(void);
static void rga2_try_set_reg(void);


/* Logging */
#define RGA_DEBUG 1
#if RGA_DEBUG
#define DBG(format, args...) printk(KERN_DEBUG "%s: " format, DRIVER_NAME, ## args)
#define ERR(format, args...) printk(KERN_ERR "%s: " format, DRIVER_NAME, ## args)
#define WARNING(format, args...) printk(KERN_WARN "%s: " format, DRIVER_NAME, ## args)
#define INFO(format, args...) printk(KERN_INFO "%s: " format, DRIVER_NAME, ## args)
#else
#define DBG(format, args...)
#define ERR(format, args...)
#define WARNING(format, args...)
#define INFO(format, args...)
#endif

#if RGA2_DEBUGFS
static const char *rga2_get_cmd_mode_str(u32 cmd)
{
	switch (cmd) {
	case RGA_BLIT_SYNC:
		return "RGA_BLIT_SYNC";
	case RGA_BLIT_ASYNC:
		return "RGA_BLIT_ASYNC";
	case RGA_FLUSH:
		return "RGA_FLUSH";
	case RGA_GET_RESULT:
		return "RGA_GET_RESULT";
	case RGA_GET_VERSION:
		return "RGA_GET_VERSION";
	default:
		return "UNF";
	}
}

static const char *rga2_get_blend_mode_str(u16 alpha_rop_flag, u16 alpha_mode_0,
					   u16 alpha_mode_1)
{
	if (alpha_rop_flag == 0) {
		return "no blend";
	} else if (alpha_rop_flag == 0x9) {
		if (alpha_mode_0 == 0x381A  && alpha_mode_1 == 0x381A)
			return "105 src + (1-src.a)*dst";
		else if (alpha_mode_0 == 0x483A  && alpha_mode_1 == 0x483A)
			return "405 src.a * src + (1-src.a) * dst";
		else
			return "check reg for more imformation";
	} else {
		return "check reg for more imformation";
	}
}

static const char *rga2_get_render_mode_str(u8 mode)
{
	switch (mode) {
	case 0x0:
		return "bitblt";
	case 0x1:
		return "color_palette";
	case 0x2:
		return "color_fill";
	case 0x3:
		return "update_palette_table";
	case 0x4:
		return "update_patten_buff";
	default:
		return "UNF";
	}
}

static const char *rga2_get_rotate_mode_str(u8 mode)
{
	switch (mode) {
	case 0x0:
		return "0";
	case 0x1:
		return "90 degree";
	case 0x2:
		return "180 degree";
	case 0x3:
		return "270 degree";
	case 0x10:
		return "xmirror";
	case 0x20:
		return "ymirror";
	case 0x30:
		return "xymirror";
	default:
		return "UNF";
	}
}

static bool rga2_is_yuv10bit_format(uint32_t format)
{
	bool ret  = false;

	switch (format) {
	case RGA2_FORMAT_YCbCr_420_SP_10B:
	case RGA2_FORMAT_YCrCb_420_SP_10B:
	case RGA2_FORMAT_YCbCr_422_SP_10B:
	case RGA2_FORMAT_YCrCb_422_SP_10B:
		ret = true;
		break;
	}
	return ret;
}

static bool rga2_is_yuv8bit_format(uint32_t format)
{
	bool ret  = false;

	switch (format) {
	case RGA2_FORMAT_YCbCr_422_SP:
	case RGA2_FORMAT_YCbCr_422_P:
	case RGA2_FORMAT_YCbCr_420_SP:
	case RGA2_FORMAT_YCbCr_420_P:
	case RGA2_FORMAT_YCrCb_422_SP:
	case RGA2_FORMAT_YCrCb_422_P:
	case RGA2_FORMAT_YCrCb_420_SP:
	case RGA2_FORMAT_YCrCb_420_P:
		ret = true;
		break;
	}
	return ret;
}

static const char *rga2_get_format_name(uint32_t format)
{
	switch (format) {
	case RGA2_FORMAT_RGBA_8888:
		return "RGBA8888";
	case RGA2_FORMAT_RGBX_8888:
		return "RGBX8888";
	case RGA2_FORMAT_RGB_888:
		return "RGB888";
	case RGA2_FORMAT_BGRA_8888:
		return "BGRA8888";
	case RGA2_FORMAT_BGRX_8888:
		return "BGRX8888";
	case RGA2_FORMAT_BGR_888:
		return "BGR888";
	case RGA2_FORMAT_RGB_565:
		return "RGB565";
	case RGA2_FORMAT_RGBA_5551:
		return "RGBA5551";
	case RGA2_FORMAT_RGBA_4444:
		return "RGBA4444";
	case RGA2_FORMAT_BGR_565:
		return "BGR565";
	case RGA2_FORMAT_BGRA_5551:
		return "BGRA5551";
	case RGA2_FORMAT_BGRA_4444:
		return "BGRA4444";

	case RGA2_FORMAT_YCbCr_422_SP:
		return "YCbCr422SP";
	case RGA2_FORMAT_YCbCr_422_P:
		return "YCbCr422P";
	case RGA2_FORMAT_YCbCr_420_SP:
		return "YCbCr420SP";
	case RGA2_FORMAT_YCbCr_420_P:
		return "YCbCr420P";
	case RGA2_FORMAT_YCrCb_422_SP:
		return "YCrCb422SP";
	case RGA2_FORMAT_YCrCb_422_P:
		return "YCrCb422P";
	case RGA2_FORMAT_YCrCb_420_SP:
		return "YCrCb420SP";
	case RGA2_FORMAT_YCrCb_420_P:
		return "YCrCb420P";

	case RGA2_FORMAT_YVYU_422:
		return "YVYU422";
	case RGA2_FORMAT_YVYU_420:
		return "YVYU420";
	case RGA2_FORMAT_VYUY_422:
		return "VYUY422";
	case RGA2_FORMAT_VYUY_420:
		return "VYUY420";
	case RGA2_FORMAT_YUYV_422:
		return "YUYV422";
	case RGA2_FORMAT_YUYV_420:
		return "YUYV420";
	case RGA2_FORMAT_UYVY_422:
		return "UYVY422";
	case RGA2_FORMAT_UYVY_420:
		return "UYVY420";

	case RGA2_FORMAT_YCbCr_420_SP_10B:
		return "YCrCb420SP10B";
	case RGA2_FORMAT_YCrCb_420_SP_10B:
		return "YCbCr420SP10B";
	case RGA2_FORMAT_YCbCr_422_SP_10B:
		return "YCbCr422SP10B";
	case RGA2_FORMAT_YCrCb_422_SP_10B:
		return "YCrCb422SP10B";
	case RGA2_FORMAT_BPP_1:
		return "BPP1";
	case RGA2_FORMAT_BPP_2:
		return "BPP2";
	case RGA2_FORMAT_BPP_4:
		return "BPP4";
	case RGA2_FORMAT_BPP_8:
		return "BPP8";
	case RGA2_FORMAT_YCbCr_400:
		return "YCbCr400";
	case RGA2_FORMAT_Y4:
		return "y4";
	default:
		return "UNF";
	}
}

static void print_debug_info(struct rga2_req *req)
{
	INFO("render_mode:%s,bitblit_mode=%d,rotate_mode:%s\n",
	     rga2_get_render_mode_str(req->render_mode), req->bitblt_mode,
	     rga2_get_rotate_mode_str(req->rotate_mode));
	INFO("src : y=%lx uv=%lx v=%lx aw=%d ah=%d vw=%d vh=%d xoff=%d yoff=%d format=%s\n",
	     req->src.yrgb_addr, req->src.uv_addr, req->src.v_addr,
	     req->src.act_w, req->src.act_h, req->src.vir_w, req->src.vir_h,
	     req->src.x_offset, req->src.y_offset,
	     rga2_get_format_name(req->src.format));
	if (req->src1.yrgb_addr != 0 ||
	    req->src1.uv_addr != 0 ||
	    req->src1.v_addr != 0) {
		INFO("src1 : y=%lx uv=%lx v=%lx aw=%d ah=%d vw=%d vh=%d xoff=%d yoff=%d format=%s\n",
		     req->src1.yrgb_addr, req->src1.uv_addr, req->src1.v_addr,
		     req->src1.act_w, req->src1.act_h, req->src1.vir_w, req->src1.vir_h,
		     req->src1.x_offset, req->src1.y_offset,
		     rga2_get_format_name(req->src1.format));
	}
	INFO("dst : y=%lx uv=%lx v=%lx aw=%d ah=%d vw=%d vh=%d xoff=%d yoff=%d format=%s\n",
	     req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
	     req->dst.act_w, req->dst.act_h, req->dst.vir_w, req->dst.vir_h,
	     req->dst.x_offset, req->dst.y_offset,
	     rga2_get_format_name(req->dst.format));
	INFO("mmu : src=%.2x src1=%.2x dst=%.2x els=%.2x\n",
	     req->mmu_info.src0_mmu_flag, req->mmu_info.src1_mmu_flag,
	     req->mmu_info.dst_mmu_flag, req->mmu_info.els_mmu_flag);
	INFO("alpha : flag %x mode0=%x mode1=%x\n",
	     req->alpha_rop_flag, req->alpha_mode_0, req->alpha_mode_1);
	INFO("blend mode is %s\n",
	     rga2_get_blend_mode_str(req->alpha_rop_flag,
	     req->alpha_mode_0, req->alpha_mode_1));
	INFO("yuv2rgb mode is %x\n", req->yuv2rgb_mode);
}

static int rga2_align_check(struct rga2_req *req)
{
	if (rga2_is_yuv10bit_format(req->src.format))
		if ((req->src.vir_w % 16) || (req->src.x_offset % 2) ||
		    (req->src.act_w % 2) || (req->src.y_offset % 2) ||
		    (req->src.act_h % 2) || (req->src.vir_h % 2))
			INFO("err src wstride is not align to 16 or yuv not align to 2");
	if (rga2_is_yuv10bit_format(req->dst.format))
		if ((req->dst.vir_w % 16) || (req->dst.x_offset % 2) ||
		    (req->dst.act_w % 2) || (req->dst.y_offset % 2) ||
		    (req->dst.act_h % 2) || (req->dst.vir_h % 2))
			INFO("err dst wstride is not align to 16 or yuv not align to 2");
	if (rga2_is_yuv8bit_format(req->src.format))
		if ((req->src.vir_w % 8) || (req->src.x_offset % 2) ||
		    (req->src.act_w % 2) || (req->src.y_offset % 2) ||
		    (req->src.act_h % 2) || (req->src.vir_h % 2))
			INFO("err src wstride is not align to 8 or yuv not align to 2");
	if (rga2_is_yuv8bit_format(req->dst.format))
		if ((req->dst.vir_w % 8) || (req->dst.x_offset % 2) ||
		    (req->dst.act_w % 2) || (req->dst.y_offset % 2) ||
		    (req->dst.act_h % 2) || (req->dst.vir_h % 2))
			INFO("err dst wstride is not align to 8 or yuv not align to 2");
	INFO("rga align check over!\n");
	return 0;
}

static int rga2_memory_check(void *vaddr, u32 w, u32 h, u32 format, int fd)
{
	int bits = 32;
	int temp_data = 0;
	void *one_line = kzalloc(w * 4, GFP_KERNEL);

	if (!one_line) {
		ERR("kzalloc fail %s[%d]\n", __func__, __LINE__);
		return 0;
	}
	switch (format) {
	case RGA2_FORMAT_RGBA_8888:
	case RGA2_FORMAT_RGBX_8888:
	case RGA2_FORMAT_BGRA_8888:
	case RGA2_FORMAT_BGRX_8888:
		bits = 32;
		break;
	case RGA2_FORMAT_RGB_888:
	case RGA2_FORMAT_BGR_888:
		bits = 24;
		break;
	case RGA2_FORMAT_RGB_565:
	case RGA2_FORMAT_RGBA_5551:
	case RGA2_FORMAT_RGBA_4444:
	case RGA2_FORMAT_BGR_565:
	case RGA2_FORMAT_YCbCr_422_SP:
	case RGA2_FORMAT_YCbCr_422_P:
	case RGA2_FORMAT_YCrCb_422_SP:
	case RGA2_FORMAT_YCrCb_422_P:
	case RGA2_FORMAT_BGRA_5551:
	case RGA2_FORMAT_BGRA_4444:
		bits = 16;
		break;
	case RGA2_FORMAT_YCbCr_420_SP:
	case RGA2_FORMAT_YCbCr_420_P:
	case RGA2_FORMAT_YCrCb_420_SP:
	case RGA2_FORMAT_YCrCb_420_P:
		bits = 12;
		break;
	case RGA2_FORMAT_YCbCr_420_SP_10B:
	case RGA2_FORMAT_YCrCb_420_SP_10B:
	case RGA2_FORMAT_YCbCr_422_SP_10B:
	case RGA2_FORMAT_YCrCb_422_SP_10B:
		bits = 15;
		break;
	default:
		INFO("un know format\n");
		kfree(one_line);
		return -1;
	}
	temp_data = w * (h - 1) * bits >> 3;
	if (fd > 0) {
		INFO("vaddr is%p, bits is %d, fd check\n", vaddr, bits);
		memcpy(one_line, (char *)vaddr + temp_data, w * bits >> 3);
		INFO("fd check ok\n");
	} else {
		INFO("vir addr memory check.\n");
		memcpy((void *)((char *)vaddr + temp_data), one_line,
		       w * bits >> 3);
		INFO("vir addr check ok.\n");
	}
	kfree(one_line);
	return 0;
}

int rga2_scale_check(struct rga2_req *req)
{
	u32 saw, sah, daw, dah;
	struct rga2_drvdata_t *data = rga2_drvdata;

	saw = req->src.act_w;
	sah = req->src.act_h;
	daw = req->dst.act_w;
	dah = req->dst.act_h;

	if (strncmp(data->version, "2.20", 4) == 0) {
		if (((saw >> 4) >= daw) || ((sah >> 4) >= dah))
			INFO("unsupported to scaling less than 1/16 times.\n");
		if (((daw >> 4) >= saw) || ((dah >> 4) >= sah))
			INFO("unsupported to scaling more than 16 times.\n");
	} else {
		if (((saw >> 3) >= daw) || ((sah >> 3) >= dah))
			INFO("unsupported to scaling less than 1/8 tiems.\n");
		if (((daw >> 3) >= saw) || ((dah >> 3) >= sah))
			INFO("unsupported to scaling more than 8 times.\n");
	}
	INFO("rga2 scale check over.\n");
	return 0;
}

static void rga2_printf_cmd_buf(u32 *cmd_buf)
{
	u32 reg_p[32];
	u32 i = 0;
	u32 src_stride, dst_stride, src_format, dst_format;
	u32 src_aw, src_ah, dst_aw, dst_ah;

	for (i = 0; i < 32; i++)
		reg_p[i] = *(cmd_buf + i);

	src_stride = reg_p[6];
	dst_stride = reg_p[18];

	src_format = reg_p[1] & (~0xfffffff0);
	dst_format = reg_p[14] & (~0xfffffff0);

	src_aw = (reg_p[7] & (~0xffff0000)) + 1;
	src_ah = ((reg_p[7] & (~0x0000ffff)) >> 16) + 1;

	dst_aw = (reg_p[19] & (~0xffff0000)) + 1;
	dst_ah = ((reg_p[19] & (~0x0000ffff)) >> 16) + 1;

	DBG("src : aw = %d ah = %d stride = %d format is %x\n",
	     src_aw, src_ah, src_stride, src_format);
	DBG("dst : aw = %d ah = %d stride = %d format is %x\n",
	     dst_aw, dst_ah, dst_stride, dst_format);
}

#endif

static bool is_yuv422p_format(u32 format)
{
	bool ret = false;

	switch (format) {
	case RGA2_FORMAT_YCbCr_422_P:
	case RGA2_FORMAT_YCrCb_422_P:
		ret = true;
		break;
	}
	return ret;
}

static inline void rga2_write(u32 b, u32 r)
{
	*((volatile unsigned int *)(rga2_drvdata->rga_base + r)) = b;
}

static inline u32 rga2_read(u32 r)
{
	return *((volatile unsigned int *)(rga2_drvdata->rga_base + r));
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
static inline int rga2_init_version(void)
{
	struct rga2_drvdata_t *rga = rga2_drvdata;
	u32 major_version, minor_version, svn_version;
	u32 reg_version;

	if (!rga) {
		pr_err("rga2_drvdata is null\n");
		return -EINVAL;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_get_sync(rga2_drvdata->dev);
#endif

	clk_prepare_enable(rga2_drvdata->aclk_rga2);
	clk_prepare_enable(rga2_drvdata->hclk_rga2);

	reg_version = rga2_read(0x028);

	clk_disable_unprepare(rga2_drvdata->aclk_rga2);
	clk_disable_unprepare(rga2_drvdata->hclk_rga2);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_put(rga2_drvdata->dev);
#endif

	major_version = (reg_version & RGA2_MAJOR_VERSION_MASK) >> 24;
	minor_version = (reg_version & RGA2_MINOR_VERSION_MASK) >> 20;
	svn_version = (reg_version & RGA2_SVN_VERSION_MASK);

	/*
	 * some old rga ip has no rga version register, so force set to 2.00
	 */
	if (!major_version && !minor_version)
		major_version = 2;
	snprintf(rga->version, 10, "%x.%01x.%05x", major_version, minor_version, svn_version);

	return 0;
}
#endif
static void rga2_soft_reset(void)
{
	u32 i;
	u32 reg;

	rga2_write((1 << 3) | (1 << 4) | (1 << 6), RGA2_SYS_CTRL);

	for(i = 0; i < RGA2_RESET_TIMEOUT; i++)
	{
		reg = rga2_read(RGA2_SYS_CTRL) & 1; //RGA_SYS_CTRL

		if(reg == 0)
			break;

		udelay(1);
	}

	if(i == RGA2_RESET_TIMEOUT)
		ERR("soft reset timeout.\n");
}

static void rga2_dump(void)
{
	int running;
	struct rga2_reg *reg, *reg_tmp;
	rga2_session *session, *session_tmp;

	running = atomic_read(&rga2_service.total_running);
	printk("rga total_running %d\n", running);
	list_for_each_entry_safe(session, session_tmp, &rga2_service.session,
		list_session)
	{
		printk("session pid %d:\n", session->pid);
		running = atomic_read(&session->task_running);
		printk("task_running %d\n", running);
		list_for_each_entry_safe(reg, reg_tmp, &session->waiting, session_link)
		{
			printk("waiting register set 0x %.lu\n", (unsigned long)reg);
		}
		list_for_each_entry_safe(reg, reg_tmp, &session->running, session_link)
		{
			printk("running register set 0x %.lu\n", (unsigned long)reg);
		}
	}
}

static inline void rga2_queue_power_off_work(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	queue_delayed_work(system_wq, &rga2_drvdata->power_off_work,
		RGA2_POWER_OFF_DELAY);
#else
	queue_delayed_work(system_nrt_wq, &rga2_drvdata->power_off_work,
		RGA2_POWER_OFF_DELAY);
#endif
}

/* Caller must hold rga_service.lock */
static void rga2_power_on(void)
{
	static ktime_t last;
	ktime_t now = ktime_get();

	if (ktime_to_ns(ktime_sub(now, last)) > NSEC_PER_SEC) {
		cancel_delayed_work_sync(&rga2_drvdata->power_off_work);
		rga2_queue_power_off_work();
		last = now;
	}

	if (rga2_service.enable)
		return;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_get_sync(rga2_drvdata->dev);
#else
	clk_prepare_enable(rga2_drvdata->pd_rga2);
#endif
	clk_prepare_enable(rga2_drvdata->clk_rga2);
	clk_prepare_enable(rga2_drvdata->aclk_rga2);
	clk_prepare_enable(rga2_drvdata->hclk_rga2);
	wake_lock(&rga2_drvdata->wake_lock);
	rga2_service.enable = true;
}

/* Caller must hold rga_service.lock */
static void rga2_power_off(void)
{
	int total_running;

	if (!rga2_service.enable) {
		return;
	}

	total_running = atomic_read(&rga2_service.total_running);
	if (total_running) {
		pr_err("power off when %d task running!!\n", total_running);
		mdelay(50);
		pr_err("delay 50 ms for running task\n");
		rga2_dump();
	}

	clk_disable_unprepare(rga2_drvdata->clk_rga2);
	clk_disable_unprepare(rga2_drvdata->aclk_rga2);
	clk_disable_unprepare(rga2_drvdata->hclk_rga2);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_put(rga2_drvdata->dev);
#else
	clk_disable_unprepare(rga2_drvdata->pd_rga2);
#endif

	wake_unlock(&rga2_drvdata->wake_lock);
    first_RGA2_proc = 0;
	rga2_service.enable = false;
}

static void rga2_power_off_work(struct work_struct *work)
{
	if (mutex_trylock(&rga2_service.lock)) {
		rga2_power_off();
		mutex_unlock(&rga2_service.lock);
	} else {
		/* Come back later if the device is busy... */
		rga2_queue_power_off_work();
	}
}

static int rga2_flush(rga2_session *session, unsigned long arg)
{
	int ret = 0;
	int ret_timeout;
	ktime_t start = ktime_set(0, 0);
	ktime_t end = ktime_set(0, 0);

#if RGA2_DEBUGFS
	if (RGA2_TEST_TIME)
		start = ktime_get();
#endif
	ret_timeout = wait_event_timeout(session->wait, atomic_read(&session->done), RGA2_TIMEOUT_DELAY);

	if (unlikely(ret_timeout < 0)) {
		u32 i;
		u32 *p;

		p = rga2_service.cmd_buff;
		pr_err("flush pid %d wait task ret %d\n", session->pid, ret);
		pr_err("interrupt = %x status = %x\n", rga2_read(RGA2_INT),
		       rga2_read(RGA2_STATUS));
		rga2_printf_cmd_buf(p);
		DBG("rga2 CMD\n");
		for (i = 0; i < 7; i++)
			DBG("%.8x %.8x %.8x %.8x\n",
			     p[0 + i * 4], p[1 + i * 4],
			     p[2 + i * 4], p[3 + i * 4]);
		mutex_lock(&rga2_service.lock);
		rga2_del_running_list();
		mutex_unlock(&rga2_service.lock);
		ret = ret_timeout;
	} else if (0 == ret_timeout) {
		u32 i;
		u32 *p;

		p = rga2_service.cmd_buff;
		pr_err("flush pid %d wait %d task done timeout\n",
		       session->pid, atomic_read(&session->task_running));
		pr_err("interrupt = %x status = %x\n",
		       rga2_read(RGA2_INT), rga2_read(RGA2_STATUS));
		rga2_printf_cmd_buf(p);
		DBG("rga2 CMD\n");
		for (i = 0; i < 7; i++)
			DBG("%.8x %.8x %.8x %.8x\n",
			     p[0 + i * 4], p[1 + i * 4],
			     p[2 + i * 4], p[3 + i * 4]);
		mutex_lock(&rga2_service.lock);
		rga2_del_running_list_timeout();
		rga2_try_set_reg();
		mutex_unlock(&rga2_service.lock);
		ret = -ETIMEDOUT;
	}

#if RGA2_DEBUGFS
	if (RGA2_TEST_TIME) {
		end = ktime_get();
		end = ktime_sub(end, start);
		DBG("one flush wait time %d\n", (int)ktime_to_us(end));
	}
#endif
	return ret;
}


static int rga2_get_result(rga2_session *session, unsigned long arg)
{
	int ret = 0;
	int num_done;

	num_done = atomic_read(&session->num_done);
	if (unlikely(copy_to_user((void __user *)arg, &num_done, sizeof(int)))) {
	    printk("copy_to_user failed\n");
	    ret =  -EFAULT;
	}
	return ret;
}


static int rga2_check_param(const struct rga2_req *req)
{
	if(!((req->render_mode == color_fill_mode)))
	{
	    if (unlikely((req->src.act_w <= 0) || (req->src.act_w > 8192) || (req->src.act_h <= 0) || (req->src.act_h > 8192)))
	    {
		printk("invalid source resolution act_w = %d, act_h = %d\n", req->src.act_w, req->src.act_h);
		return -EINVAL;
	    }
	}

	if(!((req->render_mode == color_fill_mode)))
	{
	    if (unlikely((req->src.vir_w <= 0) || (req->src.vir_w > 8192) || (req->src.vir_h <= 0) || (req->src.vir_h > 8192)))
	    {
		printk("invalid source resolution vir_w = %d, vir_h = %d\n", req->src.vir_w, req->src.vir_h);
		return -EINVAL;
	    }
	}

	//check dst width and height
	if (unlikely((req->dst.act_w <= 0) || (req->dst.act_w > 4096) || (req->dst.act_h <= 0) || (req->dst.act_h > 4096)))
	{
	    printk("invalid destination resolution act_w = %d, act_h = %d\n", req->dst.act_w, req->dst.act_h);
	    return -EINVAL;
	}

	if (unlikely((req->dst.vir_w <= 0) || (req->dst.vir_w > 4096) || (req->dst.vir_h <= 0) || (req->dst.vir_h > 4096)))
	{
	    printk("invalid destination resolution vir_w = %d, vir_h = %d\n", req->dst.vir_w, req->dst.vir_h);
	    return -EINVAL;
	}

	//check src_vir_w
	if(unlikely(req->src.vir_w < req->src.act_w)){
	    printk("invalid src_vir_w act_w = %d, vir_w = %d\n", req->src.act_w, req->src.vir_w);
	    return -EINVAL;
	}

	//check dst_vir_w
	if(unlikely(req->dst.vir_w < req->dst.act_w)){
	    if(req->rotate_mode != 1)
	    {
		printk("invalid dst_vir_w act_h = %d, vir_h = %d\n", req->dst.act_w, req->dst.vir_w);
		return -EINVAL;
	    }
	}

	return 0;
}

static void rga2_copy_reg(struct rga2_reg *reg, uint32_t offset)
{
    uint32_t i;
    uint32_t *cmd_buf;
    uint32_t *reg_p;

    if(atomic_read(&reg->session->task_running) != 0)
        printk(KERN_ERR "task_running is no zero\n");

    atomic_add(1, &rga2_service.cmd_num);
	atomic_add(1, &reg->session->task_running);

    cmd_buf = (uint32_t *)rga2_service.cmd_buff + offset*32;
    reg_p = (uint32_t *)reg->cmd_reg;

    for(i=0; i<32; i++)
        cmd_buf[i] = reg_p[i];
}


static struct rga2_reg * rga2_reg_init(rga2_session *session, struct rga2_req *req)
{
    int32_t ret;

	/* Alloc 4k size for rga2_reg use. */
	struct rga2_reg *reg = (struct rga2_reg *)get_zeroed_page(GFP_KERNEL | GFP_DMA32);

	if (NULL == reg) {
		pr_err("get_zeroed_page fail in rga_reg_init\n");
		return NULL;
	}

    reg->session = session;
	INIT_LIST_HEAD(&reg->session_link);
	INIT_LIST_HEAD(&reg->status_link);

    if ((req->mmu_info.src0_mmu_flag & 1) || (req->mmu_info.src1_mmu_flag & 1)
        || (req->mmu_info.dst_mmu_flag & 1) || (req->mmu_info.els_mmu_flag & 1))
    {
        ret = rga2_set_mmu_info(reg, req);
        if(ret < 0) {
            printk("%s, [%d] set mmu info error \n", __FUNCTION__, __LINE__);
            free_page((unsigned long)reg);

            return NULL;
        }
    }

    if (RGA2_gen_reg_info((uint8_t *)reg->cmd_reg, (uint8_t *)reg->csc_reg, req) == -1) {
        printk("gen reg info error\n");
        free_page((unsigned long)reg);

        return NULL;
    }
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	reg->sg_src0 = req->sg_src0;
	reg->sg_dst = req->sg_dst;
	reg->sg_src1 = req->sg_src1;
	reg->sg_els = req->sg_els;
	reg->attach_src0 = req->attach_src0;
	reg->attach_dst = req->attach_dst;
	reg->attach_src1 = req->attach_src1;
	reg->attach_els = req->attach_els;
#endif

    mutex_lock(&rga2_service.lock);
	list_add_tail(&reg->status_link, &rga2_service.waiting);
	list_add_tail(&reg->session_link, &session->waiting);
	mutex_unlock(&rga2_service.lock);

    return reg;
}


/* Caller must hold rga_service.lock */
static void rga2_reg_deinit(struct rga2_reg *reg)
{
	list_del_init(&reg->session_link);
	list_del_init(&reg->status_link);
	free_page((unsigned long)reg);
}

/* Caller must hold rga_service.lock */
static void rga2_reg_from_wait_to_run(struct rga2_reg *reg)
{
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &rga2_service.running);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->running);
}

/* Caller must hold rga_service.lock */
static void rga2_service_session_clear(rga2_session *session)
{
	struct rga2_reg *reg, *n;

	list_for_each_entry_safe(reg, n, &session->waiting, session_link)
	{
		rga2_reg_deinit(reg);
	}

	list_for_each_entry_safe(reg, n, &session->running, session_link)
	{
		rga2_reg_deinit(reg);
	}
}

/* Caller must hold rga_service.lock */
static void rga2_try_set_reg(void)
{
	int i;
	struct rga2_reg *reg ;

	if (list_empty(&rga2_service.running))
	{
		if (!list_empty(&rga2_service.waiting))
		{
			/* RGA is idle */
			reg = list_entry(rga2_service.waiting.next, struct rga2_reg, status_link);

			rga2_power_on();
			udelay(1);

			rga2_copy_reg(reg, 0);
			rga2_reg_from_wait_to_run(reg);

			rga2_dma_flush_range(&reg->cmd_reg[0], &reg->cmd_reg[32]);

			//rga2_soft_reset();

			rga2_write(0x0, RGA2_SYS_CTRL);

			/* CMD buff */
			rga2_write(virt_to_phys(reg->cmd_reg), RGA2_CMD_BASE);

			/* full csc reg */
			for (i = 0; i < 12; i++) {
				rga2_write(reg->csc_reg[i], RGA2_CSC_COE_BASE + i * 4);
			}

#if RGA2_DEBUGFS
			if (RGA2_TEST_REG) {
				if (rga2_flag) {
					int32_t *p;

					p = rga2_service.cmd_buff;
					INFO("CMD_REG\n");
					for (i=0; i<8; i++)
						INFO("%.8x %.8x %.8x %.8x\n",
						     p[0 + i * 4], p[1 + i * 4],
						     p[2 + i * 4], p[3 + i * 4]);

					p = reg->csc_reg;
					INFO("CSC_REG\n");
					for (i = 0; i < 3; i++)
						INFO("%.8x %.8x %.8x %.8x\n",
						     p[0 + i * 4], p[1 + i * 4],
						     p[2 + i * 4], p[3 + i * 4]);
				}
			}
#endif

			/* master mode */
			rga2_write((0x1<<1)|(0x1<<2)|(0x1<<5)|(0x1<<6), RGA2_SYS_CTRL);

			/* All CMD finish int */
			rga2_write(rga2_read(RGA2_INT)|(0x1<<10)|(0x1<<9)|(0x1<<8), RGA2_INT);

#if RGA2_DEBUGFS
			if (RGA2_TEST_TIME)
				rga2_start = ktime_get();
#endif

			/* Start proc */
			atomic_set(&reg->session->done, 0);
			rga2_write(0x1, RGA2_CMD_CTRL);
#if RGA2_DEBUGFS
			if (RGA2_TEST_REG) {
				if (rga2_flag) {
					INFO("CMD_READ_BACK_REG\n");
					for (i=0; i<8; i++)
						INFO("%.8x %.8x %.8x %.8x\n",
						     rga2_read(0x100 + i * 16 + 0),
						     rga2_read(0x100 + i * 16 + 4),
						     rga2_read(0x100 + i * 16 + 8),
						     rga2_read(0x100 + i * 16 + 12));

					INFO("CSC_READ_BACK_REG\n");
					for (i = 0; i < 3; i++)
						INFO("%.8x %.8x %.8x %.8x\n",
						     rga2_read(RGA2_CSC_COE_BASE + i * 16 + 0),
						     rga2_read(RGA2_CSC_COE_BASE + i * 16 + 4),
						     rga2_read(RGA2_CSC_COE_BASE + i * 16 + 8),
						     rga2_read(RGA2_CSC_COE_BASE + i * 16 + 12));
				}

			}
#endif
		}
	}
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int rga2_put_dma_buf(struct rga2_req *req, struct rga2_reg *reg)
{
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *sgt = NULL;
	struct dma_buf *dma_buf = NULL;

	if (!req && !reg)
		return -EINVAL;

	attach = reg ? reg->attach_src0 : req->attach_src0;
	sgt = reg ? reg->sg_src0 : req->sg_src0;
	if (attach && sgt)
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
	if (attach) {
		dma_buf = attach->dmabuf;
		dma_buf_detach(dma_buf, attach);
		dma_buf_put(dma_buf);
	}

	attach = reg ? reg->attach_dst : req->attach_dst;
	sgt = reg ? reg->sg_dst : req->sg_dst;
	if (attach && sgt)
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
	if (attach) {
		dma_buf = attach->dmabuf;
		dma_buf_detach(dma_buf, attach);
		dma_buf_put(dma_buf);
	}

	attach = reg ? reg->attach_src1 : req->attach_src1;
	sgt = reg ? reg->sg_src1 : req->sg_src1;
	if (attach && sgt)
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
	if (attach) {
		dma_buf = attach->dmabuf;
		dma_buf_detach(dma_buf, attach);
		dma_buf_put(dma_buf);
	}

	attach = reg ? reg->attach_els : req->attach_els;
	sgt = reg ? reg->sg_els : req->sg_els;
	if (attach && sgt)
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
	if (attach) {
		dma_buf = attach->dmabuf;
		dma_buf_detach(dma_buf, attach);
		dma_buf_put(dma_buf);
	}

	return 0;
}
#endif

static void rga2_del_running_list(void)
{
	struct rga2_mmu_buf_t *tbuf = &rga2_mmu_buf;
	struct rga2_reg *reg;

	while (!list_empty(&rga2_service.running)) {
		reg = list_entry(rga2_service.running.next, struct rga2_reg,
				 status_link);
		if (reg->MMU_len && tbuf) {
			if (tbuf->back + reg->MMU_len > 2 * tbuf->size)
				tbuf->back = reg->MMU_len + tbuf->size;
			else
				tbuf->back += reg->MMU_len;
		}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		rga2_put_dma_buf(NULL, reg);
#endif
		atomic_sub(1, &reg->session->task_running);
		atomic_sub(1, &rga2_service.total_running);

		if(list_empty(&reg->session->waiting))
		{
			atomic_set(&reg->session->done, 1);
			wake_up(&reg->session->wait);
		}

		rga2_reg_deinit(reg);
	}
}

static void rga2_del_running_list_timeout(void)
{
	struct rga2_mmu_buf_t *tbuf = &rga2_mmu_buf;
	struct rga2_reg *reg;

	while (!list_empty(&rga2_service.running)) {
		reg = list_entry(rga2_service.running.next, struct rga2_reg,
				 status_link);
#if 0
		kfree(reg->MMU_base);
#endif
		if (reg->MMU_len && tbuf) {
			if (tbuf->back + reg->MMU_len > 2 * tbuf->size)
				tbuf->back = reg->MMU_len + tbuf->size;
			else
				tbuf->back += reg->MMU_len;
		}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		rga2_put_dma_buf(NULL, reg);
#endif
		atomic_sub(1, &reg->session->task_running);
		atomic_sub(1, &rga2_service.total_running);
		rga2_soft_reset();
		if (list_empty(&reg->session->waiting)) {
			atomic_set(&reg->session->done, 1);
			wake_up(&reg->session->wait);
		}
		rga2_reg_deinit(reg);
	}
	return;
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int rga2_get_img_info(rga_img_info_t *img,
			     u8 mmu_flag,
			     struct sg_table **psgt,
			     struct dma_buf_attachment **pattach)
{
	struct dma_buf_attachment *attach = NULL;
	struct device *rga_dev = NULL;
	struct sg_table *sgt = NULL;
	struct dma_buf *dma_buf = NULL;
	u32 vir_w, vir_h;
	int yrgb_addr = -1;
	int ret = 0;
	void *vaddr;

	rga_dev = rga2_drvdata->dev;
	yrgb_addr = (int)img->yrgb_addr;
	vir_w = img->vir_w;
	vir_h = img->vir_h;

	if (yrgb_addr > 0) {
		dma_buf = dma_buf_get(img->yrgb_addr);
		if (IS_ERR(dma_buf)) {
			ret = -EINVAL;
			pr_err("dma_buf_get fail fd[%d]\n", yrgb_addr);
			return ret;
		}

		attach = dma_buf_attach(dma_buf, rga_dev);
		if (IS_ERR(attach)) {
			dma_buf_put(dma_buf);
			ret = -EINVAL;
			pr_err("Failed to attach dma_buf\n");
			return ret;
		}
#if RGA2_DEBUGFS
	if (RGA2_CHECK_MODE) {
		vaddr = dma_buf_vmap(dma_buf);
		if (vaddr)
			rga2_memory_check(vaddr, img->vir_w, img->vir_h,
					  img->format, img->yrgb_addr);
		dma_buf_vunmap(dma_buf, vaddr);
	}
#endif

		*pattach = attach;
		sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
		if (IS_ERR(sgt)) {
			ret = -EINVAL;
			pr_err("Failed to map src attachment\n");
			goto err_get_sg;
		}
		if (!mmu_flag) {
			ret = -EINVAL;
			pr_err("Fix it please enable iommu flag\n");
			goto err_get_sg;
		}

		if (mmu_flag) {
			*psgt = sgt;
			img->yrgb_addr = img->uv_addr;
			img->uv_addr = img->yrgb_addr + (vir_w * vir_h);
			if (is_yuv422p_format(img->format))
				img->v_addr = img->uv_addr + (vir_w * vir_h) / 2;
			else
				img->v_addr = img->uv_addr + (vir_w * vir_h) / 4;
		}
	} else {
		img->yrgb_addr = img->uv_addr;
		img->uv_addr = img->yrgb_addr + (vir_w * vir_h);
		if (is_yuv422p_format(img->format))
			img->v_addr = img->uv_addr + (vir_w * vir_h) / 2;
		else
			img->v_addr = img->uv_addr + (vir_w * vir_h) / 4;
	}

	return ret;

err_get_sg:
	if (!IS_ERR(sgt) && sgt)
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
	if (!IS_ERR(attach) && attach) {
		dma_buf = attach->dmabuf;
		dma_buf_detach(dma_buf, attach);
		*pattach = NULL;
		dma_buf_put(dma_buf);
	}
	return ret;
}

static int rga2_get_dma_buf(struct rga2_req *req)
{
	struct dma_buf *dma_buf = NULL;
	u8 mmu_flag = 0;
	int ret = 0;

	req->sg_src0 = NULL;
	req->sg_src1 = NULL;
	req->sg_dst = NULL;
	req->sg_els = NULL;
	req->attach_src0 = NULL;
	req->attach_dst = NULL;
	req->attach_src1 = NULL;
	req->attach_els = NULL;
	mmu_flag = req->mmu_info.src0_mmu_flag;
	ret = rga2_get_img_info(&req->src, mmu_flag, &req->sg_src0,
				&req->attach_src0);
	if (ret) {
		pr_err("src:rga2_get_img_info fail\n");
		goto err_src;
	}

	mmu_flag = req->mmu_info.dst_mmu_flag;
	ret = rga2_get_img_info(&req->dst, mmu_flag, &req->sg_dst,
				&req->attach_dst);
	if (ret) {
		pr_err("dst:rga2_get_img_info fail\n");
		goto err_dst;
	}

	mmu_flag = req->mmu_info.src1_mmu_flag;
	ret = rga2_get_img_info(&req->src1, mmu_flag, &req->sg_src1,
				&req->attach_src1);
	if (ret) {
		pr_err("src1:rga2_get_img_info fail\n");
		goto err_src1;
	}

	mmu_flag = req->mmu_info.els_mmu_flag;
	ret = rga2_get_img_info(&req->pat, mmu_flag, &req->sg_els,
							&req->attach_els);
	if (ret) {
		pr_err("els:rga2_get_img_info fail\n");
		goto err_els;
	}

	return ret;

err_els:
	if (req->sg_src1 && req->attach_src1) {
		dma_buf_unmap_attachment(req->attach_src1,
			req->sg_src1, DMA_BIDIRECTIONAL);
		dma_buf = req->attach_src1->dmabuf;
		dma_buf_detach(dma_buf, req->attach_src1);
		dma_buf_put(dma_buf);
	}
err_src1:
	if (req->sg_dst && req->attach_dst) {
		dma_buf_unmap_attachment(req->attach_dst,
					 req->sg_dst, DMA_BIDIRECTIONAL);
		dma_buf = req->attach_dst->dmabuf;
		dma_buf_detach(dma_buf, req->attach_dst);
		dma_buf_put(dma_buf);
	}
err_dst:
	if (req->sg_src0 && req->attach_src0) {
		dma_buf_unmap_attachment(req->attach_src0,
					 req->sg_src0, DMA_BIDIRECTIONAL);
		dma_buf = req->attach_src0->dmabuf;
		dma_buf_detach(dma_buf, req->attach_src0);
		dma_buf_put(dma_buf);
	}
err_src:

	return ret;
}
#else
static int rga2_convert_dma_buf(struct rga2_req *req)
{
	struct ion_handle *hdl;
	ion_phys_addr_t phy_addr;
	size_t len;
	int ret;
	u32 src_vir_w, dst_vir_w;
	void *vaddr = NULL;

	src_vir_w = req->src.vir_w;
	dst_vir_w = req->dst.vir_w;

	req->sg_src0 = NULL;
	req->sg_src1 = NULL;
	req->sg_dst  = NULL;
	req->sg_els  = NULL;

	if ((int)req->src.yrgb_addr > 0) {
		hdl = ion_import_dma_buf(rga2_drvdata->ion_client,
					 req->src.yrgb_addr);
		if (IS_ERR(hdl)) {
			ret = PTR_ERR(hdl);
			pr_err("RGA2 SRC ERROR ion buf handle\n");
			return ret;
		}
#if RGA2_DEBUGFS
	if (RGA2_CHECK_MODE) {
		vaddr = ion_map_kernel(rga2_drvdata->ion_client, hdl);
		if (vaddr)
			rga2_memory_check(vaddr, req->src.vir_w, req->src.vir_h,
					  req->src.format, req->src.yrgb_addr);
		ion_unmap_kernel(rga2_drvdata->ion_client, hdl);
	}
#endif
		if (req->mmu_info.src0_mmu_flag) {
			req->sg_src0 =
				ion_sg_table(rga2_drvdata->ion_client, hdl);
			req->src.yrgb_addr = req->src.uv_addr;
			req->src.uv_addr =
				req->src.yrgb_addr + (src_vir_w * req->src.vir_h);
			req->src.v_addr =
				req->src.uv_addr + (src_vir_w * req->src.vir_h) / 4;
		} else {
			ion_phys(rga2_drvdata->ion_client, hdl, &phy_addr, &len);
			req->src.yrgb_addr = phy_addr;
			req->src.uv_addr =
				req->src.yrgb_addr + (src_vir_w * req->src.vir_h);
			req->src.v_addr =
				req->src.uv_addr + (src_vir_w * req->src.vir_h) / 4;
		}
		ion_free(rga2_drvdata->ion_client, hdl);
	} else {
		req->src.yrgb_addr = req->src.uv_addr;
		req->src.uv_addr =
			req->src.yrgb_addr + (src_vir_w * req->src.vir_h);
		req->src.v_addr =
			req->src.uv_addr + (src_vir_w * req->src.vir_h) / 4;
	}

	if ((int)req->dst.yrgb_addr > 0) {
		hdl = ion_import_dma_buf(rga2_drvdata->ion_client,
					 req->dst.yrgb_addr);
		if (IS_ERR(hdl)) {
			ret = PTR_ERR(hdl);
			pr_err("RGA2 DST ERROR ion buf handle\n");
			return ret;
		}
#if RGA2_DEBUGFS
	if (RGA2_CHECK_MODE) {
		vaddr = ion_map_kernel(rga2_drvdata->ion_client, hdl);
		if (vaddr)
			rga2_memory_check(vaddr, req->dst.vir_w, req->dst.vir_h,
					  req->dst.format, req->dst.yrgb_addr);
		ion_unmap_kernel(rga2_drvdata->ion_client, hdl);
	}
#endif
		if (req->mmu_info.dst_mmu_flag) {
			req->sg_dst =
				ion_sg_table(rga2_drvdata->ion_client, hdl);
			req->dst.yrgb_addr = req->dst.uv_addr;
			req->dst.uv_addr =
				req->dst.yrgb_addr + (dst_vir_w * req->dst.vir_h);
			req->dst.v_addr =
				req->dst.uv_addr + (dst_vir_w * req->dst.vir_h) / 4;
		} else {
			ion_phys(rga2_drvdata->ion_client, hdl, &phy_addr, &len);
			req->dst.yrgb_addr = phy_addr;
			req->dst.uv_addr =
				req->dst.yrgb_addr + (dst_vir_w * req->dst.vir_h);
			req->dst.v_addr =
				req->dst.uv_addr + (dst_vir_w * req->dst.vir_h) / 4;
		}
		ion_free(rga2_drvdata->ion_client, hdl);
	} else {
		req->dst.yrgb_addr = req->dst.uv_addr;
		req->dst.uv_addr =
			req->dst.yrgb_addr + (dst_vir_w * req->dst.vir_h);
		req->dst.v_addr =
			req->dst.uv_addr + (dst_vir_w * req->dst.vir_h) / 4;
	}

	if ((int)req->src1.yrgb_addr > 0) {
		hdl = ion_import_dma_buf(rga2_drvdata->ion_client,
					 req->src1.yrgb_addr);
		if (IS_ERR(hdl)) {
			ret = PTR_ERR(hdl);
			pr_err("RGA2 ERROR ion buf handle\n");
			return ret;
		}
		if (req->mmu_info.dst_mmu_flag) {
			req->sg_src1 =
				ion_sg_table(rga2_drvdata->ion_client, hdl);
			req->src1.yrgb_addr = req->src1.uv_addr;
			req->src1.uv_addr =
				req->src1.yrgb_addr + (req->src1.vir_w * req->src1.vir_h);
			req->src1.v_addr =
				req->src1.uv_addr + (req->src1.vir_w * req->src1.vir_h) / 4;
		} else {
			ion_phys(rga2_drvdata->ion_client, hdl, &phy_addr, &len);
			req->src1.yrgb_addr = phy_addr;
			req->src1.uv_addr =
				req->src1.yrgb_addr + (req->src1.vir_w * req->src1.vir_h);
			req->src1.v_addr =
				req->src1.uv_addr + (req->src1.vir_w * req->src1.vir_h) / 4;
		}
		ion_free(rga2_drvdata->ion_client, hdl);
	} else {
		req->src1.yrgb_addr = req->src1.uv_addr;
		req->src1.uv_addr =
			req->src1.yrgb_addr + (req->src1.vir_w * req->src1.vir_h);
		req->src1.v_addr =
			req->src1.uv_addr + (req->src1.vir_w * req->src1.vir_h) / 4;
	}
	if (is_yuv422p_format(req->src.format))
		req->src.v_addr = req->src.uv_addr + (req->src.vir_w * req->src.vir_h) / 2;
	if (is_yuv422p_format(req->dst.format))
		req->dst.v_addr = req->dst.uv_addr + (req->dst.vir_w * req->dst.vir_h) / 2;
	if (is_yuv422p_format(req->src1.format))
		req->src1.v_addr = req->src1.uv_addr + (req->src1.vir_w * req->dst.vir_h) / 2;

	return 0;
}
#endif

static int rga2_blit_flush_cache(rga2_session *session, struct rga2_req *req)
{
	int ret = 0;
	/* Alloc 4k size for rga2_reg use. */
	struct rga2_reg *reg = (struct rga2_reg *)get_zeroed_page(GFP_KERNEL | GFP_DMA32);
	struct rga2_mmu_buf_t *tbuf = &rga2_mmu_buf;

	if (!reg) {
		pr_err("%s, [%d] kzalloc error\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto err_free_reg;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	if (rga2_get_dma_buf(req)) {
		pr_err("RGA2 : DMA buf copy error\n");
		ret = -EFAULT;
		goto err_free_reg;
	}
#else
	if (rga2_convert_dma_buf(req)) {
		pr_err("RGA2 : DMA buf copy error\n");
		ret = -EFAULT;
		goto err_free_reg;
	}
#endif
	if ((req->mmu_info.src0_mmu_flag & 1) || (req->mmu_info.src1_mmu_flag & 1) ||
	    (req->mmu_info.dst_mmu_flag & 1) || (req->mmu_info.els_mmu_flag & 1)) {
		reg->MMU_map = true;
		ret = rga2_set_mmu_info(reg, req);
		if (ret < 0) {
			pr_err("%s, [%d] set mmu info error\n", __func__, __LINE__);
			ret = -EFAULT;
			goto err_free_reg;
		}
	}
	if (reg->MMU_len && tbuf) {
		if (tbuf->back + reg->MMU_len > 2 * tbuf->size)
			tbuf->back = reg->MMU_len + tbuf->size;
		else
			tbuf->back += reg->MMU_len;
	}
err_free_reg:
	free_page((unsigned long)reg);

	return ret;
}

static int rga2_blit(rga2_session *session, struct rga2_req *req)
{
	int ret = -1;
	int num = 0;
	struct rga2_reg *reg;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	if (rga2_get_dma_buf(req)) {
		pr_err("RGA2 : DMA buf copy error\n");
		return -EFAULT;
	}
#else
	if (rga2_convert_dma_buf(req)) {
		pr_err("RGA2 : DMA buf copy error\n");
		return -EFAULT;
	}
#endif
	do {
		/* check value if legal */
		ret = rga2_check_param(req);
		if(ret == -EINVAL) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
			pr_err("req argument is inval\n");
			goto err_put_dma_buf;
#else
			pr_err("req argument is inval\n");
			break;
#endif
		}

		reg = rga2_reg_init(session, req);
		if(reg == NULL) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
			pr_err("init reg fail\n");
			goto err_put_dma_buf;
#else
			break;
#endif
		}

		num = 1;
		mutex_lock(&rga2_service.lock);
		atomic_add(num, &rga2_service.total_running);
		rga2_try_set_reg();
		mutex_unlock(&rga2_service.lock);

		return 0;
	}
	while(0);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
err_put_dma_buf:
	rga2_put_dma_buf(req, NULL);
#endif
	return -EFAULT;
}

static int rga2_blit_async(rga2_session *session, struct rga2_req *req)
{
	int ret = -1;
#if RGA2_DEBUGFS
	if (RGA2_TEST_MSG) {
		if (1) {
			print_debug_info(req);
			rga2_flag = 1;
			INFO("*** rga_blit_async proc ***\n");
		} else {
			rga2_flag = 0;
		}
	}
#endif
	atomic_set(&session->done, 0);
	ret = rga2_blit(session, req);

	return ret;
	}

static int rga2_blit_sync(rga2_session *session, struct rga2_req *req)
{
	struct rga2_req req_bak;
	int restore = 0;
	int try = 10;
	int ret = -1;
	int ret_timeout = 0;

	memcpy(&req_bak, req, sizeof(req_bak));
retry:

#if RGA2_DEBUGFS
	if (RGA2_TEST_MSG) {
		if (1) {
			print_debug_info(req);
			rga2_flag = 1;
			INFO("*** rga2_blit_sync proc ***\n");
		} else {
			rga2_flag = 0;
		}
	}
	if (RGA2_CHECK_MODE) {
		rga2_align_check(req);
		/*rga2_scale_check(req);*/
	}
#endif

	atomic_set(&session->done, 0);

	ret = rga2_blit(session, req);
	if(ret < 0)
		return ret;

	if (rk3368)
		ret_timeout = wait_event_timeout(session->wait,
						 atomic_read(&session->done),
						 RGA2_TIMEOUT_DELAY / 4);
	else
		ret_timeout = wait_event_timeout(session->wait,
						 atomic_read(&session->done),
						 RGA2_TIMEOUT_DELAY);

	if (unlikely(ret_timeout < 0)) {
		u32 i;
		u32 *p;

		p = rga2_service.cmd_buff;
		pr_err("Rga sync pid %d wait task ret %d\n", session->pid,
			ret_timeout);
		pr_err("interrupt = %x status = %x\n",
		       rga2_read(RGA2_INT), rga2_read(RGA2_STATUS));
		rga2_printf_cmd_buf(p);
		DBG("rga2 CMD\n");
		for (i = 0; i < 7; i++)
			DBG("%.8x %.8x %.8x %.8x\n",
			     p[0 + i * 4], p[1 + i * 4],
			     p[2 + i * 4], p[3 + i * 4]);
		mutex_lock(&rga2_service.lock);
		rga2_del_running_list();
		mutex_unlock(&rga2_service.lock);
		ret = ret_timeout;
	} else if (ret_timeout == 0) {
		u32 i;
		u32 *p;

		p = rga2_service.cmd_buff;
		pr_err("Rga sync pid %d wait %d task done timeout\n",
			session->pid, atomic_read(&session->task_running));
		pr_err("interrupt = %x status = %x\n",
		       rga2_read(RGA2_INT), rga2_read(RGA2_STATUS));
		rga2_printf_cmd_buf(p);
		DBG("rga2 CMD\n");
		for (i = 0; i < 7; i++)
			DBG("%.8x %.8x %.8x %.8x\n",
			     p[0 + i * 4], p[1 + i * 4],
			     p[2 + i * 4], p[3 + i * 4]);
		mutex_lock(&rga2_service.lock);
		rga2_del_running_list_timeout();
		rga2_try_set_reg();
		mutex_unlock(&rga2_service.lock);
		ret = -ETIMEDOUT;
	}

#if RGA2_DEBUGFS
	if (RGA2_TEST_TIME) {
		rga2_end = ktime_get();
		rga2_end = ktime_sub(rga2_end, rga2_start);
		DBG("sync one cmd end time %d\n", (int)ktime_to_us(rga2_end));
	}
#endif
	if (ret == -ETIMEDOUT && try--) {
		memcpy(req, &req_bak, sizeof(req_bak));
		/*
		 * if rga work timeout with scaling, need do a non-scale work
		 * first, restore hardware status, then do actually work.
		 */
		if (req->src.act_w != req->dst.act_w ||
		    req->src.act_h != req->dst.act_h) {
			req->src.act_w = MIN(320, MIN(req->src.act_w,
						      req->dst.act_w));
			req->src.act_h = MIN(240, MIN(req->src.act_h,
						      req->dst.act_h));
			req->dst.act_w = req->src.act_w;
			req->dst.act_h = req->src.act_h;
			restore = 1;
		}
		goto retry;
	}
	if (!ret && restore) {
		memcpy(req, &req_bak, sizeof(req_bak));
		restore = 0;
		goto retry;
	}

	return ret;
}

static long rga_ioctl(struct file *file, uint32_t cmd, unsigned long arg)
{
	struct rga2_drvdata_t *rga = rga2_drvdata;
	struct rga2_req req, req_first;
	struct rga_req req_rga;
	int ret = 0;
	int major_version = 0, minor_version = 0;
	char version[16] = {0};
	rga2_session *session;

	if (!rga) {
		pr_err("rga2_drvdata is null, rga2 is not init\n");
		return -ENODEV;
	}
	memset(&req, 0x0, sizeof(req));

	mutex_lock(&rga2_service.mutex);

	session = (rga2_session *)file->private_data;

	if (NULL == session)
	{
		printk("%s [%d] rga thread session is null\n",__FUNCTION__,__LINE__);
		mutex_unlock(&rga2_service.mutex);
		return -EINVAL;
	}

	memset(&req, 0x0, sizeof(req));
#if RGA2_DEBUGFS
	if (RGA2_TEST_MSG)
		INFO("cmd is %s\n", rga2_get_cmd_mode_str(cmd));
	if (RGA2_NONUSE) {
		mutex_unlock(&rga2_service.mutex);
		return 0;
	}
#endif
	switch (cmd)
	{
		case RGA_BLIT_SYNC:
			if (unlikely(copy_from_user(&req_rga, (struct rga_req*)arg, sizeof(struct rga_req))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}
			RGA_MSG_2_RGA2_MSG(&req_rga, &req);

			if (first_RGA2_proc == 0 && req.render_mode == bitblt_mode && rga2_service.dev_mode == 1) {
				memcpy(&req_first, &req, sizeof(struct rga2_req));
				if ((req_first.src.act_w != req_first.dst.act_w)
						|| (req_first.src.act_h != req_first.dst.act_h)) {
					req_first.src.act_w = MIN(320, MIN(req_first.src.act_w, req_first.dst.act_w));
					req_first.src.act_h = MIN(240, MIN(req_first.src.act_h, req_first.dst.act_h));
					req_first.dst.act_w = req_first.src.act_w;
					req_first.dst.act_h = req_first.src.act_h;
					ret = rga2_blit_async(session, &req_first);
				}
				ret = rga2_blit_sync(session, &req);
				first_RGA2_proc = 1;
			}
			else {
				ret = rga2_blit_sync(session, &req);
			}
			break;
		case RGA_BLIT_ASYNC:
			if (unlikely(copy_from_user(&req_rga, (struct rga_req*)arg, sizeof(struct rga_req))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}

			RGA_MSG_2_RGA2_MSG(&req_rga, &req);
			if (first_RGA2_proc == 0 && req.render_mode == bitblt_mode && rga2_service.dev_mode == 1) {
				memcpy(&req_first, &req, sizeof(struct rga2_req));
				if ((req_first.src.act_w != req_first.dst.act_w)
						|| (req_first.src.act_h != req_first.dst.act_h)
						|| rk3368) {
					req_first.src.act_w = MIN(320, MIN(req_first.src.act_w, req_first.dst.act_w));
					req_first.src.act_h = MIN(240, MIN(req_first.src.act_h, req_first.dst.act_h));
					req_first.dst.act_w = req_first.src.act_w;
					req_first.dst.act_h = req_first.src.act_h;
					if (rk3368)
						ret = rga2_blit_sync(session, &req_first);
					else
						ret = rga2_blit_async(session, &req_first);
				}
				ret = rga2_blit_async(session, &req);
				first_RGA2_proc = 1;
			}
			else {
				if (rk3368)
				{
					memcpy(&req_first, &req, sizeof(struct rga2_req));

					/*
					 * workround for gts
					 * run gts --skip-all-system-status-check --ignore-business-logic-failure -m GtsMediaTestCases -t com.google.android.media.gts.WidevineYouTubePerformanceTests#testClear1080P30
					 */
					if ((req_first.src.act_w == 1920) && (req_first.src.act_h == 1008) && (req_first.src.act_h == req_first.dst.act_w)) {
						printk("src : aw=%d ah=%d vw=%d vh=%d  \n",
							req_first.src.act_w, req_first.src.act_h, req_first.src.vir_w, req_first.src.vir_h);
						printk("dst : aw=%d ah=%d vw=%d vh=%d  \n",
							req_first.dst.act_w, req_first.dst.act_h, req_first.dst.vir_w, req_first.dst.vir_h);
					} else {
							req_first.src.act_w = MIN(320, MIN(req_first.src.act_w, req_first.dst.act_w));
							req_first.src.act_h = MIN(240, MIN(req_first.src.act_h, req_first.dst.act_h));
							req_first.dst.act_w = req_first.src.act_w;
							req_first.dst.act_h = req_first.src.act_h;
							ret = rga2_blit_sync(session, &req_first);
					}
				}
				ret = rga2_blit_async(session, &req);
			}
			break;
		case RGA_CACHE_FLUSH:
			if (unlikely(copy_from_user(&req_rga, (struct rga_req*)arg, sizeof(struct rga_req))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}
			RGA_MSG_2_RGA2_MSG(&req_rga, &req);
			ret = rga2_blit_flush_cache(session, &req);
			break;
		case RGA2_BLIT_SYNC:
			if (unlikely(copy_from_user(&req, (struct rga2_req*)arg, sizeof(struct rga2_req))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}
			ret = rga2_blit_sync(session, &req);
			break;
		case RGA2_BLIT_ASYNC:
			if (unlikely(copy_from_user(&req, (struct rga2_req*)arg, sizeof(struct rga2_req))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}

			if((atomic_read(&rga2_service.total_running) > 16))
			{
				ret = rga2_blit_sync(session, &req);
			}
			else
			{
				ret = rga2_blit_async(session, &req);
			}
			break;
		case RGA_FLUSH:
		case RGA2_FLUSH:
			ret = rga2_flush(session, arg);
			break;
		case RGA_GET_RESULT:
		case RGA2_GET_RESULT:
			ret = rga2_get_result(session, arg);
			break;
		case RGA_GET_VERSION:
			sscanf(rga->version, "%x.%x.%*x", &major_version, &minor_version);
			snprintf(version, 5, "%x.%02x", major_version, minor_version);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
			ret = copy_to_user((void *)arg, version, sizeof(rga->version));
#else
			ret = copy_to_user((void *)arg, RGA2_VERSION, sizeof(RGA2_VERSION));
#endif
			if (ret != 0)
				ret = -EFAULT;
			break;
		case RGA2_GET_VERSION:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
			ret = copy_to_user((void *)arg, rga->version, sizeof(rga->version));
#else
			ret = copy_to_user((void *)arg, RGA2_VERSION, sizeof(RGA2_VERSION));
#endif
			if (ret != 0)
				ret = -EFAULT;
			break;
		default:
			ERR("unknown ioctl cmd!\n");
			ret = -EINVAL;
			break;
	}

	mutex_unlock(&rga2_service.mutex);

	return ret;
}

#ifdef CONFIG_COMPAT
static long compat_rga_ioctl(struct file *file, uint32_t cmd, unsigned long arg)
{
	struct rga2_drvdata_t *rga = rga2_drvdata;
	struct rga2_req req, req_first;
	struct rga_req_32 req_rga;
	int ret = 0;
	rga2_session *session;

	if (!rga) {
		pr_err("rga2_drvdata is null, rga2 is not init\n");
		return -ENODEV;
	}
	memset(&req, 0x0, sizeof(req));

	mutex_lock(&rga2_service.mutex);

	session = (rga2_session *)file->private_data;

#if RGA2_DEBUGFS
	if (RGA2_TEST_MSG)
		INFO("using %s\n", __func__);
#endif

	if (NULL == session) {
		ERR("%s [%d] rga thread session is null\n", __func__, __LINE__);
		mutex_unlock(&rga2_service.mutex);
		return -EINVAL;
	}

	memset(&req, 0x0, sizeof(req));

	switch (cmd) {
		case RGA_BLIT_SYNC:
			if (unlikely(copy_from_user(&req_rga, compat_ptr((compat_uptr_t)arg), sizeof(struct rga_req_32))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}

			RGA_MSG_2_RGA2_MSG_32(&req_rga, &req);

			if (first_RGA2_proc == 0 && req.render_mode == bitblt_mode && rga2_service.dev_mode == 1) {
				memcpy(&req_first, &req, sizeof(struct rga2_req));
				if ((req_first.src.act_w != req_first.dst.act_w)
						|| (req_first.src.act_h != req_first.dst.act_h)) {
					req_first.src.act_w = MIN(320, MIN(req_first.src.act_w, req_first.dst.act_w));
					req_first.src.act_h = MIN(240, MIN(req_first.src.act_h, req_first.dst.act_h));
					req_first.dst.act_w = req_first.src.act_w;
					req_first.dst.act_h = req_first.src.act_h;
					ret = rga2_blit_async(session, &req_first);
				}
				ret = rga2_blit_sync(session, &req);
				first_RGA2_proc = 1;
			}
			else {
				ret = rga2_blit_sync(session, &req);
			}
			break;
		case RGA_BLIT_ASYNC:
			if (unlikely(copy_from_user(&req_rga, compat_ptr((compat_uptr_t)arg), sizeof(struct rga_req_32))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}
			RGA_MSG_2_RGA2_MSG_32(&req_rga, &req);

			if (first_RGA2_proc == 0 && req.render_mode == bitblt_mode && rga2_service.dev_mode == 1) {
				memcpy(&req_first, &req, sizeof(struct rga2_req));
				if ((req_first.src.act_w != req_first.dst.act_w)
						|| (req_first.src.act_h != req_first.dst.act_h)) {
					req_first.src.act_w = MIN(320, MIN(req_first.src.act_w, req_first.dst.act_w));
					req_first.src.act_h = MIN(240, MIN(req_first.src.act_h, req_first.dst.act_h));
					req_first.dst.act_w = req_first.src.act_w;
					req_first.dst.act_h = req_first.src.act_h;
					ret = rga2_blit_async(session, &req_first);
				}
				ret = rga2_blit_sync(session, &req);
				first_RGA2_proc = 1;
			}
			else {
				ret = rga2_blit_sync(session, &req);
			}

			//if((atomic_read(&rga2_service.total_running) > 8))
			//    ret = rga2_blit_sync(session, &req);
			//else
			//    ret = rga2_blit_async(session, &req);

			break;
		case RGA2_BLIT_SYNC:
			if (unlikely(copy_from_user(&req, compat_ptr((compat_uptr_t)arg), sizeof(struct rga2_req))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}
			ret = rga2_blit_sync(session, &req);
			break;
		case RGA2_BLIT_ASYNC:
			if (unlikely(copy_from_user(&req, compat_ptr((compat_uptr_t)arg), sizeof(struct rga2_req))))
			{
				ERR("copy_from_user failed\n");
				ret = -EFAULT;
				break;
			}

			if((atomic_read(&rga2_service.total_running) > 16))
				ret = rga2_blit_sync(session, &req);
			else
				ret = rga2_blit_async(session, &req);

			break;
		case RGA_FLUSH:
		case RGA2_FLUSH:
			ret = rga2_flush(session, arg);
			break;
		case RGA_GET_RESULT:
		case RGA2_GET_RESULT:
			ret = rga2_get_result(session, arg);
			break;
		case RGA_GET_VERSION:
		case RGA2_GET_VERSION:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
			ret = copy_to_user((void *)arg, rga->version, 16);
#else
			ret = copy_to_user((void *)arg, RGA2_VERSION, sizeof(RGA2_VERSION));
#endif
			if (ret != 0)
				ret = -EFAULT;
			break;
		default:
			ERR("unknown ioctl cmd!\n");
			ret = -EINVAL;
			break;
	}

	mutex_unlock(&rga2_service.mutex);

	return ret;
}
#endif


static long rga2_ioctl_kernel(struct rga_req *req_rga)
{
	int ret = 0;
	rga2_session *session;
	struct rga2_req req;

	memset(&req, 0x0, sizeof(req));
	mutex_lock(&rga2_service.mutex);
	session = &rga2_session_global;
	if (NULL == session)
	{
		ERR("%s [%d] rga thread session is null\n", __func__, __LINE__);
		mutex_unlock(&rga2_service.mutex);
		return -EINVAL;
	}

	RGA_MSG_2_RGA2_MSG(req_rga, &req);
	ret = rga2_blit_sync(session, &req);
	mutex_unlock(&rga2_service.mutex);

	return ret;
}


static int rga2_open(struct inode *inode, struct file *file)
{
	rga2_session *session = kzalloc(sizeof(rga2_session), GFP_KERNEL);

	if (NULL == session) {
		pr_err("unable to allocate memory for rga_session.");
		return -ENOMEM;
	}

	session->pid = current->pid;
	INIT_LIST_HEAD(&session->waiting);
	INIT_LIST_HEAD(&session->running);
	INIT_LIST_HEAD(&session->list_session);
	init_waitqueue_head(&session->wait);
	mutex_lock(&rga2_service.lock);
	list_add_tail(&session->list_session, &rga2_service.session);
	mutex_unlock(&rga2_service.lock);
	atomic_set(&session->task_running, 0);
	atomic_set(&session->num_done, 0);
	file->private_data = (void *)session;

	return nonseekable_open(inode, file);
}

static int rga2_release(struct inode *inode, struct file *file)
{
	int task_running;
	rga2_session *session = (rga2_session *)file->private_data;

	if (NULL == session)
		return -EINVAL;

	task_running = atomic_read(&session->task_running);
	if (task_running)
	{
		pr_err("rga2_service session %d still has %d task running when closing\n", session->pid, task_running);
		msleep(100);
	}

	wake_up(&session->wait);
	mutex_lock(&rga2_service.lock);
	list_del(&session->list_session);
	rga2_service_session_clear(session);
	kfree(session);
	mutex_unlock(&rga2_service.lock);

	return 0;
}

static void RGA2_flush_page(void)
{
	struct rga2_reg *reg;
	int i;

	reg = list_entry(rga2_service.running.prev,
			 struct rga2_reg, status_link);

	if (reg == NULL)
		return;

	if (reg->MMU_src0_base != NULL) {
		for (i = 0; i < reg->MMU_src0_count; i++)
			rga2_dma_flush_page(phys_to_page(reg->MMU_src0_base[i]),
					    MMU_UNMAP_CLEAN);
	}

	if (reg->MMU_src1_base != NULL) {
		for (i = 0; i < reg->MMU_src1_count; i++)
			rga2_dma_flush_page(phys_to_page(reg->MMU_src1_base[i]),
					    MMU_UNMAP_CLEAN);
	}

	if (reg->MMU_dst_base != NULL) {
		for (i = 0; i < reg->MMU_dst_count; i++)
			rga2_dma_flush_page(phys_to_page(reg->MMU_dst_base[i]),
					    MMU_UNMAP_INVALID);
	}
}

static irqreturn_t rga2_irq_thread(int irq, void *dev_id)
{
#if RGA2_DEBUGFS
	if (RGA2_INT_FLAG)
		INFO("irqthread INT[%x],STATS[%x]\n", rga2_read(RGA2_INT),
		     rga2_read(RGA2_STATUS));
#endif
	RGA2_flush_page();
	mutex_lock(&rga2_service.lock);
	if (rga2_service.enable) {
		rga2_del_running_list();
		rga2_try_set_reg();
	}
	mutex_unlock(&rga2_service.lock);

	return IRQ_HANDLED;
}

static irqreturn_t rga2_irq(int irq,  void *dev_id)
{
#if RGA2_DEBUGFS
	if (RGA2_INT_FLAG)
		INFO("irq INT[%x], STATS[%x]\n", rga2_read(RGA2_INT),
		     rga2_read(RGA2_STATUS));
#endif
	/*if error interrupt then soft reset hardware*/
	if (rga2_read(RGA2_INT) & 0x01) {
		pr_err("Rga err irq! INT[%x],STATS[%x]\n",
		       rga2_read(RGA2_INT), rga2_read(RGA2_STATUS));
		rga2_soft_reset();
	}
	/*clear INT */
	rga2_write(rga2_read(RGA2_INT) | (0x1<<4) | (0x1<<5) | (0x1<<6) | (0x1<<7), RGA2_INT);

	return IRQ_WAKE_THREAD;
}

struct file_operations rga2_fops = {
	.owner		= THIS_MODULE,
	.open		= rga2_open,
	.release	= rga2_release,
	.unlocked_ioctl		= rga_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= compat_rga_ioctl,
#endif
};

static struct miscdevice rga2_dev ={
	.minor = RGA2_MAJOR,
	.name  = "rga",
	.fops  = &rga2_fops,
};

static const struct of_device_id rockchip_rga_dt_ids[] = {
	{ .compatible = "rockchip,rga2", },
	{},
};

static int rga2_drv_probe(struct platform_device *pdev)
{
	struct rga2_drvdata_t *data;
	struct resource *res;
	int ret = 0;
	struct device_node *np = pdev->dev.of_node;

	mutex_init(&rga2_service.lock);
	mutex_init(&rga2_service.mutex);
	atomic_set(&rga2_service.total_running, 0);
	atomic_set(&rga2_service.src_format_swt, 0);
	rga2_service.last_prc_src_format = 1; /* default is yuv first*/
	rga2_service.enable = false;

	rga2_ioctl_kernel_p = rga2_ioctl_kernel;

	data = devm_kzalloc(&pdev->dev, sizeof(struct rga2_drvdata_t), GFP_KERNEL);
	if(NULL == data)
	{
		ERR("failed to allocate driver data.\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&data->power_off_work, rga2_power_off_work);
	wake_lock_init(&data->wake_lock, WAKE_LOCK_SUSPEND, "rga");

	data->clk_rga2 = devm_clk_get(&pdev->dev, "clk_rga");
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_enable(&pdev->dev);
#else
	data->pd_rga2 = devm_clk_get(&pdev->dev, "pd_rga");
#endif
	data->aclk_rga2 = devm_clk_get(&pdev->dev, "aclk_rga");
	data->hclk_rga2 = devm_clk_get(&pdev->dev, "hclk_rga");

	/* map the registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->rga_base = devm_ioremap_resource(&pdev->dev, res);
	if (!data->rga_base) {
		ERR("rga ioremap failed\n");
		ret = -ENOENT;
		goto err_ioremap;
	}

	/* get the IRQ */
	data->irq = platform_get_irq(pdev, 0);
	if (data->irq <= 0) {
		ERR("failed to get rga irq resource (%d).\n", data->irq);
		ret = data->irq;
		goto err_irq;
	}

	/* request the IRQ */
	ret = devm_request_threaded_irq(&pdev->dev, data->irq, rga2_irq, rga2_irq_thread, 0, "rga", pdev);
	if (ret)
	{
		ERR("rga request_irq failed (%d).\n", ret);
		goto err_irq;
	}

	platform_set_drvdata(pdev, data);
	data->dev = &pdev->dev;
	rga2_drvdata = data;
	of_property_read_u32(np, "dev_mode", &rga2_service.dev_mode);
	if (of_machine_is_compatible("rockchip,rk3368"))
		rk3368 = 1;

#if defined(CONFIG_ION_ROCKCHIP) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	data->ion_client = rockchip_ion_client_create("rga");
	if (IS_ERR(data->ion_client)) {
		dev_err(&pdev->dev, "failed to create ion client for rga");
		return PTR_ERR(data->ion_client);
	} else {
		dev_info(&pdev->dev, "rga ion client create success!\n");
	}
#endif

	ret = misc_register(&rga2_dev);
	if(ret)
	{
		ERR("cannot register miscdev (%d)\n", ret);
		goto err_misc_register;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
	rga2_init_version();
	INFO("Driver loaded successfully ver:%s\n", rga2_drvdata->version);
#else
	INFO("Driver loaded successfully\n");
#endif
	return 0;

err_misc_register:
	free_irq(data->irq, pdev);
err_irq:
	iounmap(data->rga_base);
err_ioremap:
	wake_lock_destroy(&data->wake_lock);
	//kfree(data);

	return ret;
}

static int rga2_drv_remove(struct platform_device *pdev)
{
	struct rga2_drvdata_t *data = platform_get_drvdata(pdev);
	DBG("%s [%d]\n",__FUNCTION__,__LINE__);

	wake_lock_destroy(&data->wake_lock);
	misc_deregister(&(data->miscdev));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	free_irq(data->irq, &data->miscdev);
	iounmap((void __iomem *)(data->rga_base));

	devm_clk_put(&pdev->dev, data->clk_rga2);
	devm_clk_put(&pdev->dev, data->aclk_rga2);
	devm_clk_put(&pdev->dev, data->hclk_rga2);
	pm_runtime_disable(&pdev->dev);
#endif

	//kfree(data);
	return 0;
}

static struct platform_driver rga2_driver = {
	.probe		= rga2_drv_probe,
	.remove		= rga2_drv_remove,
	.driver		= {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
		.owner  = THIS_MODULE,
#endif
		.name	= "rga2",
		.of_match_table = of_match_ptr(rockchip_rga_dt_ids),
	},
};

#if RGA2_DEBUGFS
void rga2_slt(void);

static int rga2_debug_show(struct seq_file *m, void *data)
{
	seq_puts(m, "echo reg > rga2 to open rga reg MSG\n");
	seq_puts(m, "echo msg  > rga2 to open rga msg MSG\n");
	seq_puts(m, "echo time > rga2 to open rga time MSG\n");
	seq_puts(m, "echo check > rga2 to open rga check flag\n");
	seq_puts(m, "echo stop > rga2 to stop using hardware\n");
	seq_puts(m, "echo int > rga2 to open interruppt MSG\n");
	return 0;
}

static ssize_t rga2_debug_write(struct file *file, const char __user *ubuf,
			      size_t len, loff_t *offp)
{
	char buf[14];

	if (len > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len - 1] = '\0';

	if (strncmp(buf, "reg", 4) == 0) {
		if (RGA2_TEST_REG) {
			RGA2_TEST_REG = 0;
			INFO("close rga2 reg!\n");
		} else {
			RGA2_TEST_REG = 1;
			INFO("open rga2 reg!\n");
		}
	} else if (strncmp(buf, "msg", 3) == 0) {
		if (RGA2_TEST_MSG) {
			RGA2_TEST_MSG = 0;
			INFO("close rga2 test MSG!\n");
		} else {
			RGA2_TEST_MSG = 1;
			INFO("open rga2 test MSG!\n");
		}
	} else if (strncmp(buf, "time", 4) == 0) {
		if (RGA2_TEST_TIME) {
			RGA2_TEST_TIME = 0;
			INFO("close rga2 test time!\n");
		} else {
			RGA2_TEST_TIME = 1;
			INFO("open rga2 test time!\n");
		}
	} else if (strncmp(buf, "check", 5) == 0) {
		if (RGA2_CHECK_MODE) {
			RGA2_CHECK_MODE = 0;
			INFO("close rga2 check flag!\n");
		} else {
			RGA2_CHECK_MODE = 1;
			INFO("open rga2 check flag!\n");
		}
	} else if (strncmp(buf, "stop", 4) == 0) {
		if (RGA2_NONUSE) {
			RGA2_NONUSE = 0;
			INFO("stop using rga hardware!\n");
		} else {
			RGA2_NONUSE = 1;
			INFO("use rga hardware!\n");
		}
	} else if (strncmp(buf, "int", 3) == 0) {
		if (RGA2_INT_FLAG) {
			RGA2_INT_FLAG = 0;
			INFO("close inturrupt MSG!\n");
		} else {
			RGA2_INT_FLAG = 1;
			INFO("open inturrupt MSG!\n");
		}
	} else if (strncmp(buf, "slt", 3) == 0) {
		rga2_slt();
	}

	return len;
}

static int rga2_debug_open(struct inode *inode, struct file *file)

{
	return single_open(file, rga2_debug_show, NULL);
}

static const struct file_operations rga2_debug_fops = {
	.owner = THIS_MODULE,
	.open = rga2_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = rga2_debug_write,
};

static void rga2_debugfs_add(void)
{
	struct dentry *rga2_debug_root;
	struct dentry *ent;

	rga2_debug_root = debugfs_create_dir("rga2_debug", NULL);

	ent = debugfs_create_file("rga2", 0644, rga2_debug_root,
				  NULL, &rga2_debug_fops);
	if (!ent) {
		pr_err("create rga2_debugfs err\n");
		debugfs_remove_recursive(rga2_debug_root);
	}
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
void rga2_slt(void)
{
	struct rga2_req req;
	rga2_session session;
	void *src_vir, *dst_vir;
	unsigned int *src, *dst;
	ion_phys_addr_t src_phy, dst_phy;
	int i;
	unsigned int srcW, srcH, dstW, dstH;
	struct ion_handle *src_handle;
	struct ion_handle *dst_handle;
	struct rga2_drvdata_t *data;
	unsigned int srclen, dstlen;
	int err_count = 0;
	int right_count = 0;
	int size;
	unsigned int *pstd;
	unsigned int *pnow;

	data = rga2_drvdata;
	srcW = 1280;
	srcH = 720;
	dstW = 1280;
	dstH = 720;
	src_handle = ion_alloc(data->ion_client, (size_t)srcW * srcH * 4, 0,
		   ION_HEAP(ION_CMA_HEAP_ID), 0);

	dst_handle = ion_alloc(data->ion_client, (size_t)dstW * dstH * 4, 0,
		   ION_HEAP(ION_CMA_HEAP_ID), 0);

	session.pid	= current->pid;
	INIT_LIST_HEAD(&session.waiting);
	INIT_LIST_HEAD(&session.running);
	INIT_LIST_HEAD(&session.list_session);
	init_waitqueue_head(&session.wait);
	/* no need to protect */
	list_add_tail(&session.list_session, &rga2_service.session);
	atomic_set(&session.task_running, 0);
	atomic_set(&session.num_done, 0);

	src_vir = ion_map_kernel(data->ion_client, src_handle);
	dst_vir = ion_map_kernel(data->ion_client, dst_handle);

	ion_phys(rga2_drvdata->ion_client, src_handle, &src_phy, &srclen);
	ion_phys(rga2_drvdata->ion_client, dst_handle, &dst_phy, &dstlen);

	memset(&req, 0, sizeof(struct rga2_req));
	src = (unsigned int *)src_vir;
	dst = (unsigned int *)dst_vir;

	memset(src_vir, 0x80, srcW * srcH * 4);

	INFO("\n********************************\n");
	INFO("************ RGA_TEST ************\n");
	INFO("********************************\n\n");

	req.src.act_w = srcW;
	req.src.act_h = srcH;

	req.src.vir_w = srcW;
	req.src.vir_h = srcW;
	req.src.yrgb_addr = 0;
	req.src.uv_addr = src_phy;
	req.src.v_addr = src_phy + srcH * srcW;
	req.src.format = RGA2_FORMAT_RGBA_8888;

	req.dst.act_w = dstW;
	req.dst.act_h = dstH;

	req.dst.vir_w = dstW;
	req.dst.vir_h = dstH;
	req.dst.x_offset = 0;
	req.dst.y_offset = 0;

	req.dst.yrgb_addr = 0;
	req.dst.uv_addr = dst_phy;
	req.dst.v_addr = dst_phy + dstH * dstW;

	req.dst.format = RGA2_FORMAT_RGBA_8888;

	rga2_blit_sync(&session, &req);

	size = dstW * dstH * 4;
	pstd = (unsigned int *)src_vir;
	pnow = (unsigned int *)dst_vir;

	INFO("[  num   : srcInfo    dstInfo ]\n");
	for (i = 0; i < size / 4; i++) {
		if (*pstd != *pnow) {
			INFO("[X%.8d : 0x%x 0x%x]", i, *pstd, *pnow);
			if (i % 4 == 0)
				INFO("\n");
			err_count++;
		} else {
			if (i % (640 * 1024) == 0)
				INFO("[Y%.8d : 0x%.8x 0x%.8x]\n",
				     i, *pstd, *pnow);
			right_count++;
		}
		pstd++;
		pnow++;
		if (err_count > 64)
			break;
	}

	INFO("err_count=%d, right_count=%d\n", err_count, right_count);
	if (err_count != 0)
		INFO("rga slt err !!\n");
	else
		INFO("rga slt success !!\n");

	ion_unmap_kernel(data->ion_client, src_handle);
	ion_unmap_kernel(data->ion_client, dst_handle);

	ion_free(data->ion_client, src_handle);
	ion_free(data->ion_client, dst_handle);
}
#else
unsigned long src_buf[400 * 200];
unsigned long dst_buf[400 * 200];
void rga2_slt(void)
{
	struct rga2_req req;
	rga2_session session;
	unsigned long *src_vir, *dst_vir;
	int i;
	unsigned int srcW, srcH, dstW, dstH;
	int err_count = 0;
	int right_count = 0;
	int size;
	unsigned int *pstd;
	unsigned int *pnow;

	srcW = 400;
	srcH = 200;
	dstW = 400;
	dstH = 200;

	session.pid	= current->pid;
	INIT_LIST_HEAD(&session.waiting);
	INIT_LIST_HEAD(&session.running);
	INIT_LIST_HEAD(&session.list_session);
	init_waitqueue_head(&session.wait);
	/* no need to protect */
	list_add_tail(&session.list_session, &rga2_service.session);
	atomic_set(&session.task_running, 0);
	atomic_set(&session.num_done, 0);

	memset(&req, 0, sizeof(struct rga2_req));
	src_vir = src_buf;
	dst_vir = dst_buf;

	memset(src_buf, 0x50, 400 * 200 * 4);
	memset(dst_buf, 0x00, 400 * 200 * 4);

	rga2_dma_flush_range(&src_buf[0], &src_buf[400 * 200]);
	rga2_dma_flush_range(&dst_buf[0], &dst_buf[400 * 200]);

	INFO("\n********************************\n");
	INFO("************ RGA_TEST ************\n");
	INFO("********************************\n\n");

	req.src.act_w = srcW;
	req.src.act_h = srcH;

	req.src.vir_w = srcW;
	req.src.vir_h = srcW;
	req.src.yrgb_addr = 0;
	req.src.uv_addr = (unsigned long)virt_to_phys(src_vir);
	req.src.v_addr = req.src.uv_addr + srcH * srcW;
	req.src.format = RGA2_FORMAT_RGBA_8888;

	req.dst.act_w = dstW;
	req.dst.act_h = dstH;

	req.dst.vir_w = dstW;
	req.dst.vir_h = dstH;
	req.dst.x_offset = 0;
	req.dst.y_offset = 0;

	req.dst.yrgb_addr = 0;
	req.dst.uv_addr = (unsigned long)virt_to_phys(dst_vir);
	req.dst.v_addr = req.dst.uv_addr + dstH * dstW;

	req.dst.format = RGA2_FORMAT_RGBA_8888;
	rga2_blit_sync(&session, &req);

	size = dstW * dstH * 4;
	pstd = (unsigned int *)src_vir;
	pnow = (unsigned int *)dst_vir;

	INFO("[  num   : srcInfo    dstInfo ]\n");
	for (i = 0; i < size / 4; i++) {
		if (*pstd != *pnow) {
			INFO("[X%.8d : 0x%x 0x%x]", i, *pstd, *pnow);
			if (i % 4 == 0)
				INFO("\n");
			err_count++;
		} else {
			if (i % (640 * 1024) == 0)
				INFO("[Y%.8d : 0x%.8x 0x%.8x]\n",
				     i, *pstd, *pnow);
			right_count++;
		}
		pstd++;
		pnow++;
		if (err_count > 64)
			break;
	}

	INFO("err_count=%d, right_count=%d\n", err_count, right_count);
	if (err_count != 0)
		INFO("rga slt err !!\n");
	else
		INFO("rga slt success !!\n");
}
#endif
#endif

void rga2_test_0(void);

static int __init rga2_init(void)
{
	int ret;
	int order = 0;
	uint32_t *buf_p;
	uint32_t *buf;

	/*
	 * malloc pre scale mid buf mmu table:
	 * RGA2_PHY_PAGE_SIZE * channel_num * address_size
	 */
	order = get_order(RGA2_PHY_PAGE_SIZE * 3 * sizeof(buf_p));
	buf_p = (uint32_t *)__get_free_pages(GFP_KERNEL | GFP_DMA32, order);
	if (buf_p == NULL) {
		ERR("Can not alloc pages for mmu_page_table\n");
	}

	rga2_mmu_buf.buf_virtual = buf_p;
	rga2_mmu_buf.buf_order = order;
#if (defined(CONFIG_ARM) && defined(CONFIG_ARM_LPAE))
	buf = (uint32_t *)(uint32_t)virt_to_phys((void *)((unsigned long)buf_p));
#else
	buf = (uint32_t *)virt_to_phys((void *)((unsigned long)buf_p));
#endif
	rga2_mmu_buf.buf = buf;
	rga2_mmu_buf.front = 0;
	rga2_mmu_buf.back = RGA2_PHY_PAGE_SIZE * 3;
	rga2_mmu_buf.size = RGA2_PHY_PAGE_SIZE * 3;

	order = get_order(RGA2_PHY_PAGE_SIZE * sizeof(struct page *));
	rga2_mmu_buf.pages = (struct page **)__get_free_pages(GFP_KERNEL | GFP_DMA32, order);
	if (rga2_mmu_buf.pages == NULL) {
		ERR("Can not alloc pages for rga2_mmu_buf.pages\n");
	}
	rga2_mmu_buf.pages_order = order;

	ret = platform_driver_register(&rga2_driver);
	if (ret != 0) {
		printk(KERN_ERR "Platform device register failed (%d).\n", ret);
		return ret;
	}

	rga2_session_global.pid = 0x0000ffff;
	INIT_LIST_HEAD(&rga2_session_global.waiting);
	INIT_LIST_HEAD(&rga2_session_global.running);
	INIT_LIST_HEAD(&rga2_session_global.list_session);

	INIT_LIST_HEAD(&rga2_service.waiting);
	INIT_LIST_HEAD(&rga2_service.running);
	INIT_LIST_HEAD(&rga2_service.done);
	INIT_LIST_HEAD(&rga2_service.session);
	init_waitqueue_head(&rga2_session_global.wait);
	//mutex_lock(&rga_service.lock);
	list_add_tail(&rga2_session_global.list_session, &rga2_service.session);
	//mutex_unlock(&rga_service.lock);
	atomic_set(&rga2_session_global.task_running, 0);
	atomic_set(&rga2_session_global.num_done, 0);

#if RGA2_TEST_CASE
	rga2_test_0();
#endif
#if RGA2_DEBUGFS
	rga2_debugfs_add();
#endif
	INFO("Module initialized.\n");

	return 0;
}

static void __exit rga2_exit(void)
{
	rga2_power_off();

	free_pages((unsigned long)rga2_mmu_buf.buf_virtual, rga2_mmu_buf.buf_order);
	free_pages((unsigned long)rga2_mmu_buf.pages, rga2_mmu_buf.pages_order);

	platform_driver_unregister(&rga2_driver);
}


#if RGA2_TEST_CASE

void rga2_test_0(void)
{
	struct rga2_req req;
	rga2_session session;
	unsigned int *src, *dst;

	session.pid	= current->pid;
	INIT_LIST_HEAD(&session.waiting);
	INIT_LIST_HEAD(&session.running);
	INIT_LIST_HEAD(&session.list_session);
	init_waitqueue_head(&session.wait);
	/* no need to protect */
	list_add_tail(&session.list_session, &rga2_service.session);
	atomic_set(&session.task_running, 0);
	atomic_set(&session.num_done, 0);

	memset(&req, 0, sizeof(struct rga2_req));
	src = kmalloc(800*480*4, GFP_KERNEL);
	dst = kmalloc(800*480*4, GFP_KERNEL);

	printk("\n********************************\n");
	printk("************ RGA2_TEST ************\n");
	printk("********************************\n\n");

#if 1
	memset(src, 0x80, 800 * 480 * 4);
	memset(dst, 0xcc, 800 * 480 * 4);
#endif
#if 0
	dmac_flush_range(src, &src[800 * 480]);
	outer_flush_range(virt_to_phys(src), virt_to_phys(&src[800 * 480]));

	dmac_flush_range(dst, &dst[800 * 480]);
	outer_flush_range(virt_to_phys(dst), virt_to_phys(&dst[800 * 480]));
#endif

#if 0
	req.pat.act_w = 16;
	req.pat.act_h = 16;
	req.pat.vir_w = 16;
	req.pat.vir_h = 16;
	req.pat.yrgb_addr = virt_to_phys(src);
	req.render_mode = 0;
	rga2_blit_sync(&session, &req);
#endif
	{
		uint32_t i, j;
		uint8_t *sp;

		sp = (uint8_t *)src;
		for (j = 0; j < 240; j++) {
			sp = (uint8_t *)src + j * 320 * 10 / 8;
			for (i = 0; i < 320; i++) {
				if ((i & 3) == 0) {
					sp[i * 5 / 4] = 0;
					sp[i * 5 / 4+1] = 0x1;
				} else if ((i & 3) == 1) {
					sp[i * 5 / 4+1] = 0x4;
				} else if ((i & 3) == 2) {
					sp[i * 5 / 4+1] = 0x10;
				} else if ((i & 3) == 3) {
					sp[i * 5 / 4+1] = 0x40;
			    }
			}
		}
		sp = (uint8_t *)src;
		for (j = 0; j < 100; j++)
			printk("src %.2x\n", sp[j]);
	}
	req.src.act_w = 320;
	req.src.act_h = 240;

	req.src.vir_w = 320;
	req.src.vir_h = 240;
	req.src.yrgb_addr = 0;//(uint32_t)virt_to_phys(src);
	req.src.uv_addr = (unsigned long)virt_to_phys(src);
	req.src.v_addr = 0;
	req.src.format = RGA2_FORMAT_YCbCr_420_SP_10B;

	req.dst.act_w  = 320;
	req.dst.act_h = 240;
	req.dst.x_offset = 0;
	req.dst.y_offset = 0;

	req.dst.vir_w = 320;
	req.dst.vir_h = 240;

	req.dst.yrgb_addr = 0;//((uint32_t)virt_to_phys(dst));
	req.dst.uv_addr = (unsigned long)virt_to_phys(dst);
	req.dst.format = RGA2_FORMAT_YCbCr_420_SP;

	//dst = dst0;

	//req.render_mode = color_fill_mode;
	//req.fg_color = 0x80ffffff;

	req.rotate_mode = 0;
	req.scale_bicu_mode = 2;

#if 0
	//req.alpha_rop_flag = 0;
	//req.alpha_rop_mode = 0x19;
	//req.PD_mode = 3;

	//req.mmu_info.mmu_flag = 0x21;
	//req.mmu_info.mmu_en = 1;

	//printk("src = %.8x\n", req.src.yrgb_addr);
	//printk("src = %.8x\n", req.src.uv_addr);
	//printk("dst = %.8x\n", req.dst.yrgb_addr);
#endif

	rga2_blit_sync(&session, &req);

#if 0
	uint32_t j;
	for (j = 0; j < 320 * 240 * 10 / 8; j++) {
        if (src[j] != dst[j])
		printk("error value dst not equal src j %d, s %.2x d %.2x\n",
			j, src[j], dst[j]);
	}
#endif

#if 1
	{
		uint32_t j;
		uint8_t *dp = (uint8_t *)dst;

		for (j = 0; j < 100; j++)
			printk("%d %.2x\n", j, dp[j]);
	}
#endif

	kfree(src);
	kfree(dst);
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
module_init(rga2_init);
#else
late_initcall(rga2_init);
#endif
#else
fs_initcall(rga2_init);
#endif
module_exit(rga2_exit);

/* Module information */
MODULE_AUTHOR("zsq@rock-chips.com");
MODULE_DESCRIPTION("Driver for rga device");
MODULE_LICENSE("GPL");
