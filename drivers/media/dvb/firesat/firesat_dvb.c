#include <linux/init.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <ieee1394_hotplug.h>
#include <nodemgr.h>
#include <highlevel.h>
#include <ohci1394.h>
#include <hosts.h>
#include <dvbdev.h>

#include "firesat.h"
#include "avc_api.h"
#include "cmp.h"
#include "firesat-rc.h"
#include "firesat-ci.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static struct firesat_channel *firesat_channel_allocate(struct firesat *firesat)
{
	int k;

	printk(KERN_INFO "%s\n", __func__);

	if (down_interruptible(&firesat->demux_sem))
		return NULL;

	for (k = 0; k < 16; k++) {
		printk(KERN_INFO "%s: channel %d: active = %d, pid = 0x%x\n",__func__,k,firesat->channel[k].active,firesat->channel[k].pid);

		if (firesat->channel[k].active == 0) {
			firesat->channel[k].active = 1;
			up(&firesat->demux_sem);
			return &firesat->channel[k];
		}
	}

	up(&firesat->demux_sem);
	return NULL; // no more channels available
}

static int firesat_channel_collect(struct firesat *firesat, int *pidc, u16 pid[])
{
	int k, l = 0;

	if (down_interruptible(&firesat->demux_sem))
		return -EINTR;

	for (k = 0; k < 16; k++)
		if (firesat->channel[k].active == 1)
			pid[l++] = firesat->channel[k].pid;

	up(&firesat->demux_sem);

	*pidc = l;

	return 0;
}

static int firesat_channel_release(struct firesat *firesat,
				   struct firesat_channel *channel)
{
	if (down_interruptible(&firesat->demux_sem))
		return -EINTR;

	channel->active = 0;

	up(&firesat->demux_sem);
	return 0;
}

int firesat_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct firesat *firesat = (struct firesat*)dvbdmxfeed->demux->priv;
	struct firesat_channel *channel;
	int pidc,k;
	u16 pids[16];

	printk(KERN_INFO "%s (pid %u)\n",__func__,dvbdmxfeed->pid);

	switch (dvbdmxfeed->type) {
	case DMX_TYPE_TS:
	case DMX_TYPE_SEC:
		break;
	default:
		printk("%s: invalid type %u\n",__func__,dvbdmxfeed->type);
		return -EINVAL;
	}

	if (dvbdmxfeed->type == DMX_TYPE_TS) {
		switch (dvbdmxfeed->pes_type) {
		case DMX_TS_PES_VIDEO:
		case DMX_TS_PES_AUDIO:
		case DMX_TS_PES_TELETEXT:
		case DMX_TS_PES_PCR:
		case DMX_TS_PES_OTHER:
			//Dirty fix to keep firesat->channel pid-list up to date
			for(k=0;k<16;k++){
				if(firesat->channel[k].active == 0)
					firesat->channel[k].pid =
						dvbdmxfeed->pid;
					break;
			}
			channel = firesat_channel_allocate(firesat);
			break;
		default:
			printk("%s: invalid pes type %u\n",__func__, dvbdmxfeed->pes_type);
			return -EINVAL;
		}
	} else {
		channel = firesat_channel_allocate(firesat);
	}

	if (!channel) {
		printk("%s: busy!\n", __func__);
		return -EBUSY;
	}

	dvbdmxfeed->priv = channel;

	channel->dvbdmxfeed = dvbdmxfeed;
	channel->pid = dvbdmxfeed->pid;
	channel->type = dvbdmxfeed->type;
	channel->firesat = firesat;

	if (firesat_channel_collect(firesat, &pidc, pids)) {
		firesat_channel_release(firesat, channel);
		return -EINTR;
	}

	if(dvbdmxfeed->pid == 8192) {
		if((k=AVCTuner_GetTS(firesat))) {
			firesat_channel_release(firesat, channel);
			printk("%s: AVCTuner_GetTS failed with error %d\n",
				__func__,k);
			return k;
		}
	}
	else {
		if((k=AVCTuner_SetPIDs(firesat, pidc, pids))) {
			firesat_channel_release(firesat, channel);
			printk("%s: AVCTuner_SetPIDs failed with error %d\n",
				__func__,k);
			return k;
		}
	}

	return 0;
}

int firesat_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *demux = dvbdmxfeed->demux;
	struct firesat *firesat = (struct firesat*)demux->priv;
	int k, l = 0;
	u16 pids[16];

	printk(KERN_INFO "%s (pid %u)\n", __func__, dvbdmxfeed->pid);

	if (dvbdmxfeed->type == DMX_TYPE_TS && !((dvbdmxfeed->ts_type & TS_PACKET) &&
				(demux->dmx.frontend->source != DMX_MEMORY_FE))) {

		if (dvbdmxfeed->ts_type & TS_DECODER) {

			if (dvbdmxfeed->pes_type >= DMX_TS_PES_OTHER ||
				!demux->pesfilter[dvbdmxfeed->pes_type])

				return -EINVAL;

			demux->pids[dvbdmxfeed->pes_type] |= 0x8000;
			demux->pesfilter[dvbdmxfeed->pes_type] = 0;
		}

		if (!(dvbdmxfeed->ts_type & TS_DECODER &&
			dvbdmxfeed->pes_type < DMX_TS_PES_OTHER))

			return 0;
	}

	if (down_interruptible(&firesat->demux_sem))
		return -EINTR;


	// list except channel to be removed
	for (k = 0; k < 16; k++)
		if (firesat->channel[k].active == 1)
			if (&firesat->channel[k] !=
				(struct firesat_channel *)dvbdmxfeed->priv)
				pids[l++] = firesat->channel[k].pid;
			else
				firesat->channel[k].active = 0;

	if ((k = AVCTuner_SetPIDs(firesat, l, pids))) {
		up(&firesat->demux_sem);
		return k;
	}

	((struct firesat_channel *)dvbdmxfeed->priv)->active = 0;

	up(&firesat->demux_sem);

	return 0;
}

int firesat_dvbdev_init(struct firesat *firesat,
			struct device *dev,
			struct dvb_frontend *fe)
{
	int result;

		firesat->has_ci = 1; // TEMP workaround

#if 0
		switch (firesat->type) {
		case FireSAT_DVB_S:
			firesat->model_name = "FireSAT DVB-S";
			firesat->frontend_info = &firesat_S_frontend_info;
			break;
		case FireSAT_DVB_C:
			firesat->model_name = "FireSAT DVB-C";
			firesat->frontend_info = &firesat_C_frontend_info;
			break;
		case FireSAT_DVB_T:
			firesat->model_name = "FireSAT DVB-T";
			firesat->frontend_info = &firesat_T_frontend_info;
			break;
		default:
			printk("%s: unknown model type 0x%x on subunit %d!\n",
				__func__, firesat->type,subunit);
			firesat->model_name = "Unknown";
			firesat->frontend_info = NULL;
		}
#endif
/* // ------- CRAP -----------
		if (!firesat->frontend_info) {
			spin_lock_irqsave(&firesat_list_lock, flags);
			list_del(&firesat->list);
			spin_unlock_irqrestore(&firesat_list_lock, flags);
			kfree(firesat);
			continue;
		}
*/
		//initialising firesat->adapter before calling dvb_register_adapter
		if (!(firesat->adapter = kmalloc(sizeof (struct dvb_adapter), GFP_KERNEL))) {
			printk("%s: couldn't allocate memory.\n", __func__);
			kfree(firesat->adapter);
			kfree(firesat);
			return -ENOMEM;
		}

		if ((result = dvb_register_adapter(firesat->adapter,
						   firesat->model_name,
						   THIS_MODULE,
						   dev, adapter_nr)) < 0) {

			printk("%s: dvb_register_adapter failed: error %d\n", __func__, result);
#if 0
			/* ### cleanup */
			spin_lock_irqsave(&firesat_list_lock, flags);
			list_del(&firesat->list);
			spin_unlock_irqrestore(&firesat_list_lock, flags);
#endif
			kfree(firesat);

			return result;
		}

		firesat->demux.dmx.capabilities = 0/*DMX_TS_FILTERING | DMX_SECTION_FILTERING*/;

		firesat->demux.priv		= (void *)firesat;
		firesat->demux.filternum	= 16;
		firesat->demux.feednum		= 16;
		firesat->demux.start_feed	= firesat_start_feed;
		firesat->demux.stop_feed	= firesat_stop_feed;
		firesat->demux.write_to_decoder	= NULL;

		if ((result = dvb_dmx_init(&firesat->demux)) < 0) {
			printk("%s: dvb_dmx_init failed: error %d\n", __func__,
				   result);

			dvb_unregister_adapter(firesat->adapter);

			return result;
		}

		firesat->dmxdev.filternum	= 16;
		firesat->dmxdev.demux		= &firesat->demux.dmx;
		firesat->dmxdev.capabilities	= 0;

		if ((result = dvb_dmxdev_init(&firesat->dmxdev, firesat->adapter)) < 0) {
			printk("%s: dvb_dmxdev_init failed: error %d\n",
				   __func__, result);

			dvb_dmx_release(&firesat->demux);
			dvb_unregister_adapter(firesat->adapter);

			return result;
		}

		firesat->frontend.source = DMX_FRONTEND_0;

		if ((result = firesat->demux.dmx.add_frontend(&firesat->demux.dmx,
							  &firesat->frontend)) < 0) {
			printk("%s: dvb_dmx_init failed: error %d\n", __func__,
				   result);

			dvb_dmxdev_release(&firesat->dmxdev);
			dvb_dmx_release(&firesat->demux);
			dvb_unregister_adapter(firesat->adapter);

			return result;
		}

		if ((result = firesat->demux.dmx.connect_frontend(&firesat->demux.dmx,
								  &firesat->frontend)) < 0) {
			printk("%s: dvb_dmx_init failed: error %d\n", __func__,
				   result);

			firesat->demux.dmx.remove_frontend(&firesat->demux.dmx, &firesat->frontend);
			dvb_dmxdev_release(&firesat->dmxdev);
			dvb_dmx_release(&firesat->demux);
			dvb_unregister_adapter(firesat->adapter);

			return result;
		}

		dvb_net_init(firesat->adapter, &firesat->dvbnet, &firesat->demux.dmx);

//		fe->ops = firesat_ops;
//		fe->dvb = firesat->adapter;
		firesat_frontend_attach(firesat, fe);

		fe->sec_priv = firesat; //IMPORTANT, functions depend on this!!!
		if ((result= dvb_register_frontend(firesat->adapter, fe)) < 0) {
			printk("%s: dvb_register_frontend_new failed: error %d\n", __func__, result);
			/* ### cleanup */
			return result;
		}

		if (firesat->has_ci)
			firesat_ca_init(firesat);

		return 0;
}
