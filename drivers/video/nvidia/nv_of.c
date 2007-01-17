/*
 * linux/drivers/video/nvidia/nv_of.c
 *
 * Copyright 2004 Antonino A. Daplas <adaplas @pol.net>
 *
 * Based on rivafb-i2c.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/fb.h>

#include <asm/io.h>

#include <asm/prom.h>
#include <asm/pci-bridge.h>

#include "nv_type.h"
#include "nv_local.h"
#include "nv_proto.h"

#include "../edid.h"

int nvidia_probe_of_connector(struct fb_info *info, int conn, u8 **out_edid)
{
	struct nvidia_par *par = info->par;
	struct device_node *parent, *dp;
	const unsigned char *pedid = NULL;
	static char *propnames[] = {
		"DFP,EDID", "LCD,EDID", "EDID", "EDID1",
		"EDID,B", "EDID,A", NULL };
	int i;

	parent = pci_device_to_OF_node(par->pci_dev);
	if (parent == NULL)
		return -1;
	if (par->twoHeads) {
		const char *pname;
		int len;

		for (dp = NULL;
		     (dp = of_get_next_child(parent, dp)) != NULL;) {
			pname = get_property(dp, "name", NULL);
			if (!pname)
				continue;
			len = strlen(pname);
			if ((pname[len-1] == 'A' && conn == 1) ||
			    (pname[len-1] == 'B' && conn == 2)) {
				for (i = 0; propnames[i] != NULL; ++i) {
					pedid = get_property(dp, propnames[i],
							     NULL);
					if (pedid != NULL)
						break;
				}
				of_node_put(dp);
				break;
			}
		}
	}
	if (pedid == NULL) {
		for (i = 0; propnames[i] != NULL; ++i) {
			pedid = get_property(parent, propnames[i], NULL);
			if (pedid != NULL)
				break;
		}
	}
	if (pedid) {
		*out_edid = kmemdup(pedid, EDID_LENGTH, GFP_KERNEL);
		if (*out_edid == NULL)
			return -1;
		printk(KERN_DEBUG "nvidiafb: Found OF EDID for head %d\n", conn);
		return 0;
	}
	return -1;
}
