// SPDX-License-Identifier: GPL-2.0-only
/*
 * A framebuffer driver for VBE 2.0+ compliant video cards
 *
 * (c) 2007 Michal Januszewski <spock@gentoo.org>
 *     Loosely based upon the vesafb driver.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/completion.h>
#include <linux/connector.h>
#include <linux/random.h>
#include <linux/platform_device.h>
#include <linux/limits.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <video/edid.h>
#include <video/uvesafb.h>
#ifdef CONFIG_X86
#include <video/vga.h>
#endif
#include "edid.h"

static struct cb_id uvesafb_cn_id = {
	.idx = CN_IDX_V86D,
	.val = CN_VAL_V86D_UVESAFB
};
static char v86d_path[PATH_MAX] = "/sbin/v86d";
static char v86d_started;	/* has v86d been started by uvesafb? */

static const struct fb_fix_screeninfo uvesafb_fix = {
	.id	= "VESA VGA",
	.type	= FB_TYPE_PACKED_PIXELS,
	.accel	= FB_ACCEL_NONE,
	.visual = FB_VISUAL_TRUECOLOR,
};

static int mtrr		= 3;	/* enable mtrr by default */
static bool blank	= true;	/* enable blanking by default */
static int ypan		= 1;	/* 0: scroll, 1: ypan, 2: ywrap */
static bool pmi_setpal	= true; /* use PMI for palette changes */
static bool nocrtc;		/* ignore CRTC settings */
static bool noedid;		/* don't try DDC transfers */
static int vram_remap;		/* set amt. of memory to be used */
static int vram_total;		/* set total amount of memory */
static u16 maxclk;		/* maximum pixel clock */
static u16 maxvf;		/* maximum vertical frequency */
static u16 maxhf;		/* maximum horizontal frequency */
static u16 vbemode;		/* force use of a specific VBE mode */
static char *mode_option;
static u8  dac_width	= 6;

static struct uvesafb_ktask *uvfb_tasks[UVESAFB_TASKS_MAX];
static DEFINE_MUTEX(uvfb_lock);

/*
 * A handler for replies from userspace.
 *
 * Make sure each message passes consistency checks and if it does,
 * find the kernel part of the task struct, copy the registers and
 * the buffer contents and then complete the task.
 */
static void uvesafb_cn_callback(struct cn_msg *msg, struct netlink_skb_parms *nsp)
{
	struct uvesafb_task *utask;
	struct uvesafb_ktask *task;

	if (!capable(CAP_SYS_ADMIN))
		return;

	if (msg->seq >= UVESAFB_TASKS_MAX)
		return;

	mutex_lock(&uvfb_lock);
	task = uvfb_tasks[msg->seq];

	if (!task || msg->ack != task->ack) {
		mutex_unlock(&uvfb_lock);
		return;
	}

	utask = (struct uvesafb_task *)msg->data;

	/* Sanity checks for the buffer length. */
	if (task->t.buf_len < utask->buf_len ||
	    utask->buf_len > msg->len - sizeof(*utask)) {
		mutex_unlock(&uvfb_lock);
		return;
	}

	uvfb_tasks[msg->seq] = NULL;
	mutex_unlock(&uvfb_lock);

	memcpy(&task->t, utask, sizeof(*utask));

	if (task->t.buf_len && task->buf)
		memcpy(task->buf, utask + 1, task->t.buf_len);

	complete(task->done);
	return;
}

static int uvesafb_helper_start(void)
{
	char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/bin",
		NULL,
	};

	char *argv[] = {
		v86d_path,
		NULL,
	};

	return call_usermodehelper(v86d_path, argv, envp, UMH_WAIT_PROC);
}

/*
 * Execute a uvesafb task.
 *
 * Returns 0 if the task is executed successfully.
 *
 * A message sent to the userspace consists of the uvesafb_task
 * struct and (optionally) a buffer. The uvesafb_task struct is
 * a simplified version of uvesafb_ktask (its kernel counterpart)
 * containing only the register values, flags and the length of
 * the buffer.
 *
 * Each message is assigned a sequence number (increased linearly)
 * and a random ack number. The sequence number is used as a key
 * for the uvfb_tasks array which holds pointers to uvesafb_ktask
 * structs for all requests.
 */
static int uvesafb_exec(struct uvesafb_ktask *task)
{
	static int seq;
	struct cn_msg *m;
	int err;
	int len = sizeof(task->t) + task->t.buf_len;

	/*
	 * Check whether the message isn't longer than the maximum
	 * allowed by connector.
	 */
	if (sizeof(*m) + len > CONNECTOR_MAX_MSG_SIZE) {
		pr_warn("message too long (%d), can't execute task\n",
			(int)(sizeof(*m) + len));
		return -E2BIG;
	}

	m = kzalloc(sizeof(*m) + len, GFP_KERNEL);
	if (!m)
		return -ENOMEM;

	init_completion(task->done);

	memcpy(&m->id, &uvesafb_cn_id, sizeof(m->id));
	m->seq = seq;
	m->len = len;
	m->ack = prandom_u32();

	/* uvesafb_task structure */
	memcpy(m + 1, &task->t, sizeof(task->t));

	/* Buffer */
	memcpy((u8 *)(m + 1) + sizeof(task->t), task->buf, task->t.buf_len);

	/*
	 * Save the message ack number so that we can find the kernel
	 * part of this task when a reply is received from userspace.
	 */
	task->ack = m->ack;

	mutex_lock(&uvfb_lock);

	/* If all slots are taken -- bail out. */
	if (uvfb_tasks[seq]) {
		mutex_unlock(&uvfb_lock);
		err = -EBUSY;
		goto out;
	}

	/* Save a pointer to the kernel part of the task struct. */
	uvfb_tasks[seq] = task;
	mutex_unlock(&uvfb_lock);

	err = cn_netlink_send(m, 0, 0, GFP_KERNEL);
	if (err == -ESRCH) {
		/*
		 * Try to start the userspace helper if sending
		 * the request failed the first time.
		 */
		err = uvesafb_helper_start();
		if (err) {
			pr_err("failed to execute %s\n", v86d_path);
			pr_err("make sure that the v86d helper is installed and executable\n");
		} else {
			v86d_started = 1;
			err = cn_netlink_send(m, 0, 0, gfp_any());
			if (err == -ENOBUFS)
				err = 0;
		}
	} else if (err == -ENOBUFS)
		err = 0;

	if (!err && !(task->t.flags & TF_EXIT))
		err = !wait_for_completion_timeout(task->done,
				msecs_to_jiffies(UVESAFB_TIMEOUT));

	mutex_lock(&uvfb_lock);
	uvfb_tasks[seq] = NULL;
	mutex_unlock(&uvfb_lock);

	seq++;
	if (seq >= UVESAFB_TASKS_MAX)
		seq = 0;
out:
	kfree(m);
	return err;
}

/*
 * Free a uvesafb_ktask struct.
 */
static void uvesafb_free(struct uvesafb_ktask *task)
{
	if (task) {
		kfree(task->done);
		kfree(task);
	}
}

/*
 * Prepare a uvesafb_ktask struct to be used again.
 */
static void uvesafb_reset(struct uvesafb_ktask *task)
{
	struct completion *cpl = task->done;

	memset(task, 0, sizeof(*task));
	task->done = cpl;
}

/*
 * Allocate and prepare a uvesafb_ktask struct.
 */
static struct uvesafb_ktask *uvesafb_prep(void)
{
	struct uvesafb_ktask *task;

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (task) {
		task->done = kzalloc(sizeof(*task->done), GFP_KERNEL);
		if (!task->done) {
			kfree(task);
			task = NULL;
		}
	}
	return task;
}

static void uvesafb_setup_var(struct fb_var_screeninfo *var,
		struct fb_info *info, struct vbe_mode_ib *mode)
{
	struct uvesafb_par *par = info->par;

	var->vmode = FB_VMODE_NONINTERLACED;
	var->sync = FB_SYNC_VERT_HIGH_ACT;

	var->xres = mode->x_res;
	var->yres = mode->y_res;
	var->xres_virtual = mode->x_res;
	var->yres_virtual = (par->ypan) ?
			info->fix.smem_len / mode->bytes_per_scan_line :
			mode->y_res;
	var->xoffset = 0;
	var->yoffset = 0;
	var->bits_per_pixel = mode->bits_per_pixel;

	if (var->bits_per_pixel == 15)
		var->bits_per_pixel = 16;

	if (var->bits_per_pixel > 8) {
		var->red.offset    = mode->red_off;
		var->red.length    = mode->red_len;
		var->green.offset  = mode->green_off;
		var->green.length  = mode->green_len;
		var->blue.offset   = mode->blue_off;
		var->blue.length   = mode->blue_len;
		var->transp.offset = mode->rsvd_off;
		var->transp.length = mode->rsvd_len;
	} else {
		var->red.offset    = 0;
		var->green.offset  = 0;
		var->blue.offset   = 0;
		var->transp.offset = 0;

		var->red.length    = 8;
		var->green.length  = 8;
		var->blue.length   = 8;
		var->transp.length = 0;
	}
}

static int uvesafb_vbe_find_mode(struct uvesafb_par *par,
		int xres, int yres, int depth, unsigned char flags)
{
	int i, match = -1, h = 0, d = 0x7fffffff;

	for (i = 0; i < par->vbe_modes_cnt; i++) {
		h = abs(par->vbe_modes[i].x_res - xres) +
		    abs(par->vbe_modes[i].y_res - yres) +
		    abs(depth - par->vbe_modes[i].depth);

		/*
		 * We have an exact match in terms of resolution
		 * and depth.
		 */
		if (h == 0)
			return i;

		if (h < d || (h == d && par->vbe_modes[i].depth > depth)) {
			d = h;
			match = i;
		}
	}
	i = 1;

	if (flags & UVESAFB_EXACT_DEPTH &&
			par->vbe_modes[match].depth != depth)
		i = 0;

	if (flags & UVESAFB_EXACT_RES && d > 24)
		i = 0;

	if (i != 0)
		return match;
	else
		return -1;
}

static u8 *uvesafb_vbe_state_save(struct uvesafb_par *par)
{
	struct uvesafb_ktask *task;
	u8 *state;
	int err;

	if (!par->vbe_state_size)
		return NULL;

	state = kmalloc(par->vbe_state_size, GFP_KERNEL);
	if (!state)
		return ERR_PTR(-ENOMEM);

	task = uvesafb_prep();
	if (!task) {
		kfree(state);
		return NULL;
	}

	task->t.regs.eax = 0x4f04;
	task->t.regs.ecx = 0x000f;
	task->t.regs.edx = 0x0001;
	task->t.flags = TF_BUF_RET | TF_BUF_ESBX;
	task->t.buf_len = par->vbe_state_size;
	task->buf = state;
	err = uvesafb_exec(task);

	if (err || (task->t.regs.eax & 0xffff) != 0x004f) {
		pr_warn("VBE get state call failed (eax=0x%x, err=%d)\n",
			task->t.regs.eax, err);
		kfree(state);
		state = NULL;
	}

	uvesafb_free(task);
	return state;
}

static void uvesafb_vbe_state_restore(struct uvesafb_par *par, u8 *state_buf)
{
	struct uvesafb_ktask *task;
	int err;

	if (!state_buf)
		return;

	task = uvesafb_prep();
	if (!task)
		return;

	task->t.regs.eax = 0x4f04;
	task->t.regs.ecx = 0x000f;
	task->t.regs.edx = 0x0002;
	task->t.buf_len = par->vbe_state_size;
	task->t.flags = TF_BUF_ESBX;
	task->buf = state_buf;

	err = uvesafb_exec(task);
	if (err || (task->t.regs.eax & 0xffff) != 0x004f)
		pr_warn("VBE state restore call failed (eax=0x%x, err=%d)\n",
			task->t.regs.eax, err);

	uvesafb_free(task);
}

static int uvesafb_vbe_getinfo(struct uvesafb_ktask *task,
			       struct uvesafb_par *par)
{
	int err;

	task->t.regs.eax = 0x4f00;
	task->t.flags = TF_VBEIB;
	task->t.buf_len = sizeof(struct vbe_ib);
	task->buf = &par->vbe_ib;
	strncpy(par->vbe_ib.vbe_signature, "VBE2", 4);

	err = uvesafb_exec(task);
	if (err || (task->t.regs.eax & 0xffff) != 0x004f) {
		pr_err("Getting VBE info block failed (eax=0x%x, err=%d)\n",
		       (u32)task->t.regs.eax, err);
		return -EINVAL;
	}

	if (par->vbe_ib.vbe_version < 0x0200) {
		pr_err("Sorry, pre-VBE 2.0 cards are not supported\n");
		return -EINVAL;
	}

	if (!par->vbe_ib.mode_list_ptr) {
		pr_err("Missing mode list!\n");
		return -EINVAL;
	}

	pr_info("");

	/*
	 * Convert string pointers and the mode list pointer into
	 * usable addresses. Print informational messages about the
	 * video adapter and its vendor.
	 */
	if (par->vbe_ib.oem_vendor_name_ptr)
		pr_cont("%s, ",
			((char *)task->buf) + par->vbe_ib.oem_vendor_name_ptr);

	if (par->vbe_ib.oem_product_name_ptr)
		pr_cont("%s, ",
			((char *)task->buf) + par->vbe_ib.oem_product_name_ptr);

	if (par->vbe_ib.oem_product_rev_ptr)
		pr_cont("%s, ",
			((char *)task->buf) + par->vbe_ib.oem_product_rev_ptr);

	if (par->vbe_ib.oem_string_ptr)
		pr_cont("OEM: %s, ",
			((char *)task->buf) + par->vbe_ib.oem_string_ptr);

	pr_cont("VBE v%d.%d\n",
		(par->vbe_ib.vbe_version & 0xff00) >> 8,
		par->vbe_ib.vbe_version & 0xff);

	return 0;
}

static int uvesafb_vbe_getmodes(struct uvesafb_ktask *task,
				struct uvesafb_par *par)
{
	int off = 0, err;
	u16 *mode;

	par->vbe_modes_cnt = 0;

	/* Count available modes. */
	mode = (u16 *) (((u8 *)&par->vbe_ib) + par->vbe_ib.mode_list_ptr);
	while (*mode != 0xffff) {
		par->vbe_modes_cnt++;
		mode++;
	}

	par->vbe_modes = kcalloc(par->vbe_modes_cnt,
				 sizeof(struct vbe_mode_ib),
				 GFP_KERNEL);
	if (!par->vbe_modes)
		return -ENOMEM;

	/* Get info about all available modes. */
	mode = (u16 *) (((u8 *)&par->vbe_ib) + par->vbe_ib.mode_list_ptr);
	while (*mode != 0xffff) {
		struct vbe_mode_ib *mib;

		uvesafb_reset(task);
		task->t.regs.eax = 0x4f01;
		task->t.regs.ecx = (u32) *mode;
		task->t.flags = TF_BUF_RET | TF_BUF_ESDI;
		task->t.buf_len = sizeof(struct vbe_mode_ib);
		task->buf = par->vbe_modes + off;

		err = uvesafb_exec(task);
		if (err || (task->t.regs.eax & 0xffff) != 0x004f) {
			pr_warn("Getting mode info block for mode 0x%x failed (eax=0x%x, err=%d)\n",
				*mode, (u32)task->t.regs.eax, err);
			mode++;
			par->vbe_modes_cnt--;
			continue;
		}

		mib = task->buf;
		mib->mode_id = *mode;

		/*
		 * We only want modes that are supported with the current
		 * hardware configuration, color, graphics and that have
		 * support for the LFB.
		 */
		if ((mib->mode_attr & VBE_MODE_MASK) == VBE_MODE_MASK &&
				 mib->bits_per_pixel >= 8)
			off++;
		else
			par->vbe_modes_cnt--;

		mode++;
		mib->depth = mib->red_len + mib->green_len + mib->blue_len;

		/*
		 * Handle 8bpp modes and modes with broken color component
		 * lengths.
		 */
		if (mib->depth == 0 || (mib->depth == 24 &&
					mib->bits_per_pixel == 32))
			mib->depth = mib->bits_per_pixel;
	}

	if (par->vbe_modes_cnt > 0)
		return 0;
	else
		return -EINVAL;
}

/*
 * The Protected Mode Interface is 32-bit x86 code, so we only run it on
 * x86 and not x86_64.
 */
#ifdef CONFIG_X86_32
static int uvesafb_vbe_getpmi(struct uvesafb_ktask *task,
			      struct uvesafb_par *par)
{
	int i, err;

	uvesafb_reset(task);
	task->t.regs.eax = 0x4f0a;
	task->t.regs.ebx = 0x0;
	err = uvesafb_exec(task);

	if ((task->t.regs.eax & 0xffff) != 0x4f || task->t.regs.es < 0xc000) {
		par->pmi_setpal = par->ypan = 0;
	} else {
		par->pmi_base = (u16 *)phys_to_virt(((u32)task->t.regs.es << 4)
						+ task->t.regs.edi);
		par->pmi_start = (u8 *)par->pmi_base + par->pmi_base[1];
		par->pmi_pal = (u8 *)par->pmi_base + par->pmi_base[2];
		pr_info("protected mode interface info at %04x:%04x\n",
			(u16)task->t.regs.es, (u16)task->t.regs.edi);
		pr_info("pmi: set display start = %p, set palette = %p\n",
			par->pmi_start, par->pmi_pal);

		if (par->pmi_base[3]) {
			pr_info("pmi: ports =");
			for (i = par->pmi_base[3]/2;
					par->pmi_base[i] != 0xffff; i++)
				pr_cont(" %x", par->pmi_base[i]);
			pr_cont("\n");

			if (par->pmi_base[i] != 0xffff) {
				pr_info("can't handle memory requests, pmi disabled\n");
				par->ypan = par->pmi_setpal = 0;
			}
		}
	}
	return 0;
}
#endif /* CONFIG_X86_32 */

/*
 * Check whether a video mode is supported by the Video BIOS and is
 * compatible with the monitor limits.
 */
static int uvesafb_is_valid_mode(struct fb_videomode *mode,
				 struct fb_info *info)
{
	if (info->monspecs.gtf) {
		fb_videomode_to_var(&info->var, mode);
		if (fb_validate_mode(&info->var, info))
			return 0;
	}

	if (uvesafb_vbe_find_mode(info->par, mode->xres, mode->yres, 8,
				UVESAFB_EXACT_RES) == -1)
		return 0;

	return 1;
}

static int uvesafb_vbe_getedid(struct uvesafb_ktask *task, struct fb_info *info)
{
	struct uvesafb_par *par = info->par;
	int err = 0;

	if (noedid || par->vbe_ib.vbe_version < 0x0300)
		return -EINVAL;

	task->t.regs.eax = 0x4f15;
	task->t.regs.ebx = 0;
	task->t.regs.ecx = 0;
	task->t.buf_len = 0;
	task->t.flags = 0;

	err = uvesafb_exec(task);

	if ((task->t.regs.eax & 0xffff) != 0x004f || err)
		return -EINVAL;

	if ((task->t.regs.ebx & 0x3) == 3) {
		pr_info("VBIOS/hardware supports both DDC1 and DDC2 transfers\n");
	} else if ((task->t.regs.ebx & 0x3) == 2) {
		pr_info("VBIOS/hardware supports DDC2 transfers\n");
	} else if ((task->t.regs.ebx & 0x3) == 1) {
		pr_info("VBIOS/hardware supports DDC1 transfers\n");
	} else {
		pr_info("VBIOS/hardware doesn't support DDC transfers\n");
		return -EINVAL;
	}

	task->t.regs.eax = 0x4f15;
	task->t.regs.ebx = 1;
	task->t.regs.ecx = task->t.regs.edx = 0;
	task->t.flags = TF_BUF_RET | TF_BUF_ESDI;
	task->t.buf_len = EDID_LENGTH;
	task->buf = kzalloc(EDID_LENGTH, GFP_KERNEL);
	if (!task->buf)
		return -ENOMEM;

	err = uvesafb_exec(task);

	if ((task->t.regs.eax & 0xffff) == 0x004f && !err) {
		fb_edid_to_monspecs(task->buf, &info->monspecs);

		if (info->monspecs.vfmax && info->monspecs.hfmax) {
			/*
			 * If the maximum pixel clock wasn't specified in
			 * the EDID block, set it to 300 MHz.
			 */
			if (info->monspecs.dclkmax == 0)
				info->monspecs.dclkmax = 300 * 1000000;
			info->monspecs.gtf = 1;
		}
	} else {
		err = -EINVAL;
	}

	kfree(task->buf);
	return err;
}

static void uvesafb_vbe_getmonspecs(struct uvesafb_ktask *task,
				    struct fb_info *info)
{
	struct uvesafb_par *par = info->par;
	int i;

	memset(&info->monspecs, 0, sizeof(info->monspecs));

	/*
	 * If we don't get all necessary data from the EDID block,
	 * mark it as incompatible with the GTF and set nocrtc so
	 * that we always use the default BIOS refresh rate.
	 */
	if (uvesafb_vbe_getedid(task, info)) {
		info->monspecs.gtf = 0;
		par->nocrtc = 1;
	}

	/* Kernel command line overrides. */
	if (maxclk)
		info->monspecs.dclkmax = maxclk * 1000000;
	if (maxvf)
		info->monspecs.vfmax = maxvf;
	if (maxhf)
		info->monspecs.hfmax = maxhf * 1000;

	/*
	 * In case DDC transfers are not supported, the user can provide
	 * monitor limits manually. Lower limits are set to "safe" values.
	 */
	if (info->monspecs.gtf == 0 && maxclk && maxvf && maxhf) {
		info->monspecs.dclkmin = 0;
		info->monspecs.vfmin = 60;
		info->monspecs.hfmin = 29000;
		info->monspecs.gtf = 1;
		par->nocrtc = 0;
	}

	if (info->monspecs.gtf)
		pr_info("monitor limits: vf = %d Hz, hf = %d kHz, clk = %d MHz\n",
			info->monspecs.vfmax,
			(int)(info->monspecs.hfmax / 1000),
			(int)(info->monspecs.dclkmax / 1000000));
	else
		pr_info("no monitor limits have been set, default refresh rate will be used\n");

	/* Add VBE modes to the modelist. */
	for (i = 0; i < par->vbe_modes_cnt; i++) {
		struct fb_var_screeninfo var;
		struct vbe_mode_ib *mode;
		struct fb_videomode vmode;

		mode = &par->vbe_modes[i];
		memset(&var, 0, sizeof(var));

		var.xres = mode->x_res;
		var.yres = mode->y_res;

		fb_get_mode(FB_VSYNCTIMINGS | FB_IGNOREMON, 60, &var, info);
		fb_var_to_videomode(&vmode, &var);
		fb_add_videomode(&vmode, &info->modelist);
	}

	/* Add valid VESA modes to our modelist. */
	for (i = 0; i < VESA_MODEDB_SIZE; i++) {
		if (uvesafb_is_valid_mode((struct fb_videomode *)
						&vesa_modes[i], info))
			fb_add_videomode(&vesa_modes[i], &info->modelist);
	}

	for (i = 0; i < info->monspecs.modedb_len; i++) {
		if (uvesafb_is_valid_mode(&info->monspecs.modedb[i], info))
			fb_add_videomode(&info->monspecs.modedb[i],
					&info->modelist);
	}

	return;
}

static void uvesafb_vbe_getstatesize(struct uvesafb_ktask *task,
				     struct uvesafb_par *par)
{
	int err;

	uvesafb_reset(task);

	/*
	 * Get the VBE state buffer size. We want all available
	 * hardware state data (CL = 0x0f).
	 */
	task->t.regs.eax = 0x4f04;
	task->t.regs.ecx = 0x000f;
	task->t.regs.edx = 0x0000;
	task->t.flags = 0;

	err = uvesafb_exec(task);

	if (err || (task->t.regs.eax & 0xffff) != 0x004f) {
		pr_warn("VBE state buffer size cannot be determined (eax=0x%x, err=%d)\n",
			task->t.regs.eax, err);
		par->vbe_state_size = 0;
		return;
	}

	par->vbe_state_size = 64 * (task->t.regs.ebx & 0xffff);
}

static int uvesafb_vbe_init(struct fb_info *info)
{
	struct uvesafb_ktask *task = NULL;
	struct uvesafb_par *par = info->par;
	int err;

	task = uvesafb_prep();
	if (!task)
		return -ENOMEM;

	err = uvesafb_vbe_getinfo(task, par);
	if (err)
		goto out;

	err = uvesafb_vbe_getmodes(task, par);
	if (err)
		goto out;

	par->nocrtc = nocrtc;
#ifdef CONFIG_X86_32
	par->pmi_setpal = pmi_setpal;
	par->ypan = ypan;

	if (par->pmi_setpal || par->ypan) {
		if (__supported_pte_mask & _PAGE_NX) {
			par->pmi_setpal = par->ypan = 0;
			pr_warn("NX protection is active, better not use the PMI\n");
		} else {
			uvesafb_vbe_getpmi(task, par);
		}
	}
#else
	/* The protected mode interface is not available on non-x86. */
	par->pmi_setpal = par->ypan = 0;
#endif

	INIT_LIST_HEAD(&info->modelist);
	uvesafb_vbe_getmonspecs(task, info);
	uvesafb_vbe_getstatesize(task, par);

out:	uvesafb_free(task);
	return err;
}

static int uvesafb_vbe_init_mode(struct fb_info *info)
{
	struct list_head *pos;
	struct fb_modelist *modelist;
	struct fb_videomode *mode;
	struct uvesafb_par *par = info->par;
	int i, modeid;

	/* Has the user requested a specific VESA mode? */
	if (vbemode) {
		for (i = 0; i < par->vbe_modes_cnt; i++) {
			if (par->vbe_modes[i].mode_id == vbemode) {
				modeid = i;
				uvesafb_setup_var(&info->var, info,
						&par->vbe_modes[modeid]);
				fb_get_mode(FB_VSYNCTIMINGS | FB_IGNOREMON, 60,
						&info->var, info);
				/*
				 * With pixclock set to 0, the default BIOS
				 * timings will be used in set_par().
				 */
				info->var.pixclock = 0;
				goto gotmode;
			}
		}
		pr_info("requested VBE mode 0x%x is unavailable\n", vbemode);
		vbemode = 0;
	}

	/* Count the modes in the modelist */
	i = 0;
	list_for_each(pos, &info->modelist)
		i++;

	/*
	 * Convert the modelist into a modedb so that we can use it with
	 * fb_find_mode().
	 */
	mode = kcalloc(i, sizeof(*mode), GFP_KERNEL);
	if (mode) {
		i = 0;
		list_for_each(pos, &info->modelist) {
			modelist = list_entry(pos, struct fb_modelist, list);
			mode[i] = modelist->mode;
			i++;
		}

		if (!mode_option)
			mode_option = UVESAFB_DEFAULT_MODE;

		i = fb_find_mode(&info->var, info, mode_option, mode, i,
			NULL, 8);

		kfree(mode);
	}

	/* fb_find_mode() failed */
	if (i == 0) {
		info->var.xres = 640;
		info->var.yres = 480;
		mode = (struct fb_videomode *)
				fb_find_best_mode(&info->var, &info->modelist);

		if (mode) {
			fb_videomode_to_var(&info->var, mode);
		} else {
			modeid = par->vbe_modes[0].mode_id;
			uvesafb_setup_var(&info->var, info,
					&par->vbe_modes[modeid]);
			fb_get_mode(FB_VSYNCTIMINGS | FB_IGNOREMON, 60,
					&info->var, info);

			goto gotmode;
		}
	}

	/* Look for a matching VBE mode. */
	modeid = uvesafb_vbe_find_mode(par, info->var.xres, info->var.yres,
			info->var.bits_per_pixel, UVESAFB_EXACT_RES);

	if (modeid == -1)
		return -EINVAL;

	uvesafb_setup_var(&info->var, info, &par->vbe_modes[modeid]);

gotmode:
	/*
	 * If we are not VBE3.0+ compliant, we're done -- the BIOS will
	 * ignore our timings anyway.
	 */
	if (par->vbe_ib.vbe_version < 0x0300 || par->nocrtc)
		fb_get_mode(FB_VSYNCTIMINGS | FB_IGNOREMON, 60,
					&info->var, info);

	return modeid;
}

static int uvesafb_setpalette(struct uvesafb_pal_entry *entries, int count,
		int start, struct fb_info *info)
{
	struct uvesafb_ktask *task;
#ifdef CONFIG_X86
	struct uvesafb_par *par = info->par;
	int i = par->mode_idx;
#endif
	int err = 0;

	/*
	 * We support palette modifications for 8 bpp modes only, so
	 * there can never be more than 256 entries.
	 */
	if (start + count > 256)
		return -EINVAL;

#ifdef CONFIG_X86
	/* Use VGA registers if mode is VGA-compatible. */
	if (i >= 0 && i < par->vbe_modes_cnt &&
	    par->vbe_modes[i].mode_attr & VBE_MODE_VGACOMPAT) {
		for (i = 0; i < count; i++) {
			outb_p(start + i,        dac_reg);
			outb_p(entries[i].red,   dac_val);
			outb_p(entries[i].green, dac_val);
			outb_p(entries[i].blue,  dac_val);
		}
	}
#ifdef CONFIG_X86_32
	else if (par->pmi_setpal) {
		__asm__ __volatile__(
		"call *(%%esi)"
		: /* no return value */
		: "a" (0x4f09),         /* EAX */
		  "b" (0),              /* EBX */
		  "c" (count),          /* ECX */
		  "d" (start),          /* EDX */
		  "D" (entries),        /* EDI */
		  "S" (&par->pmi_pal)); /* ESI */
	}
#endif /* CONFIG_X86_32 */
	else
#endif /* CONFIG_X86 */
	{
		task = uvesafb_prep();
		if (!task)
			return -ENOMEM;

		task->t.regs.eax = 0x4f09;
		task->t.regs.ebx = 0x0;
		task->t.regs.ecx = count;
		task->t.regs.edx = start;
		task->t.flags = TF_BUF_ESDI;
		task->t.buf_len = sizeof(struct uvesafb_pal_entry) * count;
		task->buf = entries;

		err = uvesafb_exec(task);
		if ((task->t.regs.eax & 0xffff) != 0x004f)
			err = 1;

		uvesafb_free(task);
	}
	return err;
}

static int uvesafb_setcolreg(unsigned regno, unsigned red, unsigned green,
		unsigned blue, unsigned transp,
		struct fb_info *info)
{
	struct uvesafb_pal_entry entry;
	int shift = 16 - dac_width;
	int err = 0;

	if (regno >= info->cmap.len)
		return -EINVAL;

	if (info->var.bits_per_pixel == 8) {
		entry.red   = red   >> shift;
		entry.green = green >> shift;
		entry.blue  = blue  >> shift;
		entry.pad   = 0;

		err = uvesafb_setpalette(&entry, 1, regno, info);
	} else if (regno < 16) {
		switch (info->var.bits_per_pixel) {
		case 16:
			if (info->var.red.offset == 10) {
				/* 1:5:5:5 */
				((u32 *) (info->pseudo_palette))[regno] =
						((red   & 0xf800) >>  1) |
						((green & 0xf800) >>  6) |
						((blue  & 0xf800) >> 11);
			} else {
				/* 0:5:6:5 */
				((u32 *) (info->pseudo_palette))[regno] =
						((red   & 0xf800)      ) |
						((green & 0xfc00) >>  5) |
						((blue  & 0xf800) >> 11);
			}
			break;

		case 24:
		case 32:
			red   >>= 8;
			green >>= 8;
			blue  >>= 8;
			((u32 *)(info->pseudo_palette))[regno] =
				(red   << info->var.red.offset)   |
				(green << info->var.green.offset) |
				(blue  << info->var.blue.offset);
			break;
		}
	}
	return err;
}

static int uvesafb_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	struct uvesafb_pal_entry *entries;
	int shift = 16 - dac_width;
	int i, err = 0;

	if (info->var.bits_per_pixel == 8) {
		if (cmap->start + cmap->len > info->cmap.start +
		    info->cmap.len || cmap->start < info->cmap.start)
			return -EINVAL;

		entries = kmalloc_array(cmap->len, sizeof(*entries),
					GFP_KERNEL);
		if (!entries)
			return -ENOMEM;

		for (i = 0; i < cmap->len; i++) {
			entries[i].red   = cmap->red[i]   >> shift;
			entries[i].green = cmap->green[i] >> shift;
			entries[i].blue  = cmap->blue[i]  >> shift;
			entries[i].pad   = 0;
		}
		err = uvesafb_setpalette(entries, cmap->len, cmap->start, info);
		kfree(entries);
	} else {
		/*
		 * For modes with bpp > 8, we only set the pseudo palette in
		 * the fb_info struct. We rely on uvesafb_setcolreg to do all
		 * sanity checking.
		 */
		for (i = 0; i < cmap->len; i++) {
			err |= uvesafb_setcolreg(cmap->start + i, cmap->red[i],
						cmap->green[i], cmap->blue[i],
						0, info);
		}
	}
	return err;
}

static int uvesafb_pan_display(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
#ifdef CONFIG_X86_32
	int offset;
	struct uvesafb_par *par = info->par;

	offset = (var->yoffset * info->fix.line_length + var->xoffset) / 4;

	/*
	 * It turns out it's not the best idea to do panning via vm86,
	 * so we only allow it if we have a PMI.
	 */
	if (par->pmi_start) {
		__asm__ __volatile__(
			"call *(%%edi)"
			: /* no return value */
			: "a" (0x4f07),         /* EAX */
			  "b" (0),              /* EBX */
			  "c" (offset),         /* ECX */
			  "d" (offset >> 16),   /* EDX */
			  "D" (&par->pmi_start));    /* EDI */
	}
#endif
	return 0;
}

static int uvesafb_blank(int blank, struct fb_info *info)
{
	struct uvesafb_ktask *task;
	int err = 1;
#ifdef CONFIG_X86
	struct uvesafb_par *par = info->par;

	if (par->vbe_ib.capabilities & VBE_CAP_VGACOMPAT) {
		int loop = 10000;
		u8 seq = 0, crtc17 = 0;

		if (blank == FB_BLANK_POWERDOWN) {
			seq = 0x20;
			crtc17 = 0x00;
			err = 0;
		} else {
			seq = 0x00;
			crtc17 = 0x80;
			err = (blank == FB_BLANK_UNBLANK) ? 0 : -EINVAL;
		}

		vga_wseq(NULL, 0x00, 0x01);
		seq |= vga_rseq(NULL, 0x01) & ~0x20;
		vga_wseq(NULL, 0x00, seq);

		crtc17 |= vga_rcrt(NULL, 0x17) & ~0x80;
		while (loop--);
		vga_wcrt(NULL, 0x17, crtc17);
		vga_wseq(NULL, 0x00, 0x03);
	} else
#endif /* CONFIG_X86 */
	{
		task = uvesafb_prep();
		if (!task)
			return -ENOMEM;

		task->t.regs.eax = 0x4f10;
		switch (blank) {
		case FB_BLANK_UNBLANK:
			task->t.regs.ebx = 0x0001;
			break;
		case FB_BLANK_NORMAL:
			task->t.regs.ebx = 0x0101;	/* standby */
			break;
		case FB_BLANK_POWERDOWN:
			task->t.regs.ebx = 0x0401;	/* powerdown */
			break;
		default:
			goto out;
		}

		err = uvesafb_exec(task);
		if (err || (task->t.regs.eax & 0xffff) != 0x004f)
			err = 1;
out:		uvesafb_free(task);
	}
	return err;
}

static int uvesafb_open(struct fb_info *info, int user)
{
	struct uvesafb_par *par = info->par;
	int cnt = atomic_read(&par->ref_count);
	u8 *buf = NULL;

	if (!cnt && par->vbe_state_size) {
		buf =  uvesafb_vbe_state_save(par);
		if (IS_ERR(buf)) {
			pr_warn("save hardware state failed, error code is %ld!\n",
				PTR_ERR(buf));
		} else {
			par->vbe_state_orig = buf;
		}
	}

	atomic_inc(&par->ref_count);
	return 0;
}

static int uvesafb_release(struct fb_info *info, int user)
{
	struct uvesafb_ktask *task = NULL;
	struct uvesafb_par *par = info->par;
	int cnt = atomic_read(&par->ref_count);

	if (!cnt)
		return -EINVAL;

	if (cnt != 1)
		goto out;

	task = uvesafb_prep();
	if (!task)
		goto out;

	/* First, try to set the standard 80x25 text mode. */
	task->t.regs.eax = 0x0003;
	uvesafb_exec(task);

	/*
	 * Now try to restore whatever hardware state we might have
	 * saved when the fb device was first opened.
	 */
	uvesafb_vbe_state_restore(par, par->vbe_state_orig);
out:
	atomic_dec(&par->ref_count);
	uvesafb_free(task);
	return 0;
}

static int uvesafb_set_par(struct fb_info *info)
{
	struct uvesafb_par *par = info->par;
	struct uvesafb_ktask *task = NULL;
	struct vbe_crtc_ib *crtc = NULL;
	struct vbe_mode_ib *mode = NULL;
	int i, err = 0, depth = info->var.bits_per_pixel;

	if (depth > 8 && depth != 32)
		depth = info->var.red.length + info->var.green.length +
			info->var.blue.length;

	i = uvesafb_vbe_find_mode(par, info->var.xres, info->var.yres, depth,
				 UVESAFB_EXACT_RES | UVESAFB_EXACT_DEPTH);
	if (i >= 0)
		mode = &par->vbe_modes[i];
	else
		return -EINVAL;

	task = uvesafb_prep();
	if (!task)
		return -ENOMEM;
setmode:
	task->t.regs.eax = 0x4f02;
	task->t.regs.ebx = mode->mode_id | 0x4000;	/* use LFB */

	if (par->vbe_ib.vbe_version >= 0x0300 && !par->nocrtc &&
	    info->var.pixclock != 0) {
		task->t.regs.ebx |= 0x0800;		/* use CRTC data */
		task->t.flags = TF_BUF_ESDI;
		crtc = kzalloc(sizeof(struct vbe_crtc_ib), GFP_KERNEL);
		if (!crtc) {
			err = -ENOMEM;
			goto out;
		}
		crtc->horiz_start = info->var.xres + info->var.right_margin;
		crtc->horiz_end	  = crtc->horiz_start + info->var.hsync_len;
		crtc->horiz_total = crtc->horiz_end + info->var.left_margin;

		crtc->vert_start  = info->var.yres + info->var.lower_margin;
		crtc->vert_end    = crtc->vert_start + info->var.vsync_len;
		crtc->vert_total  = crtc->vert_end + info->var.upper_margin;

		crtc->pixel_clock = PICOS2KHZ(info->var.pixclock) * 1000;
		crtc->refresh_rate = (u16)(100 * (crtc->pixel_clock /
				(crtc->vert_total * crtc->horiz_total)));

		if (info->var.vmode & FB_VMODE_DOUBLE)
			crtc->flags |= 0x1;
		if (info->var.vmode & FB_VMODE_INTERLACED)
			crtc->flags |= 0x2;
		if (!(info->var.sync & FB_SYNC_HOR_HIGH_ACT))
			crtc->flags |= 0x4;
		if (!(info->var.sync & FB_SYNC_VERT_HIGH_ACT))
			crtc->flags |= 0x8;
		memcpy(&par->crtc, crtc, sizeof(*crtc));
	} else {
		memset(&par->crtc, 0, sizeof(*crtc));
	}

	task->t.buf_len = sizeof(struct vbe_crtc_ib);
	task->buf = &par->crtc;

	err = uvesafb_exec(task);
	if (err || (task->t.regs.eax & 0xffff) != 0x004f) {
		/*
		 * The mode switch might have failed because we tried to
		 * use our own timings.  Try again with the default timings.
		 */
		if (crtc != NULL) {
			pr_warn("mode switch failed (eax=0x%x, err=%d) - trying again with default timings\n",
				task->t.regs.eax, err);
			uvesafb_reset(task);
			kfree(crtc);
			crtc = NULL;
			info->var.pixclock = 0;
			goto setmode;
		} else {
			pr_err("mode switch failed (eax=0x%x, err=%d)\n",
			       task->t.regs.eax, err);
			err = -EINVAL;
			goto out;
		}
	}
	par->mode_idx = i;

	/* For 8bpp modes, always try to set the DAC to 8 bits. */
	if (par->vbe_ib.capabilities & VBE_CAP_CAN_SWITCH_DAC &&
	    mode->bits_per_pixel <= 8) {
		uvesafb_reset(task);
		task->t.regs.eax = 0x4f08;
		task->t.regs.ebx = 0x0800;

		err = uvesafb_exec(task);
		if (err || (task->t.regs.eax & 0xffff) != 0x004f ||
		    ((task->t.regs.ebx & 0xff00) >> 8) != 8) {
			dac_width = 6;
		} else {
			dac_width = 8;
		}
	}

	info->fix.visual = (info->var.bits_per_pixel == 8) ?
				FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	info->fix.line_length = mode->bytes_per_scan_line;

out:
	kfree(crtc);
	uvesafb_free(task);

	return err;
}

static void uvesafb_check_limits(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	const struct fb_videomode *mode;
	struct uvesafb_par *par = info->par;

	/*
	 * If pixclock is set to 0, then we're using default BIOS timings
	 * and thus don't have to perform any checks here.
	 */
	if (!var->pixclock)
		return;

	if (par->vbe_ib.vbe_version < 0x0300) {
		fb_get_mode(FB_VSYNCTIMINGS | FB_IGNOREMON, 60, var, info);
		return;
	}

	if (!fb_validate_mode(var, info))
		return;

	mode = fb_find_best_mode(var, &info->modelist);
	if (mode) {
		if (mode->xres == var->xres && mode->yres == var->yres &&
		    !(mode->vmode & (FB_VMODE_INTERLACED | FB_VMODE_DOUBLE))) {
			fb_videomode_to_var(var, mode);
			return;
		}
	}

	if (info->monspecs.gtf && !fb_get_mode(FB_MAXTIMINGS, 0, var, info))
		return;
	/* Use default refresh rate */
	var->pixclock = 0;
}

static int uvesafb_check_var(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	struct uvesafb_par *par = info->par;
	struct vbe_mode_ib *mode = NULL;
	int match = -1;
	int depth = var->red.length + var->green.length + var->blue.length;

	/*
	 * Various apps will use bits_per_pixel to set the color depth,
	 * which is theoretically incorrect, but which we'll try to handle
	 * here.
	 */
	if (depth == 0 || abs(depth - var->bits_per_pixel) >= 8)
		depth = var->bits_per_pixel;

	match = uvesafb_vbe_find_mode(par, var->xres, var->yres, depth,
						UVESAFB_EXACT_RES);
	if (match == -1)
		return -EINVAL;

	mode = &par->vbe_modes[match];
	uvesafb_setup_var(var, info, mode);

	/*
	 * Check whether we have remapped enough memory for this mode.
	 * We might be called at an early stage, when we haven't remapped
	 * any memory yet, in which case we simply skip the check.
	 */
	if (var->yres * mode->bytes_per_scan_line > info->fix.smem_len
						&& info->fix.smem_len)
		return -EINVAL;

	if ((var->vmode & FB_VMODE_DOUBLE) &&
				!(par->vbe_modes[match].mode_attr & 0x100))
		var->vmode &= ~FB_VMODE_DOUBLE;

	if ((var->vmode & FB_VMODE_INTERLACED) &&
				!(par->vbe_modes[match].mode_attr & 0x200))
		var->vmode &= ~FB_VMODE_INTERLACED;

	uvesafb_check_limits(var, info);

	var->xres_virtual = var->xres;
	var->yres_virtual = (par->ypan) ?
				info->fix.smem_len / mode->bytes_per_scan_line :
				var->yres;
	return 0;
}

static struct fb_ops uvesafb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= uvesafb_open,
	.fb_release	= uvesafb_release,
	.fb_setcolreg	= uvesafb_setcolreg,
	.fb_setcmap	= uvesafb_setcmap,
	.fb_pan_display	= uvesafb_pan_display,
	.fb_blank	= uvesafb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_check_var	= uvesafb_check_var,
	.fb_set_par	= uvesafb_set_par,
};

static void uvesafb_init_info(struct fb_info *info, struct vbe_mode_ib *mode)
{
	unsigned int size_vmode;
	unsigned int size_remap;
	unsigned int size_total;
	struct uvesafb_par *par = info->par;
	int i, h;

	info->pseudo_palette = ((u8 *)info->par + sizeof(struct uvesafb_par));
	info->fix = uvesafb_fix;
	info->fix.ypanstep = par->ypan ? 1 : 0;
	info->fix.ywrapstep = (par->ypan > 1) ? 1 : 0;

	/* Disable blanking if the user requested so. */
	if (!blank)
		uvesafb_ops.fb_blank = NULL;

	/*
	 * Find out how much IO memory is required for the mode with
	 * the highest resolution.
	 */
	size_remap = 0;
	for (i = 0; i < par->vbe_modes_cnt; i++) {
		h = par->vbe_modes[i].bytes_per_scan_line *
					par->vbe_modes[i].y_res;
		if (h > size_remap)
			size_remap = h;
	}
	size_remap *= 2;

	/*
	 *   size_vmode -- that is the amount of memory needed for the
	 *                 used video mode, i.e. the minimum amount of
	 *                 memory we need.
	 */
	size_vmode = info->var.yres * mode->bytes_per_scan_line;

	/*
	 *   size_total -- all video memory we have. Used for mtrr
	 *                 entries, resource allocation and bounds
	 *                 checking.
	 */
	size_total = par->vbe_ib.total_memory * 65536;
	if (vram_total)
		size_total = vram_total * 1024 * 1024;
	if (size_total < size_vmode)
		size_total = size_vmode;

	/*
	 *   size_remap -- the amount of video memory we are going to
	 *                 use for vesafb.  With modern cards it is no
	 *                 option to simply use size_total as th
	 *                 wastes plenty of kernel address space.
	 */
	if (vram_remap)
		size_remap = vram_remap * 1024 * 1024;
	if (size_remap < size_vmode)
		size_remap = size_vmode;
	if (size_remap > size_total)
		size_remap = size_total;

	info->fix.smem_len = size_remap;
	info->fix.smem_start = mode->phys_base_ptr;

	/*
	 * We have to set yres_virtual here because when setup_var() was
	 * called, smem_len wasn't defined yet.
	 */
	info->var.yres_virtual = info->fix.smem_len /
				 mode->bytes_per_scan_line;

	if (par->ypan && info->var.yres_virtual > info->var.yres) {
		pr_info("scrolling: %s using protected mode interface, yres_virtual=%d\n",
			(par->ypan > 1) ? "ywrap" : "ypan",
			info->var.yres_virtual);
	} else {
		pr_info("scrolling: redraw\n");
		info->var.yres_virtual = info->var.yres;
		par->ypan = 0;
	}

	info->flags = FBINFO_FLAG_DEFAULT |
			(par->ypan ? FBINFO_HWACCEL_YPAN : 0);

	if (!par->ypan)
		uvesafb_ops.fb_pan_display = NULL;
}

static void uvesafb_init_mtrr(struct fb_info *info)
{
	struct uvesafb_par *par = info->par;

	if (mtrr && !(info->fix.smem_start & (PAGE_SIZE - 1))) {
		int temp_size = info->fix.smem_len;

		int rc;

		/* Find the largest power-of-two */
		temp_size = roundup_pow_of_two(temp_size);

		/* Try and find a power of two to add */
		do {
			rc = arch_phys_wc_add(info->fix.smem_start, temp_size);
			temp_size >>= 1;
		} while (temp_size >= PAGE_SIZE && rc == -EINVAL);

		if (rc >= 0)
			par->mtrr_handle = rc;
	}
}

static void uvesafb_ioremap(struct fb_info *info)
{
	info->screen_base = ioremap_wc(info->fix.smem_start, info->fix.smem_len);
}

static ssize_t uvesafb_show_vbe_ver(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct uvesafb_par *par = info->par;

	return snprintf(buf, PAGE_SIZE, "%.4x\n", par->vbe_ib.vbe_version);
}

static DEVICE_ATTR(vbe_version, S_IRUGO, uvesafb_show_vbe_ver, NULL);

static ssize_t uvesafb_show_vbe_modes(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct uvesafb_par *par = info->par;
	int ret = 0, i;

	for (i = 0; i < par->vbe_modes_cnt && ret < PAGE_SIZE; i++) {
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"%dx%d-%d, 0x%.4x\n",
			par->vbe_modes[i].x_res, par->vbe_modes[i].y_res,
			par->vbe_modes[i].depth, par->vbe_modes[i].mode_id);
	}

	return ret;
}

static DEVICE_ATTR(vbe_modes, S_IRUGO, uvesafb_show_vbe_modes, NULL);

static ssize_t uvesafb_show_vendor(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct uvesafb_par *par = info->par;

	if (par->vbe_ib.oem_vendor_name_ptr)
		return snprintf(buf, PAGE_SIZE, "%s\n", (char *)
			(&par->vbe_ib) + par->vbe_ib.oem_vendor_name_ptr);
	else
		return 0;
}

static DEVICE_ATTR(oem_vendor, S_IRUGO, uvesafb_show_vendor, NULL);

static ssize_t uvesafb_show_product_name(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct uvesafb_par *par = info->par;

	if (par->vbe_ib.oem_product_name_ptr)
		return snprintf(buf, PAGE_SIZE, "%s\n", (char *)
			(&par->vbe_ib) + par->vbe_ib.oem_product_name_ptr);
	else
		return 0;
}

static DEVICE_ATTR(oem_product_name, S_IRUGO, uvesafb_show_product_name, NULL);

static ssize_t uvesafb_show_product_rev(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct uvesafb_par *par = info->par;

	if (par->vbe_ib.oem_product_rev_ptr)
		return snprintf(buf, PAGE_SIZE, "%s\n", (char *)
			(&par->vbe_ib) + par->vbe_ib.oem_product_rev_ptr);
	else
		return 0;
}

static DEVICE_ATTR(oem_product_rev, S_IRUGO, uvesafb_show_product_rev, NULL);

static ssize_t uvesafb_show_oem_string(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct uvesafb_par *par = info->par;

	if (par->vbe_ib.oem_string_ptr)
		return snprintf(buf, PAGE_SIZE, "%s\n",
			(char *)(&par->vbe_ib) + par->vbe_ib.oem_string_ptr);
	else
		return 0;
}

static DEVICE_ATTR(oem_string, S_IRUGO, uvesafb_show_oem_string, NULL);

static ssize_t uvesafb_show_nocrtc(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct uvesafb_par *par = info->par;

	return snprintf(buf, PAGE_SIZE, "%d\n", par->nocrtc);
}

static ssize_t uvesafb_store_nocrtc(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct uvesafb_par *par = info->par;

	if (count > 0) {
		if (buf[0] == '0')
			par->nocrtc = 0;
		else
			par->nocrtc = 1;
	}
	return count;
}

static DEVICE_ATTR(nocrtc, S_IRUGO | S_IWUSR, uvesafb_show_nocrtc,
			uvesafb_store_nocrtc);

static struct attribute *uvesafb_dev_attrs[] = {
	&dev_attr_vbe_version.attr,
	&dev_attr_vbe_modes.attr,
	&dev_attr_oem_vendor.attr,
	&dev_attr_oem_product_name.attr,
	&dev_attr_oem_product_rev.attr,
	&dev_attr_oem_string.attr,
	&dev_attr_nocrtc.attr,
	NULL,
};

static const struct attribute_group uvesafb_dev_attgrp = {
	.name = NULL,
	.attrs = uvesafb_dev_attrs,
};

static int uvesafb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	struct vbe_mode_ib *mode = NULL;
	struct uvesafb_par *par;
	int err = 0, i;

	info = framebuffer_alloc(sizeof(*par) +	sizeof(u32) * 256, &dev->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;

	err = uvesafb_vbe_init(info);
	if (err) {
		pr_err("vbe_init() failed with %d\n", err);
		goto out;
	}

	info->fbops = &uvesafb_ops;

	i = uvesafb_vbe_init_mode(info);
	if (i < 0) {
		err = -EINVAL;
		goto out;
	} else {
		mode = &par->vbe_modes[i];
	}

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0) {
		err = -ENXIO;
		goto out;
	}

	uvesafb_init_info(info, mode);

	if (!request_region(0x3c0, 32, "uvesafb")) {
		pr_err("request region 0x3c0-0x3e0 failed\n");
		err = -EIO;
		goto out_mode;
	}

	if (!request_mem_region(info->fix.smem_start, info->fix.smem_len,
				"uvesafb")) {
		pr_err("cannot reserve video memory at 0x%lx\n",
		       info->fix.smem_start);
		err = -EIO;
		goto out_reg;
	}

	uvesafb_init_mtrr(info);
	uvesafb_ioremap(info);

	if (!info->screen_base) {
		pr_err("abort, cannot ioremap 0x%x bytes of video memory at 0x%lx\n",
		       info->fix.smem_len, info->fix.smem_start);
		err = -EIO;
		goto out_mem;
	}

	platform_set_drvdata(dev, info);

	if (register_framebuffer(info) < 0) {
		pr_err("failed to register framebuffer device\n");
		err = -EINVAL;
		goto out_unmap;
	}

	pr_info("framebuffer at 0x%lx, mapped to 0x%p, using %dk, total %dk\n",
		info->fix.smem_start, info->screen_base,
		info->fix.smem_len / 1024, par->vbe_ib.total_memory * 64);
	fb_info(info, "%s frame buffer device\n", info->fix.id);

	err = sysfs_create_group(&dev->dev.kobj, &uvesafb_dev_attgrp);
	if (err != 0)
		fb_warn(info, "failed to register attributes\n");

	return 0;

out_unmap:
	iounmap(info->screen_base);
out_mem:
	release_mem_region(info->fix.smem_start, info->fix.smem_len);
out_reg:
	release_region(0x3c0, 32);
out_mode:
	if (!list_empty(&info->modelist))
		fb_destroy_modelist(&info->modelist);
	fb_destroy_modedb(info->monspecs.modedb);
	fb_dealloc_cmap(&info->cmap);
out:
	kfree(par->vbe_modes);

	framebuffer_release(info);
	return err;
}

static int uvesafb_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);

	if (info) {
		struct uvesafb_par *par = info->par;

		sysfs_remove_group(&dev->dev.kobj, &uvesafb_dev_attgrp);
		unregister_framebuffer(info);
		release_region(0x3c0, 32);
		iounmap(info->screen_base);
		arch_phys_wc_del(par->mtrr_handle);
		release_mem_region(info->fix.smem_start, info->fix.smem_len);
		fb_destroy_modedb(info->monspecs.modedb);
		fb_dealloc_cmap(&info->cmap);

		kfree(par->vbe_modes);
		kfree(par->vbe_state_orig);
		kfree(par->vbe_state_saved);

		framebuffer_release(info);
	}
	return 0;
}

static struct platform_driver uvesafb_driver = {
	.probe  = uvesafb_probe,
	.remove = uvesafb_remove,
	.driver = {
		.name = "uvesafb",
	},
};

static struct platform_device *uvesafb_device;

#ifndef MODULE
static int uvesafb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt) continue;

		if (!strcmp(this_opt, "redraw"))
			ypan = 0;
		else if (!strcmp(this_opt, "ypan"))
			ypan = 1;
		else if (!strcmp(this_opt, "ywrap"))
			ypan = 2;
		else if (!strcmp(this_opt, "vgapal"))
			pmi_setpal = false;
		else if (!strcmp(this_opt, "pmipal"))
			pmi_setpal = true;
		else if (!strncmp(this_opt, "mtrr:", 5))
			mtrr = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strcmp(this_opt, "nomtrr"))
			mtrr = 0;
		else if (!strcmp(this_opt, "nocrtc"))
			nocrtc = true;
		else if (!strcmp(this_opt, "noedid"))
			noedid = true;
		else if (!strcmp(this_opt, "noblank"))
			blank = false;
		else if (!strncmp(this_opt, "vtotal:", 7))
			vram_total = simple_strtoul(this_opt + 7, NULL, 0);
		else if (!strncmp(this_opt, "vremap:", 7))
			vram_remap = simple_strtoul(this_opt + 7, NULL, 0);
		else if (!strncmp(this_opt, "maxhf:", 6))
			maxhf = simple_strtoul(this_opt + 6, NULL, 0);
		else if (!strncmp(this_opt, "maxvf:", 6))
			maxvf = simple_strtoul(this_opt + 6, NULL, 0);
		else if (!strncmp(this_opt, "maxclk:", 7))
			maxclk = simple_strtoul(this_opt + 7, NULL, 0);
		else if (!strncmp(this_opt, "vbemode:", 8))
			vbemode = simple_strtoul(this_opt + 8, NULL, 0);
		else if (this_opt[0] >= '0' && this_opt[0] <= '9') {
			mode_option = this_opt;
		} else {
			pr_warn("unrecognized option %s\n", this_opt);
		}
	}

	if (mtrr != 3 && mtrr != 0)
		pr_warn("uvesafb: mtrr should be set to 0 or 3; %d is unsupported", mtrr);

	return 0;
}
#endif /* !MODULE */

static ssize_t v86d_show(struct device_driver *dev, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", v86d_path);
}

static ssize_t v86d_store(struct device_driver *dev, const char *buf,
		size_t count)
{
	strncpy(v86d_path, buf, PATH_MAX);
	return count;
}
static DRIVER_ATTR_RW(v86d);

static int uvesafb_init(void)
{
	int err;

#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("uvesafb", &option))
		return -ENODEV;
	uvesafb_setup(option);
#endif
	err = cn_add_callback(&uvesafb_cn_id, "uvesafb", uvesafb_cn_callback);
	if (err)
		return err;

	err = platform_driver_register(&uvesafb_driver);

	if (!err) {
		uvesafb_device = platform_device_alloc("uvesafb", 0);
		if (uvesafb_device)
			err = platform_device_add(uvesafb_device);
		else
			err = -ENOMEM;

		if (err) {
			platform_device_put(uvesafb_device);
			platform_driver_unregister(&uvesafb_driver);
			cn_del_callback(&uvesafb_cn_id);
			return err;
		}

		err = driver_create_file(&uvesafb_driver.driver,
				&driver_attr_v86d);
		if (err) {
			pr_warn("failed to register attributes\n");
			err = 0;
		}
	}
	return err;
}

module_init(uvesafb_init);

static void uvesafb_exit(void)
{
	struct uvesafb_ktask *task;

	if (v86d_started) {
		task = uvesafb_prep();
		if (task) {
			task->t.flags = TF_EXIT;
			uvesafb_exec(task);
			uvesafb_free(task);
		}
	}

	cn_del_callback(&uvesafb_cn_id);
	driver_remove_file(&uvesafb_driver.driver, &driver_attr_v86d);
	platform_device_unregister(uvesafb_device);
	platform_driver_unregister(&uvesafb_driver);
}

module_exit(uvesafb_exit);

static int param_set_scroll(const char *val, const struct kernel_param *kp)
{
	ypan = 0;

	if (!strcmp(val, "redraw"))
		ypan = 0;
	else if (!strcmp(val, "ypan"))
		ypan = 1;
	else if (!strcmp(val, "ywrap"))
		ypan = 2;
	else
		return -EINVAL;

	return 0;
}
static const struct kernel_param_ops param_ops_scroll = {
	.set = param_set_scroll,
};
#define param_check_scroll(name, p) __param_check(name, p, void)

module_param_named(scroll, ypan, scroll, 0);
MODULE_PARM_DESC(scroll,
	"Scrolling mode, set to 'redraw', 'ypan', or 'ywrap'");
module_param_named(vgapal, pmi_setpal, invbool, 0);
MODULE_PARM_DESC(vgapal, "Set palette using VGA registers");
module_param_named(pmipal, pmi_setpal, bool, 0);
MODULE_PARM_DESC(pmipal, "Set palette using PMI calls");
module_param(mtrr, uint, 0);
MODULE_PARM_DESC(mtrr,
	"Memory Type Range Registers setting. Use 0 to disable.");
module_param(blank, bool, 0);
MODULE_PARM_DESC(blank, "Enable hardware blanking");
module_param(nocrtc, bool, 0);
MODULE_PARM_DESC(nocrtc, "Ignore CRTC timings when setting modes");
module_param(noedid, bool, 0);
MODULE_PARM_DESC(noedid,
	"Ignore EDID-provided monitor limits when setting modes");
module_param(vram_remap, uint, 0);
MODULE_PARM_DESC(vram_remap, "Set amount of video memory to be used [MiB]");
module_param(vram_total, uint, 0);
MODULE_PARM_DESC(vram_total, "Set total amount of video memory [MiB]");
module_param(maxclk, ushort, 0);
MODULE_PARM_DESC(maxclk, "Maximum pixelclock [MHz], overrides EDID data");
module_param(maxhf, ushort, 0);
MODULE_PARM_DESC(maxhf,
	"Maximum horizontal frequency [kHz], overrides EDID data");
module_param(maxvf, ushort, 0);
MODULE_PARM_DESC(maxvf,
	"Maximum vertical frequency [Hz], overrides EDID data");
module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option,
	"Specify initial video mode as \"<xres>x<yres>[-<bpp>][@<refresh>]\"");
module_param(vbemode, ushort, 0);
MODULE_PARM_DESC(vbemode,
	"VBE mode number to set, overrides the 'mode' option");
module_param_string(v86d, v86d_path, PATH_MAX, 0660);
MODULE_PARM_DESC(v86d, "Path to the v86d userspace helper.");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Januszewski <spock@gentoo.org>");
MODULE_DESCRIPTION("Framebuffer driver for VBE2.0+ compliant graphics boards");

