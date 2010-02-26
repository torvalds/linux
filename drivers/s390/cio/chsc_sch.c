/*
 * Driver for s390 chsc subchannels
 *
 * Copyright IBM Corp. 2008, 2009
 *
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>

#include <asm/compat.h>
#include <asm/cio.h>
#include <asm/chsc.h>
#include <asm/isc.h>

#include "cio.h"
#include "cio_debug.h"
#include "css.h"
#include "chsc_sch.h"
#include "ioasm.h"

static debug_info_t *chsc_debug_msg_id;
static debug_info_t *chsc_debug_log_id;

#define CHSC_MSG(imp, args...) do {					\
		debug_sprintf_event(chsc_debug_msg_id, imp , ##args);	\
	} while (0)

#define CHSC_LOG(imp, txt) do {					\
		debug_text_event(chsc_debug_log_id, imp , txt);	\
	} while (0)

static void CHSC_LOG_HEX(int level, void *data, int length)
{
	while (length > 0) {
		debug_event(chsc_debug_log_id, level, data, length);
		length -= chsc_debug_log_id->buf_size;
		data += chsc_debug_log_id->buf_size;
	}
}

MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("driver for s390 chsc subchannels");
MODULE_LICENSE("GPL");

static void chsc_subchannel_irq(struct subchannel *sch)
{
	struct chsc_private *private = sch->private;
	struct chsc_request *request = private->request;
	struct irb *irb = (struct irb *)__LC_IRB;

	CHSC_LOG(4, "irb");
	CHSC_LOG_HEX(4, irb, sizeof(*irb));
	/* Copy irb to provided request and set done. */
	if (!request) {
		CHSC_MSG(0, "Interrupt on sch 0.%x.%04x with no request\n",
			 sch->schid.ssid, sch->schid.sch_no);
		return;
	}
	private->request = NULL;
	memcpy(&request->irb, irb, sizeof(*irb));
	cio_update_schib(sch);
	complete(&request->completion);
	put_device(&sch->dev);
}

static int chsc_subchannel_probe(struct subchannel *sch)
{
	struct chsc_private *private;
	int ret;

	CHSC_MSG(6, "Detected chsc subchannel 0.%x.%04x\n",
		 sch->schid.ssid, sch->schid.sch_no);
	sch->isc = CHSC_SCH_ISC;
	private = kzalloc(sizeof(*private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;
	ret = cio_enable_subchannel(sch, (u32)(unsigned long)sch);
	if (ret) {
		CHSC_MSG(0, "Failed to enable 0.%x.%04x: %d\n",
			 sch->schid.ssid, sch->schid.sch_no, ret);
		kfree(private);
	} else {
		sch->private = private;
		if (dev_get_uevent_suppress(&sch->dev)) {
			dev_set_uevent_suppress(&sch->dev, 0);
			kobject_uevent(&sch->dev.kobj, KOBJ_ADD);
		}
	}
	return ret;
}

static int chsc_subchannel_remove(struct subchannel *sch)
{
	struct chsc_private *private;

	cio_disable_subchannel(sch);
	private = sch->private;
	sch->private = NULL;
	if (private->request) {
		complete(&private->request->completion);
		put_device(&sch->dev);
	}
	kfree(private);
	return 0;
}

static void chsc_subchannel_shutdown(struct subchannel *sch)
{
	cio_disable_subchannel(sch);
}

static int chsc_subchannel_prepare(struct subchannel *sch)
{
	int cc;
	struct schib schib;
	/*
	 * Don't allow suspend while the subchannel is not idle
	 * since we don't have a way to clear the subchannel and
	 * cannot disable it with a request running.
	 */
	cc = stsch(sch->schid, &schib);
	if (!cc && scsw_stctl(&schib.scsw))
		return -EAGAIN;
	return 0;
}

static int chsc_subchannel_freeze(struct subchannel *sch)
{
	return cio_disable_subchannel(sch);
}

static int chsc_subchannel_restore(struct subchannel *sch)
{
	return cio_enable_subchannel(sch, (u32)(unsigned long)sch);
}

static struct css_device_id chsc_subchannel_ids[] = {
	{ .match_flags = 0x1, .type =SUBCHANNEL_TYPE_CHSC, },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(css, chsc_subchannel_ids);

static struct css_driver chsc_subchannel_driver = {
	.owner = THIS_MODULE,
	.subchannel_type = chsc_subchannel_ids,
	.irq = chsc_subchannel_irq,
	.probe = chsc_subchannel_probe,
	.remove = chsc_subchannel_remove,
	.shutdown = chsc_subchannel_shutdown,
	.prepare = chsc_subchannel_prepare,
	.freeze = chsc_subchannel_freeze,
	.thaw = chsc_subchannel_restore,
	.restore = chsc_subchannel_restore,
	.name = "chsc_subchannel",
};

static int __init chsc_init_dbfs(void)
{
	chsc_debug_msg_id = debug_register("chsc_msg", 16, 1,
					   16 * sizeof(long));
	if (!chsc_debug_msg_id)
		goto out;
	debug_register_view(chsc_debug_msg_id, &debug_sprintf_view);
	debug_set_level(chsc_debug_msg_id, 2);
	chsc_debug_log_id = debug_register("chsc_log", 16, 1, 16);
	if (!chsc_debug_log_id)
		goto out;
	debug_register_view(chsc_debug_log_id, &debug_hex_ascii_view);
	debug_set_level(chsc_debug_log_id, 2);
	return 0;
out:
	if (chsc_debug_msg_id)
		debug_unregister(chsc_debug_msg_id);
	return -ENOMEM;
}

static void chsc_remove_dbfs(void)
{
	debug_unregister(chsc_debug_log_id);
	debug_unregister(chsc_debug_msg_id);
}

static int __init chsc_init_sch_driver(void)
{
	return css_driver_register(&chsc_subchannel_driver);
}

static void chsc_cleanup_sch_driver(void)
{
	css_driver_unregister(&chsc_subchannel_driver);
}

static DEFINE_SPINLOCK(chsc_lock);

static int chsc_subchannel_match_next_free(struct device *dev, void *data)
{
	struct subchannel *sch = to_subchannel(dev);

	return sch->schib.pmcw.ena && !scsw_fctl(&sch->schib.scsw);
}

static struct subchannel *chsc_get_next_subchannel(struct subchannel *sch)
{
	struct device *dev;

	dev = driver_find_device(&chsc_subchannel_driver.drv,
				 sch ? &sch->dev : NULL, NULL,
				 chsc_subchannel_match_next_free);
	return dev ? to_subchannel(dev) : NULL;
}

/**
 * chsc_async() - try to start a chsc request asynchronously
 * @chsc_area: request to be started
 * @request: request structure to associate
 *
 * Tries to start a chsc request on one of the existing chsc subchannels.
 * Returns:
 *  %0 if the request was performed synchronously
 *  %-EINPROGRESS if the request was successfully started
 *  %-EBUSY if all chsc subchannels are busy
 *  %-ENODEV if no chsc subchannels are available
 * Context:
 *  interrupts disabled, chsc_lock held
 */
static int chsc_async(struct chsc_async_area *chsc_area,
		      struct chsc_request *request)
{
	int cc;
	struct chsc_private *private;
	struct subchannel *sch = NULL;
	int ret = -ENODEV;
	char dbf[10];

	chsc_area->header.key = PAGE_DEFAULT_KEY >> 4;
	while ((sch = chsc_get_next_subchannel(sch))) {
		spin_lock(sch->lock);
		private = sch->private;
		if (private->request) {
			spin_unlock(sch->lock);
			ret = -EBUSY;
			continue;
		}
		chsc_area->header.sid = sch->schid;
		CHSC_LOG(2, "schid");
		CHSC_LOG_HEX(2, &sch->schid, sizeof(sch->schid));
		cc = chsc(chsc_area);
		sprintf(dbf, "cc:%d", cc);
		CHSC_LOG(2, dbf);
		switch (cc) {
		case 0:
			ret = 0;
			break;
		case 1:
			sch->schib.scsw.cmd.fctl |= SCSW_FCTL_START_FUNC;
			ret = -EINPROGRESS;
			private->request = request;
			break;
		case 2:
			ret = -EBUSY;
			break;
		default:
			ret = -ENODEV;
		}
		spin_unlock(sch->lock);
		CHSC_MSG(2, "chsc on 0.%x.%04x returned cc=%d\n",
			 sch->schid.ssid, sch->schid.sch_no, cc);
		if (ret == -EINPROGRESS)
			return -EINPROGRESS;
		put_device(&sch->dev);
		if (ret == 0)
			return 0;
	}
	return ret;
}

static void chsc_log_command(struct chsc_async_area *chsc_area)
{
	char dbf[10];

	sprintf(dbf, "CHSC:%x", chsc_area->header.code);
	CHSC_LOG(0, dbf);
	CHSC_LOG_HEX(0, chsc_area, 32);
}

static int chsc_examine_irb(struct chsc_request *request)
{
	int backed_up;

	if (!(scsw_stctl(&request->irb.scsw) & SCSW_STCTL_STATUS_PEND))
		return -EIO;
	backed_up = scsw_cstat(&request->irb.scsw) & SCHN_STAT_CHAIN_CHECK;
	request->irb.scsw.cmd.cstat &= ~SCHN_STAT_CHAIN_CHECK;
	if (scsw_cstat(&request->irb.scsw) == 0)
		return 0;
	if (!backed_up)
		return 0;
	if (scsw_cstat(&request->irb.scsw) & SCHN_STAT_PROG_CHECK)
		return -EIO;
	if (scsw_cstat(&request->irb.scsw) & SCHN_STAT_PROT_CHECK)
		return -EPERM;
	if (scsw_cstat(&request->irb.scsw) & SCHN_STAT_CHN_DATA_CHK)
		return -EAGAIN;
	if (scsw_cstat(&request->irb.scsw) & SCHN_STAT_CHN_CTRL_CHK)
		return -EAGAIN;
	return -EIO;
}

static int chsc_ioctl_start(void __user *user_area)
{
	struct chsc_request *request;
	struct chsc_async_area *chsc_area;
	int ret;
	char dbf[10];

	if (!css_general_characteristics.dynio)
		/* It makes no sense to try. */
		return -EOPNOTSUPP;
	chsc_area = (void *)get_zeroed_page(GFP_DMA | GFP_KERNEL);
	if (!chsc_area)
		return -ENOMEM;
	request = kzalloc(sizeof(*request), GFP_KERNEL);
	if (!request) {
		ret = -ENOMEM;
		goto out_free;
	}
	init_completion(&request->completion);
	if (copy_from_user(chsc_area, user_area, PAGE_SIZE)) {
		ret = -EFAULT;
		goto out_free;
	}
	chsc_log_command(chsc_area);
	spin_lock_irq(&chsc_lock);
	ret = chsc_async(chsc_area, request);
	spin_unlock_irq(&chsc_lock);
	if (ret == -EINPROGRESS) {
		wait_for_completion(&request->completion);
		ret = chsc_examine_irb(request);
	}
	/* copy area back to user */
	if (!ret)
		if (copy_to_user(user_area, chsc_area, PAGE_SIZE))
			ret = -EFAULT;
out_free:
	sprintf(dbf, "ret:%d", ret);
	CHSC_LOG(0, dbf);
	kfree(request);
	free_page((unsigned long)chsc_area);
	return ret;
}

static int chsc_ioctl_info_channel_path(void __user *user_cd)
{
	struct chsc_chp_cd *cd;
	int ret, ccode;
	struct {
		struct chsc_header request;
		u32 : 2;
		u32 m : 1;
		u32 : 1;
		u32 fmt1 : 4;
		u32 cssid : 8;
		u32 : 8;
		u32 first_chpid : 8;
		u32 : 24;
		u32 last_chpid : 8;
		u32 : 32;
		struct chsc_header response;
		u8 data[PAGE_SIZE - 20];
	} __attribute__ ((packed)) *scpcd_area;

	scpcd_area = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!scpcd_area)
		return -ENOMEM;
	cd = kzalloc(sizeof(*cd), GFP_KERNEL);
	if (!cd) {
		ret = -ENOMEM;
		goto out_free;
	}
	if (copy_from_user(cd, user_cd, sizeof(*cd))) {
		ret = -EFAULT;
		goto out_free;
	}
	scpcd_area->request.length = 0x0010;
	scpcd_area->request.code = 0x0028;
	scpcd_area->m = cd->m;
	scpcd_area->fmt1 = cd->fmt;
	scpcd_area->cssid = cd->chpid.cssid;
	scpcd_area->first_chpid = cd->chpid.id;
	scpcd_area->last_chpid = cd->chpid.id;

	ccode = chsc(scpcd_area);
	if (ccode != 0) {
		ret = -EIO;
		goto out_free;
	}
	if (scpcd_area->response.code != 0x0001) {
		ret = -EIO;
		CHSC_MSG(0, "scpcd: response code=%x\n",
			 scpcd_area->response.code);
		goto out_free;
	}
	memcpy(&cd->cpcb, &scpcd_area->response, scpcd_area->response.length);
	if (copy_to_user(user_cd, cd, sizeof(*cd)))
		ret = -EFAULT;
	else
		ret = 0;
out_free:
	kfree(cd);
	free_page((unsigned long)scpcd_area);
	return ret;
}

static int chsc_ioctl_info_cu(void __user *user_cd)
{
	struct chsc_cu_cd *cd;
	int ret, ccode;
	struct {
		struct chsc_header request;
		u32 : 2;
		u32 m : 1;
		u32 : 1;
		u32 fmt1 : 4;
		u32 cssid : 8;
		u32 : 8;
		u32 first_cun : 8;
		u32 : 24;
		u32 last_cun : 8;
		u32 : 32;
		struct chsc_header response;
		u8 data[PAGE_SIZE - 20];
	} __attribute__ ((packed)) *scucd_area;

	scucd_area = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!scucd_area)
		return -ENOMEM;
	cd = kzalloc(sizeof(*cd), GFP_KERNEL);
	if (!cd) {
		ret = -ENOMEM;
		goto out_free;
	}
	if (copy_from_user(cd, user_cd, sizeof(*cd))) {
		ret = -EFAULT;
		goto out_free;
	}
	scucd_area->request.length = 0x0010;
	scucd_area->request.code = 0x0028;
	scucd_area->m = cd->m;
	scucd_area->fmt1 = cd->fmt;
	scucd_area->cssid = cd->cssid;
	scucd_area->first_cun = cd->cun;
	scucd_area->last_cun = cd->cun;

	ccode = chsc(scucd_area);
	if (ccode != 0) {
		ret = -EIO;
		goto out_free;
	}
	if (scucd_area->response.code != 0x0001) {
		ret = -EIO;
		CHSC_MSG(0, "scucd: response code=%x\n",
			 scucd_area->response.code);
		goto out_free;
	}
	memcpy(&cd->cucb, &scucd_area->response, scucd_area->response.length);
	if (copy_to_user(user_cd, cd, sizeof(*cd)))
		ret = -EFAULT;
	else
		ret = 0;
out_free:
	kfree(cd);
	free_page((unsigned long)scucd_area);
	return ret;
}

static int chsc_ioctl_info_sch_cu(void __user *user_cud)
{
	struct chsc_sch_cud *cud;
	int ret, ccode;
	struct {
		struct chsc_header request;
		u32 : 2;
		u32 m : 1;
		u32 : 5;
		u32 fmt1 : 4;
		u32 : 2;
		u32 ssid : 2;
		u32 first_sch : 16;
		u32 : 8;
		u32 cssid : 8;
		u32 last_sch : 16;
		u32 : 32;
		struct chsc_header response;
		u8 data[PAGE_SIZE - 20];
	} __attribute__ ((packed)) *sscud_area;

	sscud_area = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sscud_area)
		return -ENOMEM;
	cud = kzalloc(sizeof(*cud), GFP_KERNEL);
	if (!cud) {
		ret = -ENOMEM;
		goto out_free;
	}
	if (copy_from_user(cud, user_cud, sizeof(*cud))) {
		ret = -EFAULT;
		goto out_free;
	}
	sscud_area->request.length = 0x0010;
	sscud_area->request.code = 0x0006;
	sscud_area->m = cud->schid.m;
	sscud_area->fmt1 = cud->fmt;
	sscud_area->ssid = cud->schid.ssid;
	sscud_area->first_sch = cud->schid.sch_no;
	sscud_area->cssid = cud->schid.cssid;
	sscud_area->last_sch = cud->schid.sch_no;

	ccode = chsc(sscud_area);
	if (ccode != 0) {
		ret = -EIO;
		goto out_free;
	}
	if (sscud_area->response.code != 0x0001) {
		ret = -EIO;
		CHSC_MSG(0, "sscud: response code=%x\n",
			 sscud_area->response.code);
		goto out_free;
	}
	memcpy(&cud->scub, &sscud_area->response, sscud_area->response.length);
	if (copy_to_user(user_cud, cud, sizeof(*cud)))
		ret = -EFAULT;
	else
		ret = 0;
out_free:
	kfree(cud);
	free_page((unsigned long)sscud_area);
	return ret;
}

static int chsc_ioctl_conf_info(void __user *user_ci)
{
	struct chsc_conf_info *ci;
	int ret, ccode;
	struct {
		struct chsc_header request;
		u32 : 2;
		u32 m : 1;
		u32 : 1;
		u32 fmt1 : 4;
		u32 cssid : 8;
		u32 : 6;
		u32 ssid : 2;
		u32 : 8;
		u64 : 64;
		struct chsc_header response;
		u8 data[PAGE_SIZE - 20];
	} __attribute__ ((packed)) *sci_area;

	sci_area = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sci_area)
		return -ENOMEM;
	ci = kzalloc(sizeof(*ci), GFP_KERNEL);
	if (!ci) {
		ret = -ENOMEM;
		goto out_free;
	}
	if (copy_from_user(ci, user_ci, sizeof(*ci))) {
		ret = -EFAULT;
		goto out_free;
	}
	sci_area->request.length = 0x0010;
	sci_area->request.code = 0x0012;
	sci_area->m = ci->id.m;
	sci_area->fmt1 = ci->fmt;
	sci_area->cssid = ci->id.cssid;
	sci_area->ssid = ci->id.ssid;

	ccode = chsc(sci_area);
	if (ccode != 0) {
		ret = -EIO;
		goto out_free;
	}
	if (sci_area->response.code != 0x0001) {
		ret = -EIO;
		CHSC_MSG(0, "sci: response code=%x\n",
			 sci_area->response.code);
		goto out_free;
	}
	memcpy(&ci->scid, &sci_area->response, sci_area->response.length);
	if (copy_to_user(user_ci, ci, sizeof(*ci)))
		ret = -EFAULT;
	else
		ret = 0;
out_free:
	kfree(ci);
	free_page((unsigned long)sci_area);
	return ret;
}

static int chsc_ioctl_conf_comp_list(void __user *user_ccl)
{
	struct chsc_comp_list *ccl;
	int ret, ccode;
	struct {
		struct chsc_header request;
		u32 ctype : 8;
		u32 : 4;
		u32 fmt : 4;
		u32 : 16;
		u64 : 64;
		u32 list_parm[2];
		u64 : 64;
		struct chsc_header response;
		u8 data[PAGE_SIZE - 36];
	} __attribute__ ((packed)) *sccl_area;
	struct {
		u32 m : 1;
		u32 : 31;
		u32 cssid : 8;
		u32 : 16;
		u32 chpid : 8;
	} __attribute__ ((packed)) *chpid_parm;
	struct {
		u32 f_cssid : 8;
		u32 l_cssid : 8;
		u32 : 16;
		u32 res;
	} __attribute__ ((packed)) *cssids_parm;

	sccl_area = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccl_area)
		return -ENOMEM;
	ccl = kzalloc(sizeof(*ccl), GFP_KERNEL);
	if (!ccl) {
		ret = -ENOMEM;
		goto out_free;
	}
	if (copy_from_user(ccl, user_ccl, sizeof(*ccl))) {
		ret = -EFAULT;
		goto out_free;
	}
	sccl_area->request.length = 0x0020;
	sccl_area->request.code = 0x0030;
	sccl_area->fmt = ccl->req.fmt;
	sccl_area->ctype = ccl->req.ctype;
	switch (sccl_area->ctype) {
	case CCL_CU_ON_CHP:
	case CCL_IOP_CHP:
		chpid_parm = (void *)&sccl_area->list_parm;
		chpid_parm->m = ccl->req.chpid.m;
		chpid_parm->cssid = ccl->req.chpid.chp.cssid;
		chpid_parm->chpid = ccl->req.chpid.chp.id;
		break;
	case CCL_CSS_IMG:
	case CCL_CSS_IMG_CONF_CHAR:
		cssids_parm = (void *)&sccl_area->list_parm;
		cssids_parm->f_cssid = ccl->req.cssids.f_cssid;
		cssids_parm->l_cssid = ccl->req.cssids.l_cssid;
		break;
	}
	ccode = chsc(sccl_area);
	if (ccode != 0) {
		ret = -EIO;
		goto out_free;
	}
	if (sccl_area->response.code != 0x0001) {
		ret = -EIO;
		CHSC_MSG(0, "sccl: response code=%x\n",
			 sccl_area->response.code);
		goto out_free;
	}
	memcpy(&ccl->sccl, &sccl_area->response, sccl_area->response.length);
	if (copy_to_user(user_ccl, ccl, sizeof(*ccl)))
		ret = -EFAULT;
	else
		ret = 0;
out_free:
	kfree(ccl);
	free_page((unsigned long)sccl_area);
	return ret;
}

static int chsc_ioctl_chpd(void __user *user_chpd)
{
	struct chsc_cpd_info *chpd;
	int ret;

	chpd = kzalloc(sizeof(*chpd), GFP_KERNEL);
	if (!chpd)
		return -ENOMEM;
	if (copy_from_user(chpd, user_chpd, sizeof(*chpd))) {
		ret = -EFAULT;
		goto out_free;
	}
	ret = chsc_determine_channel_path_desc(chpd->chpid, chpd->fmt,
					       chpd->rfmt, chpd->c, chpd->m,
					       &chpd->chpdb);
	if (ret)
		goto out_free;
	if (copy_to_user(user_chpd, chpd, sizeof(*chpd)))
		ret = -EFAULT;
out_free:
	kfree(chpd);
	return ret;
}

static int chsc_ioctl_dcal(void __user *user_dcal)
{
	struct chsc_dcal *dcal;
	int ret, ccode;
	struct {
		struct chsc_header request;
		u32 atype : 8;
		u32 : 4;
		u32 fmt : 4;
		u32 : 16;
		u32 res0[2];
		u32 list_parm[2];
		u32 res1[2];
		struct chsc_header response;
		u8 data[PAGE_SIZE - 36];
	} __attribute__ ((packed)) *sdcal_area;

	sdcal_area = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sdcal_area)
		return -ENOMEM;
	dcal = kzalloc(sizeof(*dcal), GFP_KERNEL);
	if (!dcal) {
		ret = -ENOMEM;
		goto out_free;
	}
	if (copy_from_user(dcal, user_dcal, sizeof(*dcal))) {
		ret = -EFAULT;
		goto out_free;
	}
	sdcal_area->request.length = 0x0020;
	sdcal_area->request.code = 0x0034;
	sdcal_area->atype = dcal->req.atype;
	sdcal_area->fmt = dcal->req.fmt;
	memcpy(&sdcal_area->list_parm, &dcal->req.list_parm,
	       sizeof(sdcal_area->list_parm));

	ccode = chsc(sdcal_area);
	if (ccode != 0) {
		ret = -EIO;
		goto out_free;
	}
	if (sdcal_area->response.code != 0x0001) {
		ret = -EIO;
		CHSC_MSG(0, "sdcal: response code=%x\n",
			 sdcal_area->response.code);
		goto out_free;
	}
	memcpy(&dcal->sdcal, &sdcal_area->response,
	       sdcal_area->response.length);
	if (copy_to_user(user_dcal, dcal, sizeof(*dcal)))
		ret = -EFAULT;
	else
		ret = 0;
out_free:
	kfree(dcal);
	free_page((unsigned long)sdcal_area);
	return ret;
}

static long chsc_ioctl(struct file *filp, unsigned int cmd,
		       unsigned long arg)
{
	void __user *argp;

	CHSC_MSG(2, "chsc_ioctl called, cmd=%x\n", cmd);
	if (is_compat_task())
		argp = compat_ptr(arg);
	else
		argp = (void __user *)arg;
	switch (cmd) {
	case CHSC_START:
		return chsc_ioctl_start(argp);
	case CHSC_INFO_CHANNEL_PATH:
		return chsc_ioctl_info_channel_path(argp);
	case CHSC_INFO_CU:
		return chsc_ioctl_info_cu(argp);
	case CHSC_INFO_SCH_CU:
		return chsc_ioctl_info_sch_cu(argp);
	case CHSC_INFO_CI:
		return chsc_ioctl_conf_info(argp);
	case CHSC_INFO_CCL:
		return chsc_ioctl_conf_comp_list(argp);
	case CHSC_INFO_CPD:
		return chsc_ioctl_chpd(argp);
	case CHSC_INFO_DCAL:
		return chsc_ioctl_dcal(argp);
	default: /* unknown ioctl number */
		return -ENOIOCTLCMD;
	}
}

static const struct file_operations chsc_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = chsc_ioctl,
	.compat_ioctl = chsc_ioctl,
};

static struct miscdevice chsc_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "chsc",
	.fops = &chsc_fops,
};

static int __init chsc_misc_init(void)
{
	return misc_register(&chsc_misc_device);
}

static void chsc_misc_cleanup(void)
{
	misc_deregister(&chsc_misc_device);
}

static int __init chsc_sch_init(void)
{
	int ret;

	ret = chsc_init_dbfs();
	if (ret)
		return ret;
	isc_register(CHSC_SCH_ISC);
	ret = chsc_init_sch_driver();
	if (ret)
		goto out_dbf;
	ret = chsc_misc_init();
	if (ret)
		goto out_driver;
	return ret;
out_driver:
	chsc_cleanup_sch_driver();
out_dbf:
	isc_unregister(CHSC_SCH_ISC);
	chsc_remove_dbfs();
	return ret;
}

static void __exit chsc_sch_exit(void)
{
	chsc_misc_cleanup();
	chsc_cleanup_sch_driver();
	isc_unregister(CHSC_SCH_ISC);
	chsc_remove_dbfs();
}

module_init(chsc_sch_init);
module_exit(chsc_sch_exit);
