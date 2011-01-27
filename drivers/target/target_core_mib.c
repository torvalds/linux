/*******************************************************************************
 * Filename:  target_core_mib.c
 *
 * Copyright (c) 2006-2007 SBE, Inc.  All Rights Reserved.
 * Copyright (c) 2007-2010 Rising Tide Systems
 * Copyright (c) 2008-2010 Linux-iSCSI.org
 *
 * Nicholas A. Bellinger <nab@linux-iscsi.org>
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
 ******************************************************************************/


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/version.h>
#include <generated/utsrelease.h>
#include <linux/utsname.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/blkdev.h>
#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include <target/target_core_base.h>
#include <target/target_core_transport.h>
#include <target/target_core_fabric_ops.h>
#include <target/target_core_configfs.h>

#include "target_core_hba.h"
#include "target_core_mib.h"

/* SCSI mib table index */
static struct scsi_index_table scsi_index_table;

#ifndef INITIAL_JIFFIES
#define INITIAL_JIFFIES ((unsigned long)(unsigned int) (-300*HZ))
#endif

/* SCSI Instance Table */
#define SCSI_INST_SW_INDEX		1
#define SCSI_TRANSPORT_INDEX		1

#define NONE		"None"
#define ISPRINT(a)   ((a >= ' ') && (a <= '~'))

static inline int list_is_first(const struct list_head *list,
				const struct list_head *head)
{
	return list->prev == head;
}

static void *locate_hba_start(
	struct seq_file *seq,
	loff_t *pos)
{
	spin_lock(&se_global->g_device_lock);
	return seq_list_start(&se_global->g_se_dev_list, *pos);
}

static void *locate_hba_next(
	struct seq_file *seq,
	void *v,
	loff_t *pos)
{
	return seq_list_next(v, &se_global->g_se_dev_list, pos);
}

static void locate_hba_stop(struct seq_file *seq, void *v)
{
	spin_unlock(&se_global->g_device_lock);
}

/****************************************************************************
 * SCSI MIB Tables
 ****************************************************************************/

/*
 * SCSI Instance Table
 */
static void *scsi_inst_seq_start(
	struct seq_file *seq,
	loff_t *pos)
{
	spin_lock(&se_global->hba_lock);
	return seq_list_start(&se_global->g_hba_list, *pos);
}

static void *scsi_inst_seq_next(
	struct seq_file *seq,
	void *v,
	loff_t *pos)
{
	return seq_list_next(v, &se_global->g_hba_list, pos);
}

static void scsi_inst_seq_stop(struct seq_file *seq, void *v)
{
	spin_unlock(&se_global->hba_lock);
}

static int scsi_inst_seq_show(struct seq_file *seq, void *v)
{
	struct se_hba *hba = list_entry(v, struct se_hba, hba_list);

	if (list_is_first(&hba->hba_list, &se_global->g_hba_list))
		seq_puts(seq, "inst sw_indx\n");

	seq_printf(seq, "%u %u\n", hba->hba_index, SCSI_INST_SW_INDEX);
	seq_printf(seq, "plugin: %s version: %s\n",
			hba->transport->name, TARGET_CORE_VERSION);

	return 0;
}

static const struct seq_operations scsi_inst_seq_ops = {
	.start	= scsi_inst_seq_start,
	.next	= scsi_inst_seq_next,
	.stop	= scsi_inst_seq_stop,
	.show	= scsi_inst_seq_show
};

static int scsi_inst_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scsi_inst_seq_ops);
}

static const struct file_operations scsi_inst_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = scsi_inst_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

/*
 * SCSI Device Table
 */
static void *scsi_dev_seq_start(struct seq_file *seq, loff_t *pos)
{
	return locate_hba_start(seq, pos);
}

static void *scsi_dev_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return locate_hba_next(seq, v, pos);
}

static void scsi_dev_seq_stop(struct seq_file *seq, void *v)
{
	locate_hba_stop(seq, v);
}

static int scsi_dev_seq_show(struct seq_file *seq, void *v)
{
	struct se_hba *hba;
	struct se_subsystem_dev *se_dev = list_entry(v, struct se_subsystem_dev,
						g_se_dev_list);
	struct se_device *dev = se_dev->se_dev_ptr;
	char str[28];
	int k;

	if (list_is_first(&se_dev->g_se_dev_list, &se_global->g_se_dev_list))
		seq_puts(seq, "inst indx role ports\n");

	if (!(dev))
		return 0;

	hba = dev->se_hba;
	if (!(hba)) {
		/* Log error ? */
		return 0;
	}

	seq_printf(seq, "%u %u %s %u\n", hba->hba_index,
		   dev->dev_index, "Target", dev->dev_port_count);

	memcpy(&str[0], (void *)DEV_T10_WWN(dev), 28);

	/* vendor */
	for (k = 0; k < 8; k++)
		str[k] = ISPRINT(DEV_T10_WWN(dev)->vendor[k]) ?
				DEV_T10_WWN(dev)->vendor[k] : 0x20;
	str[k] = 0x20;

	/* model */
	for (k = 0; k < 16; k++)
		str[k+9] = ISPRINT(DEV_T10_WWN(dev)->model[k]) ?
				DEV_T10_WWN(dev)->model[k] : 0x20;
	str[k + 9] = 0;

	seq_printf(seq, "dev_alias: %s\n", str);

	return 0;
}

static const struct seq_operations scsi_dev_seq_ops = {
	.start  = scsi_dev_seq_start,
	.next   = scsi_dev_seq_next,
	.stop   = scsi_dev_seq_stop,
	.show   = scsi_dev_seq_show
};

static int scsi_dev_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scsi_dev_seq_ops);
}

static const struct file_operations scsi_dev_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = scsi_dev_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

/*
 * SCSI Port Table
 */
static void *scsi_port_seq_start(struct seq_file *seq, loff_t *pos)
{
	return locate_hba_start(seq, pos);
}

static void *scsi_port_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return locate_hba_next(seq, v, pos);
}

static void scsi_port_seq_stop(struct seq_file *seq, void *v)
{
	locate_hba_stop(seq, v);
}

static int scsi_port_seq_show(struct seq_file *seq, void *v)
{
	struct se_hba *hba;
	struct se_subsystem_dev *se_dev = list_entry(v, struct se_subsystem_dev,
						g_se_dev_list);
	struct se_device *dev = se_dev->se_dev_ptr;
	struct se_port *sep, *sep_tmp;

	if (list_is_first(&se_dev->g_se_dev_list, &se_global->g_se_dev_list))
		seq_puts(seq, "inst device indx role busy_count\n");

	if (!(dev))
		return 0;

	hba = dev->se_hba;
	if (!(hba)) {
		/* Log error ? */
		return 0;
	}

	/* FIXME: scsiPortBusyStatuses count */
	spin_lock(&dev->se_port_lock);
	list_for_each_entry_safe(sep, sep_tmp, &dev->dev_sep_list, sep_list) {
		seq_printf(seq, "%u %u %u %s%u %u\n", hba->hba_index,
			dev->dev_index, sep->sep_index, "Device",
			dev->dev_index, 0);
	}
	spin_unlock(&dev->se_port_lock);

	return 0;
}

static const struct seq_operations scsi_port_seq_ops = {
	.start  = scsi_port_seq_start,
	.next   = scsi_port_seq_next,
	.stop   = scsi_port_seq_stop,
	.show   = scsi_port_seq_show
};

static int scsi_port_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scsi_port_seq_ops);
}

static const struct file_operations scsi_port_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = scsi_port_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

/*
 * SCSI Transport Table
 */
static void *scsi_transport_seq_start(struct seq_file *seq, loff_t *pos)
{
	return locate_hba_start(seq, pos);
}

static void *scsi_transport_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return locate_hba_next(seq, v, pos);
}

static void scsi_transport_seq_stop(struct seq_file *seq, void *v)
{
	locate_hba_stop(seq, v);
}

static int scsi_transport_seq_show(struct seq_file *seq, void *v)
{
	struct se_hba *hba;
	struct se_subsystem_dev *se_dev = list_entry(v, struct se_subsystem_dev,
						g_se_dev_list);
	struct se_device *dev = se_dev->se_dev_ptr;
	struct se_port *se, *se_tmp;
	struct se_portal_group *tpg;
	struct t10_wwn *wwn;
	char buf[64];

	if (list_is_first(&se_dev->g_se_dev_list, &se_global->g_se_dev_list))
		seq_puts(seq, "inst device indx dev_name\n");

	if (!(dev))
		return 0;

	hba = dev->se_hba;
	if (!(hba)) {
		/* Log error ? */
		return 0;
	}

	wwn = DEV_T10_WWN(dev);

	spin_lock(&dev->se_port_lock);
	list_for_each_entry_safe(se, se_tmp, &dev->dev_sep_list, sep_list) {
		tpg = se->sep_tpg;
		sprintf(buf, "scsiTransport%s",
				TPG_TFO(tpg)->get_fabric_name());

		seq_printf(seq, "%u %s %u %s+%s\n",
			hba->hba_index, /* scsiTransportIndex */
			buf,  /* scsiTransportType */
			(TPG_TFO(tpg)->tpg_get_inst_index != NULL) ?
			TPG_TFO(tpg)->tpg_get_inst_index(tpg) :
			0,
			TPG_TFO(tpg)->tpg_get_wwn(tpg),
			(strlen(wwn->unit_serial)) ?
			/* scsiTransportDevName */
			wwn->unit_serial : wwn->vendor);
	}
	spin_unlock(&dev->se_port_lock);

	return 0;
}

static const struct seq_operations scsi_transport_seq_ops = {
	.start  = scsi_transport_seq_start,
	.next   = scsi_transport_seq_next,
	.stop   = scsi_transport_seq_stop,
	.show   = scsi_transport_seq_show
};

static int scsi_transport_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scsi_transport_seq_ops);
}

static const struct file_operations scsi_transport_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = scsi_transport_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

/*
 * SCSI Target Device Table
 */
static void *scsi_tgt_dev_seq_start(struct seq_file *seq, loff_t *pos)
{
	return locate_hba_start(seq, pos);
}

static void *scsi_tgt_dev_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return locate_hba_next(seq, v, pos);
}

static void scsi_tgt_dev_seq_stop(struct seq_file *seq, void *v)
{
	locate_hba_stop(seq, v);
}


#define LU_COUNT	1  /* for now */
static int scsi_tgt_dev_seq_show(struct seq_file *seq, void *v)
{
	struct se_hba *hba;
	struct se_subsystem_dev *se_dev = list_entry(v, struct se_subsystem_dev,
						g_se_dev_list);
	struct se_device *dev = se_dev->se_dev_ptr;
	int non_accessible_lus = 0;
	char status[16];

	if (list_is_first(&se_dev->g_se_dev_list, &se_global->g_se_dev_list))
		seq_puts(seq, "inst indx num_LUs status non_access_LUs"
			" resets\n");

	if (!(dev))
		return 0;

	hba = dev->se_hba;
	if (!(hba)) {
		/* Log error ? */
		return 0;
	}

	switch (dev->dev_status) {
	case TRANSPORT_DEVICE_ACTIVATED:
		strcpy(status, "activated");
		break;
	case TRANSPORT_DEVICE_DEACTIVATED:
		strcpy(status, "deactivated");
		non_accessible_lus = 1;
		break;
	case TRANSPORT_DEVICE_SHUTDOWN:
		strcpy(status, "shutdown");
		non_accessible_lus = 1;
		break;
	case TRANSPORT_DEVICE_OFFLINE_ACTIVATED:
	case TRANSPORT_DEVICE_OFFLINE_DEACTIVATED:
		strcpy(status, "offline");
		non_accessible_lus = 1;
		break;
	default:
		sprintf(status, "unknown(%d)", dev->dev_status);
		non_accessible_lus = 1;
	}

	seq_printf(seq, "%u %u %u %s %u %u\n",
		   hba->hba_index, dev->dev_index, LU_COUNT,
		   status, non_accessible_lus, dev->num_resets);

	return 0;
}

static const struct seq_operations scsi_tgt_dev_seq_ops = {
	.start  = scsi_tgt_dev_seq_start,
	.next   = scsi_tgt_dev_seq_next,
	.stop   = scsi_tgt_dev_seq_stop,
	.show   = scsi_tgt_dev_seq_show
};

static int scsi_tgt_dev_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scsi_tgt_dev_seq_ops);
}

static const struct file_operations scsi_tgt_dev_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = scsi_tgt_dev_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

/*
 * SCSI Target Port Table
 */
static void *scsi_tgt_port_seq_start(struct seq_file *seq, loff_t *pos)
{
	return locate_hba_start(seq, pos);
}

static void *scsi_tgt_port_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return locate_hba_next(seq, v, pos);
}

static void scsi_tgt_port_seq_stop(struct seq_file *seq, void *v)
{
	locate_hba_stop(seq, v);
}

static int scsi_tgt_port_seq_show(struct seq_file *seq, void *v)
{
	struct se_hba *hba;
	struct se_subsystem_dev *se_dev = list_entry(v, struct se_subsystem_dev,
						g_se_dev_list);
	struct se_device *dev = se_dev->se_dev_ptr;
	struct se_port *sep, *sep_tmp;
	struct se_portal_group *tpg;
	u32 rx_mbytes, tx_mbytes;
	unsigned long long num_cmds;
	char buf[64];

	if (list_is_first(&se_dev->g_se_dev_list, &se_global->g_se_dev_list))
		seq_puts(seq, "inst device indx name port_index in_cmds"
			" write_mbytes read_mbytes hs_in_cmds\n");

	if (!(dev))
		return 0;

	hba = dev->se_hba;
	if (!(hba)) {
		/* Log error ? */
		return 0;
	}

	spin_lock(&dev->se_port_lock);
	list_for_each_entry_safe(sep, sep_tmp, &dev->dev_sep_list, sep_list) {
		tpg = sep->sep_tpg;
		sprintf(buf, "%sPort#",
			TPG_TFO(tpg)->get_fabric_name());

		seq_printf(seq, "%u %u %u %s%d %s%s%d ",
		     hba->hba_index,
		     dev->dev_index,
		     sep->sep_index,
		     buf, sep->sep_index,
		     TPG_TFO(tpg)->tpg_get_wwn(tpg), "+t+",
		     TPG_TFO(tpg)->tpg_get_tag(tpg));

		spin_lock(&sep->sep_lun->lun_sep_lock);
		num_cmds = sep->sep_stats.cmd_pdus;
		rx_mbytes = (sep->sep_stats.rx_data_octets >> 20);
		tx_mbytes = (sep->sep_stats.tx_data_octets >> 20);
		spin_unlock(&sep->sep_lun->lun_sep_lock);

		seq_printf(seq, "%llu %u %u %u\n", num_cmds,
			rx_mbytes, tx_mbytes, 0);
	}
	spin_unlock(&dev->se_port_lock);

	return 0;
}

static const struct seq_operations scsi_tgt_port_seq_ops = {
	.start  = scsi_tgt_port_seq_start,
	.next   = scsi_tgt_port_seq_next,
	.stop   = scsi_tgt_port_seq_stop,
	.show   = scsi_tgt_port_seq_show
};

static int scsi_tgt_port_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scsi_tgt_port_seq_ops);
}

static const struct file_operations scsi_tgt_port_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = scsi_tgt_port_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

/*
 * SCSI Authorized Initiator Table:
 * It contains the SCSI Initiators authorized to be attached to one of the
 * local Target ports.
 * Iterates through all active TPGs and extracts the info from the ACLs
 */
static void *scsi_auth_intr_seq_start(struct seq_file *seq, loff_t *pos)
{
	spin_lock_bh(&se_global->se_tpg_lock);
	return seq_list_start(&se_global->g_se_tpg_list, *pos);
}

static void *scsi_auth_intr_seq_next(struct seq_file *seq, void *v,
					 loff_t *pos)
{
	return seq_list_next(v, &se_global->g_se_tpg_list, pos);
}

static void scsi_auth_intr_seq_stop(struct seq_file *seq, void *v)
{
	spin_unlock_bh(&se_global->se_tpg_lock);
}

static int scsi_auth_intr_seq_show(struct seq_file *seq, void *v)
{
	struct se_portal_group *se_tpg = list_entry(v, struct se_portal_group,
						se_tpg_list);
	struct se_dev_entry *deve;
	struct se_lun *lun;
	struct se_node_acl *se_nacl;
	int j;

	if (list_is_first(&se_tpg->se_tpg_list,
			  &se_global->g_se_tpg_list))
		seq_puts(seq, "inst dev port indx dev_or_port intr_name "
			 "map_indx att_count num_cmds read_mbytes "
			 "write_mbytes hs_num_cmds creation_time row_status\n");

	if (!(se_tpg))
		return 0;

	spin_lock(&se_tpg->acl_node_lock);
	list_for_each_entry(se_nacl, &se_tpg->acl_node_list, acl_list) {

		atomic_inc(&se_nacl->mib_ref_count);
		smp_mb__after_atomic_inc();
		spin_unlock(&se_tpg->acl_node_lock);

		spin_lock_irq(&se_nacl->device_list_lock);
		for (j = 0; j < TRANSPORT_MAX_LUNS_PER_TPG; j++) {
			deve = &se_nacl->device_list[j];
			if (!(deve->lun_flags &
					TRANSPORT_LUNFLAGS_INITIATOR_ACCESS) ||
			    (!deve->se_lun))
				continue;
			lun = deve->se_lun;
			if (!lun->lun_se_dev)
				continue;

			seq_printf(seq, "%u %u %u %u %u %s %u %u %u %u %u %u"
					" %u %s\n",
				/* scsiInstIndex */
				(TPG_TFO(se_tpg)->tpg_get_inst_index != NULL) ?
				TPG_TFO(se_tpg)->tpg_get_inst_index(se_tpg) :
				0,
				/* scsiDeviceIndex */
				lun->lun_se_dev->dev_index,
				/* scsiAuthIntrTgtPortIndex */
				TPG_TFO(se_tpg)->tpg_get_tag(se_tpg),
				/* scsiAuthIntrIndex */
				se_nacl->acl_index,
				/* scsiAuthIntrDevOrPort */
				1,
				/* scsiAuthIntrName */
				se_nacl->initiatorname[0] ?
					se_nacl->initiatorname : NONE,
				/* FIXME: scsiAuthIntrLunMapIndex */
				0,
				/* scsiAuthIntrAttachedTimes */
				deve->attach_count,
				/* scsiAuthIntrOutCommands */
				deve->total_cmds,
				/* scsiAuthIntrReadMegaBytes */
				(u32)(deve->read_bytes >> 20),
				/* scsiAuthIntrWrittenMegaBytes */
				(u32)(deve->write_bytes >> 20),
				/* FIXME: scsiAuthIntrHSOutCommands */
				0,
				/* scsiAuthIntrLastCreation */
				(u32)(((u32)deve->creation_time -
					    INITIAL_JIFFIES) * 100 / HZ),
				/* FIXME: scsiAuthIntrRowStatus */
				"Ready");
		}
		spin_unlock_irq(&se_nacl->device_list_lock);

		spin_lock(&se_tpg->acl_node_lock);
		atomic_dec(&se_nacl->mib_ref_count);
		smp_mb__after_atomic_dec();
	}
	spin_unlock(&se_tpg->acl_node_lock);

	return 0;
}

static const struct seq_operations scsi_auth_intr_seq_ops = {
	.start	= scsi_auth_intr_seq_start,
	.next	= scsi_auth_intr_seq_next,
	.stop	= scsi_auth_intr_seq_stop,
	.show	= scsi_auth_intr_seq_show
};

static int scsi_auth_intr_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scsi_auth_intr_seq_ops);
}

static const struct file_operations scsi_auth_intr_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = scsi_auth_intr_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

/*
 * SCSI Attached Initiator Port Table:
 * It lists the SCSI Initiators attached to one of the local Target ports.
 * Iterates through all active TPGs and use active sessions from each TPG
 * to list the info fo this table.
 */
static void *scsi_att_intr_port_seq_start(struct seq_file *seq, loff_t *pos)
{
	spin_lock_bh(&se_global->se_tpg_lock);
	return seq_list_start(&se_global->g_se_tpg_list, *pos);
}

static void *scsi_att_intr_port_seq_next(struct seq_file *seq, void *v,
					 loff_t *pos)
{
	return seq_list_next(v, &se_global->g_se_tpg_list, pos);
}

static void scsi_att_intr_port_seq_stop(struct seq_file *seq, void *v)
{
	spin_unlock_bh(&se_global->se_tpg_lock);
}

static int scsi_att_intr_port_seq_show(struct seq_file *seq, void *v)
{
	struct se_portal_group *se_tpg = list_entry(v, struct se_portal_group,
						se_tpg_list);
	struct se_dev_entry *deve;
	struct se_lun *lun;
	struct se_node_acl *se_nacl;
	struct se_session *se_sess;
	unsigned char buf[64];
	int j;

	if (list_is_first(&se_tpg->se_tpg_list,
			  &se_global->g_se_tpg_list))
		seq_puts(seq, "inst dev port indx port_auth_indx port_name"
			" port_ident\n");

	if (!(se_tpg))
		return 0;

	spin_lock(&se_tpg->session_lock);
	list_for_each_entry(se_sess, &se_tpg->tpg_sess_list, sess_list) {
		if ((TPG_TFO(se_tpg)->sess_logged_in(se_sess)) ||
		    (!se_sess->se_node_acl) ||
		    (!se_sess->se_node_acl->device_list))
			continue;

		atomic_inc(&se_sess->mib_ref_count);
		smp_mb__after_atomic_inc();
		se_nacl = se_sess->se_node_acl;
		atomic_inc(&se_nacl->mib_ref_count);
		smp_mb__after_atomic_inc();
		spin_unlock(&se_tpg->session_lock);

		spin_lock_irq(&se_nacl->device_list_lock);
		for (j = 0; j < TRANSPORT_MAX_LUNS_PER_TPG; j++) {
			deve = &se_nacl->device_list[j];
			if (!(deve->lun_flags &
					TRANSPORT_LUNFLAGS_INITIATOR_ACCESS) ||
			   (!deve->se_lun))
				continue;

			lun = deve->se_lun;
			if (!lun->lun_se_dev)
				continue;

			memset(buf, 0, 64);
			if (TPG_TFO(se_tpg)->sess_get_initiator_sid != NULL)
				TPG_TFO(se_tpg)->sess_get_initiator_sid(
					se_sess, (unsigned char *)&buf[0], 64);

			seq_printf(seq, "%u %u %u %u %u %s+i+%s\n",
				/* scsiInstIndex */
				(TPG_TFO(se_tpg)->tpg_get_inst_index != NULL) ?
				TPG_TFO(se_tpg)->tpg_get_inst_index(se_tpg) :
				0,
				/* scsiDeviceIndex */
				lun->lun_se_dev->dev_index,
				/* scsiPortIndex */
				TPG_TFO(se_tpg)->tpg_get_tag(se_tpg),
				/* scsiAttIntrPortIndex */
				(TPG_TFO(se_tpg)->sess_get_index != NULL) ?
				TPG_TFO(se_tpg)->sess_get_index(se_sess) :
				0,
				/* scsiAttIntrPortAuthIntrIdx */
				se_nacl->acl_index,
				/* scsiAttIntrPortName */
				se_nacl->initiatorname[0] ?
					se_nacl->initiatorname : NONE,
				/* scsiAttIntrPortIdentifier */
				buf);
		}
		spin_unlock_irq(&se_nacl->device_list_lock);

		spin_lock(&se_tpg->session_lock);
		atomic_dec(&se_nacl->mib_ref_count);
		smp_mb__after_atomic_dec();
		atomic_dec(&se_sess->mib_ref_count);
		smp_mb__after_atomic_dec();
	}
	spin_unlock(&se_tpg->session_lock);

	return 0;
}

static const struct seq_operations scsi_att_intr_port_seq_ops = {
	.start	= scsi_att_intr_port_seq_start,
	.next	= scsi_att_intr_port_seq_next,
	.stop	= scsi_att_intr_port_seq_stop,
	.show	= scsi_att_intr_port_seq_show
};

static int scsi_att_intr_port_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scsi_att_intr_port_seq_ops);
}

static const struct file_operations scsi_att_intr_port_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = scsi_att_intr_port_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

/*
 * SCSI Logical Unit Table
 */
static void *scsi_lu_seq_start(struct seq_file *seq, loff_t *pos)
{
	return locate_hba_start(seq, pos);
}

static void *scsi_lu_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return locate_hba_next(seq, v, pos);
}

static void scsi_lu_seq_stop(struct seq_file *seq, void *v)
{
	locate_hba_stop(seq, v);
}

#define SCSI_LU_INDEX		1
static int scsi_lu_seq_show(struct seq_file *seq, void *v)
{
	struct se_hba *hba;
	struct se_subsystem_dev *se_dev = list_entry(v, struct se_subsystem_dev,
						g_se_dev_list);
	struct se_device *dev = se_dev->se_dev_ptr;
	int j;
	char str[28];

	if (list_is_first(&se_dev->g_se_dev_list, &se_global->g_se_dev_list))
		seq_puts(seq, "inst dev indx LUN lu_name vend prod rev"
		" dev_type status state-bit num_cmds read_mbytes"
		" write_mbytes resets full_stat hs_num_cmds creation_time\n");

	if (!(dev))
		return 0;

	hba = dev->se_hba;
	if (!(hba)) {
		/* Log error ? */
		return 0;
	}

	/* Fix LU state, if we can read it from the device */
	seq_printf(seq, "%u %u %u %llu %s", hba->hba_index,
			dev->dev_index, SCSI_LU_INDEX,
			(unsigned long long)0, /* FIXME: scsiLuDefaultLun */
			(strlen(DEV_T10_WWN(dev)->unit_serial)) ?
			/* scsiLuWwnName */
			(char *)&DEV_T10_WWN(dev)->unit_serial[0] :
			"None");

	memcpy(&str[0], (void *)DEV_T10_WWN(dev), 28);
	/* scsiLuVendorId */
	for (j = 0; j < 8; j++)
		str[j] = ISPRINT(DEV_T10_WWN(dev)->vendor[j]) ?
			DEV_T10_WWN(dev)->vendor[j] : 0x20;
	str[8] = 0;
	seq_printf(seq, " %s", str);

	/* scsiLuProductId */
	for (j = 0; j < 16; j++)
		str[j] = ISPRINT(DEV_T10_WWN(dev)->model[j]) ?
			DEV_T10_WWN(dev)->model[j] : 0x20;
	str[16] = 0;
	seq_printf(seq, " %s", str);

	/* scsiLuRevisionId */
	for (j = 0; j < 4; j++)
		str[j] = ISPRINT(DEV_T10_WWN(dev)->revision[j]) ?
			DEV_T10_WWN(dev)->revision[j] : 0x20;
	str[4] = 0;
	seq_printf(seq, " %s", str);

	seq_printf(seq, " %u %s %s %llu %u %u %u %u %u %u\n",
		/* scsiLuPeripheralType */
		   TRANSPORT(dev)->get_device_type(dev),
		   (dev->dev_status == TRANSPORT_DEVICE_ACTIVATED) ?
		"available" : "notavailable", /* scsiLuStatus */
		"exposed", 	/* scsiLuState */
		(unsigned long long)dev->num_cmds,
		/* scsiLuReadMegaBytes */
		(u32)(dev->read_bytes >> 20),
		/* scsiLuWrittenMegaBytes */
		(u32)(dev->write_bytes >> 20),
		dev->num_resets, /* scsiLuInResets */
		0, /* scsiLuOutTaskSetFullStatus */
		0, /* scsiLuHSInCommands */
		(u32)(((u32)dev->creation_time - INITIAL_JIFFIES) *
							100 / HZ));

	return 0;
}

static const struct seq_operations scsi_lu_seq_ops = {
	.start  = scsi_lu_seq_start,
	.next   = scsi_lu_seq_next,
	.stop   = scsi_lu_seq_stop,
	.show   = scsi_lu_seq_show
};

static int scsi_lu_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scsi_lu_seq_ops);
}

static const struct file_operations scsi_lu_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = scsi_lu_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

/****************************************************************************/

/*
 * Remove proc fs entries
 */
void remove_scsi_target_mib(void)
{
	remove_proc_entry("scsi_target/mib/scsi_inst", NULL);
	remove_proc_entry("scsi_target/mib/scsi_dev", NULL);
	remove_proc_entry("scsi_target/mib/scsi_port", NULL);
	remove_proc_entry("scsi_target/mib/scsi_transport", NULL);
	remove_proc_entry("scsi_target/mib/scsi_tgt_dev", NULL);
	remove_proc_entry("scsi_target/mib/scsi_tgt_port", NULL);
	remove_proc_entry("scsi_target/mib/scsi_auth_intr", NULL);
	remove_proc_entry("scsi_target/mib/scsi_att_intr_port", NULL);
	remove_proc_entry("scsi_target/mib/scsi_lu", NULL);
	remove_proc_entry("scsi_target/mib", NULL);
}

/*
 * Create proc fs entries for the mib tables
 */
int init_scsi_target_mib(void)
{
	struct proc_dir_entry *dir_entry;
	struct proc_dir_entry *scsi_inst_entry;
	struct proc_dir_entry *scsi_dev_entry;
	struct proc_dir_entry *scsi_port_entry;
	struct proc_dir_entry *scsi_transport_entry;
	struct proc_dir_entry *scsi_tgt_dev_entry;
	struct proc_dir_entry *scsi_tgt_port_entry;
	struct proc_dir_entry *scsi_auth_intr_entry;
	struct proc_dir_entry *scsi_att_intr_port_entry;
	struct proc_dir_entry *scsi_lu_entry;

	dir_entry = proc_mkdir("scsi_target/mib", NULL);
	if (!(dir_entry)) {
		printk(KERN_ERR "proc_mkdir() failed.\n");
		return -1;
	}

	scsi_inst_entry =
		create_proc_entry("scsi_target/mib/scsi_inst", 0, NULL);
	if (scsi_inst_entry)
		scsi_inst_entry->proc_fops = &scsi_inst_seq_fops;
	else
		goto error;

	scsi_dev_entry =
		create_proc_entry("scsi_target/mib/scsi_dev", 0, NULL);
	if (scsi_dev_entry)
		scsi_dev_entry->proc_fops = &scsi_dev_seq_fops;
	else
		goto error;

	scsi_port_entry =
		create_proc_entry("scsi_target/mib/scsi_port", 0, NULL);
	if (scsi_port_entry)
		scsi_port_entry->proc_fops = &scsi_port_seq_fops;
	else
		goto error;

	scsi_transport_entry =
		create_proc_entry("scsi_target/mib/scsi_transport", 0, NULL);
	if (scsi_transport_entry)
		scsi_transport_entry->proc_fops = &scsi_transport_seq_fops;
	else
		goto error;

	scsi_tgt_dev_entry =
		create_proc_entry("scsi_target/mib/scsi_tgt_dev", 0, NULL);
	if (scsi_tgt_dev_entry)
		scsi_tgt_dev_entry->proc_fops = &scsi_tgt_dev_seq_fops;
	else
		goto error;

	scsi_tgt_port_entry =
		create_proc_entry("scsi_target/mib/scsi_tgt_port", 0, NULL);
	if (scsi_tgt_port_entry)
		scsi_tgt_port_entry->proc_fops = &scsi_tgt_port_seq_fops;
	else
		goto error;

	scsi_auth_intr_entry =
		create_proc_entry("scsi_target/mib/scsi_auth_intr", 0, NULL);
	if (scsi_auth_intr_entry)
		scsi_auth_intr_entry->proc_fops = &scsi_auth_intr_seq_fops;
	else
		goto error;

	scsi_att_intr_port_entry =
	      create_proc_entry("scsi_target/mib/scsi_att_intr_port", 0, NULL);
	if (scsi_att_intr_port_entry)
		scsi_att_intr_port_entry->proc_fops =
				&scsi_att_intr_port_seq_fops;
	else
		goto error;

	scsi_lu_entry = create_proc_entry("scsi_target/mib/scsi_lu", 0, NULL);
	if (scsi_lu_entry)
		scsi_lu_entry->proc_fops = &scsi_lu_seq_fops;
	else
		goto error;

	return 0;

error:
	printk(KERN_ERR "create_proc_entry() failed.\n");
	remove_scsi_target_mib();
	return -1;
}

/*
 * Initialize the index table for allocating unique row indexes to various mib
 * tables
 */
void init_scsi_index_table(void)
{
	memset(&scsi_index_table, 0, sizeof(struct scsi_index_table));
	spin_lock_init(&scsi_index_table.lock);
}

/*
 * Allocate a new row index for the entry type specified
 */
u32 scsi_get_new_index(scsi_index_t type)
{
	u32 new_index;

	if ((type < 0) || (type >= SCSI_INDEX_TYPE_MAX)) {
		printk(KERN_ERR "Invalid index type %d\n", type);
		return -1;
	}

	spin_lock(&scsi_index_table.lock);
	new_index = ++scsi_index_table.scsi_mib_index[type];
	if (new_index == 0)
		new_index = ++scsi_index_table.scsi_mib_index[type];
	spin_unlock(&scsi_index_table.lock);

	return new_index;
}
EXPORT_SYMBOL(scsi_get_new_index);
