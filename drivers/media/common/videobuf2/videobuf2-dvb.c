// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * some helper function for simple DVB cards which simply DMA the
 * complete transport stream and let the computer sort everything else
 * (i.e. we are using the software demux, ...).  Also uses vb2
 * to manage DMA buffers.
 *
 * (c) 2004 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <media/videobuf2-dvb.h>

/* ------------------------------------------------------------------ */

MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------------ */

static int dvb_fnc(struct vb2_buffer *vb, void *priv)
{
	struct vb2_dvb *dvb = priv;

	dvb_dmx_swfilter(&dvb->demux, vb2_plane_vaddr(vb, 0),
				      vb2_get_plane_payload(vb, 0));
	return 0;
}

static int vb2_dvb_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct vb2_dvb *dvb = demux->priv;
	int rc = 0;

	if (!demux->dmx.frontend)
		return -EINVAL;

	mutex_lock(&dvb->lock);
	dvb->nfeeds++;

	if (!dvb->dvbq.threadio) {
		rc = vb2_thread_start(&dvb->dvbq, dvb_fnc, dvb, dvb->name);
		if (rc)
			dvb->nfeeds--;
	}
	if (!rc)
		rc = dvb->nfeeds;
	mutex_unlock(&dvb->lock);
	return rc;
}

static int vb2_dvb_stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct vb2_dvb *dvb = demux->priv;
	int err = 0;

	mutex_lock(&dvb->lock);
	dvb->nfeeds--;
	if (0 == dvb->nfeeds)
		err = vb2_thread_stop(&dvb->dvbq);
	mutex_unlock(&dvb->lock);
	return err;
}

static int vb2_dvb_register_adapter(struct vb2_dvb_frontends *fe,
			  struct module *module,
			  void *adapter_priv,
			  struct device *device,
			  struct media_device *mdev,
			  char *adapter_name,
			  short *adapter_nr,
			  int mfe_shared)
{
	int result;

	mutex_init(&fe->lock);

	/* register adapter */
	result = dvb_register_adapter(&fe->adapter, adapter_name, module,
		device, adapter_nr);
	if (result < 0) {
		pr_warn("%s: dvb_register_adapter failed (errno = %d)\n",
		       adapter_name, result);
	}
	fe->adapter.priv = adapter_priv;
	fe->adapter.mfe_shared = mfe_shared;
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	if (mdev)
		fe->adapter.mdev = mdev;
#endif
	return result;
}

static int vb2_dvb_register_frontend(struct dvb_adapter *adapter,
	struct vb2_dvb *dvb)
{
	int result;

	/* register frontend */
	result = dvb_register_frontend(adapter, dvb->frontend);
	if (result < 0) {
		pr_warn("%s: dvb_register_frontend failed (errno = %d)\n",
		       dvb->name, result);
		goto fail_frontend;
	}

	/* register demux stuff */
	dvb->demux.dmx.capabilities =
		DMX_TS_FILTERING | DMX_SECTION_FILTERING |
		DMX_MEMORY_BASED_FILTERING;
	dvb->demux.priv       = dvb;
	dvb->demux.filternum  = 256;
	dvb->demux.feednum    = 256;
	dvb->demux.start_feed = vb2_dvb_start_feed;
	dvb->demux.stop_feed  = vb2_dvb_stop_feed;
	result = dvb_dmx_init(&dvb->demux);
	if (result < 0) {
		pr_warn("%s: dvb_dmx_init failed (errno = %d)\n",
		       dvb->name, result);
		goto fail_dmx;
	}

	dvb->dmxdev.filternum    = 256;
	dvb->dmxdev.demux        = &dvb->demux.dmx;
	dvb->dmxdev.capabilities = 0;
	result = dvb_dmxdev_init(&dvb->dmxdev, adapter);

	if (result < 0) {
		pr_warn("%s: dvb_dmxdev_init failed (errno = %d)\n",
		       dvb->name, result);
		goto fail_dmxdev;
	}

	dvb->fe_hw.source = DMX_FRONTEND_0;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		pr_warn("%s: add_frontend failed (DMX_FRONTEND_0, errno = %d)\n",
		       dvb->name, result);
		goto fail_fe_hw;
	}

	dvb->fe_mem.source = DMX_MEMORY_FE;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	if (result < 0) {
		pr_warn("%s: add_frontend failed (DMX_MEMORY_FE, errno = %d)\n",
		       dvb->name, result);
		goto fail_fe_mem;
	}

	result = dvb->demux.dmx.connect_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		pr_warn("%s: connect_frontend failed (errno = %d)\n",
		       dvb->name, result);
		goto fail_fe_conn;
	}

	/* register network adapter */
	result = dvb_net_init(adapter, &dvb->net, &dvb->demux.dmx);
	if (result < 0) {
		pr_warn("%s: dvb_net_init failed (errno = %d)\n",
		       dvb->name, result);
		goto fail_fe_conn;
	}
	return 0;

fail_fe_conn:
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_mem);
fail_fe_mem:
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_hw);
fail_fe_hw:
	dvb_dmxdev_release(&dvb->dmxdev);
fail_dmxdev:
	dvb_dmx_release(&dvb->demux);
fail_dmx:
	dvb_unregister_frontend(dvb->frontend);
fail_frontend:
	dvb_frontend_detach(dvb->frontend);
	dvb->frontend = NULL;

	return result;
}

/* ------------------------------------------------------------------ */
/* Register a single adapter and one or more frontends */
int vb2_dvb_register_bus(struct vb2_dvb_frontends *f,
			 struct module *module,
			 void *adapter_priv,
			 struct device *device,
			 struct media_device *mdev,
			 short *adapter_nr,
			 int mfe_shared)
{
	struct list_head *list, *q;
	struct vb2_dvb_frontend *fe;
	int res;

	fe = vb2_dvb_get_frontend(f, 1);
	if (!fe) {
		pr_warn("Unable to register the adapter which has no frontends\n");
		return -EINVAL;
	}

	/* Bring up the adapter */
	res = vb2_dvb_register_adapter(f, module, adapter_priv, device, mdev,
		fe->dvb.name, adapter_nr, mfe_shared);
	if (res < 0) {
		pr_warn("vb2_dvb_register_adapter failed (errno = %d)\n", res);
		return res;
	}

	/* Attach all of the frontends to the adapter */
	mutex_lock(&f->lock);
	list_for_each_safe(list, q, &f->felist) {
		fe = list_entry(list, struct vb2_dvb_frontend, felist);
		res = vb2_dvb_register_frontend(&f->adapter, &fe->dvb);
		if (res < 0) {
			pr_warn("%s: vb2_dvb_register_frontend failed (errno = %d)\n",
				fe->dvb.name, res);
			goto err;
		}
		res = dvb_create_media_graph(&f->adapter, false);
		if (res < 0)
			goto err;
	}

	mutex_unlock(&f->lock);
	return 0;

err:
	mutex_unlock(&f->lock);
	vb2_dvb_unregister_bus(f);
	return res;
}
EXPORT_SYMBOL(vb2_dvb_register_bus);

void vb2_dvb_unregister_bus(struct vb2_dvb_frontends *f)
{
	vb2_dvb_dealloc_frontends(f);

	dvb_unregister_adapter(&f->adapter);
}
EXPORT_SYMBOL(vb2_dvb_unregister_bus);

struct vb2_dvb_frontend *vb2_dvb_get_frontend(
	struct vb2_dvb_frontends *f, int id)
{
	struct list_head *list, *q;
	struct vb2_dvb_frontend *fe, *ret = NULL;

	mutex_lock(&f->lock);

	list_for_each_safe(list, q, &f->felist) {
		fe = list_entry(list, struct vb2_dvb_frontend, felist);
		if (fe->id == id) {
			ret = fe;
			break;
		}
	}

	mutex_unlock(&f->lock);

	return ret;
}
EXPORT_SYMBOL(vb2_dvb_get_frontend);

int vb2_dvb_find_frontend(struct vb2_dvb_frontends *f,
	struct dvb_frontend *p)
{
	struct list_head *list, *q;
	struct vb2_dvb_frontend *fe = NULL;
	int ret = 0;

	mutex_lock(&f->lock);

	list_for_each_safe(list, q, &f->felist) {
		fe = list_entry(list, struct vb2_dvb_frontend, felist);
		if (fe->dvb.frontend == p) {
			ret = fe->id;
			break;
		}
	}

	mutex_unlock(&f->lock);

	return ret;
}
EXPORT_SYMBOL(vb2_dvb_find_frontend);

struct vb2_dvb_frontend *vb2_dvb_alloc_frontend(
	struct vb2_dvb_frontends *f, int id)
{
	struct vb2_dvb_frontend *fe;

	fe = kzalloc(sizeof(struct vb2_dvb_frontend), GFP_KERNEL);
	if (fe == NULL)
		return NULL;

	fe->id = id;
	mutex_init(&fe->dvb.lock);

	mutex_lock(&f->lock);
	list_add_tail(&fe->felist, &f->felist);
	mutex_unlock(&f->lock);
	return fe;
}
EXPORT_SYMBOL(vb2_dvb_alloc_frontend);

void vb2_dvb_dealloc_frontends(struct vb2_dvb_frontends *f)
{
	struct list_head *list, *q;
	struct vb2_dvb_frontend *fe;

	mutex_lock(&f->lock);
	list_for_each_safe(list, q, &f->felist) {
		fe = list_entry(list, struct vb2_dvb_frontend, felist);
		if (fe->dvb.net.dvbdev) {
			dvb_net_release(&fe->dvb.net);
			fe->dvb.demux.dmx.remove_frontend(&fe->dvb.demux.dmx,
				&fe->dvb.fe_mem);
			fe->dvb.demux.dmx.remove_frontend(&fe->dvb.demux.dmx,
				&fe->dvb.fe_hw);
			dvb_dmxdev_release(&fe->dvb.dmxdev);
			dvb_dmx_release(&fe->dvb.demux);
			dvb_unregister_frontend(fe->dvb.frontend);
		}
		if (fe->dvb.frontend)
			/* always allocated, may have been reset */
			dvb_frontend_detach(fe->dvb.frontend);
		list_del(list); /* remove list entry */
		kfree(fe);	/* free frontend allocation */
	}
	mutex_unlock(&f->lock);
}
EXPORT_SYMBOL(vb2_dvb_dealloc_frontends);
