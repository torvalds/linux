/* fcal.c: Fibre Channel Arbitrated Loop SCSI host adapter driver.
 *
 * Copyright (C) 1998,1999 Jakub Jelinek (jj@ultra.linux.cz)
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
#include "fcal.h"

#include <linux/module.h>

/* #define FCAL_DEBUG */

#define fcal_printk printk ("FCAL %s: ", fc->name); printk

#ifdef FCAL_DEBUG
#define FCALD(x)  fcal_printk x;
#define FCALND(x) printk ("FCAL: "); printk x;
#else
#define FCALD(x)
#define FCALND(x)
#endif

static unsigned char alpa2target[] = {
0x7e, 0x7d, 0x7c, 0xff, 0x7b, 0xff, 0xff, 0xff, 0x7a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x79,
0x78, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x77, 0x76, 0xff, 0xff, 0x75, 0xff, 0x74, 0x73, 0x72,
0xff, 0xff, 0xff, 0x71, 0xff, 0x70, 0x6f, 0x6e, 0xff, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x68, 0xff,
0xff, 0x67, 0x66, 0x65, 0x64, 0x63, 0x62, 0xff, 0xff, 0x61, 0x60, 0xff, 0x5f, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0x5e, 0xff, 0x5d, 0x5c, 0x5b, 0xff, 0x5a, 0x59, 0x58, 0x57, 0x56, 0x55, 0xff,
0xff, 0x54, 0x53, 0x52, 0x51, 0x50, 0x4f, 0xff, 0xff, 0x4e, 0x4d, 0xff, 0x4c, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0x4b, 0xff, 0x4a, 0x49, 0x48, 0xff, 0x47, 0x46, 0x45, 0x44, 0x43, 0x42, 0xff,
0xff, 0x41, 0x40, 0x3f, 0x3e, 0x3d, 0x3c, 0xff, 0xff, 0x3b, 0x3a, 0xff, 0x39, 0xff, 0xff, 0xff,
0x38, 0x37, 0x36, 0xff, 0x35, 0xff, 0xff, 0xff, 0x34, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x33,
0x32, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x31, 0x30, 0xff, 0xff, 0x2f, 0xff, 0x2e, 0x2d, 0x2c,
0xff, 0xff, 0xff, 0x2b, 0xff, 0x2a, 0x29, 0x28, 0xff, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0xff,
0xff, 0x21, 0x20, 0x1f, 0x1e, 0x1d, 0x1c, 0xff, 0xff, 0x1b, 0x1a, 0xff, 0x19, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0x18, 0xff, 0x17, 0x16, 0x15, 0xff, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0f, 0xff,
0xff, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0xff, 0xff, 0x08, 0x07, 0xff, 0x06, 0xff, 0xff, 0xff,
0x05, 0x04, 0x03, 0xff, 0x02, 0xff, 0xff, 0xff, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};

static unsigned char target2alpa[] = {
0xef, 0xe8, 0xe4, 0xe2, 0xe1, 0xe0, 0xdc, 0xda, 0xd9, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xce,
0xcd, 0xcc, 0xcb, 0xca, 0xc9, 0xc7, 0xc6, 0xc5, 0xc3, 0xbc, 0xba, 0xb9, 0xb6, 0xb5, 0xb4, 0xb3,
0xb2, 0xb1, 0xae, 0xad, 0xac, 0xab, 0xaa, 0xa9, 0xa7, 0xa6, 0xa5, 0xa3, 0x9f, 0x9e, 0x9d, 0x9b,
0x98, 0x97, 0x90, 0x8f, 0x88, 0x84, 0x82, 0x81, 0x80, 0x7c, 0x7a, 0x79, 0x76, 0x75, 0x74, 0x73,
0x72, 0x71, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x67, 0x66, 0x65, 0x63, 0x5c, 0x5a, 0x59, 0x56,
0x55, 0x54, 0x53, 0x52, 0x51, 0x4e, 0x4d, 0x4c, 0x4b, 0x4a, 0x49, 0x47, 0x46, 0x45, 0x43, 0x3c,
0x3a, 0x39, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31, 0x2e, 0x2d, 0x2c, 0x2b, 0x2a, 0x29, 0x27, 0x26,
0x25, 0x23, 0x1f, 0x1e, 0x1d, 0x1b, 0x18, 0x17, 0x10, 0x0f, 0x08, 0x04, 0x02, 0x01, 0x00
};

static int fcal_encode_addr(Scsi_Cmnd *SCpnt, u16 *addr, fc_channel *fc, fcp_cmnd *fcmd);

int fcal_slave_configure(Scsi_Device *device)
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

/* Detect all FC Arbitrated Loops attached to the machine.
   fc4 module has done all the work for us... */
int __init fcal_detect(Scsi_Host_Template *tpnt)
{
	int nfcals = 0;
	fc_channel *fc;
	int fcalcount;
	int i;

	tpnt->proc_name = "fcal";
	fcalcount = 0;
	for_each_online_fc_channel(fc)
		if (fc->posmap)
			fcalcount++;
	FCALND(("%d channels online\n", fcalcount))
	if (!fcalcount) {
#if defined(MODULE) && defined(CONFIG_FC4_SOCAL_MODULE) && defined(CONFIG_KMOD)
		request_module("socal");
		
		for_each_online_fc_channel(fc)
			if (fc->posmap)
				fcalcount++;
		if (!fcalcount)
#endif
			return 0;
	}
	for_each_online_fc_channel(fc) {
		struct Scsi_Host *host;
		long *ages;
		struct fcal *fcal;
		
		if (!fc->posmap) continue;
		
		/* Strange, this is already registered to some other SCSI host, then it cannot be fcal */
		if (fc->scsi_name[0]) continue;
		memcpy (fc->scsi_name, "FCAL", 4);
		
		fc->can_queue = FCAL_CAN_QUEUE;
		fc->rsp_size = 64;
		fc->encode_addr = fcal_encode_addr;
		
		ages = kmalloc (128 * sizeof(long), GFP_KERNEL);
		if (!ages) continue;
				
		host = scsi_register (tpnt, sizeof (struct fcal));
		if (!host) 
		{
			kfree(ages);
			continue;
		}
				
		if (!try_module_get(fc->module)) {
			kfree(ages);
			scsi_unregister(host);
			continue;
		}
	
		nfcals++;
				
		fcal = (struct fcal *)host->hostdata;
		
		fc->fcp_register(fc, TYPE_SCSI_FCP, 0);

		for (i = 0; i < fc->posmap->len; i++) {
			int status, target, alpa;

			alpa = fc->posmap->list[i];			
			FCALD(("Sending PLOGI to %02x\n", alpa))
			target = alpa2target[alpa];
			status = fc_do_plogi(fc, alpa, fcal->node_wwn + target, 
					     fcal->nport_wwn + target);
			FCALD(("PLOGI returned with status %d\n", status))
			if (status != FC_STATUS_OK)
				continue;
			FCALD(("Sending PRLI to %02x\n", alpa))
			status = fc_do_prli(fc, alpa);
			FCALD(("PRLI returned with status %d\n", status))
			if (status == FC_STATUS_OK)
				fcal->map[target] = 1;
		}
		
		host->max_id = 127;
		host->irq = fc->irq;
#ifdef __sparc_v9__
		host->unchecked_isa_dma = 1;
#endif

		fc->channels = 1;
		fc->targets = 127;
		fc->ages = ages;
		memset (ages, 0, 128 * sizeof(long));
				
		fcal->fc = fc;
		
		FCALD(("Found FCAL\n"))
	}
	if (nfcals)
#ifdef __sparc__
		printk ("FCAL: Total of %d Sun Enterprise Network Array (A5000 or EX500) channels found\n", nfcals);
#else
		printk ("FCAL: Total of %d Fibre Channel Arbitrated Loops found\n", nfcals);
#endif
	return nfcals;
}

int fcal_release(struct Scsi_Host *host)
{
	struct fcal *fcal = (struct fcal *)host->hostdata;
	fc_channel *fc = fcal->fc;

	module_put(fc->module);
	
	fc->fcp_register(fc, TYPE_SCSI_FCP, 1);
	FCALND((" releasing fcal.\n"));
	kfree (fc->ages);
	FCALND(("released fcal!\n"));
	return 0;
}

#undef SPRINTF
#define SPRINTF(args...) { if (pos < (buffer + length)) pos += sprintf (pos, ## args); }

int fcal_proc_info (struct Scsi_Host *host, char *buffer, char **start, off_t offset, int length, int inout)
{
	struct fcal *fcal;
	fc_channel *fc;
	char *pos = buffer;
	int i, j;

	if (inout) return length;
    
	fcal = (struct fcal *)host->hostdata;
	fc = fcal->fc;

#ifdef __sparc__
	SPRINTF ("Sun Enterprise Network Array (A5000 or E?500) on %s PROM node %x\n", fc->name, fc->dev->prom_node);
#else
	SPRINTF ("Fibre Channel Arbitrated Loop on %s\n", fc->name);
#endif
	SPRINTF ("Initiator AL-PA: %02x\n", fc->sid);

	SPRINTF ("\nAttached devices:\n");
	
	for (i = 0; i < fc->posmap->len; i++) {
		unsigned char alpa = fc->posmap->list[i];
		unsigned char target;
		u32 *u1, *u2;
		
		target = alpa2target[alpa];
		u1 = (u32 *)&fcal->nport_wwn[target];
		u2 = (u32 *)&fcal->node_wwn[target];
		if (!u1[0] && !u1[1]) {
			SPRINTF ("  [AL-PA: %02x] Not responded to PLOGI\n", alpa);
		} else if (!fcal->map[target]) {
			SPRINTF ("  [AL-PA: %02x, Port WWN: %08x%08x, Node WWN: %08x%08x] Not responded to PRLI\n",
				 alpa, u1[0], u1[1], u2[0], u2[1]);
		} else {
			Scsi_Device *scd;
			shost_for_each_device(scd, host)
				if (scd->id == target) {
					SPRINTF ("  [AL-PA: %02x, Id: %02d, Port WWN: %08x%08x, Node WWN: %08x%08x]  ",
						alpa, target, u1[0], u1[1], u2[0], u2[1]);
					SPRINTF ("%s ", (scd->type < MAX_SCSI_DEVICE_CODE) ?
						scsi_device_types[(short) scd->type] : "Unknown device");

					for (j = 0; (j < 8) && (scd->vendor[j] >= 0x20); j++)
						SPRINTF ("%c", scd->vendor[j]);
					SPRINTF (" ");

					for (j = 0; (j < 16) && (scd->model[j] >= 0x20); j++)
						SPRINTF ("%c", scd->model[j]);
		
					SPRINTF ("\n");
				}
		}
	}
	SPRINTF ("\n");

	*start = buffer + offset;

	if ((pos - buffer) < offset)
		return 0;
	else if (pos - buffer - offset < length)
		return pos - buffer - offset;
	else
		return length;
}

/* 
   For FC-AL, we use a simple addressing: we have just one channel 0,
   and all AL-PAs are mapped to targets 0..0x7e
 */
static int fcal_encode_addr(Scsi_Cmnd *SCpnt, u16 *addr, fc_channel *fc, fcp_cmnd *fcmd)
{
	struct fcal *f;
	
	/* We don't support LUNs yet - I'm not sure if LUN should be in SCSI fcp_cdb, or in second byte of addr[0] */
	if (SCpnt->cmnd[1] & 0xe0) return -EINVAL;
	/* FC-PLDA tells us... */
	memset(addr, 0, 8);
	f = (struct fcal *)SCpnt->device->host->hostdata;
	if (!f->map[SCpnt->device->id])
		return -EINVAL;
	/* Now, determine DID: It will be Native Identifier, so we zero upper
	   2 bytes of the 3 byte DID, lowest byte will be AL-PA */
	fcmd->did = target2alpa[SCpnt->device->id];
	FCALD(("trying DID %06x\n", fcmd->did))
	return 0;
}

static Scsi_Host_Template driver_template = {
	.name			= "Fibre Channel Arbitrated Loop",
	.detect			= fcal_detect,
	.release		= fcal_release,	
	.proc_info		= fcal_proc_info,
	.queuecommand		= fcp_scsi_queuecommand,
	.slave_configure	= fcal_slave_configure,
	.can_queue		= FCAL_CAN_QUEUE,
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

