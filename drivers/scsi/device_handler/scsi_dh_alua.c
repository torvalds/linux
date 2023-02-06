// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic SCSI-3 ALUA SCSI Device Handler
 *
 * Copyright (C) 2007-2010 Hannes Reinecke, SUSE Linux Products GmbH.
 * All rights reserved.
 */
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <asm/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_proto.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dh.h>

#define ALUA_DH_NAME "alua"
#define ALUA_DH_VER "2.0"

#define TPGS_SUPPORT_NONE		0x00
#define TPGS_SUPPORT_OPTIMIZED		0x01
#define TPGS_SUPPORT_NONOPTIMIZED	0x02
#define TPGS_SUPPORT_STANDBY		0x04
#define TPGS_SUPPORT_UNAVAILABLE	0x08
#define TPGS_SUPPORT_LBA_DEPENDENT	0x10
#define TPGS_SUPPORT_OFFLINE		0x40
#define TPGS_SUPPORT_TRANSITION		0x80
#define TPGS_SUPPORT_ALL		0xdf

#define RTPG_FMT_MASK			0x70
#define RTPG_FMT_EXT_HDR		0x10

#define TPGS_MODE_UNINITIALIZED		 -1
#define TPGS_MODE_NONE			0x0
#define TPGS_MODE_IMPLICIT		0x1
#define TPGS_MODE_EXPLICIT		0x2

#define ALUA_RTPG_SIZE			128
#define ALUA_FAILOVER_TIMEOUT		60
#define ALUA_FAILOVER_RETRIES		5
#define ALUA_RTPG_DELAY_MSECS		5
#define ALUA_RTPG_RETRY_DELAY		2

/* device handler flags */
#define ALUA_OPTIMIZE_STPG		0x01
#define ALUA_RTPG_EXT_HDR_UNSUPP	0x02
/* State machine flags */
#define ALUA_PG_RUN_RTPG		0x10
#define ALUA_PG_RUN_STPG		0x20
#define ALUA_PG_RUNNING			0x40

static uint optimize_stpg;
module_param(optimize_stpg, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(optimize_stpg, "Allow use of a non-optimized path, rather than sending a STPG, when implicit TPGS is supported (0=No,1=Yes). Default is 0.");

static LIST_HEAD(port_group_list);
static DEFINE_SPINLOCK(port_group_lock);
static struct workqueue_struct *kaluad_wq;

struct alua_port_group {
	struct kref		kref;
	struct rcu_head		rcu;
	struct list_head	node;
	struct list_head	dh_list;
	unsigned char		device_id_str[256];
	int			device_id_len;
	int			group_id;
	int			tpgs;
	int			state;
	int			pref;
	int			valid_states;
	unsigned		flags; /* used for optimizing STPG */
	unsigned char		transition_tmo;
	unsigned long		expiry;
	unsigned long		interval;
	struct delayed_work	rtpg_work;
	spinlock_t		lock;
	struct list_head	rtpg_list;
	struct scsi_device	*rtpg_sdev;
};

struct alua_dh_data {
	struct list_head	node;
	struct alua_port_group __rcu *pg;
	int			group_id;
	spinlock_t		pg_lock;
	struct scsi_device	*sdev;
	int			init_error;
	struct mutex		init_mutex;
	bool			disabled;
};

struct alua_queue_data {
	struct list_head	entry;
	activate_complete	callback_fn;
	void			*callback_data;
};

#define ALUA_POLICY_SWITCH_CURRENT	0
#define ALUA_POLICY_SWITCH_ALL		1

static void alua_rtpg_work(struct work_struct *work);
static bool alua_rtpg_queue(struct alua_port_group *pg,
			    struct scsi_device *sdev,
			    struct alua_queue_data *qdata, bool force);
static void alua_check(struct scsi_device *sdev, bool force);

static void release_port_group(struct kref *kref)
{
	struct alua_port_group *pg;

	pg = container_of(kref, struct alua_port_group, kref);
	if (pg->rtpg_sdev)
		flush_delayed_work(&pg->rtpg_work);
	spin_lock(&port_group_lock);
	list_del(&pg->node);
	spin_unlock(&port_group_lock);
	kfree_rcu(pg, rcu);
}

/*
 * submit_rtpg - Issue a REPORT TARGET GROUP STATES command
 * @sdev: sdev the command should be sent to
 */
static int submit_rtpg(struct scsi_device *sdev, unsigned char *buff,
		       int bufflen, struct scsi_sense_hdr *sshdr, int flags)
{
	u8 cdb[MAX_COMMAND_SIZE];
	blk_opf_t req_flags = REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT |
		REQ_FAILFAST_DRIVER;

	/* Prepare the command. */
	memset(cdb, 0x0, MAX_COMMAND_SIZE);
	cdb[0] = MAINTENANCE_IN;
	if (!(flags & ALUA_RTPG_EXT_HDR_UNSUPP))
		cdb[1] = MI_REPORT_TARGET_PGS | MI_EXT_HDR_PARAM_FMT;
	else
		cdb[1] = MI_REPORT_TARGET_PGS;
	put_unaligned_be32(bufflen, &cdb[6]);

	return scsi_execute(sdev, cdb, DMA_FROM_DEVICE, buff, bufflen, NULL,
			sshdr, ALUA_FAILOVER_TIMEOUT * HZ,
			ALUA_FAILOVER_RETRIES, req_flags, 0, NULL);
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
	u8 cdb[MAX_COMMAND_SIZE];
	unsigned char stpg_data[8];
	int stpg_len = 8;
	blk_opf_t req_flags = REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT |
		REQ_FAILFAST_DRIVER;

	/* Prepare the data buffer */
	memset(stpg_data, 0, stpg_len);
	stpg_data[4] = SCSI_ACCESS_STATE_OPTIMAL;
	put_unaligned_be16(group_id, &stpg_data[6]);

	/* Prepare the command. */
	memset(cdb, 0x0, MAX_COMMAND_SIZE);
	cdb[0] = MAINTENANCE_OUT;
	cdb[1] = MO_SET_TARGET_PGS;
	put_unaligned_be32(stpg_len, &cdb[6]);

	return scsi_execute(sdev, cdb, DMA_TO_DEVICE, stpg_data, stpg_len, NULL,
			sshdr, ALUA_FAILOVER_TIMEOUT * HZ,
			ALUA_FAILOVER_RETRIES, req_flags, 0, NULL);
}

static struct alua_port_group *alua_find_get_pg(char *id_str, size_t id_size,
						int group_id)
{
	struct alua_port_group *pg;

	if (!id_str || !id_size || !strlen(id_str))
		return NULL;

	list_for_each_entry(pg, &port_group_list, node) {
		if (pg->group_id != group_id)
			continue;
		if (!pg->device_id_len || pg->device_id_len != id_size)
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
 * @group_id: port group id
 * @tpgs: target port group settings
 *
 * Allocate a new port_group structure for a given
 * device.
 */
static struct alua_port_group *alua_alloc_pg(struct scsi_device *sdev,
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
		 * TPGS supported but no device identification found.
		 * Generate private device identification.
		 */
		sdev_printk(KERN_INFO, sdev,
			    "%s: No device descriptors found\n",
			    ALUA_DH_NAME);
		pg->device_id_str[0] = '\0';
		pg->device_id_len = 0;
	}
	pg->group_id = group_id;
	pg->tpgs = tpgs;
	pg->state = SCSI_ACCESS_STATE_OPTIMAL;
	pg->valid_states = TPGS_SUPPORT_ALL;
	if (optimize_stpg)
		pg->flags |= ALUA_OPTIMIZE_STPG;
	kref_init(&pg->kref);
	INIT_DELAYED_WORK(&pg->rtpg_work, alua_rtpg_work);
	INIT_LIST_HEAD(&pg->rtpg_list);
	INIT_LIST_HEAD(&pg->node);
	INIT_LIST_HEAD(&pg->dh_list);
	spin_lock_init(&pg->lock);

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
	struct alua_port_group *pg, *old_pg = NULL;
	bool pg_updated = false;
	unsigned long flags;

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

	pg = alua_alloc_pg(sdev, group_id, tpgs);
	if (IS_ERR(pg)) {
		if (PTR_ERR(pg) == -ENOMEM)
			return SCSI_DH_NOMEM;
		return SCSI_DH_DEV_UNSUPP;
	}
	if (pg->device_id_len)
		sdev_printk(KERN_INFO, sdev,
			    "%s: device %s port group %x rel port %x\n",
			    ALUA_DH_NAME, pg->device_id_str,
			    group_id, rel_port);
	else
		sdev_printk(KERN_INFO, sdev,
			    "%s: port group %x rel port %x\n",
			    ALUA_DH_NAME, group_id, rel_port);

	kref_get(&pg->kref);

	/* Check for existing port group references */
	spin_lock(&h->pg_lock);
	old_pg = rcu_dereference_protected(h->pg, lockdep_is_held(&h->pg_lock));
	if (old_pg != pg) {
		/* port group has changed. Update to new port group */
		if (h->pg) {
			spin_lock_irqsave(&old_pg->lock, flags);
			list_del_rcu(&h->node);
			spin_unlock_irqrestore(&old_pg->lock, flags);
		}
		rcu_assign_pointer(h->pg, pg);
		pg_updated = true;
	}

	spin_lock_irqsave(&pg->lock, flags);
	if (pg_updated)
		list_add_rcu(&h->node, &pg->dh_list);
	spin_unlock_irqrestore(&pg->lock, flags);

	spin_unlock(&h->pg_lock);

	alua_rtpg_queue(pg, sdev, NULL, true);
	kref_put(&pg->kref, release_port_group);

	if (old_pg)
		kref_put(&old_pg->kref, release_port_group);

	return SCSI_DH_OK;
}

static char print_alua_state(unsigned char state)
{
	switch (state) {
	case SCSI_ACCESS_STATE_OPTIMAL:
		return 'A';
	case SCSI_ACCESS_STATE_ACTIVE:
		return 'N';
	case SCSI_ACCESS_STATE_STANDBY:
		return 'S';
	case SCSI_ACCESS_STATE_UNAVAILABLE:
		return 'U';
	case SCSI_ACCESS_STATE_LBA:
		return 'L';
	case SCSI_ACCESS_STATE_OFFLINE:
		return 'O';
	case SCSI_ACCESS_STATE_TRANSITIONING:
		return 'T';
	default:
		return 'X';
	}
}

static enum scsi_disposition alua_check_sense(struct scsi_device *sdev,
					      struct scsi_sense_hdr *sense_hdr)
{
	struct alua_dh_data *h = sdev->handler_data;
	struct alua_port_group *pg;

	switch (sense_hdr->sense_key) {
	case NOT_READY:
		if (sense_hdr->asc == 0x04 && sense_hdr->ascq == 0x0a) {
			/*
			 * LUN Not Accessible - ALUA state transition
			 */
			rcu_read_lock();
			pg = rcu_dereference(h->pg);
			if (pg)
				pg->state = SCSI_ACCESS_STATE_TRANSITIONING;
			rcu_read_unlock();
			alua_check(sdev, false);
			return NEEDS_RETRY;
		}
		break;
	case UNIT_ATTENTION:
		if (sense_hdr->asc == 0x29 && sense_hdr->ascq == 0x00) {
			/*
			 * Power On, Reset, or Bus Device Reset.
			 * Might have obscured a state transition,
			 * so schedule a recheck.
			 */
			alua_check(sdev, true);
			return ADD_TO_MLQUEUE;
		}
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
		if (sense_hdr->asc == 0x2a && sense_hdr->ascq == 0x06) {
			/*
			 * ALUA state changed
			 */
			alua_check(sdev, true);
			return ADD_TO_MLQUEUE;
		}
		if (sense_hdr->asc == 0x2a && sense_hdr->ascq == 0x07) {
			/*
			 * Implicit ALUA state transition failed
			 */
			alua_check(sdev, true);
			return ADD_TO_MLQUEUE;
		}
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
 * alua_tur - Send a TEST UNIT READY
 * @sdev: device to which the TEST UNIT READY command should be send
 *
 * Send a TEST UNIT READY to @sdev to figure out the device state
 * Returns SCSI_DH_RETRY if the sense code is NOT READY/ALUA TRANSITIONING,
 * SCSI_DH_OK if no error occurred, and SCSI_DH_IO otherwise.
 */
static int alua_tur(struct scsi_device *sdev)
{
	struct scsi_sense_hdr sense_hdr;
	int retval;

	retval = scsi_test_unit_ready(sdev, ALUA_FAILOVER_TIMEOUT * HZ,
				      ALUA_FAILOVER_RETRIES, &sense_hdr);
	if (sense_hdr.sense_key == NOT_READY &&
	    sense_hdr.asc == 0x04 && sense_hdr.ascq == 0x0a)
		return SCSI_DH_RETRY;
	else if (retval)
		return SCSI_DH_IO;
	else
		return SCSI_DH_OK;
}

/*
 * alua_rtpg - Evaluate REPORT TARGET GROUP STATES
 * @sdev: the device to be evaluated.
 *
 * Evaluate the Target Port Group State.
 * Returns SCSI_DH_DEV_OFFLINED if the path is
 * found to be unusable.
 */
static int alua_rtpg(struct scsi_device *sdev, struct alua_port_group *pg)
{
	struct scsi_sense_hdr sense_hdr;
	struct alua_port_group *tmp_pg;
	int len, k, off, bufflen = ALUA_RTPG_SIZE;
	int group_id_old, state_old, pref_old, valid_states_old;
	unsigned char *desc, *buff;
	unsigned err;
	int retval;
	unsigned int tpg_desc_tbl_off;
	unsigned char orig_transition_tmo;
	unsigned long flags;
	bool transitioning_sense = false;

	group_id_old = pg->group_id;
	state_old = pg->state;
	pref_old = pg->pref;
	valid_states_old = pg->valid_states;

	if (!pg->expiry) {
		unsigned long transition_tmo = ALUA_FAILOVER_TIMEOUT * HZ;

		if (pg->transition_tmo)
			transition_tmo = pg->transition_tmo * HZ;

		pg->expiry = round_jiffies_up(jiffies + transition_tmo);
	}

	buff = kzalloc(bufflen, GFP_KERNEL);
	if (!buff)
		return SCSI_DH_DEV_TEMP_BUSY;

 retry:
	err = 0;
	retval = submit_rtpg(sdev, buff, bufflen, &sense_hdr, pg->flags);

	if (retval) {
		/*
		 * Some (broken) implementations have a habit of returning
		 * an error during things like firmware update etc.
		 * But if the target only supports active/optimized there's
		 * not much we can do; it's not that we can switch paths
		 * or anything.
		 * So ignore any errors to avoid spurious failures during
		 * path failover.
		 */
		if ((pg->valid_states & ~TPGS_SUPPORT_OPTIMIZED) == 0) {
			sdev_printk(KERN_INFO, sdev,
				    "%s: ignoring rtpg result %d\n",
				    ALUA_DH_NAME, retval);
			kfree(buff);
			return SCSI_DH_OK;
		}
		if (retval < 0 || !scsi_sense_valid(&sense_hdr)) {
			sdev_printk(KERN_INFO, sdev,
				    "%s: rtpg failed, result %d\n",
				    ALUA_DH_NAME, retval);
			kfree(buff);
			if (retval < 0)
				return SCSI_DH_DEV_TEMP_BUSY;
			if (host_byte(retval) == DID_NO_CONNECT)
				return SCSI_DH_RES_TEMP_UNAVAIL;
			return SCSI_DH_IO;
		}

		/*
		 * submit_rtpg() has failed on existing arrays
		 * when requesting extended header info, and
		 * the array doesn't support extended headers,
		 * even though it shouldn't according to T10.
		 * The retry without rtpg_ext_hdr_req set
		 * handles this.
		 * Note:  some arrays return a sense key of ILLEGAL_REQUEST
		 * with ASC 00h if they don't support the extended header.
		 */
		if (!(pg->flags & ALUA_RTPG_EXT_HDR_UNSUPP) &&
		    sense_hdr.sense_key == ILLEGAL_REQUEST) {
			pg->flags |= ALUA_RTPG_EXT_HDR_UNSUPP;
			goto retry;
		}
		/*
		 * If the array returns with 'ALUA state transition'
		 * sense code here it cannot return RTPG data during
		 * transition. So set the state to 'transitioning' directly.
		 */
		if (sense_hdr.sense_key == NOT_READY &&
		    sense_hdr.asc == 0x04 && sense_hdr.ascq == 0x0a) {
			transitioning_sense = true;
			goto skip_rtpg;
		}
		/*
		 * Retry on any other UNIT ATTENTION occurred.
		 */
		if (sense_hdr.sense_key == UNIT_ATTENTION)
			err = SCSI_DH_RETRY;
		if (err == SCSI_DH_RETRY &&
		    pg->expiry != 0 && time_before(jiffies, pg->expiry)) {
			sdev_printk(KERN_ERR, sdev, "%s: rtpg retry\n",
				    ALUA_DH_NAME);
			scsi_print_sense_hdr(sdev, ALUA_DH_NAME, &sense_hdr);
			kfree(buff);
			return err;
		}
		sdev_printk(KERN_ERR, sdev, "%s: rtpg failed\n",
			    ALUA_DH_NAME);
		scsi_print_sense_hdr(sdev, ALUA_DH_NAME, &sense_hdr);
		kfree(buff);
		pg->expiry = 0;
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
			pg->expiry = 0;
			return SCSI_DH_DEV_TEMP_BUSY;
		}
		goto retry;
	}

	orig_transition_tmo = pg->transition_tmo;
	if ((buff[4] & RTPG_FMT_MASK) == RTPG_FMT_EXT_HDR && buff[5] != 0)
		pg->transition_tmo = buff[5];
	else
		pg->transition_tmo = ALUA_FAILOVER_TIMEOUT;

	if (orig_transition_tmo != pg->transition_tmo) {
		sdev_printk(KERN_INFO, sdev,
			    "%s: transition timeout set to %d seconds\n",
			    ALUA_DH_NAME, pg->transition_tmo);
		pg->expiry = jiffies + pg->transition_tmo * HZ;
	}

	if ((buff[4] & RTPG_FMT_MASK) == RTPG_FMT_EXT_HDR)
		tpg_desc_tbl_off = 8;
	else
		tpg_desc_tbl_off = 4;

	for (k = tpg_desc_tbl_off, desc = buff + tpg_desc_tbl_off;
	     k < len;
	     k += off, desc += off) {
		u16 group_id = get_unaligned_be16(&desc[2]);

		spin_lock_irqsave(&port_group_lock, flags);
		tmp_pg = alua_find_get_pg(pg->device_id_str, pg->device_id_len,
					  group_id);
		spin_unlock_irqrestore(&port_group_lock, flags);
		if (tmp_pg) {
			if (spin_trylock_irqsave(&tmp_pg->lock, flags)) {
				if ((tmp_pg == pg) ||
				    !(tmp_pg->flags & ALUA_PG_RUNNING)) {
					struct alua_dh_data *h;

					tmp_pg->state = desc[0] & 0x0f;
					tmp_pg->pref = desc[0] >> 7;
					rcu_read_lock();
					list_for_each_entry_rcu(h,
						&tmp_pg->dh_list, node) {
						if (!h->sdev)
							continue;
						h->sdev->access_state = desc[0];
					}
					rcu_read_unlock();
				}
				if (tmp_pg == pg)
					tmp_pg->valid_states = desc[1];
				spin_unlock_irqrestore(&tmp_pg->lock, flags);
			}
			kref_put(&tmp_pg->kref, release_port_group);
		}
		off = 8 + (desc[7] * 4);
	}

 skip_rtpg:
	spin_lock_irqsave(&pg->lock, flags);
	if (transitioning_sense)
		pg->state = SCSI_ACCESS_STATE_TRANSITIONING;

	if (group_id_old != pg->group_id || state_old != pg->state ||
		pref_old != pg->pref || valid_states_old != pg->valid_states)
		sdev_printk(KERN_INFO, sdev,
			"%s: port group %02x state %c %s supports %c%c%c%c%c%c%c\n",
			ALUA_DH_NAME, pg->group_id, print_alua_state(pg->state),
			pg->pref ? "preferred" : "non-preferred",
			pg->valid_states&TPGS_SUPPORT_TRANSITION?'T':'t',
			pg->valid_states&TPGS_SUPPORT_OFFLINE?'O':'o',
			pg->valid_states&TPGS_SUPPORT_LBA_DEPENDENT?'L':'l',
			pg->valid_states&TPGS_SUPPORT_UNAVAILABLE?'U':'u',
			pg->valid_states&TPGS_SUPPORT_STANDBY?'S':'s',
			pg->valid_states&TPGS_SUPPORT_NONOPTIMIZED?'N':'n',
			pg->valid_states&TPGS_SUPPORT_OPTIMIZED?'A':'a');

	switch (pg->state) {
	case SCSI_ACCESS_STATE_TRANSITIONING:
		if (time_before(jiffies, pg->expiry)) {
			/* State transition, retry */
			pg->interval = ALUA_RTPG_RETRY_DELAY;
			err = SCSI_DH_RETRY;
		} else {
			struct alua_dh_data *h;

			/* Transitioning time exceeded, set port to standby */
			err = SCSI_DH_IO;
			pg->state = SCSI_ACCESS_STATE_STANDBY;
			pg->expiry = 0;
			rcu_read_lock();
			list_for_each_entry_rcu(h, &pg->dh_list, node) {
				if (!h->sdev)
					continue;
				h->sdev->access_state =
					(pg->state & SCSI_ACCESS_STATE_MASK);
				if (pg->pref)
					h->sdev->access_state |=
						SCSI_ACCESS_STATE_PREFERRED;
			}
			rcu_read_unlock();
		}
		break;
	case SCSI_ACCESS_STATE_OFFLINE:
		/* Path unusable */
		err = SCSI_DH_DEV_OFFLINED;
		pg->expiry = 0;
		break;
	default:
		/* Useable path if active */
		err = SCSI_DH_OK;
		pg->expiry = 0;
		break;
	}
	spin_unlock_irqrestore(&pg->lock, flags);
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
	case SCSI_ACCESS_STATE_OPTIMAL:
		return SCSI_DH_OK;
	case SCSI_ACCESS_STATE_ACTIVE:
		if ((pg->flags & ALUA_OPTIMIZE_STPG) &&
		    !pg->pref &&
		    (pg->tpgs & TPGS_MODE_IMPLICIT))
			return SCSI_DH_OK;
		break;
	case SCSI_ACCESS_STATE_STANDBY:
	case SCSI_ACCESS_STATE_UNAVAILABLE:
		break;
	case SCSI_ACCESS_STATE_OFFLINE:
		return SCSI_DH_IO;
	case SCSI_ACCESS_STATE_TRANSITIONING:
		break;
	default:
		sdev_printk(KERN_INFO, sdev,
			    "%s: stpg failed, unhandled TPGS state %d",
			    ALUA_DH_NAME, pg->state);
		return SCSI_DH_NOSYS;
	}
	retval = submit_stpg(sdev, pg->group_id, &sense_hdr);

	if (retval) {
		if (retval < 0 || !scsi_sense_valid(&sense_hdr)) {
			sdev_printk(KERN_INFO, sdev,
				    "%s: stpg failed, result %d",
				    ALUA_DH_NAME, retval);
			if (retval < 0)
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
 * The caller must call scsi_device_put() on the returned pointer if it is not
 * NULL.
 */
static struct scsi_device * __must_check
alua_rtpg_select_sdev(struct alua_port_group *pg)
{
	struct alua_dh_data *h;
	struct scsi_device *sdev = NULL, *prev_sdev;

	lockdep_assert_held(&pg->lock);
	if (WARN_ON(!pg->rtpg_sdev))
		return NULL;

	/*
	 * RCU protection isn't necessary for dh_list here
	 * as we hold pg->lock, but for access to h->pg.
	 */
	rcu_read_lock();
	list_for_each_entry_rcu(h, &pg->dh_list, node) {
		if (!h->sdev)
			continue;
		if (h->sdev == pg->rtpg_sdev) {
			h->disabled = true;
			continue;
		}
		if (rcu_dereference(h->pg) == pg &&
		    !h->disabled &&
		    !scsi_device_get(h->sdev)) {
			sdev = h->sdev;
			break;
		}
	}
	rcu_read_unlock();

	if (!sdev) {
		pr_warn("%s: no device found for rtpg\n",
			(pg->device_id_len ?
			 (char *)pg->device_id_str : "(nameless PG)"));
		return NULL;
	}

	sdev_printk(KERN_INFO, sdev, "rtpg retry on different device\n");

	prev_sdev = pg->rtpg_sdev;
	pg->rtpg_sdev = sdev;

	return prev_sdev;
}

static void alua_rtpg_work(struct work_struct *work)
{
	struct alua_port_group *pg =
		container_of(work, struct alua_port_group, rtpg_work.work);
	struct scsi_device *sdev, *prev_sdev = NULL;
	LIST_HEAD(qdata_list);
	int err = SCSI_DH_OK;
	struct alua_queue_data *qdata, *tmp;
	struct alua_dh_data *h;
	unsigned long flags;

	spin_lock_irqsave(&pg->lock, flags);
	sdev = pg->rtpg_sdev;
	if (!sdev) {
		WARN_ON(pg->flags & ALUA_PG_RUN_RTPG);
		WARN_ON(pg->flags & ALUA_PG_RUN_STPG);
		spin_unlock_irqrestore(&pg->lock, flags);
		kref_put(&pg->kref, release_port_group);
		return;
	}
	pg->flags |= ALUA_PG_RUNNING;
	if (pg->flags & ALUA_PG_RUN_RTPG) {
		int state = pg->state;

		pg->flags &= ~ALUA_PG_RUN_RTPG;
		spin_unlock_irqrestore(&pg->lock, flags);
		if (state == SCSI_ACCESS_STATE_TRANSITIONING) {
			if (alua_tur(sdev) == SCSI_DH_RETRY) {
				spin_lock_irqsave(&pg->lock, flags);
				pg->flags &= ~ALUA_PG_RUNNING;
				pg->flags |= ALUA_PG_RUN_RTPG;
				if (!pg->interval)
					pg->interval = ALUA_RTPG_RETRY_DELAY;
				spin_unlock_irqrestore(&pg->lock, flags);
				queue_delayed_work(kaluad_wq, &pg->rtpg_work,
						   pg->interval * HZ);
				return;
			}
			/* Send RTPG on failure or if TUR indicates SUCCESS */
		}
		err = alua_rtpg(sdev, pg);
		spin_lock_irqsave(&pg->lock, flags);

		/* If RTPG failed on the current device, try using another */
		if (err == SCSI_DH_RES_TEMP_UNAVAIL &&
		    (prev_sdev = alua_rtpg_select_sdev(pg)))
			err = SCSI_DH_IMM_RETRY;

		if (err == SCSI_DH_RETRY || err == SCSI_DH_IMM_RETRY ||
		    pg->flags & ALUA_PG_RUN_RTPG) {
			pg->flags &= ~ALUA_PG_RUNNING;
			if (err == SCSI_DH_IMM_RETRY)
				pg->interval = 0;
			else if (!pg->interval && !(pg->flags & ALUA_PG_RUN_RTPG))
				pg->interval = ALUA_RTPG_RETRY_DELAY;
			pg->flags |= ALUA_PG_RUN_RTPG;
			spin_unlock_irqrestore(&pg->lock, flags);
			goto queue_rtpg;
		}
		if (err != SCSI_DH_OK)
			pg->flags &= ~ALUA_PG_RUN_STPG;
	}
	if (pg->flags & ALUA_PG_RUN_STPG) {
		pg->flags &= ~ALUA_PG_RUN_STPG;
		spin_unlock_irqrestore(&pg->lock, flags);
		err = alua_stpg(sdev, pg);
		spin_lock_irqsave(&pg->lock, flags);
		if (err == SCSI_DH_RETRY || pg->flags & ALUA_PG_RUN_RTPG) {
			pg->flags |= ALUA_PG_RUN_RTPG;
			pg->interval = 0;
			pg->flags &= ~ALUA_PG_RUNNING;
			spin_unlock_irqrestore(&pg->lock, flags);
			goto queue_rtpg;
		}
	}

	list_splice_init(&pg->rtpg_list, &qdata_list);
	/*
	 * We went through an RTPG, for good or bad.
	 * Re-enable all devices for the next attempt.
	 */
	list_for_each_entry(h, &pg->dh_list, node)
		h->disabled = false;
	pg->rtpg_sdev = NULL;
	spin_unlock_irqrestore(&pg->lock, flags);

	if (prev_sdev)
		scsi_device_put(prev_sdev);

	list_for_each_entry_safe(qdata, tmp, &qdata_list, entry) {
		list_del(&qdata->entry);
		if (qdata->callback_fn)
			qdata->callback_fn(qdata->callback_data, err);
		kfree(qdata);
	}
	spin_lock_irqsave(&pg->lock, flags);
	pg->flags &= ~ALUA_PG_RUNNING;
	spin_unlock_irqrestore(&pg->lock, flags);
	scsi_device_put(sdev);
	kref_put(&pg->kref, release_port_group);
	return;

queue_rtpg:
	if (prev_sdev)
		scsi_device_put(prev_sdev);
	queue_delayed_work(kaluad_wq, &pg->rtpg_work, pg->interval * HZ);
}

/**
 * alua_rtpg_queue() - cause RTPG to be submitted asynchronously
 * @pg: ALUA port group associated with @sdev.
 * @sdev: SCSI device for which to submit an RTPG.
 * @qdata: Information about the callback to invoke after the RTPG.
 * @force: Whether or not to submit an RTPG if a work item that will submit an
 *         RTPG already has been scheduled.
 *
 * Returns true if and only if alua_rtpg_work() will be called asynchronously.
 * That function is responsible for calling @qdata->fn().
 *
 * Context: may be called from atomic context (alua_check()) only if the caller
 *	holds an sdev reference.
 */
static bool alua_rtpg_queue(struct alua_port_group *pg,
			    struct scsi_device *sdev,
			    struct alua_queue_data *qdata, bool force)
{
	int start_queue = 0;
	unsigned long flags;

	if (WARN_ON_ONCE(!pg) || scsi_device_get(sdev))
		return false;

	spin_lock_irqsave(&pg->lock, flags);
	if (qdata) {
		list_add_tail(&qdata->entry, &pg->rtpg_list);
		pg->flags |= ALUA_PG_RUN_STPG;
		force = true;
	}
	if (pg->rtpg_sdev == NULL) {
		struct alua_dh_data *h = sdev->handler_data;

		rcu_read_lock();
		if (h && rcu_dereference(h->pg) == pg) {
			pg->interval = 0;
			pg->flags |= ALUA_PG_RUN_RTPG;
			kref_get(&pg->kref);
			pg->rtpg_sdev = sdev;
			start_queue = 1;
		}
		rcu_read_unlock();
	} else if (!(pg->flags & ALUA_PG_RUN_RTPG) && force) {
		pg->flags |= ALUA_PG_RUN_RTPG;
		/* Do not queue if the worker is already running */
		if (!(pg->flags & ALUA_PG_RUNNING)) {
			kref_get(&pg->kref);
			start_queue = 1;
		}
	}

	spin_unlock_irqrestore(&pg->lock, flags);

	if (start_queue) {
		if (queue_delayed_work(kaluad_wq, &pg->rtpg_work,
				msecs_to_jiffies(ALUA_RTPG_DELAY_MSECS)))
			sdev = NULL;
		else
			kref_put(&pg->kref, release_port_group);
	}
	if (sdev)
		scsi_device_put(sdev);

	return true;
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

	mutex_lock(&h->init_mutex);
	h->disabled = false;
	tpgs = alua_check_tpgs(sdev);
	if (tpgs != TPGS_MODE_NONE)
		err = alua_check_vpd(sdev, h, tpgs);
	h->init_error = err;
	mutex_unlock(&h->init_mutex);
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
	unsigned long flags;

	if ((sscanf(params, "%u", &argc) != 1) || (argc != 1))
		return -EINVAL;

	while (*p++)
		;
	if ((sscanf(p, "%u", &optimize) != 1) || (optimize > 1))
		return -EINVAL;

	rcu_read_lock();
	pg = rcu_dereference(h->pg);
	if (!pg) {
		rcu_read_unlock();
		return -ENXIO;
	}
	spin_lock_irqsave(&pg->lock, flags);
	if (optimize)
		pg->flags |= ALUA_OPTIMIZE_STPG;
	else
		pg->flags &= ~ALUA_OPTIMIZE_STPG;
	spin_unlock_irqrestore(&pg->lock, flags);
	rcu_read_unlock();

	return result;
}

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
	struct alua_queue_data *qdata;
	struct alua_port_group *pg;

	qdata = kzalloc(sizeof(*qdata), GFP_KERNEL);
	if (!qdata) {
		err = SCSI_DH_RES_TEMP_UNAVAIL;
		goto out;
	}
	qdata->callback_fn = fn;
	qdata->callback_data = data;

	mutex_lock(&h->init_mutex);
	rcu_read_lock();
	pg = rcu_dereference(h->pg);
	if (!pg || !kref_get_unless_zero(&pg->kref)) {
		rcu_read_unlock();
		kfree(qdata);
		err = h->init_error;
		mutex_unlock(&h->init_mutex);
		goto out;
	}
	rcu_read_unlock();
	mutex_unlock(&h->init_mutex);

	if (alua_rtpg_queue(pg, sdev, qdata, true))
		fn = NULL;
	else
		err = SCSI_DH_DEV_OFFLINED;
	kref_put(&pg->kref, release_port_group);
out:
	if (fn)
		fn(data, err);
	return 0;
}

/*
 * alua_check - check path status
 * @sdev: device on the path to be checked
 *
 * Check the device status
 */
static void alua_check(struct scsi_device *sdev, bool force)
{
	struct alua_dh_data *h = sdev->handler_data;
	struct alua_port_group *pg;

	rcu_read_lock();
	pg = rcu_dereference(h->pg);
	if (!pg || !kref_get_unless_zero(&pg->kref)) {
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();
	alua_rtpg_queue(pg, sdev, NULL, force);
	kref_put(&pg->kref, release_port_group);
}

/*
 * alua_prep_fn - request callback
 *
 * Fail I/O to all paths not in state
 * active/optimized or active/non-optimized.
 */
static blk_status_t alua_prep_fn(struct scsi_device *sdev, struct request *req)
{
	struct alua_dh_data *h = sdev->handler_data;
	struct alua_port_group *pg;
	unsigned char state = SCSI_ACCESS_STATE_OPTIMAL;

	rcu_read_lock();
	pg = rcu_dereference(h->pg);
	if (pg)
		state = pg->state;
	rcu_read_unlock();

	switch (state) {
	case SCSI_ACCESS_STATE_OPTIMAL:
	case SCSI_ACCESS_STATE_ACTIVE:
	case SCSI_ACCESS_STATE_LBA:
	case SCSI_ACCESS_STATE_TRANSITIONING:
		return BLK_STS_OK;
	default:
		req->rq_flags |= RQF_QUIET;
		return BLK_STS_IOERR;
	}
}

static void alua_rescan(struct scsi_device *sdev)
{
	struct alua_dh_data *h = sdev->handler_data;

	alua_initialize(sdev, h);
}

/*
 * alua_bus_attach - Attach device handler
 * @sdev: device to be attached to
 */
static int alua_bus_attach(struct scsi_device *sdev)
{
	struct alua_dh_data *h;
	int err;

	h = kzalloc(sizeof(*h) , GFP_KERNEL);
	if (!h)
		return SCSI_DH_NOMEM;
	spin_lock_init(&h->pg_lock);
	rcu_assign_pointer(h->pg, NULL);
	h->init_error = SCSI_DH_OK;
	h->sdev = sdev;
	INIT_LIST_HEAD(&h->node);

	mutex_init(&h->init_mutex);
	err = alua_initialize(sdev, h);
	if (err != SCSI_DH_OK && err != SCSI_DH_DEV_OFFLINED)
		goto failed;

	sdev->handler_data = h;
	return SCSI_DH_OK;
failed:
	kfree(h);
	return err;
}

/*
 * alua_bus_detach - Detach device handler
 * @sdev: device to be detached from
 */
static void alua_bus_detach(struct scsi_device *sdev)
{
	struct alua_dh_data *h = sdev->handler_data;
	struct alua_port_group *pg;

	spin_lock(&h->pg_lock);
	pg = rcu_dereference_protected(h->pg, lockdep_is_held(&h->pg_lock));
	rcu_assign_pointer(h->pg, NULL);
	spin_unlock(&h->pg_lock);
	if (pg) {
		spin_lock_irq(&pg->lock);
		list_del_rcu(&h->node);
		spin_unlock_irq(&pg->lock);
		kref_put(&pg->kref, release_port_group);
	}
	sdev->handler_data = NULL;
	synchronize_rcu();
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
	.rescan = alua_rescan,
	.set_params = alua_set_params,
};

static int __init alua_init(void)
{
	int r;

	kaluad_wq = alloc_workqueue("kaluad", WQ_MEM_RECLAIM, 0);
	if (!kaluad_wq)
		return -ENOMEM;

	r = scsi_register_device_handler(&alua_dh);
	if (r != 0) {
		printk(KERN_ERR "%s: Failed to register scsi device handler",
			ALUA_DH_NAME);
		destroy_workqueue(kaluad_wq);
	}
	return r;
}

static void __exit alua_exit(void)
{
	scsi_unregister_device_handler(&alua_dh);
	destroy_workqueue(kaluad_wq);
}

module_init(alua_init);
module_exit(alua_exit);

MODULE_DESCRIPTION("DM Multipath ALUA support");
MODULE_AUTHOR("Hannes Reinecke <hare@suse.de>");
MODULE_LICENSE("GPL");
MODULE_VERSION(ALUA_DH_VER);
