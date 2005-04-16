/* pluto.c: SparcSTORAGE Array SCSI host adapter driver.
 *
 * Copyright (C) 1997,1998,1999 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/config.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#include <asm/irq.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "../fc4/fcp_impl.h"
#include "pluto.h"

#include <linux/module.h>

/* #define PLUTO_DEBUG */

#define pluto_printk printk ("PLUTO %s: ", fc->name); printk

#ifdef PLUTO_DEBUG
#define PLD(x)  pluto_printk x;
#define PLND(x) printk ("PLUTO: "); printk x;
#else
#define PLD(x)
#define PLND(x)
#endif

static struct ctrl_inquiry {
	struct Scsi_Host host;
	struct pluto pluto;
	Scsi_Cmnd cmd;
	char inquiry[256];
	fc_channel *fc;
} *fcs __initdata = { 0 };
static int fcscount __initdata = 0;
static atomic_t fcss __initdata = ATOMIC_INIT(0);
DECLARE_MUTEX_LOCKED(fc_sem);

static int pluto_encode_addr(Scsi_Cmnd *SCpnt, u16 *addr, fc_channel *fc, fcp_cmnd *fcmd);

static void __init pluto_detect_timeout(unsigned long data)
{
	PLND(("Timeout\n"))
	up(&fc_sem);
}

static void __init pluto_detect_done(Scsi_Cmnd *SCpnt)
{
	/* Do nothing */
}

static void __init pluto_detect_scsi_done(Scsi_Cmnd *SCpnt)
{
	SCpnt->request->rq_status = RQ_SCSI_DONE;
	PLND(("Detect done %08lx\n", (long)SCpnt))
	if (atomic_dec_and_test (&fcss))
		up(&fc_sem);
}

int pluto_slave_configure(Scsi_Device *device)
{
	int depth_to_use;

	if (device->tagged_supported)
		depth_to_use = /* 254 */ 8;
	else
		depth_to_use = 2;

	scsi_adjust_queue_depth(device,
				(device->tagged_supported ?
				 MSG_SIMPLE_TAG : 0),
				depth_to_use);

	return 0;
}

/* Detect all SSAs attached to the machine.
   To be fast, do it on all online FC channels at the same time. */
int __init pluto_detect(Scsi_Host_Template *tpnt)
{
	int i, retry, nplutos;
	fc_channel *fc;
	Scsi_Device dev;
	struct timer_list fc_timer =
		TIMER_INITIALIZER(pluto_detect_timeout, 0, 0);

	tpnt->proc_name = "pluto";
	fcscount = 0;
	for_each_online_fc_channel(fc) {
		if (!fc->posmap)
			fcscount++;
	}
	PLND(("%d channels online\n", fcscount))
	if (!fcscount) {
#if defined(MODULE) && defined(CONFIG_FC4_SOC_MODULE) && defined(CONFIG_KMOD)
		request_module("soc");
		
		for_each_online_fc_channel(fc) {
			if (!fc->posmap)
				fcscount++;
		}
		if (!fcscount)
#endif
			return 0;
	}
	fcs = (struct ctrl_inquiry *) kmalloc (sizeof (struct ctrl_inquiry) * fcscount, GFP_DMA);
	if (!fcs) {
		printk ("PLUTO: Not enough memory to probe\n");
		return 0;
	}
	
	memset (fcs, 0, sizeof (struct ctrl_inquiry) * fcscount);
	memset (&dev, 0, sizeof(dev));
	atomic_set (&fcss, fcscount);
	
	i = 0;
	for_each_online_fc_channel(fc) {
		Scsi_Cmnd *SCpnt;
		struct Scsi_Host *host;
		struct pluto *pluto;

		if (i == fcscount) break;
		if (fc->posmap) continue;
		
		PLD(("trying to find SSA\n"))

		/* If this is already registered to some other SCSI host, then it cannot be pluto */
		if (fc->scsi_name[0]) continue;
		memcpy (fc->scsi_name, "SSA", 4);
		
		fcs[i].fc = fc;
		
		fc->can_queue = PLUTO_CAN_QUEUE;
		fc->rsp_size = 64;
		fc->encode_addr = pluto_encode_addr;
		
		fc->fcp_register(fc, TYPE_SCSI_FCP, 0);
	
		SCpnt = &(fcs[i].cmd);
		host = &(fcs[i].host);
		pluto = (struct pluto *)host->hostdata;
		
		pluto->fc = fc;
	
		SCpnt->cmnd[0] = INQUIRY;
		SCpnt->cmnd[4] = 255;
		
		/* FC layer requires this, so that SCpnt->device->tagged_supported is initially 0 */
		SCpnt->device = &dev;
		dev.host = host;
		
		SCpnt->cmd_len = COMMAND_SIZE(INQUIRY);
	
		SCpnt->request->rq_status = RQ_SCSI_BUSY;
		
		SCpnt->done = pluto_detect_done;
		SCpnt->bufflen = 256;
		SCpnt->buffer = fcs[i].inquiry;
		SCpnt->request_bufflen = 256;
		SCpnt->request_buffer = fcs[i].inquiry;
		PLD(("set up %d %08lx\n", i, (long)SCpnt))
		i++;
	}
	
	for (retry = 0; retry < 5; retry++) {
		for (i = 0; i < fcscount; i++) {
			if (!fcs[i].fc) break;
			if (fcs[i].cmd.request->rq_status != RQ_SCSI_DONE) {
				disable_irq(fcs[i].fc->irq);
				PLND(("queuecommand %d %d\n", retry, i))
				fcp_scsi_queuecommand (&(fcs[i].cmd), 
					pluto_detect_scsi_done);
				enable_irq(fcs[i].fc->irq);
			}
		}
	    
		fc_timer.expires = jiffies + 10 * HZ;
		add_timer(&fc_timer);
		
		down(&fc_sem);
		PLND(("Woken up\n"))
		if (!atomic_read(&fcss))
			break; /* All fc channels have answered us */
	}
	del_timer_sync(&fc_timer);

	PLND(("Finished search\n"))
	for (i = 0, nplutos = 0; i < fcscount; i++) {
		Scsi_Cmnd *SCpnt;
		
		if (!(fc = fcs[i].fc)) break;
	
		SCpnt = &(fcs[i].cmd);
		
		/* Let FC mid-level free allocated resources */
		SCpnt->done (SCpnt);
		
		if (!SCpnt->result) {
			struct pluto_inquiry *inq;
			struct pluto *pluto;
			struct Scsi_Host *host;
			
			inq = (struct pluto_inquiry *)fcs[i].inquiry;

			if ((inq->dtype & 0x1f) == TYPE_PROCESSOR &&
			    !strncmp (inq->vendor_id, "SUN", 3) &&
			    !strncmp (inq->product_id, "SSA", 3)) {
				char *p;
				long *ages;
				
				ages = kmalloc (((inq->channels + 1) * inq->targets) * sizeof(long), GFP_KERNEL);
				if (!ages) continue;
				
				host = scsi_register (tpnt, sizeof (struct pluto));
				if(!host)
				{
					kfree(ages);
					continue;
				}
				
				if (!try_module_get(fc->module)) {
					kfree(ages);
					scsi_unregister(host);
					continue;
				}

				nplutos++;
				
				pluto = (struct pluto *)host->hostdata;
				
				host->max_id = inq->targets;
				host->max_channel = inq->channels;
				host->irq = fc->irq;

				fc->channels = inq->channels + 1;
				fc->targets = inq->targets;
				fc->ages = ages;
				memset (ages, 0, ((inq->channels + 1) * inq->targets) * sizeof(long));
				
				pluto->fc = fc;
				memcpy (pluto->rev_str, inq->revision, 4);
				pluto->rev_str[4] = 0;
				p = strchr (pluto->rev_str, ' ');
				if (p) *p = 0;
				memcpy (pluto->fw_rev_str, inq->fw_revision, 4);
				pluto->fw_rev_str[4] = 0;
				p = strchr (pluto->fw_rev_str, ' ');
				if (p) *p = 0;
				memcpy (pluto->serial_str, inq->serial, 12);
				pluto->serial_str[12] = 0;
				p = strchr (pluto->serial_str, ' ');
				if (p) *p = 0;
				
				PLD(("Found SSA rev %s fw rev %s serial %s %dx%d\n", pluto->rev_str, pluto->fw_rev_str, pluto->serial_str, host->max_channel, host->max_id))
			} else
				fc->fcp_register(fc, TYPE_SCSI_FCP, 1);
		} else
			fc->fcp_register(fc, TYPE_SCSI_FCP, 1);
	}
	kfree((char *)fcs);
	if (nplutos)
		printk ("PLUTO: Total of %d SparcSTORAGE Arrays found\n", nplutos);
	return nplutos;
}

int pluto_release(struct Scsi_Host *host)
{
	struct pluto *pluto = (struct pluto *)host->hostdata;
	fc_channel *fc = pluto->fc;

	module_put(fc->module);
	
	fc->fcp_register(fc, TYPE_SCSI_FCP, 1);
	PLND((" releasing pluto.\n"));
	kfree (fc->ages);
	PLND(("released pluto!\n"));
	return 0;
}

const char *pluto_info(struct Scsi_Host *host)
{
	static char buf[128], *p;
	struct pluto *pluto = (struct pluto *) host->hostdata;

	sprintf(buf, "SUN SparcSTORAGE Array %s fw %s serial %s %dx%d on %s",
		pluto->rev_str, pluto->fw_rev_str, pluto->serial_str,
		host->max_channel, host->max_id, pluto->fc->name);
#ifdef __sparc__
	p = strchr(buf, 0);
	sprintf(p, " PROM node %x", pluto->fc->dev->prom_node);
#endif	
	return buf;
}

/* SSA uses this FC4S addressing:
   switch (addr[0])
   {
   case 0: CONTROLLER - All of addr[1]..addr[3] has to be 0
   case 1: SINGLE DISK - addr[1] channel, addr[2] id, addr[3] 0
   case 2: DISK GROUP - ???
   }
   
   So that SCSI mid-layer can access to these, we reserve
   channel 0 id 0 lun 0 for CONTROLLER
   and channels 1 .. max_channel are normal single disks.
 */
static int pluto_encode_addr(Scsi_Cmnd *SCpnt, u16 *addr, fc_channel *fc, fcp_cmnd *fcmd)
{
	PLND(("encode addr %d %d %d\n", SCpnt->device->channel, SCpnt->device->id, SCpnt->cmnd[1] & 0xe0))
	/* We don't support LUNs - neither does SSA :) */
	if (SCpnt->cmnd[1] & 0xe0)
		return -EINVAL;
	if (!SCpnt->device->channel) {
		if (SCpnt->device->id)
			return -EINVAL;
		memset (addr, 0, 4 * sizeof(u16));
	} else {
		addr[0] = 1;
		addr[1] = SCpnt->device->channel - 1;
		addr[2] = SCpnt->device->id;
		addr[3] = 0;
	}
	/* We're Point-to-Point, so target it to the default DID */
	fcmd->did = fc->did;
	PLND(("trying %04x%04x%04x%04x\n", addr[0], addr[1], addr[2], addr[3]))
	return 0;
}

static Scsi_Host_Template driver_template = {
	.name			= "Sparc Storage Array 100/200",
	.detect			= pluto_detect,
	.release		= pluto_release,
	.info			= pluto_info,
	.queuecommand		= fcp_scsi_queuecommand,
	.slave_configure	= pluto_slave_configure,
	.can_queue		= PLUTO_CAN_QUEUE,
	.this_id		= -1,
	.sg_tablesize		= 1,
	.cmd_per_lun		= 1,
	.use_clustering		= ENABLE_CLUSTERING,
	.eh_abort_handler	= fcp_scsi_abort,
	.eh_device_reset_handler = fcp_scsi_dev_reset,
	.eh_bus_reset_handler	= fcp_scsi_bus_reset,
	.eh_host_reset_handler	= fcp_scsi_host_reset,
};

#include "scsi_module.c"

MODULE_LICENSE("GPL");

