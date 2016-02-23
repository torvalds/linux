/*
 * Copyright (c) 2012 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
 /***********************************************************/
/*This file support the handling of the Alias GUID feature. */
/***********************************************************/
#include <rdma/ib_mad.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_sa.h>
#include <rdma/ib_pack.h>
#include <linux/mlx4/cmd.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <rdma/ib_user_verbs.h>
#include <linux/delay.h>
#include "mlx4_ib.h"

/*
The driver keeps the current state of all guids, as they are in the HW.
Whenever we receive an smp mad GUIDInfo record, the data will be cached.
*/

struct mlx4_alias_guid_work_context {
	u8 port;
	struct mlx4_ib_dev     *dev ;
	struct ib_sa_query     *sa_query;
	struct completion	done;
	int			query_id;
	struct list_head	list;
	int			block_num;
	ib_sa_comp_mask		guid_indexes;
	u8			method;
};

struct mlx4_next_alias_guid_work {
	u8 port;
	u8 block_num;
	u8 method;
	struct mlx4_sriov_alias_guid_info_rec_det rec_det;
};

static int get_low_record_time_index(struct mlx4_ib_dev *dev, u8 port,
				     int *resched_delay_sec);

void mlx4_ib_update_cache_on_guid_change(struct mlx4_ib_dev *dev, int block_num,
					 u8 port_num, u8 *p_data)
{
	int i;
	u64 guid_indexes;
	int slave_id;
	int port_index = port_num - 1;

	if (!mlx4_is_master(dev->dev))
		return;

	guid_indexes = be64_to_cpu((__force __be64) dev->sriov.alias_guid.
				   ports_guid[port_num - 1].
				   all_rec_per_port[block_num].guid_indexes);
	pr_debug("port: %d, guid_indexes: 0x%llx\n", port_num, guid_indexes);

	for (i = 0; i < NUM_ALIAS_GUID_IN_REC; i++) {
		/* The location of the specific index starts from bit number 4
		 * until bit num 11 */
		if (test_bit(i + 4, (unsigned long *)&guid_indexes)) {
			slave_id = (block_num * NUM_ALIAS_GUID_IN_REC) + i ;
			if (slave_id >= dev->dev->num_slaves) {
				pr_debug("The last slave: %d\n", slave_id);
				return;
			}

			/* cache the guid: */
			memcpy(&dev->sriov.demux[port_index].guid_cache[slave_id],
			       &p_data[i * GUID_REC_SIZE],
			       GUID_REC_SIZE);
		} else
			pr_debug("Guid number: %d in block: %d"
				 " was not updated\n", i, block_num);
	}
}

static __be64 get_cached_alias_guid(struct mlx4_ib_dev *dev, int port, int index)
{
	if (index >= NUM_ALIAS_GUID_PER_PORT) {
		pr_err("%s: ERROR: asked for index:%d\n", __func__, index);
		return (__force __be64) -1;
	}
	return *(__be64 *)&dev->sriov.demux[port - 1].guid_cache[index];
}


ib_sa_comp_mask mlx4_ib_get_aguid_comp_mask_from_ix(int index)
{
	return IB_SA_COMP_MASK(4 + index);
}

void mlx4_ib_slave_alias_guid_event(struct mlx4_ib_dev *dev, int slave,
				    int port,  int slave_init)
{
	__be64 curr_guid, required_guid;
	int record_num = slave / 8;
	int index = slave % 8;
	int port_index = port - 1;
	unsigned long flags;
	int do_work = 0;

	spin_lock_irqsave(&dev->sriov.alias_guid.ag_work_lock, flags);
	if (dev->sriov.alias_guid.ports_guid[port_index].state_flags &
	    GUID_STATE_NEED_PORT_INIT)
		goto unlock;
	if (!slave_init) {
		curr_guid = *(__be64 *)&dev->sriov.
			alias_guid.ports_guid[port_index].
			all_rec_per_port[record_num].
			all_recs[GUID_REC_SIZE * index];
		if (curr_guid == cpu_to_be64(MLX4_GUID_FOR_DELETE_VAL) ||
		    !curr_guid)
			goto unlock;
		required_guid = cpu_to_be64(MLX4_GUID_FOR_DELETE_VAL);
	} else {
		required_guid = mlx4_get_admin_guid(dev->dev, slave, port);
		if (required_guid == cpu_to_be64(MLX4_GUID_FOR_DELETE_VAL))
			goto unlock;
	}
	*(__be64 *)&dev->sriov.alias_guid.ports_guid[port_index].
		all_rec_per_port[record_num].
		all_recs[GUID_REC_SIZE * index] = required_guid;
	dev->sriov.alias_guid.ports_guid[port_index].
		all_rec_per_port[record_num].guid_indexes
		|= mlx4_ib_get_aguid_comp_mask_from_ix(index);
	dev->sriov.alias_guid.ports_guid[port_index].
		all_rec_per_port[record_num].status
		= MLX4_GUID_INFO_STATUS_IDLE;
	/* set to run immediately */
	dev->sriov.alias_guid.ports_guid[port_index].
		all_rec_per_port[record_num].time_to_run = 0;
	dev->sriov.alias_guid.ports_guid[port_index].
		all_rec_per_port[record_num].
		guids_retry_schedule[index] = 0;
	do_work = 1;
unlock:
	spin_unlock_irqrestore(&dev->sriov.alias_guid.ag_work_lock, flags);

	if (do_work)
		mlx4_ib_init_alias_guid_work(dev, port_index);
}

/*
 * Whenever new GUID is set/unset (guid table change) create event and
 * notify the relevant slave (master also should be notified).
 * If the GUID value is not as we have in the cache the slave will not be
 * updated; in this case it waits for the smp_snoop or the port management
 * event to call the function and to update the slave.
 * block_number - the index of the block (16 blocks available)
 * port_number - 1 or 2
 */
void mlx4_ib_notify_slaves_on_guid_change(struct mlx4_ib_dev *dev,
					  int block_num, u8 port_num,
					  u8 *p_data)
{
	int i;
	u64 guid_indexes;
	int slave_id, slave_port;
	enum slave_port_state new_state;
	enum slave_port_state prev_state;
	__be64 tmp_cur_ag, form_cache_ag;
	enum slave_port_gen_event gen_event;
	struct mlx4_sriov_alias_guid_info_rec_det *rec;
	unsigned long flags;
	__be64 required_value;

	if (!mlx4_is_master(dev->dev))
		return;

	rec = &dev->sriov.alias_guid.ports_guid[port_num - 1].
			all_rec_per_port[block_num];
	guid_indexes = be64_to_cpu((__force __be64) dev->sriov.alias_guid.
				   ports_guid[port_num - 1].
				   all_rec_per_port[block_num].guid_indexes);
	pr_debug("port: %d, guid_indexes: 0x%llx\n", port_num, guid_indexes);

	/*calculate the slaves and notify them*/
	for (i = 0; i < NUM_ALIAS_GUID_IN_REC; i++) {
		/* the location of the specific index runs from bits 4..11 */
		if (!(test_bit(i + 4, (unsigned long *)&guid_indexes)))
			continue;

		slave_id = (block_num * NUM_ALIAS_GUID_IN_REC) + i ;
		if (slave_id >= dev->dev->persist->num_vfs + 1)
			return;

		slave_port = mlx4_phys_to_slave_port(dev->dev, slave_id, port_num);
		if (slave_port < 0) /* this port isn't available for the VF */
			continue;

		tmp_cur_ag = *(__be64 *)&p_data[i * GUID_REC_SIZE];
		form_cache_ag = get_cached_alias_guid(dev, port_num,
					(NUM_ALIAS_GUID_IN_REC * block_num) + i);
		/*
		 * Check if guid is not the same as in the cache,
		 * If it is different, wait for the snoop_smp or the port mgmt
		 * change event to update the slave on its port state change
		 */
		if (tmp_cur_ag != form_cache_ag)
			continue;

		spin_lock_irqsave(&dev->sriov.alias_guid.ag_work_lock, flags);
		required_value = *(__be64 *)&rec->all_recs[i * GUID_REC_SIZE];

		if (required_value == cpu_to_be64(MLX4_GUID_FOR_DELETE_VAL))
			required_value = 0;

		if (tmp_cur_ag == required_value) {
			rec->guid_indexes = rec->guid_indexes &
			       ~mlx4_ib_get_aguid_comp_mask_from_ix(i);
		} else {
			/* may notify port down if value is 0 */
			if (tmp_cur_ag != MLX4_NOT_SET_GUID) {
				spin_unlock_irqrestore(&dev->sriov.
					alias_guid.ag_work_lock, flags);
				continue;
			}
		}
		spin_unlock_irqrestore(&dev->sriov.alias_guid.ag_work_lock,
				       flags);
		mlx4_gen_guid_change_eqe(dev->dev, slave_id, port_num);
		/*2 cases: Valid GUID, and Invalid Guid*/

		if (tmp_cur_ag != MLX4_NOT_SET_GUID) { /*valid GUID*/
			prev_state = mlx4_get_slave_port_state(dev->dev, slave_id, port_num);
			new_state = set_and_calc_slave_port_state(dev->dev, slave_id, port_num,
								  MLX4_PORT_STATE_IB_PORT_STATE_EVENT_GID_VALID,
								  &gen_event);
			pr_debug("slave: %d, port: %d prev_port_state: %d,"
				 " new_port_state: %d, gen_event: %d\n",
				 slave_id, port_num, prev_state, new_state, gen_event);
			if (gen_event == SLAVE_PORT_GEN_EVENT_UP) {
				pr_debug("sending PORT_UP event to slave: %d, port: %d\n",
					 slave_id, port_num);
				mlx4_gen_port_state_change_eqe(dev->dev, slave_id,
							       port_num, MLX4_PORT_CHANGE_SUBTYPE_ACTIVE);
			}
		} else { /* request to invalidate GUID */
			set_and_calc_slave_port_state(dev->dev, slave_id, port_num,
						      MLX4_PORT_STATE_IB_EVENT_GID_INVALID,
						      &gen_event);
			if (gen_event == SLAVE_PORT_GEN_EVENT_DOWN) {
				pr_debug("sending PORT DOWN event to slave: %d, port: %d\n",
					 slave_id, port_num);
				mlx4_gen_port_state_change_eqe(dev->dev,
							       slave_id,
							       port_num,
							       MLX4_PORT_CHANGE_SUBTYPE_DOWN);
			}
		}
	}
}

static void aliasguid_query_handler(int status,
				    struct ib_sa_guidinfo_rec *guid_rec,
				    void *context)
{
	struct mlx4_ib_dev *dev;
	struct mlx4_alias_guid_work_context *cb_ctx = context;
	u8 port_index ;
	int i;
	struct mlx4_sriov_alias_guid_info_rec_det *rec;
	unsigned long flags, flags1;
	ib_sa_comp_mask declined_guid_indexes = 0;
	ib_sa_comp_mask applied_guid_indexes = 0;
	unsigned int resched_delay_sec = 0;

	if (!context)
		return;

	dev = cb_ctx->dev;
	port_index = cb_ctx->port - 1;
	rec = &dev->sriov.alias_guid.ports_guid[port_index].
		all_rec_per_port[cb_ctx->block_num];

	if (status) {
		pr_debug("(port: %d) failed: status = %d\n",
			 cb_ctx->port, status);
		rec->time_to_run = ktime_get_real_ns() + 1 * NSEC_PER_SEC;
		goto out;
	}

	if (guid_rec->block_num != cb_ctx->block_num) {
		pr_err("block num mismatch: %d != %d\n",
		       cb_ctx->block_num, guid_rec->block_num);
		goto out;
	}

	pr_debug("lid/port: %d/%d, block_num: %d\n",
		 be16_to_cpu(guid_rec->lid), cb_ctx->port,
		 guid_rec->block_num);

	rec = &dev->sriov.alias_guid.ports_guid[port_index].
		all_rec_per_port[guid_rec->block_num];

	spin_lock_irqsave(&dev->sriov.alias_guid.ag_work_lock, flags);
	for (i = 0 ; i < NUM_ALIAS_GUID_IN_REC; i++) {
		__be64 sm_response, required_val;

		if (!(cb_ctx->guid_indexes &
			mlx4_ib_get_aguid_comp_mask_from_ix(i)))
			continue;
		sm_response = *(__be64 *)&guid_rec->guid_info_list
				[i * GUID_REC_SIZE];
		required_val = *(__be64 *)&rec->all_recs[i * GUID_REC_SIZE];
		if (cb_ctx->method == MLX4_GUID_INFO_RECORD_DELETE) {
			if (required_val ==
			    cpu_to_be64(MLX4_GUID_FOR_DELETE_VAL))
				goto next_entry;

			/* A new value was set till we got the response */
			pr_debug("need to set new value %llx, record num %d, block_num:%d\n",
				 be64_to_cpu(required_val),
				 i, guid_rec->block_num);
			goto entry_declined;
		}

		/* check if the SM didn't assign one of the records.
		 * if it didn't, re-ask for.
		 */
		if (sm_response == MLX4_NOT_SET_GUID) {
			if (rec->guids_retry_schedule[i] == 0)
				mlx4_ib_warn(&dev->ib_dev,
					     "%s:Record num %d in  block_num: %d was declined by SM\n",
					     __func__, i,
					     guid_rec->block_num);
			goto entry_declined;
		} else {
		       /* properly assigned record. */
		       /* We save the GUID we just got from the SM in the
			* admin_guid in order to be persistent, and in the
			* request from the sm the process will ask for the same GUID */
			if (required_val &&
			    sm_response != required_val) {
				/* Warn only on first retry */
				if (rec->guids_retry_schedule[i] == 0)
					mlx4_ib_warn(&dev->ib_dev, "%s: Failed to set"
						     " admin guid after SysAdmin "
						     "configuration. "
						     "Record num %d in block_num:%d "
						     "was declined by SM, "
						     "new val(0x%llx) was kept, SM returned (0x%llx)\n",
						      __func__, i,
						     guid_rec->block_num,
						     be64_to_cpu(required_val),
						     be64_to_cpu(sm_response));
				goto entry_declined;
			} else {
				*(__be64 *)&rec->all_recs[i * GUID_REC_SIZE] =
					sm_response;
				if (required_val == 0)
					mlx4_set_admin_guid(dev->dev,
							    sm_response,
							    (guid_rec->block_num
							    * NUM_ALIAS_GUID_IN_REC) + i,
							    cb_ctx->port);
				goto next_entry;
			}
		}
entry_declined:
		declined_guid_indexes |= mlx4_ib_get_aguid_comp_mask_from_ix(i);
		rec->guids_retry_schedule[i] =
			(rec->guids_retry_schedule[i] == 0) ?  1 :
			min((unsigned int)60,
			    rec->guids_retry_schedule[i] * 2);
		/* using the minimum value among all entries in that record */
		resched_delay_sec = (resched_delay_sec == 0) ?
				rec->guids_retry_schedule[i] :
				min(resched_delay_sec,
				    rec->guids_retry_schedule[i]);
		continue;

next_entry:
		rec->guids_retry_schedule[i] = 0;
	}

	applied_guid_indexes =  cb_ctx->guid_indexes & ~declined_guid_indexes;
	if (declined_guid_indexes ||
	    rec->guid_indexes & ~(applied_guid_indexes)) {
		pr_debug("record=%d wasn't fully set, guid_indexes=0x%llx applied_indexes=0x%llx, declined_indexes=0x%llx\n",
			 guid_rec->block_num,
			 be64_to_cpu((__force __be64)rec->guid_indexes),
			 be64_to_cpu((__force __be64)applied_guid_indexes),
			 be64_to_cpu((__force __be64)declined_guid_indexes));
		rec->time_to_run = ktime_get_real_ns() +
			resched_delay_sec * NSEC_PER_SEC;
	} else {
		rec->status = MLX4_GUID_INFO_STATUS_SET;
	}
	spin_unlock_irqrestore(&dev->sriov.alias_guid.ag_work_lock, flags);
	/*
	The func is call here to close the cases when the
	sm doesn't send smp, so in the sa response the driver
	notifies the slave.
	*/
	mlx4_ib_notify_slaves_on_guid_change(dev, guid_rec->block_num,
					     cb_ctx->port,
					     guid_rec->guid_info_list);
out:
	spin_lock_irqsave(&dev->sriov.going_down_lock, flags);
	spin_lock_irqsave(&dev->sriov.alias_guid.ag_work_lock, flags1);
	if (!dev->sriov.is_going_down) {
		get_low_record_time_index(dev, port_index, &resched_delay_sec);
		queue_delayed_work(dev->sriov.alias_guid.ports_guid[port_index].wq,
				   &dev->sriov.alias_guid.ports_guid[port_index].
				   alias_guid_work,
				   msecs_to_jiffies(resched_delay_sec * 1000));
	}
	if (cb_ctx->sa_query) {
		list_del(&cb_ctx->list);
		kfree(cb_ctx);
	} else
		complete(&cb_ctx->done);
	spin_unlock_irqrestore(&dev->sriov.alias_guid.ag_work_lock, flags1);
	spin_unlock_irqrestore(&dev->sriov.going_down_lock, flags);
}

static void invalidate_guid_record(struct mlx4_ib_dev *dev, u8 port, int index)
{
	int i;
	u64 cur_admin_val;
	ib_sa_comp_mask comp_mask = 0;

	dev->sriov.alias_guid.ports_guid[port - 1].all_rec_per_port[index].status
		= MLX4_GUID_INFO_STATUS_SET;

	/* calculate the comp_mask for that record.*/
	for (i = 0; i < NUM_ALIAS_GUID_IN_REC; i++) {
		cur_admin_val =
			*(u64 *)&dev->sriov.alias_guid.ports_guid[port - 1].
			all_rec_per_port[index].all_recs[GUID_REC_SIZE * i];
		/*
		check the admin value: if it's for delete (~00LL) or
		it is the first guid of the first record (hw guid) or
		the records is not in ownership of the sysadmin and the sm doesn't
		need to assign GUIDs, then don't put it up for assignment.
		*/
		if (MLX4_GUID_FOR_DELETE_VAL == cur_admin_val ||
		    (!index && !i))
			continue;
		comp_mask |= mlx4_ib_get_aguid_comp_mask_from_ix(i);
	}
	dev->sriov.alias_guid.ports_guid[port - 1].
		all_rec_per_port[index].guid_indexes |= comp_mask;
	if (dev->sriov.alias_guid.ports_guid[port - 1].
	    all_rec_per_port[index].guid_indexes)
		dev->sriov.alias_guid.ports_guid[port - 1].
		all_rec_per_port[index].status = MLX4_GUID_INFO_STATUS_IDLE;

}

static int set_guid_rec(struct ib_device *ibdev,
			struct mlx4_next_alias_guid_work *rec)
{
	int err;
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct ib_sa_guidinfo_rec guid_info_rec;
	ib_sa_comp_mask comp_mask;
	struct ib_port_attr attr;
	struct mlx4_alias_guid_work_context *callback_context;
	unsigned long resched_delay, flags, flags1;
	u8 port = rec->port + 1;
	int index = rec->block_num;
	struct mlx4_sriov_alias_guid_info_rec_det *rec_det = &rec->rec_det;
	struct list_head *head =
		&dev->sriov.alias_guid.ports_guid[port - 1].cb_list;

	err = __mlx4_ib_query_port(ibdev, port, &attr, 1);
	if (err) {
		pr_debug("mlx4_ib_query_port failed (err: %d), port: %d\n",
			 err, port);
		return err;
	}
	/*check the port was configured by the sm, otherwise no need to send */
	if (attr.state != IB_PORT_ACTIVE) {
		pr_debug("port %d not active...rescheduling\n", port);
		resched_delay = 5 * HZ;
		err = -EAGAIN;
		goto new_schedule;
	}

	callback_context = kmalloc(sizeof *callback_context, GFP_KERNEL);
	if (!callback_context) {
		err = -ENOMEM;
		resched_delay = HZ * 5;
		goto new_schedule;
	}
	callback_context->port = port;
	callback_context->dev = dev;
	callback_context->block_num = index;
	callback_context->guid_indexes = rec_det->guid_indexes;
	callback_context->method = rec->method;

	memset(&guid_info_rec, 0, sizeof (struct ib_sa_guidinfo_rec));

	guid_info_rec.lid = cpu_to_be16(attr.lid);
	guid_info_rec.block_num = index;

	memcpy(guid_info_rec.guid_info_list, rec_det->all_recs,
	       GUID_REC_SIZE * NUM_ALIAS_GUID_IN_REC);
	comp_mask = IB_SA_GUIDINFO_REC_LID | IB_SA_GUIDINFO_REC_BLOCK_NUM |
		rec_det->guid_indexes;

	init_completion(&callback_context->done);
	spin_lock_irqsave(&dev->sriov.alias_guid.ag_work_lock, flags1);
	list_add_tail(&callback_context->list, head);
	spin_unlock_irqrestore(&dev->sriov.alias_guid.ag_work_lock, flags1);

	callback_context->query_id =
		ib_sa_guid_info_rec_query(dev->sriov.alias_guid.sa_client,
					  ibdev, port, &guid_info_rec,
					  comp_mask, rec->method, 1000,
					  GFP_KERNEL, aliasguid_query_handler,
					  callback_context,
					  &callback_context->sa_query);
	if (callback_context->query_id < 0) {
		pr_debug("ib_sa_guid_info_rec_query failed, query_id: "
			 "%d. will reschedule to the next 1 sec.\n",
			 callback_context->query_id);
		spin_lock_irqsave(&dev->sriov.alias_guid.ag_work_lock, flags1);
		list_del(&callback_context->list);
		kfree(callback_context);
		spin_unlock_irqrestore(&dev->sriov.alias_guid.ag_work_lock, flags1);
		resched_delay = 1 * HZ;
		err = -EAGAIN;
		goto new_schedule;
	}
	err = 0;
	goto out;

new_schedule:
	spin_lock_irqsave(&dev->sriov.going_down_lock, flags);
	spin_lock_irqsave(&dev->sriov.alias_guid.ag_work_lock, flags1);
	invalidate_guid_record(dev, port, index);
	if (!dev->sriov.is_going_down) {
		queue_delayed_work(dev->sriov.alias_guid.ports_guid[port - 1].wq,
				   &dev->sriov.alias_guid.ports_guid[port - 1].alias_guid_work,
				   resched_delay);
	}
	spin_unlock_irqrestore(&dev->sriov.alias_guid.ag_work_lock, flags1);
	spin_unlock_irqrestore(&dev->sriov.going_down_lock, flags);

out:
	return err;
}

static void mlx4_ib_guid_port_init(struct mlx4_ib_dev *dev, int port)
{
	int j, k, entry;
	__be64 guid;

	/*Check if the SM doesn't need to assign the GUIDs*/
	for (j = 0; j < NUM_ALIAS_GUID_REC_IN_PORT; j++) {
		for (k = 0; k < NUM_ALIAS_GUID_IN_REC; k++) {
			entry = j * NUM_ALIAS_GUID_IN_REC + k;
			/* no request for the 0 entry (hw guid) */
			if (!entry || entry > dev->dev->persist->num_vfs ||
			    !mlx4_is_slave_active(dev->dev, entry))
				continue;
			guid = mlx4_get_admin_guid(dev->dev, entry, port);
			*(__be64 *)&dev->sriov.alias_guid.ports_guid[port - 1].
				all_rec_per_port[j].all_recs
				[GUID_REC_SIZE * k] = guid;
			pr_debug("guid was set, entry=%d, val=0x%llx, port=%d\n",
				 entry,
				 be64_to_cpu(guid),
				 port);
		}
	}
}
void mlx4_ib_invalidate_all_guid_record(struct mlx4_ib_dev *dev, int port)
{
	int i;
	unsigned long flags, flags1;

	pr_debug("port %d\n", port);

	spin_lock_irqsave(&dev->sriov.going_down_lock, flags);
	spin_lock_irqsave(&dev->sriov.alias_guid.ag_work_lock, flags1);

	if (dev->sriov.alias_guid.ports_guid[port - 1].state_flags &
		GUID_STATE_NEED_PORT_INIT) {
		mlx4_ib_guid_port_init(dev, port);
		dev->sriov.alias_guid.ports_guid[port - 1].state_flags &=
			(~GUID_STATE_NEED_PORT_INIT);
	}
	for (i = 0; i < NUM_ALIAS_GUID_REC_IN_PORT; i++)
		invalidate_guid_record(dev, port, i);

	if (mlx4_is_master(dev->dev) && !dev->sriov.is_going_down) {
		/*
		make sure no work waits in the queue, if the work is already
		queued(not on the timer) the cancel will fail. That is not a problem
		because we just want the work started.
		*/
		cancel_delayed_work(&dev->sriov.alias_guid.
				      ports_guid[port - 1].alias_guid_work);
		queue_delayed_work(dev->sriov.alias_guid.ports_guid[port - 1].wq,
				   &dev->sriov.alias_guid.ports_guid[port - 1].alias_guid_work,
				   0);
	}
	spin_unlock_irqrestore(&dev->sriov.alias_guid.ag_work_lock, flags1);
	spin_unlock_irqrestore(&dev->sriov.going_down_lock, flags);
}

static void set_required_record(struct mlx4_ib_dev *dev, u8 port,
				struct mlx4_next_alias_guid_work *next_rec,
				int record_index)
{
	int i;
	int lowset_time_entry = -1;
	int lowest_time = 0;
	ib_sa_comp_mask delete_guid_indexes = 0;
	ib_sa_comp_mask set_guid_indexes = 0;
	struct mlx4_sriov_alias_guid_info_rec_det *rec =
			&dev->sriov.alias_guid.ports_guid[port].
			all_rec_per_port[record_index];

	for (i = 0; i < NUM_ALIAS_GUID_IN_REC; i++) {
		if (!(rec->guid_indexes &
			mlx4_ib_get_aguid_comp_mask_from_ix(i)))
			continue;

		if (*(__be64 *)&rec->all_recs[i * GUID_REC_SIZE] ==
				cpu_to_be64(MLX4_GUID_FOR_DELETE_VAL))
			delete_guid_indexes |=
				mlx4_ib_get_aguid_comp_mask_from_ix(i);
		else
			set_guid_indexes |=
				mlx4_ib_get_aguid_comp_mask_from_ix(i);

		if (lowset_time_entry == -1 || rec->guids_retry_schedule[i] <=
			lowest_time) {
			lowset_time_entry = i;
			lowest_time = rec->guids_retry_schedule[i];
		}
	}

	memcpy(&next_rec->rec_det, rec, sizeof(*rec));
	next_rec->port = port;
	next_rec->block_num = record_index;

	if (*(__be64 *)&rec->all_recs[lowset_time_entry * GUID_REC_SIZE] ==
				cpu_to_be64(MLX4_GUID_FOR_DELETE_VAL)) {
		next_rec->rec_det.guid_indexes = delete_guid_indexes;
		next_rec->method = MLX4_GUID_INFO_RECORD_DELETE;
	} else {
		next_rec->rec_det.guid_indexes = set_guid_indexes;
		next_rec->method = MLX4_GUID_INFO_RECORD_SET;
	}
}

/* return index of record that should be updated based on lowest
 * rescheduled time
 */
static int get_low_record_time_index(struct mlx4_ib_dev *dev, u8 port,
				     int *resched_delay_sec)
{
	int record_index = -1;
	u64 low_record_time = 0;
	struct mlx4_sriov_alias_guid_info_rec_det rec;
	int j;

	for (j = 0; j < NUM_ALIAS_GUID_REC_IN_PORT; j++) {
		rec = dev->sriov.alias_guid.ports_guid[port].
			all_rec_per_port[j];
		if (rec.status == MLX4_GUID_INFO_STATUS_IDLE &&
		    rec.guid_indexes) {
			if (record_index == -1 ||
			    rec.time_to_run < low_record_time) {
				record_index = j;
				low_record_time = rec.time_to_run;
			}
		}
	}
	if (resched_delay_sec) {
		u64 curr_time = ktime_get_real_ns();

		*resched_delay_sec = (low_record_time < curr_time) ? 0 :
			div_u64((low_record_time - curr_time), NSEC_PER_SEC);
	}

	return record_index;
}

/* The function returns the next record that was
 * not configured (or failed to be configured) */
static int get_next_record_to_update(struct mlx4_ib_dev *dev, u8 port,
				     struct mlx4_next_alias_guid_work *rec)
{
	unsigned long flags;
	int record_index;
	int ret = 0;

	spin_lock_irqsave(&dev->sriov.alias_guid.ag_work_lock, flags);
	record_index = get_low_record_time_index(dev, port, NULL);

	if (record_index < 0) {
		ret = -ENOENT;
		goto out;
	}

	set_required_record(dev, port, rec, record_index);
out:
	spin_unlock_irqrestore(&dev->sriov.alias_guid.ag_work_lock, flags);
	return ret;
}

static void alias_guid_work(struct work_struct *work)
{
	struct delayed_work *delay = to_delayed_work(work);
	int ret = 0;
	struct mlx4_next_alias_guid_work *rec;
	struct mlx4_sriov_alias_guid_port_rec_det *sriov_alias_port =
		container_of(delay, struct mlx4_sriov_alias_guid_port_rec_det,
			     alias_guid_work);
	struct mlx4_sriov_alias_guid *sriov_alias_guid = sriov_alias_port->parent;
	struct mlx4_ib_sriov *ib_sriov = container_of(sriov_alias_guid,
						struct mlx4_ib_sriov,
						alias_guid);
	struct mlx4_ib_dev *dev = container_of(ib_sriov, struct mlx4_ib_dev, sriov);

	rec = kzalloc(sizeof *rec, GFP_KERNEL);
	if (!rec) {
		pr_err("alias_guid_work: No Memory\n");
		return;
	}

	pr_debug("starting [port: %d]...\n", sriov_alias_port->port + 1);
	ret = get_next_record_to_update(dev, sriov_alias_port->port, rec);
	if (ret) {
		pr_debug("No more records to update.\n");
		goto out;
	}

	set_guid_rec(&dev->ib_dev, rec);
out:
	kfree(rec);
}


void mlx4_ib_init_alias_guid_work(struct mlx4_ib_dev *dev, int port)
{
	unsigned long flags, flags1;

	if (!mlx4_is_master(dev->dev))
		return;
	spin_lock_irqsave(&dev->sriov.going_down_lock, flags);
	spin_lock_irqsave(&dev->sriov.alias_guid.ag_work_lock, flags1);
	if (!dev->sriov.is_going_down) {
		/* If there is pending one should cancell then run, otherwise
		  * won't run till previous one is ended as same work
		  * struct is used.
		  */
		cancel_delayed_work(&dev->sriov.alias_guid.ports_guid[port].
				    alias_guid_work);
		queue_delayed_work(dev->sriov.alias_guid.ports_guid[port].wq,
			   &dev->sriov.alias_guid.ports_guid[port].alias_guid_work, 0);
	}
	spin_unlock_irqrestore(&dev->sriov.alias_guid.ag_work_lock, flags1);
	spin_unlock_irqrestore(&dev->sriov.going_down_lock, flags);
}

void mlx4_ib_destroy_alias_guid_service(struct mlx4_ib_dev *dev)
{
	int i;
	struct mlx4_ib_sriov *sriov = &dev->sriov;
	struct mlx4_alias_guid_work_context *cb_ctx;
	struct mlx4_sriov_alias_guid_port_rec_det *det;
	struct ib_sa_query *sa_query;
	unsigned long flags;

	for (i = 0 ; i < dev->num_ports; i++) {
		cancel_delayed_work(&dev->sriov.alias_guid.ports_guid[i].alias_guid_work);
		det = &sriov->alias_guid.ports_guid[i];
		spin_lock_irqsave(&sriov->alias_guid.ag_work_lock, flags);
		while (!list_empty(&det->cb_list)) {
			cb_ctx = list_entry(det->cb_list.next,
					    struct mlx4_alias_guid_work_context,
					    list);
			sa_query = cb_ctx->sa_query;
			cb_ctx->sa_query = NULL;
			list_del(&cb_ctx->list);
			spin_unlock_irqrestore(&sriov->alias_guid.ag_work_lock, flags);
			ib_sa_cancel_query(cb_ctx->query_id, sa_query);
			wait_for_completion(&cb_ctx->done);
			kfree(cb_ctx);
			spin_lock_irqsave(&sriov->alias_guid.ag_work_lock, flags);
		}
		spin_unlock_irqrestore(&sriov->alias_guid.ag_work_lock, flags);
	}
	for (i = 0 ; i < dev->num_ports; i++) {
		flush_workqueue(dev->sriov.alias_guid.ports_guid[i].wq);
		destroy_workqueue(dev->sriov.alias_guid.ports_guid[i].wq);
	}
	ib_sa_unregister_client(dev->sriov.alias_guid.sa_client);
	kfree(dev->sriov.alias_guid.sa_client);
}

int mlx4_ib_init_alias_guid_service(struct mlx4_ib_dev *dev)
{
	char alias_wq_name[15];
	int ret = 0;
	int i, j;
	union ib_gid gid;

	if (!mlx4_is_master(dev->dev))
		return 0;
	dev->sriov.alias_guid.sa_client =
		kzalloc(sizeof *dev->sriov.alias_guid.sa_client, GFP_KERNEL);
	if (!dev->sriov.alias_guid.sa_client)
		return -ENOMEM;

	ib_sa_register_client(dev->sriov.alias_guid.sa_client);

	spin_lock_init(&dev->sriov.alias_guid.ag_work_lock);

	for (i = 1; i <= dev->num_ports; ++i) {
		if (dev->ib_dev.query_gid(&dev->ib_dev , i, 0, &gid)) {
			ret = -EFAULT;
			goto err_unregister;
		}
	}

	for (i = 0 ; i < dev->num_ports; i++) {
		memset(&dev->sriov.alias_guid.ports_guid[i], 0,
		       sizeof (struct mlx4_sriov_alias_guid_port_rec_det));
		dev->sriov.alias_guid.ports_guid[i].state_flags |=
				GUID_STATE_NEED_PORT_INIT;
		for (j = 0; j < NUM_ALIAS_GUID_REC_IN_PORT; j++) {
			/* mark each val as it was deleted */
			memset(dev->sriov.alias_guid.ports_guid[i].
				all_rec_per_port[j].all_recs, 0xFF,
				sizeof(dev->sriov.alias_guid.ports_guid[i].
				all_rec_per_port[j].all_recs));
		}
		INIT_LIST_HEAD(&dev->sriov.alias_guid.ports_guid[i].cb_list);
		/*prepare the records, set them to be allocated by sm*/
		if (mlx4_ib_sm_guid_assign)
			for (j = 1; j < NUM_ALIAS_GUID_PER_PORT; j++)
				mlx4_set_admin_guid(dev->dev, 0, j, i + 1);
		for (j = 0 ; j < NUM_ALIAS_GUID_REC_IN_PORT; j++)
			invalidate_guid_record(dev, i + 1, j);

		dev->sriov.alias_guid.ports_guid[i].parent = &dev->sriov.alias_guid;
		dev->sriov.alias_guid.ports_guid[i].port  = i;

		snprintf(alias_wq_name, sizeof alias_wq_name, "alias_guid%d", i);
		dev->sriov.alias_guid.ports_guid[i].wq =
			create_singlethread_workqueue(alias_wq_name);
		if (!dev->sriov.alias_guid.ports_guid[i].wq) {
			ret = -ENOMEM;
			goto err_thread;
		}
		INIT_DELAYED_WORK(&dev->sriov.alias_guid.ports_guid[i].alias_guid_work,
			  alias_guid_work);
	}
	return 0;

err_thread:
	for (--i; i >= 0; i--) {
		destroy_workqueue(dev->sriov.alias_guid.ports_guid[i].wq);
		dev->sriov.alias_guid.ports_guid[i].wq = NULL;
	}

err_unregister:
	ib_sa_unregister_client(dev->sriov.alias_guid.sa_client);
	kfree(dev->sriov.alias_guid.sa_client);
	dev->sriov.alias_guid.sa_client = NULL;
	pr_err("init_alias_guid_service: Failed. (ret:%d)\n", ret);
	return ret;
}
