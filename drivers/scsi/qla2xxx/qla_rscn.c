/*
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include "qla_def.h"

/**
 * IO descriptor handle definitions.
 *
 * Signature form:
 *
 *	|31------28|27-------------------12|11-------0|
 *	|   Type   |   Rolling Signature   |   Index  |
 *	|----------|-----------------------|----------|
 *
 **/

#define HDL_TYPE_SCSI		0
#define HDL_TYPE_ASYNC_IOCB	0x0A

#define HDL_INDEX_BITS	12
#define HDL_ITER_BITS	16
#define HDL_TYPE_BITS	4

#define HDL_INDEX_MASK	((1UL << HDL_INDEX_BITS) - 1)
#define HDL_ITER_MASK	((1UL << HDL_ITER_BITS) - 1)
#define HDL_TYPE_MASK	((1UL << HDL_TYPE_BITS) - 1)

#define HDL_INDEX_SHIFT	0
#define HDL_ITER_SHIFT	(HDL_INDEX_SHIFT + HDL_INDEX_BITS)
#define HDL_TYPE_SHIFT	(HDL_ITER_SHIFT + HDL_ITER_BITS)

/* Local Prototypes. */
static inline uint32_t qla2x00_to_handle(uint16_t, uint16_t, uint16_t);
static inline uint16_t qla2x00_handle_to_idx(uint32_t);
static inline uint32_t qla2x00_iodesc_to_handle(struct io_descriptor *);
static inline struct io_descriptor *qla2x00_handle_to_iodesc(scsi_qla_host_t *,
    uint32_t);

static inline struct io_descriptor *qla2x00_alloc_iodesc(scsi_qla_host_t *);
static inline void qla2x00_free_iodesc(struct io_descriptor *);
static inline void qla2x00_init_io_descriptors(scsi_qla_host_t *);

static void qla2x00_iodesc_timeout(unsigned long);
static inline void qla2x00_add_iodesc_timer(struct io_descriptor *);
static inline void qla2x00_remove_iodesc_timer(struct io_descriptor *);

static inline void qla2x00_update_login_fcport(scsi_qla_host_t *,
    struct mbx_entry *, fc_port_t *);

static int qla2x00_send_abort_iocb(scsi_qla_host_t *, struct io_descriptor *,
    uint32_t, int);
static int qla2x00_send_abort_iocb_cb(scsi_qla_host_t *, struct io_descriptor *,
    struct mbx_entry *);

static int qla2x00_send_adisc_iocb(scsi_qla_host_t *, struct io_descriptor *,
    int);
static int qla2x00_send_adisc_iocb_cb(scsi_qla_host_t *, struct io_descriptor *,
    struct mbx_entry *);

static int qla2x00_send_logout_iocb(scsi_qla_host_t *, struct io_descriptor *,
    int);
static int qla2x00_send_logout_iocb_cb(scsi_qla_host_t *,
    struct io_descriptor *, struct mbx_entry *);

static int qla2x00_send_login_iocb(scsi_qla_host_t *, struct io_descriptor *,
    port_id_t *, int);
static int qla2x00_send_login_iocb_cb(scsi_qla_host_t *, struct io_descriptor *,
    struct mbx_entry *);

/** 
 * Mailbox IOCB callback array.
 **/
static int (*iocb_function_cb_list[LAST_IOCB_CB])
	(scsi_qla_host_t *, struct io_descriptor *, struct mbx_entry *) = {

	qla2x00_send_abort_iocb_cb,
	qla2x00_send_adisc_iocb_cb,
	qla2x00_send_logout_iocb_cb,
	qla2x00_send_login_iocb_cb,
};


/** 
 * Generic IO descriptor handle routines.
 **/

/**
 * qla2x00_to_handle() - Create a descriptor handle.
 * @type: descriptor type
 * @iter: descriptor rolling signature
 * @idx: index to the descriptor array
 *
 * Returns a composite handle based in the @type, @iter, and @idx.
 */
static inline uint32_t
qla2x00_to_handle(uint16_t type, uint16_t iter, uint16_t idx)
{
	return ((uint32_t)(((uint32_t)type << HDL_TYPE_SHIFT) |
	    ((uint32_t)iter << HDL_ITER_SHIFT) |
	    ((uint32_t)idx << HDL_INDEX_SHIFT)));
}

/**
 * qla2x00_handle_to_idx() - Retrive the index for a given handle.
 * @handle: descriptor handle
 *
 * Returns the index specified by the @handle.
 */
static inline uint16_t
qla2x00_handle_to_idx(uint32_t handle)
{
	return ((uint16_t)(((handle) >> HDL_INDEX_SHIFT) & HDL_INDEX_MASK));
}

/**
 * qla2x00_iodesc_to_handle() - Convert an IO descriptor to a unique handle.
 * @iodesc: io descriptor
 *
 * Returns a unique handle for @iodesc.
 */
static inline uint32_t
qla2x00_iodesc_to_handle(struct io_descriptor *iodesc)
{
	uint32_t handle;

	handle = qla2x00_to_handle(HDL_TYPE_ASYNC_IOCB,
	    ++iodesc->ha->iodesc_signature, iodesc->idx);
	iodesc->signature = handle;

	return (handle);
}

/**
 * qla2x00_handle_to_iodesc() - Retrieve an IO descriptor given a unique handle.
 * @ha: HA context
 * @handle: handle to io descriptor
 *
 * Returns a pointer to the io descriptor, or NULL, if the io descriptor does
 * not exist or the io descriptors signature does not @handle.
 */
static inline struct io_descriptor *
qla2x00_handle_to_iodesc(scsi_qla_host_t *ha, uint32_t handle)
{
	uint16_t idx;
	struct io_descriptor *iodesc;

	idx = qla2x00_handle_to_idx(handle);
	iodesc = &ha->io_descriptors[idx];
	if (iodesc)
		if (iodesc->signature != handle)
			iodesc = NULL;

	return (iodesc);
}


/** 
 * IO descriptor allocation routines.
 **/

/**
 * qla2x00_alloc_iodesc() - Allocate an IO descriptor from the pool.
 * @ha: HA context
 *
 * Returns a pointer to the allocated io descriptor, or NULL, if none available.
 */
static inline struct io_descriptor *
qla2x00_alloc_iodesc(scsi_qla_host_t *ha)
{
	uint16_t iter;
	struct io_descriptor *iodesc;

	iodesc = NULL;
	for (iter = 0; iter < MAX_IO_DESCRIPTORS; iter++) {
		if (ha->io_descriptors[iter].used)
			continue;

		iodesc = &ha->io_descriptors[iter];
		iodesc->used = 1;
		iodesc->idx = iter;
		init_timer(&iodesc->timer);
		iodesc->ha = ha;
		iodesc->signature = qla2x00_iodesc_to_handle(iodesc);
		break;
	}

	return (iodesc);
}

/**
 * qla2x00_free_iodesc() - Free an IO descriptor.
 * @iodesc: io descriptor
 *
 * NOTE: The io descriptors timer *must* be stopped before it can be free'd.
 */
static inline void
qla2x00_free_iodesc(struct io_descriptor *iodesc)
{
	iodesc->used = 0;
	iodesc->signature = 0;
}

/**
 * qla2x00_remove_iodesc_timer() - Remove an active timer from an IO descriptor.
 * @iodesc: io descriptor
 */
static inline void
qla2x00_remove_iodesc_timer(struct io_descriptor *iodesc)
{
	if (iodesc->timer.function != NULL) {
		del_timer_sync(&iodesc->timer);
		iodesc->timer.data = (unsigned long) NULL;
		iodesc->timer.function = NULL;
	}
}

/**
 * qla2x00_init_io_descriptors() - Initialize the pool of IO descriptors.
 * @ha: HA context
 */
static inline void
qla2x00_init_io_descriptors(scsi_qla_host_t *ha)
{
	uint16_t iter;

	for (iter = 0; iter < MAX_IO_DESCRIPTORS; iter++) {
		if (!ha->io_descriptors[iter].used)
			continue;

		qla2x00_remove_iodesc_timer(&ha->io_descriptors[iter]);
		qla2x00_free_iodesc(&ha->io_descriptors[iter]);
	}
}


/** 
 * IO descriptor timer routines.
 **/

/**
 * qla2x00_iodesc_timeout() - Timeout IO descriptor handler.
 * @data: io descriptor
 */
static void
qla2x00_iodesc_timeout(unsigned long data)
{
	struct io_descriptor *iodesc;

	iodesc = (struct io_descriptor *) data;

	DEBUG14(printk("scsi(%ld): IO descriptor timeout, index=%x "
	    "signature=%08x, scheduling ISP abort.\n", iodesc->ha->host_no,
	    iodesc->idx, iodesc->signature));

	qla2x00_free_iodesc(iodesc);

	qla_printk(KERN_WARNING, iodesc->ha,
	    "IO descriptor timeout. Scheduling ISP abort.\n");
	set_bit(ISP_ABORT_NEEDED, &iodesc->ha->dpc_flags);
}

/**
 * qla2x00_add_iodesc_timer() - Add and start a timer for an IO descriptor.
 * @iodesc: io descriptor
 *
 * NOTE:
 * The firmware shall timeout an outstanding mailbox IOCB in 2 * R_A_TOV (in
 * tenths of a second) after it hits the wire.  But, if there are any request
 * resource contraints (i.e. during heavy I/O), exchanges can be held off for
 * at most R_A_TOV.  Therefore, the driver will wait 4 * R_A_TOV before
 * scheduling a recovery (big hammer).
 */
static inline void
qla2x00_add_iodesc_timer(struct io_descriptor *iodesc)
{
	unsigned long timeout;

	timeout = (iodesc->ha->r_a_tov * 4) / 10;
	init_timer(&iodesc->timer);
	iodesc->timer.data = (unsigned long) iodesc;
	iodesc->timer.expires = jiffies + (timeout * HZ);
	iodesc->timer.function =
	    (void (*) (unsigned long)) qla2x00_iodesc_timeout;
	add_timer(&iodesc->timer);
}

/** 
 * IO descriptor support routines.
 **/

/**
 * qla2x00_update_login_fcport() - Update fcport data after login processing.
 * @ha: HA context
 * @mbxstat: Mailbox command status IOCB
 * @fcport: port to update
 */
static inline void
qla2x00_update_login_fcport(scsi_qla_host_t *ha, struct mbx_entry *mbxstat,
    fc_port_t *fcport)
{
	if (le16_to_cpu(mbxstat->mb1) & BIT_0) {
		fcport->port_type = FCT_INITIATOR;
	} else {
		fcport->port_type = FCT_TARGET;
		if (le16_to_cpu(mbxstat->mb1) & BIT_1) {
			fcport->flags |= FCF_TAPE_PRESENT;
		}
	}
	fcport->login_retry = 0;
	fcport->port_login_retry_count = ha->port_down_retry_count *
	    PORT_RETRY_TIME;
	atomic_set(&fcport->port_down_timer, ha->port_down_retry_count *
	    PORT_RETRY_TIME);
	fcport->flags |= FCF_FABRIC_DEVICE;
	fcport->flags &= ~FCF_FAILOVER_NEEDED;
	fcport->iodesc_idx_sent = IODESC_INVALID_INDEX;
	atomic_set(&fcport->state, FCS_ONLINE);
}


/** 
 * Mailbox IOCB commands.
 **/

/**
 * qla2x00_get_mbx_iocb_entry() - Retrieve an IOCB from the request queue.
 * @ha: HA context
 * @handle: handle to io descriptor
 *
 * Returns a pointer to the reqest entry, or NULL, if none were available.
 */
static inline struct mbx_entry *
qla2x00_get_mbx_iocb_entry(scsi_qla_host_t *ha, uint32_t handle)
{
	uint16_t cnt;
	device_reg_t __iomem *reg = ha->iobase;
	struct mbx_entry *mbxentry;

	mbxentry = NULL;

	if (ha->req_q_cnt < 3) {
		cnt = qla2x00_debounce_register(ISP_REQ_Q_OUT(ha, reg));
		if  (ha->req_ring_index < cnt)
			ha->req_q_cnt = cnt - ha->req_ring_index;
		else
			ha->req_q_cnt = ha->request_q_length -
			    (ha->req_ring_index - cnt);
	}
	if (ha->req_q_cnt >= 3) {
		mbxentry = (struct mbx_entry *)ha->request_ring_ptr;

		memset(mbxentry, 0, sizeof(struct mbx_entry));
		mbxentry->entry_type = MBX_IOCB_TYPE;
		mbxentry->entry_count = 1;
		mbxentry->sys_define1 = SOURCE_ASYNC_IOCB;
		mbxentry->handle = handle;
	}
	return (mbxentry);
}

/**
 * qla2x00_send_abort_iocb() - Issue an abort IOCB to the firmware.
 * @ha: HA context
 * @iodesc: io descriptor
 * @handle_to_abort: firmware handle to abort
 * @ha_locked: is function called with the hardware lock
 *
 * Returns QLA_SUCCESS if the IOCB was issued.
 */
static int
qla2x00_send_abort_iocb(scsi_qla_host_t *ha, struct io_descriptor *iodesc, 
    uint32_t handle_to_abort, int ha_locked)
{
	unsigned long flags = 0;
	struct mbx_entry *mbxentry;

	/* Send marker if required. */
	if (qla2x00_issue_marker(ha, ha_locked) != QLA_SUCCESS)
		return (QLA_FUNCTION_FAILED);

	if (!ha_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Build abort mailbox IOCB. */
	mbxentry = qla2x00_get_mbx_iocb_entry(ha, iodesc->signature);
	if (mbxentry == NULL) {
		if (!ha_locked)
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

		return (QLA_FUNCTION_FAILED);
	}
	mbxentry->mb0 = __constant_cpu_to_le16(MBC_ABORT_COMMAND);
	mbxentry->mb1 = mbxentry->loop_id.extended =
	    cpu_to_le16(iodesc->remote_fcport->loop_id);
	mbxentry->mb2 = LSW(handle_to_abort);
	mbxentry->mb3 = MSW(handle_to_abort);
	wmb();

	qla2x00_add_iodesc_timer(iodesc);

	/* Issue command to ISP. */
	qla2x00_isp_cmd(ha);

	if (!ha_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

	DEBUG14(printk("scsi(%ld): Sending Abort IOCB (%08x) to [%x], aborting "
	    "%08x.\n", ha->host_no, iodesc->signature,
	    iodesc->remote_fcport->loop_id, handle_to_abort));

	return (QLA_SUCCESS);
}

/**
 * qla2x00_send_abort_iocb_cb() - Abort IOCB callback.
 * @ha: HA context
 * @iodesc: io descriptor
 * @mbxstat: mailbox status IOCB
 *
 * Returns QLA_SUCCESS if @iodesc can be freed by the caller, else, @iodesc
 * will be used for a retry.
 */
static int
qla2x00_send_abort_iocb_cb(scsi_qla_host_t *ha, struct io_descriptor *iodesc,
    struct mbx_entry *mbxstat)
{
	DEBUG14(printk("scsi(%ld): Abort IOCB -- sent to [%x/%02x%02x%02x], "
	    "status=%x mb0=%x.\n", ha->host_no, iodesc->remote_fcport->loop_id,
	    iodesc->d_id.b.domain, iodesc->d_id.b.area, iodesc->d_id.b.al_pa,
	    le16_to_cpu(mbxstat->status), le16_to_cpu(mbxstat->mb0)));

	return (QLA_SUCCESS);
}


/**
 * qla2x00_send_adisc_iocb() - Issue a Get Port Database IOCB to the firmware.
 * @ha: HA context
 * @iodesc: io descriptor
 * @ha_locked: is function called with the hardware lock
 *
 * Returns QLA_SUCCESS if the IOCB was issued.
 */
static int
qla2x00_send_adisc_iocb(scsi_qla_host_t *ha, struct io_descriptor *iodesc,
    int ha_locked)
{
	unsigned long flags = 0;
	struct mbx_entry *mbxentry;

	/* Send marker if required. */
	if (qla2x00_issue_marker(ha, ha_locked) != QLA_SUCCESS)
		return (QLA_FUNCTION_FAILED);

	if (!ha_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Build Get Port Database IOCB. */
	mbxentry = qla2x00_get_mbx_iocb_entry(ha, iodesc->signature);
	if (mbxentry == NULL) {
		if (!ha_locked)
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

		return (QLA_FUNCTION_FAILED);
	}
	mbxentry->mb0 = __constant_cpu_to_le16(MBC_GET_PORT_DATABASE);
	mbxentry->mb1 = mbxentry->loop_id.extended =
	    cpu_to_le16(iodesc->remote_fcport->loop_id);
	mbxentry->mb2 = cpu_to_le16(MSW(LSD(ha->iodesc_pd_dma)));
	mbxentry->mb3 = cpu_to_le16(LSW(LSD(ha->iodesc_pd_dma)));
	mbxentry->mb6 = cpu_to_le16(MSW(MSD(ha->iodesc_pd_dma)));
	mbxentry->mb7 = cpu_to_le16(LSW(MSD(ha->iodesc_pd_dma)));
	mbxentry->mb10 = __constant_cpu_to_le16(BIT_0);
	wmb();

	qla2x00_add_iodesc_timer(iodesc);

	/* Issue command to ISP. */
	qla2x00_isp_cmd(ha);

	if (!ha_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

	DEBUG14(printk("scsi(%ld): Sending Adisc IOCB (%08x) to [%x].\n",
	    ha->host_no, iodesc->signature, iodesc->remote_fcport->loop_id));

	return (QLA_SUCCESS);
}

/**
 * qla2x00_send_adisc_iocb_cb() - Get Port Database IOCB callback.
 * @ha: HA context
 * @iodesc: io descriptor
 * @mbxstat: mailbox status IOCB
 *
 * Returns QLA_SUCCESS if @iodesc can be freed by the caller, else, @iodesc
 * will be used for a retry.
 */
static int
qla2x00_send_adisc_iocb_cb(scsi_qla_host_t *ha, struct io_descriptor *iodesc,
    struct mbx_entry *mbxstat)
{
	fc_port_t *remote_fcport;

	remote_fcport = iodesc->remote_fcport;

	/* Ensure the port IDs are consistent. */
	if (remote_fcport->d_id.b24 != iodesc->d_id.b24) {
		DEBUG14(printk("scsi(%ld): Adisc IOCB -- ignoring, remote port "
		    "id changed from [%02x%02x%02x] to [%02x%02x%02x].\n",
		    ha->host_no, remote_fcport->d_id.b.domain,
		    remote_fcport->d_id.b.area, remote_fcport->d_id.b.al_pa,
		    iodesc->d_id.b.domain, iodesc->d_id.b.area,
		    iodesc->d_id.b.al_pa));

		return (QLA_SUCCESS);
	}

	/* Only process the last command. */
	if (remote_fcport->iodesc_idx_sent != iodesc->idx) {
		DEBUG14(printk("scsi(%ld): Adisc IOCB -- ignoring, sent to "
		    "[%02x%02x%02x], expected %x, received %x.\n", ha->host_no,
		    iodesc->d_id.b.domain, iodesc->d_id.b.area,
		    iodesc->d_id.b.al_pa, remote_fcport->iodesc_idx_sent,
		    iodesc->idx));

		return (QLA_SUCCESS);
	}

	if (le16_to_cpu(mbxstat->status) == CS_COMPLETE) {
		DEBUG14(printk("scsi(%ld): Adisc IOCB -- marking "
		    "[%x/%02x%02x%02x] online.\n", ha->host_no,
		    remote_fcport->loop_id, remote_fcport->d_id.b.domain,
		    remote_fcport->d_id.b.area, remote_fcport->d_id.b.al_pa));

		atomic_set(&remote_fcport->state, FCS_ONLINE);
	} else {
		DEBUG14(printk("scsi(%ld): Adisc IOCB -- marking "
		    "[%x/%02x%02x%02x] lost, status=%x mb0=%x.\n", ha->host_no,
		    remote_fcport->loop_id, remote_fcport->d_id.b.domain,
		    remote_fcport->d_id.b.area, remote_fcport->d_id.b.al_pa,
		    le16_to_cpu(mbxstat->status), le16_to_cpu(mbxstat->mb0)));

		if (atomic_read(&remote_fcport->state) != FCS_DEVICE_DEAD)
			atomic_set(&remote_fcport->state, FCS_DEVICE_LOST);
	}
	remote_fcport->iodesc_idx_sent = IODESC_INVALID_INDEX;

	return (QLA_SUCCESS);
}


/**
 * qla2x00_send_logout_iocb() - Issue a fabric port logout IOCB to the firmware.
 * @ha: HA context
 * @iodesc: io descriptor
 * @ha_locked: is function called with the hardware lock
 *
 * Returns QLA_SUCCESS if the IOCB was issued.
 */
static int
qla2x00_send_logout_iocb(scsi_qla_host_t *ha, struct io_descriptor *iodesc,
    int ha_locked)
{
	unsigned long flags = 0;
	struct mbx_entry *mbxentry;

	/* Send marker if required. */
	if (qla2x00_issue_marker(ha, ha_locked) != QLA_SUCCESS)
		return (QLA_FUNCTION_FAILED);

	if (!ha_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Build fabric port logout mailbox IOCB. */
	mbxentry = qla2x00_get_mbx_iocb_entry(ha, iodesc->signature);
	if (mbxentry == NULL) {
		if (!ha_locked)
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

		return (QLA_FUNCTION_FAILED);
	}
	mbxentry->mb0 = __constant_cpu_to_le16(MBC_LOGOUT_FABRIC_PORT);
	mbxentry->mb1 = mbxentry->loop_id.extended =
	    cpu_to_le16(iodesc->remote_fcport->loop_id);
	wmb();

	qla2x00_add_iodesc_timer(iodesc);

	/* Issue command to ISP. */
	qla2x00_isp_cmd(ha);

	if (!ha_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

	DEBUG14(printk("scsi(%ld): Sending Logout IOCB (%08x) to [%x].\n",
	    ha->host_no, iodesc->signature, iodesc->remote_fcport->loop_id));

	return (QLA_SUCCESS);
}

/**
 * qla2x00_send_logout_iocb_cb() - Fabric port logout IOCB callback.
 * @ha: HA context
 * @iodesc: io descriptor
 * @mbxstat: mailbox status IOCB
 *
 * Returns QLA_SUCCESS if @iodesc can be freed by the caller, else, @iodesc
 * will be used for a retry.
 */
static int
qla2x00_send_logout_iocb_cb(scsi_qla_host_t *ha, struct io_descriptor *iodesc,
    struct mbx_entry *mbxstat)
{
	DEBUG14(printk("scsi(%ld): Logout IOCB -- sent to [%x/%02x%02x%02x], "
	    "status=%x mb0=%x mb1=%x.\n", ha->host_no,
	    iodesc->remote_fcport->loop_id,
	    iodesc->remote_fcport->d_id.b.domain,
	    iodesc->remote_fcport->d_id.b.area,
	    iodesc->remote_fcport->d_id.b.al_pa, le16_to_cpu(mbxstat->status),
	    le16_to_cpu(mbxstat->mb0), le16_to_cpu(mbxstat->mb1)));

	return (QLA_SUCCESS);
}


/**
 * qla2x00_send_login_iocb() - Issue a fabric port login IOCB to the firmware.
 * @ha: HA context
 * @iodesc: io descriptor
 * @d_id: port id for device
 * @ha_locked: is function called with the hardware lock
 *
 * Returns QLA_SUCCESS if the IOCB was issued.
 */
static int
qla2x00_send_login_iocb(scsi_qla_host_t *ha, struct io_descriptor *iodesc,
    port_id_t *d_id, int ha_locked)
{
	unsigned long flags = 0;
	struct mbx_entry *mbxentry;

	/* Send marker if required. */
	if (qla2x00_issue_marker(ha, ha_locked) != QLA_SUCCESS)
		return (QLA_FUNCTION_FAILED);

	if (!ha_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Build fabric port login mailbox IOCB. */
	mbxentry = qla2x00_get_mbx_iocb_entry(ha, iodesc->signature);
	if (mbxentry == NULL) {
		if (!ha_locked)
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

		return (QLA_FUNCTION_FAILED);
	}
	mbxentry->mb0 = __constant_cpu_to_le16(MBC_LOGIN_FABRIC_PORT);
	mbxentry->mb1 = mbxentry->loop_id.extended =
	    cpu_to_le16(iodesc->remote_fcport->loop_id);
	mbxentry->mb2 = cpu_to_le16(d_id->b.domain);
	mbxentry->mb3 = cpu_to_le16(d_id->b.area << 8 | d_id->b.al_pa);
	mbxentry->mb10 = __constant_cpu_to_le16(BIT_0);
	wmb();

	qla2x00_add_iodesc_timer(iodesc);

	/* Issue command to ISP. */
	qla2x00_isp_cmd(ha);

	if (!ha_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

	DEBUG14(printk("scsi(%ld): Sending Login IOCB (%08x) to "
	    "[%x/%02x%02x%02x].\n", ha->host_no, iodesc->signature,
	    iodesc->remote_fcport->loop_id, d_id->b.domain, d_id->b.area,
	    d_id->b.al_pa));

	return (QLA_SUCCESS);
}

/**
 * qla2x00_send_login_iocb_cb() - Fabric port logout IOCB callback.
 * @ha: HA context
 * @iodesc: io descriptor
 * @mbxstat: mailbox status IOCB
 *
 * Returns QLA_SUCCESS if @iodesc can be freed by the caller, else, @iodesc
 * will be used for a retry.
 */
static int
qla2x00_send_login_iocb_cb(scsi_qla_host_t *ha, struct io_descriptor *iodesc,
    struct mbx_entry *mbxstat)
{
	int rval;
	fc_port_t *fcport, *remote_fcport, *exist_fcport;
	struct io_descriptor *abort_iodesc, *login_iodesc;
	uint16_t status, mb[8];
	uint16_t reuse;
	uint16_t remote_loopid;
	port_id_t remote_did, inuse_did;

	remote_fcport = iodesc->remote_fcport;

	/* Only process the last command. */
	if (remote_fcport->iodesc_idx_sent != iodesc->idx) {
		DEBUG14(printk("scsi(%ld): Login IOCB -- ignoring, sent to "
		    "[%02x%02x%02x], expected %x, received %x.\n", 
		    ha->host_no, iodesc->d_id.b.domain, iodesc->d_id.b.area,
		    iodesc->d_id.b.al_pa, remote_fcport->iodesc_idx_sent,
		    iodesc->idx));

		/* Free RSCN fcport resources. */
		if (remote_fcport->port_type == FCT_RSCN) {
			DEBUG14(printk("scsi(%ld): Login IOCB -- Freeing RSCN "
			    "fcport %p [%x/%02x%02x%02x] given ignored Login "
			    "IOCB.\n", ha->host_no, remote_fcport,
			    remote_fcport->loop_id,
			    remote_fcport->d_id.b.domain,
			    remote_fcport->d_id.b.area,
			    remote_fcport->d_id.b.al_pa));

			list_del(&remote_fcport->list);
			kfree(remote_fcport);
		}
		return (QLA_SUCCESS);
	}

	status = le16_to_cpu(mbxstat->status);
	mb[0] = le16_to_cpu(mbxstat->mb0);
	mb[1] = le16_to_cpu(mbxstat->mb1);
	mb[2] = le16_to_cpu(mbxstat->mb2);
	mb[6] = le16_to_cpu(mbxstat->mb6);
	mb[7] = le16_to_cpu(mbxstat->mb7);

	/* Good status? */
	if ((status == CS_COMPLETE || status == CS_COMPLETE_CHKCOND) &&
	    mb[0] == MBS_COMMAND_COMPLETE) {

		DEBUG14(printk("scsi(%ld): Login IOCB -- status=%x mb1=%x pn="
		    "%02x%02x%02x%02x%02x%02x%02x%02x.\n", ha->host_no, status,
		    mb[1], mbxstat->port_name[0], mbxstat->port_name[1], 
		    mbxstat->port_name[2], mbxstat->port_name[3], 
		    mbxstat->port_name[4], mbxstat->port_name[5], 
		    mbxstat->port_name[6], mbxstat->port_name[7]));

		memcpy(remote_fcport->node_name, mbxstat->node_name, WWN_SIZE);
		memcpy(remote_fcport->port_name, mbxstat->port_name, WWN_SIZE);

		/* Is the device already in our fcports list? */
		if (remote_fcport->port_type != FCT_RSCN) {
			DEBUG14(printk("scsi(%ld): Login IOCB -- marking "
			    "[%x/%02x%02x%02x] online.\n", ha->host_no,
			    remote_fcport->loop_id,
			    remote_fcport->d_id.b.domain,
			    remote_fcport->d_id.b.area,
			    remote_fcport->d_id.b.al_pa));

			qla2x00_update_login_fcport(ha, mbxstat, remote_fcport);

			return (QLA_SUCCESS);
		}

		/* Does the RSCN portname already exist in our fcports list? */
		exist_fcport = NULL;
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (memcmp(remote_fcport->port_name, fcport->port_name,
			    WWN_SIZE) == 0) {
				exist_fcport = fcport;
				break;
			}
		}
		if (exist_fcport != NULL) {
			DEBUG14(printk("scsi(%ld): Login IOCB -- found RSCN "
			    "fcport in fcports list [%p].\n", ha->host_no,
			    exist_fcport));

			/* Abort any ADISC that could have been sent. */
			if (exist_fcport->iodesc_idx_sent != iodesc->idx &&
			    exist_fcport->iodesc_idx_sent <
			    MAX_IO_DESCRIPTORS &&
			    ha->io_descriptors[exist_fcport->iodesc_idx_sent].
			    cb_idx == ADISC_PORT_IOCB_CB) {

				abort_iodesc = qla2x00_alloc_iodesc(ha);
				if (abort_iodesc) {
					DEBUG14(printk("scsi(%ld): Login IOCB "
					    "-- issuing abort to outstanding "
					    "Adisc [%x/%02x%02x%02x].\n",
					    ha->host_no, remote_fcport->loop_id,
					    exist_fcport->d_id.b.domain,
					    exist_fcport->d_id.b.area,
					    exist_fcport->d_id.b.al_pa));

					abort_iodesc->cb_idx = ABORT_IOCB_CB;
					abort_iodesc->d_id.b24 =
					    exist_fcport->d_id.b24;
					abort_iodesc->remote_fcport =
					    exist_fcport;
					exist_fcport->iodesc_idx_sent =
					    abort_iodesc->idx;
					qla2x00_send_abort_iocb(ha,
					    abort_iodesc, ha->io_descriptors[
					     exist_fcport->iodesc_idx_sent].
					      signature, 1);
				} else {
					DEBUG14(printk("scsi(%ld): Login IOCB "
					    "-- unable to abort outstanding "
					    "Adisc [%x/%02x%02x%02x].\n",
					    ha->host_no, remote_fcport->loop_id,
					    exist_fcport->d_id.b.domain,
					    exist_fcport->d_id.b.area,
					    exist_fcport->d_id.b.al_pa));
				}
			}

			/*
			 * If the existing fcport is waiting to send an ADISC
			 * or LOGIN, then reuse remote fcport (RSCN) to
			 * continue waiting.
			 */
			reuse = 0;
			remote_loopid = remote_fcport->loop_id;
			remote_did.b24 = remote_fcport->d_id.b24;
			if (exist_fcport->iodesc_idx_sent ==
			    IODESC_ADISC_NEEDED ||
			    exist_fcport->iodesc_idx_sent ==
			    IODESC_LOGIN_NEEDED) {
				DEBUG14(printk("scsi(%ld): Login IOCB -- "
				    "existing fcport [%x/%02x%02x%02x] "
				    "waiting for IO descriptor, reuse RSCN "
				    "fcport.\n", ha->host_no,
				    exist_fcport->loop_id,
				    exist_fcport->d_id.b.domain,
				    exist_fcport->d_id.b.area,
				    exist_fcport->d_id.b.al_pa));

				reuse++;
				remote_fcport->iodesc_idx_sent =
				    exist_fcport->iodesc_idx_sent;
				exist_fcport->iodesc_idx_sent =
				    IODESC_INVALID_INDEX;
				remote_fcport->loop_id = exist_fcport->loop_id;
				remote_fcport->d_id.b24 =
				    exist_fcport->d_id.b24;
			}

			/* Logout the old loopid. */
			if (!reuse &&
			    exist_fcport->loop_id != remote_fcport->loop_id &&
			    exist_fcport->loop_id != FC_NO_LOOP_ID) {
				login_iodesc = qla2x00_alloc_iodesc(ha);
				if (login_iodesc) {
					DEBUG14(printk("scsi(%ld): Login IOCB "
					    "-- issuing logout to free old "
					    "loop id [%x/%02x%02x%02x].\n",
					    ha->host_no, exist_fcport->loop_id,
					    exist_fcport->d_id.b.domain,
					    exist_fcport->d_id.b.area,
					    exist_fcport->d_id.b.al_pa));

					login_iodesc->cb_idx =
					    LOGOUT_PORT_IOCB_CB;
					login_iodesc->d_id.b24 =
					    exist_fcport->d_id.b24;
					login_iodesc->remote_fcport =
					    exist_fcport;
					exist_fcport->iodesc_idx_sent =
					    login_iodesc->idx;
					qla2x00_send_logout_iocb(ha,
					    login_iodesc, 1);
				} else {
					/* Ran out of IO descriptiors. */
					DEBUG14(printk("scsi(%ld): Login IOCB "
					    "-- unable to logout to free old "
					    "loop id [%x/%02x%02x%02x].\n",
					    ha->host_no, exist_fcport->loop_id,
					    exist_fcport->d_id.b.domain,
					    exist_fcport->d_id.b.area,
					    exist_fcport->d_id.b.al_pa));

					exist_fcport->iodesc_idx_sent =
					    IODESC_INVALID_INDEX;
				}

			}

			/* Update existing fcport with remote fcport info. */
			DEBUG14(printk("scsi(%ld): Login IOCB -- marking "
			    "existing fcport [%x/%02x%02x%02x] online.\n",
			    ha->host_no, remote_loopid, remote_did.b.domain,
			    remote_did.b.area, remote_did.b.al_pa));

			memcpy(exist_fcport->node_name,
			    remote_fcport->node_name, WWN_SIZE);
			exist_fcport->loop_id = remote_loopid;
			exist_fcport->d_id.b24 = remote_did.b24;
			qla2x00_update_login_fcport(ha, mbxstat, exist_fcport);

			/* Finally, free the remote (RSCN) fcport. */
			if (!reuse) {
				DEBUG14(printk("scsi(%ld): Login IOCB -- "
				    "Freeing RSCN fcport %p "
				    "[%x/%02x%02x%02x].\n", ha->host_no,
				    remote_fcport, remote_fcport->loop_id,
				    remote_fcport->d_id.b.domain,
				    remote_fcport->d_id.b.area,
				    remote_fcport->d_id.b.al_pa));

				list_del(&remote_fcport->list);
				kfree(remote_fcport);
			}

			return (QLA_SUCCESS);
		}

		/*
		 * A new device has been added, move the RSCN fcport to our
		 * fcports list.
		 */
		DEBUG14(printk("scsi(%ld): Login IOCB -- adding RSCN fcport "
		    "[%x/%02x%02x%02x] to fcports list.\n", ha->host_no,
		    remote_fcport->loop_id, remote_fcport->d_id.b.domain,
		    remote_fcport->d_id.b.area, remote_fcport->d_id.b.al_pa));

		list_del(&remote_fcport->list);
		remote_fcport->flags = (FCF_RLC_SUPPORT | FCF_RESCAN_NEEDED);
		qla2x00_update_login_fcport(ha, mbxstat, remote_fcport);
		list_add_tail(&remote_fcport->list, &ha->fcports);
		set_bit(FCPORT_RESCAN_NEEDED, &ha->dpc_flags);
	} else {
		/* Handle login failure. */
		if (remote_fcport->login_retry != 0) {
			if (mb[0] == MBS_LOOP_ID_USED) {
				inuse_did.b.domain = LSB(mb[1]);
				inuse_did.b.area = MSB(mb[2]);
				inuse_did.b.al_pa = LSB(mb[2]);

				DEBUG14(printk("scsi(%ld): Login IOCB -- loop "
				    "id [%x] used by port id [%02x%02x%02x].\n",
				    ha->host_no, remote_fcport->loop_id,
				    inuse_did.b.domain, inuse_did.b.area,
				    inuse_did.b.al_pa));

				if (remote_fcport->d_id.b24 ==
				    INVALID_PORT_ID) {
					/*
					 * Invalid port id means we are trying
					 * to login to a remote port with just
					 * a loop id without knowing about the
					 * port id.  Copy the port id and try
					 * again.
					 */
					remote_fcport->d_id.b24 = inuse_did.b24;
					iodesc->d_id.b24 = inuse_did.b24;
				} else {
					remote_fcport->loop_id++;
					rval = qla2x00_find_new_loop_id(ha,
					    remote_fcport);
					if (rval == QLA_FUNCTION_FAILED) {
						/* No more loop ids. */
						return (QLA_SUCCESS);
					}
				}
			} else if (mb[0] == MBS_PORT_ID_USED) {
				/*
				 * Device has another loop ID.  The firmware
				 * group recommends the driver perform an
				 * implicit login with the specified ID.
				 */
				DEBUG14(printk("scsi(%ld): Login IOCB -- port "
				    "id [%02x%02x%02x] already assigned to "
				    "loop id [%x].\n", ha->host_no,
				    iodesc->d_id.b.domain, iodesc->d_id.b.area,
				    iodesc->d_id.b.al_pa, mb[1]));

				remote_fcport->loop_id = mb[1];

			} else {
				/* Unable to perform login, try again. */
				DEBUG14(printk("scsi(%ld): Login IOCB -- "
				    "failed login [%x/%02x%02x%02x], status=%x "
				    "mb0=%x mb1=%x mb2=%x mb6=%x mb7=%x.\n",
				    ha->host_no, remote_fcport->loop_id,
				    iodesc->d_id.b.domain, iodesc->d_id.b.area,
				    iodesc->d_id.b.al_pa, status, mb[0], mb[1],
				    mb[2], mb[6], mb[7]));
			}

			/* Reissue Login with the same IO descriptor. */
			iodesc->signature =
			    qla2x00_iodesc_to_handle(iodesc);
			iodesc->cb_idx = LOGIN_PORT_IOCB_CB;
			iodesc->d_id.b24 = remote_fcport->d_id.b24;
			remote_fcport->iodesc_idx_sent = iodesc->idx;
			remote_fcport->login_retry--;

			DEBUG14(printk("scsi(%ld): Login IOCB -- retrying "
			    "login to [%x/%02x%02x%02x] (%d).\n", ha->host_no,
			    remote_fcport->loop_id,
			    remote_fcport->d_id.b.domain,
			    remote_fcport->d_id.b.area,
			    remote_fcport->d_id.b.al_pa,
			    remote_fcport->login_retry));

			qla2x00_send_login_iocb(ha, iodesc,
			    &remote_fcport->d_id, 1);

			return (QLA_FUNCTION_FAILED);
		} else {
			/* No more logins, mark device dead. */
			DEBUG14(printk("scsi(%ld): Login IOCB -- failed "
			    "login [%x/%02x%02x%02x] after retries, status=%x "
			    "mb0=%x mb1=%x mb2=%x mb6=%x mb7=%x.\n",
			    ha->host_no, remote_fcport->loop_id,
			    iodesc->d_id.b.domain, iodesc->d_id.b.area,
			    iodesc->d_id.b.al_pa, status, mb[0], mb[1],
			    mb[2], mb[6], mb[7]));

			atomic_set(&remote_fcport->state, FCS_DEVICE_DEAD);
			if (remote_fcport->port_type == FCT_RSCN) {
				DEBUG14(printk("scsi(%ld): Login IOCB -- "
				    "Freeing dead RSCN fcport %p "
				    "[%x/%02x%02x%02x].\n", ha->host_no,
				    remote_fcport, remote_fcport->loop_id,
				    remote_fcport->d_id.b.domain,
				    remote_fcport->d_id.b.area,
				    remote_fcport->d_id.b.al_pa));

				list_del(&remote_fcport->list);
				kfree(remote_fcport);
			}
		}
	}

	return (QLA_SUCCESS);
}


/** 
 * IO descriptor processing routines.
 **/

/**
 * qla2x00_alloc_rscn_fcport() - Allocate an RSCN type fcport.
 * @ha: HA context
 * @flags: allocation flags
 *
 * Returns a pointer to the allocated RSCN fcport, or NULL, if none available.
 */
fc_port_t *
qla2x00_alloc_rscn_fcport(scsi_qla_host_t *ha, int flags)
{
	fc_port_t *fcport;

	fcport = qla2x00_alloc_fcport(ha, flags);
	if (fcport == NULL)
		return (fcport);

	/* Setup RSCN fcport structure. */
	fcport->port_type = FCT_RSCN;

	return (fcport);
}

/**
 * qla2x00_handle_port_rscn() - Handle port RSCN.
 * @ha: HA context
 * @rscn_entry: RSCN entry
 * @fcport: fcport entry to updated
 *
 * Returns QLA_SUCCESS if the port RSCN was handled.
 */
int
qla2x00_handle_port_rscn(scsi_qla_host_t *ha, uint32_t rscn_entry,
    fc_port_t *known_fcport, int ha_locked)
{
	int	rval;
	port_id_t rscn_pid;
	fc_port_t *fcport, *remote_fcport, *rscn_fcport;
	struct io_descriptor *iodesc;

	remote_fcport = NULL;
	rscn_fcport = NULL;

	/* Prepare port id based on incoming entries. */
	if (known_fcport) {
		rscn_pid.b24 = known_fcport->d_id.b24;
		remote_fcport = known_fcport;

		DEBUG14(printk("scsi(%ld): Handle RSCN -- process RSCN for "
		    "fcport [%02x%02x%02x].\n", ha->host_no,
		    remote_fcport->d_id.b.domain, remote_fcport->d_id.b.area,
		    remote_fcport->d_id.b.al_pa));
	} else {
		rscn_pid.b.domain = LSB(MSW(rscn_entry));
		rscn_pid.b.area = MSB(LSW(rscn_entry));
		rscn_pid.b.al_pa = LSB(LSW(rscn_entry));

		DEBUG14(printk("scsi(%ld): Handle RSCN -- process RSCN for "
		    "port id [%02x%02x%02x].\n", ha->host_no,
		    rscn_pid.b.domain, rscn_pid.b.area, rscn_pid.b.al_pa));

		/*
		 * Search fcport lists for a known entry at the specified port
		 * ID.
		 */
		list_for_each_entry(fcport, &ha->fcports, list) {
		    if (rscn_pid.b24 == fcport->d_id.b24) {
			    remote_fcport = fcport;
			    break;
		    }
		}
		list_for_each_entry(fcport, &ha->rscn_fcports, list) {
		    if (rscn_pid.b24 == fcport->d_id.b24) {
			    rscn_fcport = fcport;
			    break;
		    }
		}
		if (remote_fcport == NULL)
		    remote_fcport = rscn_fcport;
	}

	/* 
	 * If the port is already in our fcport list and online, send an ADISC
	 * to see if it's still alive.  Issue login if a new fcport or the known
	 * fcport is currently offline.
	 */
	if (remote_fcport) {
		/*
		 * No need to send request if the remote fcport is currently
		 * waiting for an available io descriptor.
		 */
		if (known_fcport == NULL &&
		    (remote_fcport->iodesc_idx_sent == IODESC_ADISC_NEEDED ||
		    remote_fcport->iodesc_idx_sent == IODESC_LOGIN_NEEDED)) {
			/*
			 * If previous waiting io descriptor is an ADISC, then
			 * the new RSCN may come from a new remote fcport being
			 * plugged into the same location.
			 */
			if (remote_fcport->port_type == FCT_RSCN) {
			    remote_fcport->iodesc_idx_sent =
				IODESC_LOGIN_NEEDED;
			} else if (remote_fcport->iodesc_idx_sent ==
			    IODESC_ADISC_NEEDED) {
				fc_port_t *new_fcport;

				remote_fcport->iodesc_idx_sent =
				    IODESC_INVALID_INDEX;

				/* Create new fcport for later login. */
				new_fcport = qla2x00_alloc_rscn_fcport(ha,
				    ha_locked ? GFP_ATOMIC: GFP_KERNEL);
				if (new_fcport) {
					DEBUG14(printk("scsi(%ld): Handle RSCN "
					    "-- creating RSCN fcport %p for "
					    "future login.\n", ha->host_no,
					    new_fcport));

					new_fcport->d_id.b24 =
					    remote_fcport->d_id.b24;
					new_fcport->iodesc_idx_sent =
					    IODESC_LOGIN_NEEDED;

					list_add_tail(&new_fcport->list,
					    &ha->rscn_fcports);
					set_bit(IODESC_PROCESS_NEEDED,
					    &ha->dpc_flags);
				} else {
					DEBUG14(printk("scsi(%ld): Handle RSCN "
					    "-- unable to allocate RSCN fcport "
					    "for future login.\n",
					    ha->host_no));
				}
			}
			return (QLA_SUCCESS);
		}
		
		/* Send ADISC if the fcport is online */
		if (atomic_read(&remote_fcport->state) == FCS_ONLINE ||
		    remote_fcport->iodesc_idx_sent == IODESC_ADISC_NEEDED) {

			atomic_set(&remote_fcport->state, FCS_DEVICE_LOST);

			iodesc = qla2x00_alloc_iodesc(ha);
			if (iodesc == NULL) {
				/* Mark fcport for later adisc processing */
				DEBUG14(printk("scsi(%ld): Handle RSCN -- not "
				    "enough IO descriptors for Adisc, flag "
				    "for later processing.\n", ha->host_no));

				remote_fcport->iodesc_idx_sent =
				    IODESC_ADISC_NEEDED;
				set_bit(IODESC_PROCESS_NEEDED, &ha->dpc_flags);

				return (QLA_SUCCESS);
			}

			iodesc->cb_idx = ADISC_PORT_IOCB_CB;
			iodesc->d_id.b24 = rscn_pid.b24;
			iodesc->remote_fcport = remote_fcport;
			remote_fcport->iodesc_idx_sent = iodesc->idx;
			qla2x00_send_adisc_iocb(ha, iodesc, ha_locked);

			return (QLA_SUCCESS);
		} else if (remote_fcport->iodesc_idx_sent <
		    MAX_IO_DESCRIPTORS &&
		    ha->io_descriptors[remote_fcport->iodesc_idx_sent].cb_idx ==
		    ADISC_PORT_IOCB_CB) {
			/*
			 * Receiving another RSCN while an ADISC is pending,
			 * abort the IOCB.  Use the same descriptor for the
			 * abort.
			 */
			uint32_t handle_to_abort;
			
			iodesc = &ha->io_descriptors[
				remote_fcport->iodesc_idx_sent];
			qla2x00_remove_iodesc_timer(iodesc);
			handle_to_abort = iodesc->signature;
			iodesc->signature = qla2x00_iodesc_to_handle(iodesc);
			iodesc->cb_idx = ABORT_IOCB_CB;
			iodesc->d_id.b24 = remote_fcport->d_id.b24;
			iodesc->remote_fcport = remote_fcport;
			remote_fcport->iodesc_idx_sent = iodesc->idx;

			DEBUG14(printk("scsi(%ld): Handle RSCN -- issuing "
			    "abort to outstanding Adisc [%x/%02x%02x%02x].\n",
			    ha->host_no, remote_fcport->loop_id,
			    iodesc->d_id.b.domain, iodesc->d_id.b.area,
			    iodesc->d_id.b.al_pa));

			qla2x00_send_abort_iocb(ha, iodesc, handle_to_abort,
			    ha_locked);
		}
	}

	/* We need to login to the remote port, find it. */
	if (known_fcport) {
		remote_fcport = known_fcport;
	} else if (rscn_fcport && rscn_fcport->d_id.b24 != INVALID_PORT_ID &&
	    rscn_fcport->iodesc_idx_sent < MAX_IO_DESCRIPTORS &&
	    ha->io_descriptors[rscn_fcport->iodesc_idx_sent].cb_idx ==
	    LOGIN_PORT_IOCB_CB) {
		/*
		 * Ignore duplicate RSCN on fcport which has already
		 * initiated a login IOCB.
		 */
		DEBUG14(printk("scsi(%ld): Handle RSCN -- ignoring, login "
		    "already sent to [%02x%02x%02x].\n", ha->host_no,
		    rscn_fcport->d_id.b.domain, rscn_fcport->d_id.b.area,
		    rscn_fcport->d_id.b.al_pa));

		return (QLA_SUCCESS);
	} else if (rscn_fcport && rscn_fcport->d_id.b24 != INVALID_PORT_ID &&
	    rscn_fcport != remote_fcport) {
		/* Reuse same rscn fcport. */
		DEBUG14(printk("scsi(%ld): Handle RSCN -- reusing RSCN fcport "
		    "[%02x%02x%02x].\n", ha->host_no,
		    rscn_fcport->d_id.b.domain, rscn_fcport->d_id.b.area,
		    rscn_fcport->d_id.b.al_pa));

		remote_fcport = rscn_fcport;
	} else {
		/* Create new fcport for later login. */
		remote_fcport = qla2x00_alloc_rscn_fcport(ha,
		    ha_locked ? GFP_ATOMIC: GFP_KERNEL);
		list_add_tail(&remote_fcport->list, &ha->rscn_fcports);
	}
	if (remote_fcport == NULL)
		return (QLA_SUCCESS);

	/* Prepare fcport for login. */
	atomic_set(&remote_fcport->state, FCS_DEVICE_LOST);
	remote_fcport->login_retry = 3; /* ha->login_retry_count; */
	remote_fcport->d_id.b24 = rscn_pid.b24;

	iodesc = qla2x00_alloc_iodesc(ha);
	if (iodesc == NULL) {
		/* Mark fcport for later adisc processing. */
		DEBUG14(printk("scsi(%ld): Handle RSCN -- not enough IO "
		    "descriptors for Login, flag for later processing.\n",
		    ha->host_no));

		remote_fcport->iodesc_idx_sent = IODESC_LOGIN_NEEDED;
		set_bit(IODESC_PROCESS_NEEDED, &ha->dpc_flags);

		return (QLA_SUCCESS);
	}

	if (known_fcport == NULL || rscn_pid.b24 != INVALID_PORT_ID) {
		remote_fcport->loop_id = ha->min_external_loopid;

		rval = qla2x00_find_new_loop_id(ha, remote_fcport);
		if (rval == QLA_FUNCTION_FAILED) {
			/* No more loop ids, failed. */
			DEBUG14(printk("scsi(%ld): Handle RSCN -- no available "
			    "loop id to perform Login, failed.\n",
			    ha->host_no));

			return (rval);
		}
	}

	iodesc->cb_idx = LOGIN_PORT_IOCB_CB;
	iodesc->d_id.b24 = rscn_pid.b24;
	iodesc->remote_fcport = remote_fcport;
	remote_fcport->iodesc_idx_sent = iodesc->idx;

	DEBUG14(printk("scsi(%ld): Handle RSCN -- attempting login to "
	    "[%x/%02x%02x%02x].\n", ha->host_no, remote_fcport->loop_id,
	    iodesc->d_id.b.domain, iodesc->d_id.b.area, iodesc->d_id.b.al_pa));

	qla2x00_send_login_iocb(ha, iodesc, &rscn_pid, ha_locked);

	return (QLA_SUCCESS);
}

/**
 * qla2x00_process_iodesc() - Complete IO descriptor processing.
 * @ha: HA context
 * @mbxstat: Mailbox IOCB status
 */
void
qla2x00_process_iodesc(scsi_qla_host_t *ha, struct mbx_entry *mbxstat)
{
	int rval;
	uint32_t signature;
	fc_port_t *fcport;
	struct io_descriptor *iodesc;

	signature = mbxstat->handle;

	DEBUG14(printk("scsi(%ld): Process IODesc -- processing %08x.\n",
	    ha->host_no, signature));

	/* Retrieve proper IO descriptor. */
	iodesc = qla2x00_handle_to_iodesc(ha, signature);
	if (iodesc == NULL) {
		DEBUG14(printk("scsi(%ld): Process IODesc -- ignoring, "
		    "incorrect signature %08x.\n", ha->host_no, signature));

		return;
	}

	/* Stop IO descriptor timer. */
	qla2x00_remove_iodesc_timer(iodesc);

	/* Verify signature match. */
	if (iodesc->signature != signature) {
		DEBUG14(printk("scsi(%ld): Process IODesc -- ignoring, "
		    "signature mismatch, sent %08x, received %08x.\n",
		    ha->host_no, iodesc->signature, signature));

		return;
	}

	/* Go with IOCB callback. */
	rval = iocb_function_cb_list[iodesc->cb_idx](ha, iodesc, mbxstat);
	if (rval != QLA_SUCCESS) {
		/* IO descriptor reused by callback. */
		return;
	}

	qla2x00_free_iodesc(iodesc);

	if (test_bit(IODESC_PROCESS_NEEDED, &ha->dpc_flags)) {
		/* Scan our fcports list for any RSCN requests. */
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (fcport->iodesc_idx_sent == IODESC_ADISC_NEEDED ||
			    fcport->iodesc_idx_sent == IODESC_LOGIN_NEEDED) {
				qla2x00_handle_port_rscn(ha, 0, fcport, 1);
				return;
			}
		}

		/* Scan our RSCN fcports list for any RSCN requests. */
		list_for_each_entry(fcport, &ha->rscn_fcports, list) {
			if (fcport->iodesc_idx_sent == IODESC_ADISC_NEEDED ||
			    fcport->iodesc_idx_sent == IODESC_LOGIN_NEEDED) {
				qla2x00_handle_port_rscn(ha, 0, fcport, 1);
				return;
			}
		}
	}
	clear_bit(IODESC_PROCESS_NEEDED, &ha->dpc_flags);
}

/**
 * qla2x00_cancel_io_descriptors() - Cancel all outstanding io descriptors.
 * @ha: HA context
 *
 * This routine will also delete any RSCN entries related to the outstanding
 * IO descriptors.
 */
void
qla2x00_cancel_io_descriptors(scsi_qla_host_t *ha)
{
	fc_port_t *fcport, *fcptemp;

	clear_bit(IODESC_PROCESS_NEEDED, &ha->dpc_flags);

	/* Abort all IO descriptors. */
	qla2x00_init_io_descriptors(ha);

	/* Reset all pending IO descriptors in fcports list. */
	list_for_each_entry(fcport, &ha->fcports, list) {
		fcport->iodesc_idx_sent = IODESC_INVALID_INDEX;
	}

	/* Reset all pending IO descriptors in rscn fcports list. */
	list_for_each_entry_safe(fcport, fcptemp, &ha->rscn_fcports, list) {
		DEBUG14(printk("scsi(%ld): Cancel IOs -- Freeing RSCN fcport "
		    "%p [%x/%02x%02x%02x].\n", ha->host_no, fcport,
		    fcport->loop_id, fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa));

		list_del(&fcport->list);
		kfree(fcport);
	}
}
