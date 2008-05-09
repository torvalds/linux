/* -*- mode: c; c-basic-offset: 8 -*- */

/*
 * MCA bus support functions for the proc fs.
 *
 * NOTE: this code *requires* the legacy MCA api.
 *
 * Legacy API means the API that operates in terms of MCA slot number
 *
 * (C) 2002 James Bottomley <James.Bottomley@HansenPartnership.com>
 *
**-----------------------------------------------------------------------------
**  
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/mca.h>

static int get_mca_info_helper(struct mca_device *mca_dev, char *page, int len)
{
	int j;

	for(j=0; j<8; j++)
		len += sprintf(page+len, "%02x ",
			       mca_dev ? mca_dev->pos[j] : 0xff);
	len += sprintf(page+len, " %s\n", mca_dev ? mca_dev->name : "");
	return len;
}

static int get_mca_info(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int i, len = 0;

	if(MCA_bus) {
		struct mca_device *mca_dev;
		/* Format POS registers of eight MCA slots */

		for(i=0; i<MCA_MAX_SLOT_NR; i++) {
			mca_dev = mca_find_device_by_slot(i);

			len += sprintf(page+len, "Slot %d: ", i+1);
			len = get_mca_info_helper(mca_dev, page, len);
		}

		/* Format POS registers of integrated video subsystem */

		mca_dev = mca_find_device_by_slot(MCA_INTEGVIDEO);
		len += sprintf(page+len, "Video : ");
		len = get_mca_info_helper(mca_dev, page, len);

		/* Format POS registers of integrated SCSI subsystem */

		mca_dev = mca_find_device_by_slot(MCA_INTEGSCSI);
		len += sprintf(page+len, "SCSI  : ");
		len = get_mca_info_helper(mca_dev, page, len);

		/* Format POS registers of motherboard */

		mca_dev = mca_find_device_by_slot(MCA_MOTHERBOARD);
		len += sprintf(page+len, "Planar: ");
		len = get_mca_info_helper(mca_dev, page, len);
	} else {
		/* Leave it empty if MCA not detected - this should *never*
		 * happen!
		 */
	}

	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}

/*--------------------------------------------------------------------*/

static int mca_default_procfn(char* buf, struct mca_device *mca_dev)
{
	int len = 0, i;
	int slot = mca_dev->slot;

	/* Print out the basic information */

	if(slot < MCA_MAX_SLOT_NR) {
		len += sprintf(buf+len, "Slot: %d\n", slot+1);
	} else if(slot == MCA_INTEGSCSI) {
		len += sprintf(buf+len, "Integrated SCSI Adapter\n");
	} else if(slot == MCA_INTEGVIDEO) {
		len += sprintf(buf+len, "Integrated Video Adapter\n");
	} else if(slot == MCA_MOTHERBOARD) {
		len += sprintf(buf+len, "Motherboard\n");
	}
	if (mca_dev->name[0]) {

		/* Drivers might register a name without /proc handler... */

		len += sprintf(buf+len, "Adapter Name: %s\n",
			       mca_dev->name);
	} else {
		len += sprintf(buf+len, "Adapter Name: Unknown\n");
	}
	len += sprintf(buf+len, "Id: %02x%02x\n",
		mca_dev->pos[1], mca_dev->pos[0]);
	len += sprintf(buf+len, "Enabled: %s\nPOS: ",
		mca_device_status(mca_dev) == MCA_ADAPTER_NORMAL ?
			"Yes" : "No");
	for(i=0; i<8; i++) {
		len += sprintf(buf+len, "%02x ", mca_dev->pos[i]);
	}
	len += sprintf(buf+len, "\nDriver Installed: %s",
		mca_device_claimed(mca_dev) ? "Yes" : "No");
	buf[len++] = '\n';
	buf[len] = 0;

	return len;
} /* mca_default_procfn() */

static int get_mca_machine_info(char* page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	int len = 0;

	len += sprintf(page+len, "Model Id: 0x%x\n", machine_id);
	len += sprintf(page+len, "Submodel Id: 0x%x\n", machine_submodel_id);
	len += sprintf(page+len, "BIOS Revision: 0x%x\n", BIOS_revision);

	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}

static int mca_read_proc(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	struct mca_device *mca_dev = (struct mca_device *)data;
	int len = 0;

	/* Get the standard info */

	len = mca_default_procfn(page, mca_dev);

	/* Do any device-specific processing, if there is any */

	if(mca_dev->procfn) {
		len += mca_dev->procfn(page+len, mca_dev->slot,
				       mca_dev->proc_dev);
	}
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
} /* mca_read_proc() */

/*--------------------------------------------------------------------*/

void __init mca_do_proc_init(void)
{
	int i;
	struct proc_dir_entry *proc_mca;
	struct proc_dir_entry* node = NULL;
	struct mca_device *mca_dev;

	proc_mca = proc_mkdir("mca", NULL);
	create_proc_read_entry("pos",0,proc_mca,get_mca_info,NULL);
	create_proc_read_entry("machine",0,proc_mca,get_mca_machine_info,NULL);

	/* Initialize /proc/mca entries for existing adapters */

	for(i = 0; i < MCA_NUMADAPTERS; i++) {
		enum MCA_AdapterStatus status;
		mca_dev = mca_find_device_by_slot(i);
		if(!mca_dev)
			continue;

		mca_dev->procfn = NULL;

		if(i < MCA_MAX_SLOT_NR) sprintf(mca_dev->procname,"slot%d", i+1);
		else if(i == MCA_INTEGVIDEO) sprintf(mca_dev->procname,"video");
		else if(i == MCA_INTEGSCSI) sprintf(mca_dev->procname,"scsi");
		else if(i == MCA_MOTHERBOARD) sprintf(mca_dev->procname,"planar");

		status = mca_device_status(mca_dev);
		if (status != MCA_ADAPTER_NORMAL &&
		    status != MCA_ADAPTER_DISABLED)
			continue;

		node = create_proc_read_entry(mca_dev->procname, 0, proc_mca,
					      mca_read_proc, (void *)mca_dev);

		if(node == NULL) {
			printk("Failed to allocate memory for MCA proc-entries!");
			return;
		}
	}

} /* mca_do_proc_init() */

/**
 *	mca_set_adapter_procfn - Set the /proc callback
 *	@slot: slot to configure
 *	@procfn: callback function to call for /proc
 *	@dev: device information passed to the callback
 *
 *	This sets up an information callback for /proc/mca/slot?.  The
 *	function is called with the buffer, slot, and device pointer (or
 *	some equally informative context information, or nothing, if you
 *	prefer), and is expected to put useful information into the
 *	buffer.  The adapter name, ID, and POS registers get printed
 *	before this is called though, so don't do it again.
 *
 *	This should be called with a %NULL @procfn when a module
 *	unregisters, thus preventing kernel crashes and other such
 *	nastiness.
 */

void mca_set_adapter_procfn(int slot, MCA_ProcFn procfn, void* proc_dev)
{
	struct mca_device *mca_dev = mca_find_device_by_slot(slot);

	if(!mca_dev)
		return;

	mca_dev->procfn = procfn;
	mca_dev->proc_dev = proc_dev;
}
EXPORT_SYMBOL(mca_set_adapter_procfn);
