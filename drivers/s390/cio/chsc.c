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

	ssd_area->request.length = 0x0010;
	ssd_area->request.code = 0x0004;

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
		int j, chpid, mask;
		/* Allocate channel path structures, if needed. */
		for (j = 0; j < 8; j++) {
			mask = 0x80 >> j;
			chpid = sch->ssd_info.chpid[j];
			if ((sch->schib.pmcw.pim & mask) &&
			    (get_chp_status(chpid) < 0))
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
	for (j = 0; j < 8; j++) {
		mask = 0x80 >> j;
		if ((sch->schib.pmcw.pim & mask) &&
		    (sch->schib.pmcw.chpid[j] == chpid->id))
			break;
	}
	if (j >= 8)
		return 0;

	spin_lock_irq(&sch->lock);

	stsch(sch->schid, &schib);
	if (!schib.pmcw.dnv)
		goto out_unreg;
	memcpy(&sch->schib, &schib, sizeof(struct schib));
	/* Check for single path devices. */
	if (sch->schib.pmcw.pim == 0x80)
		goto out_unreg;

	if ((sch->schib.scsw.actl & SCSW_ACTL_DEVACT) &&
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
	else if (sch->lpm == mask)
		goto out_unreg;
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

	res_data = data;
	sch = get_subchannel_by_schid(schid);
	if (!sch)
		/* Check if a subchannel is newly available. */
		return s390_process_res_acc_new_sch(schid);

	spin_lock_irq(&sch->lock);

	chp_mask = s390_process_res_acc_sch(res_data, sch);

	if (chp_mask == 0) {
		spin_unlock_irq(&sch->lock);
		put_device(&sch->dev);
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
	return 0;
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

	lir = data;
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
		sei_area->request.length = 0x0010;
		sei_area->request.code = 0x000e;

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
	int i, mask;
	struct channel_path *chp;
	struct subchannel *sch;

	chp = data;
	sch = get_subchannel_by_schid(schid);
	if (!sch)
		/* Check if the subchannel is now available. */
		return __chp_add_new_sch(schid);
	spin_lock_irq(&sch->lock);
	for (i=0; i<8; i++) {
		mask = 0x80 >> i;
		if ((sch->schib.pmcw.pim & mask) &&
		    (sch->schib.pmcw.chpid[i] == chp->id)) {
			if (stsch(sch->schid, &sch->schib) != 0) {
				/* Endgame. */
				spin_unlock_irq(&sch->lock);
				return -ENXIO;
			}
			break;
		}
	}
	if (i==8) {
		spin_unlock_irq(&sch->lock);
		return 0;
	}
	sch->lpm = ((sch->schib.pmcw.pim &
		     sch->schib.pmcw.pam &
		     sch->schib.pmcw.pom)
		    | mask) & sch->opm;

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

static inline int check_for_io_on_path(struct subchannel *sch, int index)
{
	int cc;

	if (!device_is_online(sch))
		/* cio could be doing I/O. */
		return 0;
	cc = stsch(sch->schid, &sch->schib);
	if (cc)
		return 0;
	if (sch->schib.scsw.actl && sch->schib.pmcw.lpum == (0x80 >> index))
		return 1;
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
			if (check_for_io_on_path(sch, chp))
				/* Path verification is done after killing. */
				device_kill_io(sch);
			else if (!sch->lpm) {
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
 * Channel measurement related functions
 */
static ssize_t
chp_measurement_chars_read(struct kobject *kobj, char *buf, loff_t off,
			   size_t count)
{
	struct channel_path *chp;
	unsigned int size;

	chp = to_channelpath(container_of(kobj, struct device, kobj));
	if (!chp->cmg_chars)
		return 0;

	size = sizeof(struct cmg_chars);

	if (off > size)
		return 0;
	if (off + count > size)
		count = size - off;
	memcpy(buf, chp->cmg_chars + off, count);
	return count;
}

static struct bin_attribute chp_measurement_chars_attr = {
	.attr = {
		.name = "measurement_chars",
		.mode = S_IRUSR,
		.owner = THIS_MODULE,
	},
	.size = sizeof(struct cmg_chars),
	.read = chp_measurement_chars_read,
};

static void
chp_measurement_copy_block(struct cmg_entry *buf,
			   struct channel_subsystem *css, int chpid)
{
	void *area;
	struct cmg_entry *entry, reference_buf;
	int idx;

	if (chpid < 128) {
		area = css->cub_addr1;
		idx = chpid;
	} else {
		area = css->cub_addr2;
		idx = chpid - 128;
	}
	entry = area + (idx * sizeof(struct cmg_entry));
	do {
		memcpy(buf, entry, sizeof(*entry));
		memcpy(&reference_buf, entry, sizeof(*entry));
	} while (reference_buf.values[0] != buf->values[0]);
}

static ssize_t
chp_measurement_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct channel_path *chp;
	struct channel_subsystem *css;
	unsigned int size;

	chp = to_channelpath(container_of(kobj, struct device, kobj));
	css = to_css(chp->dev.parent);

	size = sizeof(struct cmg_entry);

	/* Only allow single reads. */
	if (off || count < size)
		return 0;
	chp_measurement_copy_block((struct cmg_entry *)buf, css, chp->id);
	count = size;
	return count;
}

static struct bin_attribute chp_measurement_attr = {
	.attr = {
		.name = "measurement",
		.mode = S_IRUSR,
		.owner = THIS_MODULE,
	},
	.size = sizeof(struct cmg_entry),
	.read = chp_measurement_read,
};

static void
chsc_remove_chp_cmg_attr(struct channel_path *chp)
{
	sysfs_remove_bin_file(&chp->dev.kobj, &chp_measurement_chars_attr);
	sysfs_remove_bin_file(&chp->dev.kobj, &chp_measurement_attr);
}

static int
chsc_add_chp_cmg_attr(struct channel_path *chp)
{
	int ret;

	ret = sysfs_create_bin_file(&chp->dev.kobj,
				    &chp_measurement_chars_attr);
	if (ret)
		return ret;
	ret = sysfs_create_bin_file(&chp->dev.kobj, &chp_measurement_attr);
	if (ret)
		sysfs_remove_bin_file(&chp->dev.kobj,
				      &chp_measurement_chars_attr);
	return ret;
}

static void
chsc_remove_cmg_attr(struct channel_subsystem *css)
{
	int i;

	for (i = 0; i <= __MAX_CHPID; i++) {
		if (!css->chps[i])
			continue;
		chsc_remove_chp_cmg_attr(css->chps[i]);
	}
}

static int
chsc_add_cmg_attr(struct channel_subsystem *css)
{
	int i, ret;

	ret = 0;
	for (i = 0; i <= __MAX_CHPID; i++) {
		if (!css->chps[i])
			continue;
		ret = chsc_add_chp_cmg_attr(css->chps[i]);
		if (ret)
			goto cleanup;
	}
	return ret;
cleanup:
	for (--i; i >= 0; i--) {
		if (!css->chps[i])
			continue;
		chsc_remove_chp_cmg_attr(css->chps[i]);
	}
	return ret;
}


static int
__chsc_do_secm(struct channel_subsystem *css, int enable, void *page)
{
	struct {
		struct chsc_header request;
		u32 operation_code : 2;
		u32 : 30;
		u32 key : 4;
		u32 : 28;
		u32 zeroes1;
		u32 cub_addr1;
		u32 zeroes2;
		u32 cub_addr2;
		u32 reserved[13];
		struct chsc_header response;
		u32 status : 8;
		u32 : 4;
		u32 fmt : 4;
		u32 : 16;
	} *secm_area;
	int ret, ccode;

	secm_area = page;
	secm_area->request.length = 0x0050;
	secm_area->request.code = 0x0016;

	secm_area->key = PAGE_DEFAULT_KEY;
	secm_area->cub_addr1 = (u64)(unsigned long)css->cub_addr1;
	secm_area->cub_addr2 = (u64)(unsigned long)css->cub_addr2;

	secm_area->operation_code = enable ? 0 : 1;

	ccode = chsc(secm_area);
	if (ccode > 0)
		return (ccode == 3) ? -ENODEV : -EBUSY;

	switch (secm_area->response.code) {
	case 0x0001: /* Success. */
		ret = 0;
		break;
	case 0x0003: /* Invalid block. */
	case 0x0007: /* Invalid format. */
	case 0x0008: /* Other invalid block. */
		CIO_CRW_EVENT(2, "Error in chsc request block!\n");
		ret = -EINVAL;
		break;
	case 0x0004: /* Command not provided in model. */
		CIO_CRW_EVENT(2, "Model does not provide secm\n");
		ret = -EOPNOTSUPP;
		break;
	case 0x0102: /* cub adresses incorrect */
		CIO_CRW_EVENT(2, "Invalid addresses in chsc request block\n");
		ret = -EINVAL;
		break;
	case 0x0103: /* key error */
		CIO_CRW_EVENT(2, "Access key error in secm\n");
		ret = -EINVAL;
		break;
	case 0x0105: /* error while starting */
		CIO_CRW_EVENT(2, "Error while starting channel measurement\n");
		ret = -EIO;
		break;
	default:
		CIO_CRW_EVENT(2, "Unknown CHSC response %d\n",
			      secm_area->response.code);
		ret = -EIO;
	}
	return ret;
}

int
chsc_secm(struct channel_subsystem *css, int enable)
{
	void  *secm_area;
	int ret;

	secm_area = (void *)get_zeroed_page(GFP_KERNEL |  GFP_DMA);
	if (!secm_area)
		return -ENOMEM;

	mutex_lock(&css->mutex);
	if (enable && !css->cm_enabled) {
		css->cub_addr1 = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
		css->cub_addr2 = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
		if (!css->cub_addr1 || !css->cub_addr2) {
			free_page((unsigned long)css->cub_addr1);
			free_page((unsigned long)css->cub_addr2);
			free_page((unsigned long)secm_area);
			mutex_unlock(&css->mutex);
			return -ENOMEM;
		}
	}
	ret = __chsc_do_secm(css, enable, secm_area);
	if (!ret) {
		css->cm_enabled = enable;
		if (css->cm_enabled) {
			ret = chsc_add_cmg_attr(css);
			if (ret) {
				memset(secm_area, 0, PAGE_SIZE);
				__chsc_do_secm(css, 0, secm_area);
				css->cm_enabled = 0;
			}
		} else
			chsc_remove_cmg_attr(css);
	}
	if (enable && !css->cm_enabled) {
		free_page((unsigned long)css->cub_addr1);
		free_page((unsigned long)css->cub_addr2);
	}
	mutex_unlock(&css->mutex);
	free_page((unsigned long)secm_area);
	return ret;
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

static ssize_t
chp_cmg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct channel_path *chp = to_channelpath(dev);

	if (!chp)
		return 0;
	if (chp->cmg == -1) /* channel measurements not available */
		return sprintf(buf, "unknown\n");
	return sprintf(buf, "%x\n", chp->cmg);
}

static DEVICE_ATTR(cmg, 0444, chp_cmg_show, NULL);

static ssize_t
chp_shared_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct channel_path *chp = to_channelpath(dev);

	if (!chp)
		return 0;
	if (chp->shared == -1) /* channel measurements not available */
		return sprintf(buf, "unknown\n");
	return sprintf(buf, "%x\n", chp->shared);
}

static DEVICE_ATTR(shared, 0444, chp_shared_show, NULL);

static struct attribute * chp_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_type.attr,
	&dev_attr_cmg.attr,
	&dev_attr_shared.attr,
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

	scpd_area->request.length = 0x0010;
	scpd_area->request.code = 0x0002;

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

static void
chsc_initialize_cmg_chars(struct channel_path *chp, u8 cmcv,
			  struct cmg_chars *chars)
{
	switch (chp->cmg) {
	case 2:
	case 3:
		chp->cmg_chars = kmalloc(sizeof(struct cmg_chars),
					 GFP_KERNEL);
		if (chp->cmg_chars) {
			int i, mask;
			struct cmg_chars *cmg_chars;

			cmg_chars = chp->cmg_chars;
			for (i = 0; i < NR_MEASUREMENT_CHARS; i++) {
				mask = 0x80 >> (i + 3);
				if (cmcv & mask)
					cmg_chars->values[i] = chars->values[i];
				else
					cmg_chars->values[i] = 0;
			}
		}
		break;
	default:
		/* No cmg-dependent data. */
		break;
	}
}

static int
chsc_get_channel_measurement_chars(struct channel_path *chp)
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
		u32 not_valid : 1;
		u32 shared : 1;
		u32 : 22;
		u32 chpid : 8;
		u32 cmcv : 5;
		u32 : 11;
		u32 cmgq : 8;
		u32 cmg : 8;
		u32 zeroes3;
		u32 data[NR_MEASUREMENT_CHARS];
	} *scmc_area;

	scmc_area = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!scmc_area)
		return -ENOMEM;

	scmc_area->request.length = 0x0010;
	scmc_area->request.code = 0x0022;

	scmc_area->first_chpid = chp->id;
	scmc_area->last_chpid = chp->id;

	ccode = chsc(scmc_area);
	if (ccode > 0) {
		ret = (ccode == 3) ? -ENODEV : -EBUSY;
		goto out;
	}

	switch (scmc_area->response.code) {
	case 0x0001: /* Success. */
		if (!scmc_area->not_valid) {
			chp->cmg = scmc_area->cmg;
			chp->shared = scmc_area->shared;
			chsc_initialize_cmg_chars(chp, scmc_area->cmcv,
						  (struct cmg_chars *)
						  &scmc_area->data);
		} else {
			chp->cmg = -1;
			chp->shared = -1;
		}
		ret = 0;
		break;
	case 0x0003: /* Invalid block. */
	case 0x0007: /* Invalid format. */
	case 0x0008: /* Invalid bit combination. */
		CIO_CRW_EVENT(2, "Error in chsc request block!\n");
		ret = -EINVAL;
		break;
	case 0x0004: /* Command not provided. */
		CIO_CRW_EVENT(2, "Model does not provide scmc\n");
		ret = -EOPNOTSUPP;
		break;
	default:
		CIO_CRW_EVENT(2, "Unknown CHSC response %d\n",
			      scmc_area->response.code);
		ret = -EIO;
	}
out:
	free_page((unsigned long)scmc_area);
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

	chp = kzalloc(sizeof(struct channel_path), GFP_KERNEL);
	if (!chp)
		return -ENOMEM;

	/* fill in status, etc. */
	chp->id = chpid;
	chp->state = 1;
	chp->dev.parent = &css[0]->device;
	chp->dev.release = chp_release;
	snprintf(chp->dev.bus_id, BUS_ID_SIZE, "chp0.%x", chpid);

	/* Obtain channel path description and fill it in. */
	ret = chsc_determine_channel_path_description(chpid, &chp->desc);
	if (ret)
		goto out_free;
	/* Get channel-measurement characteristics. */
	if (css_characteristics_avail && css_chsc_characteristics.scmc
	    && css_chsc_characteristics.secm) {
		ret = chsc_get_channel_measurement_chars(chp);
		if (ret)
			goto out_free;
	} else {
		static int msg_done;

		if (!msg_done) {
			printk(KERN_WARNING "cio: Channel measurements not "
			       "available, continuing.\n");
			msg_done = 1;
		}
		chp->cmg = -1;
	}

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
	}
	mutex_lock(&css[0]->mutex);
	if (css[0]->cm_enabled) {
		ret = chsc_add_chp_cmg_attr(chp);
		if (ret) {
			sysfs_remove_group(&chp->dev.kobj, &chp_attr_group);
			device_unregister(&chp->dev);
			mutex_unlock(&css[0]->mutex);
			goto out_free;
		}
	}
	css[0]->chps[chpid] = chp;
	mutex_unlock(&css[0]->mutex);
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
	sda_area->request.length = 0x0400;
	sda_area->request.code = 0x0031;
	sda_area->operation_code = operation_code;

	ret = chsc(sda_area);
	if (ret > 0) {
		ret = (ret == 3) ? -ENODEV : -EBUSY;
		goto out;
	}
	switch (sda_area->response.code) {
	case 0x0001: /* everything ok */
		ret = 0;
		break;
	case 0x0003: /* invalid request block */
	case 0x0007:
		ret = -EINVAL;
		break;
	case 0x0004: /* command not provided */
	case 0x0101: /* facility not provided */
		ret = -EOPNOTSUPP;
		break;
	default: /* something went wrong */
		ret = -EIO;
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

	scsc_area->request.length = 0x0010;
	scsc_area->request.code = 0x0010;

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
