/*
 * Copyright 2005-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file ipu_device.c
 *
 * @brief This file contains the IPUv3 driver device interface and fops functions.
 *
 * @ingroup IPU
 */
#include <linux/clk.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ipu-v3.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#include <asm/cacheflush.h>
#include <asm/outercache.h>

#include "ipu_param_mem.h"
#include "ipu_regs.h"
#include "vdoa.h"

#define CHECK_RETCODE(cont, str, err, label, ret)			\
do {									\
	if (cont) {							\
		dev_err(t->dev, "ERR:[0x%p]-no:0x%x "#str" ret:%d,"	\
				"line:%d\n", t, t->task_no, ret, __LINE__);\
		if (ret != -EACCES) {					\
			t->state = err;					\
			goto label;					\
		}							\
	}								\
} while (0)

#define CHECK_RETCODE_CONT(cont, str, err, ret)				\
do {									\
	if (cont) {							\
		dev_err(t->dev, "ERR:[0x%p]-no:0x%x"#str" ret:%d,"	\
				"line:%d\n", t, t->task_no, ret, __LINE__);\
		if (ret != -EACCES) {					\
			if (t->state == STATE_OK)			\
				t->state = err;				\
		}							\
	}								\
} while (0)

#undef DBG_IPU_PERF
#ifdef DBG_IPU_PERF
#define CHECK_PERF(ts)							\
do {									\
	getnstimeofday(ts);						\
} while (0)

#define DECLARE_PERF_VAR						\
	struct timespec ts_queue;					\
	struct timespec ts_dotask;					\
	struct timespec ts_waitirq;					\
	struct timespec ts_sche;					\
	struct timespec ts_rel;						\
	struct timespec ts_frame

#define PRINT_TASK_STATISTICS						\
do {									\
	ts_queue = timespec_sub(tsk->ts_dotask, tsk->ts_queue);		\
	ts_dotask = timespec_sub(tsk->ts_waitirq, tsk->ts_dotask);	\
	ts_waitirq = timespec_sub(tsk->ts_inirq, tsk->ts_waitirq);	\
	ts_sche = timespec_sub(tsk->ts_wakeup, tsk->ts_inirq);		\
	ts_rel = timespec_sub(tsk->ts_rel, tsk->ts_wakeup);		\
	ts_frame = timespec_sub(tsk->ts_rel, tsk->ts_queue);		\
	dev_dbg(tsk->dev, "[0x%p] no-0x%x, ts_q:%ldus, ts_do:%ldus,"	\
		"ts_waitirq:%ldus,ts_sche:%ldus, ts_rel:%ldus,"		\
		"ts_frame: %ldus\n", tsk, tsk->task_no,			\
	ts_queue.tv_nsec / NSEC_PER_USEC + ts_queue.tv_sec * USEC_PER_SEC,\
	ts_dotask.tv_nsec / NSEC_PER_USEC + ts_dotask.tv_sec * USEC_PER_SEC,\
	ts_waitirq.tv_nsec / NSEC_PER_USEC + ts_waitirq.tv_sec * USEC_PER_SEC,\
	ts_sche.tv_nsec / NSEC_PER_USEC + ts_sche.tv_sec * USEC_PER_SEC,\
	ts_rel.tv_nsec / NSEC_PER_USEC + ts_rel.tv_sec * USEC_PER_SEC,\
	ts_frame.tv_nsec / NSEC_PER_USEC + ts_frame.tv_sec * USEC_PER_SEC); \
	if ((ts_frame.tv_nsec/NSEC_PER_USEC + ts_frame.tv_sec*USEC_PER_SEC) > \
		80000)	\
		dev_dbg(tsk->dev, "ts_frame larger than 80ms [0x%p] no-0x%x.\n"\
				, tsk, tsk->task_no);	\
} while (0)
#else
#define CHECK_PERF(ts)
#define DECLARE_PERF_VAR
#define PRINT_TASK_STATISTICS
#endif

#define	IPU_PP_CH_VF	(IPU_TASK_ID_VF - 1)
#define	IPU_PP_CH_PP	(IPU_TASK_ID_PP - 1)
#define MAX_PP_CH	(IPU_TASK_ID_MAX - 1)
#define VDOA_DEF_TIMEOUT_MS	(HZ/2)

/* Strucutures and variables for exporting MXC IPU as device*/
typedef enum {
	STATE_OK = 0,
	STATE_QUEUE,
	STATE_IN_PROGRESS,
	STATE_ERR,
	STATE_TIMEOUT,
	STATE_RES_TIMEOUT,
	STATE_NO_IPU,
	STATE_NO_IRQ,
	STATE_IPU_BUSY,
	STATE_IRQ_FAIL,
	STATE_IRQ_TIMEOUT,
	STATE_ENABLE_CHAN_FAIL,
	STATE_DISABLE_CHAN_FAIL,
	STATE_SEL_BUF_FAIL,
	STATE_INIT_CHAN_FAIL,
	STATE_LINK_CHAN_FAIL,
	STATE_UNLINK_CHAN_FAIL,
	STATE_INIT_CHAN_BUF_FAIL,
	STATE_INIT_CHAN_BAND_FAIL,
	STATE_SYS_NO_MEM,
	STATE_VDOA_IRQ_TIMEOUT,
	STATE_VDOA_IRQ_FAIL,
	STATE_VDOA_TASK_FAIL,
} ipu_state_t;

enum {
	INPUT_CHAN_VDI_P = 1,
	INPUT_CHAN,
	INPUT_CHAN_VDI_N,
};

struct ipu_state_msg {
	int state;
	char *msg;
} state_msg[] = {
	{STATE_OK, "ok"},
	{STATE_QUEUE, "split queue"},
	{STATE_IN_PROGRESS, "split in progress"},
	{STATE_ERR, "error"},
	{STATE_TIMEOUT, "split task timeout"},
	{STATE_RES_TIMEOUT, "wait resource timeout"},
	{STATE_NO_IPU, "no ipu found"},
	{STATE_NO_IRQ, "no irq found for task"},
	{STATE_IPU_BUSY, "ipu busy"},
	{STATE_IRQ_FAIL, "request irq failed"},
	{STATE_IRQ_TIMEOUT, "wait for irq timeout"},
	{STATE_ENABLE_CHAN_FAIL, "ipu enable channel fail"},
	{STATE_DISABLE_CHAN_FAIL, "ipu disable channel fail"},
	{STATE_SEL_BUF_FAIL, "ipu select buf fail"},
	{STATE_INIT_CHAN_FAIL, "ipu init channel fail"},
	{STATE_LINK_CHAN_FAIL, "ipu link channel fail"},
	{STATE_UNLINK_CHAN_FAIL, "ipu unlink channel fail"},
	{STATE_INIT_CHAN_BUF_FAIL, "ipu init channel buffer fail"},
	{STATE_INIT_CHAN_BAND_FAIL, "ipu init channel band mode fail"},
	{STATE_SYS_NO_MEM, "sys no mem: -ENOMEM"},
	{STATE_VDOA_IRQ_TIMEOUT, "wait for vdoa irq timeout"},
	{STATE_VDOA_IRQ_FAIL, "vdoa irq fail"},
	{STATE_VDOA_TASK_FAIL, "vdoa task fail"},
};

struct stripe_setting {
	u32 iw;
	u32 ih;
	u32 ow;
	u32 oh;
	u32 outh_resize_ratio;
	u32 outv_resize_ratio;
	u32 i_left_pos;
	u32 i_right_pos;
	u32 i_top_pos;
	u32 i_bottom_pos;
	u32 o_left_pos;
	u32 o_right_pos;
	u32 o_top_pos;
	u32 o_bottom_pos;
	u32 rl_split_line;
	u32 ud_split_line;
};

struct task_set {
#define	NULL_MODE	0x0
#define	IC_MODE		0x1
#define	ROT_MODE	0x2
#define	VDI_MODE	0x4
#define IPU_PREPROCESS_MODE_MASK	(IC_MODE | ROT_MODE | VDI_MODE)
/* VDOA_MODE means this task use vdoa, and VDOA has two modes:
 * BAND MODE and non-BAND MODE. Non-band mode will do transfer data
 * to memory. BAND mode needs hareware sync with IPU, it is used default
 * if connected to VDIC.
 */
#define	VDOA_MODE	0x8
#define	VDOA_BAND_MODE	0x10
	u8	mode;
#define IC_VF	0x1
#define IC_PP	0x2
#define ROT_VF	0x4
#define ROT_PP	0x8
#define VDI_VF	0x10
#define	VDOA_ONLY	0x20
	u8	task;
#define NO_SPLIT	0x0
#define RL_SPLIT	0x1
#define UD_SPLIT	0x2
#define LEFT_STRIPE	0x1
#define RIGHT_STRIPE	0x2
#define UP_STRIPE	0x4
#define DOWN_STRIPE	0x8
#define SPLIT_MASK	0xF
	u8	split_mode;
	u8	band_lines;
	ipu_channel_t ic_chan;
	ipu_channel_t rot_chan;
	ipu_channel_t vdi_ic_p_chan;
	ipu_channel_t vdi_ic_n_chan;

	u32 i_off;
	u32 i_uoff;
	u32 i_voff;
	u32 istride;

	u32 ov_off;
	u32 ov_uoff;
	u32 ov_voff;
	u32 ovstride;

	u32 ov_alpha_off;
	u32 ov_alpha_stride;

	u32 o_off;
	u32 o_uoff;
	u32 o_voff;
	u32 ostride;

	u32 r_fmt;
	u32 r_width;
	u32 r_height;
	u32 r_stride;
	dma_addr_t r_paddr;

	struct stripe_setting sp_setting;
};

struct ipu_split_task {
	struct ipu_task task;
	struct ipu_task_entry *parent_task;
	struct ipu_task_entry *child_task;
	u32 task_no;
};

struct ipu_task_entry {
	struct ipu_input input;
	struct ipu_output output;

	bool overlay_en;
	struct ipu_overlay overlay;
#define DEF_TIMEOUT_MS	1000
#define DEF_DELAY_MS 20
	int	timeout;
	int	irq;

	u8	task_id;
	u8	ipu_id;
	u8	task_in_list;
	u8	split_done;
	struct mutex split_lock;
	struct mutex vdic_lock;
	wait_queue_head_t split_waitq;

	struct list_head node;
	struct list_head split_list;
	struct ipu_soc *ipu;
	struct device *dev;
	struct task_set set;
	wait_queue_head_t task_waitq;
	struct completion irq_comp;
	struct kref refcount;
	ipu_state_t state;
	u32 task_no;
	atomic_t done;
	atomic_t res_free;
	atomic_t res_get;

	struct ipu_task_entry *parent;
	char *vditmpbuf[2];
	u32 old_save_lines;
	u32 old_size;
	bool buf1filled;
	bool buf0filled;

	vdoa_handle_t vdoa_handle;
	struct vdoa_output_mem {
		void *vaddr;
		dma_addr_t paddr;
		int size;
	} vdoa_dma;

#ifdef DBG_IPU_PERF
	struct timespec ts_queue;
	struct timespec ts_dotask;
	struct timespec ts_waitirq;
	struct timespec ts_inirq;
	struct timespec ts_wakeup;
	struct timespec ts_rel;
#endif
};

struct ipu_channel_tabel {
	struct mutex	lock;
	u8		used[MXC_IPU_MAX_NUM][MAX_PP_CH];
	u8		vdoa_used;
};

struct ipu_thread_data {
	struct ipu_soc *ipu;
	u32	id;
	u32	is_vdoa;
};

struct ipu_alloc_list {
	struct list_head list;
	dma_addr_t phy_addr;
	void *cpu_addr;
	u32 size;
	void *file_index;
};

static LIST_HEAD(ipu_alloc_list);
static DEFINE_MUTEX(ipu_alloc_lock);
static struct ipu_channel_tabel	ipu_ch_tbl;
static LIST_HEAD(ipu_task_list);
static DEFINE_SPINLOCK(ipu_task_list_lock);
static DECLARE_WAIT_QUEUE_HEAD(thread_waitq);
static DECLARE_WAIT_QUEUE_HEAD(res_waitq);
static atomic_t req_cnt;
static atomic_t file_index = ATOMIC_INIT(1);
static int major;
static int max_ipu_no;
static int thread_id;
static atomic_t frame_no;
static struct class *ipu_class;
static struct device *ipu_dev;
static int debug;
module_param(debug, int, 0600);
#ifdef DBG_IPU_PERF
static struct timespec ts_frame_max;
static u32 ts_frame_avg;
static atomic_t frame_cnt;
#endif

static bool deinterlace_3_field(struct ipu_task_entry *t)
{
	return ((t->set.mode & VDI_MODE) &&
		(t->input.deinterlace.motion != HIGH_MOTION));
}

static u32 tiled_filed_size(struct ipu_task_entry *t)
{
	u32 field_size;

	/* note: page_align is required by VPU hw ouput buffer */
	field_size = TILED_NV12_FRAME_SIZE(t->input.width, t->input.height/2);
	return field_size;
}

static bool only_ic(u8 mode)
{
	mode = mode & IPU_PREPROCESS_MODE_MASK;
	return ((mode == IC_MODE) || (mode == VDI_MODE));
}

static bool only_rot(u8 mode)
{
	mode = mode & IPU_PREPROCESS_MODE_MASK;
	return (mode == ROT_MODE);
}

static bool ic_and_rot(u8 mode)
{
	mode = mode & IPU_PREPROCESS_MODE_MASK;
	return ((mode == (IC_MODE | ROT_MODE)) ||
		 (mode == (VDI_MODE | ROT_MODE)));
}

static bool need_split(struct ipu_task_entry *t)
{
	return ((t->set.split_mode != NO_SPLIT) || (t->task_no & SPLIT_MASK));
}

unsigned int fmt_to_bpp(unsigned int pixelformat)
{
	u32 bpp;

	switch (pixelformat) {
	case IPU_PIX_FMT_RGB565:
	/*interleaved 422*/
	case IPU_PIX_FMT_YUYV:
	case IPU_PIX_FMT_UYVY:
	/*non-interleaved 422*/
	case IPU_PIX_FMT_YUV422P:
	case IPU_PIX_FMT_YVU422P:
		bpp = 16;
		break;
	case IPU_PIX_FMT_BGR24:
	case IPU_PIX_FMT_RGB24:
	case IPU_PIX_FMT_YUV444:
	case IPU_PIX_FMT_YUV444P:
		bpp = 24;
		break;
	case IPU_PIX_FMT_BGR32:
	case IPU_PIX_FMT_BGRA32:
	case IPU_PIX_FMT_RGB32:
	case IPU_PIX_FMT_RGBA32:
	case IPU_PIX_FMT_ABGR32:
		bpp = 32;
		break;
	/*non-interleaved 420*/
	case IPU_PIX_FMT_YUV420P:
	case IPU_PIX_FMT_YVU420P:
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_NV12:
		bpp = 12;
		break;
	default:
		bpp = 8;
		break;
	}
	return bpp;
}
EXPORT_SYMBOL_GPL(fmt_to_bpp);

cs_t colorspaceofpixel(int fmt)
{
	switch (fmt) {
	case IPU_PIX_FMT_RGB565:
	case IPU_PIX_FMT_RGB666:
	case IPU_PIX_FMT_BGR24:
	case IPU_PIX_FMT_RGB24:
	case IPU_PIX_FMT_BGRA32:
	case IPU_PIX_FMT_BGR32:
	case IPU_PIX_FMT_RGBA32:
	case IPU_PIX_FMT_RGB32:
	case IPU_PIX_FMT_ABGR32:
		return RGB_CS;
		break;
	case IPU_PIX_FMT_UYVY:
	case IPU_PIX_FMT_YUYV:
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_YUV420P:
	case IPU_PIX_FMT_YVU420P:
	case IPU_PIX_FMT_YVU422P:
	case IPU_PIX_FMT_YUV422P:
	case IPU_PIX_FMT_YUV444:
	case IPU_PIX_FMT_YUV444P:
	case IPU_PIX_FMT_NV12:
	case IPU_PIX_FMT_TILED_NV12:
	case IPU_PIX_FMT_TILED_NV12F:
		return YUV_CS;
		break;
	default:
		return NULL_CS;
	}
}
EXPORT_SYMBOL_GPL(colorspaceofpixel);

int need_csc(int ifmt, int ofmt)
{
	cs_t ics, ocs;

	ics = colorspaceofpixel(ifmt);
	ocs = colorspaceofpixel(ofmt);

	if ((ics == NULL_CS) || (ocs == NULL_CS))
		return -1;
	else if (ics != ocs)
		return 1;

	return 0;
}
EXPORT_SYMBOL_GPL(need_csc);

static int soc_max_in_width(u32 is_vdoa)
{
	return is_vdoa ? 8192 : 4096;
}

static int soc_max_vdi_in_width(struct ipu_soc *ipu)
{
	int i;

	if (!ipu) {
		for (i = 0; i < max_ipu_no; i++) {
			ipu = ipu_get_soc(i);
			if (!IS_ERR_OR_NULL(ipu))
				break;
		}

		if (i == max_ipu_no)
			return 720;
	}
	return IPU_MAX_VDI_IN_WIDTH(ipu->devtype);
}
static int soc_max_in_height(void)
{
	return 4096;
}

static int soc_max_out_width(void)
{
	/* mx51/mx53/mx6q is 1024*/
	return 1024;
}

static int soc_max_out_height(void)
{
	/* mx51/mx53/mx6q is 1024*/
	return 1024;
}

static void dump_task_info(struct ipu_task_entry *t)
{
	if (!debug)
		return;
	dev_dbg(t->dev, "[0x%p]input:\n", (void *)t);
	dev_dbg(t->dev, "[0x%p]\tformat = 0x%x\n", (void *)t, t->input.format);
	dev_dbg(t->dev, "[0x%p]\twidth = %d\n", (void *)t, t->input.width);
	dev_dbg(t->dev, "[0x%p]\theight = %d\n", (void *)t, t->input.height);
	dev_dbg(t->dev, "[0x%p]\tcrop.w = %d\n", (void *)t, t->input.crop.w);
	dev_dbg(t->dev, "[0x%p]\tcrop.h = %d\n", (void *)t, t->input.crop.h);
	dev_dbg(t->dev, "[0x%p]\tcrop.pos.x = %d\n",
			(void *)t, t->input.crop.pos.x);
	dev_dbg(t->dev, "[0x%p]\tcrop.pos.y = %d\n",
			(void *)t, t->input.crop.pos.y);
	dev_dbg(t->dev, "[0x%p]input buffer:\n", (void *)t);
	dev_dbg(t->dev, "[0x%p]\tpaddr = 0x%x\n", (void *)t, t->input.paddr);
	dev_dbg(t->dev, "[0x%p]\ti_off = 0x%x\n", (void *)t, t->set.i_off);
	dev_dbg(t->dev, "[0x%p]\ti_uoff = 0x%x\n", (void *)t, t->set.i_uoff);
	dev_dbg(t->dev, "[0x%p]\ti_voff = 0x%x\n", (void *)t, t->set.i_voff);
	dev_dbg(t->dev, "[0x%p]\tistride = %d\n", (void *)t, t->set.istride);
	if (t->input.deinterlace.enable) {
		dev_dbg(t->dev, "[0x%p]deinterlace enabled with:\n", (void *)t);
		if (t->input.deinterlace.motion != HIGH_MOTION) {
			dev_dbg(t->dev, "[0x%p]\tlow/medium motion\n", (void *)t);
			dev_dbg(t->dev, "[0x%p]\tpaddr_n = 0x%x\n",
				(void *)t, t->input.paddr_n);
		} else
			dev_dbg(t->dev, "[0x%p]\thigh motion\n", (void *)t);
	}

	dev_dbg(t->dev, "[0x%p]output:\n", (void *)t);
	dev_dbg(t->dev, "[0x%p]\tformat = 0x%x\n", (void *)t, t->output.format);
	dev_dbg(t->dev, "[0x%p]\twidth = %d\n", (void *)t, t->output.width);
	dev_dbg(t->dev, "[0x%p]\theight = %d\n", (void *)t, t->output.height);
	dev_dbg(t->dev, "[0x%p]\tcrop.w = %d\n", (void *)t, t->output.crop.w);
	dev_dbg(t->dev, "[0x%p]\tcrop.h = %d\n", (void *)t, t->output.crop.h);
	dev_dbg(t->dev, "[0x%p]\tcrop.pos.x = %d\n",
			(void *)t, t->output.crop.pos.x);
	dev_dbg(t->dev, "[0x%p]\tcrop.pos.y = %d\n",
			(void *)t, t->output.crop.pos.y);
	dev_dbg(t->dev, "[0x%p]\trotate = %d\n", (void *)t, t->output.rotate);
	dev_dbg(t->dev, "[0x%p]output buffer:\n", (void *)t);
	dev_dbg(t->dev, "[0x%p]\tpaddr = 0x%x\n", (void *)t, t->output.paddr);
	dev_dbg(t->dev, "[0x%p]\to_off = 0x%x\n", (void *)t, t->set.o_off);
	dev_dbg(t->dev, "[0x%p]\to_uoff = 0x%x\n", (void *)t, t->set.o_uoff);
	dev_dbg(t->dev, "[0x%p]\to_voff = 0x%x\n", (void *)t, t->set.o_voff);
	dev_dbg(t->dev, "[0x%p]\tostride = %d\n", (void *)t, t->set.ostride);

	if (t->overlay_en) {
		dev_dbg(t->dev, "[0x%p]overlay:\n", (void *)t);
		dev_dbg(t->dev, "[0x%p]\tformat = 0x%x\n",
				(void *)t, t->overlay.format);
		dev_dbg(t->dev, "[0x%p]\twidth = %d\n",
				(void *)t, t->overlay.width);
		dev_dbg(t->dev, "[0x%p]\theight = %d\n",
				(void *)t, t->overlay.height);
		dev_dbg(t->dev, "[0x%p]\tcrop.w = %d\n",
				(void *)t, t->overlay.crop.w);
		dev_dbg(t->dev, "[0x%p]\tcrop.h = %d\n",
				(void *)t, t->overlay.crop.h);
		dev_dbg(t->dev, "[0x%p]\tcrop.pos.x = %d\n",
				(void *)t, t->overlay.crop.pos.x);
		dev_dbg(t->dev, "[0x%p]\tcrop.pos.y = %d\n",
				(void *)t, t->overlay.crop.pos.y);
		dev_dbg(t->dev, "[0x%p]overlay buffer:\n", (void *)t);
		dev_dbg(t->dev, "[0x%p]\tpaddr = 0x%x\n",
				(void *)t, t->overlay.paddr);
		dev_dbg(t->dev, "[0x%p]\tov_off = 0x%x\n",
				(void *)t, t->set.ov_off);
		dev_dbg(t->dev, "[0x%p]\tov_uoff = 0x%x\n",
				(void *)t, t->set.ov_uoff);
		dev_dbg(t->dev, "[0x%p]\tov_voff = 0x%x\n",
				(void *)t, t->set.ov_voff);
		dev_dbg(t->dev, "[0x%p]\tovstride = %d\n",
				(void *)t, t->set.ovstride);
		if (t->overlay.alpha.mode == IPU_ALPHA_MODE_LOCAL) {
			dev_dbg(t->dev, "[0x%p]local alpha enabled with:\n",
					(void *)t);
			dev_dbg(t->dev, "[0x%p]\tpaddr = 0x%x\n",
					(void *)t, t->overlay.alpha.loc_alp_paddr);
			dev_dbg(t->dev, "[0x%p]\tov_alpha_off = 0x%x\n",
					(void *)t, t->set.ov_alpha_off);
			dev_dbg(t->dev, "[0x%p]\tov_alpha_stride = %d\n",
					(void *)t, t->set.ov_alpha_stride);
		} else
			dev_dbg(t->dev, "[0x%p]globle alpha enabled with value 0x%x\n",
					(void *)t, t->overlay.alpha.gvalue);
		if (t->overlay.colorkey.enable)
			dev_dbg(t->dev, "[0x%p]colorkey enabled with value 0x%x\n",
					(void *)t, t->overlay.colorkey.value);
	}

	dev_dbg(t->dev, "[0x%p]want task_id = %d\n", (void *)t, t->task_id);
	dev_dbg(t->dev, "[0x%p]want task mode is 0x%x\n",
				(void *)t, t->set.mode);
	dev_dbg(t->dev, "[0x%p]\tIC_MODE = 0x%x\n", (void *)t, IC_MODE);
	dev_dbg(t->dev, "[0x%p]\tROT_MODE = 0x%x\n", (void *)t, ROT_MODE);
	dev_dbg(t->dev, "[0x%p]\tVDI_MODE = 0x%x\n", (void *)t, VDI_MODE);
	dev_dbg(t->dev, "[0x%p]\tTask_no = 0x%x\n\n\n", (void *)t, t->task_no);
}

static void dump_check_err(struct device *dev, int err)
{
	switch (err) {
	case IPU_CHECK_ERR_INPUT_CROP:
		dev_err(dev, "input crop setting error\n");
		break;
	case IPU_CHECK_ERR_OUTPUT_CROP:
		dev_err(dev, "output crop setting error\n");
		break;
	case IPU_CHECK_ERR_OVERLAY_CROP:
		dev_err(dev, "overlay crop setting error\n");
		break;
	case IPU_CHECK_ERR_INPUT_OVER_LIMIT:
		dev_err(dev, "input over limitation\n");
		break;
	case IPU_CHECK_ERR_OVERLAY_WITH_VDI:
		dev_err(dev, "do not support overlay with deinterlace\n");
		break;
	case IPU_CHECK_ERR_OV_OUT_NO_FIT:
		dev_err(dev,
			"width/height of overlay and ic output should be same\n");
		break;
	case IPU_CHECK_ERR_PROC_NO_NEED:
		dev_err(dev, "no ipu processing need\n");
		break;
	case IPU_CHECK_ERR_SPLIT_INPUTW_OVER:
		dev_err(dev, "split mode input width overflow\n");
		break;
	case IPU_CHECK_ERR_SPLIT_INPUTH_OVER:
		dev_err(dev, "split mode input height overflow\n");
		break;
	case IPU_CHECK_ERR_SPLIT_OUTPUTW_OVER:
		dev_err(dev, "split mode output width overflow\n");
		break;
	case IPU_CHECK_ERR_SPLIT_OUTPUTH_OVER:
		dev_err(dev, "split mode output height overflow\n");
		break;
	case IPU_CHECK_ERR_SPLIT_WITH_ROT:
		dev_err(dev, "not support split mode with rotation\n");
		break;
	case IPU_CHECK_ERR_W_DOWNSIZE_OVER:
		dev_err(dev, "horizontal downsizing ratio overflow\n");
		break;
	case IPU_CHECK_ERR_H_DOWNSIZE_OVER:
		dev_err(dev, "vertical downsizing ratio overflow\n");
		break;
	default:
		break;
	}
}

static void dump_check_warn(struct device *dev, int warn)
{
	if (warn & IPU_CHECK_WARN_INPUT_OFFS_NOT8ALIGN)
		dev_warn(dev, "input u/v offset not 8 align\n");
	if (warn & IPU_CHECK_WARN_OUTPUT_OFFS_NOT8ALIGN)
		dev_warn(dev, "output u/v offset not 8 align\n");
	if (warn & IPU_CHECK_WARN_OVERLAY_OFFS_NOT8ALIGN)
		dev_warn(dev, "overlay u/v offset not 8 align\n");
}

static int set_crop(struct ipu_crop *crop, int width, int height, int fmt)
{
	if ((width == 0) || (height == 0)) {
		pr_err("Invalid param: width=%d, height=%d\n", width, height);
		return -EINVAL;
	}

	if ((IPU_PIX_FMT_TILED_NV12 == fmt) ||
		(IPU_PIX_FMT_TILED_NV12F == fmt)) {
		if (crop->w || crop->h) {
			if (((crop->w + crop->pos.x) > width)
			|| ((crop->h + crop->pos.y) > height)
			|| (0 != (crop->w % IPU_PIX_FMT_TILED_NV12_MBALIGN))
			|| (0 != (crop->h % IPU_PIX_FMT_TILED_NV12_MBALIGN))
			|| (0 != (crop->pos.x % IPU_PIX_FMT_TILED_NV12_MBALIGN))
			|| (0 != (crop->pos.y % IPU_PIX_FMT_TILED_NV12_MBALIGN))
			) {
				pr_err("set_crop error MB align.\n");
				return -EINVAL;
			}
		} else {
			crop->pos.x = 0;
			crop->pos.y = 0;
			crop->w = width;
			crop->h = height;
			if ((0 != (crop->w % IPU_PIX_FMT_TILED_NV12_MBALIGN))
			|| (0 != (crop->h % IPU_PIX_FMT_TILED_NV12_MBALIGN))) {
				pr_err("set_crop error w/h MB align.\n");
				return -EINVAL;
			}
		}
	} else {
		if (crop->w || crop->h) {
			if (((crop->w + crop->pos.x) > (width + 16))
			|| ((crop->h + crop->pos.y) > height + 16)) {
				pr_err("set_crop error exceeds width/height.\n");
				return -EINVAL;
			}
		} else {
			crop->pos.x = 0;
			crop->pos.y = 0;
			crop->w = width;
			crop->h = height;
		}
		crop->w -= crop->w%8;
		crop->h -= crop->h%8;
	}

	if ((crop->w == 0) || (crop->h == 0)) {
		pr_err("Invalid crop param: crop.w=%d, crop.h=%d\n",
			crop->w, crop->h);
		return -EINVAL;
	}

	return 0;
}

static void update_offset(unsigned int fmt,
				unsigned int width, unsigned int height,
				unsigned int pos_x, unsigned int pos_y,
				int *off, int *uoff, int *voff, int *stride)
{
	/* NOTE: u v offset should based on start point of off*/
	switch (fmt) {
	case IPU_PIX_FMT_YUV420P2:
	case IPU_PIX_FMT_YUV420P:
		*off = pos_y * width + pos_x;
		*uoff = (width * (height - pos_y) - pos_x)
			+ (width/2) * (pos_y/2) + pos_x/2;
		/* In case height is odd, round up to even */
		*voff = *uoff + (width/2) * ((height+1)/2);
		break;
	case IPU_PIX_FMT_YVU420P:
		*off = pos_y * width + pos_x;
		*voff = (width * (height - pos_y) - pos_x)
			+ (width/2) * (pos_y/2) + pos_x/2;
		/* In case height is odd, round up to even */
		*uoff = *voff + (width/2) * ((height+1)/2);
		break;
	case IPU_PIX_FMT_YVU422P:
		*off = pos_y * width + pos_x;
		*voff = (width * (height - pos_y) - pos_x)
			+ (width/2) * pos_y + pos_x/2;
		*uoff = *voff + (width/2) * height;
		break;
	case IPU_PIX_FMT_YUV422P:
		*off = pos_y * width + pos_x;
		*uoff = (width * (height - pos_y) - pos_x)
			+ (width/2) * pos_y + pos_x/2;
		*voff = *uoff + (width/2) * height;
		break;
	case IPU_PIX_FMT_YUV444P:
		*off = pos_y * width + pos_x;
		*uoff = width * height;
		*voff = width * height * 2;
		break;
	case IPU_PIX_FMT_NV12:
		*off = pos_y * width + pos_x;
		*uoff = (width * (height - pos_y) - pos_x)
			+ width * (pos_y/2) + pos_x;
		break;
	case IPU_PIX_FMT_TILED_NV12:
		/*
		 * tiled format, progressive:
		 * assuming that line is aligned with MB height (aligned to 16)
		 * offset = line * stride + (pixel / MB_width) * pixels_in_MB
		 * = line * stride + (pixel / 16) * 256
		 * = line * stride + pixel * 16
		 */
		*off = pos_y * width + (pos_x << 4);
		*uoff = ALIGN(width * height, SZ_4K) + (*off >> 1) - *off;
		break;
	case IPU_PIX_FMT_TILED_NV12F:
		/*
		 * tiled format, interlaced:
		 * same as above, only number of pixels in MB is 128,
		 * instead of 256
		 */
		*off = (pos_y >> 1) * width + (pos_x << 3);
		*uoff = ALIGN(width * height/2, SZ_4K) + (*off >> 1) - *off;
		break;
	default:
		*off = (pos_y * width + pos_x) * fmt_to_bpp(fmt)/8;
		break;
	}
	*stride = width * bytes_per_pixel(fmt);
}

static int update_split_setting(struct ipu_task_entry *t, bool vdi_split)
{
	struct stripe_param left_stripe;
	struct stripe_param right_stripe;
	struct stripe_param up_stripe;
	struct stripe_param down_stripe;
	u32 iw, ih, ow, oh;
	u32 max_width;
	int ret;

	if (t->output.rotate >= IPU_ROTATE_90_RIGHT)
		return IPU_CHECK_ERR_SPLIT_WITH_ROT;

	iw = t->input.crop.w;
	ih = t->input.crop.h;

	ow = t->output.crop.w;
	oh = t->output.crop.h;

	memset(&left_stripe, 0, sizeof(left_stripe));
	memset(&right_stripe, 0, sizeof(right_stripe));
	memset(&up_stripe, 0, sizeof(up_stripe));
	memset(&down_stripe, 0, sizeof(down_stripe));

	if (t->set.split_mode & RL_SPLIT) {
		/*
		 * We do want equal strips: initialize stripes in case
		 * calc_stripes returns before actually doing the calculation
		 */
		left_stripe.input_width = iw / 2;
		left_stripe.output_width = ow / 2;
		right_stripe.input_column = iw / 2;
		right_stripe.output_column = ow / 2;

		if (vdi_split)
			max_width = soc_max_vdi_in_width(t->ipu);
		else
			max_width = soc_max_out_width();
		ret = ipu_calc_stripes_sizes(iw,
				ow,
				max_width,
				(((unsigned long long)1) << 32), /* 32bit for fractional*/
				1, /* equal stripes */
				t->input.format,
				t->output.format,
				&left_stripe,
				&right_stripe);
		if (ret < 0)
			return IPU_CHECK_ERR_W_DOWNSIZE_OVER;
		else if (ret)
			dev_dbg(t->dev, "Warn: no:0x%x,calc_stripes ret:%d\n",
				 t->task_no, ret);
		t->set.sp_setting.iw = left_stripe.input_width;
		t->set.sp_setting.ow = left_stripe.output_width;
		t->set.sp_setting.outh_resize_ratio = left_stripe.irr;
		t->set.sp_setting.i_left_pos = left_stripe.input_column;
		t->set.sp_setting.o_left_pos = left_stripe.output_column;
		t->set.sp_setting.i_right_pos = right_stripe.input_column;
		t->set.sp_setting.o_right_pos = right_stripe.output_column;
	} else {
		t->set.sp_setting.iw = iw;
		t->set.sp_setting.ow = ow;
		t->set.sp_setting.outh_resize_ratio = 0;
		t->set.sp_setting.i_left_pos = 0;
		t->set.sp_setting.o_left_pos = 0;
		t->set.sp_setting.i_right_pos = 0;
		t->set.sp_setting.o_right_pos = 0;
	}
	if ((t->set.sp_setting.iw + t->set.sp_setting.i_right_pos) > (iw+16))
		return IPU_CHECK_ERR_SPLIT_INPUTW_OVER;
	if (((t->set.sp_setting.ow + t->set.sp_setting.o_right_pos) > ow)
		|| (t->set.sp_setting.ow > soc_max_out_width()))
		return IPU_CHECK_ERR_SPLIT_OUTPUTW_OVER;
	if (rounddown(t->set.sp_setting.ow, 8) * 8 <=
	    rounddown(t->set.sp_setting.iw, 8))
		return IPU_CHECK_ERR_W_DOWNSIZE_OVER;

	if (t->set.split_mode & UD_SPLIT) {
		/*
		 * We do want equal strips: initialize stripes in case
		 * calc_stripes returns before actually doing the calculation
		 */
		up_stripe.input_width = ih / 2;
		up_stripe.output_width = oh / 2;
		down_stripe.input_column = ih / 2;
		down_stripe.output_column = oh / 2;
		ret = ipu_calc_stripes_sizes(ih,
				oh,
				soc_max_out_height(),
				(((unsigned long long)1) << 32), /* 32bit for fractional*/
				0x1 | 0x2, /* equal stripes and vertical */
				t->input.format,
				t->output.format,
				&up_stripe,
				&down_stripe);
		if (ret < 0)
			return IPU_CHECK_ERR_H_DOWNSIZE_OVER;
		else if (ret)
			dev_err(t->dev, "Warn: no:0x%x,calc_stripes ret:%d\n",
				 t->task_no, ret);
		t->set.sp_setting.ih = up_stripe.input_width;
		t->set.sp_setting.oh = up_stripe.output_width;
		t->set.sp_setting.outv_resize_ratio = up_stripe.irr;
		t->set.sp_setting.i_top_pos = up_stripe.input_column;
		t->set.sp_setting.o_top_pos = up_stripe.output_column;
		t->set.sp_setting.i_bottom_pos = down_stripe.input_column;
		t->set.sp_setting.o_bottom_pos = down_stripe.output_column;
	} else {
		t->set.sp_setting.ih = ih;
		t->set.sp_setting.oh = oh;
		t->set.sp_setting.outv_resize_ratio = 0;
		t->set.sp_setting.i_top_pos = 0;
		t->set.sp_setting.o_top_pos = 0;
		t->set.sp_setting.i_bottom_pos = 0;
		t->set.sp_setting.o_bottom_pos = 0;
	}

	/* downscale case: enforce limits */
	if (((t->set.sp_setting.ih + t->set.sp_setting.i_bottom_pos) > (ih))
	     && (t->set.sp_setting.ih >= t->set.sp_setting.oh))
		return IPU_CHECK_ERR_SPLIT_INPUTH_OVER;
	/* upscale case: relax limits because ipu_calc_stripes_sizes() may
	   create input stripe that falls just outside of the input window */
	else if ((t->set.sp_setting.ih + t->set.sp_setting.i_bottom_pos)
		 > (ih+16))
		return IPU_CHECK_ERR_SPLIT_INPUTH_OVER;
	if (((t->set.sp_setting.oh + t->set.sp_setting.o_bottom_pos) > oh)
		|| (t->set.sp_setting.oh > soc_max_out_height()))
		return IPU_CHECK_ERR_SPLIT_OUTPUTH_OVER;
	if (rounddown(t->set.sp_setting.oh, 8) * 8 <=
	    rounddown(t->set.sp_setting.ih, 8))
		return IPU_CHECK_ERR_H_DOWNSIZE_OVER;

	return IPU_CHECK_OK;
}

static int check_task(struct ipu_task_entry *t)
{
	int tmp;
	int ret = IPU_CHECK_OK;
	int timeout;
	bool vdi_split = false;
	int ocw, och;

	if ((IPU_PIX_FMT_TILED_NV12 == t->overlay.format) ||
		(IPU_PIX_FMT_TILED_NV12F == t->overlay.format) ||
		(IPU_PIX_FMT_TILED_NV12 == t->output.format) ||
		(IPU_PIX_FMT_TILED_NV12F == t->output.format) ||
		((IPU_PIX_FMT_TILED_NV12F == t->input.format) &&
			!t->input.deinterlace.enable)) {
		ret = IPU_CHECK_ERR_NOT_SUPPORT;
		goto done;
	}

	/* check input */
	ret = set_crop(&t->input.crop, t->input.width, t->input.height,
		t->input.format);
	if (ret < 0) {
		ret = IPU_CHECK_ERR_INPUT_CROP;
		goto done;
	} else
		update_offset(t->input.format, t->input.width, t->input.height,
				t->input.crop.pos.x, t->input.crop.pos.y,
				&t->set.i_off, &t->set.i_uoff,
				&t->set.i_voff, &t->set.istride);

	/* check output */
	ret = set_crop(&t->output.crop, t->output.width, t->output.height,
		t->output.format);
	if (ret < 0) {
		ret = IPU_CHECK_ERR_OUTPUT_CROP;
		goto done;
	} else
		update_offset(t->output.format,
				t->output.width, t->output.height,
				t->output.crop.pos.x, t->output.crop.pos.y,
				&t->set.o_off, &t->set.o_uoff,
				&t->set.o_voff, &t->set.ostride);

	if (t->output.rotate >= IPU_ROTATE_90_RIGHT) {
		/*
		 * Cache output width and height and
		 * swap them so that we may check
		 * downsize overflow correctly.
		 */
		ocw = t->output.crop.h;
		och = t->output.crop.w;
	} else {
		ocw = t->output.crop.w;
		och = t->output.crop.h;
	}

	if (ocw * 8 <= t->input.crop.w) {
		ret = IPU_CHECK_ERR_W_DOWNSIZE_OVER;
		goto done;
	}

	if (och * 8 <= t->input.crop.h) {
		ret = IPU_CHECK_ERR_H_DOWNSIZE_OVER;
		goto done;
	}

	if ((IPU_PIX_FMT_TILED_NV12 == t->input.format) ||
		(IPU_PIX_FMT_TILED_NV12F == t->input.format)) {
		if ((t->input.crop.w > soc_max_in_width(1)) ||
			(t->input.crop.h > soc_max_in_height())) {
			ret = IPU_CHECK_ERR_INPUT_OVER_LIMIT;
			goto done;
		}
		/* output fmt: NV12 and YUYV, now don't support resize */
		if (((IPU_PIX_FMT_NV12 != t->output.format) &&
				(IPU_PIX_FMT_YUYV != t->output.format)) ||
			(t->input.crop.w != t->output.crop.w) ||
			(t->input.crop.h != t->output.crop.h)) {
			ret = IPU_CHECK_ERR_NOT_SUPPORT;
			goto done;
		}
	}

	/* check overlay if there is */
	if (t->overlay_en) {
		if (t->input.deinterlace.enable) {
			ret = IPU_CHECK_ERR_OVERLAY_WITH_VDI;
			goto done;
		}

		ret = set_crop(&t->overlay.crop, t->overlay.width,
			t->overlay.height, t->overlay.format);
		if (ret < 0) {
			ret = IPU_CHECK_ERR_OVERLAY_CROP;
			goto done;
		} else {
			ocw = t->output.crop.w;
			och = t->output.crop.h;

			if (t->output.rotate >= IPU_ROTATE_90_RIGHT) {
				ocw = t->output.crop.h;
				och = t->output.crop.w;
			}
			if ((t->overlay.crop.w != ocw) ||
			    (t->overlay.crop.h != och)) {
				ret = IPU_CHECK_ERR_OV_OUT_NO_FIT;
				goto done;
			}

			update_offset(t->overlay.format,
					t->overlay.width, t->overlay.height,
					t->overlay.crop.pos.x, t->overlay.crop.pos.y,
					&t->set.ov_off, &t->set.ov_uoff,
					&t->set.ov_voff, &t->set.ovstride);
			if (t->overlay.alpha.mode == IPU_ALPHA_MODE_LOCAL) {
				t->set.ov_alpha_stride = t->overlay.width;
				t->set.ov_alpha_off = t->overlay.crop.pos.y *
					t->overlay.width + t->overlay.crop.pos.x;
			}
		}
	}

	/* input overflow? */
	if (!((IPU_PIX_FMT_TILED_NV12 == t->input.format) ||
		(IPU_PIX_FMT_TILED_NV12F == t->input.format))) {
		if ((t->input.crop.w > soc_max_in_width(0)) ||
			(t->input.crop.h > soc_max_in_height())) {
				ret = IPU_CHECK_ERR_INPUT_OVER_LIMIT;
				goto done;
		}
	}

	/* check task mode */
	t->set.mode = NULL_MODE;
	t->set.split_mode = NO_SPLIT;

	if (t->output.rotate >= IPU_ROTATE_90_RIGHT) {
		/*output swap*/
		tmp = t->output.crop.w;
		t->output.crop.w = t->output.crop.h;
		t->output.crop.h = tmp;
	}

	if (t->output.rotate >= IPU_ROTATE_90_RIGHT)
		t->set.mode |= ROT_MODE;

	/*need resize or CSC?*/
	if ((t->input.crop.w != t->output.crop.w) ||
			(t->input.crop.h != t->output.crop.h) ||
			need_csc(t->input.format, t->output.format))
		t->set.mode |= IC_MODE;

	/*need cropping?*/
	if ((t->input.crop.w != t->input.width)       ||
		(t->input.crop.h != t->input.height)  ||
		(t->output.crop.w != t->output.width) ||
		(t->output.crop.h != t->output.height))
		t->set.mode |= IC_MODE;

	/*need flip?*/
	if ((t->set.mode == NULL_MODE) && (t->output.rotate > IPU_ROTATE_NONE))
		t->set.mode |= IC_MODE;

	/*need IDMAC do format(same color space)?*/
	if ((t->set.mode == NULL_MODE) && (t->input.format != t->output.format))
		t->set.mode |= IC_MODE;

	/*overlay support*/
	if (t->overlay_en)
		t->set.mode |= IC_MODE;

	/*deinterlace*/
	if (t->input.deinterlace.enable) {
		t->set.mode &= ~IC_MODE;
		t->set.mode |= VDI_MODE;
	}
	if ((IPU_PIX_FMT_TILED_NV12 == t->input.format) ||
		(IPU_PIX_FMT_TILED_NV12F == t->input.format)) {
		if (t->set.mode & ROT_MODE) {
			ret = IPU_CHECK_ERR_NOT_SUPPORT;
			goto done;
		}
		t->set.mode |= VDOA_MODE;
		if (IPU_PIX_FMT_TILED_NV12F == t->input.format)
			t->set.mode |= VDOA_BAND_MODE;
		t->set.mode &= ~IC_MODE;
	}

	if ((t->set.mode & (IC_MODE | VDI_MODE)) &&
		(IPU_PIX_FMT_TILED_NV12F != t->input.format)) {
		if (t->output.crop.w > soc_max_out_width())
			t->set.split_mode |= RL_SPLIT;
		if (t->output.crop.h > soc_max_out_height())
			t->set.split_mode |= UD_SPLIT;
		if (!t->set.split_mode && (t->set.mode & VDI_MODE) &&
				(t->input.crop.w >
				 soc_max_vdi_in_width(t->ipu))) {
			t->set.split_mode |= RL_SPLIT;
			vdi_split = true;
		}
		if (t->set.split_mode) {
			if ((t->set.split_mode == RL_SPLIT) ||
				 (t->set.split_mode == UD_SPLIT))
				timeout = DEF_TIMEOUT_MS * 2 + DEF_DELAY_MS;
			else
				timeout = DEF_TIMEOUT_MS * 4 + DEF_DELAY_MS;
			if (t->timeout < timeout)
				t->timeout = timeout;

			ret = update_split_setting(t, vdi_split);
			if (ret > IPU_CHECK_ERR_MIN)
				goto done;
		}
	}

	if (t->output.rotate >= IPU_ROTATE_90_RIGHT) {
		/*output swap*/
		tmp = t->output.crop.w;
		t->output.crop.w = t->output.crop.h;
		t->output.crop.h = tmp;
	}

	if (t->set.mode == NULL_MODE) {
		ret = IPU_CHECK_ERR_PROC_NO_NEED;
		goto done;
	}

	if ((t->set.i_uoff % 8) || (t->set.i_voff % 8))
		ret |= IPU_CHECK_WARN_INPUT_OFFS_NOT8ALIGN;
	if ((t->set.o_uoff % 8) || (t->set.o_voff % 8))
		ret |= IPU_CHECK_WARN_OUTPUT_OFFS_NOT8ALIGN;
	if (t->overlay_en && ((t->set.ov_uoff % 8) || (t->set.ov_voff % 8)))
		ret |= IPU_CHECK_WARN_OVERLAY_OFFS_NOT8ALIGN;

done:
	/* dump msg */
	if (debug) {
		if (ret > IPU_CHECK_ERR_MIN)
			dump_check_err(t->dev, ret);
		else if (ret != IPU_CHECK_OK)
			dump_check_warn(t->dev, ret);
	}

	return ret;
}

static int prepare_task(struct ipu_task_entry *t)
{
	int ret = 0;

	ret = check_task(t);
	if (ret > IPU_CHECK_ERR_MIN)
		return -EINVAL;

	if (t->set.mode & VDI_MODE) {
		t->task_id = IPU_TASK_ID_VF;
		t->set.task = VDI_VF;
		if (t->set.mode & ROT_MODE)
			t->set.task |= ROT_VF;
	}

	if (VDOA_MODE == t->set.mode) {
		if (t->set.task != 0) {
			dev_err(t->dev, "ERR: vdoa only task:0x%x, [0x%p].\n",
					t->set.task, t);
			return -EINVAL;
		}
		t->set.task |= VDOA_ONLY;
	}

	if (VDOA_BAND_MODE & t->set.mode) {
		/* to save band size: 1<<3 = 8 lines */
		t->set.band_lines = 3;
	}

	dump_task_info(t);

	return ret;
}

static uint32_t ic_vf_pp_is_busy(struct ipu_soc *ipu, bool is_vf)
{
	uint32_t	status;
	uint32_t	status_vf;
	uint32_t	status_rot;

	if (is_vf) {
		status = ipu_channel_status(ipu, MEM_VDI_PRP_VF_MEM);
		status_vf = ipu_channel_status(ipu, MEM_PRP_VF_MEM);
		status_rot = ipu_channel_status(ipu, MEM_ROT_VF_MEM);
		return status || status_vf || status_rot;
	} else {
		status = ipu_channel_status(ipu, MEM_PP_MEM);
		status_rot = ipu_channel_status(ipu, MEM_ROT_PP_MEM);
		return status || status_rot;
	}
}

static int _get_vdoa_ipu_res(struct ipu_task_entry *t)
{
	int		i;
	struct ipu_soc	*ipu;
	u8		*used;
	uint32_t	found_ipu = 0;
	uint32_t	found_vdoa = 0;
	struct ipu_channel_tabel	*tbl = &ipu_ch_tbl;

	mutex_lock(&tbl->lock);
	if (t->set.mode & VDOA_MODE) {
		if (NULL != t->vdoa_handle)
			found_vdoa = 1;
		else {
			found_vdoa = tbl->vdoa_used ? 0 : 1;
			if (found_vdoa) {
				tbl->vdoa_used = 1;
				vdoa_get_handle(&t->vdoa_handle);
			} else
				/* first get vdoa->ipu resource sequence */
				goto out;
			if (t->set.task & VDOA_ONLY)
				goto out;
		}
	}

	for (i = 0; i < max_ipu_no; i++) {
		ipu = ipu_get_soc(i);
		if (IS_ERR(ipu))
			dev_err(t->dev, "no:0x%x,found_vdoa:%d, ipu:%d\n",
				 t->task_no, found_vdoa, i);

		used = &tbl->used[i][IPU_PP_CH_VF];
		if (t->set.mode & VDI_MODE) {
			if (0 == *used) {
				*used = 1;
				found_ipu = 1;
				break;
			}
		} else if ((t->set.mode & IC_MODE) || only_rot(t->set.mode)) {
			if (0 == *used) {
				t->task_id = IPU_TASK_ID_VF;
				if (t->set.mode & IC_MODE)
					t->set.task |= IC_VF;
				if (t->set.mode & ROT_MODE)
					t->set.task |= ROT_VF;
				*used = 1;
				found_ipu = 1;
				break;
			}
		} else
			dev_err(t->dev, "no:0x%x,found_vdoa:%d, mode:0x%x\n",
				 t->task_no, found_vdoa, t->set.mode);
	}
	if (found_ipu)
		goto next;

	for (i = 0; i < max_ipu_no; i++) {
		ipu = ipu_get_soc(i);
		if (IS_ERR(ipu))
			dev_err(t->dev, "no:0x%x,found_vdoa:%d, ipu:%d\n",
				 t->task_no, found_vdoa, i);

		if ((t->set.mode & IC_MODE) || only_rot(t->set.mode)) {
			used = &tbl->used[i][IPU_PP_CH_PP];
			if (0 == *used) {
				t->task_id = IPU_TASK_ID_PP;
				if (t->set.mode & IC_MODE)
					t->set.task |= IC_PP;
				if (t->set.mode & ROT_MODE)
					t->set.task |= ROT_PP;
				*used = 1;
				found_ipu = 1;
				break;
			}
		}
	}

next:
	if (found_ipu) {
		t->ipu = ipu;
		t->ipu_id = i;
		t->dev = ipu->dev;
		if (atomic_inc_return(&t->res_get) == 2)
			dev_err(t->dev,
				"ERR no:0x%x,found_vdoa:%d,get ipu twice\n",
				 t->task_no, found_vdoa);
	}
out:
	dev_dbg(t->dev,
		"%s:no:0x%x,found_vdoa:%d, found_ipu:%d\n",
		 __func__, t->task_no, found_vdoa, found_ipu);
	mutex_unlock(&tbl->lock);
	if (t->set.task & VDOA_ONLY)
		return found_vdoa;
	else if (t->set.mode & VDOA_MODE)
		return found_vdoa && found_ipu;
	else
		return found_ipu;
}

static void put_vdoa_ipu_res(struct ipu_task_entry *tsk, int vdoa_only)
{
	int ret;
	int rel_vdoa = 0, rel_ipu = 0;
	struct ipu_channel_tabel	*tbl = &ipu_ch_tbl;

	mutex_lock(&tbl->lock);
	if (tsk->set.mode & VDOA_MODE) {
		if (!tbl->vdoa_used && tsk->vdoa_handle)
			dev_err(tsk->dev,
				"ERR no:0x%x,vdoa not used,mode:0x%x\n",
				 tsk->task_no, tsk->set.mode);
		if (tbl->vdoa_used && tsk->vdoa_handle) {
			tbl->vdoa_used = 0;
			vdoa_put_handle(&tsk->vdoa_handle);
			if (tsk->ipu)
				tsk->ipu->vdoa_en = 0;
			rel_vdoa = 1;
			if (vdoa_only || (tsk->set.task & VDOA_ONLY))
				goto out;
		}
	}

	tbl->used[tsk->ipu_id][tsk->task_id - 1] = 0;
	rel_ipu = 1;
	ret = atomic_inc_return(&tsk->res_free);
	if (ret == 2)
		dev_err(tsk->dev,
			"ERR no:0x%x,rel_vdoa:%d,put ipu twice\n",
			 tsk->task_no, rel_vdoa);
out:
	dev_dbg(tsk->dev,
		"%s:no:0x%x,rel_vdoa:%d, rel_ipu:%d\n",
		 __func__, tsk->task_no, rel_vdoa, rel_ipu);
	mutex_unlock(&tbl->lock);
}

static int get_vdoa_ipu_res(struct ipu_task_entry *t)
{
	int		ret;
	uint32_t	found = 0;

	found = _get_vdoa_ipu_res(t);
	if (!found) {
		t->ipu_id = -1;
		t->ipu = NULL;
		/* blocking to get resource */
		ret = atomic_inc_return(&req_cnt);
		dev_dbg(t->dev,
			"wait_res:no:0x%x,req_cnt:%d\n", t->task_no, ret);
		ret = wait_event_timeout(res_waitq, _get_vdoa_ipu_res(t),
				 msecs_to_jiffies(t->timeout - DEF_DELAY_MS));
		if (ret == 0) {
			dev_err(t->dev, "ERR[0x%p,no-0x%x] wait_res timeout:%dms!\n",
					 t, t->task_no, t->timeout - DEF_DELAY_MS);
			ret = -ETIMEDOUT;
			t->state = STATE_RES_TIMEOUT;
			goto out;
		} else {
			if (!(t->set.task & VDOA_ONLY) && (!t->ipu))
				dev_err(t->dev,
					"ERR[no-0x%x] can not get ipu!\n",
					t->task_no);
			ret = atomic_read(&req_cnt);
			if (ret > 0)
				ret = atomic_dec_return(&req_cnt);
			else
				dev_err(t->dev,
					"ERR[no-0x%x] req_cnt:%d mismatch!\n",
					t->task_no, ret);
			dev_dbg(t->dev, "no-0x%x,[0x%p],req_cnt:%d, got_res!\n",
						t->task_no, t, ret);
			found = 1;
		}
	}

out:
	return found;
}

static struct ipu_task_entry *create_task_entry(struct ipu_task *task)
{
	struct ipu_task_entry *tsk;

	tsk = kzalloc(sizeof(struct ipu_task_entry), GFP_KERNEL);
	if (!tsk)
		return ERR_PTR(-ENOMEM);
	kref_init(&tsk->refcount);
	tsk->state = -EINVAL;
	tsk->ipu_id = -1;
	tsk->dev = ipu_dev;
	tsk->input = task->input;
	tsk->output = task->output;
	tsk->overlay_en = task->overlay_en;
	if (tsk->overlay_en)
		tsk->overlay = task->overlay;
	if (task->timeout > DEF_TIMEOUT_MS)
		tsk->timeout = task->timeout;
	else
		tsk->timeout = DEF_TIMEOUT_MS;

	return tsk;
}

static void task_mem_free(struct kref *ref)
{
	struct ipu_task_entry *tsk =
			container_of(ref, struct ipu_task_entry, refcount);
	kfree(tsk);
}

int create_split_child_task(struct ipu_split_task *sp_task)
{
	int ret = 0;
	struct ipu_task_entry *tsk;

	tsk = create_task_entry(&sp_task->task);
	if (IS_ERR(tsk))
		return PTR_ERR(tsk);

	sp_task->child_task = tsk;
	tsk->task_no = sp_task->task_no;

	ret = prepare_task(tsk);
	if (ret < 0)
		goto err;

	tsk->parent = sp_task->parent_task;
	tsk->set.sp_setting = sp_task->parent_task->set.sp_setting;

	list_add(&tsk->node, &tsk->parent->split_list);
	dev_dbg(tsk->dev, "[0x%p] sp_tsk Q list,no-0x%x\n", tsk, tsk->task_no);
	tsk->state = STATE_QUEUE;
	CHECK_PERF(&tsk->ts_queue);
err:
	return ret;
}

static inline int sp_task_check_done(struct ipu_split_task *sp_task,
			struct ipu_task_entry *parent, int num, int *idx)
{
	int i;
	int ret = 0;
	struct ipu_task_entry *tsk;
	struct mutex *lock = &parent->split_lock;

	*idx = -EINVAL;
	mutex_lock(lock);
	for (i = 0; i < num; i++) {
		tsk = sp_task[i].child_task;
		if (tsk && tsk->split_done) {
			*idx = i;
			ret = 1;
			goto out;
		}
	}

out:
	mutex_unlock(lock);
	return ret;
}

static int create_split_task(
		int stripe,
		struct ipu_split_task *sp_task)
{
	struct ipu_task *task = &(sp_task->task);
	struct ipu_task_entry *t = sp_task->parent_task;
	int ret;

	sp_task->task_no |= stripe;

	task->input = t->input;
	task->output = t->output;
	task->overlay_en = t->overlay_en;
	if (task->overlay_en)
		task->overlay = t->overlay;
	task->task_id = t->task_id;
	if ((t->set.split_mode == RL_SPLIT) ||
		 (t->set.split_mode == UD_SPLIT))
		task->timeout = t->timeout / 2;
	else
		task->timeout = t->timeout / 4;

	task->input.crop.w = t->set.sp_setting.iw;
	task->input.crop.h = t->set.sp_setting.ih;
	if (task->overlay_en) {
		task->overlay.crop.w = t->set.sp_setting.ow;
		task->overlay.crop.h = t->set.sp_setting.oh;
	}
	if (t->output.rotate >= IPU_ROTATE_90_RIGHT) {
		task->output.crop.w = t->set.sp_setting.oh;
		task->output.crop.h = t->set.sp_setting.ow;
		t->set.sp_setting.rl_split_line = t->set.sp_setting.o_bottom_pos;
		t->set.sp_setting.ud_split_line = t->set.sp_setting.o_right_pos;

	} else {
		task->output.crop.w = t->set.sp_setting.ow;
		task->output.crop.h = t->set.sp_setting.oh;
		t->set.sp_setting.rl_split_line = t->set.sp_setting.o_right_pos;
		t->set.sp_setting.ud_split_line = t->set.sp_setting.o_bottom_pos;
	}

	if (stripe & LEFT_STRIPE)
		task->input.crop.pos.x += t->set.sp_setting.i_left_pos;
	else if (stripe & RIGHT_STRIPE)
		task->input.crop.pos.x += t->set.sp_setting.i_right_pos;
	if (stripe & UP_STRIPE)
		task->input.crop.pos.y += t->set.sp_setting.i_top_pos;
	else if (stripe & DOWN_STRIPE)
		task->input.crop.pos.y += t->set.sp_setting.i_bottom_pos;

	if (task->overlay_en) {
		if (stripe & LEFT_STRIPE)
			task->overlay.crop.pos.x += t->set.sp_setting.o_left_pos;
		else if (stripe & RIGHT_STRIPE)
			task->overlay.crop.pos.x += t->set.sp_setting.o_right_pos;
		if (stripe & UP_STRIPE)
			task->overlay.crop.pos.y += t->set.sp_setting.o_top_pos;
		else if (stripe & DOWN_STRIPE)
			task->overlay.crop.pos.y += t->set.sp_setting.o_bottom_pos;
	}

	switch (t->output.rotate) {
	case IPU_ROTATE_NONE:
		if (stripe & LEFT_STRIPE)
			task->output.crop.pos.x += t->set.sp_setting.o_left_pos;
		else if (stripe & RIGHT_STRIPE)
			task->output.crop.pos.x += t->set.sp_setting.o_right_pos;
		if (stripe & UP_STRIPE)
			task->output.crop.pos.y += t->set.sp_setting.o_top_pos;
		else if (stripe & DOWN_STRIPE)
			task->output.crop.pos.y += t->set.sp_setting.o_bottom_pos;
		break;
	case IPU_ROTATE_VERT_FLIP:
		if (stripe & LEFT_STRIPE)
			task->output.crop.pos.x += t->set.sp_setting.o_left_pos;
		else if (stripe & RIGHT_STRIPE)
			task->output.crop.pos.x += t->set.sp_setting.o_right_pos;
		if (stripe & UP_STRIPE)
			task->output.crop.pos.y =
					t->output.crop.pos.y + t->output.crop.h
					- t->set.sp_setting.o_top_pos - t->set.sp_setting.oh;
		else if (stripe & DOWN_STRIPE)
			task->output.crop.pos.y =
					t->output.crop.pos.y + t->output.crop.h
					- t->set.sp_setting.o_bottom_pos - t->set.sp_setting.oh;
		break;
	case IPU_ROTATE_HORIZ_FLIP:
		if (stripe & LEFT_STRIPE)
			task->output.crop.pos.x =
					t->output.crop.pos.x + t->output.crop.w
					- t->set.sp_setting.o_left_pos - t->set.sp_setting.ow;
		else if (stripe & RIGHT_STRIPE)
			task->output.crop.pos.x =
					t->output.crop.pos.x + t->output.crop.w
					- t->set.sp_setting.o_right_pos - t->set.sp_setting.ow;
		if (stripe & UP_STRIPE)
			task->output.crop.pos.y += t->set.sp_setting.o_top_pos;
		else if (stripe & DOWN_STRIPE)
			task->output.crop.pos.y += t->set.sp_setting.o_bottom_pos;
		break;
	case IPU_ROTATE_180:
		if (stripe & LEFT_STRIPE)
			task->output.crop.pos.x =
					t->output.crop.pos.x + t->output.crop.w
					- t->set.sp_setting.o_left_pos - t->set.sp_setting.ow;
		else if (stripe & RIGHT_STRIPE)
			task->output.crop.pos.x =
					t->output.crop.pos.x + t->output.crop.w
					- t->set.sp_setting.o_right_pos - t->set.sp_setting.ow;
		if (stripe & UP_STRIPE)
			task->output.crop.pos.y =
					t->output.crop.pos.y + t->output.crop.h
					- t->set.sp_setting.o_top_pos - t->set.sp_setting.oh;
		else if (stripe & DOWN_STRIPE)
			task->output.crop.pos.y =
					t->output.crop.pos.y + t->output.crop.h
					- t->set.sp_setting.o_bottom_pos - t->set.sp_setting.oh;
		break;
	case IPU_ROTATE_90_RIGHT:
		if (stripe & UP_STRIPE)
			task->output.crop.pos.x =
					t->output.crop.pos.x + t->output.crop.w
					- t->set.sp_setting.o_top_pos - t->set.sp_setting.oh;
		else if (stripe & DOWN_STRIPE)
			task->output.crop.pos.x =
					t->output.crop.pos.x + t->output.crop.w
					- t->set.sp_setting.o_bottom_pos - t->set.sp_setting.oh;
		if (stripe & LEFT_STRIPE)
			task->output.crop.pos.y += t->set.sp_setting.o_left_pos;
		else if (stripe & RIGHT_STRIPE)
			task->output.crop.pos.y += t->set.sp_setting.o_right_pos;
		break;
	case IPU_ROTATE_90_RIGHT_HFLIP:
		if (stripe & UP_STRIPE)
			task->output.crop.pos.x += t->set.sp_setting.o_top_pos;
		else if (stripe & DOWN_STRIPE)
			task->output.crop.pos.x += t->set.sp_setting.o_bottom_pos;
		if (stripe & LEFT_STRIPE)
			task->output.crop.pos.y += t->set.sp_setting.o_left_pos;
		else if (stripe & RIGHT_STRIPE)
			task->output.crop.pos.y += t->set.sp_setting.o_right_pos;
		break;
	case IPU_ROTATE_90_RIGHT_VFLIP:
		if (stripe & UP_STRIPE)
			task->output.crop.pos.x =
					t->output.crop.pos.x + t->output.crop.w
					- t->set.sp_setting.o_top_pos - t->set.sp_setting.oh;
		else if (stripe & DOWN_STRIPE)
			task->output.crop.pos.x =
					t->output.crop.pos.x + t->output.crop.w
					- t->set.sp_setting.o_bottom_pos - t->set.sp_setting.oh;
		if (stripe & LEFT_STRIPE)
			task->output.crop.pos.y =
					t->output.crop.pos.y + t->output.crop.h
					- t->set.sp_setting.o_left_pos - t->set.sp_setting.ow;
		else if (stripe & RIGHT_STRIPE)
			task->output.crop.pos.y =
					t->output.crop.pos.y + t->output.crop.h
					- t->set.sp_setting.o_right_pos - t->set.sp_setting.ow;
		break;
	case IPU_ROTATE_90_LEFT:
		if (stripe & UP_STRIPE)
			task->output.crop.pos.x += t->set.sp_setting.o_top_pos;
		else if (stripe & DOWN_STRIPE)
			task->output.crop.pos.x += t->set.sp_setting.o_bottom_pos;
		if (stripe & LEFT_STRIPE)
			task->output.crop.pos.y =
					t->output.crop.pos.y + t->output.crop.h
					- t->set.sp_setting.o_left_pos - t->set.sp_setting.ow;
		else if (stripe & RIGHT_STRIPE)
			task->output.crop.pos.y =
					t->output.crop.pos.y + t->output.crop.h
					- t->set.sp_setting.o_right_pos - t->set.sp_setting.ow;
		break;
	default:
		dev_err(t->dev, "ERR:should not be here\n");
		break;
	}

	ret = create_split_child_task(sp_task);
	if (ret < 0)
		dev_err(t->dev, "ERR:create_split_child_task() ret:%d\n", ret);
	return ret;
}

static int queue_split_task(struct ipu_task_entry *t,
				struct ipu_split_task *sp_task, uint32_t size)
{
	int err[4];
	int ret = 0;
	int i, j;
	struct ipu_task_entry *tsk = NULL;
	struct mutex *lock = &t->split_lock;
	struct mutex *vdic_lock = &t->vdic_lock;

	dev_dbg(t->dev, "Split task 0x%p, no-0x%x, size:%d\n",
			 t, t->task_no, size);
	mutex_init(lock);
	mutex_init(vdic_lock);
	init_waitqueue_head(&t->split_waitq);
	INIT_LIST_HEAD(&t->split_list);
	for (j = 0; j < size; j++) {
		memset(&sp_task[j], 0, sizeof(*sp_task));
		sp_task[j].parent_task = t;
		sp_task[j].task_no = t->task_no;
	}

	if (t->set.split_mode == RL_SPLIT) {
		i = 0;
		err[i] = create_split_task(RIGHT_STRIPE, &sp_task[i]);
		if (err[i] < 0)
			goto err_start;
		i = 1;
		err[i] = create_split_task(LEFT_STRIPE, &sp_task[i]);
	} else if (t->set.split_mode == UD_SPLIT) {
		i = 0;
		err[i] = create_split_task(DOWN_STRIPE, &sp_task[i]);
		if (err[i] < 0)
			goto err_start;
		i = 1;
		err[i] = create_split_task(UP_STRIPE, &sp_task[i]);
	} else {
		i = 0;
		err[i] = create_split_task(RIGHT_STRIPE | DOWN_STRIPE, &sp_task[i]);
		if (err[i] < 0)
			goto err_start;
		i = 1;
		err[i] = create_split_task(LEFT_STRIPE | DOWN_STRIPE, &sp_task[i]);
		if (err[i] < 0)
			goto err_start;
		i = 2;
		err[i] = create_split_task(RIGHT_STRIPE | UP_STRIPE, &sp_task[i]);
		if (err[i] < 0)
			goto err_start;
		i = 3;
		err[i] = create_split_task(LEFT_STRIPE | UP_STRIPE, &sp_task[i]);
	}

err_start:
	for (j = 0; j < (i + 1); j++) {
		if (err[j] < 0) {
			if (sp_task[j].child_task)
				dev_err(t->dev,
				 "sp_task[%d],no-0x%x fail state:%d, queue err:%d.\n",
				j, sp_task[j].child_task->task_no,
				sp_task[j].child_task->state, err[j]);
			goto err_exit;
		}
		dev_dbg(t->dev, "[0x%p] sp_task[%d], no-0x%x state:%s, queue ret:%d.\n",
			sp_task[j].child_task, j, sp_task[j].child_task->task_no,
			state_msg[sp_task[j].child_task->state].msg, err[j]);
	}

	return ret;

err_exit:
	for (j = 0; j < (i + 1); j++) {
		if (err[j] < 0 && !ret)
			ret = err[j];
		tsk = sp_task[j].child_task;
		if (!tsk)
			continue;
		kfree(tsk);
	}
	t->state = STATE_ERR;
	return ret;

}

static int init_tiled_buf(struct ipu_soc *ipu, struct ipu_task_entry *t,
				ipu_channel_t channel, uint32_t ch_type)
{
	int ret = 0;
	int i;
	uint32_t ipu_fmt;
	dma_addr_t inbuf_base = 0;
	u32 field_size;
	struct vdoa_params param;
	struct vdoa_ipu_buf buf;
	struct ipu_soc *ipu_idx;
	u32 ipu_stride, obuf_size;
	u32 height, width;
	ipu_buffer_t type;

	if ((IPU_PIX_FMT_YUYV != t->output.format) &&
		(IPU_PIX_FMT_NV12 != t->output.format)) {
		dev_err(t->dev, "ERR:[0x%d] output format\n", t->task_no);
		return -EINVAL;
	}

	memset(&param, 0, sizeof(param));
	/* init channel tiled bufs */
	if (deinterlace_3_field(t) &&
		(IPU_PIX_FMT_TILED_NV12F == t->input.format)) {
		field_size = tiled_filed_size(t);
		if (INPUT_CHAN_VDI_P == ch_type) {
			inbuf_base = t->input.paddr + field_size;
			param.vfield_buf.prev_veba = inbuf_base + t->set.i_off;
		} else if (INPUT_CHAN == ch_type) {
			inbuf_base = t->input.paddr_n;
			param.vfield_buf.cur_veba = inbuf_base + t->set.i_off;
		} else if (INPUT_CHAN_VDI_N == ch_type) {
			inbuf_base = t->input.paddr_n + field_size;
			param.vfield_buf.next_veba = inbuf_base + t->set.i_off;
		} else
			return -EINVAL;
		height = t->input.crop.h >> 1; /* field format for vdoa */
		width = t->input.crop.w;
		param.vfield_buf.vubo = t->set.i_uoff;
		param.interlaced = 1;
		param.scan_order = 1;
		type = IPU_INPUT_BUFFER;
	} else if ((IPU_PIX_FMT_TILED_NV12 == t->input.format) &&
			(INPUT_CHAN == ch_type)) {
		height = t->input.crop.h;
		width = t->input.crop.w;
		param.vframe_buf.veba = t->input.paddr + t->set.i_off;
		param.vframe_buf.vubo = t->set.i_uoff;
		type = IPU_INPUT_BUFFER;
	} else
		return -EINVAL;

	param.band_mode = (t->set.mode & VDOA_BAND_MODE) ? 1 : 0;
	if (param.band_mode && (t->set.band_lines != 3) &&
		 (t->set.band_lines != 4) && (t->set.band_lines != 5))
		return -EINVAL;
	else if (param.band_mode)
		param.band_lines = (1 << t->set.band_lines);
	for (i = 0; i < max_ipu_no; i++) {
		ipu_idx = ipu_get_soc(i);
		if (!IS_ERR(ipu_idx) && ipu_idx == ipu)
			break;
	}
	if (t->set.task & VDOA_ONLY)
		/* dummy, didn't need ipu res */
		i = 0;
	if (max_ipu_no == i) {
		dev_err(t->dev, "ERR:[0x%p] get ipu num\n", t);
		return -EINVAL;
	}

	param.ipu_num = i;
	param.vpu_stride = t->input.width;
	param.height = height;
	param.width = width;
	if (IPU_PIX_FMT_NV12 == t->output.format)
		param.pfs = VDOA_PFS_NV12;
	else
		param.pfs = VDOA_PFS_YUYV;
	ipu_fmt = (param.pfs == VDOA_PFS_YUYV) ? IPU_PIX_FMT_YUYV :
				IPU_PIX_FMT_NV12;
	ipu_stride = param.width * bytes_per_pixel(ipu_fmt);
	obuf_size = PAGE_ALIGN(param.width * param.height *
				fmt_to_bpp(ipu_fmt)/8);
	dev_dbg(t->dev, "band_mode:%d, band_lines:%d\n",
			param.band_mode, param.band_lines);
	if (!param.band_mode) {
		/* note: if only for tiled -> raster convert and
		   no other post-processing, we don't need alloc buf
		   and use output buffer directly.
		*/
		if (t->set.task & VDOA_ONLY)
			param.ieba0 = t->output.paddr;
		else {
			dev_err(t->dev, "ERR:[0x%d] vdoa task\n", t->task_no);
			return -EINVAL;
		}
	} else {
		if (IPU_PIX_FMT_TILED_NV12F != t->input.format) {
			dev_err(t->dev, "ERR [0x%d] vdoa task\n", t->task_no);
			return -EINVAL;
		}
	}
	ret = vdoa_setup(t->vdoa_handle, &param);
	if (ret)
		goto done;
	vdoa_get_output_buf(t->vdoa_handle, &buf);
	if (t->set.task & VDOA_ONLY)
		goto done;

	ret = ipu_init_channel_buffer(ipu,
			channel,
			type,
			ipu_fmt,
			width,
			height,
			ipu_stride,
			IPU_ROTATE_NONE,
			buf.ieba0,
			buf.ieba1,
			0,
			buf.iubo,
			0);
	if (ret < 0) {
		t->state = STATE_INIT_CHAN_BUF_FAIL;
		goto done;
	}

	if (param.band_mode) {
		ret = ipu_set_channel_bandmode(ipu, channel,
				type, t->set.band_lines);
		if (ret < 0) {
			t->state = STATE_INIT_CHAN_BAND_FAIL;
			goto done;
		}
	}
done:
	return ret;
}

static int init_tiled_ch_bufs(struct ipu_soc *ipu, struct ipu_task_entry *t)
{
	int ret = 0;

	if (IPU_PIX_FMT_TILED_NV12 == t->input.format) {
		ret = init_tiled_buf(ipu, t, t->set.ic_chan, INPUT_CHAN);
		CHECK_RETCODE(ret < 0, "init tiled_ch", t->state, done, ret);
	} else if (IPU_PIX_FMT_TILED_NV12F == t->input.format) {
		ret = init_tiled_buf(ipu, t, t->set.ic_chan, INPUT_CHAN);
		CHECK_RETCODE(ret < 0, "init tiled_ch-c", t->state, done, ret);
		ret = init_tiled_buf(ipu, t, t->set.vdi_ic_p_chan,
					INPUT_CHAN_VDI_P);
		CHECK_RETCODE(ret < 0, "init tiled_ch-p", t->state, done, ret);
		ret = init_tiled_buf(ipu, t, t->set.vdi_ic_n_chan,
					INPUT_CHAN_VDI_N);
		CHECK_RETCODE(ret < 0, "init tiled_ch-n", t->state, done, ret);
	} else {
		ret = -EINVAL;
		dev_err(t->dev, "ERR[no-0x%x] invalid fmt:0x%x!\n",
			t->task_no, t->input.format);
	}

done:
	return ret;
}

static int init_ic(struct ipu_soc *ipu, struct ipu_task_entry *t)
{
	int ret = 0;
	ipu_channel_params_t params;
	dma_addr_t inbuf = 0, ovbuf = 0, ov_alp_buf = 0;
	dma_addr_t inbuf_p = 0, inbuf_n = 0;
	dma_addr_t outbuf = 0;
	int out_uoff = 0, out_voff = 0, out_rot;
	int out_w = 0, out_h = 0, out_stride;
	int out_fmt;
	u32 vdi_frame_idx = 0;

	memset(&params, 0, sizeof(params));

	/* is it need link a rot channel */
	if (ic_and_rot(t->set.mode)) {
		outbuf = t->set.r_paddr;
		out_w = t->set.r_width;
		out_h = t->set.r_height;
		out_stride = t->set.r_stride;
		out_fmt = t->set.r_fmt;
		out_uoff = 0;
		out_voff = 0;
		out_rot = IPU_ROTATE_NONE;
	} else {
		outbuf = t->output.paddr + t->set.o_off;
		out_w = t->output.crop.w;
		out_h = t->output.crop.h;
		out_stride = t->set.ostride;
		out_fmt = t->output.format;
		out_uoff = t->set.o_uoff;
		out_voff = t->set.o_voff;
		out_rot = t->output.rotate;
	}

	/* settings */
	params.mem_prp_vf_mem.in_width = t->input.crop.w;
	params.mem_prp_vf_mem.out_width = out_w;
	params.mem_prp_vf_mem.in_height = t->input.crop.h;
	params.mem_prp_vf_mem.out_height = out_h;
	params.mem_prp_vf_mem.in_pixel_fmt = t->input.format;
	params.mem_prp_vf_mem.out_pixel_fmt = out_fmt;
	params.mem_prp_vf_mem.motion_sel = t->input.deinterlace.motion;

	params.mem_prp_vf_mem.outh_resize_ratio =
			t->set.sp_setting.outh_resize_ratio;
	params.mem_prp_vf_mem.outv_resize_ratio =
			t->set.sp_setting.outv_resize_ratio;

	if (t->overlay_en) {
		params.mem_prp_vf_mem.in_g_pixel_fmt = t->overlay.format;
		params.mem_prp_vf_mem.graphics_combine_en = 1;
		if (t->overlay.alpha.mode == IPU_ALPHA_MODE_GLOBAL)
			params.mem_prp_vf_mem.global_alpha_en = 1;
		else if (t->overlay.alpha.loc_alp_paddr)
			params.mem_prp_vf_mem.alpha_chan_en = 1;
		/* otherwise, alpha bending per pixel is used. */
		params.mem_prp_vf_mem.alpha = t->overlay.alpha.gvalue;
		if (t->overlay.colorkey.enable) {
			params.mem_prp_vf_mem.key_color_en = 1;
			params.mem_prp_vf_mem.key_color = t->overlay.colorkey.value;
		}
	}

	if (t->input.deinterlace.enable) {
		if (t->input.deinterlace.field_fmt & IPU_DEINTERLACE_FIELD_MASK)
			params.mem_prp_vf_mem.field_fmt =
				IPU_DEINTERLACE_FIELD_BOTTOM;
		else
			params.mem_prp_vf_mem.field_fmt =
				IPU_DEINTERLACE_FIELD_TOP;

		if (t->input.deinterlace.field_fmt & IPU_DEINTERLACE_RATE_EN)
			vdi_frame_idx = t->input.deinterlace.field_fmt &
						IPU_DEINTERLACE_RATE_FRAME1;
	}

	if (t->set.mode & VDOA_MODE)
		ipu->vdoa_en = 1;

	/* init channels */
	if (!(t->set.task & VDOA_ONLY)) {
		ret = ipu_init_channel(ipu, t->set.ic_chan, &params);
		if (ret < 0) {
			t->state = STATE_INIT_CHAN_FAIL;
			goto done;
		}
	}

	if (deinterlace_3_field(t)) {
		ret = ipu_init_channel(ipu, t->set.vdi_ic_p_chan, &params);
		if (ret < 0) {
			t->state = STATE_INIT_CHAN_FAIL;
			goto done;
		}
		ret = ipu_init_channel(ipu, t->set.vdi_ic_n_chan, &params);
		if (ret < 0) {
			t->state = STATE_INIT_CHAN_FAIL;
			goto done;
		}
	}

	/* init channel bufs */
	if ((IPU_PIX_FMT_TILED_NV12 == t->input.format) ||
		(IPU_PIX_FMT_TILED_NV12F == t->input.format)) {
		ret = init_tiled_ch_bufs(ipu, t);
		if (ret < 0)
			goto done;
	} else {
		if ((deinterlace_3_field(t)) &&
			(IPU_PIX_FMT_TILED_NV12F != t->input.format)) {
			if (params.mem_prp_vf_mem.field_fmt ==
				IPU_DEINTERLACE_FIELD_TOP) {
				if (vdi_frame_idx) {
					inbuf_p = t->input.paddr + t->set.istride +
							t->set.i_off;
					inbuf = t->input.paddr_n + t->set.i_off;
					inbuf_n = t->input.paddr_n + t->set.istride +
							t->set.i_off;
					params.mem_prp_vf_mem.field_fmt =
						IPU_DEINTERLACE_FIELD_BOTTOM;
				} else {
					inbuf_p = t->input.paddr + t->set.i_off;
					inbuf = t->input.paddr + t->set.istride + t->set.i_off;
					inbuf_n = t->input.paddr_n + t->set.i_off;
				}
			} else {
				if (vdi_frame_idx) {
					inbuf_p = t->input.paddr + t->set.i_off;
					inbuf = t->input.paddr_n + t->set.istride + t->set.i_off;
					inbuf_n = t->input.paddr_n + t->set.i_off;
					params.mem_prp_vf_mem.field_fmt =
						IPU_DEINTERLACE_FIELD_TOP;
				} else {
					inbuf_p = t->input.paddr + t->set.istride +
							t->set.i_off;
					inbuf = t->input.paddr + t->set.i_off;
					inbuf_n = t->input.paddr_n + t->set.istride +
							t->set.i_off;
				}
			}
		} else {
			if (t->input.deinterlace.enable) {
				if (params.mem_prp_vf_mem.field_fmt ==
					IPU_DEINTERLACE_FIELD_TOP) {
					if (vdi_frame_idx) {
						inbuf = t->input.paddr + t->set.istride + t->set.i_off;
						params.mem_prp_vf_mem.field_fmt =
							IPU_DEINTERLACE_FIELD_BOTTOM;
					} else
						inbuf = t->input.paddr + t->set.i_off;
				} else {
					if (vdi_frame_idx) {
						inbuf = t->input.paddr + t->set.i_off;
						params.mem_prp_vf_mem.field_fmt =
							IPU_DEINTERLACE_FIELD_TOP;
					} else
						inbuf = t->input.paddr + t->set.istride + t->set.i_off;
				}
			} else
				inbuf = t->input.paddr + t->set.i_off;
		}

		if (t->overlay_en)
			ovbuf = t->overlay.paddr + t->set.ov_off;
	}
	if (t->overlay_en && (t->overlay.alpha.mode == IPU_ALPHA_MODE_LOCAL))
		ov_alp_buf = t->overlay.alpha.loc_alp_paddr
			+ t->set.ov_alpha_off;

	if ((IPU_PIX_FMT_TILED_NV12 != t->input.format) &&
		(IPU_PIX_FMT_TILED_NV12F != t->input.format)) {
		ret = ipu_init_channel_buffer(ipu,
				t->set.ic_chan,
				IPU_INPUT_BUFFER,
				t->input.format,
				t->input.crop.w,
				t->input.crop.h,
				t->set.istride,
				IPU_ROTATE_NONE,
				inbuf,
				0,
				0,
				t->set.i_uoff,
				t->set.i_voff);
		if (ret < 0) {
			t->state = STATE_INIT_CHAN_BUF_FAIL;
			goto done;
		}
	}
	if (deinterlace_3_field(t) &&
		(IPU_PIX_FMT_TILED_NV12F != t->input.format)) {
		ret = ipu_init_channel_buffer(ipu,
				t->set.vdi_ic_p_chan,
				IPU_INPUT_BUFFER,
				t->input.format,
				t->input.crop.w,
				t->input.crop.h,
				t->set.istride,
				IPU_ROTATE_NONE,
				inbuf_p,
				0,
				0,
				t->set.i_uoff,
				t->set.i_voff);
		if (ret < 0) {
			t->state = STATE_INIT_CHAN_BUF_FAIL;
			goto done;
		}

		ret = ipu_init_channel_buffer(ipu,
				t->set.vdi_ic_n_chan,
				IPU_INPUT_BUFFER,
				t->input.format,
				t->input.crop.w,
				t->input.crop.h,
				t->set.istride,
				IPU_ROTATE_NONE,
				inbuf_n,
				0,
				0,
				t->set.i_uoff,
				t->set.i_voff);
		if (ret < 0) {
			t->state = STATE_INIT_CHAN_BUF_FAIL;
			goto done;
		}
	}

	if (t->overlay_en) {
		ret = ipu_init_channel_buffer(ipu,
				t->set.ic_chan,
				IPU_GRAPH_IN_BUFFER,
				t->overlay.format,
				t->overlay.crop.w,
				t->overlay.crop.h,
				t->set.ovstride,
				IPU_ROTATE_NONE,
				ovbuf,
				0,
				0,
				t->set.ov_uoff,
				t->set.ov_voff);
		if (ret < 0) {
			t->state = STATE_INIT_CHAN_BUF_FAIL;
			goto done;
		}
	}

	if (t->overlay.alpha.mode == IPU_ALPHA_MODE_LOCAL) {
		ret = ipu_init_channel_buffer(ipu,
				t->set.ic_chan,
				IPU_ALPHA_IN_BUFFER,
				IPU_PIX_FMT_GENERIC,
				t->overlay.crop.w,
				t->overlay.crop.h,
				t->set.ov_alpha_stride,
				IPU_ROTATE_NONE,
				ov_alp_buf,
				0,
				0,
				0, 0);
		if (ret < 0) {
			t->state = STATE_INIT_CHAN_BUF_FAIL;
			goto done;
		}
	}

	if (!(t->set.task & VDOA_ONLY)) {
		ret = ipu_init_channel_buffer(ipu,
				t->set.ic_chan,
				IPU_OUTPUT_BUFFER,
				out_fmt,
				out_w,
				out_h,
				out_stride,
				out_rot,
				outbuf,
				0,
				0,
				out_uoff,
				out_voff);
		if (ret < 0) {
			t->state = STATE_INIT_CHAN_BUF_FAIL;
			goto done;
		}
	}

	if ((t->set.mode & VDOA_BAND_MODE) && (t->set.task & VDI_VF)) {
		ret = ipu_link_channels(ipu, MEM_VDOA_MEM, t->set.ic_chan);
		CHECK_RETCODE(ret < 0, "ipu_link_ch vdoa_ic",
				STATE_LINK_CHAN_FAIL, done, ret);
	}

done:
	return ret;
}

static void uninit_ic(struct ipu_soc *ipu, struct ipu_task_entry *t)
{
	int ret;

	if ((t->set.mode & VDOA_BAND_MODE) && (t->set.task & VDI_VF)) {
		ret = ipu_unlink_channels(ipu, MEM_VDOA_MEM, t->set.ic_chan);
		CHECK_RETCODE_CONT(ret < 0, "ipu_unlink_ch vdoa_ic",
				STATE_UNLINK_CHAN_FAIL, ret);
	}
	ipu_uninit_channel(ipu, t->set.ic_chan);
	if (deinterlace_3_field(t)) {
		ipu_uninit_channel(ipu, t->set.vdi_ic_p_chan);
		ipu_uninit_channel(ipu, t->set.vdi_ic_n_chan);
	}
}

static int init_rot(struct ipu_soc *ipu, struct ipu_task_entry *t)
{
	int ret = 0;
	dma_addr_t inbuf = 0, outbuf = 0;
	int in_uoff = 0, in_voff = 0;
	int in_fmt, in_width, in_height, in_stride;

	/* init channel */
	ret = ipu_init_channel(ipu, t->set.rot_chan, NULL);
	if (ret < 0) {
		t->state = STATE_INIT_CHAN_FAIL;
		goto done;
	}

	/* init channel buf */
	/* is it need link to a ic channel */
	if (ic_and_rot(t->set.mode)) {
		in_fmt = t->set.r_fmt;
		in_width = t->set.r_width;
		in_height = t->set.r_height;
		in_stride = t->set.r_stride;
		inbuf = t->set.r_paddr;
		in_uoff = 0;
		in_voff = 0;
	} else {
		in_fmt = t->input.format;
		in_width = t->input.crop.w;
		in_height = t->input.crop.h;
		in_stride = t->set.istride;
		inbuf = t->input.paddr + t->set.i_off;
		in_uoff = t->set.i_uoff;
		in_voff = t->set.i_voff;
	}
	outbuf = t->output.paddr + t->set.o_off;

	ret = ipu_init_channel_buffer(ipu,
			t->set.rot_chan,
			IPU_INPUT_BUFFER,
			in_fmt,
			in_width,
			in_height,
			in_stride,
			t->output.rotate,
			inbuf,
			0,
			0,
			in_uoff,
			in_voff);
	if (ret < 0) {
		t->state = STATE_INIT_CHAN_BUF_FAIL;
		goto done;
	}

	ret = ipu_init_channel_buffer(ipu,
			t->set.rot_chan,
			IPU_OUTPUT_BUFFER,
			t->output.format,
			t->output.crop.w,
			t->output.crop.h,
			t->set.ostride,
			IPU_ROTATE_NONE,
			outbuf,
			0,
			0,
			t->set.o_uoff,
			t->set.o_voff);
	if (ret < 0) {
		t->state = STATE_INIT_CHAN_BUF_FAIL;
		goto done;
	}

done:
	return ret;
}

static void uninit_rot(struct ipu_soc *ipu, struct ipu_task_entry *t)
{
	ipu_uninit_channel(ipu, t->set.rot_chan);
}

static int get_irq(struct ipu_task_entry *t)
{
	int irq;
	ipu_channel_t chan;

	if (only_ic(t->set.mode))
		chan = t->set.ic_chan;
	else
		chan = t->set.rot_chan;

	switch (chan) {
	case MEM_ROT_VF_MEM:
		irq = IPU_IRQ_PRP_VF_ROT_OUT_EOF;
		break;
	case MEM_ROT_PP_MEM:
		irq = IPU_IRQ_PP_ROT_OUT_EOF;
		break;
	case MEM_VDI_PRP_VF_MEM:
	case MEM_PRP_VF_MEM:
		irq = IPU_IRQ_PRP_VF_OUT_EOF;
		break;
	case MEM_PP_MEM:
		irq = IPU_IRQ_PP_OUT_EOF;
		break;
	case MEM_VDI_MEM:
		irq = IPU_IRQ_VDIC_OUT_EOF;
		break;
	default:
		irq = -EINVAL;
	}

	return irq;
}

static irqreturn_t task_irq_handler(int irq, void *dev_id)
{
	struct ipu_task_entry *prev_tsk = dev_id;

	CHECK_PERF(&prev_tsk->ts_inirq);
	complete(&prev_tsk->irq_comp);
	dev_dbg(prev_tsk->dev, "[0x%p] no-0x%x in-irq!",
				 prev_tsk, prev_tsk->task_no);

	return IRQ_HANDLED;
}

/* Fix deinterlace up&down split mode medium line */
static void vdi_split_process(struct ipu_soc *ipu, struct ipu_task_entry *t)
{
	u32 vdi_size;
	u32 vdi_save_lines;
	u32 stripe_mode;
	u32 task_no;
	u32 i, offset_addr;
	u32 line_size;
	unsigned char  *base_off;
	struct ipu_task_entry *parent = t->parent;
	struct mutex *lock = &parent->vdic_lock;

	if (!parent) {
		dev_err(t->dev, "ERR[0x%x]invalid parent\n", t->task_no);
		return;
	}
	mutex_lock(lock);
	stripe_mode = t->task_no & 0xf;
	task_no = t->task_no >> 4;

	/* Save both luma and chroma part for interleaved YUV(e.g. YUYV).
	 * Save luma part for non-interleaved and partial-interleaved
	 * YUV format (e.g NV12 and YV12). */
	if (t->output.format == IPU_PIX_FMT_YUYV ||
			t->output.format == IPU_PIX_FMT_UYVY)
		line_size = t->output.crop.w * fmt_to_bpp(t->output.format)/8;
	else
		line_size = t->output.crop.w;

	vdi_save_lines = (t->output.crop.h - t->set.sp_setting.ud_split_line)/2;
	vdi_size = vdi_save_lines * line_size;
	if (vdi_save_lines <= 0) {
		dev_err(t->dev, "[0x%p] vdi_save_line error\n", (void *)t);
		mutex_unlock(lock);
		return;
	}

	/*check vditmpbuf buffer have alloced or buffer size is changed */
	if ((vdi_save_lines != parent->old_save_lines) ||
		(vdi_size != parent->old_size)) {
		if (parent->vditmpbuf[0] != NULL)
			kfree(parent->vditmpbuf[0]);
		if (parent->vditmpbuf[1] != NULL)
			kfree(parent->vditmpbuf[1]);

		parent->vditmpbuf[0] = kmalloc(vdi_size, GFP_KERNEL);
		if (parent->vditmpbuf[0] == NULL) {
			dev_err(t->dev,
				"[0x%p]Falied Alloc vditmpbuf[0]\n", (void *)t);
			mutex_unlock(lock);
			return;
		}
		memset(parent->vditmpbuf[0], 0, vdi_size);

		parent->vditmpbuf[1] = kmalloc(vdi_size, GFP_KERNEL);
		if (parent->vditmpbuf[1] == NULL) {
			dev_err(t->dev,
				"[0x%p]Falied Alloc vditmpbuf[1]\n", (void *)t);
			mutex_unlock(lock);
			return;
		}
		memset(parent->vditmpbuf[1], 0, vdi_size);

		parent->old_save_lines = vdi_save_lines;
		parent->old_size = vdi_size;
	}

	if (pfn_valid(t->output.paddr >> PAGE_SHIFT)) {
		base_off = page_address(pfn_to_page(t->output.paddr >> PAGE_SHIFT));
		base_off += t->output.paddr & ((1 << PAGE_SHIFT) - 1);
	} else {
		base_off = (char *)ioremap_nocache(t->output.paddr,
				t->output.width * t->output.height *
				fmt_to_bpp(t->output.format)/8);
	}
	if (base_off == NULL) {
		dev_err(t->dev, "ERR[0x%p]Failed get virtual address\n", t);
		mutex_unlock(lock);
		return;
	}

	/* UP stripe or UP&LEFT stripe */
	if ((stripe_mode == UP_STRIPE) ||
			(stripe_mode == (UP_STRIPE | LEFT_STRIPE))) {
		if (!parent->buf0filled) {
			offset_addr = t->set.o_off +
				t->set.sp_setting.ud_split_line*t->set.ostride;
			dmac_flush_range(base_off + offset_addr,
					base_off + offset_addr + vdi_size);
			outer_flush_range(t->output.paddr + offset_addr,
				t->output.paddr + offset_addr + vdi_size);

			for (i = 0; i < vdi_save_lines; i++)
				memcpy(parent->vditmpbuf[0] + i*line_size,
					base_off + offset_addr +
					i*t->set.ostride, line_size);
			parent->buf0filled = true;
		} else {
			offset_addr = t->set.o_off + (t->output.crop.h -
					vdi_save_lines) * t->set.ostride;
			for (i = 0; i < vdi_save_lines; i++)
				memcpy(base_off + offset_addr + i*t->set.ostride,
						parent->vditmpbuf[0] + i*line_size, line_size);

			dmac_flush_range(base_off + offset_addr,
					base_off + offset_addr + i*t->set.ostride);
			outer_flush_range(t->output.paddr + offset_addr,
					t->output.paddr + offset_addr + i*t->set.ostride);
			parent->buf0filled = false;
		}
	}
	/*Down stripe or Down&Left stripe*/
	else if ((stripe_mode == DOWN_STRIPE) ||
			(stripe_mode == (DOWN_STRIPE | LEFT_STRIPE))) {
		if (!parent->buf0filled) {
			offset_addr = t->set.o_off + vdi_save_lines*t->set.ostride;
			dmac_flush_range(base_off + offset_addr,
					base_off + offset_addr + vdi_size);
			outer_flush_range(t->output.paddr + offset_addr,
					t->output.paddr + offset_addr + vdi_size);

			for (i = 0; i < vdi_save_lines; i++)
				memcpy(parent->vditmpbuf[0] + i*line_size,
						base_off + offset_addr + i*t->set.ostride,
						line_size);
			parent->buf0filled = true;
		} else {
			offset_addr = t->set.o_off;
			for (i = 0; i < vdi_save_lines; i++)
				memcpy(base_off + offset_addr + i*t->set.ostride,
						parent->vditmpbuf[0] + i*line_size,
						line_size);

			dmac_flush_range(base_off + offset_addr,
					base_off + offset_addr + i*t->set.ostride);
			outer_flush_range(t->output.paddr + offset_addr,
					t->output.paddr + offset_addr + i*t->set.ostride);
			parent->buf0filled = false;
		}
	}
	/*Up&Right stripe*/
	else if (stripe_mode == (UP_STRIPE | RIGHT_STRIPE)) {
		if (!parent->buf1filled) {
			offset_addr = t->set.o_off +
				t->set.sp_setting.ud_split_line*t->set.ostride;
			dmac_flush_range(base_off + offset_addr,
					base_off + offset_addr + vdi_size);
			outer_flush_range(t->output.paddr + offset_addr,
					t->output.paddr + offset_addr + vdi_size);

			for (i = 0; i < vdi_save_lines; i++)
				memcpy(parent->vditmpbuf[1] + i*line_size,
						base_off + offset_addr + i*t->set.ostride,
						line_size);
			parent->buf1filled = true;
		} else {
			offset_addr = t->set.o_off +
				(t->output.crop.h - vdi_save_lines)*t->set.ostride;
			for (i = 0; i < vdi_save_lines; i++)
				memcpy(base_off + offset_addr + i*t->set.ostride,
						parent->vditmpbuf[1] + i*line_size,
						line_size);

			dmac_flush_range(base_off + offset_addr,
					base_off + offset_addr + i*t->set.ostride);
			outer_flush_range(t->output.paddr + offset_addr,
					t->output.paddr + offset_addr + i*t->set.ostride);
			parent->buf1filled = false;
		}
	}
	/*Down stripe or Down&Right stript*/
	else if (stripe_mode == (DOWN_STRIPE | RIGHT_STRIPE)) {
		if (!parent->buf1filled) {
			offset_addr = t->set.o_off + vdi_save_lines*t->set.ostride;
			dmac_flush_range(base_off + offset_addr,
					base_off + offset_addr + vdi_save_lines*t->set.ostride);
			outer_flush_range(t->output.paddr + offset_addr,
					t->output.paddr + offset_addr + vdi_save_lines*t->set.ostride);

			for (i = 0; i < vdi_save_lines; i++)
				memcpy(parent->vditmpbuf[1] + i*line_size,
						base_off + offset_addr + i*t->set.ostride,
						line_size);
			parent->buf1filled = true;
		} else {
			offset_addr = t->set.o_off;
			for (i = 0; i < vdi_save_lines; i++)
				memcpy(base_off + offset_addr + i*t->set.ostride,
						parent->vditmpbuf[1] + i*line_size,
						line_size);

			dmac_flush_range(base_off + offset_addr,
					base_off + offset_addr + vdi_save_lines*t->set.ostride);
			outer_flush_range(t->output.paddr + offset_addr,
					t->output.paddr + offset_addr + vdi_save_lines*t->set.ostride);
			parent->buf1filled = false;
		}
	}
	if (!pfn_valid(t->output.paddr >> PAGE_SHIFT))
		iounmap(base_off);
	mutex_unlock(lock);
}

static void do_task_release(struct ipu_task_entry *t, int fail)
{
	int ret;
	struct ipu_soc *ipu = t->ipu;

	if (t->input.deinterlace.enable && !fail &&
			(t->task_no & (UP_STRIPE | DOWN_STRIPE)))
		vdi_split_process(ipu, t);

	ipu_free_irq(ipu, t->irq, t);

	if (t->vdoa_dma.vaddr)
		dma_free_coherent(t->dev,
			t->vdoa_dma.size,
			t->vdoa_dma.vaddr,
			t->vdoa_dma.paddr);

	if (only_ic(t->set.mode)) {
		ret = ipu_disable_channel(ipu, t->set.ic_chan, true);
		CHECK_RETCODE_CONT(ret < 0, "ipu_disable_ch only_ic",
				STATE_DISABLE_CHAN_FAIL, ret);
		if (deinterlace_3_field(t)) {
			ret = ipu_disable_channel(ipu, t->set.vdi_ic_p_chan,
							true);
			CHECK_RETCODE_CONT(ret < 0, "ipu_disable_ch only_ic_p",
					STATE_DISABLE_CHAN_FAIL, ret);
			ret = ipu_disable_channel(ipu, t->set.vdi_ic_n_chan,
							true);
			CHECK_RETCODE_CONT(ret < 0, "ipu_disable_ch only_ic_n",
					STATE_DISABLE_CHAN_FAIL, ret);
		}
	} else if (only_rot(t->set.mode)) {
		ret = ipu_disable_channel(ipu, t->set.rot_chan, true);
		CHECK_RETCODE_CONT(ret < 0, "ipu_disable_ch only_rot",
				STATE_DISABLE_CHAN_FAIL, ret);
	} else if (ic_and_rot(t->set.mode)) {
		ret = ipu_unlink_channels(ipu, t->set.ic_chan, t->set.rot_chan);
		CHECK_RETCODE_CONT(ret < 0, "ipu_unlink_ch",
				STATE_UNLINK_CHAN_FAIL, ret);
		ret = ipu_disable_channel(ipu, t->set.rot_chan, true);
		CHECK_RETCODE_CONT(ret < 0, "ipu_disable_ch ic_and_rot-rot",
				STATE_DISABLE_CHAN_FAIL, ret);
		ret = ipu_disable_channel(ipu, t->set.ic_chan, true);
		CHECK_RETCODE_CONT(ret < 0, "ipu_disable_ch ic_and_rot-ic",
				STATE_DISABLE_CHAN_FAIL, ret);
		if (deinterlace_3_field(t)) {
			ret = ipu_disable_channel(ipu, t->set.vdi_ic_p_chan,
							true);
			CHECK_RETCODE_CONT(ret < 0, "ipu_disable_ch icrot-ic-p",
					STATE_DISABLE_CHAN_FAIL, ret);
			ret = ipu_disable_channel(ipu, t->set.vdi_ic_n_chan,
							true);
			CHECK_RETCODE_CONT(ret < 0, "ipu_disable_ch icrot-ic-n",
					STATE_DISABLE_CHAN_FAIL, ret);
		}
	}

	if (only_ic(t->set.mode))
		uninit_ic(ipu, t);
	else if (only_rot(t->set.mode))
		uninit_rot(ipu, t);
	else if (ic_and_rot(t->set.mode)) {
		uninit_ic(ipu, t);
		uninit_rot(ipu, t);
	}

	t->state = STATE_OK;
	CHECK_PERF(&t->ts_rel);
	return;
}

static void do_task_vdoa_only(struct ipu_task_entry *t)
{
	int ret;

	ret = init_tiled_ch_bufs(NULL, t);
	CHECK_RETCODE(ret < 0, "do_vdoa_only", STATE_ERR, out, ret);
	ret = vdoa_start(t->vdoa_handle, VDOA_DEF_TIMEOUT_MS);
	vdoa_stop(t->vdoa_handle);
	CHECK_RETCODE(ret < 0, "vdoa_wait4complete, do_vdoa_only",
			STATE_VDOA_IRQ_TIMEOUT, out, ret);

	t->state = STATE_OK;
out:
	return;
}

static void do_task(struct ipu_task_entry *t)
{
	int r_size;
	int irq;
	int ret;
	uint32_t busy;
	struct ipu_soc *ipu = t->ipu;

	CHECK_PERF(&t->ts_dotask);

	if (!ipu) {
		t->state = STATE_NO_IPU;
		return;
	}

	init_completion(&t->irq_comp);
	dev_dbg(ipu->dev, "[0x%p]Do task no:0x%x: id %d\n", (void *)t,
		 t->task_no, t->task_id);
	dump_task_info(t);

	if (t->set.task & IC_PP) {
		t->set.ic_chan = MEM_PP_MEM;
		dev_dbg(ipu->dev, "[0x%p]ic channel MEM_PP_MEM\n", (void *)t);
	} else if (t->set.task & IC_VF) {
		t->set.ic_chan = MEM_PRP_VF_MEM;
		dev_dbg(ipu->dev, "[0x%p]ic channel MEM_PRP_VF_MEM\n", (void *)t);
	} else if (t->set.task & VDI_VF) {
		if (t->set.mode & VDOA_BAND_MODE) {
			t->set.ic_chan = MEM_VDI_MEM;
			if (deinterlace_3_field(t)) {
				t->set.vdi_ic_p_chan = MEM_VDI_MEM_P;
				t->set.vdi_ic_n_chan = MEM_VDI_MEM_N;
			}
			dev_dbg(ipu->dev, "[0x%p]ic ch MEM_VDI_MEM\n",
					 (void *)t);
		} else {
			t->set.ic_chan = MEM_VDI_PRP_VF_MEM;
			if (deinterlace_3_field(t)) {
				t->set.vdi_ic_p_chan = MEM_VDI_PRP_VF_MEM_P;
				t->set.vdi_ic_n_chan = MEM_VDI_PRP_VF_MEM_N;
			}
			dev_dbg(ipu->dev,
				"[0x%p]ic ch MEM_VDI_PRP_VF_MEM\n", t);
		}
	}

	if (t->set.task & ROT_PP) {
		t->set.rot_chan = MEM_ROT_PP_MEM;
		dev_dbg(ipu->dev, "[0x%p]rot channel MEM_ROT_PP_MEM\n", (void *)t);
	} else if (t->set.task & ROT_VF) {
		t->set.rot_chan = MEM_ROT_VF_MEM;
		dev_dbg(ipu->dev, "[0x%p]rot channel MEM_ROT_VF_MEM\n", (void *)t);
	}

	if (t->task_id == IPU_TASK_ID_VF)
		busy = ic_vf_pp_is_busy(ipu, true);
	else if (t->task_id == IPU_TASK_ID_PP)
		busy = ic_vf_pp_is_busy(ipu, false);
	else {
		dev_err(ipu->dev, "ERR[no:0x%x]ipu task_id:%d invalid!\n",
				t->task_no, t->task_id);
		return;
	}
	if (busy) {
		dev_err(ipu->dev, "ERR[0x%p-no:0x%x]ipu task_id:%d busy!\n",
				(void *)t, t->task_no, t->task_id);
		t->state = STATE_IPU_BUSY;
		return;
	}

	irq = get_irq(t);
	if (irq < 0) {
		t->state = STATE_NO_IRQ;
		return;
	}
	t->irq = irq;

	/* channel setup */
	if (only_ic(t->set.mode)) {
		dev_dbg(t->dev, "[0x%p]only ic mode\n", (void *)t);
		ret = init_ic(ipu, t);
		CHECK_RETCODE(ret < 0, "init_ic only_ic",
				t->state, chan_setup, ret);
	} else if (only_rot(t->set.mode)) {
		dev_dbg(t->dev, "[0x%p]only rot mode\n", (void *)t);
		ret = init_rot(ipu, t);
		CHECK_RETCODE(ret < 0, "init_rot only_rot",
				t->state, chan_setup, ret);
	} else if (ic_and_rot(t->set.mode)) {
		int rot_idx = (t->task_id == IPU_TASK_ID_VF) ? 0 : 1;

		dev_dbg(t->dev, "[0x%p]ic + rot mode\n", (void *)t);
		t->set.r_fmt = t->output.format;
		if (t->output.rotate >= IPU_ROTATE_90_RIGHT) {
			t->set.r_width = t->output.crop.h;
			t->set.r_height = t->output.crop.w;
		} else {
			t->set.r_width = t->output.crop.w;
			t->set.r_height = t->output.crop.h;
		}
		t->set.r_stride = t->set.r_width *
			bytes_per_pixel(t->set.r_fmt);
		r_size = PAGE_ALIGN(t->set.r_width * t->set.r_height
			* fmt_to_bpp(t->set.r_fmt)/8);

		if (r_size > ipu->rot_dma[rot_idx].size) {
			dev_dbg(t->dev, "[0x%p]realloc rot buffer\n", (void *)t);

			if (ipu->rot_dma[rot_idx].vaddr)
				dma_free_coherent(t->dev,
					ipu->rot_dma[rot_idx].size,
					ipu->rot_dma[rot_idx].vaddr,
					ipu->rot_dma[rot_idx].paddr);

			ipu->rot_dma[rot_idx].size = r_size;
			ipu->rot_dma[rot_idx].vaddr = dma_alloc_coherent(t->dev,
						r_size,
						&ipu->rot_dma[rot_idx].paddr,
						GFP_DMA | GFP_KERNEL);
			CHECK_RETCODE(ipu->rot_dma[rot_idx].vaddr == NULL,
					"ic_and_rot", STATE_SYS_NO_MEM,
					chan_setup, -ENOMEM);
		}
		t->set.r_paddr = ipu->rot_dma[rot_idx].paddr;

		dev_dbg(t->dev, "[0x%p]rotation:\n", (void *)t);
		dev_dbg(t->dev, "[0x%p]\tformat = 0x%x\n", (void *)t, t->set.r_fmt);
		dev_dbg(t->dev, "[0x%p]\twidth = %d\n", (void *)t, t->set.r_width);
		dev_dbg(t->dev, "[0x%p]\theight = %d\n", (void *)t, t->set.r_height);
		dev_dbg(t->dev, "[0x%p]\tpaddr = 0x%x\n", (void *)t, t->set.r_paddr);
		dev_dbg(t->dev, "[0x%p]\trstride = %d\n", (void *)t, t->set.r_stride);

		ret = init_ic(ipu, t);
		CHECK_RETCODE(ret < 0, "init_ic ic_and_rot",
				t->state, chan_setup, ret);
		ret = init_rot(ipu, t);
		CHECK_RETCODE(ret < 0, "init_rot ic_and_rot",
				t->state, chan_setup, ret);
		ret = ipu_link_channels(ipu, t->set.ic_chan,
				t->set.rot_chan);
		CHECK_RETCODE(ret < 0, "ipu_link_ch ic_and_rot",
				STATE_LINK_CHAN_FAIL, chan_setup, ret);
	} else {
		dev_err(t->dev, "ERR [0x%p]do task: should not be here\n", t);
		t->state = STATE_ERR;
		return;
	}

	ret = ipu_request_irq(ipu, irq, task_irq_handler, 0, NULL, t);
	CHECK_RETCODE(ret < 0, "ipu_req_irq",
			STATE_IRQ_FAIL, chan_setup, ret);

	/* enable/start channel */
	if (only_ic(t->set.mode)) {
		ret = ipu_enable_channel(ipu, t->set.ic_chan);
		CHECK_RETCODE(ret < 0, "ipu_enable_ch only_ic",
				STATE_ENABLE_CHAN_FAIL, chan_en, ret);
		if (deinterlace_3_field(t)) {
			ret = ipu_enable_channel(ipu, t->set.vdi_ic_p_chan);
			CHECK_RETCODE(ret < 0, "ipu_enable_ch only_ic_p",
					STATE_ENABLE_CHAN_FAIL, chan_en, ret);
			ret = ipu_enable_channel(ipu, t->set.vdi_ic_n_chan);
			CHECK_RETCODE(ret < 0, "ipu_enable_ch only_ic_n",
					STATE_ENABLE_CHAN_FAIL, chan_en, ret);
		}

		ret = ipu_select_buffer(ipu, t->set.ic_chan, IPU_OUTPUT_BUFFER,
					0);
		CHECK_RETCODE(ret < 0, "ipu_sel_buf only_ic",
				STATE_SEL_BUF_FAIL, chan_buf, ret);
		if (t->overlay_en) {
			ret = ipu_select_buffer(ipu, t->set.ic_chan,
						IPU_GRAPH_IN_BUFFER, 0);
			CHECK_RETCODE(ret < 0, "ipu_sel_buf only_ic_g",
					STATE_SEL_BUF_FAIL, chan_buf, ret);
			if (t->overlay.alpha.mode == IPU_ALPHA_MODE_LOCAL) {
				ret = ipu_select_buffer(ipu, t->set.ic_chan,
							IPU_ALPHA_IN_BUFFER, 0);
				CHECK_RETCODE(ret < 0, "ipu_sel_buf only_ic_a",
						STATE_SEL_BUF_FAIL, chan_buf,
						ret);
			}
		}
		if (!(t->set.mode & VDOA_BAND_MODE)) {
			if (deinterlace_3_field(t))
				ipu_select_multi_vdi_buffer(ipu, 0);
			else {
				ret = ipu_select_buffer(ipu, t->set.ic_chan,
							IPU_INPUT_BUFFER, 0);
				CHECK_RETCODE(ret < 0, "ipu_sel_buf only_ic_i",
					STATE_SEL_BUF_FAIL, chan_buf, ret);
			}
		}
	} else if (only_rot(t->set.mode)) {
		ret = ipu_enable_channel(ipu, t->set.rot_chan);
		CHECK_RETCODE(ret < 0, "ipu_enable_ch only_rot",
				STATE_ENABLE_CHAN_FAIL, chan_en, ret);
		ret = ipu_select_buffer(ipu, t->set.rot_chan,
						IPU_OUTPUT_BUFFER, 0);
		CHECK_RETCODE(ret < 0, "ipu_sel_buf only_rot_o",
				STATE_SEL_BUF_FAIL, chan_buf, ret);
		ret = ipu_select_buffer(ipu, t->set.rot_chan,
						IPU_INPUT_BUFFER, 0);
		CHECK_RETCODE(ret < 0, "ipu_sel_buf only_rot_i",
				STATE_SEL_BUF_FAIL, chan_buf, ret);
	} else if (ic_and_rot(t->set.mode)) {
		ret = ipu_enable_channel(ipu, t->set.rot_chan);
		CHECK_RETCODE(ret < 0, "ipu_enable_ch ic_and_rot-rot",
				STATE_ENABLE_CHAN_FAIL, chan_en, ret);
		ret = ipu_enable_channel(ipu, t->set.ic_chan);
		CHECK_RETCODE(ret < 0, "ipu_enable_ch ic_and_rot-ic",
				STATE_ENABLE_CHAN_FAIL, chan_en, ret);
		if (deinterlace_3_field(t)) {
			ret = ipu_enable_channel(ipu, t->set.vdi_ic_p_chan);
			CHECK_RETCODE(ret < 0, "ipu_enable_ch ic_and_rot-p",
					STATE_ENABLE_CHAN_FAIL, chan_en, ret);
			ret = ipu_enable_channel(ipu, t->set.vdi_ic_n_chan);
			CHECK_RETCODE(ret < 0, "ipu_enable_ch ic_and_rot-n",
					STATE_ENABLE_CHAN_FAIL, chan_en, ret);
		}

		ret = ipu_select_buffer(ipu, t->set.rot_chan,
						IPU_OUTPUT_BUFFER, 0);
		CHECK_RETCODE(ret < 0, "ipu_sel_buf ic_and_rot-rot-o",
				STATE_SEL_BUF_FAIL, chan_buf, ret);
		if (t->overlay_en) {
			ret = ipu_select_buffer(ipu, t->set.ic_chan,
							IPU_GRAPH_IN_BUFFER, 0);
			CHECK_RETCODE(ret < 0, "ipu_sel_buf ic_and_rot-ic-g",
					STATE_SEL_BUF_FAIL, chan_buf, ret);
			if (t->overlay.alpha.mode == IPU_ALPHA_MODE_LOCAL) {
				ret = ipu_select_buffer(ipu, t->set.ic_chan,
							IPU_ALPHA_IN_BUFFER, 0);
				CHECK_RETCODE(ret < 0, "ipu_sel_buf icrot-ic-a",
						STATE_SEL_BUF_FAIL,
						chan_buf, ret);
			}
		}
		ret = ipu_select_buffer(ipu, t->set.ic_chan,
						IPU_OUTPUT_BUFFER, 0);
		CHECK_RETCODE(ret < 0, "ipu_sel_buf ic_and_rot-ic-o",
				STATE_SEL_BUF_FAIL, chan_buf, ret);
		if (deinterlace_3_field(t))
			ipu_select_multi_vdi_buffer(ipu, 0);
		else {
			ret = ipu_select_buffer(ipu, t->set.ic_chan,
							IPU_INPUT_BUFFER, 0);
			CHECK_RETCODE(ret < 0, "ipu_sel_buf ic_and_rot-ic-i",
					STATE_SEL_BUF_FAIL, chan_buf, ret);
		}
	}

	if (need_split(t))
		t->state = STATE_IN_PROGRESS;

	if (t->set.mode & VDOA_BAND_MODE) {
		ret = vdoa_start(t->vdoa_handle, VDOA_DEF_TIMEOUT_MS);
		CHECK_RETCODE(ret < 0, "vdoa_wait4complete, do_vdoa_band",
				STATE_VDOA_IRQ_TIMEOUT, chan_rel, ret);
	}

	CHECK_PERF(&t->ts_waitirq);
	ret = wait_for_completion_timeout(&t->irq_comp,
				 msecs_to_jiffies(t->timeout - DEF_DELAY_MS));
	CHECK_PERF(&t->ts_wakeup);
	CHECK_RETCODE(ret == 0, "wait_for_comp_timeout",
			STATE_IRQ_TIMEOUT, chan_rel, ret);
	dev_dbg(t->dev, "[0x%p] no-0x%x ipu irq done!", t, t->task_no);

chan_rel:
chan_buf:
chan_en:
chan_setup:
	if (t->set.mode & VDOA_BAND_MODE)
		vdoa_stop(t->vdoa_handle);
	do_task_release(t, t->state >= STATE_ERR);
	return;
}

static void do_task_vdoa_vdi(struct ipu_task_entry *t)
{
	int i;
	int ret;
	u32 stripe_width;

	/* FIXME: crop mode not support now */
	stripe_width = t->input.width >> 1;
	t->input.crop.pos.x = 0;
	t->input.crop.pos.y = 0;
	t->input.crop.w = stripe_width;
	t->input.crop.h = t->input.height;
	t->output.crop.w = stripe_width;
	t->output.crop.h = t->input.height;

	for (i = 0; i < 2; i++) {
		t->input.crop.pos.x = t->input.crop.pos.x + i * stripe_width;
		t->output.crop.pos.x = t->output.crop.pos.x + i * stripe_width;
		/* check input */
		ret = set_crop(&t->input.crop, t->input.width, t->input.height,
			t->input.format);
		if (ret < 0) {
			ret = STATE_ERR;
			goto done;
		} else
			update_offset(t->input.format,
					t->input.width, t->input.height,
					t->input.crop.pos.x,
					t->input.crop.pos.y,
					&t->set.i_off, &t->set.i_uoff,
					&t->set.i_voff, &t->set.istride);
		dev_dbg(t->dev, "i_off:0x%x, i_uoff:0x%x, istride:%d.\n",
			t->set.i_off, t->set.i_uoff, t->set.istride);
		/* check output */
		ret = set_crop(&t->output.crop, t->input.width,
					t->output.height, t->output.format);
		if (ret < 0) {
			ret = STATE_ERR;
			goto done;
		} else
			update_offset(t->output.format,
					t->output.width, t->output.height,
					t->output.crop.pos.x,
					t->output.crop.pos.y,
					&t->set.o_off, &t->set.o_uoff,
					&t->set.o_voff, &t->set.ostride);

		dev_dbg(t->dev, "o_off:0x%x, o_uoff:0x%x, ostride:%d.\n",
				t->set.o_off, t->set.o_uoff, t->set.ostride);

		do_task(t);
	}

	return;
done:
	dev_err(t->dev, "ERR %s set_crop.\n", __func__);
	t->state = ret;
	return;
}

static void get_res_do_task(struct ipu_task_entry *t)
{
	uint32_t	found;
	uint32_t	split_child;
	struct mutex	*lock;

	found = get_vdoa_ipu_res(t);
	if (!found) {
		dev_err(t->dev, "ERR:[0x%p] no-0x%x can not get res\n",
			t, t->task_no);
		return;
	} else {
		if (t->set.task & VDOA_ONLY)
			do_task_vdoa_only(t);
		else if ((IPU_PIX_FMT_TILED_NV12F == t->input.format) &&
				(t->set.mode & VDOA_BAND_MODE) &&
				(t->input.crop.w >
				 soc_max_vdi_in_width(t->ipu)))
			do_task_vdoa_vdi(t);
		else
			do_task(t);
		put_vdoa_ipu_res(t, 0);
	}
	if (t->state != STATE_OK) {
		dev_err(t->dev, "ERR:[0x%p] no-0x%x state: %s\n",
			t, t->task_no, state_msg[t->state].msg);
	}

	split_child = need_split(t) && t->parent;
	if (split_child) {
		lock = &t->parent->split_lock;
		mutex_lock(lock);
		t->split_done = 1;
		mutex_unlock(lock);
		wake_up(&t->parent->split_waitq);
	}

	return;
}

static void wait_split_task_complete(struct ipu_task_entry *parent,
				struct ipu_split_task *sp_task, uint32_t size)
{
	struct ipu_task_entry *tsk = NULL;
	int ret = 0, rc;
	int j, idx = -1;
	unsigned long flags;
	struct mutex *lock = &parent->split_lock;
	int k, busy_vf, busy_pp;
	struct ipu_soc *ipu;
	DECLARE_PERF_VAR;

	for (j = 0; j < size; j++) {
		rc = wait_event_timeout(
			parent->split_waitq,
			sp_task_check_done(sp_task, parent, size, &idx),
			msecs_to_jiffies(parent->timeout - DEF_DELAY_MS));
		if (!rc) {
			dev_err(parent->dev,
				"ERR:[0x%p] no-0x%x, split_task timeout,j:%d,"
				"size:%d.\n",
				 parent, parent->task_no, j, size);
			ret = -ETIMEDOUT;
			goto out;
		} else {
			if (idx < 0) {
				dev_err(parent->dev,
				"ERR:[0x%p] no-0x%x, invalid task idx:%d\n",
				 parent, parent->task_no, idx);
				continue;
			}
			tsk = sp_task[idx].child_task;
			mutex_lock(lock);
			if (!tsk->split_done || !tsk->ipu)
				dev_err(tsk->dev,
				"ERR:no-0x%x,split not done:%d/null ipu:0x%p\n",
				 tsk->task_no, tsk->split_done, tsk->ipu);
			tsk->split_done = 0;
			mutex_unlock(lock);

			dev_dbg(tsk->dev,
				"[0x%p] no-0x%x sp_tsk[%d] done,state:%d.\n",
				 tsk, tsk->task_no, idx, tsk->state);
			#ifdef DBG_IPU_PERF
				CHECK_PERF(&tsk->ts_rel);
				PRINT_TASK_STATISTICS;
			#endif
		}
	}

out:
	if (ret == -ETIMEDOUT) {
		/* debug */
		for (k = 0; k < max_ipu_no; k++) {
			ipu = ipu_get_soc(k);
			if (IS_ERR(ipu)) {
				dev_err(parent->dev, "no:0x%x, null ipu:%d\n",
				 parent->task_no, k);
			} else {
				busy_vf = ic_vf_pp_is_busy(ipu, true);
				busy_pp = ic_vf_pp_is_busy(ipu, false);
				dev_err(parent->dev,
					"ERR:ipu[%d] busy_vf:%d, busy_pp:%d.\n",
					k, busy_vf, busy_pp);
			}
		}
		for (k = 0; k < size; k++) {
			tsk = sp_task[k].child_task;
			if (!tsk)
				continue;
			dev_err(parent->dev,
				"ERR: sp_task[%d][0x%p] no-0x%x done:%d,"
				 "state:%s,on_list:%d, ipu:0x%p,timeout!\n",
				 k, tsk, tsk->task_no, tsk->split_done,
				 state_msg[tsk->state].msg, tsk->task_in_list,
				 tsk->ipu);
		}
	}

	for (j = 0; j < size; j++) {
		tsk = sp_task[j].child_task;
		if (!tsk)
			continue;
		spin_lock_irqsave(&ipu_task_list_lock, flags);
		if (tsk->task_in_list) {
			list_del(&tsk->node);
			tsk->task_in_list = 0;
			dev_dbg(tsk->dev,
				"[0x%p] no-0x%x,id:%d sp_tsk timeout list_del.\n",
				 tsk, tsk->task_no, tsk->task_id);
		}
		spin_unlock_irqrestore(&ipu_task_list_lock, flags);
		if (!tsk->ipu)
			continue;
		if (tsk->state != STATE_OK) {
			dev_err(tsk->dev,
				"ERR:[0x%p] no-0x%x,id:%d, sp_tsk state: %s\n",
					tsk, tsk->task_no, tsk->task_id,
					state_msg[tsk->state].msg);
		}
		kref_put(&tsk->refcount, task_mem_free);
	}

	kfree(parent->vditmpbuf[0]);
	kfree(parent->vditmpbuf[1]);

	if (ret < 0)
		parent->state = STATE_TIMEOUT;
	else
		parent->state = STATE_OK;
	return;
}

static inline int find_task(struct ipu_task_entry **t, int thread_id)
{
	int found;
	unsigned long flags;
	struct ipu_task_entry *tsk;
	struct list_head *task_list = &ipu_task_list;

	*t = NULL;
	spin_lock_irqsave(&ipu_task_list_lock, flags);
	found = !list_empty(task_list);
	if (found) {
		tsk = list_first_entry(task_list, struct ipu_task_entry, node);
		if (tsk->task_in_list) {
			list_del(&tsk->node);
			tsk->task_in_list = 0;
			*t = tsk;
			kref_get(&tsk->refcount);
			dev_dbg(tsk->dev,
			"thread_id:%d,[0x%p] task_no:0x%x,mode:0x%x list_del\n",
			thread_id, tsk, tsk->task_no, tsk->set.mode);
		} else
			dev_err(tsk->dev,
			"thread_id:%d,task_no:0x%x,mode:0x%x not on list_del\n",
			thread_id, tsk->task_no, tsk->set.mode);
	}
	spin_unlock_irqrestore(&ipu_task_list_lock, flags);

	return found;
}

static int ipu_task_thread(void *argv)
{
	struct ipu_task_entry *tsk;
	struct ipu_task_entry *sp_tsk0;
	struct ipu_split_task sp_task[4];
	/* priority lower than irq_thread */
	const struct sched_param param = {
		.sched_priority = MAX_USER_RT_PRIO/2 - 1,
	};
	int ret;
	int curr_thread_id;
	uint32_t size;
	unsigned long flags;
	unsigned int cpu;
	struct cpumask cpu_mask;
	struct ipu_thread_data *data = (struct ipu_thread_data *)argv;

	thread_id++;
	curr_thread_id = thread_id;
	sched_setscheduler(current, SCHED_FIFO, &param);

	if (!data->is_vdoa) {
		cpu = cpumask_first(cpu_online_mask);
		cpumask_set_cpu(cpu, &cpu_mask);
		ret = sched_setaffinity(data->ipu->thread[data->id]->pid,
			&cpu_mask);
		if (ret < 0) {
			pr_err("%s: sched_setaffinity fail:%d.\n", __func__, ret);
		}
		pr_debug("%s: sched_setaffinity cpu:%d.\n", __func__, cpu);
	}

	while (!kthread_should_stop()) {
		int split_fail = 0;
		int split_parent;
		int split_child;

		wait_event_interruptible(thread_waitq, find_task(&tsk, curr_thread_id));

		if (!tsk) {
			pr_err("thread:%d can not find task.\n",
				curr_thread_id);
			continue;
		}

		/* note: other threads run split child task */
		split_parent = need_split(tsk) && !tsk->parent;
		split_child = need_split(tsk) && tsk->parent;
		if (split_parent) {
			if ((tsk->set.split_mode == RL_SPLIT) ||
				 (tsk->set.split_mode == UD_SPLIT))
				size = 2;
			else
				size = 4;
			ret = queue_split_task(tsk, sp_task, size);
			if (ret < 0) {
				split_fail = 1;
			} else {
				struct list_head *pos;

				spin_lock_irqsave(&ipu_task_list_lock, flags);

				sp_tsk0 = list_first_entry(&tsk->split_list,
						struct ipu_task_entry, node);
				list_del(&sp_tsk0->node);

				list_for_each(pos, &tsk->split_list) {
					struct ipu_task_entry *tmp;

					tmp = list_entry(pos,
						struct ipu_task_entry, node);
					tmp->task_in_list = 1;
					dev_dbg(tmp->dev,
						"[0x%p] no-0x%x,id:%d sp_tsk "
						"add_to_list.\n", tmp,
						tmp->task_no, tmp->task_id);
				}
				/* add to global list */
				list_splice(&tsk->split_list, &ipu_task_list);

				spin_unlock_irqrestore(&ipu_task_list_lock,
									flags);
				/* let the parent thread do the first sp_task */
				/* FIXME: ensure the correct sequence for split
					4size: 5/6->9/a*/
				if (!sp_tsk0)
					dev_err(tsk->dev,
					"ERR: no-0x%x,can not get split_tsk0\n",
					tsk->task_no);
				wake_up_interruptible(&thread_waitq);
				get_res_do_task(sp_tsk0);
				dev_dbg(sp_tsk0->dev,
					"thread:%d complete tsk no:0x%x.\n",
					curr_thread_id, sp_tsk0->task_no);
				ret = atomic_read(&req_cnt);
				if (ret > 0) {
					wake_up(&res_waitq);
					dev_dbg(sp_tsk0->dev,
					"sp_tsk0 sche thread:%d no:0x%x,"
					"req_cnt:%d\n", curr_thread_id,
					sp_tsk0->task_no, ret);
					/* For other threads to get_res */
					schedule();
				}
			}
		} else
			get_res_do_task(tsk);

		/* wait for all 4 sp_task finished here or timeout
			and then release all resources */
		if (split_parent && !split_fail)
			wait_split_task_complete(tsk, sp_task, size);

		if (!split_child) {
			atomic_inc(&tsk->done);
			wake_up(&tsk->task_waitq);
		}

		dev_dbg(tsk->dev, "thread:%d complete tsk no:0x%x-[0x%p].\n",
				curr_thread_id, tsk->task_no, tsk);
		ret = atomic_read(&req_cnt);
		if (ret > 0) {
			wake_up(&res_waitq);
			dev_dbg(tsk->dev, "sche thread:%d no:0x%x,req_cnt:%d\n",
				curr_thread_id, tsk->task_no, ret);
			/* note: give cpu to other threads to get_res */
			schedule();
		}

		kref_put(&tsk->refcount, task_mem_free);
	}

	pr_info("ERR %s exit.\n", __func__);
	return 0;
}

int ipu_check_task(struct ipu_task *task)
{
	struct ipu_task_entry *tsk;
	int ret = 0;

	tsk = create_task_entry(task);
	if (IS_ERR(tsk))
		return PTR_ERR(tsk);

	ret = check_task(tsk);

	task->input = tsk->input;
	task->output = tsk->output;
	task->overlay = tsk->overlay;
	dump_task_info(tsk);

	kref_put(&tsk->refcount, task_mem_free);
	if (ret != 0)
		pr_debug("%s ret:%d.\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(ipu_check_task);

int ipu_queue_task(struct ipu_task *task)
{
	struct ipu_task_entry *tsk;
	unsigned long flags;
	int ret;
	u32 tmp_task_no;
	DECLARE_PERF_VAR;

	tsk = create_task_entry(task);
	if (IS_ERR(tsk))
		return PTR_ERR(tsk);

	CHECK_PERF(&tsk->ts_queue);
	ret = prepare_task(tsk);
	if (ret < 0)
		goto done;

	if (need_split(tsk)) {
		CHECK_PERF(&tsk->ts_dotask);
		CHECK_PERF(&tsk->ts_waitirq);
		CHECK_PERF(&tsk->ts_inirq);
		CHECK_PERF(&tsk->ts_wakeup);
	}

	/* task_no last four bits for split task type*/
	tmp_task_no = atomic_inc_return(&frame_no);
	tsk->task_no = tmp_task_no << 4;
	init_waitqueue_head(&tsk->task_waitq);

	spin_lock_irqsave(&ipu_task_list_lock, flags);
	list_add_tail(&tsk->node, &ipu_task_list);
	tsk->task_in_list = 1;
	dev_dbg(tsk->dev, "[0x%p,no-0x%x] list_add_tail\n", tsk, tsk->task_no);
	spin_unlock_irqrestore(&ipu_task_list_lock, flags);
	wake_up_interruptible(&thread_waitq);

	ret = wait_event_timeout(tsk->task_waitq, atomic_read(&tsk->done),
						msecs_to_jiffies(tsk->timeout));
	if (0 == ret) {
		/* note: the timeout should larger than the internal timeout!*/
		ret = -ETIMEDOUT;
		dev_err(tsk->dev, "ERR: [0x%p] no-0x%x, timeout:%dms!\n",
				tsk, tsk->task_no, tsk->timeout);
	} else {
		if (STATE_OK != tsk->state) {
			dev_err(tsk->dev, "ERR: [0x%p] no-0x%x,state %d: %s\n",
				tsk, tsk->task_no, tsk->state,
				state_msg[tsk->state].msg);
			ret = -ECANCELED;
		} else
			ret = 0;
	}

	spin_lock_irqsave(&ipu_task_list_lock, flags);
	if (tsk->task_in_list) {
		list_del(&tsk->node);
		tsk->task_in_list = 0;
		dev_dbg(tsk->dev, "[0x%p] no:0x%x list_del\n",
				tsk, tsk->task_no);
	}
	spin_unlock_irqrestore(&ipu_task_list_lock, flags);

#ifdef DBG_IPU_PERF
	CHECK_PERF(&tsk->ts_rel);
	PRINT_TASK_STATISTICS;
	if (ts_frame_avg == 0)
		ts_frame_avg = ts_frame.tv_nsec / NSEC_PER_USEC +
				ts_frame.tv_sec * USEC_PER_SEC;
	else
		ts_frame_avg = (ts_frame_avg + ts_frame.tv_nsec / NSEC_PER_USEC
				+ ts_frame.tv_sec * USEC_PER_SEC)/2;
	if (timespec_compare(&ts_frame, &ts_frame_max) > 0)
		ts_frame_max = ts_frame;

	atomic_inc(&frame_cnt);

	if ((atomic_read(&frame_cnt) %  1000) == 0)
		pr_debug("ipu_dev: max frame time:%ldus, avg frame time:%dus,"
			"frame_cnt:%d\n", ts_frame_max.tv_nsec / NSEC_PER_USEC
			+ ts_frame_max.tv_sec * USEC_PER_SEC,
			ts_frame_avg, atomic_read(&frame_cnt));
#endif
done:
	if (ret < 0)
		dev_err(tsk->dev, "ERR: no-0x%x,ipu_queue_task err:%d\n",
				tsk->task_no, ret);

	kref_put(&tsk->refcount, task_mem_free);

	return ret;
}
EXPORT_SYMBOL_GPL(ipu_queue_task);

static int mxc_ipu_open(struct inode *inode, struct file *file)
{
	file->private_data = (void *)atomic_inc_return(&file_index);
	return 0;
}

static long mxc_ipu_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	int __user *argp = (void __user *)arg;
	int ret = 0;

	switch (cmd) {
	case IPU_CHECK_TASK:
		{
			struct ipu_task task;

			if (copy_from_user
					(&task, (struct ipu_task *) arg,
					 sizeof(struct ipu_task)))
				return -EFAULT;
			ret = ipu_check_task(&task);
			if (copy_to_user((struct ipu_task *) arg,
				&task, sizeof(struct ipu_task)))
				return -EFAULT;
			break;
		}
	case IPU_QUEUE_TASK:
		{
			struct ipu_task task;

			if (copy_from_user
					(&task, (struct ipu_task *) arg,
					 sizeof(struct ipu_task)))
				return -EFAULT;
			ret = ipu_queue_task(&task);
			break;
		}
	case IPU_ALLOC:
		{
			int size;
			struct ipu_alloc_list *mem;

			mem = kzalloc(sizeof(*mem), GFP_KERNEL);
			if (mem == NULL)
				return -ENOMEM;

			if (get_user(size, argp))
				return -EFAULT;

			mem->size = PAGE_ALIGN(size);

			mem->cpu_addr = dma_alloc_coherent(ipu_dev, size,
							   &mem->phy_addr,
							   GFP_DMA | GFP_KERNEL);
			if (mem->cpu_addr == NULL) {
				kfree(mem);
				return -ENOMEM;
			}
			mem->file_index = file->private_data;
			mutex_lock(&ipu_alloc_lock);
			list_add(&mem->list, &ipu_alloc_list);
			mutex_unlock(&ipu_alloc_lock);

			dev_dbg(ipu_dev, "allocated %d bytes @ 0x%08X\n",
				mem->size, mem->phy_addr);

			if (put_user(mem->phy_addr, argp))
				return -EFAULT;

			break;
		}
	case IPU_FREE:
		{
			unsigned long offset;
			struct ipu_alloc_list *mem;

			if (get_user(offset, argp))
				return -EFAULT;

			ret = -EINVAL;
			mutex_lock(&ipu_alloc_lock);
			list_for_each_entry(mem, &ipu_alloc_list, list) {
				if (mem->phy_addr == offset) {
					list_del(&mem->list);
					dma_free_coherent(ipu_dev,
							  mem->size,
							  mem->cpu_addr,
							  mem->phy_addr);
					kfree(mem);
					ret = 0;
					break;
				}
			}
			mutex_unlock(&ipu_alloc_lock);
			if (0 == ret)
				dev_dbg(ipu_dev, "free %d bytes @ 0x%08X\n",
					mem->size, mem->phy_addr);

			break;
		}
	default:
		break;
	}
	return ret;
}

static int mxc_ipu_mmap(struct file *file, struct vm_area_struct *vma)
{
	bool found = false;
	u32 len;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	struct ipu_alloc_list *mem;

	mutex_lock(&ipu_alloc_lock);
	list_for_each_entry(mem, &ipu_alloc_list, list) {
		if (offset == mem->phy_addr) {
			found = true;
			len = mem->size;
			break;
		}
	}
	mutex_unlock(&ipu_alloc_lock);
	if (!found)
		return -EINVAL;

	if (vma->vm_end - vma->vm_start > len)
		return -EINVAL;

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot)) {
		printk(KERN_ERR
				"mmap failed!\n");
		return -ENOBUFS;
	}
	return 0;
}

static int mxc_ipu_release(struct inode *inode, struct file *file)
{
	struct ipu_alloc_list *mem;
	struct ipu_alloc_list *n;

	mutex_lock(&ipu_alloc_lock);
	list_for_each_entry_safe(mem, n, &ipu_alloc_list, list) {
		if ((mem->cpu_addr != 0) &&
			(file->private_data == mem->file_index)) {
			list_del(&mem->list);
			dma_free_coherent(ipu_dev,
					  mem->size,
					  mem->cpu_addr,
					  mem->phy_addr);
			dev_dbg(ipu_dev, "rel-free %d bytes @ 0x%08X\n",
				mem->size, mem->phy_addr);
			kfree(mem);
		}
	}
	mutex_unlock(&ipu_alloc_lock);
	atomic_dec(&file_index);

	return 0;
}

static struct file_operations mxc_ipu_fops = {
	.owner = THIS_MODULE,
	.open = mxc_ipu_open,
	.mmap = mxc_ipu_mmap,
	.release = mxc_ipu_release,
	.unlocked_ioctl = mxc_ipu_ioctl,
};

int register_ipu_device(struct ipu_soc *ipu, int id)
{
	int ret = 0;
	static int idx;
	static struct ipu_thread_data thread_data[5];

	if (!major) {
		major = register_chrdev(0, "mxc_ipu", &mxc_ipu_fops);
		if (major < 0) {
			printk(KERN_ERR "Unable to register mxc_ipu as a char device\n");
			ret = major;
			goto register_cdev_fail;
		}

		ipu_class = class_create(THIS_MODULE, "mxc_ipu");
		if (IS_ERR(ipu_class)) {
			ret = PTR_ERR(ipu_class);
			goto ipu_class_fail;
		}

		ipu_dev = device_create(ipu_class, NULL, MKDEV(major, 0),
				NULL, "mxc_ipu");
		if (IS_ERR(ipu_dev)) {
			ret = PTR_ERR(ipu_dev);
			goto dev_create_fail;
		}
		ipu_dev->dma_mask = kmalloc(sizeof(*ipu_dev->dma_mask), GFP_KERNEL);
		*ipu_dev->dma_mask = DMA_BIT_MASK(32);
		ipu_dev->coherent_dma_mask = DMA_BIT_MASK(32);

		mutex_init(&ipu_ch_tbl.lock);
	}
	max_ipu_no = ++id;
	ipu->rot_dma[0].size = 0;
	ipu->rot_dma[1].size = 0;

	thread_data[idx].ipu = ipu;
	thread_data[idx].id = 0;
	thread_data[idx].is_vdoa = 0;
	ipu->thread[0] = kthread_run(ipu_task_thread, &thread_data[idx++],
					"ipu%d_task", id);
	if (IS_ERR(ipu->thread[0])) {
		ret = PTR_ERR(ipu->thread[0]);
		goto kthread0_fail;
	}

	thread_data[idx].ipu = ipu;
	thread_data[idx].id = 1;
	thread_data[idx].is_vdoa = 0;
	ipu->thread[1] = kthread_run(ipu_task_thread, &thread_data[idx++],
				"ipu%d_task", id);
	if (IS_ERR(ipu->thread[1])) {
		ret = PTR_ERR(ipu->thread[1]);
		goto kthread1_fail;
	}


	return ret;

kthread1_fail:
	kthread_stop(ipu->thread[0]);
kthread0_fail:
	if (id == 0)
		device_destroy(ipu_class, MKDEV(major, 0));
dev_create_fail:
	if (id == 0) {
		class_destroy(ipu_class);
	}
ipu_class_fail:
	if (id == 0)
		unregister_chrdev(major, "mxc_ipu");
register_cdev_fail:
	return ret;
}

void unregister_ipu_device(struct ipu_soc *ipu, int id)
{
	int i;

	kthread_stop(ipu->thread[0]);
	kthread_stop(ipu->thread[1]);
	for (i = 0; i < 2; i++) {
		if (ipu->rot_dma[i].vaddr)
			dma_free_coherent(ipu_dev,
				ipu->rot_dma[i].size,
				ipu->rot_dma[i].vaddr,
				ipu->rot_dma[i].paddr);
	}

	if (major) {
		device_destroy(ipu_class, MKDEV(major, 0));
		class_destroy(ipu_class);
		unregister_chrdev(major, "mxc_ipu");
		major = 0;
	}
}
