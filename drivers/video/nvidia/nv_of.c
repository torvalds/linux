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

#include <linux/config.h>
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

void nvidia_create_i2c_busses(struct nvidia_par *par) {}
void nvidia_delete_i2c_busses(struct nvidia_par *par) {}

int nvidia_probe_i2c_connector(struct fb_info *info, int conn, u8 **out_edid)
{
	struct nvidia_par *par = info->par;
	struct device_node *dp;
	unsigned char *pedid = NULL;
	unsigned char *disptype = NULL;
	static char *propnames[] = {
		"DFP,EDID", "LCD,EDID", "EDID", "EDID1", "EDID,B", "EDID,A", NULL };
	int i;

	dp = pci_device_to_OF_node(par->pci_dev);
	for (; dp != NULL; dp = dp->child) {
		disptype = (unsigned char *)get_property(dp, "display-type", NULL);
		if (disptype == NULL)
			continue;
		if (strncmp(disptype, "LCD", 3) != 0)
			continue;
		for (i = 0; propnames[i] != NULL; ++i) {
			pedid = (unsigned char *)
				get_property(dp, propnames[i], NULL);
			if (pedid != NULL) {
				*out_edid = pedid;
				return 0;
			}
		}
	}
	return 1;
}
