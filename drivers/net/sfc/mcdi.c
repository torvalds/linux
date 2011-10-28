/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2008-2011 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/delay.h>
#include "net_driver.h"
#include "nic.h"
#include "io.h"
#include "regs.h"
#include "mcdi_pcol.h"
#include "phy.h"

/**************************************************************************
 *
 * Management-Controller-to-Driver Interface
 *
 **************************************************************************
 */

/* Software-defined structure to the shared-memory */
#define CMD_NOTIFY_PORT0 0
#define CMD_NOTIFY_PORT1 4
#define CMD_PDU_PORT0    0x008
#define CMD_PDU_PORT1    0x108
#define REBOOT_FLAG_PORT0 0x3f8
#define REBOOT_FLAG_PORT1 0x3fc

#define MCDI_RPC_TIMEOUT       10 /*seconds */

#define MCDI_PDU(efx)							\
	(efx_port_num(efx) ? CMD_PDU_PORT1 : CMD_PDU_PORT0)
#define MCDI_DOORBELL(efx)						\
	(efx_port_num(efx) ? CMD_NOTIFY_PORT1 : CMD_NOTIFY_PORT0)
#define MCDI_REBOOT_FLAG(efx)						\
	(efx_port_num(efx) ? REBOOT_FLAG_PORT1 : REBOOT_FLAG_PORT0)

#define SEQ_MASK							\
	EFX_MASK32(EFX_WIDTH(MCDI_HEADER_SEQ))

static inline struct efx_mcdi_iface *efx_mcdi(struct efx_nic *efx)
{
	struct siena_nic_data *nic_data;
	EFX_BUG_ON_PARANOID(efx_nic_rev(efx) < EFX_REV_SIENA_A0);
	nic_data = efx->nic_data;
	return &nic_data->mcdi;
}

static inline void
efx_mcdi_readd(struct efx_nic *efx, efx_dword_t *value, unsigned reg)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	value->u32[0] = (__force __le32)__raw_readl(nic_data->mcdi_smem + reg);
}

static inline void
efx_mcdi_writed(struct efx_nic *efx, const efx_dword_t *value, unsigned reg)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	__raw_writel((__force u32)value->u32[0], nic_data->mcdi_smem + reg);
}

void efx_mcdi_init(struct efx_nic *efx)
{
	struct efx_mcdi_iface *mcdi;

	if (efx_nic_rev(efx) < EFX_REV_SIENA_A0)
		return;

	mcdi = efx_mcdi(efx);
	init_waitqueue_head(&mcdi->wq);
	spin_lock_init(&mcdi->iface_lock);
	atomic_set(&mcdi->state, MCDI_STATE_QUIESCENT);
	mcdi->mode = MCDI_MODE_POLL;

	(void) efx_mcdi_poll_reboot(efx);
}

static void efx_mcdi_copyin(struct efx_nic *efx, unsigned cmd,
			    const u8 *inbuf, size_t inlen)
{
	struct efx_mcdi_iface *mcdi = efx_mcdi(efx);
	unsigned pdu = MCDI_PDU(efx);
	unsigned doorbell = MCDI_DOORBELL(efx);
	unsigned int i;
	efx_dword_t hdr;
	u32 xflags, seqno;

	BUG_ON(atomic_read(&mcdi->state) == MCDI_STATE_QUIESCENT);
	BUG_ON(inlen & 3 || inlen >= 0x100);

	seqno = mcdi->seqno & SEQ_MASK;
	xflags = 0;
	if (mcdi->mode == MCDI_MODE_EVENTS)
		xflags |= MCDI_HEADER_XFLAGS_EVREQ;

	EFX_POPULATE_DWORD_6(hdr,
			     MCDI_HEADER_RESPONSE, 0,
			     MCDI_HEADER_RESYNC, 1,
			     MCDI_HEADER_CODE, cmd,
			     MCDI_HEADER_DATALEN, inlen,
			     MCDI_HEADER_SEQ, seqno,
			     MCDI_HEADER_XFLAGS, xflags);

	efx_mcdi_writed(efx, &hdr, pdu);

	for (i = 0; i < inlen; i += 4)
		efx_mcdi_writed(efx, (const efx_dword_t *)(inbuf + i),
				pdu + 4 + i);

	/* ring the doorbell with a distinctive value */
	EFX_POPULATE_DWORD_1(hdr, EFX_DWORD_0, 0x45789abc);
	efx_mcdi_writed(efx, &hdr, doorbell);
}

static void efx_mcdi_copyout(struct efx_nic *efx, u8 *outbuf, size_t outlen)
{
	struct efx_mcdi_iface *mcdi = efx_mcdi(efx);
	unsigned int pdu = MCDI_PDU(efx);
	int i;

	BUG_ON(atomic_read(&mcdi->state) == MCDI_STATE_QUIESCENT);
	BUG_ON(outlen & 3 || outlen >= 0x100);

	for (i = 0; i < outlen; i += 4)
		efx_mcdi_readd(efx, (efx_dword_t *)(outbuf + i), pdu + 4 + i);
}

static int efx_mcdi_poll(struct efx_nic *efx)
{
	struct efx_mcdi_iface *mcdi = efx_mcdi(efx);
	unsigned int time, finish;
	unsigned int respseq, respcmd, error;
	unsigned int pdu = MCDI_PDU(efx);
	unsigned int rc, spins;
	efx_dword_t reg;

	/* Check for a reboot atomically with respect to efx_mcdi_copyout() */
	rc = -efx_mcdi_poll_reboot(efx);
	if (rc)
		goto out;

	/* Poll for completion. Poll quickly (once a us) for the 1st jiffy,
	 * because generally mcdi responses are fast. After that, back off
	 * and poll once a jiffy (approximately)
	 */
	spins = TICK_USEC;
	finish = get_seconds() + MCDI_RPC_TIMEOUT;

	while (1) {
		if (spins != 0) {
			--spins;
			udelay(1);
		} else {
			schedule_timeout_uninterruptible(1);
		}

		time = get_seconds();

		efx_mcdi_readd(efx, &reg, pdu);

		/* All 1's indicates that shared memory is in reset (and is
		 * not a valid header). Wait for it to come out reset before
		 * completing the command */
		if (EFX_DWORD_FIELD(reg, EFX_DWORD_0) != 0xffffffff &&
		    EFX_DWORD_FIELD(reg, MCDI_HEADER_RESPONSE))
			break;

		if (time >= finish)
			return -ETIMEDOUT;
	}

	mcdi->resplen = EFX_DWORD_FIELD(reg, MCDI_HEADER_DATALEN);
	respseq = EFX_DWORD_FIELD(reg, MCDI_HEADER_SEQ);
	respcmd = EFX_DWORD_FIELD(reg, MCDI_HEADER_CODE);
	error = EFX_DWORD_FIELD(reg, MCDI_HEADER_ERROR);

	if (error && mcdi->resplen == 0) {
		netif_err(efx, hw, efx->net_dev, "MC rebooted\n");
		rc = EIO;
	} else if ((respseq ^ mcdi->seqno) & SEQ_MASK) {
		netif_err(efx, hw, efx->net_dev,
			  "MC response mismatch tx seq 0x%x rx seq 0x%x\n",
			  respseq, mcdi->seqno);
		rc = EIO;
	} else if (error) {
		efx_mcdi_readd(efx, &reg, pdu + 4);
		switch (EFX_DWORD_FIELD(reg, EFX_DWORD_0)) {
#define TRANSLATE_ERROR(name)					\
		case MC_CMD_ERR_ ## name:			\
			rc = name;				\
			break
			TRANSLATE_ERROR(ENOENT);
			TRANSLATE_ERROR(EINTR);
			TRANSLATE_ERROR(EACCES);
			TRANSLATE_ERROR(EBUSY);
			TRANSLATE_ERROR(EINVAL);
			TRANSLATE_ERROR(EDEADLK);
			TRANSLATE_ERROR(ENOSYS);
			TRANSLATE_ERROR(ETIME);
#undef TRANSLATE_ERROR
		default:
			rc = EIO;
			break;
		}
	} else
		rc = 0;

out:
	mcdi->resprc = rc;
	if (rc)
		mcdi->resplen = 0;

	/* Return rc=0 like wait_event_timeout() */
	return 0;
}

/* Test and clear MC-rebooted flag for this port/function */
int efx_mcdi_poll_reboot(struct efx_nic *efx)
{
	unsigned int addr = MCDI_REBOOT_FLAG(efx);
	efx_dword_t reg;
	uint32_t value;

	if (efx_nic_rev(efx) < EFX_REV_SIENA_A0)
		return false;

	efx_mcdi_readd(efx, &reg, addr);
	value = EFX_DWORD_FIELD(reg, EFX_DWORD_0);

	if (value == 0)
		return 0;

	EFX_ZERO_DWORD(reg);
	efx_mcdi_writed(efx, &reg, addr);

	if (value == MC_STATUS_DWORD_ASSERT)
		return -EINTR;
	else
		return -EIO;
}

static void efx_mcdi_acquire(struct efx_mcdi_iface *mcdi)
{
	/* Wait until the interface becomes QUIESCENT and we win the race
	 * to mark it RUNNING. */
	wait_event(mcdi->wq,
		   atomic_cmpxchg(&mcdi->state,
				  MCDI_STATE_QUIESCENT,
				  MCDI_STATE_RUNNING)
		   == MCDI_STATE_QUIESCENT);
}

static int efx_mcdi_await_completion(struct efx_nic *efx)
{
	struct efx_mcdi_iface *mcdi = efx_mcdi(efx);

	if (wait_event_timeout(
		    mcdi->wq,
		    atomic_read(&mcdi->state) == MCDI_STATE_COMPLETED,
		    msecs_to_jiffies(MCDI_RPC_TIMEOUT * 1000)) == 0)
		return -ETIMEDOUT;

	/* Check if efx_mcdi_set_mode() switched us back to polled completions.
	 * In which case, poll for completions directly. If efx_mcdi_ev_cpl()
	 * completed the request first, then we'll just end up completing the
	 * request again, which is safe.
	 *
	 * We need an smp_rmb() to synchronise with efx_mcdi_mode_poll(), which
	 * wait_event_timeout() implicitly provides.
	 */
	if (mcdi->mode == MCDI_MODE_POLL)
		return efx_mcdi_poll(efx);

	return 0;
}

static bool efx_mcdi_complete(struct efx_mcdi_iface *mcdi)
{
	/* If the interface is RUNNING, then move to COMPLETED and wake any
	 * waiters. If the interface isn't in RUNNING then we've received a
	 * duplicate completion after we've already transitioned back to
	 * QUIESCENT. [A subsequent invocation would increment seqno, so would
	 * have failed the seqno check].
	 */
	if (atomic_cmpxchg(&mcdi->state,
			   MCDI_STATE_RUNNING,
			   MCDI_STATE_COMPLETED) == MCDI_STATE_RUNNING) {
		wake_up(&mcdi->wq);
		return true;
	}

	return false;
}

static void efx_mcdi_release(struct efx_mcdi_iface *mcdi)
{
	atomic_set(&mcdi->state, MCDI_STATE_QUIESCENT);
	wake_up(&mcdi->wq);
}

static void efx_mcdi_ev_cpl(struct efx_nic *efx, unsigned int seqno,
			    unsigned int datalen, unsigned int errno)
{
	struct efx_mcdi_iface *mcdi = efx_mcdi(efx);
	bool wake = false;

	spin_lock(&mcdi->iface_lock);

	if ((seqno ^ mcdi->seqno) & SEQ_MASK) {
		if (mcdi->credits)
			/* The request has been cancelled */
			--mcdi->credits;
		else
			netif_err(efx, hw, efx->net_dev,
				  "MC response mismatch tx seq 0x%x rx "
				  "seq 0x%x\n", seqno, mcdi->seqno);
	} else {
		mcdi->resprc = errno;
		mcdi->resplen = datalen;

		wake = true;
	}

	spin_unlock(&mcdi->iface_lock);

	if (wake)
		efx_mcdi_complete(mcdi);
}

/* Issue the given command by writing the data into the shared memory PDU,
 * ring the doorbell and wait for completion. Copyout the result. */
int efx_mcdi_rpc(struct efx_nic *efx, unsigned cmd,
		 const u8 *inbuf, size_t inlen, u8 *outbuf, size_t outlen,
		 size_t *outlen_actual)
{
	struct efx_mcdi_iface *mcdi = efx_mcdi(efx);
	int rc;
	BUG_ON(efx_nic_rev(efx) < EFX_REV_SIENA_A0);

	efx_mcdi_acquire(mcdi);

	/* Serialise with efx_mcdi_ev_cpl() and efx_mcdi_ev_death() */
	spin_lock_bh(&mcdi->iface_lock);
	++mcdi->seqno;
	spin_unlock_bh(&mcdi->iface_lock);

	efx_mcdi_copyin(efx, cmd, inbuf, inlen);

	if (mcdi->mode == MCDI_MODE_POLL)
		rc = efx_mcdi_poll(efx);
	else
		rc = efx_mcdi_await_completion(efx);

	if (rc != 0) {
		/* Close the race with efx_mcdi_ev_cpl() executing just too late
		 * and completing a request we've just cancelled, by ensuring
		 * that the seqno check therein fails.
		 */
		spin_lock_bh(&mcdi->iface_lock);
		++mcdi->seqno;
		++mcdi->credits;
		spin_unlock_bh(&mcdi->iface_lock);

		netif_err(efx, hw, efx->net_dev,
			  "MC command 0x%x inlen %d mode %d timed out\n",
			  cmd, (int)inlen, mcdi->mode);
	} else {
		size_t resplen;

		/* At the very least we need a memory barrier here to ensure
		 * we pick up changes from efx_mcdi_ev_cpl(). Protect against
		 * a spurious efx_mcdi_ev_cpl() running concurrently by
		 * acquiring the iface_lock. */
		spin_lock_bh(&mcdi->iface_lock);
		rc = -mcdi->resprc;
		resplen = mcdi->resplen;
		spin_unlock_bh(&mcdi->iface_lock);

		if (rc == 0) {
			efx_mcdi_copyout(efx, outbuf,
					 min(outlen, mcdi->resplen + 3) & ~0x3);
			if (outlen_actual != NULL)
				*outlen_actual = resplen;
		} else if (cmd == MC_CMD_REBOOT && rc == -EIO)
			; /* Don't reset if MC_CMD_REBOOT returns EIO */
		else if (rc == -EIO || rc == -EINTR) {
			netif_err(efx, hw, efx->net_dev, "MC fatal error %d\n",
				  -rc);
			efx_schedule_reset(efx, RESET_TYPE_MC_FAILURE);
		} else
			netif_dbg(efx, hw, efx->net_dev,
				  "MC command 0x%x inlen %d failed rc=%d\n",
				  cmd, (int)inlen, -rc);
	}

	efx_mcdi_release(mcdi);
	return rc;
}

void efx_mcdi_mode_poll(struct efx_nic *efx)
{
	struct efx_mcdi_iface *mcdi;

	if (efx_nic_rev(efx) < EFX_REV_SIENA_A0)
		return;

	mcdi = efx_mcdi(efx);
	if (mcdi->mode == MCDI_MODE_POLL)
		return;

	/* We can switch from event completion to polled completion, because
	 * mcdi requests are always completed in shared memory. We do this by
	 * switching the mode to POLL'd then completing the request.
	 * efx_mcdi_await_completion() will then call efx_mcdi_poll().
	 *
	 * We need an smp_wmb() to synchronise with efx_mcdi_await_completion(),
	 * which efx_mcdi_complete() provides for us.
	 */
	mcdi->mode = MCDI_MODE_POLL;

	efx_mcdi_complete(mcdi);
}

void efx_mcdi_mode_event(struct efx_nic *efx)
{
	struct efx_mcdi_iface *mcdi;

	if (efx_nic_rev(efx) < EFX_REV_SIENA_A0)
		return;

	mcdi = efx_mcdi(efx);

	if (mcdi->mode == MCDI_MODE_EVENTS)
		return;

	/* We can't switch from polled to event completion in the middle of a
	 * request, because the completion method is specified in the request.
	 * So acquire the interface to serialise the requestors. We don't need
	 * to acquire the iface_lock to change the mode here, but we do need a
	 * write memory barrier ensure that efx_mcdi_rpc() sees it, which
	 * efx_mcdi_acquire() provides.
	 */
	efx_mcdi_acquire(mcdi);
	mcdi->mode = MCDI_MODE_EVENTS;
	efx_mcdi_release(mcdi);
}

static void efx_mcdi_ev_death(struct efx_nic *efx, int rc)
{
	struct efx_mcdi_iface *mcdi = efx_mcdi(efx);

	/* If there is an outstanding MCDI request, it has been terminated
	 * either by a BADASSERT or REBOOT event. If the mcdi interface is
	 * in polled mode, then do nothing because the MC reboot handler will
	 * set the header correctly. However, if the mcdi interface is waiting
	 * for a CMDDONE event it won't receive it [and since all MCDI events
	 * are sent to the same queue, we can't be racing with
	 * efx_mcdi_ev_cpl()]
	 *
	 * There's a race here with efx_mcdi_rpc(), because we might receive
	 * a REBOOT event *before* the request has been copied out. In polled
	 * mode (during startup) this is irrelevant, because efx_mcdi_complete()
	 * is ignored. In event mode, this condition is just an edge-case of
	 * receiving a REBOOT event after posting the MCDI request. Did the mc
	 * reboot before or after the copyout? The best we can do always is
	 * just return failure.
	 */
	spin_lock(&mcdi->iface_lock);
	if (efx_mcdi_complete(mcdi)) {
		if (mcdi->mode == MCDI_MODE_EVENTS) {
			mcdi->resprc = rc;
			mcdi->resplen = 0;
			++mcdi->credits;
		}
	} else
		/* Nobody was waiting for an MCDI request, so trigger a reset */
		efx_schedule_reset(efx, RESET_TYPE_MC_FAILURE);

	spin_unlock(&mcdi->iface_lock);
}

static unsigned int efx_mcdi_event_link_speed[] = {
	[MCDI_EVENT_LINKCHANGE_SPEED_100M] = 100,
	[MCDI_EVENT_LINKCHANGE_SPEED_1G] = 1000,
	[MCDI_EVENT_LINKCHANGE_SPEED_10G] = 10000,
};


static void efx_mcdi_process_link_change(struct efx_nic *efx, efx_qword_t *ev)
{
	u32 flags, fcntl, speed, lpa;

	speed = EFX_QWORD_FIELD(*ev, MCDI_EVENT_LINKCHANGE_SPEED);
	EFX_BUG_ON_PARANOID(speed >= ARRAY_SIZE(efx_mcdi_event_link_speed));
	speed = efx_mcdi_event_link_speed[speed];

	flags = EFX_QWORD_FIELD(*ev, MCDI_EVENT_LINKCHANGE_LINK_FLAGS);
	fcntl = EFX_QWORD_FIELD(*ev, MCDI_EVENT_LINKCHANGE_FCNTL);
	lpa = EFX_QWORD_FIELD(*ev, MCDI_EVENT_LINKCHANGE_LP_CAP);

	/* efx->link_state is only modified by efx_mcdi_phy_get_link(),
	 * which is only run after flushing the event queues. Therefore, it
	 * is safe to modify the link state outside of the mac_lock here.
	 */
	efx_mcdi_phy_decode_link(efx, &efx->link_state, speed, flags, fcntl);

	efx_mcdi_phy_check_fcntl(efx, lpa);

	efx_link_status_changed(efx);
}

static const char *sensor_names[] = {
	[MC_CMD_SENSOR_CONTROLLER_TEMP] = "Controller temp. sensor",
	[MC_CMD_SENSOR_PHY_COMMON_TEMP] = "PHY shared temp. sensor",
	[MC_CMD_SENSOR_CONTROLLER_COOLING] = "Controller cooling",
	[MC_CMD_SENSOR_PHY0_TEMP] = "PHY 0 temp. sensor",
	[MC_CMD_SENSOR_PHY0_COOLING] = "PHY 0 cooling",
	[MC_CMD_SENSOR_PHY1_TEMP] = "PHY 1 temp. sensor",
	[MC_CMD_SENSOR_PHY1_COOLING] = "PHY 1 cooling",
	[MC_CMD_SENSOR_IN_1V0] = "1.0V supply sensor",
	[MC_CMD_SENSOR_IN_1V2] = "1.2V supply sensor",
	[MC_CMD_SENSOR_IN_1V8] = "1.8V supply sensor",
	[MC_CMD_SENSOR_IN_2V5] = "2.5V supply sensor",
	[MC_CMD_SENSOR_IN_3V3] = "3.3V supply sensor",
	[MC_CMD_SENSOR_IN_12V0] = "12V supply sensor"
};

static const char *sensor_status_names[] = {
	[MC_CMD_SENSOR_STATE_OK] = "OK",
	[MC_CMD_SENSOR_STATE_WARNING] = "Warning",
	[MC_CMD_SENSOR_STATE_FATAL] = "Fatal",
	[MC_CMD_SENSOR_STATE_BROKEN] = "Device failure",
};

static void efx_mcdi_sensor_event(struct efx_nic *efx, efx_qword_t *ev)
{
	unsigned int monitor, state, value;
	const char *name, *state_txt;
	monitor = EFX_QWORD_FIELD(*ev, MCDI_EVENT_SENSOREVT_MONITOR);
	state = EFX_QWORD_FIELD(*ev, MCDI_EVENT_SENSOREVT_STATE);
	value = EFX_QWORD_FIELD(*ev, MCDI_EVENT_SENSOREVT_VALUE);
	/* Deal gracefully with the board having more drivers than we
	 * know about, but do not expect new sensor states. */
	name = (monitor >= ARRAY_SIZE(sensor_names))
				    ? "No sensor name available" :
				    sensor_names[monitor];
	EFX_BUG_ON_PARANOID(state >= ARRAY_SIZE(sensor_status_names));
	state_txt = sensor_status_names[state];

	netif_err(efx, hw, efx->net_dev,
		  "Sensor %d (%s) reports condition '%s' for raw value %d\n",
		  monitor, name, state_txt, value);
}

/* Called from  falcon_process_eventq for MCDI events */
void efx_mcdi_process_event(struct efx_channel *channel,
			    efx_qword_t *event)
{
	struct efx_nic *efx = channel->efx;
	int code = EFX_QWORD_FIELD(*event, MCDI_EVENT_CODE);
	u32 data = EFX_QWORD_FIELD(*event, MCDI_EVENT_DATA);

	switch (code) {
	case MCDI_EVENT_CODE_BADSSERT:
		netif_err(efx, hw, efx->net_dev,
			  "MC watchdog or assertion failure at 0x%x\n", data);
		efx_mcdi_ev_death(efx, EINTR);
		break;

	case MCDI_EVENT_CODE_PMNOTICE:
		netif_info(efx, wol, efx->net_dev, "MCDI PM event.\n");
		break;

	case MCDI_EVENT_CODE_CMDDONE:
		efx_mcdi_ev_cpl(efx,
				MCDI_EVENT_FIELD(*event, CMDDONE_SEQ),
				MCDI_EVENT_FIELD(*event, CMDDONE_DATALEN),
				MCDI_EVENT_FIELD(*event, CMDDONE_ERRNO));
		break;

	case MCDI_EVENT_CODE_LINKCHANGE:
		efx_mcdi_process_link_change(efx, event);
		break;
	case MCDI_EVENT_CODE_SENSOREVT:
		efx_mcdi_sensor_event(efx, event);
		break;
	case MCDI_EVENT_CODE_SCHEDERR:
		netif_info(efx, hw, efx->net_dev,
			   "MC Scheduler error address=0x%x\n", data);
		break;
	case MCDI_EVENT_CODE_REBOOT:
		netif_info(efx, hw, efx->net_dev, "MC Reboot\n");
		efx_mcdi_ev_death(efx, EIO);
		break;
	case MCDI_EVENT_CODE_MAC_STATS_DMA:
		/* MAC stats are gather lazily.  We can ignore this. */
		break;

	default:
		netif_err(efx, hw, efx->net_dev, "Unknown MCDI event 0x%x\n",
			  code);
	}
}

/**************************************************************************
 *
 * Specific request functions
 *
 **************************************************************************
 */

void efx_mcdi_print_fwver(struct efx_nic *efx, char *buf, size_t len)
{
	u8 outbuf[ALIGN(MC_CMD_GET_VERSION_V1_OUT_LEN, 4)];
	size_t outlength;
	const __le16 *ver_words;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_VERSION_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_VERSION, NULL, 0,
			  outbuf, sizeof(outbuf), &outlength);
	if (rc)
		goto fail;

	if (outlength < MC_CMD_GET_VERSION_V1_OUT_LEN) {
		rc = -EIO;
		goto fail;
	}

	ver_words = (__le16 *)MCDI_PTR(outbuf, GET_VERSION_OUT_VERSION);
	snprintf(buf, len, "%u.%u.%u.%u",
		 le16_to_cpu(ver_words[0]), le16_to_cpu(ver_words[1]),
		 le16_to_cpu(ver_words[2]), le16_to_cpu(ver_words[3]));
	return;

fail:
	netif_err(efx, probe, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	buf[0] = 0;
}

int efx_mcdi_drv_attach(struct efx_nic *efx, bool driver_operating,
			bool *was_attached)
{
	u8 inbuf[MC_CMD_DRV_ATTACH_IN_LEN];
	u8 outbuf[MC_CMD_DRV_ATTACH_OUT_LEN];
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, DRV_ATTACH_IN_NEW_STATE,
		       driver_operating ? 1 : 0);
	MCDI_SET_DWORD(inbuf, DRV_ATTACH_IN_UPDATE, 1);

	rc = efx_mcdi_rpc(efx, MC_CMD_DRV_ATTACH, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;
	if (outlen < MC_CMD_DRV_ATTACH_OUT_LEN) {
		rc = -EIO;
		goto fail;
	}

	if (was_attached != NULL)
		*was_attached = MCDI_DWORD(outbuf, DRV_ATTACH_OUT_OLD_STATE);
	return 0;

fail:
	netif_err(efx, probe, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

int efx_mcdi_get_board_cfg(struct efx_nic *efx, u8 *mac_address,
			   u16 *fw_subtype_list)
{
	uint8_t outbuf[MC_CMD_GET_BOARD_CFG_OUT_LEN];
	size_t outlen;
	int port_num = efx_port_num(efx);
	int offset;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_BOARD_CFG_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_BOARD_CFG, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	if (outlen < MC_CMD_GET_BOARD_CFG_OUT_LEN) {
		rc = -EIO;
		goto fail;
	}

	offset = (port_num)
		? MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT1_OFST
		: MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT0_OFST;
	if (mac_address)
		memcpy(mac_address, outbuf + offset, ETH_ALEN);
	if (fw_subtype_list)
		memcpy(fw_subtype_list,
		       outbuf + MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_OFST,
		       MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_LEN);

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d len=%d\n",
		  __func__, rc, (int)outlen);

	return rc;
}

int efx_mcdi_log_ctrl(struct efx_nic *efx, bool evq, bool uart, u32 dest_evq)
{
	u8 inbuf[MC_CMD_LOG_CTRL_IN_LEN];
	u32 dest = 0;
	int rc;

	if (uart)
		dest |= MC_CMD_LOG_CTRL_IN_LOG_DEST_UART;
	if (evq)
		dest |= MC_CMD_LOG_CTRL_IN_LOG_DEST_EVQ;

	MCDI_SET_DWORD(inbuf, LOG_CTRL_IN_LOG_DEST, dest);
	MCDI_SET_DWORD(inbuf, LOG_CTRL_IN_LOG_DEST_EVQ, dest_evq);

	BUILD_BUG_ON(MC_CMD_LOG_CTRL_OUT_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_LOG_CTRL, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	if (rc)
		goto fail;

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

int efx_mcdi_nvram_types(struct efx_nic *efx, u32 *nvram_types_out)
{
	u8 outbuf[MC_CMD_NVRAM_TYPES_OUT_LEN];
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_NVRAM_TYPES_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_NVRAM_TYPES, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;
	if (outlen < MC_CMD_NVRAM_TYPES_OUT_LEN) {
		rc = -EIO;
		goto fail;
	}

	*nvram_types_out = MCDI_DWORD(outbuf, NVRAM_TYPES_OUT_TYPES);
	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n",
		  __func__, rc);
	return rc;
}

int efx_mcdi_nvram_info(struct efx_nic *efx, unsigned int type,
			size_t *size_out, size_t *erase_size_out,
			bool *protected_out)
{
	u8 inbuf[MC_CMD_NVRAM_INFO_IN_LEN];
	u8 outbuf[MC_CMD_NVRAM_INFO_OUT_LEN];
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, NVRAM_INFO_IN_TYPE, type);

	rc = efx_mcdi_rpc(efx, MC_CMD_NVRAM_INFO, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;
	if (outlen < MC_CMD_NVRAM_INFO_OUT_LEN) {
		rc = -EIO;
		goto fail;
	}

	*size_out = MCDI_DWORD(outbuf, NVRAM_INFO_OUT_SIZE);
	*erase_size_out = MCDI_DWORD(outbuf, NVRAM_INFO_OUT_ERASESIZE);
	*protected_out = !!(MCDI_DWORD(outbuf, NVRAM_INFO_OUT_FLAGS) &
				(1 << MC_CMD_NVRAM_PROTECTED_LBN));
	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

int efx_mcdi_nvram_update_start(struct efx_nic *efx, unsigned int type)
{
	u8 inbuf[MC_CMD_NVRAM_UPDATE_START_IN_LEN];
	int rc;

	MCDI_SET_DWORD(inbuf, NVRAM_UPDATE_START_IN_TYPE, type);

	BUILD_BUG_ON(MC_CMD_NVRAM_UPDATE_START_OUT_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_NVRAM_UPDATE_START, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	if (rc)
		goto fail;

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

int efx_mcdi_nvram_read(struct efx_nic *efx, unsigned int type,
			loff_t offset, u8 *buffer, size_t length)
{
	u8 inbuf[MC_CMD_NVRAM_READ_IN_LEN];
	u8 outbuf[MC_CMD_NVRAM_READ_OUT_LEN(EFX_MCDI_NVRAM_LEN_MAX)];
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, NVRAM_READ_IN_TYPE, type);
	MCDI_SET_DWORD(inbuf, NVRAM_READ_IN_OFFSET, offset);
	MCDI_SET_DWORD(inbuf, NVRAM_READ_IN_LENGTH, length);

	rc = efx_mcdi_rpc(efx, MC_CMD_NVRAM_READ, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	memcpy(buffer, MCDI_PTR(outbuf, NVRAM_READ_OUT_READ_BUFFER), length);
	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

int efx_mcdi_nvram_write(struct efx_nic *efx, unsigned int type,
			   loff_t offset, const u8 *buffer, size_t length)
{
	u8 inbuf[MC_CMD_NVRAM_WRITE_IN_LEN(EFX_MCDI_NVRAM_LEN_MAX)];
	int rc;

	MCDI_SET_DWORD(inbuf, NVRAM_WRITE_IN_TYPE, type);
	MCDI_SET_DWORD(inbuf, NVRAM_WRITE_IN_OFFSET, offset);
	MCDI_SET_DWORD(inbuf, NVRAM_WRITE_IN_LENGTH, length);
	memcpy(MCDI_PTR(inbuf, NVRAM_WRITE_IN_WRITE_BUFFER), buffer, length);

	BUILD_BUG_ON(MC_CMD_NVRAM_WRITE_OUT_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_NVRAM_WRITE, inbuf,
			  ALIGN(MC_CMD_NVRAM_WRITE_IN_LEN(length), 4),
			  NULL, 0, NULL);
	if (rc)
		goto fail;

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

int efx_mcdi_nvram_erase(struct efx_nic *efx, unsigned int type,
			 loff_t offset, size_t length)
{
	u8 inbuf[MC_CMD_NVRAM_ERASE_IN_LEN];
	int rc;

	MCDI_SET_DWORD(inbuf, NVRAM_ERASE_IN_TYPE, type);
	MCDI_SET_DWORD(inbuf, NVRAM_ERASE_IN_OFFSET, offset);
	MCDI_SET_DWORD(inbuf, NVRAM_ERASE_IN_LENGTH, length);

	BUILD_BUG_ON(MC_CMD_NVRAM_ERASE_OUT_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_NVRAM_ERASE, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	if (rc)
		goto fail;

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

int efx_mcdi_nvram_update_finish(struct efx_nic *efx, unsigned int type)
{
	u8 inbuf[MC_CMD_NVRAM_UPDATE_FINISH_IN_LEN];
	int rc;

	MCDI_SET_DWORD(inbuf, NVRAM_UPDATE_FINISH_IN_TYPE, type);

	BUILD_BUG_ON(MC_CMD_NVRAM_UPDATE_FINISH_OUT_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_NVRAM_UPDATE_FINISH, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	if (rc)
		goto fail;

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

static int efx_mcdi_nvram_test(struct efx_nic *efx, unsigned int type)
{
	u8 inbuf[MC_CMD_NVRAM_TEST_IN_LEN];
	u8 outbuf[MC_CMD_NVRAM_TEST_OUT_LEN];
	int rc;

	MCDI_SET_DWORD(inbuf, NVRAM_TEST_IN_TYPE, type);

	rc = efx_mcdi_rpc(efx, MC_CMD_NVRAM_TEST, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), NULL);
	if (rc)
		return rc;

	switch (MCDI_DWORD(outbuf, NVRAM_TEST_OUT_RESULT)) {
	case MC_CMD_NVRAM_TEST_PASS:
	case MC_CMD_NVRAM_TEST_NOTSUPP:
		return 0;
	default:
		return -EIO;
	}
}

int efx_mcdi_nvram_test_all(struct efx_nic *efx)
{
	u32 nvram_types;
	unsigned int type;
	int rc;

	rc = efx_mcdi_nvram_types(efx, &nvram_types);
	if (rc)
		goto fail1;

	type = 0;
	while (nvram_types != 0) {
		if (nvram_types & 1) {
			rc = efx_mcdi_nvram_test(efx, type);
			if (rc)
				goto fail2;
		}
		type++;
		nvram_types >>= 1;
	}

	return 0;

fail2:
	netif_err(efx, hw, efx->net_dev, "%s: failed type=%u\n",
		  __func__, type);
fail1:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

static int efx_mcdi_read_assertion(struct efx_nic *efx)
{
	u8 inbuf[MC_CMD_GET_ASSERTS_IN_LEN];
	u8 outbuf[MC_CMD_GET_ASSERTS_OUT_LEN];
	unsigned int flags, index, ofst;
	const char *reason;
	size_t outlen;
	int retry;
	int rc;

	/* Attempt to read any stored assertion state before we reboot
	 * the mcfw out of the assertion handler. Retry twice, once
	 * because a boot-time assertion might cause this command to fail
	 * with EINTR. And once again because GET_ASSERTS can race with
	 * MC_CMD_REBOOT running on the other port. */
	retry = 2;
	do {
		MCDI_SET_DWORD(inbuf, GET_ASSERTS_IN_CLEAR, 1);
		rc = efx_mcdi_rpc(efx, MC_CMD_GET_ASSERTS,
				  inbuf, MC_CMD_GET_ASSERTS_IN_LEN,
				  outbuf, sizeof(outbuf), &outlen);
	} while ((rc == -EINTR || rc == -EIO) && retry-- > 0);

	if (rc)
		return rc;
	if (outlen < MC_CMD_GET_ASSERTS_OUT_LEN)
		return -EIO;

	/* Print out any recorded assertion state */
	flags = MCDI_DWORD(outbuf, GET_ASSERTS_OUT_GLOBAL_FLAGS);
	if (flags == MC_CMD_GET_ASSERTS_FLAGS_NO_FAILS)
		return 0;

	reason = (flags == MC_CMD_GET_ASSERTS_FLAGS_SYS_FAIL)
		? "system-level assertion"
		: (flags == MC_CMD_GET_ASSERTS_FLAGS_THR_FAIL)
		? "thread-level assertion"
		: (flags == MC_CMD_GET_ASSERTS_FLAGS_WDOG_FIRED)
		? "watchdog reset"
		: "unknown assertion";
	netif_err(efx, hw, efx->net_dev,
		  "MCPU %s at PC = 0x%.8x in thread 0x%.8x\n", reason,
		  MCDI_DWORD(outbuf, GET_ASSERTS_OUT_SAVED_PC_OFFS),
		  MCDI_DWORD(outbuf, GET_ASSERTS_OUT_THREAD_OFFS));

	/* Print out the registers */
	ofst = MC_CMD_GET_ASSERTS_OUT_GP_REGS_OFFS_OFST;
	for (index = 1; index < 32; index++) {
		netif_err(efx, hw, efx->net_dev, "R%.2d (?): 0x%.8x\n", index,
			MCDI_DWORD2(outbuf, ofst));
		ofst += sizeof(efx_dword_t);
	}

	return 0;
}

static void efx_mcdi_exit_assertion(struct efx_nic *efx)
{
	u8 inbuf[MC_CMD_REBOOT_IN_LEN];

	/* Atomically reboot the mcfw out of the assertion handler */
	BUILD_BUG_ON(MC_CMD_REBOOT_OUT_LEN != 0);
	MCDI_SET_DWORD(inbuf, REBOOT_IN_FLAGS,
		       MC_CMD_REBOOT_FLAGS_AFTER_ASSERTION);
	efx_mcdi_rpc(efx, MC_CMD_REBOOT, inbuf, MC_CMD_REBOOT_IN_LEN,
		     NULL, 0, NULL);
}

int efx_mcdi_handle_assertion(struct efx_nic *efx)
{
	int rc;

	rc = efx_mcdi_read_assertion(efx);
	if (rc)
		return rc;

	efx_mcdi_exit_assertion(efx);

	return 0;
}

void efx_mcdi_set_id_led(struct efx_nic *efx, enum efx_led_mode mode)
{
	u8 inbuf[MC_CMD_SET_ID_LED_IN_LEN];
	int rc;

	BUILD_BUG_ON(EFX_LED_OFF != MC_CMD_LED_OFF);
	BUILD_BUG_ON(EFX_LED_ON != MC_CMD_LED_ON);
	BUILD_BUG_ON(EFX_LED_DEFAULT != MC_CMD_LED_DEFAULT);

	BUILD_BUG_ON(MC_CMD_SET_ID_LED_OUT_LEN != 0);

	MCDI_SET_DWORD(inbuf, SET_ID_LED_IN_STATE, mode);

	rc = efx_mcdi_rpc(efx, MC_CMD_SET_ID_LED, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	if (rc)
		netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n",
			  __func__, rc);
}

int efx_mcdi_reset_port(struct efx_nic *efx)
{
	int rc = efx_mcdi_rpc(efx, MC_CMD_PORT_RESET, NULL, 0, NULL, 0, NULL);
	if (rc)
		netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n",
			  __func__, rc);
	return rc;
}

int efx_mcdi_reset_mc(struct efx_nic *efx)
{
	u8 inbuf[MC_CMD_REBOOT_IN_LEN];
	int rc;

	BUILD_BUG_ON(MC_CMD_REBOOT_OUT_LEN != 0);
	MCDI_SET_DWORD(inbuf, REBOOT_IN_FLAGS, 0);
	rc = efx_mcdi_rpc(efx, MC_CMD_REBOOT, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	/* White is black, and up is down */
	if (rc == -EIO)
		return 0;
	if (rc == 0)
		rc = -EIO;
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

static int efx_mcdi_wol_filter_set(struct efx_nic *efx, u32 type,
				   const u8 *mac, int *id_out)
{
	u8 inbuf[MC_CMD_WOL_FILTER_SET_IN_LEN];
	u8 outbuf[MC_CMD_WOL_FILTER_SET_OUT_LEN];
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, WOL_FILTER_SET_IN_WOL_TYPE, type);
	MCDI_SET_DWORD(inbuf, WOL_FILTER_SET_IN_FILTER_MODE,
		       MC_CMD_FILTER_MODE_SIMPLE);
	memcpy(MCDI_PTR(inbuf, WOL_FILTER_SET_IN_MAGIC_MAC), mac, ETH_ALEN);

	rc = efx_mcdi_rpc(efx, MC_CMD_WOL_FILTER_SET, inbuf, sizeof(inbuf),
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	if (outlen < MC_CMD_WOL_FILTER_SET_OUT_LEN) {
		rc = -EIO;
		goto fail;
	}

	*id_out = (int)MCDI_DWORD(outbuf, WOL_FILTER_SET_OUT_FILTER_ID);

	return 0;

fail:
	*id_out = -1;
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;

}


int
efx_mcdi_wol_filter_set_magic(struct efx_nic *efx,  const u8 *mac, int *id_out)
{
	return efx_mcdi_wol_filter_set(efx, MC_CMD_WOL_TYPE_MAGIC, mac, id_out);
}


int efx_mcdi_wol_filter_get_magic(struct efx_nic *efx, int *id_out)
{
	u8 outbuf[MC_CMD_WOL_FILTER_GET_OUT_LEN];
	size_t outlen;
	int rc;

	rc = efx_mcdi_rpc(efx, MC_CMD_WOL_FILTER_GET, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	if (outlen < MC_CMD_WOL_FILTER_GET_OUT_LEN) {
		rc = -EIO;
		goto fail;
	}

	*id_out = (int)MCDI_DWORD(outbuf, WOL_FILTER_GET_OUT_FILTER_ID);

	return 0;

fail:
	*id_out = -1;
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}


int efx_mcdi_wol_filter_remove(struct efx_nic *efx, int id)
{
	u8 inbuf[MC_CMD_WOL_FILTER_REMOVE_IN_LEN];
	int rc;

	MCDI_SET_DWORD(inbuf, WOL_FILTER_REMOVE_IN_FILTER_ID, (u32)id);

	rc = efx_mcdi_rpc(efx, MC_CMD_WOL_FILTER_REMOVE, inbuf, sizeof(inbuf),
			  NULL, 0, NULL);
	if (rc)
		goto fail;

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}


int efx_mcdi_wol_filter_reset(struct efx_nic *efx)
{
	int rc;

	rc = efx_mcdi_rpc(efx, MC_CMD_WOL_FILTER_RESET, NULL, 0, NULL, 0, NULL);
	if (rc)
		goto fail;

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

