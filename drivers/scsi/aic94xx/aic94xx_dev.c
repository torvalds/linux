// SPDX-License-Identifier: GPL-2.0-only
/*
 * Aic94xx SAS/SATA DDB management
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * $Id: //depot/aic94xx/aic94xx_dev.c#21 $
 */

#include "aic94xx.h"
#include "aic94xx_hwi.h"
#include "aic94xx_reg.h"
#include "aic94xx_sas.h"

#define FIND_FREE_DDB(_ha) find_first_zero_bit((_ha)->hw_prof.ddb_bitmap, \
					       (_ha)->hw_prof.max_ddbs)
#define SET_DDB(_ddb, _ha) set_bit(_ddb, (_ha)->hw_prof.ddb_bitmap)
#define CLEAR_DDB(_ddb, _ha) clear_bit(_ddb, (_ha)->hw_prof.ddb_bitmap)

static int asd_get_ddb(struct asd_ha_struct *asd_ha)
{
	int ddb, i;

	ddb = FIND_FREE_DDB(asd_ha);
	if (ddb >= asd_ha->hw_prof.max_ddbs) {
		ddb = -ENOMEM;
		goto out;
	}
	SET_DDB(ddb, asd_ha);

	for (i = 0; i < sizeof(struct asd_ddb_ssp_smp_target_port); i+= 4)
		asd_ddbsite_write_dword(asd_ha, ddb, i, 0);
out:
	return ddb;
}

#define INIT_CONN_TAG   offsetof(struct asd_ddb_ssp_smp_target_port, init_conn_tag)
#define DEST_SAS_ADDR   offsetof(struct asd_ddb_ssp_smp_target_port, dest_sas_addr)
#define SEND_QUEUE_HEAD offsetof(struct asd_ddb_ssp_smp_target_port, send_queue_head)
#define DDB_TYPE        offsetof(struct asd_ddb_ssp_smp_target_port, ddb_type)
#define CONN_MASK       offsetof(struct asd_ddb_ssp_smp_target_port, conn_mask)
#define DDB_TARG_FLAGS  offsetof(struct asd_ddb_ssp_smp_target_port, flags)
#define DDB_TARG_FLAGS2 offsetof(struct asd_ddb_stp_sata_target_port, flags2)
#define EXEC_QUEUE_TAIL offsetof(struct asd_ddb_ssp_smp_target_port, exec_queue_tail)
#define SEND_QUEUE_TAIL offsetof(struct asd_ddb_ssp_smp_target_port, send_queue_tail)
#define SISTER_DDB      offsetof(struct asd_ddb_ssp_smp_target_port, sister_ddb)
#define MAX_CCONN       offsetof(struct asd_ddb_ssp_smp_target_port, max_concurrent_conn)
#define NUM_CTX         offsetof(struct asd_ddb_ssp_smp_target_port, num_contexts)
#define ATA_CMD_SCBPTR  offsetof(struct asd_ddb_stp_sata_target_port, ata_cmd_scbptr)
#define SATA_TAG_ALLOC_MASK offsetof(struct asd_ddb_stp_sata_target_port, sata_tag_alloc_mask)
#define NUM_SATA_TAGS   offsetof(struct asd_ddb_stp_sata_target_port, num_sata_tags)
#define SATA_STATUS     offsetof(struct asd_ddb_stp_sata_target_port, sata_status)
#define NCQ_DATA_SCB_PTR offsetof(struct asd_ddb_stp_sata_target_port, ncq_data_scb_ptr)
#define ITNL_TIMEOUT    offsetof(struct asd_ddb_ssp_smp_target_port, itnl_timeout)

static void asd_free_ddb(struct asd_ha_struct *asd_ha, int ddb)
{
	if (!ddb || ddb >= 0xFFFF)
		return;
	asd_ddbsite_write_byte(asd_ha, ddb, DDB_TYPE, DDB_TYPE_UNUSED);
	CLEAR_DDB(ddb, asd_ha);
}

static void asd_set_ddb_type(struct domain_device *dev)
{
	struct asd_ha_struct *asd_ha = dev->port->ha->lldd_ha;
	int ddb = (int) (unsigned long) dev->lldd_dev;

	if (dev->dev_type == SAS_SATA_PM_PORT)
		asd_ddbsite_write_byte(asd_ha,ddb, DDB_TYPE, DDB_TYPE_PM_PORT);
	else if (dev->tproto)
		asd_ddbsite_write_byte(asd_ha,ddb, DDB_TYPE, DDB_TYPE_TARGET);
	else
		asd_ddbsite_write_byte(asd_ha,ddb,DDB_TYPE,DDB_TYPE_INITIATOR);
}

static int asd_init_sata_tag_ddb(struct domain_device *dev)
{
	struct asd_ha_struct *asd_ha = dev->port->ha->lldd_ha;
	int ddb, i;

	ddb = asd_get_ddb(asd_ha);
	if (ddb < 0)
		return ddb;

	for (i = 0; i < sizeof(struct asd_ddb_sata_tag); i += 2)
		asd_ddbsite_write_word(asd_ha, ddb, i, 0xFFFF);

	asd_ddbsite_write_word(asd_ha, (int) (unsigned long) dev->lldd_dev,
			       SISTER_DDB, ddb);
	return 0;
}

void asd_set_dmamode(struct domain_device *dev)
{
	struct asd_ha_struct *asd_ha = dev->port->ha->lldd_ha;
	struct ata_device *ata_dev = sas_to_ata_dev(dev);
	int ddb = (int) (unsigned long) dev->lldd_dev;
	u32 qdepth = 0;

	if (dev->dev_type == SAS_SATA_DEV || dev->dev_type == SAS_SATA_PM_PORT) {
		if (ata_id_has_ncq(ata_dev->id))
			qdepth = ata_id_queue_depth(ata_dev->id);
		asd_ddbsite_write_dword(asd_ha, ddb, SATA_TAG_ALLOC_MASK,
					(1ULL<<qdepth)-1);
		asd_ddbsite_write_byte(asd_ha, ddb, NUM_SATA_TAGS, qdepth);
	}

	if (qdepth > 0)
		if (asd_init_sata_tag_ddb(dev) != 0) {
			unsigned long flags;

			spin_lock_irqsave(dev->sata_dev.ap->lock, flags);
			ata_dev->flags |= ATA_DFLAG_NCQ_OFF;
			spin_unlock_irqrestore(dev->sata_dev.ap->lock, flags);
		}
}

static int asd_init_sata(struct domain_device *dev)
{
	struct asd_ha_struct *asd_ha = dev->port->ha->lldd_ha;
	int ddb = (int) (unsigned long) dev->lldd_dev;

	asd_ddbsite_write_word(asd_ha, ddb, ATA_CMD_SCBPTR, 0xFFFF);
	if (dev->dev_type == SAS_SATA_DEV || dev->dev_type == SAS_SATA_PM ||
	    dev->dev_type == SAS_SATA_PM_PORT) {
		struct dev_to_host_fis *fis = (struct dev_to_host_fis *)
			dev->frame_rcvd;
		asd_ddbsite_write_byte(asd_ha, ddb, SATA_STATUS, fis->status);
	}
	asd_ddbsite_write_word(asd_ha, ddb, NCQ_DATA_SCB_PTR, 0xFFFF);

	return 0;
}

static int asd_init_target_ddb(struct domain_device *dev)
{
	int ddb, i;
	struct asd_ha_struct *asd_ha = dev->port->ha->lldd_ha;
	u8 flags = 0;

	ddb = asd_get_ddb(asd_ha);
	if (ddb < 0)
		return ddb;

	dev->lldd_dev = (void *) (unsigned long) ddb;

	asd_ddbsite_write_byte(asd_ha, ddb, 0, DDB_TP_CONN_TYPE);
	asd_ddbsite_write_byte(asd_ha, ddb, 1, 0);
	asd_ddbsite_write_word(asd_ha, ddb, INIT_CONN_TAG, 0xFFFF);
	for (i = 0; i < SAS_ADDR_SIZE; i++)
		asd_ddbsite_write_byte(asd_ha, ddb, DEST_SAS_ADDR+i,
				       dev->sas_addr[i]);
	asd_ddbsite_write_word(asd_ha, ddb, SEND_QUEUE_HEAD, 0xFFFF);
	asd_set_ddb_type(dev);
	asd_ddbsite_write_byte(asd_ha, ddb, CONN_MASK, dev->port->phy_mask);
	if (dev->port->oob_mode != SATA_OOB_MODE) {
		flags |= OPEN_REQUIRED;
		if ((dev->dev_type == SAS_SATA_DEV) ||
		    (dev->tproto & SAS_PROTOCOL_STP)) {
			struct smp_resp *rps_resp = &dev->sata_dev.rps_resp;
			if (rps_resp->frame_type == SMP_RESPONSE &&
			    rps_resp->function == SMP_REPORT_PHY_SATA &&
			    rps_resp->result == SMP_RESP_FUNC_ACC) {
				if (rps_resp->rps.affil_valid)
					flags |= STP_AFFIL_POL;
				if (rps_resp->rps.affil_supp)
					flags |= SUPPORTS_AFFIL;
			}
		} else {
			flags |= CONCURRENT_CONN_SUPP;
			if (!dev->parent &&
			    (dev->dev_type == SAS_EDGE_EXPANDER_DEVICE ||
			     dev->dev_type == SAS_FANOUT_EXPANDER_DEVICE))
				asd_ddbsite_write_byte(asd_ha, ddb, MAX_CCONN,
						       4);
			else
				asd_ddbsite_write_byte(asd_ha, ddb, MAX_CCONN,
						       dev->pathways);
			asd_ddbsite_write_byte(asd_ha, ddb, NUM_CTX, 1);
		}
	}
	if (dev->dev_type == SAS_SATA_PM)
		flags |= SATA_MULTIPORT;
	asd_ddbsite_write_byte(asd_ha, ddb, DDB_TARG_FLAGS, flags);

	flags = 0;
	if (dev->tproto & SAS_PROTOCOL_STP)
		flags |= STP_CL_POL_NO_TX;
	asd_ddbsite_write_byte(asd_ha, ddb, DDB_TARG_FLAGS2, flags);

	asd_ddbsite_write_word(asd_ha, ddb, EXEC_QUEUE_TAIL, 0xFFFF);
	asd_ddbsite_write_word(asd_ha, ddb, SEND_QUEUE_TAIL, 0xFFFF);
	asd_ddbsite_write_word(asd_ha, ddb, SISTER_DDB, 0xFFFF);

	if (dev->dev_type == SAS_SATA_DEV || (dev->tproto & SAS_PROTOCOL_STP)) {
		i = asd_init_sata(dev);
		if (i < 0) {
			asd_free_ddb(asd_ha, ddb);
			return i;
		}
	}

	if (dev->dev_type == SAS_END_DEVICE) {
		struct sas_end_device *rdev = rphy_to_end_device(dev->rphy);
		if (rdev->I_T_nexus_loss_timeout > 0)
			asd_ddbsite_write_word(asd_ha, ddb, ITNL_TIMEOUT,
					       min(rdev->I_T_nexus_loss_timeout,
						   (u16)ITNL_TIMEOUT_CONST));
		else
			asd_ddbsite_write_word(asd_ha, ddb, ITNL_TIMEOUT,
					       (u16)ITNL_TIMEOUT_CONST);
	}
	return 0;
}

static int asd_init_sata_pm_table_ddb(struct domain_device *dev)
{
	struct asd_ha_struct *asd_ha = dev->port->ha->lldd_ha;
	int ddb, i;

	ddb = asd_get_ddb(asd_ha);
	if (ddb < 0)
		return ddb;

	for (i = 0; i < 32; i += 2)
		asd_ddbsite_write_word(asd_ha, ddb, i, 0xFFFF);

	asd_ddbsite_write_word(asd_ha, (int) (unsigned long) dev->lldd_dev,
			       SISTER_DDB, ddb);

	return 0;
}

#define PM_PORT_FLAGS offsetof(struct asd_ddb_sata_pm_port, pm_port_flags)
#define PARENT_DDB    offsetof(struct asd_ddb_sata_pm_port, parent_ddb)

/**
 * asd_init_sata_pm_port_ddb -- SATA Port Multiplier Port
 * dev: pointer to domain device
 *
 * For SATA Port Multiplier Ports we need to allocate one SATA Port
 * Multiplier Port DDB and depending on whether the target on it
 * supports SATA II NCQ, one SATA Tag DDB.
 */
static int asd_init_sata_pm_port_ddb(struct domain_device *dev)
{
	int ddb, i, parent_ddb, pmtable_ddb;
	struct asd_ha_struct *asd_ha = dev->port->ha->lldd_ha;
	u8  flags;

	ddb = asd_get_ddb(asd_ha);
	if (ddb < 0)
		return ddb;

	asd_set_ddb_type(dev);
	flags = (dev->sata_dev.port_no << 4) | PM_PORT_SET;
	asd_ddbsite_write_byte(asd_ha, ddb, PM_PORT_FLAGS, flags);
	asd_ddbsite_write_word(asd_ha, ddb, SISTER_DDB, 0xFFFF);
	asd_ddbsite_write_word(asd_ha, ddb, ATA_CMD_SCBPTR, 0xFFFF);
	asd_init_sata(dev);

	parent_ddb = (int) (unsigned long) dev->parent->lldd_dev;
	asd_ddbsite_write_word(asd_ha, ddb, PARENT_DDB, parent_ddb);
	pmtable_ddb = asd_ddbsite_read_word(asd_ha, parent_ddb, SISTER_DDB);
	asd_ddbsite_write_word(asd_ha, pmtable_ddb, dev->sata_dev.port_no,ddb);

	if (asd_ddbsite_read_byte(asd_ha, ddb, NUM_SATA_TAGS) > 0) {
		i = asd_init_sata_tag_ddb(dev);
		if (i < 0) {
			asd_free_ddb(asd_ha, ddb);
			return i;
		}
	}
	return 0;
}

static int asd_init_initiator_ddb(struct domain_device *dev)
{
	return -ENODEV;
}

/**
 * asd_init_sata_pm_ddb -- SATA Port Multiplier
 * dev: pointer to domain device
 *
 * For STP and direct-attached SATA Port Multipliers we need
 * one target port DDB entry and one SATA PM table DDB entry.
 */
static int asd_init_sata_pm_ddb(struct domain_device *dev)
{
	int res = 0;

	res = asd_init_target_ddb(dev);
	if (res)
		goto out;
	res = asd_init_sata_pm_table_ddb(dev);
	if (res)
		asd_free_ddb(dev->port->ha->lldd_ha,
			     (int) (unsigned long) dev->lldd_dev);
out:
	return res;
}

int asd_dev_found(struct domain_device *dev)
{
	unsigned long flags;
	int res = 0;
	struct asd_ha_struct *asd_ha = dev->port->ha->lldd_ha;

	spin_lock_irqsave(&asd_ha->hw_prof.ddb_lock, flags);
	switch (dev->dev_type) {
	case SAS_SATA_PM:
		res = asd_init_sata_pm_ddb(dev);
		break;
	case SAS_SATA_PM_PORT:
		res = asd_init_sata_pm_port_ddb(dev);
		break;
	default:
		if (dev->tproto)
			res = asd_init_target_ddb(dev);
		else
			res = asd_init_initiator_ddb(dev);
	}
	spin_unlock_irqrestore(&asd_ha->hw_prof.ddb_lock, flags);

	return res;
}

void asd_dev_gone(struct domain_device *dev)
{
	int ddb, sister_ddb;
	unsigned long flags;
	struct asd_ha_struct *asd_ha = dev->port->ha->lldd_ha;

	spin_lock_irqsave(&asd_ha->hw_prof.ddb_lock, flags);
	ddb = (int) (unsigned long) dev->lldd_dev;
	sister_ddb = asd_ddbsite_read_word(asd_ha, ddb, SISTER_DDB);

	if (sister_ddb != 0xFFFF)
		asd_free_ddb(asd_ha, sister_ddb);
	asd_free_ddb(asd_ha, ddb);
	dev->lldd_dev = NULL;
	spin_unlock_irqrestore(&asd_ha->hw_prof.ddb_lock, flags);
}
