/*
 * Setup routines for AGP 3.5 compliant bridges.
 */

#include <linux/list.h>
#include <linux/pci.h>
#include <linux/agp_backend.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "agp.h"

/* Generic AGP 3.5 enabling routines */

struct agp_3_5_dev {
	struct list_head list;
	u8 capndx;
	u32 maxbw;
	struct pci_dev *dev;
};

static void agp_3_5_dev_list_insert(struct list_head *head, struct list_head *new)
{
	struct agp_3_5_dev *cur, *n = list_entry(new, struct agp_3_5_dev, list);
	struct list_head *pos;

	list_for_each(pos, head) {
		cur = list_entry(pos, struct agp_3_5_dev, list);
		if(cur->maxbw > n->maxbw)
			break;
	}
	list_add_tail(new, pos);
}

static void agp_3_5_dev_list_sort(struct agp_3_5_dev *list, unsigned int ndevs)
{
	struct agp_3_5_dev *cur;
	struct pci_dev *dev;
	struct list_head *pos, *tmp, *head = &list->list, *start = head->next;
	u32 nistat;

	INIT_LIST_HEAD(head);

	for (pos=start; pos!=head; ) {
		cur = list_entry(pos, struct agp_3_5_dev, list);
		dev = cur->dev;

		pci_read_config_dword(dev, cur->capndx+AGPNISTAT, &nistat);
		cur->maxbw = (nistat >> 16) & 0xff;

		tmp = pos;
		pos = pos->next;
		agp_3_5_dev_list_insert(head, tmp);
	}
}

/* 
 * Initialize all isochronous transfer parameters for an AGP 3.0 
 * node (i.e. a host bridge in combination with the adapters 
 * lying behind it...)
 */

static int agp_3_5_isochronous_node_enable(struct agp_bridge_data *bridge,
		struct agp_3_5_dev *dev_list, unsigned int ndevs)
{
	/*
	 * Convenience structure to make the calculations clearer
	 * here.  The field names come straight from the AGP 3.0 spec.
	 */
	struct isoch_data {
		u32 maxbw;
		u32 n;
		u32 y;
		u32 l;
		u32 rq;
		struct agp_3_5_dev *dev;
	};

	struct pci_dev *td = bridge->dev, *dev;
	struct list_head *head = &dev_list->list, *pos;
	struct agp_3_5_dev *cur;
	struct isoch_data *master, target;
	unsigned int cdev = 0;
	u32 mnistat, tnistat, tstatus, mcmd;
	u16 tnicmd, mnicmd;
	u8 mcapndx;
	u32 tot_bw = 0, tot_n = 0, tot_rq = 0, y_max, rq_isoch, rq_async;
	u32 step, rem, rem_isoch, rem_async;
	int ret = 0;

	/*
	 * We'll work with an array of isoch_data's (one for each
	 * device in dev_list) throughout this function.
	 */
	if ((master = kmalloc(ndevs * sizeof(*master), GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto get_out;
	}

	/*
	 * Sort the device list by maxbw.  We need to do this because the
	 * spec suggests that the devices with the smallest requirements
	 * have their resources allocated first, with all remaining resources
	 * falling to the device with the largest requirement.
	 *
	 * We don't exactly do this, we divide target resources by ndevs
	 * and split them amongst the AGP 3.0 devices.  The remainder of such
	 * division operations are dropped on the last device, sort of like
	 * the spec mentions it should be done.
	 *
	 * We can't do this sort when we initially construct the dev_list
	 * because we don't know until this function whether isochronous
	 * transfers are enabled and consequently whether maxbw will mean
	 * anything.
	 */
	agp_3_5_dev_list_sort(dev_list, ndevs);

	pci_read_config_dword(td, bridge->capndx+AGPNISTAT, &tnistat);
	pci_read_config_dword(td, bridge->capndx+AGPSTAT, &tstatus);

	/* Extract power-on defaults from the target */
	target.maxbw = (tnistat >> 16) & 0xff;
	target.n     = (tnistat >> 8)  & 0xff;
	target.y     = (tnistat >> 6)  & 0x3;
	target.l     = (tnistat >> 3)  & 0x7;
	target.rq    = (tstatus >> 24) & 0xff;

	y_max = target.y;

	/*
	 * Extract power-on defaults for each device in dev_list.  Along
	 * the way, calculate the total isochronous bandwidth required
	 * by these devices and the largest requested payload size.
	 */
	list_for_each(pos, head) {
		cur = list_entry(pos, struct agp_3_5_dev, list);
		dev = cur->dev;

		mcapndx = cur->capndx;

		pci_read_config_dword(dev, cur->capndx+AGPNISTAT, &mnistat);

		master[cdev].maxbw = (mnistat >> 16) & 0xff;
		master[cdev].n     = (mnistat >> 8)  & 0xff;
		master[cdev].y     = (mnistat >> 6)  & 0x3;
		master[cdev].dev   = cur;

		tot_bw += master[cdev].maxbw;
		y_max = max(y_max, master[cdev].y);

		cdev++;
	}

	/* Check if this configuration has any chance of working */
	if (tot_bw > target.maxbw) {
		printk(KERN_ERR PFX "isochronous bandwidth required "
			"by AGP 3.0 devices exceeds that which is supported by "
			"the AGP 3.0 bridge!\n");
		ret = -ENODEV;
		goto free_and_exit;
	}

	target.y = y_max;

	/*
	 * Write the calculated payload size into the target's NICMD
	 * register.  Doing this directly effects the ISOCH_N value
	 * in the target's NISTAT register, so we need to do this now
	 * to get an accurate value for ISOCH_N later.
	 */
	pci_read_config_word(td, bridge->capndx+AGPNICMD, &tnicmd);
	tnicmd &= ~(0x3 << 6);
	tnicmd |= target.y << 6;
	pci_write_config_word(td, bridge->capndx+AGPNICMD, tnicmd);

	/* Reread the target's ISOCH_N */
	pci_read_config_dword(td, bridge->capndx+AGPNISTAT, &tnistat);
	target.n = (tnistat >> 8) & 0xff;

	/* Calculate the minimum ISOCH_N needed by each master */
	for (cdev=0; cdev<ndevs; cdev++) {
		master[cdev].y = target.y;
		master[cdev].n = master[cdev].maxbw / (master[cdev].y + 1);

		tot_n += master[cdev].n;
	}

	/* Exit if the minimal ISOCH_N allocation among the masters is more
	 * than the target can handle. */
	if (tot_n > target.n) {
		printk(KERN_ERR PFX "number of isochronous "
			"transactions per period required by AGP 3.0 devices "
			"exceeds that which is supported by the AGP 3.0 "
			"bridge!\n");
		ret = -ENODEV;
		goto free_and_exit;
	}

	/* Calculate left over ISOCH_N capability in the target.  We'll give
	 * this to the hungriest device (as per the spec) */
	rem  = target.n - tot_n;

	/* 
	 * Calculate the minimum isochronous RQ depth needed by each master.
	 * Along the way, distribute the extra ISOCH_N capability calculated
	 * above.
	 */
	for (cdev=0; cdev<ndevs; cdev++) {
		/*
		 * This is a little subtle.  If ISOCH_Y > 64B, then ISOCH_Y
		 * byte isochronous writes will be broken into 64B pieces.
		 * This means we need to budget more RQ depth to account for
		 * these kind of writes (each isochronous write is actually
		 * many writes on the AGP bus).
		 */
		master[cdev].rq = master[cdev].n;
		if(master[cdev].y > 0x1)
			master[cdev].rq *= (1 << (master[cdev].y - 1));

		tot_rq += master[cdev].rq;

		if (cdev == ndevs-1)
			master[cdev].n += rem;
	}

	/* Figure the number of isochronous and asynchronous RQ slots the
	 * target is providing. */
	rq_isoch = (target.y > 0x1) ? target.n * (1 << (target.y - 1)) : target.n;
	rq_async = target.rq - rq_isoch;

	/* Exit if the minimal RQ needs of the masters exceeds what the target
	 * can provide. */
	if (tot_rq > rq_isoch) {
		printk(KERN_ERR PFX "number of request queue slots "
			"required by the isochronous bandwidth requested by "
			"AGP 3.0 devices exceeds the number provided by the "
			"AGP 3.0 bridge!\n");
		ret = -ENODEV;
		goto free_and_exit;
	}

	/* Calculate asynchronous RQ capability in the target (per master) as
	 * well as the total number of leftover isochronous RQ slots. */
	step      = rq_async / ndevs;
	rem_async = step + (rq_async % ndevs);
	rem_isoch = rq_isoch - tot_rq;

	/* Distribute the extra RQ slots calculated above and write our
	 * isochronous settings out to the actual devices. */
	for (cdev=0; cdev<ndevs; cdev++) {
		cur = master[cdev].dev;
		dev = cur->dev;

		mcapndx = cur->capndx;

		master[cdev].rq += (cdev == ndevs - 1)
		              ? (rem_async + rem_isoch) : step;

		pci_read_config_word(dev, cur->capndx+AGPNICMD, &mnicmd);
		pci_read_config_dword(dev, cur->capndx+AGPCMD, &mcmd);

		mnicmd &= ~(0xff << 8);
		mnicmd &= ~(0x3  << 6);
		mcmd   &= ~(0xff << 24);

		mnicmd |= master[cdev].n  << 8;
		mnicmd |= master[cdev].y  << 6;
		mcmd   |= master[cdev].rq << 24;

		pci_write_config_dword(dev, cur->capndx+AGPCMD, mcmd);
		pci_write_config_word(dev, cur->capndx+AGPNICMD, mnicmd);
	}

free_and_exit:
	kfree(master);

get_out:
	return ret;
}

/*
 * This function basically allocates request queue slots among the
 * AGP 3.0 systems in nonisochronous nodes.  The algorithm is
 * pretty stupid, divide the total number of RQ slots provided by the
 * target by ndevs.  Distribute this many slots to each AGP 3.0 device,
 * giving any left over slots to the last device in dev_list.
 */
static void agp_3_5_nonisochronous_node_enable(struct agp_bridge_data *bridge,
		struct agp_3_5_dev *dev_list, unsigned int ndevs)
{
	struct agp_3_5_dev *cur;
	struct list_head *head = &dev_list->list, *pos;
	u32 tstatus, mcmd;
	u32 trq, mrq, rem;
	unsigned int cdev = 0;

	pci_read_config_dword(bridge->dev, bridge->capndx+AGPSTAT, &tstatus);

	trq = (tstatus >> 24) & 0xff;
	mrq = trq / ndevs;

	rem = mrq + (trq % ndevs);

	for (pos=head->next; cdev<ndevs; cdev++, pos=pos->next) {
		cur = list_entry(pos, struct agp_3_5_dev, list);

		pci_read_config_dword(cur->dev, cur->capndx+AGPCMD, &mcmd);
		mcmd &= ~(0xff << 24);
		mcmd |= ((cdev == ndevs - 1) ? rem : mrq) << 24;
		pci_write_config_dword(cur->dev, cur->capndx+AGPCMD, mcmd);
	}
}

/*
 * Fully configure and enable an AGP 3.0 host bridge and all the devices
 * lying behind it.
 */
int agp_3_5_enable(struct agp_bridge_data *bridge)
{
	struct pci_dev *td = bridge->dev, *dev = NULL;
	u8 mcapndx;
	u32 isoch, arqsz;
	u32 tstatus, mstatus, ncapid;
	u32 mmajor;
	u16 mpstat;
	struct agp_3_5_dev *dev_list, *cur;
	struct list_head *head, *pos;
	unsigned int ndevs = 0;
	int ret = 0;

	/* Extract some power-on defaults from the target */
	pci_read_config_dword(td, bridge->capndx+AGPSTAT, &tstatus);
	isoch     = (tstatus >> 17) & 0x1;
	if (isoch == 0)	/* isoch xfers not available, bail out. */
		return -ENODEV;

	arqsz     = (tstatus >> 13) & 0x7;

	/* 
	 * Allocate a head for our AGP 3.5 device list
	 * (multiple AGP v3 devices are allowed behind a single bridge). 
	 */
	if ((dev_list = kmalloc(sizeof(*dev_list), GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto get_out;
	}
	head = &dev_list->list;
	INIT_LIST_HEAD(head);

	/* Find all AGP devices, and add them to dev_list. */
	for_each_pci_dev(dev) {
		mcapndx = pci_find_capability(dev, PCI_CAP_ID_AGP);
		if (mcapndx == 0)
			continue;

		switch ((dev->class >>8) & 0xff00) {
			case 0x0600:    /* Bridge */
				/* Skip bridges. We should call this function for each one. */
				continue;

			case 0x0001:    /* Unclassified device */
				/* Don't know what this is, but log it for investigation. */
				if (mcapndx != 0) {
					printk (KERN_INFO PFX "Wacky, found unclassified AGP device. %x:%x\n",
						dev->vendor, dev->device);
				}
				continue;

			case 0x0300:    /* Display controller */
			case 0x0400:    /* Multimedia controller */
				if((cur = kmalloc(sizeof(*cur), GFP_KERNEL)) == NULL) {
					ret = -ENOMEM;
					goto free_and_exit;
				}
				cur->dev = dev;

				pos = &cur->list;
				list_add(pos, head);
				ndevs++;
				continue;

			default:
				continue;
		}
	}

	/*
	 * Take an initial pass through the devices lying behind our host
	 * bridge.  Make sure each one is actually an AGP 3.0 device, otherwise
	 * exit with an error message.  Along the way store the AGP 3.0
	 * cap_ptr for each device
	 */
	list_for_each(pos, head) {
		cur = list_entry(pos, struct agp_3_5_dev, list);
		dev = cur->dev;
		
		pci_read_config_word(dev, PCI_STATUS, &mpstat);
		if ((mpstat & PCI_STATUS_CAP_LIST) == 0)
			continue;

		pci_read_config_byte(dev, PCI_CAPABILITY_LIST, &mcapndx);
		if (mcapndx != 0) {
			do {
				pci_read_config_dword(dev, mcapndx, &ncapid);
				if ((ncapid & 0xff) != 2)
					mcapndx = (ncapid >> 8) & 0xff;
			}
			while (((ncapid & 0xff) != 2) && (mcapndx != 0));
		}

		if (mcapndx == 0) {
			printk(KERN_ERR PFX "woah!  Non-AGP device "
				"found on the secondary bus of an AGP 3.5 bridge!\n");
			ret = -ENODEV;
			goto free_and_exit;
		}

		mmajor = (ncapid >> AGP_MAJOR_VERSION_SHIFT) & 0xf;
		if (mmajor < 3) {
			printk(KERN_ERR PFX "woah!  AGP 2.0 device "
				"found on the secondary bus of an AGP 3.5 "
				"bridge operating with AGP 3.0 electricals!\n");
			ret = -ENODEV;
			goto free_and_exit;
		}

		cur->capndx = mcapndx;

		pci_read_config_dword(dev, cur->capndx+AGPSTAT, &mstatus);

		if (((mstatus >> 3) & 0x1) == 0) {
			printk(KERN_ERR PFX "woah!  AGP 3.x device "
				"not operating in AGP 3.x mode found on the "
				"secondary bus of an AGP 3.5 bridge operating "
				"with AGP 3.0 electricals!\n");
			ret = -ENODEV;
			goto free_and_exit;
		}
	}		

	/*
	 * Call functions to divide target resources amongst the AGP 3.0
	 * masters.  This process is dramatically different depending on
	 * whether isochronous transfers are supported.
	 */
	if (isoch) {
		ret = agp_3_5_isochronous_node_enable(bridge, dev_list, ndevs);
		if (ret) {
			printk(KERN_INFO PFX "Something bad happened setting "
			       "up isochronous xfers.  Falling back to "
			       "non-isochronous xfer mode.\n");
		} else {
			goto free_and_exit;
		}
	}
	agp_3_5_nonisochronous_node_enable(bridge, dev_list, ndevs);

free_and_exit:
	/* Be sure to free the dev_list */
	for (pos=head->next; pos!=head; ) {
		cur = list_entry(pos, struct agp_3_5_dev, list);

		pos = pos->next;
		kfree(cur);
	}
	kfree(dev_list);

get_out:
	return ret;
}

