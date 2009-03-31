/* -*- mode: c; c-basic-offset: 8 -*- */

/*
 * MCA bus support functions for sysfs.
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

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mca.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>

/* Very few machines have more than one MCA bus.  However, there are
 * those that do (Voyager 35xx/5xxx), so we do it this way for future
 * expansion.  None that I know have more than 2 */
static struct mca_bus *mca_root_busses[MAX_MCA_BUSSES];

#define MCA_DEVINFO(i,s) { .pos = i, .name = s }

struct mca_device_info {
	short pos_id;		/* the 2 byte pos id for this card */
	char name[50];
};

static int mca_bus_match (struct device *dev, struct device_driver *drv)
{
	struct mca_device *mca_dev = to_mca_device (dev);
	struct mca_driver *mca_drv = to_mca_driver (drv);
	const unsigned short *mca_ids = mca_drv->id_table;
	int i = 0;

	if (mca_ids) {
		for(i = 0; mca_ids[i]; i++) {
			if (mca_ids[i] == mca_dev->pos_id) {
				mca_dev->index = i;
				return 1;
			}
		}
	}
	/* If the integrated id is present, treat it as though it were an
	 * additional id in the id_table (it can't be because by definition,
	 * integrated id's overflow a short */
	if (mca_drv->integrated_id && mca_dev->pos_id ==
	    mca_drv->integrated_id) {
		mca_dev->index = i;
		return 1;
	}
	return 0;
}

struct bus_type mca_bus_type = {
	.name  = "MCA",
	.match = mca_bus_match,
};
EXPORT_SYMBOL (mca_bus_type);

static ssize_t mca_show_pos_id(struct device *dev, struct device_attribute *attr, char *buf)
{
	/* four digits, \n and trailing \0 */
	struct mca_device *mca_dev = to_mca_device(dev);
	int len;

	if(mca_dev->pos_id < MCA_DUMMY_POS_START)
		len = sprintf(buf, "%04x\n", mca_dev->pos_id);
	else
		len = sprintf(buf, "none\n");
	return len;
}
static ssize_t mca_show_pos(struct device *dev, struct device_attribute *attr, char *buf)
{
	/* enough for 8 two byte hex chars plus space and new line */
	int j, len=0;
	struct mca_device *mca_dev = to_mca_device(dev);

	for(j=0; j<8; j++)
		len += sprintf(buf+len, "%02x ", mca_dev->pos[j]);
	/* change last trailing space to new line */
	buf[len-1] = '\n';
	return len;
}

static DEVICE_ATTR(id, S_IRUGO, mca_show_pos_id, NULL);
static DEVICE_ATTR(pos, S_IRUGO, mca_show_pos, NULL);

int __init mca_register_device(int bus, struct mca_device *mca_dev)
{
	struct mca_bus *mca_bus = mca_root_busses[bus];
	int rc;

	mca_dev->dev.parent = &mca_bus->dev;
	mca_dev->dev.bus = &mca_bus_type;
	dev_set_name(&mca_dev->dev, "%02d:%02X", bus, mca_dev->slot);
	mca_dev->dma_mask = mca_bus->default_dma_mask;
	mca_dev->dev.dma_mask = &mca_dev->dma_mask;
	mca_dev->dev.coherent_dma_mask = mca_dev->dma_mask;

	rc = device_register(&mca_dev->dev);
	if (rc)
		goto err_out;

	rc = device_create_file(&mca_dev->dev, &dev_attr_id);
	if (rc) goto err_out_devreg;
	rc = device_create_file(&mca_dev->dev, &dev_attr_pos);
	if (rc) goto err_out_id;

	return 1;

err_out_id:
	device_remove_file(&mca_dev->dev, &dev_attr_id);
err_out_devreg:
	device_unregister(&mca_dev->dev);
err_out:
	return 0;
}

/* */
struct mca_bus * __devinit mca_attach_bus(int bus)
{
	struct mca_bus *mca_bus;

	if (unlikely(mca_root_busses[bus] != NULL)) {
		/* This should never happen, but just in case */
		printk(KERN_EMERG "MCA tried to add already existing bus %d\n",
		       bus);
		dump_stack();
		return NULL;
	}

	mca_bus = kzalloc(sizeof(struct mca_bus), GFP_KERNEL);
	if (!mca_bus)
		return NULL;

	dev_set_name(&mca_bus->dev, "mca%d", bus);
	sprintf(mca_bus->name,"Host %s MCA Bridge", bus ? "Secondary" : "Primary");
	if (device_register(&mca_bus->dev)) {
		kfree(mca_bus);
		return NULL;
	}

	mca_root_busses[bus] = mca_bus;

	return mca_bus;
}

int __init mca_system_init (void)
{
	return bus_register(&mca_bus_type);
}
