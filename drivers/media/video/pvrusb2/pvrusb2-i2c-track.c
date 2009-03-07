/*
 *
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "pvrusb2-i2c-track.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"
#include "pvrusb2-fx2-cmd.h"
#include "pvrusb2.h"

#define trace_i2c(...) pvr2_trace(PVR2_TRACE_I2C,__VA_ARGS__)


/*

  This module implements the foundation of a rather large architecture for
  tracking state in all the various V4L I2C modules.  This is obsolete with
  kernels later than roughly 2.6.24, but it is still present in the
  standalone pvrusb2 driver to allow continued operation with older
  kernel.

*/

static unsigned int pvr2_i2c_client_describe(struct pvr2_i2c_client *cp,
					     unsigned int detail,
					     char *buf,unsigned int maxlen);

static int pvr2_i2c_core_singleton(struct i2c_client *cp,
				   unsigned int cmd,void *arg)
{
	int stat;
	if (!cp) return -EINVAL;
	if (!(cp->driver)) return -EINVAL;
	if (!(cp->driver->command)) return -EINVAL;
	if (!try_module_get(cp->driver->driver.owner)) return -EAGAIN;
	stat = cp->driver->command(cp,cmd,arg);
	module_put(cp->driver->driver.owner);
	return stat;
}

int pvr2_i2c_client_cmd(struct pvr2_i2c_client *cp,unsigned int cmd,void *arg)
{
	int stat;
	if (pvrusb2_debug & PVR2_TRACE_I2C_CMD) {
		char buf[100];
		unsigned int cnt;
		cnt = pvr2_i2c_client_describe(cp,PVR2_I2C_DETAIL_DEBUG,
					       buf,sizeof(buf));
		pvr2_trace(PVR2_TRACE_I2C_CMD,
			   "i2c COMMAND (code=%u 0x%x) to %.*s",
			   cmd,cmd,cnt,buf);
	}
	stat = pvr2_i2c_core_singleton(cp->client,cmd,arg);
	if (pvrusb2_debug & PVR2_TRACE_I2C_CMD) {
		char buf[100];
		unsigned int cnt;
		cnt = pvr2_i2c_client_describe(cp,PVR2_I2C_DETAIL_DEBUG,
					       buf,sizeof(buf));
		pvr2_trace(PVR2_TRACE_I2C_CMD,
			   "i2c COMMAND to %.*s (ret=%d)",cnt,buf,stat);
	}
	return stat;
}

int pvr2_i2c_core_cmd(struct pvr2_hdw *hdw,unsigned int cmd,void *arg)
{
	struct pvr2_i2c_client *cp, *ncp;
	int stat = -EINVAL;

	if (!hdw) return stat;

	mutex_lock(&hdw->i2c_list_lock);
	list_for_each_entry_safe(cp, ncp, &hdw->i2c_clients, list) {
		if (!cp->recv_enable) continue;
		mutex_unlock(&hdw->i2c_list_lock);
		stat = pvr2_i2c_client_cmd(cp,cmd,arg);
		mutex_lock(&hdw->i2c_list_lock);
	}
	mutex_unlock(&hdw->i2c_list_lock);
	return stat;
}


static int handler_check(struct pvr2_i2c_client *cp)
{
	struct pvr2_i2c_handler *hp = cp->handler;
	if (!hp) return 0;
	if (!hp->func_table->check) return 0;
	return hp->func_table->check(hp->func_data) != 0;
}

#define BUFSIZE 500


void pvr2_i2c_core_status_poll(struct pvr2_hdw *hdw)
{
	struct pvr2_i2c_client *cp;
	mutex_lock(&hdw->i2c_list_lock); do {
		struct v4l2_tuner *vtp = &hdw->tuner_signal_info;
		memset(vtp,0,sizeof(*vtp));
		list_for_each_entry(cp, &hdw->i2c_clients, list) {
			if (!cp->detected_flag) continue;
			if (!cp->status_poll) continue;
			cp->status_poll(cp);
		}
		hdw->tuner_signal_stale = 0;
		pvr2_trace(PVR2_TRACE_CHIPS,"i2c status poll"
			   " type=%u strength=%u audio=0x%x cap=0x%x"
			   " low=%u hi=%u",
			   vtp->type,
			   vtp->signal,vtp->rxsubchans,vtp->capability,
			   vtp->rangelow,vtp->rangehigh);
	} while (0); mutex_unlock(&hdw->i2c_list_lock);
}


/* Issue various I2C operations to bring chip-level drivers into sync with
   state stored in this driver. */
void pvr2_i2c_core_sync(struct pvr2_hdw *hdw)
{
	unsigned long msk;
	unsigned int idx;
	struct pvr2_i2c_client *cp, *ncp;

	if (!hdw->i2c_linked) return;
	if (!(hdw->i2c_pend_types & PVR2_I2C_PEND_ALL)) {
		return;
	}
	mutex_lock(&hdw->i2c_list_lock); do {
		pvr2_trace(PVR2_TRACE_I2C_CORE,"i2c: core_sync BEGIN");
		if (hdw->i2c_pend_types & PVR2_I2C_PEND_DETECT) {
			/* One or more I2C clients have attached since we
			   last synced.  So scan the list and identify the
			   new clients. */
			char *buf;
			unsigned int cnt;
			unsigned long amask = 0;
			buf = kmalloc(BUFSIZE,GFP_KERNEL);
			pvr2_trace(PVR2_TRACE_I2C_CORE,"i2c: PEND_DETECT");
			hdw->i2c_pend_types &= ~PVR2_I2C_PEND_DETECT;
			list_for_each_entry(cp, &hdw->i2c_clients, list) {
				if (!cp->detected_flag) {
					cp->ctl_mask = 0;
					pvr2_i2c_probe(hdw,cp);
					cp->detected_flag = !0;
					msk = cp->ctl_mask;
					cnt = 0;
					if (buf) {
						cnt = pvr2_i2c_client_describe(
							cp,
							PVR2_I2C_DETAIL_ALL,
							buf,BUFSIZE);
					}
					trace_i2c("Probed: %.*s",cnt,buf);
					if (handler_check(cp)) {
						hdw->i2c_pend_types |=
							PVR2_I2C_PEND_CLIENT;
					}
					cp->pend_mask = msk;
					hdw->i2c_pend_mask |= msk;
					hdw->i2c_pend_types |=
						PVR2_I2C_PEND_REFRESH;
				}
				amask |= cp->ctl_mask;
			}
			hdw->i2c_active_mask = amask;
			if (buf) kfree(buf);
		}
		if (hdw->i2c_pend_types & PVR2_I2C_PEND_STALE) {
			/* Need to do one or more global updates.  Arrange
			   for this to happen. */
			unsigned long m2;
			pvr2_trace(PVR2_TRACE_I2C_CORE,
				   "i2c: PEND_STALE (0x%lx)",
				   hdw->i2c_stale_mask);
			hdw->i2c_pend_types &= ~PVR2_I2C_PEND_STALE;
			list_for_each_entry(cp, &hdw->i2c_clients, list) {
				m2 = hdw->i2c_stale_mask;
				m2 &= cp->ctl_mask;
				m2 &= ~cp->pend_mask;
				if (m2) {
					pvr2_trace(PVR2_TRACE_I2C_CORE,
						   "i2c: cp=%p setting 0x%lx",
						   cp,m2);
					cp->pend_mask |= m2;
				}
			}
			hdw->i2c_pend_mask |= hdw->i2c_stale_mask;
			hdw->i2c_stale_mask = 0;
			hdw->i2c_pend_types |= PVR2_I2C_PEND_REFRESH;
		}
		if (hdw->i2c_pend_types & PVR2_I2C_PEND_CLIENT) {
			/* One or more client handlers are asking for an
			   update.  Run through the list of known clients
			   and update each one. */
			pvr2_trace(PVR2_TRACE_I2C_CORE,"i2c: PEND_CLIENT");
			hdw->i2c_pend_types &= ~PVR2_I2C_PEND_CLIENT;
			list_for_each_entry_safe(cp, ncp, &hdw->i2c_clients,
						 list) {
				if (!cp->handler) continue;
				if (!cp->handler->func_table->update) continue;
				pvr2_trace(PVR2_TRACE_I2C_CORE,
					   "i2c: cp=%p update",cp);
				mutex_unlock(&hdw->i2c_list_lock);
				cp->handler->func_table->update(
					cp->handler->func_data);
				mutex_lock(&hdw->i2c_list_lock);
				/* If client's update function set some
				   additional pending bits, account for that
				   here. */
				if (cp->pend_mask & ~hdw->i2c_pend_mask) {
					hdw->i2c_pend_mask |= cp->pend_mask;
					hdw->i2c_pend_types |=
						PVR2_I2C_PEND_REFRESH;
				}
			}
		}
		if (hdw->i2c_pend_types & PVR2_I2C_PEND_REFRESH) {
			const struct pvr2_i2c_op *opf;
			unsigned long pm;
			/* Some actual updates are pending.  Walk through
			   each update type and perform it. */
			pvr2_trace(PVR2_TRACE_I2C_CORE,"i2c: PEND_REFRESH"
				   " (0x%lx)",hdw->i2c_pend_mask);
			hdw->i2c_pend_types &= ~PVR2_I2C_PEND_REFRESH;
			pm = hdw->i2c_pend_mask;
			hdw->i2c_pend_mask = 0;
			for (idx = 0, msk = 1; pm; idx++, msk <<= 1) {
				if (!(pm & msk)) continue;
				pm &= ~msk;
				list_for_each_entry(cp, &hdw->i2c_clients,
						    list) {
					if (cp->pend_mask & msk) {
						cp->pend_mask &= ~msk;
						cp->recv_enable = !0;
					} else {
						cp->recv_enable = 0;
					}
				}
				opf = pvr2_i2c_get_op(idx);
				if (!opf) continue;
				mutex_unlock(&hdw->i2c_list_lock);
				opf->update(hdw);
				mutex_lock(&hdw->i2c_list_lock);
			}
		}
		pvr2_trace(PVR2_TRACE_I2C_CORE,"i2c: core_sync END");
	} while (0); mutex_unlock(&hdw->i2c_list_lock);
}

int pvr2_i2c_core_check_stale(struct pvr2_hdw *hdw)
{
	unsigned long msk,sm,pm;
	unsigned int idx;
	const struct pvr2_i2c_op *opf;
	struct pvr2_i2c_client *cp;
	unsigned int pt = 0;

	pvr2_trace(PVR2_TRACE_I2C_CORE,"pvr2_i2c_core_check_stale BEGIN");

	pm = hdw->i2c_active_mask;
	sm = 0;
	for (idx = 0, msk = 1; pm; idx++, msk <<= 1) {
		if (!(msk & pm)) continue;
		pm &= ~msk;
		opf = pvr2_i2c_get_op(idx);
		if (!(opf && opf->check)) continue;
		if (opf->check(hdw)) {
			sm |= msk;
		}
	}
	if (sm) pt |= PVR2_I2C_PEND_STALE;

	list_for_each_entry(cp, &hdw->i2c_clients, list)
		if (handler_check(cp))
			pt |= PVR2_I2C_PEND_CLIENT;

	if (pt) {
		mutex_lock(&hdw->i2c_list_lock); do {
			hdw->i2c_pend_types |= pt;
			hdw->i2c_stale_mask |= sm;
			hdw->i2c_pend_mask |= hdw->i2c_stale_mask;
		} while (0); mutex_unlock(&hdw->i2c_list_lock);
	}

	pvr2_trace(PVR2_TRACE_I2C_CORE,
		   "i2c: types=0x%x stale=0x%lx pend=0x%lx",
		   hdw->i2c_pend_types,
		   hdw->i2c_stale_mask,
		   hdw->i2c_pend_mask);
	pvr2_trace(PVR2_TRACE_I2C_CORE,"pvr2_i2c_core_check_stale END");

	return (hdw->i2c_pend_types & PVR2_I2C_PEND_ALL) != 0;
}

static unsigned int pvr2_i2c_client_describe(struct pvr2_i2c_client *cp,
					     unsigned int detail,
					     char *buf,unsigned int maxlen)
{
	unsigned int ccnt,bcnt;
	int spcfl = 0;
	const struct pvr2_i2c_op *opf;

	ccnt = 0;
	if (detail & PVR2_I2C_DETAIL_DEBUG) {
		bcnt = scnprintf(buf,maxlen,
				 "ctxt=%p ctl_mask=0x%lx",
				 cp,cp->ctl_mask);
		ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
		spcfl = !0;
	}
	bcnt = scnprintf(buf,maxlen,
			 "%s%s @ 0x%x",
			 (spcfl ? " " : ""),
			 cp->client->name,
			 cp->client->addr);
	ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
	if ((detail & PVR2_I2C_DETAIL_HANDLER) &&
	    cp->handler && cp->handler->func_table->describe) {
		bcnt = scnprintf(buf,maxlen," (");
		ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
		bcnt = cp->handler->func_table->describe(
			cp->handler->func_data,buf,maxlen);
		ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
		bcnt = scnprintf(buf,maxlen,")");
		ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
	}
	if ((detail & PVR2_I2C_DETAIL_CTLMASK) && cp->ctl_mask) {
		unsigned int idx;
		unsigned long msk,sm;

		bcnt = scnprintf(buf,maxlen," [");
		ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
		sm = 0;
		spcfl = 0;
		for (idx = 0, msk = 1; msk; idx++, msk <<= 1) {
			if (!(cp->ctl_mask & msk)) continue;
			opf = pvr2_i2c_get_op(idx);
			if (opf) {
				bcnt = scnprintf(buf,maxlen,"%s%s",
						 spcfl ? " " : "",
						 opf->name);
				ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
				spcfl = !0;
			} else {
				sm |= msk;
			}
		}
		if (sm) {
			bcnt = scnprintf(buf,maxlen,"%s%lx",
					 idx != 0 ? " " : "",sm);
			ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
		}
		bcnt = scnprintf(buf,maxlen,"]");
		ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
	}
	return ccnt;
}

unsigned int pvr2_i2c_report(struct pvr2_hdw *hdw,
			     char *buf,unsigned int maxlen)
{
	unsigned int ccnt,bcnt;
	struct pvr2_i2c_client *cp;
	ccnt = 0;
	mutex_lock(&hdw->i2c_list_lock); do {
		list_for_each_entry(cp, &hdw->i2c_clients, list) {
			bcnt = pvr2_i2c_client_describe(
				cp,
				(PVR2_I2C_DETAIL_HANDLER|
				 PVR2_I2C_DETAIL_CTLMASK),
				buf,maxlen);
			ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
			bcnt = scnprintf(buf,maxlen,"\n");
			ccnt += bcnt; buf += bcnt; maxlen -= bcnt;
		}
	} while (0); mutex_unlock(&hdw->i2c_list_lock);
	return ccnt;
}

void pvr2_i2c_track_attach_inform(struct i2c_client *client)
{
	struct pvr2_hdw *hdw = (struct pvr2_hdw *)(client->adapter->algo_data);
	struct pvr2_i2c_client *cp;
	int fl = !(hdw->i2c_pend_types & PVR2_I2C_PEND_ALL);
	cp = kzalloc(sizeof(*cp),GFP_KERNEL);
	trace_i2c("i2c_attach [client=%s @ 0x%x ctxt=%p]",
		  client->name,
		  client->addr,cp);
	if (!cp) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			"Unable to allocate tracking memory for incoming"
			   " i2c module; ignoring module.  This is likely"
			   " going to be a problem.");
		return;
	}
	cp->hdw = hdw;
	INIT_LIST_HEAD(&cp->list);
	cp->client = client;
	mutex_lock(&hdw->i2c_list_lock); do {
		hdw->cropcap_stale = !0;
		list_add_tail(&cp->list,&hdw->i2c_clients);
		hdw->i2c_pend_types |= PVR2_I2C_PEND_DETECT;
	} while (0); mutex_unlock(&hdw->i2c_list_lock);
	if (fl) queue_work(hdw->workqueue,&hdw->worki2csync);
}

void pvr2_i2c_track_detach_inform(struct i2c_client *client)
{
	struct pvr2_hdw *hdw = (struct pvr2_hdw *)(client->adapter->algo_data);
	struct pvr2_i2c_client *cp, *ncp;
	unsigned long amask = 0;
	int foundfl = 0;
	mutex_lock(&hdw->i2c_list_lock); do {
		hdw->cropcap_stale = !0;
		list_for_each_entry_safe(cp, ncp, &hdw->i2c_clients, list) {
			if (cp->client == client) {
				trace_i2c("pvr2_i2c_detach"
					  " [client=%s @ 0x%x ctxt=%p]",
					  client->name,
					  client->addr,cp);
				if (cp->handler &&
				    cp->handler->func_table->detach) {
					cp->handler->func_table->detach(
						cp->handler->func_data);
				}
				list_del(&cp->list);
				kfree(cp);
				foundfl = !0;
				continue;
			}
			amask |= cp->ctl_mask;
		}
		hdw->i2c_active_mask = amask;
	} while (0); mutex_unlock(&hdw->i2c_list_lock);
	if (!foundfl) {
		trace_i2c("pvr2_i2c_detach [client=%s @ 0x%x ctxt=<unknown>]",
			  client->name,
			  client->addr);
	}
}

void pvr2_i2c_track_init(struct pvr2_hdw *hdw)
{
	hdw->i2c_pend_mask = 0;
	hdw->i2c_stale_mask = 0;
	hdw->i2c_active_mask = 0;
	INIT_LIST_HEAD(&hdw->i2c_clients);
	mutex_init(&hdw->i2c_list_lock);
}

void pvr2_i2c_track_done(struct pvr2_hdw *hdw)
{
	/* Empty for now */
}


/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 75 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */
