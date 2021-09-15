// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include <linux/delay.h>

#include "iosm_ipc_chnl_cfg.h"
#include "iosm_ipc_imem.h"
#include "iosm_ipc_port.h"

/* Check the wwan ips if it is valid with Channel as input. */
static int ipc_imem_check_wwan_ips(struct ipc_mem_channel *chnl)
{
	if (chnl)
		return chnl->ctype == IPC_CTYPE_WWAN &&
		       chnl->if_id == IPC_MEM_MUX_IP_CH_IF_ID;
	return false;
}

static int ipc_imem_msg_send_device_sleep(struct iosm_imem *ipc_imem, u32 state)
{
	union ipc_msg_prep_args prep_args = {
		.sleep.target = 1,
		.sleep.state = state,
	};

	ipc_imem->device_sleep = state;

	return ipc_protocol_tq_msg_send(ipc_imem->ipc_protocol,
					IPC_MSG_PREP_SLEEP, &prep_args, NULL);
}

static bool ipc_imem_dl_skb_alloc(struct iosm_imem *ipc_imem,
				  struct ipc_pipe *pipe)
{
	/* limit max. nr of entries */
	if (pipe->nr_of_queued_entries >= pipe->max_nr_of_queued_entries)
		return false;

	return ipc_protocol_dl_td_prepare(ipc_imem->ipc_protocol, pipe);
}

/* This timer handler will retry DL buff allocation if a pipe has no free buf
 * and gives doorbell if TD is available
 */
static int ipc_imem_tq_td_alloc_timer(struct iosm_imem *ipc_imem, int arg,
				      void *msg, size_t size)
{
	bool new_buffers_available = false;
	bool retry_allocation = false;
	int i;

	for (i = 0; i < IPC_MEM_MAX_CHANNELS; i++) {
		struct ipc_pipe *pipe = &ipc_imem->channels[i].dl_pipe;

		if (!pipe->is_open || pipe->nr_of_queued_entries > 0)
			continue;

		while (ipc_imem_dl_skb_alloc(ipc_imem, pipe))
			new_buffers_available = true;

		if (pipe->nr_of_queued_entries == 0)
			retry_allocation = true;
	}

	if (new_buffers_available)
		ipc_protocol_doorbell_trigger(ipc_imem->ipc_protocol,
					      IPC_HP_DL_PROCESS);

	if (retry_allocation) {
		ipc_imem->hrtimer_period =
		ktime_set(0, IPC_TD_ALLOC_TIMER_PERIOD_MS * 1000 * 1000ULL);
		if (!hrtimer_active(&ipc_imem->td_alloc_timer))
			hrtimer_start(&ipc_imem->td_alloc_timer,
				      ipc_imem->hrtimer_period,
				      HRTIMER_MODE_REL);
	}
	return 0;
}

static enum hrtimer_restart ipc_imem_td_alloc_timer_cb(struct hrtimer *hr_timer)
{
	struct iosm_imem *ipc_imem =
		container_of(hr_timer, struct iosm_imem, td_alloc_timer);
	/* Post an async tasklet event to trigger HP update Doorbell */
	ipc_task_queue_send_task(ipc_imem, ipc_imem_tq_td_alloc_timer, 0, NULL,
				 0, false);
	return HRTIMER_NORESTART;
}

/* Fast update timer tasklet handler to trigger HP update */
static int ipc_imem_tq_fast_update_timer_cb(struct iosm_imem *ipc_imem, int arg,
					    void *msg, size_t size)
{
	ipc_protocol_doorbell_trigger(ipc_imem->ipc_protocol,
				      IPC_HP_FAST_TD_UPD_TMR);

	return 0;
}

static enum hrtimer_restart
ipc_imem_fast_update_timer_cb(struct hrtimer *hr_timer)
{
	struct iosm_imem *ipc_imem =
		container_of(hr_timer, struct iosm_imem, fast_update_timer);
	/* Post an async tasklet event to trigger HP update Doorbell */
	ipc_task_queue_send_task(ipc_imem, ipc_imem_tq_fast_update_timer_cb, 0,
				 NULL, 0, false);
	return HRTIMER_NORESTART;
}

static int ipc_imem_setup_cp_mux_cap_init(struct iosm_imem *ipc_imem,
					  struct ipc_mux_config *cfg)
{
	ipc_mmio_update_cp_capability(ipc_imem->mmio);

	if (!ipc_imem->mmio->has_mux_lite) {
		dev_err(ipc_imem->dev, "Failed to get Mux capability.");
		return -EINVAL;
	}

	cfg->protocol = MUX_LITE;

	cfg->ul_flow = (ipc_imem->mmio->has_ul_flow_credit == 1) ?
			       MUX_UL_ON_CREDITS :
			       MUX_UL;

	/* The instance ID is same as channel ID because this is been reused
	 * for channel alloc function.
	 */
	cfg->instance_id = IPC_MEM_MUX_IP_CH_IF_ID;
	cfg->nr_sessions = IPC_MEM_MUX_IP_SESSION_ENTRIES;

	return 0;
}

void ipc_imem_msg_send_feature_set(struct iosm_imem *ipc_imem,
				   unsigned int reset_enable, bool atomic_ctx)
{
	union ipc_msg_prep_args prep_args = { .feature_set.reset_enable =
						      reset_enable };

	if (atomic_ctx)
		ipc_protocol_tq_msg_send(ipc_imem->ipc_protocol,
					 IPC_MSG_PREP_FEATURE_SET, &prep_args,
					 NULL);
	else
		ipc_protocol_msg_send(ipc_imem->ipc_protocol,
				      IPC_MSG_PREP_FEATURE_SET, &prep_args);
}

void ipc_imem_td_update_timer_start(struct iosm_imem *ipc_imem)
{
	/* Use the TD update timer only in the runtime phase */
	if (!ipc_imem->enter_runtime || ipc_imem->td_update_timer_suspended) {
		/* trigger the doorbell irq on CP directly. */
		ipc_protocol_doorbell_trigger(ipc_imem->ipc_protocol,
					      IPC_HP_TD_UPD_TMR_START);
		return;
	}

	if (!hrtimer_active(&ipc_imem->tdupdate_timer)) {
		ipc_imem->hrtimer_period =
		ktime_set(0, TD_UPDATE_DEFAULT_TIMEOUT_USEC * 1000ULL);
		if (!hrtimer_active(&ipc_imem->tdupdate_timer))
			hrtimer_start(&ipc_imem->tdupdate_timer,
				      ipc_imem->hrtimer_period,
				      HRTIMER_MODE_REL);
	}
}

void ipc_imem_hrtimer_stop(struct hrtimer *hr_timer)
{
	if (hrtimer_active(hr_timer))
		hrtimer_cancel(hr_timer);
}

bool ipc_imem_ul_write_td(struct iosm_imem *ipc_imem)
{
	struct ipc_mem_channel *channel;
	struct sk_buff_head *ul_list;
	bool hpda_pending = false;
	bool forced_hpdu = false;
	struct ipc_pipe *pipe;
	int i;

	/* Analyze the uplink pipe of all active channels. */
	for (i = 0; i < ipc_imem->nr_of_channels; i++) {
		channel = &ipc_imem->channels[i];

		if (channel->state != IMEM_CHANNEL_ACTIVE)
			continue;

		pipe = &channel->ul_pipe;

		/* Get the reference to the skbuf accumulator list. */
		ul_list = &channel->ul_list;

		/* Fill the transfer descriptor with the uplink buffer info. */
		hpda_pending |= ipc_protocol_ul_td_send(ipc_imem->ipc_protocol,
							pipe, ul_list);

		/* forced HP update needed for non data channels */
		if (hpda_pending && !ipc_imem_check_wwan_ips(channel))
			forced_hpdu = true;
	}

	if (forced_hpdu) {
		hpda_pending = false;
		ipc_protocol_doorbell_trigger(ipc_imem->ipc_protocol,
					      IPC_HP_UL_WRITE_TD);
	}

	return hpda_pending;
}

void ipc_imem_ipc_init_check(struct iosm_imem *ipc_imem)
{
	int timeout = IPC_MODEM_BOOT_TIMEOUT;

	ipc_imem->ipc_requested_state = IPC_MEM_DEVICE_IPC_INIT;

	/* Trigger the CP interrupt to enter the init state. */
	ipc_doorbell_fire(ipc_imem->pcie, IPC_DOORBELL_IRQ_IPC,
			  IPC_MEM_DEVICE_IPC_INIT);
	/* Wait for the CP update. */
	do {
		if (ipc_mmio_get_ipc_state(ipc_imem->mmio) ==
		    ipc_imem->ipc_requested_state) {
			/* Prepare the MMIO space */
			ipc_mmio_config(ipc_imem->mmio);

			/* Trigger the CP irq to enter the running state. */
			ipc_imem->ipc_requested_state =
				IPC_MEM_DEVICE_IPC_RUNNING;
			ipc_doorbell_fire(ipc_imem->pcie, IPC_DOORBELL_IRQ_IPC,
					  IPC_MEM_DEVICE_IPC_RUNNING);

			return;
		}
		msleep(20);
	} while (--timeout);

	/* timeout */
	dev_err(ipc_imem->dev, "%s: ipc_status(%d) ne. IPC_MEM_DEVICE_IPC_INIT",
		ipc_imem_phase_get_string(ipc_imem->phase),
		ipc_mmio_get_ipc_state(ipc_imem->mmio));

	ipc_uevent_send(ipc_imem->dev, UEVENT_MDM_TIMEOUT);
}

/* Analyze the packet type and distribute it. */
static void ipc_imem_dl_skb_process(struct iosm_imem *ipc_imem,
				    struct ipc_pipe *pipe, struct sk_buff *skb)
{
	u16 port_id;

	if (!skb)
		return;

	/* An AT/control or IP packet is expected. */
	switch (pipe->channel->ctype) {
	case IPC_CTYPE_CTRL:
		port_id = pipe->channel->channel_id;

		/* Pass the packet to the wwan layer. */
		wwan_port_rx(ipc_imem->ipc_port[port_id]->iosm_port, skb);
		break;

	case IPC_CTYPE_WWAN:
		if (pipe->channel->if_id == IPC_MEM_MUX_IP_CH_IF_ID)
			ipc_mux_dl_decode(ipc_imem->mux, skb);
		break;
	default:
		dev_err(ipc_imem->dev, "Invalid channel type");
		break;
	}
}

/* Process the downlink data and pass them to the char or net layer. */
static void ipc_imem_dl_pipe_process(struct iosm_imem *ipc_imem,
				     struct ipc_pipe *pipe)
{
	s32 cnt = 0, processed_td_cnt = 0;
	struct ipc_mem_channel *channel;
	u32 head = 0, tail = 0;
	bool processed = false;
	struct sk_buff *skb;

	channel = pipe->channel;

	ipc_protocol_get_head_tail_index(ipc_imem->ipc_protocol, pipe, &head,
					 &tail);
	if (pipe->old_tail != tail) {
		if (pipe->old_tail < tail)
			cnt = tail - pipe->old_tail;
		else
			cnt = pipe->nr_of_entries - pipe->old_tail + tail;
	}

	processed_td_cnt = cnt;

	/* Seek for pipes with pending DL data. */
	while (cnt--) {
		skb = ipc_protocol_dl_td_process(ipc_imem->ipc_protocol, pipe);

		/* Analyze the packet type and distribute it. */
		ipc_imem_dl_skb_process(ipc_imem, pipe, skb);
	}

	/* try to allocate new empty DL SKbs from head..tail - 1*/
	while (ipc_imem_dl_skb_alloc(ipc_imem, pipe))
		processed = true;

	if (processed && !ipc_imem_check_wwan_ips(channel)) {
		/* Force HP update for non IP channels */
		ipc_protocol_doorbell_trigger(ipc_imem->ipc_protocol,
					      IPC_HP_DL_PROCESS);
		processed = false;

		/* If Fast Update timer is already running then stop */
		ipc_imem_hrtimer_stop(&ipc_imem->fast_update_timer);
	}

	/* Any control channel process will get immediate HP update.
	 * Start Fast update timer only for IP channel if all the TDs were
	 * used in last process.
	 */
	if (processed && (processed_td_cnt == pipe->nr_of_entries - 1)) {
		ipc_imem->hrtimer_period =
		ktime_set(0, FORCE_UPDATE_DEFAULT_TIMEOUT_USEC * 1000ULL);
		hrtimer_start(&ipc_imem->fast_update_timer,
			      ipc_imem->hrtimer_period, HRTIMER_MODE_REL);
	}

	if (ipc_imem->app_notify_dl_pend)
		complete(&ipc_imem->dl_pend_sem);
}

/* process open uplink pipe */
static void ipc_imem_ul_pipe_process(struct iosm_imem *ipc_imem,
				     struct ipc_pipe *pipe)
{
	struct ipc_mem_channel *channel;
	u32 tail = 0, head = 0;
	struct sk_buff *skb;
	s32 cnt = 0;

	channel = pipe->channel;

	/* Get the internal phase. */
	ipc_protocol_get_head_tail_index(ipc_imem->ipc_protocol, pipe, &head,
					 &tail);

	if (pipe->old_tail != tail) {
		if (pipe->old_tail < tail)
			cnt = tail - pipe->old_tail;
		else
			cnt = pipe->nr_of_entries - pipe->old_tail + tail;
	}

	/* Free UL buffers. */
	while (cnt--) {
		skb = ipc_protocol_ul_td_process(ipc_imem->ipc_protocol, pipe);

		if (!skb)
			continue;

		/* If the user app was suspended in uplink direction - blocking
		 * write, resume it.
		 */
		if (IPC_CB(skb)->op_type == UL_USR_OP_BLOCKED)
			complete(&channel->ul_sem);

		/* Free the skbuf element. */
		if (IPC_CB(skb)->op_type == UL_MUX_OP_ADB) {
			if (channel->if_id == IPC_MEM_MUX_IP_CH_IF_ID)
				ipc_mux_ul_encoded_process(ipc_imem->mux, skb);
			else
				dev_err(ipc_imem->dev,
					"OP Type is UL_MUX, unknown if_id %d",
					channel->if_id);
		} else {
			ipc_pcie_kfree_skb(ipc_imem->pcie, skb);
		}
	}

	/* Trace channel stats for IP UL pipe. */
	if (ipc_imem_check_wwan_ips(pipe->channel))
		ipc_mux_check_n_restart_tx(ipc_imem->mux);

	if (ipc_imem->app_notify_ul_pend)
		complete(&ipc_imem->ul_pend_sem);
}

/* Executes the irq. */
static void ipc_imem_rom_irq_exec(struct iosm_imem *ipc_imem)
{
	struct ipc_mem_channel *channel;

	if (ipc_imem->flash_channel_id < 0) {
		ipc_imem->rom_exit_code = IMEM_ROM_EXIT_FAIL;
		dev_err(ipc_imem->dev, "Missing flash app:%d",
			ipc_imem->flash_channel_id);
		return;
	}

	ipc_imem->rom_exit_code = ipc_mmio_get_rom_exit_code(ipc_imem->mmio);

	/* Wake up the flash app to continue or to terminate depending
	 * on the CP ROM exit code.
	 */
	channel = &ipc_imem->channels[ipc_imem->flash_channel_id];
	complete(&channel->ul_sem);
}

/* Execute the UL bundle timer actions, generating the doorbell irq. */
static int ipc_imem_tq_td_update_timer_cb(struct iosm_imem *ipc_imem, int arg,
					  void *msg, size_t size)
{
	ipc_protocol_doorbell_trigger(ipc_imem->ipc_protocol,
				      IPC_HP_TD_UPD_TMR);
	return 0;
}

/* Consider link power management in the runtime phase. */
static void ipc_imem_slp_control_exec(struct iosm_imem *ipc_imem)
{
	    /* link will go down, Test pending UL packets.*/
	if (ipc_protocol_pm_dev_sleep_handle(ipc_imem->ipc_protocol) &&
	    hrtimer_active(&ipc_imem->tdupdate_timer)) {
		/* Generate the doorbell irq. */
		ipc_imem_tq_td_update_timer_cb(ipc_imem, 0, NULL, 0);
		/* Stop the TD update timer. */
		ipc_imem_hrtimer_stop(&ipc_imem->tdupdate_timer);
		/* Stop the fast update timer. */
		ipc_imem_hrtimer_stop(&ipc_imem->fast_update_timer);
	}
}

/* Execute startup timer and wait for delayed start (e.g. NAND) */
static int ipc_imem_tq_startup_timer_cb(struct iosm_imem *ipc_imem, int arg,
					void *msg, size_t size)
{
	/* Update & check the current operation phase. */
	if (ipc_imem_phase_update(ipc_imem) != IPC_P_RUN)
		return -EIO;

	if (ipc_mmio_get_ipc_state(ipc_imem->mmio) ==
	    IPC_MEM_DEVICE_IPC_UNINIT) {
		ipc_imem->ipc_requested_state = IPC_MEM_DEVICE_IPC_INIT;

		ipc_doorbell_fire(ipc_imem->pcie, IPC_DOORBELL_IRQ_IPC,
				  IPC_MEM_DEVICE_IPC_INIT);

		ipc_imem->hrtimer_period = ktime_set(0, 100 * 1000UL * 1000ULL);
		/* reduce period to 100 ms to check for mmio init state */
		if (!hrtimer_active(&ipc_imem->startup_timer))
			hrtimer_start(&ipc_imem->startup_timer,
				      ipc_imem->hrtimer_period,
				      HRTIMER_MODE_REL);
	} else if (ipc_mmio_get_ipc_state(ipc_imem->mmio) ==
		   IPC_MEM_DEVICE_IPC_INIT) {
		/* Startup complete  - disable timer */
		ipc_imem_hrtimer_stop(&ipc_imem->startup_timer);

		/* Prepare the MMIO space */
		ipc_mmio_config(ipc_imem->mmio);
		ipc_imem->ipc_requested_state = IPC_MEM_DEVICE_IPC_RUNNING;
		ipc_doorbell_fire(ipc_imem->pcie, IPC_DOORBELL_IRQ_IPC,
				  IPC_MEM_DEVICE_IPC_RUNNING);
	}

	return 0;
}

static enum hrtimer_restart ipc_imem_startup_timer_cb(struct hrtimer *hr_timer)
{
	enum hrtimer_restart result = HRTIMER_NORESTART;
	struct iosm_imem *ipc_imem =
		container_of(hr_timer, struct iosm_imem, startup_timer);

	if (ktime_to_ns(ipc_imem->hrtimer_period)) {
		hrtimer_forward(&ipc_imem->startup_timer, ktime_get(),
				ipc_imem->hrtimer_period);
		result = HRTIMER_RESTART;
	}

	ipc_task_queue_send_task(ipc_imem, ipc_imem_tq_startup_timer_cb, 0,
				 NULL, 0, false);
	return result;
}

/* Get the CP execution stage */
static enum ipc_mem_exec_stage
ipc_imem_get_exec_stage_buffered(struct iosm_imem *ipc_imem)
{
	return (ipc_imem->phase == IPC_P_RUN &&
		ipc_imem->ipc_status == IPC_MEM_DEVICE_IPC_RUNNING) ?
		       ipc_protocol_get_ap_exec_stage(ipc_imem->ipc_protocol) :
		       ipc_mmio_get_exec_stage(ipc_imem->mmio);
}

/* Callback to send the modem ready uevent */
static int ipc_imem_send_mdm_rdy_cb(struct iosm_imem *ipc_imem, int arg,
				    void *msg, size_t size)
{
	enum ipc_mem_exec_stage exec_stage =
		ipc_imem_get_exec_stage_buffered(ipc_imem);

	if (exec_stage == IPC_MEM_EXEC_STAGE_RUN)
		ipc_uevent_send(ipc_imem->dev, UEVENT_MDM_READY);

	return 0;
}

/* This function is executed in a task context via an ipc_worker object,
 * as the creation or removal of device can't be done from tasklet.
 */
static void ipc_imem_run_state_worker(struct work_struct *instance)
{
	struct ipc_chnl_cfg chnl_cfg_port = { 0 };
	struct ipc_mux_config mux_cfg;
	struct iosm_imem *ipc_imem;
	u8 ctrl_chl_idx = 0;

	ipc_imem = container_of(instance, struct iosm_imem, run_state_worker);

	if (ipc_imem->phase != IPC_P_RUN) {
		dev_err(ipc_imem->dev,
			"Modem link down. Exit run state worker.");
		return;
	}

	if (!ipc_imem_setup_cp_mux_cap_init(ipc_imem, &mux_cfg))
		ipc_imem->mux = ipc_mux_init(&mux_cfg, ipc_imem);

	ipc_imem_wwan_channel_init(ipc_imem, mux_cfg.protocol);
	if (ipc_imem->mux)
		ipc_imem->mux->wwan = ipc_imem->wwan;

	while (ctrl_chl_idx < IPC_MEM_MAX_CHANNELS) {
		if (!ipc_chnl_cfg_get(&chnl_cfg_port, ctrl_chl_idx)) {
			ipc_imem->ipc_port[ctrl_chl_idx] = NULL;
			if (chnl_cfg_port.wwan_port_type != WWAN_PORT_UNKNOWN) {
				ipc_imem_channel_init(ipc_imem, IPC_CTYPE_CTRL,
						      chnl_cfg_port,
						      IRQ_MOD_OFF);
				ipc_imem->ipc_port[ctrl_chl_idx] =
					ipc_port_init(ipc_imem, chnl_cfg_port);
			}
		}
		ctrl_chl_idx++;
	}

	ipc_task_queue_send_task(ipc_imem, ipc_imem_send_mdm_rdy_cb, 0, NULL, 0,
				 false);

	/* Complete all memory stores before setting bit */
	smp_mb__before_atomic();

	set_bit(FULLY_FUNCTIONAL, &ipc_imem->flag);

	/* Complete all memory stores after setting bit */
	smp_mb__after_atomic();
}

static void ipc_imem_handle_irq(struct iosm_imem *ipc_imem, int irq)
{
	enum ipc_mem_device_ipc_state curr_ipc_status;
	enum ipc_phase old_phase, phase;
	bool retry_allocation = false;
	bool ul_pending = false;
	int ch_id, i;

	if (irq != IMEM_IRQ_DONT_CARE)
		ipc_imem->ev_irq_pending[irq] = false;

	/* Get the internal phase. */
	old_phase = ipc_imem->phase;

	if (old_phase == IPC_P_OFF_REQ) {
		dev_dbg(ipc_imem->dev,
			"[%s]: Ignoring MSI. Deinit sequence in progress!",
			ipc_imem_phase_get_string(old_phase));
		return;
	}

	/* Update the phase controlled by CP. */
	phase = ipc_imem_phase_update(ipc_imem);

	switch (phase) {
	case IPC_P_RUN:
		if (!ipc_imem->enter_runtime) {
			/* Excute the transition from flash/boot to runtime. */
			ipc_imem->enter_runtime = 1;

			/* allow device to sleep, default value is
			 * IPC_HOST_SLEEP_ENTER_SLEEP
			 */
			ipc_imem_msg_send_device_sleep(ipc_imem,
						       ipc_imem->device_sleep);

			ipc_imem_msg_send_feature_set(ipc_imem,
						      IPC_MEM_INBAND_CRASH_SIG,
						  true);
		}

		curr_ipc_status =
			ipc_protocol_get_ipc_status(ipc_imem->ipc_protocol);

		/* check ipc_status change */
		if (ipc_imem->ipc_status != curr_ipc_status) {
			ipc_imem->ipc_status = curr_ipc_status;

			if (ipc_imem->ipc_status ==
			    IPC_MEM_DEVICE_IPC_RUNNING) {
				schedule_work(&ipc_imem->run_state_worker);
			}
		}

		/* Consider power management in the runtime phase. */
		ipc_imem_slp_control_exec(ipc_imem);
		break; /* Continue with skbuf processing. */

		/* Unexpected phases. */
	case IPC_P_OFF:
	case IPC_P_OFF_REQ:
		dev_err(ipc_imem->dev, "confused phase %s",
			ipc_imem_phase_get_string(phase));
		return;

	case IPC_P_PSI:
		if (old_phase != IPC_P_ROM)
			break;

		fallthrough;
		/* On CP the PSI phase is already active. */

	case IPC_P_ROM:
		/* Before CP ROM driver starts the PSI image, it sets
		 * the exit_code field on the doorbell scratchpad and
		 * triggers the irq.
		 */
		ipc_imem_rom_irq_exec(ipc_imem);
		return;

	default:
		break;
	}

	/* process message ring */
	ipc_protocol_msg_process(ipc_imem, irq);

	/* process all open pipes */
	for (i = 0; i < IPC_MEM_MAX_CHANNELS; i++) {
		struct ipc_pipe *ul_pipe = &ipc_imem->channels[i].ul_pipe;
		struct ipc_pipe *dl_pipe = &ipc_imem->channels[i].dl_pipe;

		if (dl_pipe->is_open &&
		    (irq == IMEM_IRQ_DONT_CARE || irq == dl_pipe->irq)) {
			ipc_imem_dl_pipe_process(ipc_imem, dl_pipe);

			if (dl_pipe->nr_of_queued_entries == 0)
				retry_allocation = true;
		}

		if (ul_pipe->is_open)
			ipc_imem_ul_pipe_process(ipc_imem, ul_pipe);
	}

	/* Try to generate new ADB or ADGH. */
	if (ipc_mux_ul_data_encode(ipc_imem->mux))
		ipc_imem_td_update_timer_start(ipc_imem);

	/* Continue the send procedure with accumulated SIO or NETIF packets.
	 * Reset the debounce flags.
	 */
	ul_pending |= ipc_imem_ul_write_td(ipc_imem);

	/* if UL data is pending restart TD update timer */
	if (ul_pending) {
		ipc_imem->hrtimer_period =
		ktime_set(0, TD_UPDATE_DEFAULT_TIMEOUT_USEC * 1000ULL);
		if (!hrtimer_active(&ipc_imem->tdupdate_timer))
			hrtimer_start(&ipc_imem->tdupdate_timer,
				      ipc_imem->hrtimer_period,
				      HRTIMER_MODE_REL);
	}

	/* If CP has executed the transition
	 * from IPC_INIT to IPC_RUNNING in the PSI
	 * phase, wake up the flash app to open the pipes.
	 */
	if ((phase == IPC_P_PSI || phase == IPC_P_EBL) &&
	    ipc_imem->ipc_requested_state == IPC_MEM_DEVICE_IPC_RUNNING &&
	    ipc_mmio_get_ipc_state(ipc_imem->mmio) ==
		    IPC_MEM_DEVICE_IPC_RUNNING &&
	    ipc_imem->flash_channel_id >= 0) {
		/* Wake up the flash app to open the pipes. */
		ch_id = ipc_imem->flash_channel_id;
		complete(&ipc_imem->channels[ch_id].ul_sem);
	}

	/* Reset the expected CP state. */
	ipc_imem->ipc_requested_state = IPC_MEM_DEVICE_IPC_DONT_CARE;

	if (retry_allocation) {
		ipc_imem->hrtimer_period =
		ktime_set(0, IPC_TD_ALLOC_TIMER_PERIOD_MS * 1000 * 1000ULL);
		if (!hrtimer_active(&ipc_imem->td_alloc_timer))
			hrtimer_start(&ipc_imem->td_alloc_timer,
				      ipc_imem->hrtimer_period,
				      HRTIMER_MODE_REL);
	}
}

/* Callback by tasklet for handling interrupt events. */
static int ipc_imem_tq_irq_cb(struct iosm_imem *ipc_imem, int arg, void *msg,
			      size_t size)
{
	ipc_imem_handle_irq(ipc_imem, arg);

	return 0;
}

void ipc_imem_ul_send(struct iosm_imem *ipc_imem)
{
	/* start doorbell irq delay timer if UL is pending */
	if (ipc_imem_ul_write_td(ipc_imem))
		ipc_imem_td_update_timer_start(ipc_imem);
}

/* Check the execution stage and update the AP phase */
static enum ipc_phase ipc_imem_phase_update_check(struct iosm_imem *ipc_imem,
						  enum ipc_mem_exec_stage stage)
{
	switch (stage) {
	case IPC_MEM_EXEC_STAGE_BOOT:
		if (ipc_imem->phase != IPC_P_ROM) {
			/* Send this event only once */
			ipc_uevent_send(ipc_imem->dev, UEVENT_ROM_READY);
		}

		ipc_imem->phase = IPC_P_ROM;
		break;

	case IPC_MEM_EXEC_STAGE_PSI:
		ipc_imem->phase = IPC_P_PSI;
		break;

	case IPC_MEM_EXEC_STAGE_EBL:
		ipc_imem->phase = IPC_P_EBL;
		break;

	case IPC_MEM_EXEC_STAGE_RUN:
		if (ipc_imem->phase != IPC_P_RUN &&
		    ipc_imem->ipc_status == IPC_MEM_DEVICE_IPC_RUNNING) {
			ipc_uevent_send(ipc_imem->dev, UEVENT_MDM_READY);
		}
		ipc_imem->phase = IPC_P_RUN;
		break;

	case IPC_MEM_EXEC_STAGE_CRASH:
		if (ipc_imem->phase != IPC_P_CRASH)
			ipc_uevent_send(ipc_imem->dev, UEVENT_CRASH);

		ipc_imem->phase = IPC_P_CRASH;
		break;

	case IPC_MEM_EXEC_STAGE_CD_READY:
		if (ipc_imem->phase != IPC_P_CD_READY)
			ipc_uevent_send(ipc_imem->dev, UEVENT_CD_READY);
		ipc_imem->phase = IPC_P_CD_READY;
		break;

	default:
		/* unknown exec stage:
		 * assume that link is down and send info to listeners
		 */
		ipc_uevent_send(ipc_imem->dev, UEVENT_CD_READY_LINK_DOWN);
		break;
	}

	return ipc_imem->phase;
}

/* Send msg to device to open pipe */
static bool ipc_imem_pipe_open(struct iosm_imem *ipc_imem,
			       struct ipc_pipe *pipe)
{
	union ipc_msg_prep_args prep_args = {
		.pipe_open.pipe = pipe,
	};

	if (ipc_protocol_msg_send(ipc_imem->ipc_protocol,
				  IPC_MSG_PREP_PIPE_OPEN, &prep_args) == 0)
		pipe->is_open = true;

	return pipe->is_open;
}

/* Allocates the TDs for the given pipe along with firing HP update DB. */
static int ipc_imem_tq_pipe_td_alloc(struct iosm_imem *ipc_imem, int arg,
				     void *msg, size_t size)
{
	struct ipc_pipe *dl_pipe = msg;
	bool processed = false;
	int i;

	for (i = 0; i < dl_pipe->nr_of_entries - 1; i++)
		processed |= ipc_imem_dl_skb_alloc(ipc_imem, dl_pipe);

	/* Trigger the doorbell irq to inform CP that new downlink buffers are
	 * available.
	 */
	if (processed)
		ipc_protocol_doorbell_trigger(ipc_imem->ipc_protocol, arg);

	return 0;
}

static enum hrtimer_restart
ipc_imem_td_update_timer_cb(struct hrtimer *hr_timer)
{
	struct iosm_imem *ipc_imem =
		container_of(hr_timer, struct iosm_imem, tdupdate_timer);

	ipc_task_queue_send_task(ipc_imem, ipc_imem_tq_td_update_timer_cb, 0,
				 NULL, 0, false);
	return HRTIMER_NORESTART;
}

/* Get the CP execution state and map it to the AP phase. */
enum ipc_phase ipc_imem_phase_update(struct iosm_imem *ipc_imem)
{
	enum ipc_mem_exec_stage exec_stage =
				ipc_imem_get_exec_stage_buffered(ipc_imem);
	/* If the CP stage is undef, return the internal precalculated phase. */
	return ipc_imem->phase == IPC_P_OFF_REQ ?
		       ipc_imem->phase :
		       ipc_imem_phase_update_check(ipc_imem, exec_stage);
}

const char *ipc_imem_phase_get_string(enum ipc_phase phase)
{
	switch (phase) {
	case IPC_P_RUN:
		return "A-RUN";

	case IPC_P_OFF:
		return "A-OFF";

	case IPC_P_ROM:
		return "A-ROM";

	case IPC_P_PSI:
		return "A-PSI";

	case IPC_P_EBL:
		return "A-EBL";

	case IPC_P_CRASH:
		return "A-CRASH";

	case IPC_P_CD_READY:
		return "A-CD_READY";

	case IPC_P_OFF_REQ:
		return "A-OFF_REQ";

	default:
		return "A-???";
	}
}

void ipc_imem_pipe_close(struct iosm_imem *ipc_imem, struct ipc_pipe *pipe)
{
	union ipc_msg_prep_args prep_args = { .pipe_close.pipe = pipe };

	pipe->is_open = false;
	ipc_protocol_msg_send(ipc_imem->ipc_protocol, IPC_MSG_PREP_PIPE_CLOSE,
			      &prep_args);

	ipc_imem_pipe_cleanup(ipc_imem, pipe);
}

void ipc_imem_channel_close(struct iosm_imem *ipc_imem, int channel_id)
{
	struct ipc_mem_channel *channel;

	if (channel_id < 0 || channel_id >= ipc_imem->nr_of_channels) {
		dev_err(ipc_imem->dev, "invalid channel id %d", channel_id);
		return;
	}

	channel = &ipc_imem->channels[channel_id];

	if (channel->state == IMEM_CHANNEL_FREE) {
		dev_err(ipc_imem->dev, "ch[%d]: invalid channel state %d",
			channel_id, channel->state);
		return;
	}

	/* Free only the channel id in the CP power off mode. */
	if (channel->state == IMEM_CHANNEL_RESERVED)
		/* Release only the channel id. */
		goto channel_free;

	if (ipc_imem->phase == IPC_P_RUN) {
		ipc_imem_pipe_close(ipc_imem, &channel->ul_pipe);
		ipc_imem_pipe_close(ipc_imem, &channel->dl_pipe);
	}

	ipc_imem_pipe_cleanup(ipc_imem, &channel->ul_pipe);
	ipc_imem_pipe_cleanup(ipc_imem, &channel->dl_pipe);

channel_free:
	ipc_imem_channel_free(channel);
}

struct ipc_mem_channel *ipc_imem_channel_open(struct iosm_imem *ipc_imem,
					      int channel_id, u32 db_id)
{
	struct ipc_mem_channel *channel;

	if (channel_id < 0 || channel_id >= IPC_MEM_MAX_CHANNELS) {
		dev_err(ipc_imem->dev, "invalid channel ID: %d", channel_id);
		return NULL;
	}

	channel = &ipc_imem->channels[channel_id];

	channel->state = IMEM_CHANNEL_ACTIVE;

	if (!ipc_imem_pipe_open(ipc_imem, &channel->ul_pipe))
		goto ul_pipe_err;

	if (!ipc_imem_pipe_open(ipc_imem, &channel->dl_pipe))
		goto dl_pipe_err;

	/* Allocate the downlink buffers in tasklet context. */
	if (ipc_task_queue_send_task(ipc_imem, ipc_imem_tq_pipe_td_alloc, db_id,
				     &channel->dl_pipe, 0, false)) {
		dev_err(ipc_imem->dev, "td allocation failed : %d", channel_id);
		goto task_failed;
	}

	/* Active channel. */
	return channel;
task_failed:
	ipc_imem_pipe_close(ipc_imem, &channel->dl_pipe);
dl_pipe_err:
	ipc_imem_pipe_close(ipc_imem, &channel->ul_pipe);
ul_pipe_err:
	ipc_imem_channel_free(channel);
	return NULL;
}

void ipc_imem_pm_suspend(struct iosm_imem *ipc_imem)
{
	ipc_protocol_suspend(ipc_imem->ipc_protocol);
}

void ipc_imem_pm_s2idle_sleep(struct iosm_imem *ipc_imem, bool sleep)
{
	ipc_protocol_s2idle_sleep(ipc_imem->ipc_protocol, sleep);
}

void ipc_imem_pm_resume(struct iosm_imem *ipc_imem)
{
	enum ipc_mem_exec_stage stage;

	if (ipc_protocol_resume(ipc_imem->ipc_protocol)) {
		stage = ipc_mmio_get_exec_stage(ipc_imem->mmio);
		ipc_imem_phase_update_check(ipc_imem, stage);
	}
}

void ipc_imem_channel_free(struct ipc_mem_channel *channel)
{
	/* Reset dynamic channel elements. */
	channel->state = IMEM_CHANNEL_FREE;
}

int ipc_imem_channel_alloc(struct iosm_imem *ipc_imem, int index,
			   enum ipc_ctype ctype)
{
	struct ipc_mem_channel *channel;
	int i;

	/* Find channel of given type/index */
	for (i = 0; i < ipc_imem->nr_of_channels; i++) {
		channel = &ipc_imem->channels[i];
		if (channel->ctype == ctype && channel->index == index)
			break;
	}

	if (i >= ipc_imem->nr_of_channels) {
		dev_dbg(ipc_imem->dev,
			"no channel definition for index=%d ctype=%d", index,
			ctype);
		return -ECHRNG;
	}

	if (ipc_imem->channels[i].state != IMEM_CHANNEL_FREE) {
		dev_dbg(ipc_imem->dev, "channel is in use");
		return -EBUSY;
	}

	if (channel->ctype == IPC_CTYPE_WWAN &&
	    index == IPC_MEM_MUX_IP_CH_IF_ID)
		channel->if_id = index;

	channel->channel_id = index;
	channel->state = IMEM_CHANNEL_RESERVED;

	return i;
}

void ipc_imem_channel_init(struct iosm_imem *ipc_imem, enum ipc_ctype ctype,
			   struct ipc_chnl_cfg chnl_cfg, u32 irq_moderation)
{
	struct ipc_mem_channel *channel;

	if (chnl_cfg.ul_pipe >= IPC_MEM_MAX_PIPES ||
	    chnl_cfg.dl_pipe >= IPC_MEM_MAX_PIPES) {
		dev_err(ipc_imem->dev, "invalid pipe: ul_pipe=%d, dl_pipe=%d",
			chnl_cfg.ul_pipe, chnl_cfg.dl_pipe);
		return;
	}

	if (ipc_imem->nr_of_channels >= IPC_MEM_MAX_CHANNELS) {
		dev_err(ipc_imem->dev, "too many channels");
		return;
	}

	channel = &ipc_imem->channels[ipc_imem->nr_of_channels];
	channel->channel_id = ipc_imem->nr_of_channels;
	channel->ctype = ctype;
	channel->index = chnl_cfg.id;
	channel->net_err_count = 0;
	channel->state = IMEM_CHANNEL_FREE;
	ipc_imem->nr_of_channels++;

	ipc_imem_channel_update(ipc_imem, channel->channel_id, chnl_cfg,
				IRQ_MOD_OFF);

	skb_queue_head_init(&channel->ul_list);

	init_completion(&channel->ul_sem);
}

void ipc_imem_channel_update(struct iosm_imem *ipc_imem, int id,
			     struct ipc_chnl_cfg chnl_cfg, u32 irq_moderation)
{
	struct ipc_mem_channel *channel;

	if (id < 0 || id >= ipc_imem->nr_of_channels) {
		dev_err(ipc_imem->dev, "invalid channel id %d", id);
		return;
	}

	channel = &ipc_imem->channels[id];

	if (channel->state != IMEM_CHANNEL_FREE &&
	    channel->state != IMEM_CHANNEL_RESERVED) {
		dev_err(ipc_imem->dev, "invalid channel state %d",
			channel->state);
		return;
	}

	channel->ul_pipe.nr_of_entries = chnl_cfg.ul_nr_of_entries;
	channel->ul_pipe.pipe_nr = chnl_cfg.ul_pipe;
	channel->ul_pipe.is_open = false;
	channel->ul_pipe.irq = IPC_UL_PIPE_IRQ_VECTOR;
	channel->ul_pipe.channel = channel;
	channel->ul_pipe.dir = IPC_MEM_DIR_UL;
	channel->ul_pipe.accumulation_backoff = chnl_cfg.accumulation_backoff;
	channel->ul_pipe.irq_moderation = irq_moderation;
	channel->ul_pipe.buf_size = 0;

	channel->dl_pipe.nr_of_entries = chnl_cfg.dl_nr_of_entries;
	channel->dl_pipe.pipe_nr = chnl_cfg.dl_pipe;
	channel->dl_pipe.is_open = false;
	channel->dl_pipe.irq = IPC_DL_PIPE_IRQ_VECTOR;
	channel->dl_pipe.channel = channel;
	channel->dl_pipe.dir = IPC_MEM_DIR_DL;
	channel->dl_pipe.accumulation_backoff = chnl_cfg.accumulation_backoff;
	channel->dl_pipe.irq_moderation = irq_moderation;
	channel->dl_pipe.buf_size = chnl_cfg.dl_buf_size;
}

static void ipc_imem_channel_reset(struct iosm_imem *ipc_imem)
{
	int i;

	for (i = 0; i < ipc_imem->nr_of_channels; i++) {
		struct ipc_mem_channel *channel;

		channel = &ipc_imem->channels[i];

		ipc_imem_pipe_cleanup(ipc_imem, &channel->dl_pipe);
		ipc_imem_pipe_cleanup(ipc_imem, &channel->ul_pipe);

		ipc_imem_channel_free(channel);
	}
}

void ipc_imem_pipe_cleanup(struct iosm_imem *ipc_imem, struct ipc_pipe *pipe)
{
	struct sk_buff *skb;

	/* Force pipe to closed state also when not explicitly closed through
	 * ipc_imem_pipe_close()
	 */
	pipe->is_open = false;

	/* Empty the uplink skb accumulator. */
	while ((skb = skb_dequeue(&pipe->channel->ul_list)))
		ipc_pcie_kfree_skb(ipc_imem->pcie, skb);

	ipc_protocol_pipe_cleanup(ipc_imem->ipc_protocol, pipe);
}

/* Send IPC protocol uninit to the modem when Link is active. */
static void ipc_imem_device_ipc_uninit(struct iosm_imem *ipc_imem)
{
	int timeout = IPC_MODEM_UNINIT_TIMEOUT_MS;
	enum ipc_mem_device_ipc_state ipc_state;

	/* When PCIe link is up set IPC_UNINIT
	 * of the modem otherwise ignore it when PCIe link down happens.
	 */
	if (ipc_pcie_check_data_link_active(ipc_imem->pcie)) {
		/* set modem to UNINIT
		 * (in case we want to reload the AP driver without resetting
		 * the modem)
		 */
		ipc_doorbell_fire(ipc_imem->pcie, IPC_DOORBELL_IRQ_IPC,
				  IPC_MEM_DEVICE_IPC_UNINIT);
		ipc_state = ipc_mmio_get_ipc_state(ipc_imem->mmio);

		/* Wait for maximum 30ms to allow the Modem to uninitialize the
		 * protocol.
		 */
		while ((ipc_state <= IPC_MEM_DEVICE_IPC_DONT_CARE) &&
		       (ipc_state != IPC_MEM_DEVICE_IPC_UNINIT) &&
		       (timeout > 0)) {
			usleep_range(1000, 1250);
			timeout--;
			ipc_state = ipc_mmio_get_ipc_state(ipc_imem->mmio);
		}
	}
}

void ipc_imem_cleanup(struct iosm_imem *ipc_imem)
{
	ipc_imem->phase = IPC_P_OFF_REQ;

	/* forward MDM_NOT_READY to listeners */
	ipc_uevent_send(ipc_imem->dev, UEVENT_MDM_NOT_READY);

	hrtimer_cancel(&ipc_imem->td_alloc_timer);
	hrtimer_cancel(&ipc_imem->tdupdate_timer);
	hrtimer_cancel(&ipc_imem->fast_update_timer);
	hrtimer_cancel(&ipc_imem->startup_timer);

	/* cancel the workqueue */
	cancel_work_sync(&ipc_imem->run_state_worker);

	if (test_and_clear_bit(FULLY_FUNCTIONAL, &ipc_imem->flag)) {
		ipc_mux_deinit(ipc_imem->mux);
		ipc_wwan_deinit(ipc_imem->wwan);
		ipc_port_deinit(ipc_imem->ipc_port);
	}

	ipc_imem_device_ipc_uninit(ipc_imem);
	ipc_imem_channel_reset(ipc_imem);

	ipc_protocol_deinit(ipc_imem->ipc_protocol);
	ipc_task_deinit(ipc_imem->ipc_task);

	kfree(ipc_imem->ipc_task);
	kfree(ipc_imem->mmio);

	ipc_imem->phase = IPC_P_OFF;
}

/* After CP has unblocked the PCIe link, save the start address of the doorbell
 * scratchpad and prepare the shared memory region. If the flashing to RAM
 * procedure shall be executed, copy the chip information from the doorbell
 * scratchtpad to the application buffer and wake up the flash app.
 */
static int ipc_imem_config(struct iosm_imem *ipc_imem)
{
	enum ipc_phase phase;

	/* Initialize the semaphore for the blocking read UL/DL transfer. */
	init_completion(&ipc_imem->ul_pend_sem);

	init_completion(&ipc_imem->dl_pend_sem);

	/* clear internal flags */
	ipc_imem->ipc_status = IPC_MEM_DEVICE_IPC_UNINIT;
	ipc_imem->enter_runtime = 0;

	phase = ipc_imem_phase_update(ipc_imem);

	/* Either CP shall be in the power off or power on phase. */
	switch (phase) {
	case IPC_P_ROM:
		ipc_imem->hrtimer_period = ktime_set(0, 1000 * 1000 * 1000ULL);
		/* poll execution stage (for delayed start, e.g. NAND) */
		if (!hrtimer_active(&ipc_imem->startup_timer))
			hrtimer_start(&ipc_imem->startup_timer,
				      ipc_imem->hrtimer_period,
				      HRTIMER_MODE_REL);
		return 0;

	case IPC_P_PSI:
	case IPC_P_EBL:
	case IPC_P_RUN:
		/* The initial IPC state is IPC_MEM_DEVICE_IPC_UNINIT. */
		ipc_imem->ipc_requested_state = IPC_MEM_DEVICE_IPC_UNINIT;

		/* Verify the exepected initial state. */
		if (ipc_imem->ipc_requested_state ==
		    ipc_mmio_get_ipc_state(ipc_imem->mmio)) {
			ipc_imem_ipc_init_check(ipc_imem);

			return 0;
		}
		dev_err(ipc_imem->dev,
			"ipc_status(%d) != IPC_MEM_DEVICE_IPC_UNINIT",
			ipc_mmio_get_ipc_state(ipc_imem->mmio));
		break;
	case IPC_P_CRASH:
	case IPC_P_CD_READY:
		dev_dbg(ipc_imem->dev,
			"Modem is in phase %d, reset Modem to collect CD",
			phase);
		return 0;
	default:
		dev_err(ipc_imem->dev, "unexpected operation phase %d", phase);
		break;
	}

	complete(&ipc_imem->dl_pend_sem);
	complete(&ipc_imem->ul_pend_sem);
	ipc_imem->phase = IPC_P_OFF;
	return -EIO;
}

/* Pass the dev ptr to the shared memory driver and request the entry points */
struct iosm_imem *ipc_imem_init(struct iosm_pcie *pcie, unsigned int device_id,
				void __iomem *mmio, struct device *dev)
{
	struct iosm_imem *ipc_imem = kzalloc(sizeof(*pcie->imem), GFP_KERNEL);

	if (!ipc_imem)
		return NULL;

	/* Save the device address. */
	ipc_imem->pcie = pcie;
	ipc_imem->dev = dev;

	ipc_imem->pci_device_id = device_id;

	ipc_imem->ev_cdev_write_pending = false;
	ipc_imem->cp_version = 0;
	ipc_imem->device_sleep = IPC_HOST_SLEEP_ENTER_SLEEP;

	/* Reset the flash channel id. */
	ipc_imem->flash_channel_id = -1;

	/* Reset the max number of configured channels */
	ipc_imem->nr_of_channels = 0;

	/* allocate IPC MMIO */
	ipc_imem->mmio = ipc_mmio_init(mmio, ipc_imem->dev);
	if (!ipc_imem->mmio) {
		dev_err(ipc_imem->dev, "failed to initialize mmio region");
		goto mmio_init_fail;
	}

	ipc_imem->ipc_task = kzalloc(sizeof(*ipc_imem->ipc_task),
				     GFP_KERNEL);

	/* Create tasklet for event handling*/
	if (!ipc_imem->ipc_task)
		goto ipc_task_fail;

	if (ipc_task_init(ipc_imem->ipc_task))
		goto ipc_task_init_fail;

	ipc_imem->ipc_task->dev = ipc_imem->dev;

	INIT_WORK(&ipc_imem->run_state_worker, ipc_imem_run_state_worker);

	ipc_imem->ipc_protocol = ipc_protocol_init(ipc_imem);

	if (!ipc_imem->ipc_protocol)
		goto protocol_init_fail;

	/* The phase is set to power off. */
	ipc_imem->phase = IPC_P_OFF;

	hrtimer_init(&ipc_imem->startup_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	ipc_imem->startup_timer.function = ipc_imem_startup_timer_cb;

	hrtimer_init(&ipc_imem->tdupdate_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	ipc_imem->tdupdate_timer.function = ipc_imem_td_update_timer_cb;

	hrtimer_init(&ipc_imem->fast_update_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	ipc_imem->fast_update_timer.function = ipc_imem_fast_update_timer_cb;

	hrtimer_init(&ipc_imem->td_alloc_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	ipc_imem->td_alloc_timer.function = ipc_imem_td_alloc_timer_cb;

	if (ipc_imem_config(ipc_imem)) {
		dev_err(ipc_imem->dev, "failed to initialize the imem");
		goto imem_config_fail;
	}

	return ipc_imem;

imem_config_fail:
	hrtimer_cancel(&ipc_imem->td_alloc_timer);
	hrtimer_cancel(&ipc_imem->fast_update_timer);
	hrtimer_cancel(&ipc_imem->tdupdate_timer);
	hrtimer_cancel(&ipc_imem->startup_timer);
protocol_init_fail:
	cancel_work_sync(&ipc_imem->run_state_worker);
	ipc_task_deinit(ipc_imem->ipc_task);
ipc_task_init_fail:
	kfree(ipc_imem->ipc_task);
ipc_task_fail:
	kfree(ipc_imem->mmio);
mmio_init_fail:
	kfree(ipc_imem);
	return NULL;
}

void ipc_imem_irq_process(struct iosm_imem *ipc_imem, int irq)
{
	/* Debounce IPC_EV_IRQ. */
	if (ipc_imem && !ipc_imem->ev_irq_pending[irq]) {
		ipc_imem->ev_irq_pending[irq] = true;
		ipc_task_queue_send_task(ipc_imem, ipc_imem_tq_irq_cb, irq,
					 NULL, 0, false);
	}
}

void ipc_imem_td_update_timer_suspend(struct iosm_imem *ipc_imem, bool suspend)
{
	ipc_imem->td_update_timer_suspended = suspend;
}
