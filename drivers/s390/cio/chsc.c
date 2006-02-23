/*
 *  drivers/s390/cio/chsc.c
 *   S/390 common I/O routines -- channel subsystem call
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *			      IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *		 Cornelia Huck (cornelia.huck@de.ibm.com)
 *		 Arnd Bergmann (arndb@de.ibm.com)
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/cio.h>

#include "css.h"
#include "cio.h"
#include "cio_debug.h"
#include "ioasm.h"
#include "chsc.h"

static void *sei_page;

static int new_channel_path(int chpid);

static inline void
set_chp_logically_online(int chp, int onoff)
{
	css[0]->chps[chp]->state = onoff;
}

static int
get_chp_status(int chp)
{
	return (css[0]->chps[chp] ? css[0]->chps[chp]->state : -ENODEV);
}

void
chsc_validate_chpids(struct subchannel *sch)
{
	int mask, chp;

	for (chp = 0; chp <= 7; chp++) {
		mask = 0x80 >> chp;
		if (!get_chp_status(sch->schib.pmcw.chpid[chp]))
			/* disable using this path */
			sch->opm &= ~mask;
	}
}

void
chpid_is_actually_online(int chp)
{
	int state;

	state = get_chp_status(chp);
	if (state < 0) {
		need_rescan = 1;
		queue_work(slow_path_wq, &slow_path_work);
	} else
		WARN_ON(!state);
}

/* FIXME: this is _always_ called for every subchannel. shouldn't we
 *	  process more than one at a time? */
static int
chsc_get_sch_desc_irq(struct subchannel *sch, void *page)
{
	int ccode, j;

	struct {
		struct chsc_header request;
		u16 reserved1a:10;
		u16 ssid:2;
		u16 reserved1b:4;
		u16 f_sch;	  /* first subchannel */
		u16 reserved2;
		u16 l_sch;	  /* last subchannel */
		u32 reserved3;
		struct chsc_header response;
		u32 reserved4;
		u8 sch_valid : 1;
		u8 dev_valid : 1;
		u8 st	     : 3; /* subchannel type */
		u8 zeroes    : 3;
		u8  unit_addr;	  /* unit address */
		u16 devno;	  /* device number */
		u8 path_mask;
		u8 fla_valid_mask;
		u16 sch;	  /* subchannel */
		u8 chpid[8];	  /* chpids 0-7 */
		u16 fla[8];	  /* full link addresses 0-7 */
	} *ssd_area;

	ssd_area = page;

	ssd_area->request = (struct chsc_header) {
		.length = 0x0010,
		.code   = 0x0004,
	};

	ssd_area->ssid = sch->schid.ssid;
	ssd_area->f_sch = sch->schid.sch_no;
	ssd_area->l_sch = sch->schid.sch_no;

	ccode = chsc(ssd_area);
	if (ccode > 0) {
		pr_debug("chsc returned with ccode = %d\n", ccode);
		return (ccode == 3) ? -ENODEV : -EBUSY;
	}

	switch (ssd_area->response.code) {
	case 0x0001: /* everything ok */
		break;
	case 0x0002:
		CIO_CRW_EVENT(2, "Invalid command!\n");
		return -EINVAL;
	case 0x0003:
		CIO_CRW_EVENT(2, "Error in chsc request block!\n");
		return -EINVAL;
	case 0x0004:
		CIO_CRW_EVENT(2, "Model does not provide ssd\n");
		return -EOPNOTSUPP;
	default:
		CIO_CRW_EVENT(2, "Unknown CHSC response %d\n",
			      ssd_area->response.code);
		return -EIO;
	}

	/*
	 * ssd_area->st stores the type of the detected
	 * subchannel, with the following definitions:
	 *
	 * 0: I/O subchannel:	  All fields have meaning
	 * 1: CHSC subchannel:	  Only sch_val, st and sch
	 *			  have meaning
	 * 2: Message subchannel: All fields except unit_addr
	 *			  have meaning
	 * 3: ADM subchannel:	  Only sch_val, st and sch
	 *			  have meaning
	 *
	 * Other types are currently undefined.
	 */
	if (ssd_area->st > 3) { /* uhm, that looks strange... */
		CIO_CRW_EVENT(0, "Strange subchannel type %d"
			      " for sch 0.%x.%04x\n", ssd_area->st,
			      sch->schid.ssid, sch->schid.sch_no);
		/*
		 * There may have been a new subchannel type defined in the
		 * time since this code was written; since we don't know which
		 * fields have meaning and what to do with it we just jump out
		 */
		return 0;
	} else {
		const char *type[4] = {"I/O", "chsc", "message", "ADM"};
		CIO_CRW_EVENT(6, "ssd: sch 0.%x.%04x is %s subchannel\n",
			      sch->schid.ssid, sch->schid.sch_no,
			      type[ssd_area->st]);

		sch->ssd_info.valid = 1;
		sch->ssd_info.type = ssd_area->st;
	}

	if (ssd_area->st == 0 || ssd_area->st == 2) {
		for (j = 0; j < 8; j++) {
			if (!((0x80 >> j) & ssd_area->path_mask &
			      ssd_area->fla_valid_mask))
				continue;
			sch->ssd_info.chpid[j] = ssd_area->chpid[j];
			sch->ssd_info.fla[j]   = ssd_area->fla[j];
		}
	}
	return 0;
}

int
css_get_ssd_info(struct subchannel *sch)
{
	int ret;
	void *page;

	page = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!page)
		return -ENOMEM;
	spin_lock_irq(&sch->lock);
	ret = chsc_get_sch_desc_irq(sch, page);
	if (ret) {
		static int cio_chsc_err_msg;
		
		if (!cio_chsc_err_msg) {
			printk(KERN_ERR
			       "chsc_get_sch_descriptions:"
			       " Error %d while doing chsc; "
			       "processing some machine checks may "
			       "not work\n", ret);
			cio_chsc_err_msg = 1;
		}
	}
	spin_unlock_irq(&sch->lock);
	free_page((unsigned long)page);
	if (!ret) {
		int j, chpid;
		/* Allocate channel path structures, if needed. */
		for (j = 0; j < 8; j++) {
			chpid = sch->ssd_info.chpid[j];
			if (chpid && (get_chp_status(chpid) < 0))
			    new_channel_path(chpid);
		}
	}
	return ret;
}

static int
s390_subchannel_remove_chpid(struct device *dev, void *data)
{
	int j;
	int mask;
	struct subchannel *sch;
	struct channel_path *chpid;
	struct schib schib;

	sch = to_subchannel(dev);
	chpid = data;
	for (j = 0; j < 8; j++)
		if (sch->schib.pmcw.chpid[j] == chpid->id)
			break;
	if (j >= 8)
		return 0;

	mask = 0x80 >> j;
	spin_lock_irq(&sch->lock);

	stsch(sch->schid, &schib);
	if (!schib.pmcw.dnv)
		goto out_unreg;
	memcpy(&sch->schib, &schib, sizeof(struct schib));
	/* Check for single path devices. */
	if (sch->schib.pmcw.pim == 0x80)
		goto out_unreg;
	if (sch->vpm == mask)
		goto out_unreg;

	if ((sch->schib.scsw.actl & (SCSW_ACTL_CLEAR_PEND |
				     SCSW_ACTL_HALT_PEND |
				     SCSW_ACTL_START_PEND |
				     SCSW_ACTL_RESUME_PEND)) &&
	    (sch->schib.pmcw.lpum == mask)) {
		int cc = cio_cancel(sch);
		
		if (cc == -ENODEV)
			goto out_unreg;

		if (cc == -EINVAL) {
			cc = cio_clear(sch);
			if (cc == -ENODEV)
				goto out_unreg;
			/* Call handler. */
			if (sch->driver && sch->driver->termination)
				sch->driver->termination(&sch->dev);
			goto out_unlock;
		}
	} else if ((sch->schib.scsw.actl & SCSW_ACTL_DEVACT) &&
		   (sch->schib.scsw.actl & SCSW_ACTL_SCHACT) &&
		   (sch->schib.pmcw.lpum == mask)) {
		int cc;

		cc = cio_clear(sch);
		if (cc == -ENODEV)
			goto out_unreg;
		/* Call handler. */
		if (sch->driver && sch->driver->termination)
			sch->driver->termination(&sch->dev);
		goto out_unlock;
	}

	/* trigger path verification. */
	if (sch->driver && sch->driver->verify)
		sch->driver->verify(&sch->dev);
out_unlock:
	spin_unlock_irq(&sch->lock);
	return 0;
out_unreg:
	spin_unlock_irq(&sch->lock);
	sch->lpm = 0;
	if (css_enqueue_subchannel_slow(sch->schid)) {
		css_clear_subchannel_slow_list();
		need_rescan = 1;
	}
	return 0;
}

static inline void
s390_set_chpid_offline( __u8 chpid)
{
	char dbf_txt[15];
	struct device *dev;

	sprintf(dbf_txt, "chpr%x", chpid);
	CIO_TRACE_EVENT(2, dbf_txt);

	if (get_chp_status(chpid) <= 0)
		return;
	dev = get_device(&css[0]->chps[chpid]->dev);
	bus_for_each_dev(&css_bus_type, NULL, to_channelpath(dev),
			 s390_subchannel_remove_chpid);

	if (need_rescan || css_slow_subchannels_exist())
		queue_work(slow_path_wq, &slow_path_work);
	put_device(dev);
}

struct res_acc_data {
	struct channel_path *chp;
	u32 fla_mask;
	u16 fla;
};

static int
s390_process_res_acc_sch(struct res_acc_data *res_data, struct subchannel *sch)
{
	int found;
	int chp;
	int ccode;
	
	found = 0;
	for (chp = 0; chp <= 7; chp++)
		/*
		 * check if chpid is in information updated by ssd
		 */
		if (sch->ssd_info.valid &&
		    sch->ssd_info.chpid[chp] == res_data->chp->id &&
		    (sch->ssd_info.fla[chp] & res_data->fla_mask)
		    == res_data->fla) {
			found = 1;
			break;
		}
	
	if (found == 0)
		return 0;

	/*
	 * Do a stsch to update our subchannel structure with the
	 * new path information and eventually check for logically
	 * offline chpids.
	 */
	ccode = stsch(sch->schid, &sch->schib);
	if (ccode > 0)
		return 0;

	return 0x80 >> chp;
}

static inline int
s390_process_res_acc_new_sch(struct subchannel_id schid)
{
	struct schib schib;
	int ret;
	/*
	 * We don't know the device yet, but since a path
	 * may be available now to the device we'll have
	 * to do recognition again.
	 * Since we don't have any idea about which chpid
	 * that beast may be on we'll have to do a stsch
	 * on all devices, grr...
	 */
	if (stsch_err(schid, &schib))
		/* We're through */
		return need_rescan ? -EAGAIN : -ENXIO;

	/* Put it on the slow path. */
	ret = css_enqueue_subchannel_slow(schid);
	if (ret) {
		css_clear_subchannel_slow_list();
		need_rescan = 1;
		return -EAGAIN;
	}
	return 0;
}

static int
__s390_process_res_acc(struct subchannel_id schid, void *data)
{
	int chp_mask, old_lpm;
	struct res_acc_data *res_data;
	struct subchannel *sch;

	res_data = (struct res_acc_data *)data;
	sch = get_subchannel_by_schid(schid);
	if (!sch)
		/* Check if a subchannel is newly available. */
		return s390_process_res_acc_new_sch(schid);

	spin_lock_irq(&sch->lock);

	chp_mask = s390_process_res_acc_sch(res_data, sch);

	if (chp_mask == 0) {
		spin_unlock_irq(&sch->lock);
		return 0;
	}
	old_lpm = sch->lpm;
	sch->lpm = ((sch->schib.pmcw.pim &
		     sch->schib.pmcw.pam &
		     sch->schib.pmcw.pom)
		    | chp_mask) & sch->opm;
	if (!old_lpm && sch->lpm)
		device_trigger_reprobe(sch);
	else if (sch->driver && sch->driver->verify)
		sch->driver->verify(&sch->dev);

	spin_unlock_irq(&sch->lock);
	put_device(&sch->dev);
	return (res_data->fla_mask == 0xffff) ? -ENODEV : 0;
}


static int
s390_process_res_acc (struct res_acc_data *res_data)
{
	int rc;
	char dbf_txt[15];

	sprintf(dbf_txt, "accpr%x", res_data->chp->id);
	CIO_TRACE_EVENT( 2, dbf_txt);
	if (res_data->fla != 0) {
		sprintf(dbf_txt, "fla%x", res_data->fla);
		CIO_TRACE_EVENT( 2, dbf_txt);
	}

	/*
	 * I/O resources may have become accessible.
	 * Scan through all subchannels that may be concerned and
	 * do a validation on those.
	 * The more information we have (info), the less scanning
	 * will we have to do.
	 */
	rc = for_each_subchannel(__s390_process_res_acc, res_data);
	if (css_slow_subchannels_exist())
		rc = -EAGAIN;
	else if (rc != -EAGAIN)
		rc = 0;
	return rc;
}

static int
__get_chpid_from_lir(void *data)
{
	struct lir {
		u8  iq;
		u8  ic;
		u16 sci;
		/* incident-node descriptor */
		u32 indesc[28];
		/* attached-node descriptor */
		u32 andesc[28];
		/* incident-specific information */
		u32 isinfo[28];
	} *lir;

	lir = (struct lir*) data;
	if (!(lir->iq&0x80))
		/* NULL link incident record */
		return -EINVAL;
	if (!(lir->indesc[0]&0xc0000000))
		/* node descriptor not valid */
		return -EINVAL;
	if (!(lir->indesc[0]&0x10000000))
		/* don't handle device-type nodes - FIXME */
		return -EINVAL;
	/* Byte 3 contains the chpid. Could also be CTCA, but we don't care */

	return (u16) (lir->indesc[0]&0x000000ff);
}

int
chsc_process_crw(void)
{
	int chpid, ret;
	struct res_acc_data res_data;
	struct {
		struct chsc_header request;
		u32 reserved1;
		u32 reserved2;
		u32 reserved3;
		struct chsc_header response;
		u32 reserved4;
		u8  flags;
		u8  vf;		/* validity flags */
		u8  rs;		/* reporting source */
		u8  cc;		/* content code */
		u16 fla;	/* full link address */
		u16 rsid;	/* reporting source id */
		u32 reserved5;
		u32 reserved6;
		u32 ccdf[96];	/* content-code dependent field */
		/* ccdf has to be big enough for a link-incident record */
	} *sei_area;

	if (!sei_page)
		return 0;
	/*
	 * build the chsc request block for store event information
	 * and do the call
	 * This function is only called by the machine check handler thread,
	 * so we don't need locking for the sei_page.
	 */
	sei_area = sei_page;

	CIO_TRACE_EVENT( 2, "prcss");
	ret = 0;
	do {
		int ccode, status;
		struct device *dev;
		memset(sei_area, 0, sizeof(*sei_area));
		memset(&res_data, 0, sizeof(struct res_acc_data));
		sei_area->request = (struct chsc_header) {
			.length = 0x0010,
			.code   = 0x000e,
		};

		ccode = chsc(sei_area);
		if (ccode > 0)
			return 0;

		switch (sei_area->response.code) {
			/* for debug purposes, check for problems */
		case 0x0001:
			CIO_CRW_EVENT(4, "chsc_process_crw: event information "
					"successfully stored\n");
			break; /* everything ok */
		case 0x0002:
			CIO_CRW_EVENT(2,
				      "chsc_process_crw: invalid command!\n");
			return 0;
		case 0x0003:
			CIO_CRW_EVENT(2, "chsc_process_crw: error in chsc "
				      "request block!\n");
			return 0;
		case 0x0005:
			CIO_CRW_EVENT(2, "chsc_process_crw: no event "
				      "information stored\n");
			return 0;
		default:
			CIO_CRW_EVENT(2, "chsc_process_crw: chsc response %d\n",
				      sei_area->response.code);
			return 0;
		}

		/* Check if we might have lost some information. */
		if (sei_area->flags & 0x40)
			CIO_CRW_EVENT(2, "chsc_process_crw: Event information "
				       "has been lost due to overflow!\n");

		if (sei_area->rs != 4) {
			CIO_CRW_EVENT(2, "chsc_process_crw: reporting source "
				      "(%04X) isn't a chpid!\n",
				      sei_area->rsid);
			continue;
		}

		/* which kind of information was stored? */
		switch (sei_area->cc) {
		case 1: /* link incident*/
			CIO_CRW_EVENT(4, "chsc_process_crw: "
				      "channel subsystem reports link incident,"
				      " reporting source is chpid %x\n",
				      sei_area->rsid);
			chpid = __get_chpid_from_lir(sei_area->ccdf);
			if (chpid < 0)
				CIO_CRW_EVENT(4, "%s: Invalid LIR, skipping\n",
					      __FUNCTION__);
			else
				s390_set_chpid_offline(chpid);
			break;
			
		case 2: /* i/o resource accessibiliy */
			CIO_CRW_EVENT(4, "chsc_process_crw: "
				      "channel subsystem reports some I/O "
				      "devices may have become accessible\n");
			pr_debug("Data received after sei: \n");
			pr_debug("Validity flags: %x\n", sei_area->vf);
			
			/* allocate a new channel path structure, if needed */
			status = get_chp_status(sei_area->rsid);
			if (status < 0)
				new_channel_path(sei_area->rsid);
			else if (!status)
				break;
			dev = get_device(&css[0]->chps[sei_area->rsid]->dev);
			res_data.chp = to_channelpath(dev);
			pr_debug("chpid: %x", sei_area->rsid);
			if ((sei_area->vf & 0xc0) != 0) {
				res_data.fla = sei_area->fla;
				if ((sei_area->vf & 0xc0) == 0xc0) {
					pr_debug(" full link addr: %x",
						 sei_area->fla);
					res_data.fla_mask = 0xffff;
				} else {
					pr_debug(" link addr: %x",
						 sei_area->fla);
					res_data.fla_mask = 0xff00;
				}
			}
			ret = s390_process_res_acc(&res_data);
			pr_debug("\n\n");
			put_device(dev);
			break;
			
		default: /* other stuff */
			CIO_CRW_EVENT(4, "chsc_process_crw: event %d\n",
				      sei_area->cc);
			break;
		}
	} while (sei_area->flags & 0x80);
	return ret;
}

static inline int
__chp_add_new_sch(struct subchannel_id schid)
{
	struct schib schib;
	int ret;

	if (stsch(schid, &schib))
		/* We're through */
		return need_rescan ? -EAGAIN : -ENXIO;

	/* Put it on the slow path. */
	ret = css_enqueue_subchannel_slow(schid);
	if (ret) {
		css_clear_subchannel_slow_list();
		need_rescan = 1;
		return -EAGAIN;
	}
	return 0;
}


static int
__chp_add(struct subchannel_id schid, void *data)
{
	int i;
	struct channel_path *chp;
	struct subchannel *sch;

	chp = (struct channel_path *)data;
	sch = get_subchannel_by_schid(schid);
	if (!sch)
		/* Check if the subchannel is now available. */
		return __chp_add_new_sch(schid);
	spin_lock_irq(&sch->lock);
	for (i=0; i<8; i++)
		if (sch->schib.pmcw.chpid[i] == chp->id) {
			if (stsch(sch->schid, &sch->schib) != 0) {
				/* Endgame. */
				spin_unlock(&sch->lock);
				return -ENXIO;
			}
			break;
		}
	if (i==8) {
		spin_unlock(&sch->lock);
		return 0;
	}
	sch->lpm = ((sch->schib.pmcw.pim &
		     sch->schib.pmcw.pam &
		     sch->schib.pmcw.pom)
		    | 0x80 >> i) & sch->opm;

	if (sch->driver && sch->driver->verify)
		sch->driver->verify(&sch->dev);

	spin_unlock_irq(&sch->lock);
	put_device(&sch->dev);
	return 0;
}

static int
chp_add(int chpid)
{
	int rc;
	char dbf_txt[15];
	struct device *dev;

	if (!get_chp_status(chpid))
		return 0; /* no need to do the rest */
	
	sprintf(dbf_txt, "cadd%x", chpid);
	CIO_TRACE_EVENT(2, dbf_txt);

	dev = get_device(&css[0]->chps[chpid]->dev);
	rc = for_each_subchannel(__chp_add, to_channelpath(dev));
	if (css_slow_subchannels_exist())
		rc = -EAGAIN;
	if (rc != -EAGAIN)
		rc = 0;
	put_device(dev);
	return rc;
}

/* 
 * Handling of crw machine checks with channel path source.
 */
int
chp_process_crw(int chpid, int on)
{
	if (on == 0) {
		/* Path has gone. We use the link incident routine.*/
		s390_set_chpid_offline(chpid);
		return 0; /* De-register is async anyway. */
	}
	/*
	 * Path has come. Allocate a new channel path structure,
	 * if needed.
	 */
	if (get_chp_status(chpid) < 0)
		new_channel_path(chpid);
	/* Avoid the extra overhead in process_rec_acc. */
	return chp_add(chpid);
}

static inline int
__check_for_io_and_kill(struct subchannel *sch, int index)
{
	int cc;

	if (!device_is_online(sch))
		/* cio could be doing I/O. */
		return 0;
	cc = stsch(sch->schid, &sch->schib);
	if (cc)
		return 0;
	if (sch->schib.scsw.actl && sch->schib.pmcw.lpum == (0x80 >> index)) {
		device_set_waiting(sch);
		return 1;
	}
	return 0;
}

static inline void
__s390_subchannel_vary_chpid(struct subchannel *sch, __u8 chpid, int on)
{
	int chp, old_lpm;
	unsigned long flags;

	if (!sch->ssd_info.valid)
		return;
	
	spin_lock_irqsave(&sch->lock, flags);
	old_lpm = sch->lpm;
	for (chp = 0; chp < 8; chp++) {
		if (sch->ssd_info.chpid[chp] != chpid)
			continue;

		if (on) {
			sch->opm |= (0x80 >> chp);
			sch->lpm |= (0x80 >> chp);
			if (!old_lpm)
				device_trigger_reprobe(sch);
			else if (sch->driver && sch->driver->verify)
				sch->driver->verify(&sch->dev);
		} else {
			sch->opm &= ~(0x80 >> chp);
			sch->lpm &= ~(0x80 >> chp);
			/*
			 * Give running I/O a grace period in which it
			 * can successfully terminate, even using the
			 * just varied off path. Then kill it.
			 */
			if (!__check_for_io_and_kill(sch, chp) && !sch->lpm) {
				if (css_enqueue_subchannel_slow(sch->schid)) {
					css_clear_subchannel_slow_list();
					need_rescan = 1;
				}
			} else if (sch->driver && sch->driver->verify)
				sch->driver->verify(&sch->dev);
		}
		break;
	}
	spin_unlock_irqrestore(&sch->lock, flags);
}

static int
s390_subchannel_vary_chpid_off(struct device *dev, void *data)
{
	struct subchannel *sch;
	__u8 *chpid;

	sch = to_subchannel(dev);
	chpid = data;

	__s390_subchannel_vary_chpid(sch, *chpid, 0);
	return 0;
}

static int
s390_subchannel_vary_chpid_on(struct device *dev, void *data)
{
	struct subchannel *sch;
	__u8 *chpid;

	sch = to_subchannel(dev);
	chpid = data;

	__s390_subchannel_vary_chpid(sch, *chpid, 1);
	return 0;
}

static int
__s390_vary_chpid_on(struct subchannel_id schid, void *data)
{
	struct schib schib;
	struct subchannel *sch;

	sch = get_subchannel_by_schid(schid);
	if (sch) {
		put_device(&sch->dev);
		return 0;
	}
	if (stsch_err(schid, &schib))
		/* We're through */
		return -ENXIO;
	/* Put it on the slow path. */
	if (css_enqueue_subchannel_slow(schid)) {
		css_clear_subchannel_slow_list();
		need_rescan = 1;
		return -EAGAIN;
	}
	return 0;
}

/*
 * Function: s390_vary_chpid
 * Varies the specified chpid online or offline
 */
static int
s390_vary_chpid( __u8 chpid, int on)
{
	char dbf_text[15];
	int status;

	sprintf(dbf_text, on?"varyon%x":"varyoff%x", chpid);
	CIO_TRACE_EVENT( 2, dbf_text);

	status = get_chp_status(chpid);
	if (status < 0) {
		printk(KERN_ERR "Can't vary unknown chpid %02X\n", chpid);
		return -EINVAL;
	}

	if (!on && !status) {
		printk(KERN_ERR "chpid %x is already offline\n", chpid);
		return -EINVAL;
	}

	set_chp_logically_online(chpid, on);

	/*
	 * Redo PathVerification on the devices the chpid connects to
	 */

	bus_for_each_dev(&css_bus_type, NULL, &chpid, on ?
			 s390_subchannel_vary_chpid_on :
			 s390_subchannel_vary_chpid_off);
	if (on)
		/* Scan for new devices on varied on path. */
		for_each_subchannel(__s390_vary_chpid_on, NULL);
	if (need_rescan || css_slow_subchannels_exist())
		queue_work(slow_path_wq, &slow_path_work);
	return 0;
}

/*
 * Files for the channel path entries.
 */
static ssize_t
chp_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct channel_path *chp = container_of(dev, struct channel_path, dev);

	if (!chp)
		return 0;
	return (get_chp_status(chp->id) ? sprintf(buf, "online\n") :
		sprintf(buf, "offline\n"));
}

static ssize_t
chp_status_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct channel_path *cp = container_of(dev, struct channel_path, dev);
	char cmd[10];
	int num_args;
	int error;

	num_args = sscanf(buf, "%5s", cmd);
	if (!num_args)
		return count;

	if (!strnicmp(cmd, "on", 2))
		error = s390_vary_chpid(cp->id, 1);
	else if (!strnicmp(cmd, "off", 3))
		error = s390_vary_chpid(cp->id, 0);
	else
		error = -EINVAL;

	return error < 0 ? error : count;

}

static DEVICE_ATTR(status, 0644, chp_status_show, chp_status_write);

static ssize_t
chp_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct channel_path *chp = container_of(dev, struct channel_path, dev);

	if (!chp)
		return 0;
	return sprintf(buf, "%x\n", chp->desc.desc);
}

static DEVICE_ATTR(type, 0444, chp_type_show, NULL);

static struct attribute * chp_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_type.attr,
	NULL,
};

static struct attribute_group chp_attr_group = {
	.attrs = chp_attrs,
};

static void
chp_release(struct device *dev)
{
	struct channel_path *cp;
	
	cp = container_of(dev, struct channel_path, dev);
	kfree(cp);
}

static int
chsc_determine_channel_path_description(int chpid,
					struct channel_path_desc *desc)
{
	int ccode, ret;

	struct {
		struct chsc_header request;
		u32 : 24;
		u32 first_chpid : 8;
		u32 : 24;
		u32 last_chpid : 8;
		u32 zeroes1;
		struct chsc_header response;
		u32 zeroes2;
		struct channel_path_desc desc;
	} *scpd_area;

	scpd_area = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!scpd_area)
		return -ENOMEM;

	scpd_area->request = (struct chsc_header) {
		.length = 0x0010,
		.code   = 0x0002,
	};

	scpd_area->first_chpid = chpid;
	scpd_area->last_chpid = chpid;

	ccode = chsc(scpd_area);
	if (ccode > 0) {
		ret = (ccode == 3) ? -ENODEV : -EBUSY;
		goto out;
	}

	switch (scpd_area->response.code) {
	case 0x0001: /* Success. */
		memcpy(desc, &scpd_area->desc,
		       sizeof(struct channel_path_desc));
		ret = 0;
		break;
	case 0x0003: /* Invalid block. */
	case 0x0007: /* Invalid format. */
	case 0x0008: /* Other invalid block. */
		CIO_CRW_EVENT(2, "Error in chsc request block!\n");
		ret = -EINVAL;
		break;
	case 0x0004: /* Command not provided in model. */
		CIO_CRW_EVENT(2, "Model does not provide scpd\n");
		ret = -EOPNOTSUPP;
		break;
	default:
		CIO_CRW_EVENT(2, "Unknown CHSC response %d\n",
			      scpd_area->response.code);
		ret = -EIO;
	}
out:
	free_page((unsigned long)scpd_area);
	return ret;
}

/*
 * Entries for chpids on the system bus.
 * This replaces /proc/chpids.
 */
static int
new_channel_path(int chpid)
{
	struct channel_path *chp;
	int ret;

	chp = kmalloc(sizeof(struct channel_path), GFP_KERNEL);
	if (!chp)
		return -ENOMEM;
	memset(chp, 0, sizeof(struct channel_path));

	/* fill in status, etc. */
	chp->id = chpid;
	chp->state = 1;
	chp->dev = (struct device) {
		.parent  = &css[0]->device,
		.release = chp_release,
	};
	snprintf(chp->dev.bus_id, BUS_ID_SIZE, "chp0.%x", chpid);

	/* Obtain channel path description and fill it in. */
	ret = chsc_determine_channel_path_description(chpid, &chp->desc);
	if (ret)
		goto out_free;

	/* make it known to the system */
	ret = device_register(&chp->dev);
	if (ret) {
		printk(KERN_WARNING "%s: could not register %02x\n",
		       __func__, chpid);
		goto out_free;
	}
	ret = sysfs_create_group(&chp->dev.kobj, &chp_attr_group);
	if (ret) {
		device_unregister(&chp->dev);
		goto out_free;
	} else
		css[0]->chps[chpid] = chp;
	return ret;
out_free:
	kfree(chp);
	return ret;
}

void *
chsc_get_chp_desc(struct subchannel *sch, int chp_no)
{
	struct channel_path *chp;
	struct channel_path_desc *desc;

	chp = css[0]->chps[sch->schib.pmcw.chpid[chp_no]];
	if (!chp)
		return NULL;
	desc = kmalloc(sizeof(struct channel_path_desc), GFP_KERNEL);
	if (!desc)
		return NULL;
	memcpy(desc, &chp->desc, sizeof(struct channel_path_desc));
	return desc;
}


static int __init
chsc_alloc_sei_area(void)
{
	sei_page = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sei_page)
		printk(KERN_WARNING"Can't allocate page for processing of " \
		       "chsc machine checks!\n");
	return (sei_page ? 0 : -ENOMEM);
}

int __init
chsc_enable_facility(int operation_code)
{
	int ret;
	struct {
		struct chsc_header request;
		u8 reserved1:4;
		u8 format:4;
		u8 reserved2;
		u16 operation_code;
		u32 reserved3;
		u32 reserved4;
		u32 operation_data_area[252];
		struct chsc_header response;
		u32 reserved5:4;
		u32 format2:4;
		u32 reserved6:24;
	} *sda_area;

	sda_area = (void *)get_zeroed_page(GFP_KERNEL|GFP_DMA);
	if (!sda_area)
		return -ENOMEM;
	sda_area->request = (struct chsc_header) {
		.length = 0x0400,
		.code = 0x0031,
	};
	sda_area->operation_code = operation_code;

	ret = chsc(sda_area);
	if (ret > 0) {
		ret = (ret == 3) ? -ENODEV : -EBUSY;
		goto out;
	}
	switch (sda_area->response.code) {
	case 0x0003: /* invalid request block */
	case 0x0007:
		ret = -EINVAL;
		break;
	case 0x0004: /* command not provided */
	case 0x0101: /* facility not provided */
		ret = -EOPNOTSUPP;
		break;
	}
 out:
	free_page((unsigned long)sda_area);
	return ret;
}

subsys_initcall(chsc_alloc_sei_area);

struct css_general_char css_general_characteristics;
struct css_chsc_char css_chsc_characteristics;

int __init
chsc_determine_css_characteristics(void)
{
	int result;
	struct {
		struct chsc_header request;
		u32 reserved1;
		u32 reserved2;
		u32 reserved3;
		struct chsc_header response;
		u32 reserved4;
		u32 general_char[510];
		u32 chsc_char[518];
	} *scsc_area;

	scsc_area = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!scsc_area) {
	        printk(KERN_WARNING"cio: Was not able to determine available" \
		       "CHSCs due to no memory.\n");
		return -ENOMEM;
	}

	scsc_area->request = (struct chsc_header) {
		.length = 0x0010,
		.code   = 0x0010,
	};

	result = chsc(scsc_area);
	if (result) {
		printk(KERN_WARNING"cio: Was not able to determine " \
		       "available CHSCs, cc=%i.\n", result);
		result = -EIO;
		goto exit;
	}

	if (scsc_area->response.code != 1) {
		printk(KERN_WARNING"cio: Was not able to determine " \
		       "available CHSCs.\n");
		result = -EIO;
		goto exit;
	}
	memcpy(&css_general_characteristics, scsc_area->general_char,
	       sizeof(css_general_characteristics));
	memcpy(&css_chsc_characteristics, scsc_area->chsc_char,
	       sizeof(css_chsc_characteristics));
exit:
	free_page ((unsigned long) scsc_area);
	return result;
}

EXPORT_SYMBOL_GPL(css_general_characteristics);
EXPORT_SYMBOL_GPL(css_chsc_characteristics);
