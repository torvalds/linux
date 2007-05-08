/* $Id: capimain.c,v 1.24 2003/09/09 06:51:05 schindler Exp $
 *
 * ISDN interface module for Eicon active cards DIVA.
 * CAPI Interface
 * 
 * Copyright 2000-2003 by Armin Schindler (mac@melware.de) 
 * Copyright 2000-2003 Cytronics & Melware (info@melware.de)
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/skbuff.h>

#include "os_capi.h"

#include "platform.h"
#include "di_defs.h"
#include "capi20.h"
#include "divacapi.h"
#include "cp_vers.h"
#include "capifunc.h"

static char *main_revision = "$Revision: 1.24 $";
static char *DRIVERNAME =
    "Eicon DIVA - CAPI Interface driver (http://www.melware.net)";
static char *DRIVERLNAME = "divacapi";

MODULE_DESCRIPTION("CAPI driver for Eicon DIVA cards");
MODULE_AUTHOR("Cytronics & Melware, Eicon Networks");
MODULE_SUPPORTED_DEVICE("CAPI and DIVA card drivers");
MODULE_LICENSE("GPL");

/*
 * get revision number from revision string
 */
static char *getrev(const char *revision)
{
	char *rev;
	char *p;
	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "1.0";
	return rev;

}

/*
 * alloc a message buffer
 */
diva_os_message_buffer_s *diva_os_alloc_message_buffer(unsigned long size,
						       void **data_buf)
{
	diva_os_message_buffer_s *dmb = alloc_skb(size, GFP_ATOMIC);
	if (dmb) {
		*data_buf = skb_put(dmb, size);
	}
	return (dmb);
}

/*
 * free a message buffer
 */
void diva_os_free_message_buffer(diva_os_message_buffer_s * dmb)
{
	kfree_skb(dmb);
}

/*
 * proc function for controller info
 */
static int diva_ctl_read_proc(char *page, char **start, off_t off,
			      int count, int *eof, struct capi_ctr *ctrl)
{
	diva_card *card = (diva_card *) ctrl->driverdata;
	int len = 0;

	len += sprintf(page + len, "%s\n", ctrl->name);
	len += sprintf(page + len, "Serial No. : %s\n", ctrl->serial);
	len += sprintf(page + len, "Id         : %d\n", card->Id);
	len += sprintf(page + len, "Channels   : %d\n", card->d.channels);

	if (off + count >= len)
		*eof = 1;
	if (len < off)
		return 0;
	*start = page + off;
	return ((count < len - off) ? count : len - off);
}

/*
 * set additional os settings in capi_ctr struct
 */
void diva_os_set_controller_struct(struct capi_ctr *ctrl)
{
	ctrl->driver_name = DRIVERLNAME;
	ctrl->load_firmware = NULL;
	ctrl->reset_ctr = NULL;
	ctrl->ctr_read_proc = diva_ctl_read_proc;
	ctrl->owner = THIS_MODULE;
}

/*
 * module init
 */
static int DIVA_INIT_FUNCTION divacapi_init(void)
{
	char tmprev[32];
	int ret = 0;

	sprintf(DRIVERRELEASE_CAPI, "%d.%d%s", DRRELMAJOR, DRRELMINOR,
		DRRELEXTRA);

	printk(KERN_INFO "%s\n", DRIVERNAME);
	printk(KERN_INFO "%s: Rel:%s  Rev:", DRIVERLNAME, DRIVERRELEASE_CAPI);
	strcpy(tmprev, main_revision);
	printk("%s  Build: %s(%s)\n", getrev(tmprev),
	       diva_capi_common_code_build, DIVA_BUILD);

	if (!(init_capifunc())) {
		printk(KERN_ERR "%s: failed init capi_driver.\n",
		       DRIVERLNAME);
		ret = -EIO;
	}

	return ret;
}

/*
 * module exit
 */
static void DIVA_EXIT_FUNCTION divacapi_exit(void)
{
	finit_capifunc();
	printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(divacapi_init);
module_exit(divacapi_exit);
