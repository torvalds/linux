/*
 * Generic SCSI-3 ALUA SCSI Device Handler
 *
 * Copyright (C) 2007-2010 Hannes Reinecke, SUSE Linux Products GmbH.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <asm/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dh.h>

#define ALUA_DH_NAME "alua"
#define ALUA_DH_VER "1.3"

#define TPGS_STATE_OPTIMIZED		0x0
#define TPGS_STATE_NONOPTIMIZED		0x1
#define TPGS_STATE_STANDBY		0x2
#define TPGS_STATE_UNAVAILABLE		0x3
#define TPGS_STATE_LBA_DEPENDENT	0x4
#define TPGS_STATE_OFFLINE		0xe
#define TPGS_STATE_TRANSITIONING	0xf

#define TPGS_SUPPORT_NONE		0x00
#define TPGS_SUPPORT_OPTIMIZED		0x01
#define TPGS_SUPPORT_NONOPTIMIZED	0x02
#define TPGS_SUPPORT_STANDBY		0x04
#define TPGS_SUPPORT_UNAVAILABLE	0x08
#define TPGS_SUPPORT_LBA_DEPENDENT	0x10
#define TPGS_SUPPORT_OFFLINE		0x40
#define TPGS_SUPPORT_TRANSITION		0x80

#define RTPG_FMT_MASK			0x70
#define RTPG_FMT_EXT_HDR		0x10

#define TPGS_MODE_UNINITIALIZED		 -1
#define TPGS_MODE_NONE			0x0
#define TPGS_MODE_IMPLICIT		0x1
#define TPGS_MODE_EXPLICIT		0x2

#define ALUA_RTPG_SIZE			128
#define ALUA_FAILOVER_TIMEOUT		60
#define ALUA_FAILOVER_RETRIES		5

/* device handler flags */
#define ALUA_OPTIMIZE_STPG		1
#define ALUA_RTPG_EXT_HDR_UNSUPP	2

static LIST_HEAD(port_group_list);
static DEFINE_SPINLOCK(port_group_lock);

struct alua_port_group {
	struct kref		kref;
	struct list_head	node;
	unsigned char		device_id_str[256];
	int			device_id_len;
	int			group_id;
	int			tpgs;
	int			state;
	int			pref;
	unsigned		flags; /* used for optimizing STPG */
	unsigned char		transition_tmo;
};

struct alua_dh_data {
	struct alua_port_group	*pg;
	int			group_id;
	int			rel_port;
	struct scsi_device	*sdev;
	activate_complete	callback_fn;
	void			*callback_data;
};

#define ALUA_POLICY_SWITCH_CURRENT	0
#define ALUA_POLICY_SWITCH_ALL		1

static int alua_rtpg(struct scsi_device *, struct alua_port_group *, int);
static char print_alua_state(int);

static void release_port_group(struct kref *kref)
{
	struct alua_port_group *pg;

	pg = container_of(kref, struct alua_port_group, kref);
	spin_lock(&port_group_lock);
	list_del(&pg->node);
	spin_unlock(&port_group_lock);
	kfree(pg);
}

/*
 * submit_rtpg - Issue a REPORT TARGET GROUP STATES command
 * @sdev: sdev the command should be sent to
 */
static int submit_rtpg(struct scsi_device *sdev, unsigned char *buff,
		       int bufflen, struct scsi_sense_hdr *sshdr, int flags)
{
	u8 cdb[COMMAND_SIZE(MAINTENANCE_IN)];
	int req_flags = REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT |
		REQ_FAILFAST_DRIVER;

	/* Prepare the command. */
	memset(cdb, 0x0, COMMAND_SIZE(MAINTENANCE_IN));
	cdb[0] = MAINTENANCE_IN;
	if (!(flags & ALUA_RTPG_EXT_HDR_UNSUPP))
		cdb[1] = MI_REPORT_TARGET_PGS | MI_EXT_HDR_PARAM_FMT;
	else
		cdb[1] = MI_REPORT_TARGET_PGS;
	put_unaligned_be32(bufflen, &cdb[6]);

	return scsi_execute_req_flags(sdev, cdb, DMA_FROM_DEVICE,
				      buff, bufflen, sshdr,
				      ALUA_FAILOVER_TIMEOUT * HZ,
				      ALUA_FAILOVER_RETRIES, NULL, req_flags);
}

/*
 * submit_stpg - Issue a SET TARGET PORT GROUP command
 *
 * Currently we're only setting the current target port group state
 * to 'active/optimized' and let the array firmware figure out
 * the states of the remaining groups.
 */
static int submit_stpg(struct scsi_device *sdev, int group_id,
		       struct scsi_sense_hdr *sshdr)
{
	u8 cdb[COMMAND_SIZE(MAINTENANCE_OUT)];
	unsigned char stpg_data[8];
	int stpg_len = 8;
	int req_flags = REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT |
		REQ_FAILFAST_DRIVER;

	/* Prepare the data buffer */
	memset(stpg_data, 0, stpg_len);
	stpg_data[4] = TPGS_STATE_OPTIMIZED & 0x0f;
	put_unaligned_be16(group_id, &stpg_data[6]);

	/* Prepare the command. */
	memset(cdb, 0x0, COMMAND_SIZE(MAINTENANCE_OUT));
	cdb[0] = MAINTENANCE_OUT;
	cdb[1] = MO_SET_TARGET_PGS;
	put_unaligned_be32(stpg_len, &cdb[6]);

	return scsi_execute_req_flags(sdev, cdb, DMA_TO_DEVICE,
				      stpg_data, stpg_len,
				      sshdr, ALUA_FAILOVER_TIMEOUT * HZ,
				      ALUA_FAILOVER_RETRIES, NULL, req_flags);
}

struct alua_port_group *alua_find_get_pg(char *id_str, size_t id_size,
					 int group_id)
{
	struct alua_port_group *pg;

	list_for_each_entry(pg, &port_group_list, node) {
		if (pg->group_id != group_id)
			continue;
		if (pg->device_id_len != id_size)
			continue;
		if (strncmp(pg->device_id_str, id_str, id_size))
			continue;
		if (!kref_get_unless_zero(&pg->kref))
			continue;
		return pg;
	}

	return NULL;
}

/*
 * alua_alloc_pg - Allocate a new port_group structure
 * @sdev: scsi device
 * @h: alua device_handler data
 * @group_id: port group id
 *
 * Allocate a new port_group structure for a given
 * device.
 */
struct alua_port_group *alua_alloc_pg(struct scsi_device *sdev,
				      int group_id, int tpgs)
{
	struct alua_port_group *pg, *tmp_pg;

	pg = kzalloc(sizeof(struct alua_port_group), GFP_KERNEL);
	if (!pg)
		return ERR_PTR(-ENOMEM);

	pg->device_id_len = scsi_vpd_lun_id(sdev, pg->device_id_str,
					    sizeof(pg->device_id_str));
	if (pg->device_id_len <= 0) {
		/*
		 * Internal error: TPGS supported but no device
		 * identifcation found. Disable ALUA support.
		 */
		kfree(pg);
		sdev_printk(KERN_INFO, sdev,
			    "%s: No device descriptors found\n",
			    ALUA_DH_NAME);
		return ERR_PTR(-ENXIO);
	}
	pg->group_id = group_id;
	pg->tpgs = tpgs;
	pg->state = TPGS_STATE_OPTIMIZED;
	kref_init(&pg->kref);

	spin_lock(&port_group_lock);
	tmp_pg = alua_find_get_pg(pg->device_id_str, pg->device_id_len,
				  group_id);
	if (tmp_pg) {
		spin_unlock(&port_group_lock);
		kfree(pg);
		return tmp_pg;
	}

	list_add(&pg->node, &port_group_list);
	spin_unlock(&port_group_lock);

	return pg;
}

/*
 * alua_check_tpgs - Evaluate TPGS setting
 * @sdev: device to be checked
 *
 * Examine the TPGS setting of the sdev to find out if ALUA
 * is supported.
 */
static int alua_check_tpgs(struct scsi_device *sdev)
{
	int tpgs = TPGS_MODE_NONE;

	/*
	 * ALUA support for non-disk devices is fraught with
	 * difficulties, so disable it for now.
	 */
	if (sdev->type != TYPE_DISK) {
		sdev_printk(KERN_INFO, sdev,
			    "%s: disable for non-disk devices\n",
			    ALUA_DH_NAME);
		return tpgs;
	}

	tpgs = scsi_device_tpgs(sdev);
	switch (tpgs) {
	case TPGS_MODE_EXPLICIT|TPGS_MODE_IMPLICIT:
		sdev_printk(KERN_INFO, sdev,
			    "%s: supports implicit and explicit TPGS\n",
			    ALUA_DH_NAME);
		break;
	case TPGS_MODE_EXPLICIT:
		sdev_printk(KERN_INFO, sdev, "%s: supports explicit TPGS\n",
			    ALUA_DH_NAME);
		break;
	case TPGS_MODE_IMPLICIT:
		sdev_printk(KERN_INFO, sdev, "%s: supports implicit TPGS\n",
			    ALUA_DH_NAME);
		break;
	case TPGS_MODE_NONE:
		sdev_printk(KERN_INFO, sdev, "%s: not supported\n",
			    ALUA_DH_NAME);
		break;
	default:
		sdev_printk(KERN_INFO, sdev,
			    "%s: unsupported TPGS setting %d\n",
			    ALUA_DH_NAME, tpgs);
		tpgs = TPGS_MODE_NONE;
		break;
	}

	return tpgs;
}

/*
 * alua_check_vpd - Evaluate INQUIRY vpd page 0x83
 * @sdev: device to be checked
 *
 * Extract the relative target port and the target port group
 * descriptor from the list of identificators.
 */
static int alua_check_vpd(struct scsi_device *sdev, struct alua_dh_data *h,
			  int tpgs)
{
	int rel_port = -1, group_id;

	group_id = scsi_vpd_tpg_id(sdev, &rel_port);
	if (group_id < 0) {
		/*
		 * Internal error; TPGS supported but required
		 * VPD identification descriptors not present.
		 * Disable ALUA support
		 */
		sdev_printk(KERN_INFO, sdev,
			    "%s: No target port descriptors found\n",
			    ALUA_DH_NAME);
		return SCSI_DH_DEV_UNSUPP;
	}

	h->pg = alua_alloc_pg(sdev, group_id, tpgs);
	if (IS_ERR(h->pg)) {
		if (PTR_ERR(h->pg) == -ENOMEM)
			return SCSI_DH_NOMEM;
		return SCSI_DH_DEV_UNSUPP;
	}
	h->rel_port = rel_port;

	sdev_printk(KERN_INFO, sdev,
		    "%s: device %s port group %x rel port %x\n",
		    ALUA_DH_NAME, h->pg->device_id_str,
		    h->group_id, h->rel_port);

	return alua_rtpg(sdev, h->pg, 0);
}

static char print_alua_state(int state)
{
	switch (state) {
	case TPGS_STATE_OPTIMIZED:
		return 'A';
	case TPGS_STATE_NONOPTIMIZED:
		return 'N';
	case TPGS_STATE_STANDBY:
		return 'S';
	case TPGS_STATE_UNAVAILABLE:
		return 'U';
	case TPGS_STATE_LBA_DEPENDENT:
		return 'L';
	case TPGS_STATE_OFFLINE:
		return 'O';
	case TPGS_STATE_TRANSITIONING:
		return 'T';
	default:
		return 'X';
	}
}

static int alua_check_sense(struct scsi_device *sdev,
			    struct scsi_sense_hdr *sense_hdr)
{
	switch (sense_hdr->sense_key) {
	case NOT_READY:
		if (sense_hdr->asc == 0x04 && sense_hdr->ascq == 0x0a)
			/*
			 * LUN Not Accessible - ALUA state transition
			 */
			return ADD_TO_MLQUEUE;
		break;
	case UNIT_ATTENTION:
		if (sense_hdr->asc == 0x29 && sense_hdr->ascq == 0x00)
			/*
			 * Power On, Reset, or Bus Device Reset, just retry.
			 */
			return ADD_TO_MLQUEUE;
		if (sense_hdr->asc == 0x29 && sense_hdr->ascq == 0x04)
			/*
			 * Device internal reset
			 */
			return ADD_TO_MLQUEUE;
		if (sense_hdr->asc == 0x2a && sense_hdr->ascq == 0x01)
			/*
			 * Mode Parameters Changed
			 */
			return ADD_TO_MLQUEUE;
		if (sense_hdr->asc == 0x2a && sense_hdr->ascq == 0x06)
			/*
			 * ALUA state changed
			 */
			return ADD_TO_MLQUEUE;
		if (sense_hdr->asc == 0x2a && sense_hdr->ascq == 0x07)
			/*
			 * Implicit ALUA state transition failed
			 */
			return ADD_TO_MLQUEUE;
		if (sense_hdr->asc == 0x3f && sense_hdr->ascq == 0x03)
			/*
			 * Inquiry data has changed
			 */
			return ADD_TO_MLQUEUE;
		if (sense_hdr->asc == 0x3f && sense_hdr->ascq == 0x0e)
			/*
			 * REPORTED_LUNS_DATA_HAS_CHANGED is reported
			 * when switching controllers on targets like
			 * Intel Multi-Flex. We can just retry.
			 */
			return ADD_TO_MLQUEUE;
		break;
	}

	return SCSI_RETURN_NOT_HANDLED;
}

/*
 * alua_rtpg - Evaluate REPORT TARGET GROUP STATES
 * @sdev: the device to be evaluated.
 * @wait_for_transition: if nonzero, wait ALUA_FAILOVER_TIMEOUT seconds for device to exit transitioning state
 *
 * Evaluate the Target Port Group State.
 * Returns SCSI_DH_DEV_OFFLINED if the path is
 * found to be unusable.
 */
static int alua_rtpg(struct scsi_device *sdev, struct alua_port_group *pg, int wait_for_transition)
{
	struct scsi_sense_hdr sense_hdr;
	int len, k, off, valid_states = 0, bufflen = ALUA_RTPG_SIZE;
	unsigned char *ucp, *buff;
	unsigned err, retval;
	unsigned long expiry, interval = 0;
	unsigned int tpg_desc_tbl_off;
	unsigned char orig_transition_tmo;

	if (!pg->transition_tmo)
		expiry = round_jiffies_up(jiffies + ALUA_FAILOVER_TIMEOUT * HZ);
	else
		expiry = round_jiffies_up(jiffies + pg->transition_tmo * HZ);

	buff = kzalloc(bufflen, GFP_KERNEL);
	if (!buff)
		return SCSI_DH_DEV_TEMP_BUSY;

 retry:
	retval = submit_rtpg(sdev, buff, bufflen, &sense_hdr, pg->flags);

	if (retval) {
		if (!scsi_sense_valid(&sense_hdr)) {
			sdev_printk(KERN_INFO, sdev,
				    "%s: rtpg failed, result %d\n",
				    ALUA_DH_NAME, retval);
			kfree(buff);
			if (driver_byte(retval) == DRIVER_ERROR)
				return SCSI_DH_DEV_TEMP_BUSY;
			return SCSI_DH_IO;
		}

		/*
		 * submit_rtpg() has failed on existing arrays
		 * when requesting extended header info, and
		 * the array doesn't support extended headers,
		 * even though it shouldn't according to T10.
		 * The retry without rtpg_ext_hdr_req set
		 * handles this.
		 */
		if (!(pg->flags & ALUA_RTPG_EXT_HDR_UNSUPP) &&
		    sense_hdr.sense_key == ILLEGAL_REQUEST &&
		    sense_hdr.asc == 0x24 && sense_hdr.ascq == 0) {
			pg->flags |= ALUA_RTPG_EXT_HDR_UNSUPP;
			goto retry;
		}
		/*
		 * Retry on ALUA state transition or if any
		 * UNIT ATTENTION occurred.
		 */
		if (sense_hdr.sense_key == NOT_READY &&
		    sense_hdr.asc == 0x04 && sense_hdr.ascq == 0x0a)
			err = SCSI_DH_RETRY;
		else if (sense_hdr.sense_key == UNIT_ATTENTION)
			err = SCSI_DH_RETRY;
		if (err == SCSI_DH_RETRY && time_before(jiffies, expiry)) {
			sdev_printk(KERN_ERR, sdev, "%s: rtpg retry\n",
				    ALUA_DH_NAME);
			scsi_print_sense_hdr(sdev, ALUA_DH_NAME, &sense_hdr);
			goto retry;
		}
		sdev_printk(KERN_ERR, sdev, "%s: rtpg failed\n",
			    ALUA_DH_NAME);
		scsi_print_sense_hdr(sdev, ALUA_DH_NAME, &sense_hdr);
		kfree(buff);
		return SCSI_DH_IO;
	}

	len = get_unaligned_be32(&buff[0]) + 4;

	if (len > bufflen) {
		/* Resubmit with the correct length */
		kfree(buff);
		bufflen = len;
		buff = kmalloc(bufflen, GFP_KERNEL);
		if (!buff) {
			sdev_printk(KERN_WARNING, sdev,
				    "%s: kmalloc buffer failed\n",__func__);
			/* Temporary failure, bypass */
			return SCSI_DH_DEV_TEMP_BUSY;
		}
		goto retry;
	}

	orig_transition_tmo = pg->transition_tmo;
	if ((buff[4] & RTPG_FMT_MASK) == RTPG_FMT_EXT_HDR && buff[5] != 0)
		pg->transition_tmo = buff[5];
	else
		pg->transition_tmo = ALUA_FAILOVER_TIMEOUT;

	if (wait_for_transition &&
	    (orig_transition_tmo != pg->transition_tmo)) {
		sdev_printk(KERN_INFO, sdev,
			    "%s: transition timeout set to %d seconds\n",
			    ALUA_DH_NAME, pg->transition_tmo);
		expiry = jiffies + pg->transition_tmo * HZ;
	}

	if ((buff[4] & RTPG_FMT_MASK) == RTPG_FMT_EXT_HDR)
		tpg_desc_tbl_off = 8;
	else
		tpg_desc_tbl_off = 4;

	for (k = tpg_desc_tbl_off, ucp = buff + tpg_desc_tbl_off;
	     k < len;
	     k += off, ucp += off) {

		if (pg->group_id == get_unaligned_be16(&ucp[2])) {
			pg->state = ucp[0] & 0x0f;
			pg->pref = ucp[0] >> 7;
			valid_states = ucp[1];
		}
		off = 8 + (ucp[7] * 4);
	}

	sdev_printk(KERN_INFO, sdev,
		    "%s: port group %02x state %c %s supports %c%c%c%c%c%c%c\n",
		    ALUA_DH_NAME, pg->group_id, print_alua_state(pg->state),
		    pg->pref ? "preferred" : "non-preferred",
		    valid_states&TPGS_SUPPORT_TRANSITION?'T':'t',
		    valid_states&TPGS_SUPPORT_OFFLINE?'O':'o',
		    valid_states&TPGS_SUPPORT_LBA_DEPENDENT?'L':'l',
		    valid_states&TPGS_SUPPORT_UNAVAILABLE?'U':'u',
		    valid_states&TPGS_SUPPORT_STANDBY?'S':'s',
		    valid_states&TPGS_SUPPORT_NONOPTIMIZED?'N':'n',
		    valid_states&TPGS_SUPPORT_OPTIMIZED?'A':'a');

	switch (pg->state) {
	case TPGS_STATE_TRANSITIONING:
		if (wait_for_transition) {
			if (time_before(jiffies, expiry)) {
				/* State transition, retry */
				interval += 2000;
				msleep(interval);
				goto retry;
			}
			err = SCSI_DH_RETRY;
		} else {
			err = SCSI_DH_OK;
		}

		/* Transitioning time exceeded, set port to standby */
		pg->state = TPGS_STATE_STANDBY;
		break;
	case TPGS_STATE_OFFLINE:
		/* Path unusable */
		err = SCSI_DH_DEV_OFFLINED;
		break;
	default:
		/* Useable path if active */
		err = SCSI_DH_OK;
		break;
	}
	kfree(buff);
	return err;
}

/*
 * alua_stpg - Issue a SET TARGET PORT GROUP command
 *
 * Issue a SET TARGET PORT GROUP command and evaluate the
 * response. Returns SCSI_DH_RETRY per default to trigger
 * a re-evaluation of the target group state or SCSI_DH_OK
 * if no further action needs to be taken.
 */
static unsigned alua_stpg(struct scsi_device *sdev, struct alua_port_group *pg)
{
	int retval;
	struct scsi_sense_hdr sense_hdr;

	if (!(pg->tpgs & TPGS_MODE_EXPLICIT)) {
		/* Only implicit ALUA supported, retry */
		return SCSI_DH_RETRY;
	}
	switch (pg->state) {
	case TPGS_STATE_OPTIMIZED:
		return SCSI_DH_OK;
	case TPGS_STATE_NONOPTIMIZED:
		if ((pg->flags & ALUA_OPTIMIZE_STPG) &&
		    !pg->pref &&
		    (pg->tpgs & TPGS_MODE_IMPLICIT))
			return SCSI_DH_OK;
		break;
	case TPGS_STATE_STANDBY:
	case TPGS_STATE_UNAVAILABLE:
		break;
	case TPGS_STATE_OFFLINE:
		return SCSI_DH_IO;
	case TPGS_STATE_TRANSITIONING:
		break;
	default:
		sdev_printk(KERN_INFO, sdev,
			    "%s: stpg failed, unhandled TPGS state %d",
			    ALUA_DH_NAME, pg->state);
		return SCSI_DH_NOSYS;
	}
	retval = submit_stpg(sdev, pg->group_id, &sense_hdr);

	if (retval) {
		if (!scsi_sense_valid(&sense_hdr)) {
			sdev_printk(KERN_INFO, sdev,
				    "%s: stpg failed, result %d",
				    ALUA_DH_NAME, retval);
			if (driver_byte(retval) == DRIVER_ERROR)
				return SCSI_DH_DEV_TEMP_BUSY;
		} else {
			sdev_printk(KERN_INFO, sdev, "%s: stpg failed\n",
				    ALUA_DH_NAME);
			scsi_print_sense_hdr(sdev, ALUA_DH_NAME, &sense_hdr);
		}
	}
	/* Retry RTPG */
	return SCSI_DH_RETRY;
}

/*
 * alua_initialize - Initialize ALUA state
 * @sdev: the device to be initialized
 *
 * For the prep_fn to work correctly we have
 * to initialize the ALUA state for the device.
 */
static int alua_initialize(struct scsi_device *sdev, struct alua_dh_data *h)
{
	int err = SCSI_DH_DEV_UNSUPP, tpgs;

	tpgs = alua_check_tpgs(sdev);
	if (tpgs != TPGS_MODE_NONE)
		err = alua_check_vpd(sdev, h, tpgs);

	return err;
}
/*
 * alua_set_params - set/unset the optimize flag
 * @sdev: device on the path to be activated
 * params - parameters in the following format
 *      "no_of_params\0param1\0param2\0param3\0...\0"
 * For example, to set the flag pass the following parameters
 * from multipath.conf
 *     hardware_handler        "2 alua 1"
 */
static int alua_set_params(struct scsi_device *sdev, const char *params)
{
	struct alua_dh_data *h = sdev->handler_data;
	struct alua_port_group *pg = NULL;
	unsigned int optimize = 0, argc;
	const char *p = params;
	int result = SCSI_DH_OK;

	if ((sscanf(params, "%u", &argc) != 1) || (argc != 1))
		return -EINVAL;

	while (*p++)
		;
	if ((sscanf(p, "%u", &optimize) != 1) || (optimize > 1))
		return -EINVAL;

	pg = h->pg;
	if (!pg)
		return -ENXIO;

	if (optimize)
		pg->flags |= ALUA_OPTIMIZE_STPG;
	else
		pg->flags &= ~ALUA_OPTIMIZE_STPG;

	return result;
}

static uint optimize_stpg;
module_param(optimize_stpg, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(optimize_stpg, "Allow use of a non-optimized path, rather than sending a STPG, when implicit TPGS is supported (0=No,1=Yes). Default is 0.");

/*
 * alua_activate - activate a path
 * @sdev: device on the path to be activated
 *
 * We're currently switching the port group to be activated only and
 * let the array figure out the rest.
 * There may be other arrays which require us to switch all port groups
 * based on a certain policy. But until we actually encounter them it
 * should be okay.
 */
static int alua_activate(struct scsi_device *sdev,
			activate_complete fn, void *data)
{
	struct alua_dh_data *h = sdev->handler_data;
	int err = SCSI_DH_OK;

	if (!h->pg)
		goto out;

	kref_get(&h->pg->kref);

	if (optimize_stpg)
		h->pg->flags |= ALUA_OPTIMIZE_STPG;

	err = alua_rtpg(sdev, h->pg, 1);
	if (err != SCSI_DH_OK) {
		kref_put(&h->pg->kref, release_port_group);
		goto out;
	}
	err = alua_stpg(sdev, h->pg);
	if (err == SCSI_DH_RETRY)
		err = alua_rtpg(sdev, h->pg, 1);
	kref_put(&h->pg->kref, release_port_group);
out:
	if (fn)
		fn(data, err);
	return 0;
}

/*
 * alua_prep_fn - request callback
 *
 * Fail I/O to all paths not in state
 * active/optimized or active/non-optimized.
 */
static int alua_prep_fn(struct scsi_device *sdev, struct request *req)
{
	struct alua_dh_data *h = sdev->handler_data;
	int state;
	int ret = BLKPREP_OK;

	if (!h->pg)
		return ret;
	kref_get(&h->pg->kref);
	state = h->pg->state;
	kref_put(&h->pg->kref, release_port_group);
	if (state == TPGS_STATE_TRANSITIONING)
		ret = BLKPREP_DEFER;
	else if (state != TPGS_STATE_OPTIMIZED &&
		 state != TPGS_STATE_NONOPTIMIZED &&
		 state != TPGS_STATE_LBA_DEPENDENT) {
		ret = BLKPREP_KILL;
		req->cmd_flags |= REQ_QUIET;
	}
	return ret;

}

/*
 * alua_bus_attach - Attach device handler
 * @sdev: device to be attached to
 */
static int alua_bus_attach(struct scsi_device *sdev)
{
	struct alua_dh_data *h;
	int err, ret = -EINVAL;

	h = kzalloc(sizeof(*h) , GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	h->pg = NULL;
	h->rel_port = -1;
	h->sdev = sdev;

	err = alua_initialize(sdev, h);
	if (err == SCSI_DH_NOMEM)
		ret = -ENOMEM;
	if (err != SCSI_DH_OK && err != SCSI_DH_DEV_OFFLINED)
		goto failed;

	sdev->handler_data = h;
	return 0;
failed:
	kfree(h);
	return ret;
}

/*
 * alua_bus_detach - Detach device handler
 * @sdev: device to be detached from
 */
static void alua_bus_detach(struct scsi_device *sdev)
{
	struct alua_dh_data *h = sdev->handler_data;

	if (h->pg) {
		kref_put(&h->pg->kref, release_port_group);
		h->pg = NULL;
	}
	sdev->handler_data = NULL;
	kfree(h);
}

static struct scsi_device_handler alua_dh = {
	.name = ALUA_DH_NAME,
	.module = THIS_MODULE,
	.attach = alua_bus_attach,
	.detach = alua_bus_detach,
	.prep_fn = alua_prep_fn,
	.check_sense = alua_check_sense,
	.activate = alua_activate,
	.set_params = alua_set_params,
};

static int __init alua_init(void)
{
	int r;

	r = scsi_register_device_handler(&alua_dh);
	if (r != 0)
		printk(KERN_ERR "%s: Failed to register scsi device handler",
			ALUA_DH_NAME);
	return r;
}

static void __exit alua_exit(void)
{
	scsi_unregister_device_handler(&alua_dh);
}

module_init(alua_init);
module_exit(alua_exit);

MODULE_DESCRIPTION("DM Multipath ALUA support");
MODULE_AUTHOR("Hannes Reinecke <hare@suse.de>");
MODULE_LICENSE("GPL");
MODULE_VERSION(ALUA_DH_VER);
