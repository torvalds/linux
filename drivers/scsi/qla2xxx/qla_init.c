/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2010 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"
#include "qla_gbl.h"

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "qla_devtbl.h"

#ifdef CONFIG_SPARC
#include <asm/prom.h>
#endif

/*
*  QLogic ISP2x00 Hardware Support Function Prototypes.
*/
static int qla2x00_isp_firmware(scsi_qla_host_t *);
static int qla2x00_setup_chip(scsi_qla_host_t *);
static int qla2x00_init_rings(scsi_qla_host_t *);
static int qla2x00_fw_ready(scsi_qla_host_t *);
static int qla2x00_configure_hba(scsi_qla_host_t *);
static int qla2x00_configure_loop(scsi_qla_host_t *);
static int qla2x00_configure_local_loop(scsi_qla_host_t *);
static int qla2x00_configure_fabric(scsi_qla_host_t *);
static int qla2x00_find_all_fabric_devs(scsi_qla_host_t *, struct list_head *);
static int qla2x00_device_resync(scsi_qla_host_t *);
static int qla2x00_fabric_dev_login(scsi_qla_host_t *, fc_port_t *,
    uint16_t *);

static int qla2x00_restart_isp(scsi_qla_host_t *);

static int qla2x00_find_new_loop_id(scsi_qla_host_t *, fc_port_t *);

static struct qla_chip_state_84xx *qla84xx_get_chip(struct scsi_qla_host *);
static int qla84xx_init_chip(scsi_qla_host_t *);
static int qla25xx_init_queues(struct qla_hw_data *);

/* SRB Extensions ---------------------------------------------------------- */

static void
qla2x00_ctx_sp_timeout(unsigned long __data)
{
	srb_t *sp = (srb_t *)__data;
	struct srb_ctx *ctx;
	struct srb_iocb *iocb;
	fc_port_t *fcport = sp->fcport;
	struct qla_hw_data *ha = fcport->vha->hw;
	struct req_que *req;
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	req = ha->req_q_map[0];
	req->outstanding_cmds[sp->handle] = NULL;
	ctx = sp->ctx;
	iocb = ctx->u.iocb_cmd;
	iocb->timeout(sp);
	iocb->free(sp);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static void
qla2x00_ctx_sp_free(srb_t *sp)
{
	struct srb_ctx *ctx = sp->ctx;
	struct srb_iocb *iocb = ctx->u.iocb_cmd;
	struct scsi_qla_host *vha = sp->fcport->vha;

	del_timer(&iocb->timer);
	kfree(iocb);
	kfree(ctx);
	mempool_free(sp, sp->fcport->vha->hw->srb_mempool);

	QLA_VHA_MARK_NOT_BUSY(vha);
}

inline srb_t *
qla2x00_get_ctx_sp(scsi_qla_host_t *vha, fc_port_t *fcport, size_t size,
    unsigned long tmo)
{
	srb_t *sp = NULL;
	struct qla_hw_data *ha = vha->hw;
	struct srb_ctx *ctx;
	struct srb_iocb *iocb;
	uint8_t bail;

	QLA_VHA_MARK_BUSY(vha, bail);
	if (bail)
		return NULL;

	sp = mempool_alloc(ha->srb_mempool, GFP_KERNEL);
	if (!sp)
		goto done;
	ctx = kzalloc(size, GFP_KERNEL);
	if (!ctx) {
		mempool_free(sp, ha->srb_mempool);
		sp = NULL;
		goto done;
	}
	iocb = kzalloc(sizeof(struct srb_iocb), GFP_KERNEL);
	if (!iocb) {
		mempool_free(sp, ha->srb_mempool);
		sp = NULL;
		kfree(ctx);
		goto done;
	}

	memset(sp, 0, sizeof(*sp));
	sp->fcport = fcport;
	sp->ctx = ctx;
	ctx->u.iocb_cmd = iocb;
	iocb->free = qla2x00_ctx_sp_free;

	init_timer(&iocb->timer);
	if (!tmo)
		goto done;
	iocb->timer.expires = jiffies + tmo * HZ;
	iocb->timer.data = (unsigned long)sp;
	iocb->timer.function = qla2x00_ctx_sp_timeout;
	add_timer(&iocb->timer);
done:
	if (!sp)
		QLA_VHA_MARK_NOT_BUSY(vha);
	return sp;
}

/* Asynchronous Login/Logout Routines -------------------------------------- */

static inline unsigned long
qla2x00_get_async_timeout(struct scsi_qla_host *vha)
{
	unsigned long tmo;
	struct qla_hw_data *ha = vha->hw;

	/* Firmware should use switch negotiated r_a_tov for timeout. */
	tmo = ha->r_a_tov / 10 * 2;
	if (!IS_FWI2_CAPABLE(ha)) {
		/*
		 * Except for earlier ISPs where the timeout is seeded from the
		 * initialization control block.
		 */
		tmo = ha->login_timeout;
	}
	return tmo;
}

static void
qla2x00_async_iocb_timeout(srb_t *sp)
{
	fc_port_t *fcport = sp->fcport;
	struct srb_ctx *ctx = sp->ctx;

	DEBUG2(printk(KERN_WARNING
		"scsi(%ld:%x): Async-%s timeout - portid=%02x%02x%02x.\n",
		fcport->vha->host_no, sp->handle,
		ctx->name, fcport->d_id.b.domain,
		fcport->d_id.b.area, fcport->d_id.b.al_pa));

	fcport->flags &= ~FCF_ASYNC_SENT;
	if (ctx->type == SRB_LOGIN_CMD) {
		struct srb_iocb *lio = ctx->u.iocb_cmd;
		qla2x00_post_async_logout_work(fcport->vha, fcport, NULL);
		/* Retry as needed. */
		lio->u.logio.data[0] = MBS_COMMAND_ERROR;
		lio->u.logio.data[1] = lio->u.logio.flags & SRB_LOGIN_RETRIED ?
			QLA_LOGIO_LOGIN_RETRIED : 0;
		qla2x00_post_async_login_done_work(fcport->vha, fcport,
			lio->u.logio.data);
	}
}

static void
qla2x00_async_login_ctx_done(srb_t *sp)
{
	struct srb_ctx *ctx = sp->ctx;
	struct srb_iocb *lio = ctx->u.iocb_cmd;

	qla2x00_post_async_login_done_work(sp->fcport->vha, sp->fcport,
		lio->u.logio.data);
	lio->free(sp);
}

int
qla2x00_async_login(struct scsi_qla_host *vha, fc_port_t *fcport,
    uint16_t *data)
{
	srb_t *sp;
	struct srb_ctx *ctx;
	struct srb_iocb *lio;
	int rval;

	rval = QLA_FUNCTION_FAILED;
	sp = qla2x00_get_ctx_sp(vha, fcport, sizeof(struct srb_ctx),
	    qla2x00_get_async_timeout(vha) + 2);
	if (!sp)
		goto done;

	ctx = sp->ctx;
	ctx->type = SRB_LOGIN_CMD;
	ctx->name = "login";
	lio = ctx->u.iocb_cmd;
	lio->timeout = qla2x00_async_iocb_timeout;
	lio->done = qla2x00_async_login_ctx_done;
	lio->u.logio.flags |= SRB_LOGIN_COND_PLOGI;
	if (data[1] & QLA_LOGIO_LOGIN_RETRIED)
		lio->u.logio.flags |= SRB_LOGIN_RETRIED;
	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS)
		goto done_free_sp;

	DEBUG2(printk(KERN_DEBUG
	    "scsi(%ld:%x): Async-login - loop-id=%x portid=%02x%02x%02x "
	    "retries=%d.\n", fcport->vha->host_no, sp->handle, fcport->loop_id,
	    fcport->d_id.b.domain, fcport->d_id.b.area, fcport->d_id.b.al_pa,
	    fcport->login_retry));
	return rval;

done_free_sp:
	lio->free(sp);
done:
	return rval;
}

static void
qla2x00_async_logout_ctx_done(srb_t *sp)
{
	struct srb_ctx *ctx = sp->ctx;
	struct srb_iocb *lio = ctx->u.iocb_cmd;

	qla2x00_post_async_logout_done_work(sp->fcport->vha, sp->fcport,
	    lio->u.logio.data);
	lio->free(sp);
}

int
qla2x00_async_logout(struct scsi_qla_host *vha, fc_port_t *fcport)
{
	srb_t *sp;
	struct srb_ctx *ctx;
	struct srb_iocb *lio;
	int rval;

	rval = QLA_FUNCTION_FAILED;
	sp = qla2x00_get_ctx_sp(vha, fcport, sizeof(struct srb_ctx),
	    qla2x00_get_async_timeout(vha) + 2);
	if (!sp)
		goto done;

	ctx = sp->ctx;
	ctx->type = SRB_LOGOUT_CMD;
	ctx->name = "logout";
	lio = ctx->u.iocb_cmd;
	lio->timeout = qla2x00_async_iocb_timeout;
	lio->done = qla2x00_async_logout_ctx_done;
	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS)
		goto done_free_sp;

	DEBUG2(printk(KERN_DEBUG
	    "scsi(%ld:%x): Async-logout - loop-id=%x portid=%02x%02x%02x.\n",
	    fcport->vha->host_no, sp->handle, fcport->loop_id,
	    fcport->d_id.b.domain, fcport->d_id.b.area, fcport->d_id.b.al_pa));
	return rval;

done_free_sp:
	lio->free(sp);
done:
	return rval;
}

static void
qla2x00_async_adisc_ctx_done(srb_t *sp)
{
	struct srb_ctx *ctx = sp->ctx;
	struct srb_iocb *lio = ctx->u.iocb_cmd;

	qla2x00_post_async_adisc_done_work(sp->fcport->vha, sp->fcport,
	    lio->u.logio.data);
	lio->free(sp);
}

int
qla2x00_async_adisc(struct scsi_qla_host *vha, fc_port_t *fcport,
    uint16_t *data)
{
	srb_t *sp;
	struct srb_ctx *ctx;
	struct srb_iocb *lio;
	int rval;

	rval = QLA_FUNCTION_FAILED;
	sp = qla2x00_get_ctx_sp(vha, fcport, sizeof(struct srb_ctx),
	    qla2x00_get_async_timeout(vha) + 2);
	if (!sp)
		goto done;

	ctx = sp->ctx;
	ctx->type = SRB_ADISC_CMD;
	ctx->name = "adisc";
	lio = ctx->u.iocb_cmd;
	lio->timeout = qla2x00_async_iocb_timeout;
	lio->done = qla2x00_async_adisc_ctx_done;
	if (data[1] & QLA_LOGIO_LOGIN_RETRIED)
		lio->u.logio.flags |= SRB_LOGIN_RETRIED;
	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS)
		goto done_free_sp;

	DEBUG2(printk(KERN_DEBUG
	    "scsi(%ld:%x): Async-adisc - loop-id=%x portid=%02x%02x%02x.\n",
	    fcport->vha->host_no, sp->handle, fcport->loop_id,
	    fcport->d_id.b.domain, fcport->d_id.b.area, fcport->d_id.b.al_pa));

	return rval;

done_free_sp:
	lio->free(sp);
done:
	return rval;
}

static void
qla2x00_async_tm_cmd_ctx_done(srb_t *sp)
{
	struct srb_ctx *ctx = sp->ctx;
	struct srb_iocb *iocb = (struct srb_iocb *)ctx->u.iocb_cmd;

	qla2x00_async_tm_cmd_done(sp->fcport->vha, sp->fcport, iocb);
	iocb->free(sp);
}

int
qla2x00_async_tm_cmd(fc_port_t *fcport, uint32_t flags, uint32_t lun,
	uint32_t tag)
{
	struct scsi_qla_host *vha = fcport->vha;
	srb_t *sp;
	struct srb_ctx *ctx;
	struct srb_iocb *tcf;
	int rval;

	rval = QLA_FUNCTION_FAILED;
	sp = qla2x00_get_ctx_sp(vha, fcport, sizeof(struct srb_ctx),
	    qla2x00_get_async_timeout(vha) + 2);
	if (!sp)
		goto done;

	ctx = sp->ctx;
	ctx->type = SRB_TM_CMD;
	ctx->name = "tmf";
	tcf = ctx->u.iocb_cmd;
	tcf->u.tmf.flags = flags;
	tcf->u.tmf.lun = lun;
	tcf->u.tmf.data = tag;
	tcf->timeout = qla2x00_async_iocb_timeout;
	tcf->done = qla2x00_async_tm_cmd_ctx_done;

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS)
		goto done_free_sp;

	DEBUG2(printk(KERN_DEBUG
	    "scsi(%ld:%x): Async-tmf - loop-id=%x portid=%02x%02x%02x.\n",
	    fcport->vha->host_no, sp->handle, fcport->loop_id,
	    fcport->d_id.b.domain, fcport->d_id.b.area, fcport->d_id.b.al_pa));

	return rval;

done_free_sp:
	tcf->free(sp);
done:
	return rval;
}

void
qla2x00_async_login_done(struct scsi_qla_host *vha, fc_port_t *fcport,
    uint16_t *data)
{
	int rval;

	switch (data[0]) {
	case MBS_COMMAND_COMPLETE:
		if (fcport->flags & FCF_FCP2_DEVICE) {
			fcport->flags |= FCF_ASYNC_SENT;
			qla2x00_post_async_adisc_work(vha, fcport, data);
			break;
		}
		qla2x00_update_fcport(vha, fcport);
		break;
	case MBS_COMMAND_ERROR:
		fcport->flags &= ~FCF_ASYNC_SENT;
		if (data[1] & QLA_LOGIO_LOGIN_RETRIED)
			set_bit(RELOGIN_NEEDED, &vha->dpc_flags);
		else
			qla2x00_mark_device_lost(vha, fcport, 1, 1);
		break;
	case MBS_PORT_ID_USED:
		fcport->loop_id = data[1];
		qla2x00_post_async_logout_work(vha, fcport, NULL);
		qla2x00_post_async_login_work(vha, fcport, NULL);
		break;
	case MBS_LOOP_ID_USED:
		fcport->loop_id++;
		rval = qla2x00_find_new_loop_id(vha, fcport);
		if (rval != QLA_SUCCESS) {
			fcport->flags &= ~FCF_ASYNC_SENT;
			qla2x00_mark_device_lost(vha, fcport, 1, 1);
			break;
		}
		qla2x00_post_async_login_work(vha, fcport, NULL);
		break;
	}
	return;
}

void
qla2x00_async_logout_done(struct scsi_qla_host *vha, fc_port_t *fcport,
    uint16_t *data)
{
	qla2x00_mark_device_lost(vha, fcport, 1, 0);
	return;
}

void
qla2x00_async_adisc_done(struct scsi_qla_host *vha, fc_port_t *fcport,
    uint16_t *data)
{
	if (data[0] == MBS_COMMAND_COMPLETE) {
		qla2x00_update_fcport(vha, fcport);

		return;
	}

	/* Retry login. */
	fcport->flags &= ~FCF_ASYNC_SENT;
	if (data[1] & QLA_LOGIO_LOGIN_RETRIED)
		set_bit(RELOGIN_NEEDED, &vha->dpc_flags);
	else
		qla2x00_mark_device_lost(vha, fcport, 1, 1);

	return;
}

void
qla2x00_async_tm_cmd_done(struct scsi_qla_host *vha, fc_port_t *fcport,
    struct srb_iocb *iocb)
{
	int rval;
	uint32_t flags;
	uint16_t lun;

	flags = iocb->u.tmf.flags;
	lun = (uint16_t)iocb->u.tmf.lun;

	/* Issue Marker IOCB */
	rval = qla2x00_marker(vha, vha->hw->req_q_map[0],
		vha->hw->rsp_q_map[0], fcport->loop_id, lun,
		flags == TCF_LUN_RESET ? MK_SYNC_ID_LUN : MK_SYNC_ID);

	if ((rval != QLA_SUCCESS) || iocb->u.tmf.data) {
		DEBUG2_3_11(printk(KERN_WARNING
			"%s(%ld): TM IOCB failed (%x).\n",
			__func__, vha->host_no, rval));
	}

	return;
}

/****************************************************************************/
/*                QLogic ISP2x00 Hardware Support Functions.                */
/****************************************************************************/

/*
* qla2x00_initialize_adapter
*      Initialize board.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success
*/
int
qla2x00_initialize_adapter(scsi_qla_host_t *vha)
{
	int	rval;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];

	/* Clear adapter flags. */
	vha->flags.online = 0;
	ha->flags.chip_reset_done = 0;
	vha->flags.reset_active = 0;
	ha->flags.pci_channel_io_perm_failure = 0;
	ha->flags.eeh_busy = 0;
	ha->flags.thermal_supported = 1;
	atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);
	atomic_set(&vha->loop_state, LOOP_DOWN);
	vha->device_flags = DFLG_NO_CABLE;
	vha->dpc_flags = 0;
	vha->flags.management_server_logged_in = 0;
	vha->marker_needed = 0;
	ha->isp_abort_cnt = 0;
	ha->beacon_blink_led = 0;

	set_bit(0, ha->req_qid_map);
	set_bit(0, ha->rsp_qid_map);

	qla_printk(KERN_INFO, ha, "Configuring PCI space...\n");
	rval = ha->isp_ops->pci_config(vha);
	if (rval) {
		DEBUG2(printk("scsi(%ld): Unable to configure PCI space.\n",
		    vha->host_no));
		return (rval);
	}

	ha->isp_ops->reset_chip(vha);

	rval = qla2xxx_get_flash_info(vha);
	if (rval) {
		DEBUG2(printk("scsi(%ld): Unable to validate FLASH data.\n",
		    vha->host_no));
		return (rval);
	}

	ha->isp_ops->get_flash_version(vha, req->ring);

	qla_printk(KERN_INFO, ha, "Configure NVRAM parameters...\n");

	ha->isp_ops->nvram_config(vha);

	if (ha->flags.disable_serdes) {
		/* Mask HBA via NVRAM settings? */
		qla_printk(KERN_INFO, ha, "Masking HBA WWPN "
		    "%02x%02x%02x%02x%02x%02x%02x%02x (via NVRAM).\n",
		    vha->port_name[0], vha->port_name[1],
		    vha->port_name[2], vha->port_name[3],
		    vha->port_name[4], vha->port_name[5],
		    vha->port_name[6], vha->port_name[7]);
		return QLA_FUNCTION_FAILED;
	}

	qla_printk(KERN_INFO, ha, "Verifying loaded RISC code...\n");

	if (qla2x00_isp_firmware(vha) != QLA_SUCCESS) {
		rval = ha->isp_ops->chip_diag(vha);
		if (rval)
			return (rval);
		rval = qla2x00_setup_chip(vha);
		if (rval)
			return (rval);
	}

	if (IS_QLA84XX(ha)) {
		ha->cs84xx = qla84xx_get_chip(vha);
		if (!ha->cs84xx) {
			qla_printk(KERN_ERR, ha,
			    "Unable to configure ISP84XX.\n");
			return QLA_FUNCTION_FAILED;
		}
	}
	rval = qla2x00_init_rings(vha);
	ha->flags.chip_reset_done = 1;

	if (rval == QLA_SUCCESS && IS_QLA84XX(ha)) {
		/* Issue verify 84xx FW IOCB to complete 84xx initialization */
		rval = qla84xx_init_chip(vha);
		if (rval != QLA_SUCCESS) {
			qla_printk(KERN_ERR, ha,
				"Unable to initialize ISP84XX.\n");
		qla84xx_put_chip(vha);
		}
	}

	if (IS_QLA24XX_TYPE(ha) || IS_QLA25XX(ha))
		qla24xx_read_fcp_prio_cfg(vha);

	return (rval);
}

/**
 * qla2100_pci_config() - Setup ISP21xx PCI configuration registers.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2100_pci_config(scsi_qla_host_t *vha)
{
	uint16_t w;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	pci_set_master(ha->pdev);
	pci_try_set_mwi(ha->pdev);

	pci_read_config_word(ha->pdev, PCI_COMMAND, &w);
	w |= (PCI_COMMAND_PARITY | PCI_COMMAND_SERR);
	pci_write_config_word(ha->pdev, PCI_COMMAND, w);

	pci_disable_rom(ha->pdev);

	/* Get PCI bus information. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->pci_attr = RD_REG_WORD(&reg->ctrl_status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_SUCCESS;
}

/**
 * qla2300_pci_config() - Setup ISP23xx PCI configuration registers.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2300_pci_config(scsi_qla_host_t *vha)
{
	uint16_t	w;
	unsigned long   flags = 0;
	uint32_t	cnt;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	pci_set_master(ha->pdev);
	pci_try_set_mwi(ha->pdev);

	pci_read_config_word(ha->pdev, PCI_COMMAND, &w);
	w |= (PCI_COMMAND_PARITY | PCI_COMMAND_SERR);

	if (IS_QLA2322(ha) || IS_QLA6322(ha))
		w &= ~PCI_COMMAND_INTX_DISABLE;
	pci_write_config_word(ha->pdev, PCI_COMMAND, w);

	/*
	 * If this is a 2300 card and not 2312, reset the
	 * COMMAND_INVALIDATE due to a bug in the 2300. Unfortunately,
	 * the 2310 also reports itself as a 2300 so we need to get the
	 * fb revision level -- a 6 indicates it really is a 2300 and
	 * not a 2310.
	 */
	if (IS_QLA2300(ha)) {
		spin_lock_irqsave(&ha->hardware_lock, flags);

		/* Pause RISC. */
		WRT_REG_WORD(&reg->hccr, HCCR_PAUSE_RISC);
		for (cnt = 0; cnt < 30000; cnt++) {
			if ((RD_REG_WORD(&reg->hccr) & HCCR_RISC_PAUSE) != 0)
				break;

			udelay(10);
		}

		/* Select FPM registers. */
		WRT_REG_WORD(&reg->ctrl_status, 0x20);
		RD_REG_WORD(&reg->ctrl_status);

		/* Get the fb rev level */
		ha->fb_rev = RD_FB_CMD_REG(ha, reg);

		if (ha->fb_rev == FPM_2300)
			pci_clear_mwi(ha->pdev);

		/* Deselect FPM registers. */
		WRT_REG_WORD(&reg->ctrl_status, 0x0);
		RD_REG_WORD(&reg->ctrl_status);

		/* Release RISC module. */
		WRT_REG_WORD(&reg->hccr, HCCR_RELEASE_RISC);
		for (cnt = 0; cnt < 30000; cnt++) {
			if ((RD_REG_WORD(&reg->hccr) & HCCR_RISC_PAUSE) == 0)
				break;

			udelay(10);
		}

		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}

	pci_write_config_byte(ha->pdev, PCI_LATENCY_TIMER, 0x80);

	pci_disable_rom(ha->pdev);

	/* Get PCI bus information. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->pci_attr = RD_REG_WORD(&reg->ctrl_status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_SUCCESS;
}

/**
 * qla24xx_pci_config() - Setup ISP24xx PCI configuration registers.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla24xx_pci_config(scsi_qla_host_t *vha)
{
	uint16_t w;
	unsigned long flags = 0;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	pci_set_master(ha->pdev);
	pci_try_set_mwi(ha->pdev);

	pci_read_config_word(ha->pdev, PCI_COMMAND, &w);
	w |= (PCI_COMMAND_PARITY | PCI_COMMAND_SERR);
	w &= ~PCI_COMMAND_INTX_DISABLE;
	pci_write_config_word(ha->pdev, PCI_COMMAND, w);

	pci_write_config_byte(ha->pdev, PCI_LATENCY_TIMER, 0x80);

	/* PCI-X -- adjust Maximum Memory Read Byte Count (2048). */
	if (pci_find_capability(ha->pdev, PCI_CAP_ID_PCIX))
		pcix_set_mmrbc(ha->pdev, 2048);

	/* PCIe -- adjust Maximum Read Request Size (2048). */
	if (pci_find_capability(ha->pdev, PCI_CAP_ID_EXP))
		pcie_set_readrq(ha->pdev, 2048);

	pci_disable_rom(ha->pdev);

	ha->chip_revision = ha->pdev->revision;

	/* Get PCI bus information. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->pci_attr = RD_REG_DWORD(&reg->ctrl_status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_SUCCESS;
}

/**
 * qla25xx_pci_config() - Setup ISP25xx PCI configuration registers.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla25xx_pci_config(scsi_qla_host_t *vha)
{
	uint16_t w;
	struct qla_hw_data *ha = vha->hw;

	pci_set_master(ha->pdev);
	pci_try_set_mwi(ha->pdev);

	pci_read_config_word(ha->pdev, PCI_COMMAND, &w);
	w |= (PCI_COMMAND_PARITY | PCI_COMMAND_SERR);
	w &= ~PCI_COMMAND_INTX_DISABLE;
	pci_write_config_word(ha->pdev, PCI_COMMAND, w);

	/* PCIe -- adjust Maximum Read Request Size (2048). */
	if (pci_find_capability(ha->pdev, PCI_CAP_ID_EXP))
		pcie_set_readrq(ha->pdev, 2048);

	pci_disable_rom(ha->pdev);

	ha->chip_revision = ha->pdev->revision;

	return QLA_SUCCESS;
}

/**
 * qla2x00_isp_firmware() - Choose firmware image.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
static int
qla2x00_isp_firmware(scsi_qla_host_t *vha)
{
	int  rval;
	uint16_t loop_id, topo, sw_cap;
	uint8_t domain, area, al_pa;
	struct qla_hw_data *ha = vha->hw;

	/* Assume loading risc code */
	rval = QLA_FUNCTION_FAILED;

	if (ha->flags.disable_risc_code_load) {
		DEBUG2(printk("scsi(%ld): RISC CODE NOT loaded\n",
		    vha->host_no));
		qla_printk(KERN_INFO, ha, "RISC CODE NOT loaded\n");

		/* Verify checksum of loaded RISC code. */
		rval = qla2x00_verify_checksum(vha, ha->fw_srisc_address);
		if (rval == QLA_SUCCESS) {
			/* And, verify we are not in ROM code. */
			rval = qla2x00_get_adapter_id(vha, &loop_id, &al_pa,
			    &area, &domain, &topo, &sw_cap);
		}
	}

	if (rval) {
		DEBUG2_3(printk("scsi(%ld): **** Load RISC code ****\n",
		    vha->host_no));
	}

	return (rval);
}

/**
 * qla2x00_reset_chip() - Reset ISP chip.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
void
qla2x00_reset_chip(scsi_qla_host_t *vha)
{
	unsigned long   flags = 0;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint32_t	cnt;
	uint16_t	cmd;

	if (unlikely(pci_channel_offline(ha->pdev)))
		return;

	ha->isp_ops->disable_intrs(ha);

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Turn off master enable */
	cmd = 0;
	pci_read_config_word(ha->pdev, PCI_COMMAND, &cmd);
	cmd &= ~PCI_COMMAND_MASTER;
	pci_write_config_word(ha->pdev, PCI_COMMAND, cmd);

	if (!IS_QLA2100(ha)) {
		/* Pause RISC. */
		WRT_REG_WORD(&reg->hccr, HCCR_PAUSE_RISC);
		if (IS_QLA2200(ha) || IS_QLA2300(ha)) {
			for (cnt = 0; cnt < 30000; cnt++) {
				if ((RD_REG_WORD(&reg->hccr) &
				    HCCR_RISC_PAUSE) != 0)
					break;
				udelay(100);
			}
		} else {
			RD_REG_WORD(&reg->hccr);	/* PCI Posting. */
			udelay(10);
		}

		/* Select FPM registers. */
		WRT_REG_WORD(&reg->ctrl_status, 0x20);
		RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */

		/* FPM Soft Reset. */
		WRT_REG_WORD(&reg->fpm_diag_config, 0x100);
		RD_REG_WORD(&reg->fpm_diag_config);	/* PCI Posting. */

		/* Toggle Fpm Reset. */
		if (!IS_QLA2200(ha)) {
			WRT_REG_WORD(&reg->fpm_diag_config, 0x0);
			RD_REG_WORD(&reg->fpm_diag_config); /* PCI Posting. */
		}

		/* Select frame buffer registers. */
		WRT_REG_WORD(&reg->ctrl_status, 0x10);
		RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */

		/* Reset frame buffer FIFOs. */
		if (IS_QLA2200(ha)) {
			WRT_FB_CMD_REG(ha, reg, 0xa000);
			RD_FB_CMD_REG(ha, reg);		/* PCI Posting. */
		} else {
			WRT_FB_CMD_REG(ha, reg, 0x00fc);

			/* Read back fb_cmd until zero or 3 seconds max */
			for (cnt = 0; cnt < 3000; cnt++) {
				if ((RD_FB_CMD_REG(ha, reg) & 0xff) == 0)
					break;
				udelay(100);
			}
		}

		/* Select RISC module registers. */
		WRT_REG_WORD(&reg->ctrl_status, 0);
		RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */

		/* Reset RISC processor. */
		WRT_REG_WORD(&reg->hccr, HCCR_RESET_RISC);
		RD_REG_WORD(&reg->hccr);		/* PCI Posting. */

		/* Release RISC processor. */
		WRT_REG_WORD(&reg->hccr, HCCR_RELEASE_RISC);
		RD_REG_WORD(&reg->hccr);		/* PCI Posting. */
	}

	WRT_REG_WORD(&reg->hccr, HCCR_CLR_RISC_INT);
	WRT_REG_WORD(&reg->hccr, HCCR_CLR_HOST_INT);

	/* Reset ISP chip. */
	WRT_REG_WORD(&reg->ctrl_status, CSR_ISP_SOFT_RESET);

	/* Wait for RISC to recover from reset. */
	if (IS_QLA2100(ha) || IS_QLA2200(ha) || IS_QLA2300(ha)) {
		/*
		 * It is necessary to for a delay here since the card doesn't
		 * respond to PCI reads during a reset. On some architectures
		 * this will result in an MCA.
		 */
		udelay(20);
		for (cnt = 30000; cnt; cnt--) {
			if ((RD_REG_WORD(&reg->ctrl_status) &
			    CSR_ISP_SOFT_RESET) == 0)
				break;
			udelay(100);
		}
	} else
		udelay(10);

	/* Reset RISC processor. */
	WRT_REG_WORD(&reg->hccr, HCCR_RESET_RISC);

	WRT_REG_WORD(&reg->semaphore, 0);

	/* Release RISC processor. */
	WRT_REG_WORD(&reg->hccr, HCCR_RELEASE_RISC);
	RD_REG_WORD(&reg->hccr);			/* PCI Posting. */

	if (IS_QLA2100(ha) || IS_QLA2200(ha) || IS_QLA2300(ha)) {
		for (cnt = 0; cnt < 30000; cnt++) {
			if (RD_MAILBOX_REG(ha, reg, 0) != MBS_BUSY)
				break;

			udelay(100);
		}
	} else
		udelay(100);

	/* Turn on master enable */
	cmd |= PCI_COMMAND_MASTER;
	pci_write_config_word(ha->pdev, PCI_COMMAND, cmd);

	/* Disable RISC pause on FPM parity error. */
	if (!IS_QLA2100(ha)) {
		WRT_REG_WORD(&reg->hccr, HCCR_DISABLE_PARITY_PAUSE);
		RD_REG_WORD(&reg->hccr);		/* PCI Posting. */
	}

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

/**
 * qla81xx_reset_mpi() - Reset's MPI FW via Write MPI Register MBC.
 *
 * Returns 0 on success.
 */
int
qla81xx_reset_mpi(scsi_qla_host_t *vha)
{
	uint16_t mb[4] = {0x1010, 0, 1, 0};

	return qla81xx_write_mpi_register(vha, mb);
}

/**
 * qla24xx_reset_risc() - Perform full reset of ISP24xx RISC.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
static inline void
qla24xx_reset_risc(scsi_qla_host_t *vha)
{
	unsigned long flags = 0;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	uint32_t cnt, d2;
	uint16_t wd;
	static int abts_cnt; /* ISP abort retry counts */

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Reset RISC. */
	WRT_REG_DWORD(&reg->ctrl_status, CSRX_DMA_SHUTDOWN|MWB_4096_BYTES);
	for (cnt = 0; cnt < 30000; cnt++) {
		if ((RD_REG_DWORD(&reg->ctrl_status) & CSRX_DMA_ACTIVE) == 0)
			break;

		udelay(10);
	}

	WRT_REG_DWORD(&reg->ctrl_status,
	    CSRX_ISP_SOFT_RESET|CSRX_DMA_SHUTDOWN|MWB_4096_BYTES);
	pci_read_config_word(ha->pdev, PCI_COMMAND, &wd);

	udelay(100);
	/* Wait for firmware to complete NVRAM accesses. */
	d2 = (uint32_t) RD_REG_WORD(&reg->mailbox0);
	for (cnt = 10000 ; cnt && d2; cnt--) {
		udelay(5);
		d2 = (uint32_t) RD_REG_WORD(&reg->mailbox0);
		barrier();
	}

	/* Wait for soft-reset to complete. */
	d2 = RD_REG_DWORD(&reg->ctrl_status);
	for (cnt = 6000000 ; cnt && (d2 & CSRX_ISP_SOFT_RESET); cnt--) {
		udelay(5);
		d2 = RD_REG_DWORD(&reg->ctrl_status);
		barrier();
	}

	/* If required, do an MPI FW reset now */
	if (test_and_clear_bit(MPI_RESET_NEEDED, &vha->dpc_flags)) {
		if (qla81xx_reset_mpi(vha) != QLA_SUCCESS) {
			if (++abts_cnt < 5) {
				set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
				set_bit(MPI_RESET_NEEDED, &vha->dpc_flags);
			} else {
				/*
				 * We exhausted the ISP abort retries. We have to
				 * set the board offline.
				 */
				abts_cnt = 0;
				vha->flags.online = 0;
			}
		}
	}

	WRT_REG_DWORD(&reg->hccr, HCCRX_SET_RISC_RESET);
	RD_REG_DWORD(&reg->hccr);

	WRT_REG_DWORD(&reg->hccr, HCCRX_REL_RISC_PAUSE);
	RD_REG_DWORD(&reg->hccr);

	WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_RESET);
	RD_REG_DWORD(&reg->hccr);

	d2 = (uint32_t) RD_REG_WORD(&reg->mailbox0);
	for (cnt = 6000000 ; cnt && d2; cnt--) {
		udelay(5);
		d2 = (uint32_t) RD_REG_WORD(&reg->mailbox0);
		barrier();
	}

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (IS_NOPOLLING_TYPE(ha))
		ha->isp_ops->enable_intrs(ha);
}

/**
 * qla24xx_reset_chip() - Reset ISP24xx chip.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
void
qla24xx_reset_chip(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	if (pci_channel_offline(ha->pdev) &&
	    ha->flags.pci_channel_io_perm_failure) {
		return;
	}

	ha->isp_ops->disable_intrs(ha);

	/* Perform RISC reset. */
	qla24xx_reset_risc(vha);
}

/**
 * qla2x00_chip_diag() - Test chip for proper operation.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2x00_chip_diag(scsi_qla_host_t *vha)
{
	int		rval;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	unsigned long	flags = 0;
	uint16_t	data;
	uint32_t	cnt;
	uint16_t	mb[5];
	struct req_que *req = ha->req_q_map[0];

	/* Assume a failed state */
	rval = QLA_FUNCTION_FAILED;

	DEBUG3(printk("scsi(%ld): Testing device at %lx.\n",
	    vha->host_no, (u_long)&reg->flash_address));

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Reset ISP chip. */
	WRT_REG_WORD(&reg->ctrl_status, CSR_ISP_SOFT_RESET);

	/*
	 * We need to have a delay here since the card will not respond while
	 * in reset causing an MCA on some architectures.
	 */
	udelay(20);
	data = qla2x00_debounce_register(&reg->ctrl_status);
	for (cnt = 6000000 ; cnt && (data & CSR_ISP_SOFT_RESET); cnt--) {
		udelay(5);
		data = RD_REG_WORD(&reg->ctrl_status);
		barrier();
	}

	if (!cnt)
		goto chip_diag_failed;

	DEBUG3(printk("scsi(%ld): Reset register cleared by chip reset\n",
	    vha->host_no));

	/* Reset RISC processor. */
	WRT_REG_WORD(&reg->hccr, HCCR_RESET_RISC);
	WRT_REG_WORD(&reg->hccr, HCCR_RELEASE_RISC);

	/* Workaround for QLA2312 PCI parity error */
	if (IS_QLA2100(ha) || IS_QLA2200(ha) || IS_QLA2300(ha)) {
		data = qla2x00_debounce_register(MAILBOX_REG(ha, reg, 0));
		for (cnt = 6000000; cnt && (data == MBS_BUSY); cnt--) {
			udelay(5);
			data = RD_MAILBOX_REG(ha, reg, 0);
			barrier();
		}
	} else
		udelay(10);

	if (!cnt)
		goto chip_diag_failed;

	/* Check product ID of chip */
	DEBUG3(printk("scsi(%ld): Checking product ID of chip\n", vha->host_no));

	mb[1] = RD_MAILBOX_REG(ha, reg, 1);
	mb[2] = RD_MAILBOX_REG(ha, reg, 2);
	mb[3] = RD_MAILBOX_REG(ha, reg, 3);
	mb[4] = qla2x00_debounce_register(MAILBOX_REG(ha, reg, 4));
	if (mb[1] != PROD_ID_1 || (mb[2] != PROD_ID_2 && mb[2] != PROD_ID_2a) ||
	    mb[3] != PROD_ID_3) {
		qla_printk(KERN_WARNING, ha,
		    "Wrong product ID = 0x%x,0x%x,0x%x\n", mb[1], mb[2], mb[3]);

		goto chip_diag_failed;
	}
	ha->product_id[0] = mb[1];
	ha->product_id[1] = mb[2];
	ha->product_id[2] = mb[3];
	ha->product_id[3] = mb[4];

	/* Adjust fw RISC transfer size */
	if (req->length > 1024)
		ha->fw_transfer_size = REQUEST_ENTRY_SIZE * 1024;
	else
		ha->fw_transfer_size = REQUEST_ENTRY_SIZE *
		    req->length;

	if (IS_QLA2200(ha) &&
	    RD_MAILBOX_REG(ha, reg, 7) == QLA2200A_RISC_ROM_VER) {
		/* Limit firmware transfer size with a 2200A */
		DEBUG3(printk("scsi(%ld): Found QLA2200A chip.\n",
		    vha->host_no));

		ha->device_type |= DT_ISP2200A;
		ha->fw_transfer_size = 128;
	}

	/* Wrap Incoming Mailboxes Test. */
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	DEBUG3(printk("scsi(%ld): Checking mailboxes.\n", vha->host_no));
	rval = qla2x00_mbx_reg_test(vha);
	if (rval) {
		DEBUG(printk("scsi(%ld): Failed mailbox send register test\n",
		    vha->host_no));
		qla_printk(KERN_WARNING, ha,
		    "Failed mailbox send register test\n");
	}
	else {
		/* Flag a successful rval */
		rval = QLA_SUCCESS;
	}
	spin_lock_irqsave(&ha->hardware_lock, flags);

chip_diag_failed:
	if (rval)
		DEBUG2_3(printk("scsi(%ld): Chip diagnostics **** FAILED "
		    "****\n", vha->host_no));

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return (rval);
}

/**
 * qla24xx_chip_diag() - Test ISP24xx for proper operation.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla24xx_chip_diag(scsi_qla_host_t *vha)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];

	if (IS_QLA82XX(ha))
		return QLA_SUCCESS;

	ha->fw_transfer_size = REQUEST_ENTRY_SIZE * req->length;

	rval = qla2x00_mbx_reg_test(vha);
	if (rval) {
		DEBUG(printk("scsi(%ld): Failed mailbox send register test\n",
		    vha->host_no));
		qla_printk(KERN_WARNING, ha,
		    "Failed mailbox send register test\n");
	} else {
		/* Flag a successful rval */
		rval = QLA_SUCCESS;
	}

	return rval;
}

void
qla2x00_alloc_fw_dump(scsi_qla_host_t *vha)
{
	int rval;
	uint32_t dump_size, fixed_size, mem_size, req_q_size, rsp_q_size,
	    eft_size, fce_size, mq_size;
	dma_addr_t tc_dma;
	void *tc;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];
	struct rsp_que *rsp = ha->rsp_q_map[0];

	if (ha->fw_dump) {
		qla_printk(KERN_WARNING, ha,
		    "Firmware dump previously allocated.\n");
		return;
	}

	ha->fw_dumped = 0;
	fixed_size = mem_size = eft_size = fce_size = mq_size = 0;
	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		fixed_size = sizeof(struct qla2100_fw_dump);
	} else if (IS_QLA23XX(ha)) {
		fixed_size = offsetof(struct qla2300_fw_dump, data_ram);
		mem_size = (ha->fw_memory_size - 0x11000 + 1) *
		    sizeof(uint16_t);
	} else if (IS_FWI2_CAPABLE(ha)) {
		if (IS_QLA81XX(ha))
			fixed_size = offsetof(struct qla81xx_fw_dump, ext_mem);
		else if (IS_QLA25XX(ha))
			fixed_size = offsetof(struct qla25xx_fw_dump, ext_mem);
		else
			fixed_size = offsetof(struct qla24xx_fw_dump, ext_mem);
		mem_size = (ha->fw_memory_size - 0x100000 + 1) *
		    sizeof(uint32_t);
		if (ha->mqenable)
			mq_size = sizeof(struct qla2xxx_mq_chain);
		/* Allocate memory for Fibre Channel Event Buffer. */
		if (!IS_QLA25XX(ha) && !IS_QLA81XX(ha))
			goto try_eft;

		tc = dma_alloc_coherent(&ha->pdev->dev, FCE_SIZE, &tc_dma,
		    GFP_KERNEL);
		if (!tc) {
			qla_printk(KERN_WARNING, ha, "Unable to allocate "
			    "(%d KB) for FCE.\n", FCE_SIZE / 1024);
			goto try_eft;
		}

		memset(tc, 0, FCE_SIZE);
		rval = qla2x00_enable_fce_trace(vha, tc_dma, FCE_NUM_BUFFERS,
		    ha->fce_mb, &ha->fce_bufs);
		if (rval) {
			qla_printk(KERN_WARNING, ha, "Unable to initialize "
			    "FCE (%d).\n", rval);
			dma_free_coherent(&ha->pdev->dev, FCE_SIZE, tc,
			    tc_dma);
			ha->flags.fce_enabled = 0;
			goto try_eft;
		}

		qla_printk(KERN_INFO, ha, "Allocated (%d KB) for FCE...\n",
		    FCE_SIZE / 1024);

		fce_size = sizeof(struct qla2xxx_fce_chain) + FCE_SIZE;
		ha->flags.fce_enabled = 1;
		ha->fce_dma = tc_dma;
		ha->fce = tc;
try_eft:
		/* Allocate memory for Extended Trace Buffer. */
		tc = dma_alloc_coherent(&ha->pdev->dev, EFT_SIZE, &tc_dma,
		    GFP_KERNEL);
		if (!tc) {
			qla_printk(KERN_WARNING, ha, "Unable to allocate "
			    "(%d KB) for EFT.\n", EFT_SIZE / 1024);
			goto cont_alloc;
		}

		memset(tc, 0, EFT_SIZE);
		rval = qla2x00_enable_eft_trace(vha, tc_dma, EFT_NUM_BUFFERS);
		if (rval) {
			qla_printk(KERN_WARNING, ha, "Unable to initialize "
			    "EFT (%d).\n", rval);
			dma_free_coherent(&ha->pdev->dev, EFT_SIZE, tc,
			    tc_dma);
			goto cont_alloc;
		}

		qla_printk(KERN_INFO, ha, "Allocated (%d KB) for EFT...\n",
		    EFT_SIZE / 1024);

		eft_size = EFT_SIZE;
		ha->eft_dma = tc_dma;
		ha->eft = tc;
	}
cont_alloc:
	req_q_size = req->length * sizeof(request_t);
	rsp_q_size = rsp->length * sizeof(response_t);

	dump_size = offsetof(struct qla2xxx_fw_dump, isp);
	dump_size += fixed_size + mem_size + req_q_size + rsp_q_size + eft_size;
	ha->chain_offset = dump_size;
	dump_size += mq_size + fce_size;

	ha->fw_dump = vmalloc(dump_size);
	if (!ha->fw_dump) {
		qla_printk(KERN_WARNING, ha, "Unable to allocate (%d KB) for "
		    "firmware dump!!!\n", dump_size / 1024);

		if (ha->fce) {
			dma_free_coherent(&ha->pdev->dev, FCE_SIZE, ha->fce,
			    ha->fce_dma);
			ha->fce = NULL;
			ha->fce_dma = 0;
		}

		if (ha->eft) {
			dma_free_coherent(&ha->pdev->dev, eft_size, ha->eft,
			    ha->eft_dma);
			ha->eft = NULL;
			ha->eft_dma = 0;
		}
		return;
	}
	qla_printk(KERN_INFO, ha, "Allocated (%d KB) for firmware dump...\n",
	    dump_size / 1024);

	ha->fw_dump_len = dump_size;
	ha->fw_dump->signature[0] = 'Q';
	ha->fw_dump->signature[1] = 'L';
	ha->fw_dump->signature[2] = 'G';
	ha->fw_dump->signature[3] = 'C';
	ha->fw_dump->version = __constant_htonl(1);

	ha->fw_dump->fixed_size = htonl(fixed_size);
	ha->fw_dump->mem_size = htonl(mem_size);
	ha->fw_dump->req_q_size = htonl(req_q_size);
	ha->fw_dump->rsp_q_size = htonl(rsp_q_size);

	ha->fw_dump->eft_size = htonl(eft_size);
	ha->fw_dump->eft_addr_l = htonl(LSD(ha->eft_dma));
	ha->fw_dump->eft_addr_h = htonl(MSD(ha->eft_dma));

	ha->fw_dump->header_size =
	    htonl(offsetof(struct qla2xxx_fw_dump, isp));
}

static int
qla81xx_mpi_sync(scsi_qla_host_t *vha)
{
#define MPS_MASK	0xe0
	int rval;
	uint16_t dc;
	uint32_t dw;
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA81XX(vha->hw))
		return QLA_SUCCESS;

	rval = qla2x00_write_ram_word(vha, 0x7c00, 1);
	if (rval != QLA_SUCCESS) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
		    "Sync-MPI: Unable to acquire semaphore.\n"));
		goto done;
	}

	pci_read_config_word(vha->hw->pdev, 0x54, &dc);
	rval = qla2x00_read_ram_word(vha, 0x7a15, &dw);
	if (rval != QLA_SUCCESS) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
		    "Sync-MPI: Unable to read sync.\n"));
		goto done_release;
	}

	dc &= MPS_MASK;
	if (dc == (dw & MPS_MASK))
		goto done_release;

	dw &= ~MPS_MASK;
	dw |= dc;
	rval = qla2x00_write_ram_word(vha, 0x7a15, dw);
	if (rval != QLA_SUCCESS) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
		    "Sync-MPI: Unable to gain sync.\n"));
	}

done_release:
	rval = qla2x00_write_ram_word(vha, 0x7c00, 0);
	if (rval != QLA_SUCCESS) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
		    "Sync-MPI: Unable to release semaphore.\n"));
	}

done:
	return rval;
}

/**
 * qla2x00_setup_chip() - Load and start RISC firmware.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
static int
qla2x00_setup_chip(scsi_qla_host_t *vha)
{
	int rval;
	uint32_t srisc_address = 0;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	unsigned long flags;
	uint16_t fw_major_version;

	if (IS_QLA82XX(ha)) {
		rval = ha->isp_ops->load_risc(vha, &srisc_address);
		if (rval == QLA_SUCCESS) {
			qla2x00_stop_firmware(vha);
			goto enable_82xx_npiv;
		} else
			goto failed;
	}

	if (!IS_FWI2_CAPABLE(ha) && !IS_QLA2100(ha) && !IS_QLA2200(ha)) {
		/* Disable SRAM, Instruction RAM and GP RAM parity.  */
		spin_lock_irqsave(&ha->hardware_lock, flags);
		WRT_REG_WORD(&reg->hccr, (HCCR_ENABLE_PARITY + 0x0));
		RD_REG_WORD(&reg->hccr);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}

	qla81xx_mpi_sync(vha);

	/* Load firmware sequences */
	rval = ha->isp_ops->load_risc(vha, &srisc_address);
	if (rval == QLA_SUCCESS) {
		DEBUG(printk("scsi(%ld): Verifying Checksum of loaded RISC "
		    "code.\n", vha->host_no));

		rval = qla2x00_verify_checksum(vha, srisc_address);
		if (rval == QLA_SUCCESS) {
			/* Start firmware execution. */
			DEBUG(printk("scsi(%ld): Checksum OK, start "
			    "firmware.\n", vha->host_no));

			rval = qla2x00_execute_fw(vha, srisc_address);
			/* Retrieve firmware information. */
			if (rval == QLA_SUCCESS) {
enable_82xx_npiv:
				fw_major_version = ha->fw_major_version;
				rval = qla2x00_get_fw_version(vha,
				    &ha->fw_major_version,
				    &ha->fw_minor_version,
				    &ha->fw_subminor_version,
				    &ha->fw_attributes, &ha->fw_memory_size,
				    ha->mpi_version, &ha->mpi_capabilities,
				    ha->phy_version);
				if (rval != QLA_SUCCESS)
					goto failed;
				ha->flags.npiv_supported = 0;
				if (IS_QLA2XXX_MIDTYPE(ha) &&
					 (ha->fw_attributes & BIT_2)) {
					ha->flags.npiv_supported = 1;
					if ((!ha->max_npiv_vports) ||
					    ((ha->max_npiv_vports + 1) %
					    MIN_MULTI_ID_FABRIC))
						ha->max_npiv_vports =
						    MIN_MULTI_ID_FABRIC - 1;
				}
				qla2x00_get_resource_cnts(vha, NULL,
				    &ha->fw_xcb_count, NULL, NULL,
				    &ha->max_npiv_vports, NULL);

				if (!fw_major_version && ql2xallocfwdump) {
					if (!IS_QLA82XX(ha))
						qla2x00_alloc_fw_dump(vha);
				}
			}
		} else {
			DEBUG2(printk(KERN_INFO
			    "scsi(%ld): ISP Firmware failed checksum.\n",
			    vha->host_no));
		}
	}

	if (!IS_FWI2_CAPABLE(ha) && !IS_QLA2100(ha) && !IS_QLA2200(ha)) {
		/* Enable proper parity. */
		spin_lock_irqsave(&ha->hardware_lock, flags);
		if (IS_QLA2300(ha))
			/* SRAM parity */
			WRT_REG_WORD(&reg->hccr, HCCR_ENABLE_PARITY + 0x1);
		else
			/* SRAM, Instruction RAM and GP RAM parity */
			WRT_REG_WORD(&reg->hccr, HCCR_ENABLE_PARITY + 0x7);
		RD_REG_WORD(&reg->hccr);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}

	if (rval == QLA_SUCCESS && IS_FAC_REQUIRED(ha)) {
		uint32_t size;

		rval = qla81xx_fac_get_sector_size(vha, &size);
		if (rval == QLA_SUCCESS) {
			ha->flags.fac_supported = 1;
			ha->fdt_block_size = size << 2;
		} else {
			qla_printk(KERN_ERR, ha,
			    "Unsupported FAC firmware (%d.%02d.%02d).\n",
			    ha->fw_major_version, ha->fw_minor_version,
			    ha->fw_subminor_version);
		}
	}
failed:
	if (rval) {
		DEBUG2_3(printk("scsi(%ld): Setup chip **** FAILED ****.\n",
		    vha->host_no));
	}

	return (rval);
}

/**
 * qla2x00_init_response_q_entries() - Initializes response queue entries.
 * @ha: HA context
 *
 * Beginning of request ring has initialization control block already built
 * by nvram config routine.
 *
 * Returns 0 on success.
 */
void
qla2x00_init_response_q_entries(struct rsp_que *rsp)
{
	uint16_t cnt;
	response_t *pkt;

	rsp->ring_ptr = rsp->ring;
	rsp->ring_index    = 0;
	rsp->status_srb = NULL;
	pkt = rsp->ring_ptr;
	for (cnt = 0; cnt < rsp->length; cnt++) {
		pkt->signature = RESPONSE_PROCESSED;
		pkt++;
	}
}

/**
 * qla2x00_update_fw_options() - Read and process firmware options.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
void
qla2x00_update_fw_options(scsi_qla_host_t *vha)
{
	uint16_t swing, emphasis, tx_sens, rx_sens;
	struct qla_hw_data *ha = vha->hw;

	memset(ha->fw_options, 0, sizeof(ha->fw_options));
	qla2x00_get_fw_options(vha, ha->fw_options);

	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		return;

	/* Serial Link options. */
	DEBUG3(printk("scsi(%ld): Serial link options:\n",
	    vha->host_no));
	DEBUG3(qla2x00_dump_buffer((uint8_t *)&ha->fw_seriallink_options,
	    sizeof(ha->fw_seriallink_options)));

	ha->fw_options[1] &= ~FO1_SET_EMPHASIS_SWING;
	if (ha->fw_seriallink_options[3] & BIT_2) {
		ha->fw_options[1] |= FO1_SET_EMPHASIS_SWING;

		/*  1G settings */
		swing = ha->fw_seriallink_options[2] & (BIT_2 | BIT_1 | BIT_0);
		emphasis = (ha->fw_seriallink_options[2] &
		    (BIT_4 | BIT_3)) >> 3;
		tx_sens = ha->fw_seriallink_options[0] &
		    (BIT_3 | BIT_2 | BIT_1 | BIT_0);
		rx_sens = (ha->fw_seriallink_options[0] &
		    (BIT_7 | BIT_6 | BIT_5 | BIT_4)) >> 4;
		ha->fw_options[10] = (emphasis << 14) | (swing << 8);
		if (IS_QLA2300(ha) || IS_QLA2312(ha) || IS_QLA6312(ha)) {
			if (rx_sens == 0x0)
				rx_sens = 0x3;
			ha->fw_options[10] |= (tx_sens << 4) | rx_sens;
		} else if (IS_QLA2322(ha) || IS_QLA6322(ha))
			ha->fw_options[10] |= BIT_5 |
			    ((rx_sens & (BIT_1 | BIT_0)) << 2) |
			    (tx_sens & (BIT_1 | BIT_0));

		/*  2G settings */
		swing = (ha->fw_seriallink_options[2] &
		    (BIT_7 | BIT_6 | BIT_5)) >> 5;
		emphasis = ha->fw_seriallink_options[3] & (BIT_1 | BIT_0);
		tx_sens = ha->fw_seriallink_options[1] &
		    (BIT_3 | BIT_2 | BIT_1 | BIT_0);
		rx_sens = (ha->fw_seriallink_options[1] &
		    (BIT_7 | BIT_6 | BIT_5 | BIT_4)) >> 4;
		ha->fw_options[11] = (emphasis << 14) | (swing << 8);
		if (IS_QLA2300(ha) || IS_QLA2312(ha) || IS_QLA6312(ha)) {
			if (rx_sens == 0x0)
				rx_sens = 0x3;
			ha->fw_options[11] |= (tx_sens << 4) | rx_sens;
		} else if (IS_QLA2322(ha) || IS_QLA6322(ha))
			ha->fw_options[11] |= BIT_5 |
			    ((rx_sens & (BIT_1 | BIT_0)) << 2) |
			    (tx_sens & (BIT_1 | BIT_0));
	}

	/* FCP2 options. */
	/*  Return command IOCBs without waiting for an ABTS to complete. */
	ha->fw_options[3] |= BIT_13;

	/* LED scheme. */
	if (ha->flags.enable_led_scheme)
		ha->fw_options[2] |= BIT_12;

	/* Detect ISP6312. */
	if (IS_QLA6312(ha))
		ha->fw_options[2] |= BIT_13;

	/* Update firmware options. */
	qla2x00_set_fw_options(vha, ha->fw_options);
}

void
qla24xx_update_fw_options(scsi_qla_host_t *vha)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;

	if (IS_QLA82XX(ha))
		return;

	/* Update Serial Link options. */
	if ((le16_to_cpu(ha->fw_seriallink_options24[0]) & BIT_0) == 0)
		return;

	rval = qla2x00_set_serdes_params(vha,
	    le16_to_cpu(ha->fw_seriallink_options24[1]),
	    le16_to_cpu(ha->fw_seriallink_options24[2]),
	    le16_to_cpu(ha->fw_seriallink_options24[3]));
	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to update Serial Link options (%x).\n", rval);
	}
}

void
qla2x00_config_rings(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	struct req_que *req = ha->req_q_map[0];
	struct rsp_que *rsp = ha->rsp_q_map[0];

	/* Setup ring parameters in initialization control block. */
	ha->init_cb->request_q_outpointer = __constant_cpu_to_le16(0);
	ha->init_cb->response_q_inpointer = __constant_cpu_to_le16(0);
	ha->init_cb->request_q_length = cpu_to_le16(req->length);
	ha->init_cb->response_q_length = cpu_to_le16(rsp->length);
	ha->init_cb->request_q_address[0] = cpu_to_le32(LSD(req->dma));
	ha->init_cb->request_q_address[1] = cpu_to_le32(MSD(req->dma));
	ha->init_cb->response_q_address[0] = cpu_to_le32(LSD(rsp->dma));
	ha->init_cb->response_q_address[1] = cpu_to_le32(MSD(rsp->dma));

	WRT_REG_WORD(ISP_REQ_Q_IN(ha, reg), 0);
	WRT_REG_WORD(ISP_REQ_Q_OUT(ha, reg), 0);
	WRT_REG_WORD(ISP_RSP_Q_IN(ha, reg), 0);
	WRT_REG_WORD(ISP_RSP_Q_OUT(ha, reg), 0);
	RD_REG_WORD(ISP_RSP_Q_OUT(ha, reg));		/* PCI Posting. */
}

void
qla24xx_config_rings(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	device_reg_t __iomem *reg = ISP_QUE_REG(ha, 0);
	struct device_reg_2xxx __iomem *ioreg = &ha->iobase->isp;
	struct qla_msix_entry *msix;
	struct init_cb_24xx *icb;
	uint16_t rid = 0;
	struct req_que *req = ha->req_q_map[0];
	struct rsp_que *rsp = ha->rsp_q_map[0];

/* Setup ring parameters in initialization control block. */
	icb = (struct init_cb_24xx *)ha->init_cb;
	icb->request_q_outpointer = __constant_cpu_to_le16(0);
	icb->response_q_inpointer = __constant_cpu_to_le16(0);
	icb->request_q_length = cpu_to_le16(req->length);
	icb->response_q_length = cpu_to_le16(rsp->length);
	icb->request_q_address[0] = cpu_to_le32(LSD(req->dma));
	icb->request_q_address[1] = cpu_to_le32(MSD(req->dma));
	icb->response_q_address[0] = cpu_to_le32(LSD(rsp->dma));
	icb->response_q_address[1] = cpu_to_le32(MSD(rsp->dma));

	if (ha->mqenable) {
		icb->qos = __constant_cpu_to_le16(QLA_DEFAULT_QUE_QOS);
		icb->rid = __constant_cpu_to_le16(rid);
		if (ha->flags.msix_enabled) {
			msix = &ha->msix_entries[1];
			DEBUG2_17(printk(KERN_INFO
			"Registering vector 0x%x for base que\n", msix->entry));
			icb->msix = cpu_to_le16(msix->entry);
		}
		/* Use alternate PCI bus number */
		if (MSB(rid))
			icb->firmware_options_2 |=
				__constant_cpu_to_le32(BIT_19);
		/* Use alternate PCI devfn */
		if (LSB(rid))
			icb->firmware_options_2 |=
				__constant_cpu_to_le32(BIT_18);

		/* Use Disable MSIX Handshake mode for capable adapters */
		if (IS_MSIX_NACK_CAPABLE(ha)) {
			icb->firmware_options_2 &=
				__constant_cpu_to_le32(~BIT_22);
			ha->flags.disable_msix_handshake = 1;
			qla_printk(KERN_INFO, ha,
				"MSIX Handshake Disable Mode turned on\n");
		} else {
			icb->firmware_options_2 |=
				__constant_cpu_to_le32(BIT_22);
		}
		icb->firmware_options_2 |= __constant_cpu_to_le32(BIT_23);

		WRT_REG_DWORD(&reg->isp25mq.req_q_in, 0);
		WRT_REG_DWORD(&reg->isp25mq.req_q_out, 0);
		WRT_REG_DWORD(&reg->isp25mq.rsp_q_in, 0);
		WRT_REG_DWORD(&reg->isp25mq.rsp_q_out, 0);
	} else {
		WRT_REG_DWORD(&reg->isp24.req_q_in, 0);
		WRT_REG_DWORD(&reg->isp24.req_q_out, 0);
		WRT_REG_DWORD(&reg->isp24.rsp_q_in, 0);
		WRT_REG_DWORD(&reg->isp24.rsp_q_out, 0);
	}
	/* PCI posting */
	RD_REG_DWORD(&ioreg->hccr);
}

/**
 * qla2x00_init_rings() - Initializes firmware.
 * @ha: HA context
 *
 * Beginning of request ring has initialization control block already built
 * by nvram config routine.
 *
 * Returns 0 on success.
 */
static int
qla2x00_init_rings(scsi_qla_host_t *vha)
{
	int	rval;
	unsigned long flags = 0;
	int cnt, que;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req;
	struct rsp_que *rsp;
	struct scsi_qla_host *vp;
	struct mid_init_cb_24xx *mid_init_cb =
	    (struct mid_init_cb_24xx *) ha->init_cb;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Clear outstanding commands array. */
	for (que = 0; que < ha->max_req_queues; que++) {
		req = ha->req_q_map[que];
		if (!req)
			continue;
		for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++)
			req->outstanding_cmds[cnt] = NULL;

		req->current_outstanding_cmd = 1;

		/* Initialize firmware. */
		req->ring_ptr  = req->ring;
		req->ring_index    = 0;
		req->cnt      = req->length;
	}

	for (que = 0; que < ha->max_rsp_queues; que++) {
		rsp = ha->rsp_q_map[que];
		if (!rsp)
			continue;
		/* Initialize response queue entries */
		qla2x00_init_response_q_entries(rsp);
	}

	spin_lock(&ha->vport_slock);
	/* Clear RSCN queue. */
	list_for_each_entry(vp, &ha->vp_list, list) {
		vp->rscn_in_ptr = 0;
		vp->rscn_out_ptr = 0;
	}

	spin_unlock(&ha->vport_slock);

	ha->isp_ops->config_rings(vha);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Update any ISP specific firmware options before initialization. */
	ha->isp_ops->update_fw_options(vha);

	DEBUG(printk("scsi(%ld): Issue init firmware.\n", vha->host_no));

	if (ha->flags.npiv_supported) {
		if (ha->operating_mode == LOOP)
			ha->max_npiv_vports = MIN_MULTI_ID_FABRIC - 1;
		mid_init_cb->count = cpu_to_le16(ha->max_npiv_vports);
	}

	if (IS_FWI2_CAPABLE(ha)) {
		mid_init_cb->options = __constant_cpu_to_le16(BIT_1);
		mid_init_cb->init_cb.execution_throttle =
		    cpu_to_le16(ha->fw_xcb_count);
	}

	rval = qla2x00_init_firmware(vha, ha->init_cb_size);
	if (rval) {
		DEBUG2_3(printk("scsi(%ld): Init firmware **** FAILED ****.\n",
		    vha->host_no));
	} else {
		DEBUG3(printk("scsi(%ld): Init firmware -- success.\n",
		    vha->host_no));
	}

	return (rval);
}

/**
 * qla2x00_fw_ready() - Waits for firmware ready.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
static int
qla2x00_fw_ready(scsi_qla_host_t *vha)
{
	int		rval;
	unsigned long	wtime, mtime, cs84xx_time;
	uint16_t	min_wait;	/* Minimum wait time if loop is down */
	uint16_t	wait_time;	/* Wait time if loop is coming ready */
	uint16_t	state[5];
	struct qla_hw_data *ha = vha->hw;

	rval = QLA_SUCCESS;

	/* 20 seconds for loop down. */
	min_wait = 20;

	/*
	 * Firmware should take at most one RATOV to login, plus 5 seconds for
	 * our own processing.
	 */
	if ((wait_time = (ha->retry_count*ha->login_timeout) + 5) < min_wait) {
		wait_time = min_wait;
	}

	/* Min wait time if loop down */
	mtime = jiffies + (min_wait * HZ);

	/* wait time before firmware ready */
	wtime = jiffies + (wait_time * HZ);

	/* Wait for ISP to finish LIP */
	if (!vha->flags.init_done)
 		qla_printk(KERN_INFO, ha, "Waiting for LIP to complete...\n");

	DEBUG3(printk("scsi(%ld): Waiting for LIP to complete...\n",
	    vha->host_no));

	do {
		rval = qla2x00_get_firmware_state(vha, state);
		if (rval == QLA_SUCCESS) {
			if (state[0] < FSTATE_LOSS_OF_SYNC) {
				vha->device_flags &= ~DFLG_NO_CABLE;
			}
			if (IS_QLA84XX(ha) && state[0] != FSTATE_READY) {
				DEBUG16(printk("scsi(%ld): fw_state=%x "
				    "84xx=%x.\n", vha->host_no, state[0],
				    state[2]));
				if ((state[2] & FSTATE_LOGGED_IN) &&
				     (state[2] & FSTATE_WAITING_FOR_VERIFY)) {
					DEBUG16(printk("scsi(%ld): Sending "
					    "verify iocb.\n", vha->host_no));

					cs84xx_time = jiffies;
					rval = qla84xx_init_chip(vha);
					if (rval != QLA_SUCCESS)
						break;

					/* Add time taken to initialize. */
					cs84xx_time = jiffies - cs84xx_time;
					wtime += cs84xx_time;
					mtime += cs84xx_time;
					DEBUG16(printk("scsi(%ld): Increasing "
					    "wait time by %ld. New time %ld\n",
					    vha->host_no, cs84xx_time, wtime));
				}
			} else if (state[0] == FSTATE_READY) {
				DEBUG(printk("scsi(%ld): F/W Ready - OK \n",
				    vha->host_no));

				qla2x00_get_retry_cnt(vha, &ha->retry_count,
				    &ha->login_timeout, &ha->r_a_tov);

				rval = QLA_SUCCESS;
				break;
			}

			rval = QLA_FUNCTION_FAILED;

			if (atomic_read(&vha->loop_down_timer) &&
			    state[0] != FSTATE_READY) {
				/* Loop down. Timeout on min_wait for states
				 * other than Wait for Login.
				 */
				if (time_after_eq(jiffies, mtime)) {
					qla_printk(KERN_INFO, ha,
					    "Cable is unplugged...\n");

					vha->device_flags |= DFLG_NO_CABLE;
					break;
				}
			}
		} else {
			/* Mailbox cmd failed. Timeout on min_wait. */
			if (time_after_eq(jiffies, mtime) ||
			    (IS_QLA82XX(ha) && ha->flags.fw_hung))
				break;
		}

		if (time_after_eq(jiffies, wtime))
			break;

		/* Delay for a while */
		msleep(500);

		DEBUG3(printk("scsi(%ld): fw_state=%x curr time=%lx.\n",
		    vha->host_no, state[0], jiffies));
	} while (1);

	DEBUG(printk("scsi(%ld): fw_state=%x (%x, %x, %x, %x) curr time=%lx.\n",
	    vha->host_no, state[0], state[1], state[2], state[3], state[4],
	    jiffies));

	if (rval) {
		DEBUG2_3(printk("scsi(%ld): Firmware ready **** FAILED ****.\n",
		    vha->host_no));
	}

	return (rval);
}

/*
*  qla2x00_configure_hba
*      Setup adapter context.
*
* Input:
*      ha = adapter state pointer.
*
* Returns:
*      0 = success
*
* Context:
*      Kernel context.
*/
static int
qla2x00_configure_hba(scsi_qla_host_t *vha)
{
	int       rval;
	uint16_t      loop_id;
	uint16_t      topo;
	uint16_t      sw_cap;
	uint8_t       al_pa;
	uint8_t       area;
	uint8_t       domain;
	char		connect_type[22];
	struct qla_hw_data *ha = vha->hw;

	/* Get host addresses. */
	rval = qla2x00_get_adapter_id(vha,
	    &loop_id, &al_pa, &area, &domain, &topo, &sw_cap);
	if (rval != QLA_SUCCESS) {
		if (LOOP_TRANSITION(vha) || atomic_read(&ha->loop_down_timer) ||
		    IS_QLA8XXX_TYPE(ha) ||
		    (rval == QLA_COMMAND_ERROR && loop_id == 0x7)) {
			DEBUG2(printk("%s(%ld) Loop is in a transition state\n",
			    __func__, vha->host_no));
		} else {
			qla_printk(KERN_WARNING, ha,
			    "ERROR -- Unable to get host loop ID.\n");
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		}
		return (rval);
	}

	if (topo == 4) {
		qla_printk(KERN_INFO, ha,
			"Cannot get topology - retrying.\n");
		return (QLA_FUNCTION_FAILED);
	}

	vha->loop_id = loop_id;

	/* initialize */
	ha->min_external_loopid = SNS_FIRST_LOOP_ID;
	ha->operating_mode = LOOP;
	ha->switch_cap = 0;

	switch (topo) {
	case 0:
		DEBUG3(printk("scsi(%ld): HBA in NL topology.\n",
		    vha->host_no));
		ha->current_topology = ISP_CFG_NL;
		strcpy(connect_type, "(Loop)");
		break;

	case 1:
		DEBUG3(printk("scsi(%ld): HBA in FL topology.\n",
		    vha->host_no));
		ha->switch_cap = sw_cap;
		ha->current_topology = ISP_CFG_FL;
		strcpy(connect_type, "(FL_Port)");
		break;

	case 2:
		DEBUG3(printk("scsi(%ld): HBA in N P2P topology.\n",
		    vha->host_no));
		ha->operating_mode = P2P;
		ha->current_topology = ISP_CFG_N;
		strcpy(connect_type, "(N_Port-to-N_Port)");
		break;

	case 3:
		DEBUG3(printk("scsi(%ld): HBA in F P2P topology.\n",
		    vha->host_no));
		ha->switch_cap = sw_cap;
		ha->operating_mode = P2P;
		ha->current_topology = ISP_CFG_F;
		strcpy(connect_type, "(F_Port)");
		break;

	default:
		DEBUG3(printk("scsi(%ld): HBA in unknown topology %x. "
		    "Using NL.\n",
		    vha->host_no, topo));
		ha->current_topology = ISP_CFG_NL;
		strcpy(connect_type, "(Loop)");
		break;
	}

	/* Save Host port and loop ID. */
	/* byte order - Big Endian */
	vha->d_id.b.domain = domain;
	vha->d_id.b.area = area;
	vha->d_id.b.al_pa = al_pa;

	if (!vha->flags.init_done)
 		qla_printk(KERN_INFO, ha,
		    "Topology - %s, Host Loop address 0x%x\n",
		    connect_type, vha->loop_id);

	if (rval) {
		DEBUG2_3(printk("scsi(%ld): FAILED.\n", vha->host_no));
	} else {
		DEBUG3(printk("scsi(%ld): exiting normally.\n", vha->host_no));
	}

	return(rval);
}

inline void
qla2x00_set_model_info(scsi_qla_host_t *vha, uint8_t *model, size_t len,
	char *def)
{
	char *st, *en;
	uint16_t index;
	struct qla_hw_data *ha = vha->hw;
	int use_tbl = !IS_QLA24XX_TYPE(ha) && !IS_QLA25XX(ha) &&
	    !IS_QLA8XXX_TYPE(ha);

	if (memcmp(model, BINZERO, len) != 0) {
		strncpy(ha->model_number, model, len);
		st = en = ha->model_number;
		en += len - 1;
		while (en > st) {
			if (*en != 0x20 && *en != 0x00)
				break;
			*en-- = '\0';
		}

		index = (ha->pdev->subsystem_device & 0xff);
		if (use_tbl &&
		    ha->pdev->subsystem_vendor == PCI_VENDOR_ID_QLOGIC &&
		    index < QLA_MODEL_NAMES)
			strncpy(ha->model_desc,
			    qla2x00_model_name[index * 2 + 1],
			    sizeof(ha->model_desc) - 1);
	} else {
		index = (ha->pdev->subsystem_device & 0xff);
		if (use_tbl &&
		    ha->pdev->subsystem_vendor == PCI_VENDOR_ID_QLOGIC &&
		    index < QLA_MODEL_NAMES) {
			strcpy(ha->model_number,
			    qla2x00_model_name[index * 2]);
			strncpy(ha->model_desc,
			    qla2x00_model_name[index * 2 + 1],
			    sizeof(ha->model_desc) - 1);
		} else {
			strcpy(ha->model_number, def);
		}
	}
	if (IS_FWI2_CAPABLE(ha))
		qla2xxx_get_vpd_field(vha, "\x82", ha->model_desc,
		    sizeof(ha->model_desc));
}

/* On sparc systems, obtain port and node WWN from firmware
 * properties.
 */
static void qla2xxx_nvram_wwn_from_ofw(scsi_qla_host_t *vha, nvram_t *nv)
{
#ifdef CONFIG_SPARC
	struct qla_hw_data *ha = vha->hw;
	struct pci_dev *pdev = ha->pdev;
	struct device_node *dp = pci_device_to_OF_node(pdev);
	const u8 *val;
	int len;

	val = of_get_property(dp, "port-wwn", &len);
	if (val && len >= WWN_SIZE)
		memcpy(nv->port_name, val, WWN_SIZE);

	val = of_get_property(dp, "node-wwn", &len);
	if (val && len >= WWN_SIZE)
		memcpy(nv->node_name, val, WWN_SIZE);
#endif
}

/*
* NVRAM configuration for ISP 2xxx
*
* Input:
*      ha                = adapter block pointer.
*
* Output:
*      initialization control block in response_ring
*      host adapters parameters in host adapter block
*
* Returns:
*      0 = success.
*/
int
qla2x00_nvram_config(scsi_qla_host_t *vha)
{
	int             rval;
	uint8_t         chksum = 0;
	uint16_t        cnt;
	uint8_t         *dptr1, *dptr2;
	struct qla_hw_data *ha = vha->hw;
	init_cb_t       *icb = ha->init_cb;
	nvram_t         *nv = ha->nvram;
	uint8_t         *ptr = ha->nvram;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	rval = QLA_SUCCESS;

	/* Determine NVRAM starting address. */
	ha->nvram_size = sizeof(nvram_t);
	ha->nvram_base = 0;
	if (!IS_QLA2100(ha) && !IS_QLA2200(ha) && !IS_QLA2300(ha))
		if ((RD_REG_WORD(&reg->ctrl_status) >> 14) == 1)
			ha->nvram_base = 0x80;

	/* Get NVRAM data and calculate checksum. */
	ha->isp_ops->read_nvram(vha, ptr, ha->nvram_base, ha->nvram_size);
	for (cnt = 0, chksum = 0; cnt < ha->nvram_size; cnt++)
		chksum += *ptr++;

	DEBUG5(printk("scsi(%ld): Contents of NVRAM\n", vha->host_no));
	DEBUG5(qla2x00_dump_buffer((uint8_t *)nv, ha->nvram_size));

	/* Bad NVRAM data, set defaults parameters. */
	if (chksum || nv->id[0] != 'I' || nv->id[1] != 'S' ||
	    nv->id[2] != 'P' || nv->id[3] != ' ' || nv->nvram_version < 1) {
		/* Reset NVRAM data. */
		qla_printk(KERN_WARNING, ha, "Inconsistent NVRAM detected: "
		    "checksum=0x%x id=%c version=0x%x.\n", chksum, nv->id[0],
		    nv->nvram_version);
		qla_printk(KERN_WARNING, ha, "Falling back to functioning (yet "
		    "invalid -- WWPN) defaults.\n");

		/*
		 * Set default initialization control block.
		 */
		memset(nv, 0, ha->nvram_size);
		nv->parameter_block_version = ICB_VERSION;

		if (IS_QLA23XX(ha)) {
			nv->firmware_options[0] = BIT_2 | BIT_1;
			nv->firmware_options[1] = BIT_7 | BIT_5;
			nv->add_firmware_options[0] = BIT_5;
			nv->add_firmware_options[1] = BIT_5 | BIT_4;
			nv->frame_payload_size = __constant_cpu_to_le16(2048);
			nv->special_options[1] = BIT_7;
		} else if (IS_QLA2200(ha)) {
			nv->firmware_options[0] = BIT_2 | BIT_1;
			nv->firmware_options[1] = BIT_7 | BIT_5;
			nv->add_firmware_options[0] = BIT_5;
			nv->add_firmware_options[1] = BIT_5 | BIT_4;
			nv->frame_payload_size = __constant_cpu_to_le16(1024);
		} else if (IS_QLA2100(ha)) {
			nv->firmware_options[0] = BIT_3 | BIT_1;
			nv->firmware_options[1] = BIT_5;
			nv->frame_payload_size = __constant_cpu_to_le16(1024);
		}

		nv->max_iocb_allocation = __constant_cpu_to_le16(256);
		nv->execution_throttle = __constant_cpu_to_le16(16);
		nv->retry_count = 8;
		nv->retry_delay = 1;

		nv->port_name[0] = 33;
		nv->port_name[3] = 224;
		nv->port_name[4] = 139;

		qla2xxx_nvram_wwn_from_ofw(vha, nv);

		nv->login_timeout = 4;

		/*
		 * Set default host adapter parameters
		 */
		nv->host_p[1] = BIT_2;
		nv->reset_delay = 5;
		nv->port_down_retry_count = 8;
		nv->max_luns_per_target = __constant_cpu_to_le16(8);
		nv->link_down_timeout = 60;

		rval = 1;
	}

#if defined(CONFIG_IA64_GENERIC) || defined(CONFIG_IA64_SGI_SN2)
	/*
	 * The SN2 does not provide BIOS emulation which means you can't change
	 * potentially bogus BIOS settings. Force the use of default settings
	 * for link rate and frame size.  Hope that the rest of the settings
	 * are valid.
	 */
	if (ia64_platform_is("sn2")) {
		nv->frame_payload_size = __constant_cpu_to_le16(2048);
		if (IS_QLA23XX(ha))
			nv->special_options[1] = BIT_7;
	}
#endif

	/* Reset Initialization control block */
	memset(icb, 0, ha->init_cb_size);

	/*
	 * Setup driver NVRAM options.
	 */
	nv->firmware_options[0] |= (BIT_6 | BIT_1);
	nv->firmware_options[0] &= ~(BIT_5 | BIT_4);
	nv->firmware_options[1] |= (BIT_5 | BIT_0);
	nv->firmware_options[1] &= ~BIT_4;

	if (IS_QLA23XX(ha)) {
		nv->firmware_options[0] |= BIT_2;
		nv->firmware_options[0] &= ~BIT_3;
		nv->firmware_options[0] &= ~BIT_6;
		nv->add_firmware_options[1] |= BIT_5 | BIT_4;

		if (IS_QLA2300(ha)) {
			if (ha->fb_rev == FPM_2310) {
				strcpy(ha->model_number, "QLA2310");
			} else {
				strcpy(ha->model_number, "QLA2300");
			}
		} else {
			qla2x00_set_model_info(vha, nv->model_number,
			    sizeof(nv->model_number), "QLA23xx");
		}
	} else if (IS_QLA2200(ha)) {
		nv->firmware_options[0] |= BIT_2;
		/*
		 * 'Point-to-point preferred, else loop' is not a safe
		 * connection mode setting.
		 */
		if ((nv->add_firmware_options[0] & (BIT_6 | BIT_5 | BIT_4)) ==
		    (BIT_5 | BIT_4)) {
			/* Force 'loop preferred, else point-to-point'. */
			nv->add_firmware_options[0] &= ~(BIT_6 | BIT_5 | BIT_4);
			nv->add_firmware_options[0] |= BIT_5;
		}
		strcpy(ha->model_number, "QLA22xx");
	} else /*if (IS_QLA2100(ha))*/ {
		strcpy(ha->model_number, "QLA2100");
	}

	/*
	 * Copy over NVRAM RISC parameter block to initialization control block.
	 */
	dptr1 = (uint8_t *)icb;
	dptr2 = (uint8_t *)&nv->parameter_block_version;
	cnt = (uint8_t *)&icb->request_q_outpointer - (uint8_t *)&icb->version;
	while (cnt--)
		*dptr1++ = *dptr2++;

	/* Copy 2nd half. */
	dptr1 = (uint8_t *)icb->add_firmware_options;
	cnt = (uint8_t *)icb->reserved_3 - (uint8_t *)icb->add_firmware_options;
	while (cnt--)
		*dptr1++ = *dptr2++;

	/* Use alternate WWN? */
	if (nv->host_p[1] & BIT_7) {
		memcpy(icb->node_name, nv->alternate_node_name, WWN_SIZE);
		memcpy(icb->port_name, nv->alternate_port_name, WWN_SIZE);
	}

	/* Prepare nodename */
	if ((icb->firmware_options[1] & BIT_6) == 0) {
		/*
		 * Firmware will apply the following mask if the nodename was
		 * not provided.
		 */
		memcpy(icb->node_name, icb->port_name, WWN_SIZE);
		icb->node_name[0] &= 0xF0;
	}

	/*
	 * Set host adapter parameters.
	 */
	if (nv->host_p[0] & BIT_7)
		ql2xextended_error_logging = 1;
	ha->flags.disable_risc_code_load = ((nv->host_p[0] & BIT_4) ? 1 : 0);
	/* Always load RISC code on non ISP2[12]00 chips. */
	if (!IS_QLA2100(ha) && !IS_QLA2200(ha))
		ha->flags.disable_risc_code_load = 0;
	ha->flags.enable_lip_reset = ((nv->host_p[1] & BIT_1) ? 1 : 0);
	ha->flags.enable_lip_full_login = ((nv->host_p[1] & BIT_2) ? 1 : 0);
	ha->flags.enable_target_reset = ((nv->host_p[1] & BIT_3) ? 1 : 0);
	ha->flags.enable_led_scheme = (nv->special_options[1] & BIT_4) ? 1 : 0;
	ha->flags.disable_serdes = 0;

	ha->operating_mode =
	    (icb->add_firmware_options[0] & (BIT_6 | BIT_5 | BIT_4)) >> 4;

	memcpy(ha->fw_seriallink_options, nv->seriallink_options,
	    sizeof(ha->fw_seriallink_options));

	/* save HBA serial number */
	ha->serial0 = icb->port_name[5];
	ha->serial1 = icb->port_name[6];
	ha->serial2 = icb->port_name[7];
	memcpy(vha->node_name, icb->node_name, WWN_SIZE);
	memcpy(vha->port_name, icb->port_name, WWN_SIZE);

	icb->execution_throttle = __constant_cpu_to_le16(0xFFFF);

	ha->retry_count = nv->retry_count;

	/* Set minimum login_timeout to 4 seconds. */
	if (nv->login_timeout != ql2xlogintimeout)
		nv->login_timeout = ql2xlogintimeout;
	if (nv->login_timeout < 4)
		nv->login_timeout = 4;
	ha->login_timeout = nv->login_timeout;
	icb->login_timeout = nv->login_timeout;

	/* Set minimum RATOV to 100 tenths of a second. */
	ha->r_a_tov = 100;

	ha->loop_reset_delay = nv->reset_delay;

	/* Link Down Timeout = 0:
	 *
	 * 	When Port Down timer expires we will start returning
	 *	I/O's to OS with "DID_NO_CONNECT".
	 *
	 * Link Down Timeout != 0:
	 *
	 *	 The driver waits for the link to come up after link down
	 *	 before returning I/Os to OS with "DID_NO_CONNECT".
	 */
	if (nv->link_down_timeout == 0) {
		ha->loop_down_abort_time =
		    (LOOP_DOWN_TIME - LOOP_DOWN_TIMEOUT);
	} else {
		ha->link_down_timeout =	 nv->link_down_timeout;
		ha->loop_down_abort_time =
		    (LOOP_DOWN_TIME - ha->link_down_timeout);
	}

	/*
	 * Need enough time to try and get the port back.
	 */
	ha->port_down_retry_count = nv->port_down_retry_count;
	if (qlport_down_retry)
		ha->port_down_retry_count = qlport_down_retry;
	/* Set login_retry_count */
	ha->login_retry_count  = nv->retry_count;
	if (ha->port_down_retry_count == nv->port_down_retry_count &&
	    ha->port_down_retry_count > 3)
		ha->login_retry_count = ha->port_down_retry_count;
	else if (ha->port_down_retry_count > (int)ha->login_retry_count)
		ha->login_retry_count = ha->port_down_retry_count;
	if (ql2xloginretrycount)
		ha->login_retry_count = ql2xloginretrycount;

	icb->lun_enables = __constant_cpu_to_le16(0);
	icb->command_resource_count = 0;
	icb->immediate_notify_resource_count = 0;
	icb->timeout = __constant_cpu_to_le16(0);

	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		/* Enable RIO */
		icb->firmware_options[0] &= ~BIT_3;
		icb->add_firmware_options[0] &=
		    ~(BIT_3 | BIT_2 | BIT_1 | BIT_0);
		icb->add_firmware_options[0] |= BIT_2;
		icb->response_accumulation_timer = 3;
		icb->interrupt_delay_timer = 5;

		vha->flags.process_response_queue = 1;
	} else {
		/* Enable ZIO. */
		if (!vha->flags.init_done) {
			ha->zio_mode = icb->add_firmware_options[0] &
			    (BIT_3 | BIT_2 | BIT_1 | BIT_0);
			ha->zio_timer = icb->interrupt_delay_timer ?
			    icb->interrupt_delay_timer: 2;
		}
		icb->add_firmware_options[0] &=
		    ~(BIT_3 | BIT_2 | BIT_1 | BIT_0);
		vha->flags.process_response_queue = 0;
		if (ha->zio_mode != QLA_ZIO_DISABLED) {
			ha->zio_mode = QLA_ZIO_MODE_6;

			DEBUG2(printk("scsi(%ld): ZIO mode %d enabled; timer "
			    "delay (%d us).\n", vha->host_no, ha->zio_mode,
			    ha->zio_timer * 100));
			qla_printk(KERN_INFO, ha,
			    "ZIO mode %d enabled; timer delay (%d us).\n",
			    ha->zio_mode, ha->zio_timer * 100);

			icb->add_firmware_options[0] |= (uint8_t)ha->zio_mode;
			icb->interrupt_delay_timer = (uint8_t)ha->zio_timer;
			vha->flags.process_response_queue = 1;
		}
	}

	if (rval) {
		DEBUG2_3(printk(KERN_WARNING
		    "scsi(%ld): NVRAM configuration failed!\n", vha->host_no));
	}
	return (rval);
}

static void
qla2x00_rport_del(void *data)
{
	fc_port_t *fcport = data;
	struct fc_rport *rport;

	spin_lock_irq(fcport->vha->host->host_lock);
	rport = fcport->drport ? fcport->drport: fcport->rport;
	fcport->drport = NULL;
	spin_unlock_irq(fcport->vha->host->host_lock);
	if (rport)
		fc_remote_port_delete(rport);
}

/**
 * qla2x00_alloc_fcport() - Allocate a generic fcport.
 * @ha: HA context
 * @flags: allocation flags
 *
 * Returns a pointer to the allocated fcport, or NULL, if none available.
 */
fc_port_t *
qla2x00_alloc_fcport(scsi_qla_host_t *vha, gfp_t flags)
{
	fc_port_t *fcport;

	fcport = kzalloc(sizeof(fc_port_t), flags);
	if (!fcport)
		return NULL;

	/* Setup fcport template structure. */
	fcport->vha = vha;
	fcport->vp_idx = vha->vp_idx;
	fcport->port_type = FCT_UNKNOWN;
	fcport->loop_id = FC_NO_LOOP_ID;
	atomic_set(&fcport->state, FCS_UNCONFIGURED);
	fcport->supported_classes = FC_COS_UNSPECIFIED;

	return fcport;
}

/*
 * qla2x00_configure_loop
 *      Updates Fibre Channel Device Database with what is actually on loop.
 *
 * Input:
 *      ha                = adapter block pointer.
 *
 * Returns:
 *      0 = success.
 *      1 = error.
 *      2 = database was full and device was not configured.
 */
static int
qla2x00_configure_loop(scsi_qla_host_t *vha)
{
	int  rval;
	unsigned long flags, save_flags;
	struct qla_hw_data *ha = vha->hw;
	rval = QLA_SUCCESS;

	/* Get Initiator ID */
	if (test_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags)) {
		rval = qla2x00_configure_hba(vha);
		if (rval != QLA_SUCCESS) {
			DEBUG(printk("scsi(%ld): Unable to configure HBA.\n",
			    vha->host_no));
			return (rval);
		}
	}

	save_flags = flags = vha->dpc_flags;
	DEBUG(printk("scsi(%ld): Configure loop -- dpc flags =0x%lx\n",
	    vha->host_no, flags));

	/*
	 * If we have both an RSCN and PORT UPDATE pending then handle them
	 * both at the same time.
	 */
	clear_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags);
	clear_bit(RSCN_UPDATE, &vha->dpc_flags);

	qla2x00_get_data_rate(vha);

	/* Determine what we need to do */
	if (ha->current_topology == ISP_CFG_FL &&
	    (test_bit(LOCAL_LOOP_UPDATE, &flags))) {

		vha->flags.rscn_queue_overflow = 1;
		set_bit(RSCN_UPDATE, &flags);

	} else if (ha->current_topology == ISP_CFG_F &&
	    (test_bit(LOCAL_LOOP_UPDATE, &flags))) {

		vha->flags.rscn_queue_overflow = 1;
		set_bit(RSCN_UPDATE, &flags);
		clear_bit(LOCAL_LOOP_UPDATE, &flags);

	} else if (ha->current_topology == ISP_CFG_N) {
		clear_bit(RSCN_UPDATE, &flags);

	} else if (!vha->flags.online ||
	    (test_bit(ABORT_ISP_ACTIVE, &flags))) {

		vha->flags.rscn_queue_overflow = 1;
		set_bit(RSCN_UPDATE, &flags);
		set_bit(LOCAL_LOOP_UPDATE, &flags);
	}

	if (test_bit(LOCAL_LOOP_UPDATE, &flags)) {
		if (test_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags))
			rval = QLA_FUNCTION_FAILED;
		else
			rval = qla2x00_configure_local_loop(vha);
	}

	if (rval == QLA_SUCCESS && test_bit(RSCN_UPDATE, &flags)) {
		if (LOOP_TRANSITION(vha))
			rval = QLA_FUNCTION_FAILED;
		else
			rval = qla2x00_configure_fabric(vha);
	}

	if (rval == QLA_SUCCESS) {
		if (atomic_read(&vha->loop_down_timer) ||
		    test_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags)) {
			rval = QLA_FUNCTION_FAILED;
		} else {
			atomic_set(&vha->loop_state, LOOP_READY);

			DEBUG(printk("scsi(%ld): LOOP READY\n", vha->host_no));
		}
	}

	if (rval) {
		DEBUG2_3(printk("%s(%ld): *** FAILED ***\n",
		    __func__, vha->host_no));
	} else {
		DEBUG3(printk("%s: exiting normally\n", __func__));
	}

	/* Restore state if a resync event occurred during processing */
	if (test_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags)) {
		if (test_bit(LOCAL_LOOP_UPDATE, &save_flags))
			set_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags);
		if (test_bit(RSCN_UPDATE, &save_flags)) {
			set_bit(RSCN_UPDATE, &vha->dpc_flags);
			if (!IS_ALOGIO_CAPABLE(ha))
				vha->flags.rscn_queue_overflow = 1;
		}
	}

	return (rval);
}



/*
 * qla2x00_configure_local_loop
 *	Updates Fibre Channel Device Database with local loop devices.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Returns:
 *	0 = success.
 */
static int
qla2x00_configure_local_loop(scsi_qla_host_t *vha)
{
	int		rval, rval2;
	int		found_devs;
	int		found;
	fc_port_t	*fcport, *new_fcport;

	uint16_t	index;
	uint16_t	entries;
	char		*id_iter;
	uint16_t	loop_id;
	uint8_t		domain, area, al_pa;
	struct qla_hw_data *ha = vha->hw;

	found_devs = 0;
	new_fcport = NULL;
	entries = MAX_FIBRE_DEVICES;

	DEBUG3(printk("scsi(%ld): Getting FCAL position map\n", vha->host_no));
	DEBUG3(qla2x00_get_fcal_position_map(vha, NULL));

	/* Get list of logged in devices. */
	memset(ha->gid_list, 0, GID_LIST_SIZE);
	rval = qla2x00_get_id_list(vha, ha->gid_list, ha->gid_list_dma,
	    &entries);
	if (rval != QLA_SUCCESS)
		goto cleanup_allocation;

	DEBUG3(printk("scsi(%ld): Entries in ID list (%d)\n",
	    vha->host_no, entries));
	DEBUG3(qla2x00_dump_buffer((uint8_t *)ha->gid_list,
	    entries * sizeof(struct gid_list_info)));

	/* Allocate temporary fcport for any new fcports discovered. */
	new_fcport = qla2x00_alloc_fcport(vha, GFP_KERNEL);
	if (new_fcport == NULL) {
		rval = QLA_MEMORY_ALLOC_FAILED;
		goto cleanup_allocation;
	}
	new_fcport->flags &= ~FCF_FABRIC_DEVICE;

	/*
	 * Mark local devices that were present with FCF_DEVICE_LOST for now.
	 */
	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		if (atomic_read(&fcport->state) == FCS_ONLINE &&
		    fcport->port_type != FCT_BROADCAST &&
		    (fcport->flags & FCF_FABRIC_DEVICE) == 0) {

			DEBUG(printk("scsi(%ld): Marking port lost, "
			    "loop_id=0x%04x\n",
			    vha->host_no, fcport->loop_id));

			atomic_set(&fcport->state, FCS_DEVICE_LOST);
		}
	}

	/* Add devices to port list. */
	id_iter = (char *)ha->gid_list;
	for (index = 0; index < entries; index++) {
		domain = ((struct gid_list_info *)id_iter)->domain;
		area = ((struct gid_list_info *)id_iter)->area;
		al_pa = ((struct gid_list_info *)id_iter)->al_pa;
		if (IS_QLA2100(ha) || IS_QLA2200(ha))
			loop_id = (uint16_t)
			    ((struct gid_list_info *)id_iter)->loop_id_2100;
		else
			loop_id = le16_to_cpu(
			    ((struct gid_list_info *)id_iter)->loop_id);
		id_iter += ha->gid_list_info_size;

		/* Bypass reserved domain fields. */
		if ((domain & 0xf0) == 0xf0)
			continue;

		/* Bypass if not same domain and area of adapter. */
		if (area && domain &&
		    (area != vha->d_id.b.area || domain != vha->d_id.b.domain))
			continue;

		/* Bypass invalid local loop ID. */
		if (loop_id > LAST_LOCAL_LOOP_ID)
			continue;

		/* Fill in member data. */
		new_fcport->d_id.b.domain = domain;
		new_fcport->d_id.b.area = area;
		new_fcport->d_id.b.al_pa = al_pa;
		new_fcport->loop_id = loop_id;
		new_fcport->vp_idx = vha->vp_idx;
		rval2 = qla2x00_get_port_database(vha, new_fcport, 0);
		if (rval2 != QLA_SUCCESS) {
			DEBUG2(printk("scsi(%ld): Failed to retrieve fcport "
			    "information -- get_port_database=%x, "
			    "loop_id=0x%04x\n",
			    vha->host_no, rval2, new_fcport->loop_id));
			DEBUG2(printk("scsi(%ld): Scheduling resync...\n",
			    vha->host_no));
			set_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
			continue;
		}

		/* Check for matching device in port list. */
		found = 0;
		fcport = NULL;
		list_for_each_entry(fcport, &vha->vp_fcports, list) {
			if (memcmp(new_fcport->port_name, fcport->port_name,
			    WWN_SIZE))
				continue;

			fcport->flags &= ~FCF_FABRIC_DEVICE;
			fcport->loop_id = new_fcport->loop_id;
			fcport->port_type = new_fcport->port_type;
			fcport->d_id.b24 = new_fcport->d_id.b24;
			memcpy(fcport->node_name, new_fcport->node_name,
			    WWN_SIZE);

			found++;
			break;
		}

		if (!found) {
			/* New device, add to fcports list. */
			if (vha->vp_idx) {
				new_fcport->vha = vha;
				new_fcport->vp_idx = vha->vp_idx;
			}
			list_add_tail(&new_fcport->list, &vha->vp_fcports);

			/* Allocate a new replacement fcport. */
			fcport = new_fcport;
			new_fcport = qla2x00_alloc_fcport(vha, GFP_KERNEL);
			if (new_fcport == NULL) {
				rval = QLA_MEMORY_ALLOC_FAILED;
				goto cleanup_allocation;
			}
			new_fcport->flags &= ~FCF_FABRIC_DEVICE;
		}

		/* Base iIDMA settings on HBA port speed. */
		fcport->fp_speed = ha->link_data_rate;

		qla2x00_update_fcport(vha, fcport);

		found_devs++;
	}

cleanup_allocation:
	kfree(new_fcport);

	if (rval != QLA_SUCCESS) {
		DEBUG2(printk("scsi(%ld): Configure local loop error exit: "
		    "rval=%x\n", vha->host_no, rval));
	}

	return (rval);
}

static void
qla2x00_iidma_fcport(scsi_qla_host_t *vha, fc_port_t *fcport)
{
#define LS_UNKNOWN      2
	static char *link_speeds[] = { "1", "2", "?", "4", "8", "10" };
	char *link_speed;
	int rval;
	uint16_t mb[4];
	struct qla_hw_data *ha = vha->hw;

	if (!IS_IIDMA_CAPABLE(ha))
		return;

	if (atomic_read(&fcport->state) != FCS_ONLINE)
		return;

	if (fcport->fp_speed == PORT_SPEED_UNKNOWN ||
	    fcport->fp_speed > ha->link_data_rate)
		return;

	rval = qla2x00_set_idma_speed(vha, fcport->loop_id, fcport->fp_speed,
	    mb);
	if (rval != QLA_SUCCESS) {
		DEBUG2(printk("scsi(%ld): Unable to adjust iIDMA "
		    "%02x%02x%02x%02x%02x%02x%02x%02x -- %04x %x %04x %04x.\n",
		    vha->host_no, fcport->port_name[0], fcport->port_name[1],
		    fcport->port_name[2], fcport->port_name[3],
		    fcport->port_name[4], fcport->port_name[5],
		    fcport->port_name[6], fcport->port_name[7], rval,
		    fcport->fp_speed, mb[0], mb[1]));
	} else {
		link_speed = link_speeds[LS_UNKNOWN];
		if (fcport->fp_speed < 5)
			link_speed = link_speeds[fcport->fp_speed];
		else if (fcport->fp_speed == 0x13)
			link_speed = link_speeds[5];
		DEBUG2(qla_printk(KERN_INFO, ha,
		    "iIDMA adjusted to %s GB/s on "
		    "%02x%02x%02x%02x%02x%02x%02x%02x.\n",
		    link_speed, fcport->port_name[0],
		    fcport->port_name[1], fcport->port_name[2],
		    fcport->port_name[3], fcport->port_name[4],
		    fcport->port_name[5], fcport->port_name[6],
		    fcport->port_name[7]));
	}
}

static void
qla2x00_reg_remote_port(scsi_qla_host_t *vha, fc_port_t *fcport)
{
	struct fc_rport_identifiers rport_ids;
	struct fc_rport *rport;
	struct qla_hw_data *ha = vha->hw;

	qla2x00_rport_del(fcport);

	rport_ids.node_name = wwn_to_u64(fcport->node_name);
	rport_ids.port_name = wwn_to_u64(fcport->port_name);
	rport_ids.port_id = fcport->d_id.b.domain << 16 |
	    fcport->d_id.b.area << 8 | fcport->d_id.b.al_pa;
	rport_ids.roles = FC_RPORT_ROLE_UNKNOWN;
	fcport->rport = rport = fc_remote_port_add(vha->host, 0, &rport_ids);
	if (!rport) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to allocate fc remote port!\n");
		return;
	}
	spin_lock_irq(fcport->vha->host->host_lock);
	*((fc_port_t **)rport->dd_data) = fcport;
	spin_unlock_irq(fcport->vha->host->host_lock);

	rport->supported_classes = fcport->supported_classes;

	rport_ids.roles = FC_RPORT_ROLE_UNKNOWN;
	if (fcport->port_type == FCT_INITIATOR)
		rport_ids.roles |= FC_RPORT_ROLE_FCP_INITIATOR;
	if (fcport->port_type == FCT_TARGET)
		rport_ids.roles |= FC_RPORT_ROLE_FCP_TARGET;
	fc_remote_port_rolechg(rport, rport_ids.roles);
}

/*
 * qla2x00_update_fcport
 *	Updates device on list.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = port structure pointer.
 *
 * Return:
 *	0  - Success
 *  BIT_0 - error
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_update_fcport(scsi_qla_host_t *vha, fc_port_t *fcport)
{
	fcport->vha = vha;
	fcport->login_retry = 0;
	fcport->flags &= ~(FCF_LOGIN_NEEDED | FCF_ASYNC_SENT);

	qla2x00_iidma_fcport(vha, fcport);
	qla24xx_update_fcport_fcp_prio(vha, fcport);
	qla2x00_reg_remote_port(vha, fcport);
	atomic_set(&fcport->state, FCS_ONLINE);
}

/*
 * qla2x00_configure_fabric
 *      Setup SNS devices with loop ID's.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success.
 *      BIT_0 = error
 */
static int
qla2x00_configure_fabric(scsi_qla_host_t *vha)
{
	int	rval, rval2;
	fc_port_t	*fcport, *fcptemp;
	uint16_t	next_loopid;
	uint16_t	mb[MAILBOX_REGISTER_COUNT];
	uint16_t	loop_id;
	LIST_HEAD(new_fcports);
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	/* If FL port exists, then SNS is present */
	if (IS_FWI2_CAPABLE(ha))
		loop_id = NPH_F_PORT;
	else
		loop_id = SNS_FL_PORT;
	rval = qla2x00_get_port_name(vha, loop_id, vha->fabric_node_name, 1);
	if (rval != QLA_SUCCESS) {
		DEBUG2(printk("scsi(%ld): MBC_GET_PORT_NAME Failed, No FL "
		    "Port\n", vha->host_no));

		vha->device_flags &= ~SWITCH_FOUND;
		return (QLA_SUCCESS);
	}
	vha->device_flags |= SWITCH_FOUND;

	/* Mark devices that need re-synchronization. */
	rval2 = qla2x00_device_resync(vha);
	if (rval2 == QLA_RSCNS_HANDLED) {
		/* No point doing the scan, just continue. */
		return (QLA_SUCCESS);
	}
	do {
		/* FDMI support. */
		if (ql2xfdmienable &&
		    test_and_clear_bit(REGISTER_FDMI_NEEDED, &vha->dpc_flags))
			qla2x00_fdmi_register(vha);

		/* Ensure we are logged into the SNS. */
		if (IS_FWI2_CAPABLE(ha))
			loop_id = NPH_SNS;
		else
			loop_id = SIMPLE_NAME_SERVER;
		ha->isp_ops->fabric_login(vha, loop_id, 0xff, 0xff,
		    0xfc, mb, BIT_1 | BIT_0);
		if (mb[0] != MBS_COMMAND_COMPLETE) {
			DEBUG2(qla_printk(KERN_INFO, ha,
			    "Failed SNS login: loop_id=%x mb[0]=%x mb[1]=%x "
			    "mb[2]=%x mb[6]=%x mb[7]=%x\n", loop_id,
			    mb[0], mb[1], mb[2], mb[6], mb[7]));
			return (QLA_SUCCESS);
		}

		if (test_and_clear_bit(REGISTER_FC4_NEEDED, &vha->dpc_flags)) {
			if (qla2x00_rft_id(vha)) {
				/* EMPTY */
				DEBUG2(printk("scsi(%ld): Register FC-4 "
				    "TYPE failed.\n", vha->host_no));
			}
			if (qla2x00_rff_id(vha)) {
				/* EMPTY */
				DEBUG2(printk("scsi(%ld): Register FC-4 "
				    "Features failed.\n", vha->host_no));
			}
			if (qla2x00_rnn_id(vha)) {
				/* EMPTY */
				DEBUG2(printk("scsi(%ld): Register Node Name "
				    "failed.\n", vha->host_no));
			} else if (qla2x00_rsnn_nn(vha)) {
				/* EMPTY */
				DEBUG2(printk("scsi(%ld): Register Symbolic "
				    "Node Name failed.\n", vha->host_no));
			}
		}

		rval = qla2x00_find_all_fabric_devs(vha, &new_fcports);
		if (rval != QLA_SUCCESS)
			break;

		/*
		 * Logout all previous fabric devices marked lost, except
		 * FCP2 devices.
		 */
		list_for_each_entry(fcport, &vha->vp_fcports, list) {
			if (test_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags))
				break;

			if ((fcport->flags & FCF_FABRIC_DEVICE) == 0)
				continue;

			if (atomic_read(&fcport->state) == FCS_DEVICE_LOST) {
				qla2x00_mark_device_lost(vha, fcport,
				    ql2xplogiabsentdevice, 0);
				if (fcport->loop_id != FC_NO_LOOP_ID &&
				    (fcport->flags & FCF_FCP2_DEVICE) == 0 &&
				    fcport->port_type != FCT_INITIATOR &&
				    fcport->port_type != FCT_BROADCAST) {
					ha->isp_ops->fabric_logout(vha,
					    fcport->loop_id,
					    fcport->d_id.b.domain,
					    fcport->d_id.b.area,
					    fcport->d_id.b.al_pa);
					fcport->loop_id = FC_NO_LOOP_ID;
				}
			}
		}

		/* Starting free loop ID. */
		next_loopid = ha->min_external_loopid;

		/*
		 * Scan through our port list and login entries that need to be
		 * logged in.
		 */
		list_for_each_entry(fcport, &vha->vp_fcports, list) {
			if (atomic_read(&vha->loop_down_timer) ||
			    test_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags))
				break;

			if ((fcport->flags & FCF_FABRIC_DEVICE) == 0 ||
			    (fcport->flags & FCF_LOGIN_NEEDED) == 0)
				continue;

			if (fcport->loop_id == FC_NO_LOOP_ID) {
				fcport->loop_id = next_loopid;
				rval = qla2x00_find_new_loop_id(
				    base_vha, fcport);
				if (rval != QLA_SUCCESS) {
					/* Ran out of IDs to use */
					break;
				}
			}
			/* Login and update database */
			qla2x00_fabric_dev_login(vha, fcport, &next_loopid);
		}

		/* Exit if out of loop IDs. */
		if (rval != QLA_SUCCESS) {
			break;
		}

		/*
		 * Login and add the new devices to our port list.
		 */
		list_for_each_entry_safe(fcport, fcptemp, &new_fcports, list) {
			if (atomic_read(&vha->loop_down_timer) ||
			    test_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags))
				break;

			/* Find a new loop ID to use. */
			fcport->loop_id = next_loopid;
			rval = qla2x00_find_new_loop_id(base_vha, fcport);
			if (rval != QLA_SUCCESS) {
				/* Ran out of IDs to use */
				break;
			}

			/* Login and update database */
			qla2x00_fabric_dev_login(vha, fcport, &next_loopid);

			if (vha->vp_idx) {
				fcport->vha = vha;
				fcport->vp_idx = vha->vp_idx;
			}
			list_move_tail(&fcport->list, &vha->vp_fcports);
		}
	} while (0);

	/* Free all new device structures not processed. */
	list_for_each_entry_safe(fcport, fcptemp, &new_fcports, list) {
		list_del(&fcport->list);
		kfree(fcport);
	}

	if (rval) {
		DEBUG2(printk("scsi(%ld): Configure fabric error exit: "
		    "rval=%d\n", vha->host_no, rval));
	}

	return (rval);
}

/*
 * qla2x00_find_all_fabric_devs
 *
 * Input:
 *	ha = adapter block pointer.
 *	dev = database device entry pointer.
 *
 * Returns:
 *	0 = success.
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_find_all_fabric_devs(scsi_qla_host_t *vha,
	struct list_head *new_fcports)
{
	int		rval;
	uint16_t	loop_id;
	fc_port_t	*fcport, *new_fcport, *fcptemp;
	int		found;

	sw_info_t	*swl;
	int		swl_idx;
	int		first_dev, last_dev;
	port_id_t	wrap = {}, nxt_d_id;
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *vp, *base_vha = pci_get_drvdata(ha->pdev);
	struct scsi_qla_host *tvp;

	rval = QLA_SUCCESS;

	/* Try GID_PT to get device list, else GAN. */
	swl = kcalloc(MAX_FIBRE_DEVICES, sizeof(sw_info_t), GFP_KERNEL);
	if (!swl) {
		/*EMPTY*/
		DEBUG2(printk("scsi(%ld): GID_PT allocations failed, fallback "
		    "on GA_NXT\n", vha->host_no));
	} else {
		if (qla2x00_gid_pt(vha, swl) != QLA_SUCCESS) {
			kfree(swl);
			swl = NULL;
		} else if (qla2x00_gpn_id(vha, swl) != QLA_SUCCESS) {
			kfree(swl);
			swl = NULL;
		} else if (qla2x00_gnn_id(vha, swl) != QLA_SUCCESS) {
			kfree(swl);
			swl = NULL;
		} else if (ql2xiidmaenable &&
		    qla2x00_gfpn_id(vha, swl) == QLA_SUCCESS) {
			qla2x00_gpsc(vha, swl);
		}

		/* If other queries succeeded probe for FC-4 type */
		if (swl)
			qla2x00_gff_id(vha, swl);
	}
	swl_idx = 0;

	/* Allocate temporary fcport for any new fcports discovered. */
	new_fcport = qla2x00_alloc_fcport(vha, GFP_KERNEL);
	if (new_fcport == NULL) {
		kfree(swl);
		return (QLA_MEMORY_ALLOC_FAILED);
	}
	new_fcport->flags |= (FCF_FABRIC_DEVICE | FCF_LOGIN_NEEDED);
	/* Set start port ID scan at adapter ID. */
	first_dev = 1;
	last_dev = 0;

	/* Starting free loop ID. */
	loop_id = ha->min_external_loopid;
	for (; loop_id <= ha->max_loop_id; loop_id++) {
		if (qla2x00_is_reserved_id(vha, loop_id))
			continue;

		if (ha->current_topology == ISP_CFG_FL &&
		    (atomic_read(&vha->loop_down_timer) ||
		     LOOP_TRANSITION(vha))) {
			atomic_set(&vha->loop_down_timer, 0);
			set_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
			set_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags);
			break;
		}

		if (swl != NULL) {
			if (last_dev) {
				wrap.b24 = new_fcport->d_id.b24;
			} else {
				new_fcport->d_id.b24 = swl[swl_idx].d_id.b24;
				memcpy(new_fcport->node_name,
				    swl[swl_idx].node_name, WWN_SIZE);
				memcpy(new_fcport->port_name,
				    swl[swl_idx].port_name, WWN_SIZE);
				memcpy(new_fcport->fabric_port_name,
				    swl[swl_idx].fabric_port_name, WWN_SIZE);
				new_fcport->fp_speed = swl[swl_idx].fp_speed;
				new_fcport->fc4_type = swl[swl_idx].fc4_type;

				if (swl[swl_idx].d_id.b.rsvd_1 != 0) {
					last_dev = 1;
				}
				swl_idx++;
			}
		} else {
			/* Send GA_NXT to the switch */
			rval = qla2x00_ga_nxt(vha, new_fcport);
			if (rval != QLA_SUCCESS) {
				qla_printk(KERN_WARNING, ha,
				    "SNS scan failed -- assuming zero-entry "
				    "result...\n");
				list_for_each_entry_safe(fcport, fcptemp,
				    new_fcports, list) {
					list_del(&fcport->list);
					kfree(fcport);
				}
				rval = QLA_SUCCESS;
				break;
			}
		}

		/* If wrap on switch device list, exit. */
		if (first_dev) {
			wrap.b24 = new_fcport->d_id.b24;
			first_dev = 0;
		} else if (new_fcport->d_id.b24 == wrap.b24) {
			DEBUG2(printk("scsi(%ld): device wrap (%02x%02x%02x)\n",
			    vha->host_no, new_fcport->d_id.b.domain,
			    new_fcport->d_id.b.area, new_fcport->d_id.b.al_pa));
			break;
		}

		/* Bypass if same physical adapter. */
		if (new_fcport->d_id.b24 == base_vha->d_id.b24)
			continue;

		/* Bypass virtual ports of the same host. */
		found = 0;
		if (ha->num_vhosts) {
			unsigned long flags;

			spin_lock_irqsave(&ha->vport_slock, flags);
			list_for_each_entry_safe(vp, tvp, &ha->vp_list, list) {
				if (new_fcport->d_id.b24 == vp->d_id.b24) {
					found = 1;
					break;
				}
			}
			spin_unlock_irqrestore(&ha->vport_slock, flags);

			if (found)
				continue;
		}

		/* Bypass if same domain and area of adapter. */
		if (((new_fcport->d_id.b24 & 0xffff00) ==
		    (vha->d_id.b24 & 0xffff00)) && ha->current_topology ==
			ISP_CFG_FL)
			    continue;

		/* Bypass reserved domain fields. */
		if ((new_fcport->d_id.b.domain & 0xf0) == 0xf0)
			continue;

		/* Bypass ports whose FCP-4 type is not FCP_SCSI */
		if (ql2xgffidenable &&
		    (new_fcport->fc4_type != FC4_TYPE_FCP_SCSI &&
		    new_fcport->fc4_type != FC4_TYPE_UNKNOWN))
			continue;

		/* Locate matching device in database. */
		found = 0;
		list_for_each_entry(fcport, &vha->vp_fcports, list) {
			if (memcmp(new_fcport->port_name, fcport->port_name,
			    WWN_SIZE))
				continue;

			found++;

			/* Update port state. */
			memcpy(fcport->fabric_port_name,
			    new_fcport->fabric_port_name, WWN_SIZE);
			fcport->fp_speed = new_fcport->fp_speed;

			/*
			 * If address the same and state FCS_ONLINE, nothing
			 * changed.
			 */
			if (fcport->d_id.b24 == new_fcport->d_id.b24 &&
			    atomic_read(&fcport->state) == FCS_ONLINE) {
				break;
			}

			/*
			 * If device was not a fabric device before.
			 */
			if ((fcport->flags & FCF_FABRIC_DEVICE) == 0) {
				fcport->d_id.b24 = new_fcport->d_id.b24;
				fcport->loop_id = FC_NO_LOOP_ID;
				fcport->flags |= (FCF_FABRIC_DEVICE |
				    FCF_LOGIN_NEEDED);
				break;
			}

			/*
			 * Port ID changed or device was marked to be updated;
			 * Log it out if still logged in and mark it for
			 * relogin later.
			 */
			fcport->d_id.b24 = new_fcport->d_id.b24;
			fcport->flags |= FCF_LOGIN_NEEDED;
			if (fcport->loop_id != FC_NO_LOOP_ID &&
			    (fcport->flags & FCF_FCP2_DEVICE) == 0 &&
			    fcport->port_type != FCT_INITIATOR &&
			    fcport->port_type != FCT_BROADCAST) {
				ha->isp_ops->fabric_logout(vha, fcport->loop_id,
				    fcport->d_id.b.domain, fcport->d_id.b.area,
				    fcport->d_id.b.al_pa);
				fcport->loop_id = FC_NO_LOOP_ID;
			}

			break;
		}

		if (found)
			continue;
		/* If device was not in our fcports list, then add it. */
		list_add_tail(&new_fcport->list, new_fcports);

		/* Allocate a new replacement fcport. */
		nxt_d_id.b24 = new_fcport->d_id.b24;
		new_fcport = qla2x00_alloc_fcport(vha, GFP_KERNEL);
		if (new_fcport == NULL) {
			kfree(swl);
			return (QLA_MEMORY_ALLOC_FAILED);
		}
		new_fcport->flags |= (FCF_FABRIC_DEVICE | FCF_LOGIN_NEEDED);
		new_fcport->d_id.b24 = nxt_d_id.b24;
	}

	kfree(swl);
	kfree(new_fcport);

	return (rval);
}

/*
 * qla2x00_find_new_loop_id
 *	Scan through our port list and find a new usable loop ID.
 *
 * Input:
 *	ha:	adapter state pointer.
 *	dev:	port structure pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_find_new_loop_id(scsi_qla_host_t *vha, fc_port_t *dev)
{
	int	rval;
	int	found;
	fc_port_t *fcport;
	uint16_t first_loop_id;
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *vp;
	struct scsi_qla_host *tvp;
	unsigned long flags = 0;

	rval = QLA_SUCCESS;

	/* Save starting loop ID. */
	first_loop_id = dev->loop_id;

	for (;;) {
		/* Skip loop ID if already used by adapter. */
		if (dev->loop_id == vha->loop_id)
			dev->loop_id++;

		/* Skip reserved loop IDs. */
		while (qla2x00_is_reserved_id(vha, dev->loop_id))
			dev->loop_id++;

		/* Reset loop ID if passed the end. */
		if (dev->loop_id > ha->max_loop_id) {
			/* first loop ID. */
			dev->loop_id = ha->min_external_loopid;
		}

		/* Check for loop ID being already in use. */
		found = 0;
		fcport = NULL;

		spin_lock_irqsave(&ha->vport_slock, flags);
		list_for_each_entry_safe(vp, tvp, &ha->vp_list, list) {
			list_for_each_entry(fcport, &vp->vp_fcports, list) {
				if (fcport->loop_id == dev->loop_id &&
								fcport != dev) {
					/* ID possibly in use */
					found++;
					break;
				}
			}
			if (found)
				break;
		}
		spin_unlock_irqrestore(&ha->vport_slock, flags);

		/* If not in use then it is free to use. */
		if (!found) {
			break;
		}

		/* ID in use. Try next value. */
		dev->loop_id++;

		/* If wrap around. No free ID to use. */
		if (dev->loop_id == first_loop_id) {
			dev->loop_id = FC_NO_LOOP_ID;
			rval = QLA_FUNCTION_FAILED;
			break;
		}
	}

	return (rval);
}

/*
 * qla2x00_device_resync
 *	Marks devices in the database that needs resynchronization.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_device_resync(scsi_qla_host_t *vha)
{
	int	rval;
	uint32_t mask;
	fc_port_t *fcport;
	uint32_t rscn_entry;
	uint8_t rscn_out_iter;
	uint8_t format;
	port_id_t d_id = {};

	rval = QLA_RSCNS_HANDLED;

	while (vha->rscn_out_ptr != vha->rscn_in_ptr ||
	    vha->flags.rscn_queue_overflow) {

		rscn_entry = vha->rscn_queue[vha->rscn_out_ptr];
		format = MSB(MSW(rscn_entry));
		d_id.b.domain = LSB(MSW(rscn_entry));
		d_id.b.area = MSB(LSW(rscn_entry));
		d_id.b.al_pa = LSB(LSW(rscn_entry));

		DEBUG(printk("scsi(%ld): RSCN queue entry[%d] = "
		    "[%02x/%02x%02x%02x].\n",
		    vha->host_no, vha->rscn_out_ptr, format, d_id.b.domain,
		    d_id.b.area, d_id.b.al_pa));

		vha->rscn_out_ptr++;
		if (vha->rscn_out_ptr == MAX_RSCN_COUNT)
			vha->rscn_out_ptr = 0;

		/* Skip duplicate entries. */
		for (rscn_out_iter = vha->rscn_out_ptr;
		    !vha->flags.rscn_queue_overflow &&
		    rscn_out_iter != vha->rscn_in_ptr;
		    rscn_out_iter = (rscn_out_iter ==
			(MAX_RSCN_COUNT - 1)) ? 0: rscn_out_iter + 1) {

			if (rscn_entry != vha->rscn_queue[rscn_out_iter])
				break;

			DEBUG(printk("scsi(%ld): Skipping duplicate RSCN queue "
			    "entry found at [%d].\n", vha->host_no,
			    rscn_out_iter));

			vha->rscn_out_ptr = rscn_out_iter;
		}

		/* Queue overflow, set switch default case. */
		if (vha->flags.rscn_queue_overflow) {
			DEBUG(printk("scsi(%ld): device_resync: rscn "
			    "overflow.\n", vha->host_no));

			format = 3;
			vha->flags.rscn_queue_overflow = 0;
		}

		switch (format) {
		case 0:
			mask = 0xffffff;
			break;
		case 1:
			mask = 0xffff00;
			break;
		case 2:
			mask = 0xff0000;
			break;
		default:
			mask = 0x0;
			d_id.b24 = 0;
			vha->rscn_out_ptr = vha->rscn_in_ptr;
			break;
		}

		rval = QLA_SUCCESS;

		list_for_each_entry(fcport, &vha->vp_fcports, list) {
			if ((fcport->flags & FCF_FABRIC_DEVICE) == 0 ||
			    (fcport->d_id.b24 & mask) != d_id.b24 ||
			    fcport->port_type == FCT_BROADCAST)
				continue;

			if (atomic_read(&fcport->state) == FCS_ONLINE) {
				if (format != 3 ||
				    fcport->port_type != FCT_INITIATOR) {
					qla2x00_mark_device_lost(vha, fcport,
					    0, 0);
				}
			}
		}
	}
	return (rval);
}

/*
 * qla2x00_fabric_dev_login
 *	Login fabric target device and update FC port database.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	fcport:		port structure list pointer.
 *	next_loopid:	contains value of a new loop ID that can be used
 *			by the next login attempt.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_fabric_dev_login(scsi_qla_host_t *vha, fc_port_t *fcport,
    uint16_t *next_loopid)
{
	int	rval;
	int	retry;
	uint8_t opts;
	struct qla_hw_data *ha = vha->hw;

	rval = QLA_SUCCESS;
	retry = 0;

	if (IS_ALOGIO_CAPABLE(ha)) {
		if (fcport->flags & FCF_ASYNC_SENT)
			return rval;
		fcport->flags |= FCF_ASYNC_SENT;
		rval = qla2x00_post_async_login_work(vha, fcport, NULL);
		if (!rval)
			return rval;
	}

	fcport->flags &= ~FCF_ASYNC_SENT;
	rval = qla2x00_fabric_login(vha, fcport, next_loopid);
	if (rval == QLA_SUCCESS) {
		/* Send an ADISC to FCP2 devices.*/
		opts = 0;
		if (fcport->flags & FCF_FCP2_DEVICE)
			opts |= BIT_1;
		rval = qla2x00_get_port_database(vha, fcport, opts);
		if (rval != QLA_SUCCESS) {
			ha->isp_ops->fabric_logout(vha, fcport->loop_id,
			    fcport->d_id.b.domain, fcport->d_id.b.area,
			    fcport->d_id.b.al_pa);
			qla2x00_mark_device_lost(vha, fcport, 1, 0);
		} else {
			qla2x00_update_fcport(vha, fcport);
		}
	}

	return (rval);
}

/*
 * qla2x00_fabric_login
 *	Issue fabric login command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	device = pointer to FC device type structure.
 *
 * Returns:
 *      0 - Login successfully
 *      1 - Login failed
 *      2 - Initiator device
 *      3 - Fatal error
 */
int
qla2x00_fabric_login(scsi_qla_host_t *vha, fc_port_t *fcport,
    uint16_t *next_loopid)
{
	int	rval;
	int	retry;
	uint16_t tmp_loopid;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	struct qla_hw_data *ha = vha->hw;

	retry = 0;
	tmp_loopid = 0;

	for (;;) {
		DEBUG(printk("scsi(%ld): Trying Fabric Login w/loop id 0x%04x "
 		    "for port %02x%02x%02x.\n",
		    vha->host_no, fcport->loop_id, fcport->d_id.b.domain,
		    fcport->d_id.b.area, fcport->d_id.b.al_pa));

		/* Login fcport on switch. */
		ha->isp_ops->fabric_login(vha, fcport->loop_id,
		    fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa, mb, BIT_0);
		if (mb[0] == MBS_PORT_ID_USED) {
			/*
			 * Device has another loop ID.  The firmware team
			 * recommends the driver perform an implicit login with
			 * the specified ID again. The ID we just used is save
			 * here so we return with an ID that can be tried by
			 * the next login.
			 */
			retry++;
			tmp_loopid = fcport->loop_id;
			fcport->loop_id = mb[1];

			DEBUG(printk("Fabric Login: port in use - next "
 			    "loop id=0x%04x, port Id=%02x%02x%02x.\n",
			    fcport->loop_id, fcport->d_id.b.domain,
			    fcport->d_id.b.area, fcport->d_id.b.al_pa));

		} else if (mb[0] == MBS_COMMAND_COMPLETE) {
			/*
			 * Login succeeded.
			 */
			if (retry) {
				/* A retry occurred before. */
				*next_loopid = tmp_loopid;
			} else {
				/*
				 * No retry occurred before. Just increment the
				 * ID value for next login.
				 */
				*next_loopid = (fcport->loop_id + 1);
			}

			if (mb[1] & BIT_0) {
				fcport->port_type = FCT_INITIATOR;
			} else {
				fcport->port_type = FCT_TARGET;
				if (mb[1] & BIT_1) {
					fcport->flags |= FCF_FCP2_DEVICE;
				}
			}

			if (mb[10] & BIT_0)
				fcport->supported_classes |= FC_COS_CLASS2;
			if (mb[10] & BIT_1)
				fcport->supported_classes |= FC_COS_CLASS3;

			rval = QLA_SUCCESS;
			break;
		} else if (mb[0] == MBS_LOOP_ID_USED) {
			/*
			 * Loop ID already used, try next loop ID.
			 */
			fcport->loop_id++;
			rval = qla2x00_find_new_loop_id(vha, fcport);
			if (rval != QLA_SUCCESS) {
				/* Ran out of loop IDs to use */
				break;
			}
		} else if (mb[0] == MBS_COMMAND_ERROR) {
			/*
			 * Firmware possibly timed out during login. If NO
			 * retries are left to do then the device is declared
			 * dead.
			 */
			*next_loopid = fcport->loop_id;
			ha->isp_ops->fabric_logout(vha, fcport->loop_id,
			    fcport->d_id.b.domain, fcport->d_id.b.area,
			    fcport->d_id.b.al_pa);
			qla2x00_mark_device_lost(vha, fcport, 1, 0);

			rval = 1;
			break;
		} else {
			/*
			 * unrecoverable / not handled error
			 */
			DEBUG2(printk("%s(%ld): failed=%x port_id=%02x%02x%02x "
 			    "loop_id=%x jiffies=%lx.\n",
			    __func__, vha->host_no, mb[0],
			    fcport->d_id.b.domain, fcport->d_id.b.area,
			    fcport->d_id.b.al_pa, fcport->loop_id, jiffies));

			*next_loopid = fcport->loop_id;
			ha->isp_ops->fabric_logout(vha, fcport->loop_id,
			    fcport->d_id.b.domain, fcport->d_id.b.area,
			    fcport->d_id.b.al_pa);
			fcport->loop_id = FC_NO_LOOP_ID;
			fcport->login_retry = 0;

			rval = 3;
			break;
		}
	}

	return (rval);
}

/*
 * qla2x00_local_device_login
 *	Issue local device login command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = loop id of device to login to.
 *
 * Returns (Where's the #define!!!!):
 *      0 - Login successfully
 *      1 - Login failed
 *      3 - Fatal error
 */
int
qla2x00_local_device_login(scsi_qla_host_t *vha, fc_port_t *fcport)
{
	int		rval;
	uint16_t	mb[MAILBOX_REGISTER_COUNT];

	memset(mb, 0, sizeof(mb));
	rval = qla2x00_login_local_device(vha, fcport, mb, BIT_0);
	if (rval == QLA_SUCCESS) {
		/* Interrogate mailbox registers for any errors */
		if (mb[0] == MBS_COMMAND_ERROR)
			rval = 1;
		else if (mb[0] == MBS_COMMAND_PARAMETER_ERROR)
			/* device not in PCB table */
			rval = 3;
	}

	return (rval);
}

/*
 *  qla2x00_loop_resync
 *      Resync with fibre channel devices.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success
 */
int
qla2x00_loop_resync(scsi_qla_host_t *vha)
{
	int rval = QLA_SUCCESS;
	uint32_t wait_time;
	struct req_que *req;
	struct rsp_que *rsp;

	if (vha->hw->flags.cpu_affinity_enabled)
		req = vha->hw->req_q_map[0];
	else
		req = vha->req;
	rsp = req->rsp;

	atomic_set(&vha->loop_state, LOOP_UPDATE);
	clear_bit(ISP_ABORT_RETRY, &vha->dpc_flags);
	if (vha->flags.online) {
		if (!(rval = qla2x00_fw_ready(vha))) {
			/* Wait at most MAX_TARGET RSCNs for a stable link. */
			wait_time = 256;
			do {
				atomic_set(&vha->loop_state, LOOP_UPDATE);

				/* Issue a marker after FW becomes ready. */
				qla2x00_marker(vha, req, rsp, 0, 0,
					MK_SYNC_ALL);
				vha->marker_needed = 0;

				/* Remap devices on Loop. */
				clear_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);

				qla2x00_configure_loop(vha);
				wait_time--;
			} while (!atomic_read(&vha->loop_down_timer) &&
				!(test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags))
				&& wait_time && (test_bit(LOOP_RESYNC_NEEDED,
				&vha->dpc_flags)));
		}
	}

	if (test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags))
		return (QLA_FUNCTION_FAILED);

	if (rval)
		DEBUG2_3(printk("%s(): **** FAILED ****\n", __func__));

	return (rval);
}

/*
* qla2x00_perform_loop_resync
* Description: This function will set the appropriate flags and call
*              qla2x00_loop_resync. If successful loop will be resynced
* Arguments : scsi_qla_host_t pointer
* returm    : Success or Failure
*/

int qla2x00_perform_loop_resync(scsi_qla_host_t *ha)
{
	int32_t rval = 0;

	if (!test_and_set_bit(LOOP_RESYNC_ACTIVE, &ha->dpc_flags)) {
		/*Configure the flags so that resync happens properly*/
		atomic_set(&ha->loop_down_timer, 0);
		if (!(ha->device_flags & DFLG_NO_CABLE)) {
			atomic_set(&ha->loop_state, LOOP_UP);
			set_bit(LOCAL_LOOP_UPDATE, &ha->dpc_flags);
			set_bit(REGISTER_FC4_NEEDED, &ha->dpc_flags);
			set_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);

			rval = qla2x00_loop_resync(ha);
		} else
			atomic_set(&ha->loop_state, LOOP_DEAD);

		clear_bit(LOOP_RESYNC_ACTIVE, &ha->dpc_flags);
	}

	return rval;
}

void
qla2x00_update_fcports(scsi_qla_host_t *base_vha)
{
	fc_port_t *fcport;
	struct scsi_qla_host *vha;
	struct qla_hw_data *ha = base_vha->hw;
	unsigned long flags;

	spin_lock_irqsave(&ha->vport_slock, flags);
	/* Go with deferred removal of rport references. */
	list_for_each_entry(vha, &base_vha->hw->vp_list, list) {
		atomic_inc(&vha->vref_count);
		list_for_each_entry(fcport, &vha->vp_fcports, list) {
			if (fcport->drport &&
			    atomic_read(&fcport->state) != FCS_UNCONFIGURED) {
				spin_unlock_irqrestore(&ha->vport_slock, flags);

				qla2x00_rport_del(fcport);

				spin_lock_irqsave(&ha->vport_slock, flags);
			}
		}
		atomic_dec(&vha->vref_count);
	}
	spin_unlock_irqrestore(&ha->vport_slock, flags);
}

/*
* qla82xx_quiescent_state_cleanup
* Description: This function will block the new I/Os
*              Its not aborting any I/Os as context
*              is not destroyed during quiescence
* Arguments: scsi_qla_host_t
* return   : void
*/
void
qla82xx_quiescent_state_cleanup(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *vp;

	qla_printk(KERN_INFO, ha,
			"Performing ISP error recovery - ha= %p.\n", ha);

	atomic_set(&ha->loop_down_timer, LOOP_DOWN_TIME);
	if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
		atomic_set(&vha->loop_state, LOOP_DOWN);
		qla2x00_mark_all_devices_lost(vha, 0);
		list_for_each_entry(vp, &ha->vp_list, list)
			qla2x00_mark_all_devices_lost(vha, 0);
	} else {
		if (!atomic_read(&vha->loop_down_timer))
			atomic_set(&vha->loop_down_timer,
					LOOP_DOWN_TIME);
	}
	/* Wait for pending cmds to complete */
	qla2x00_eh_wait_for_pending_commands(vha, 0, 0, WAIT_HOST);
}

void
qla2x00_abort_isp_cleanup(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *vp;
	unsigned long flags;

	vha->flags.online = 0;
	ha->flags.chip_reset_done = 0;
	clear_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
	ha->qla_stats.total_isp_aborts++;

	qla_printk(KERN_INFO, ha,
	    "Performing ISP error recovery - ha= %p.\n", ha);

	/* Chip reset does not apply to 82XX */
	if (!IS_QLA82XX(ha))
		ha->isp_ops->reset_chip(vha);

	atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);
	if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
		atomic_set(&vha->loop_state, LOOP_DOWN);
		qla2x00_mark_all_devices_lost(vha, 0);

		spin_lock_irqsave(&ha->vport_slock, flags);
		list_for_each_entry(vp, &ha->vp_list, list) {
			atomic_inc(&vp->vref_count);
			spin_unlock_irqrestore(&ha->vport_slock, flags);

			qla2x00_mark_all_devices_lost(vp, 0);

			spin_lock_irqsave(&ha->vport_slock, flags);
			atomic_dec(&vp->vref_count);
		}
		spin_unlock_irqrestore(&ha->vport_slock, flags);
	} else {
		if (!atomic_read(&vha->loop_down_timer))
			atomic_set(&vha->loop_down_timer,
			    LOOP_DOWN_TIME);
	}

	if (!ha->flags.eeh_busy) {
		/* Make sure for ISP 82XX IO DMA is complete */
		if (IS_QLA82XX(ha)) {
			if (qla2x00_eh_wait_for_pending_commands(vha, 0, 0,
				WAIT_HOST) == QLA_SUCCESS) {
				DEBUG2(qla_printk(KERN_INFO, ha,
				"Done wait for pending commands\n"));
			}
		}

		/* Requeue all commands in outstanding command list. */
		qla2x00_abort_all_cmds(vha, DID_RESET << 16);
	}
}

/*
*  qla2x00_abort_isp
*      Resets ISP and aborts all outstanding commands.
*
* Input:
*      ha           = adapter block pointer.
*
* Returns:
*      0 = success
*/
int
qla2x00_abort_isp(scsi_qla_host_t *vha)
{
	int rval;
	uint8_t        status = 0;
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *vp;
	struct req_que *req = ha->req_q_map[0];
	unsigned long flags;

	if (vha->flags.online) {
		qla2x00_abort_isp_cleanup(vha);

		if (unlikely(pci_channel_offline(ha->pdev) &&
		    ha->flags.pci_channel_io_perm_failure)) {
			clear_bit(ISP_ABORT_RETRY, &vha->dpc_flags);
			status = 0;
			return status;
		}

		ha->isp_ops->get_flash_version(vha, req->ring);

		ha->isp_ops->nvram_config(vha);

		if (!qla2x00_restart_isp(vha)) {
			clear_bit(RESET_MARKER_NEEDED, &vha->dpc_flags);

			if (!atomic_read(&vha->loop_down_timer)) {
				/*
				 * Issue marker command only when we are going
				 * to start the I/O .
				 */
				vha->marker_needed = 1;
			}

			vha->flags.online = 1;

			ha->isp_ops->enable_intrs(ha);

			ha->isp_abort_cnt = 0;
			clear_bit(ISP_ABORT_RETRY, &vha->dpc_flags);

			if (IS_QLA81XX(ha))
				qla2x00_get_fw_version(vha,
				    &ha->fw_major_version,
				    &ha->fw_minor_version,
				    &ha->fw_subminor_version,
				    &ha->fw_attributes, &ha->fw_memory_size,
				    ha->mpi_version, &ha->mpi_capabilities,
				    ha->phy_version);

			if (ha->fce) {
				ha->flags.fce_enabled = 1;
				memset(ha->fce, 0,
				    fce_calc_size(ha->fce_bufs));
				rval = qla2x00_enable_fce_trace(vha,
				    ha->fce_dma, ha->fce_bufs, ha->fce_mb,
				    &ha->fce_bufs);
				if (rval) {
					qla_printk(KERN_WARNING, ha,
					    "Unable to reinitialize FCE "
					    "(%d).\n", rval);
					ha->flags.fce_enabled = 0;
				}
			}

			if (ha->eft) {
				memset(ha->eft, 0, EFT_SIZE);
				rval = qla2x00_enable_eft_trace(vha,
				    ha->eft_dma, EFT_NUM_BUFFERS);
				if (rval) {
					qla_printk(KERN_WARNING, ha,
					    "Unable to reinitialize EFT "
					    "(%d).\n", rval);
				}
			}
		} else {	/* failed the ISP abort */
			vha->flags.online = 1;
			if (test_bit(ISP_ABORT_RETRY, &vha->dpc_flags)) {
				if (ha->isp_abort_cnt == 0) {
 					qla_printk(KERN_WARNING, ha,
					    "ISP error recovery failed - "
					    "board disabled\n");
					/*
					 * The next call disables the board
					 * completely.
					 */
					ha->isp_ops->reset_adapter(vha);
					vha->flags.online = 0;
					clear_bit(ISP_ABORT_RETRY,
					    &vha->dpc_flags);
					status = 0;
				} else { /* schedule another ISP abort */
					ha->isp_abort_cnt--;
					DEBUG(printk("qla%ld: ISP abort - "
					    "retry remaining %d\n",
					    vha->host_no, ha->isp_abort_cnt));
					status = 1;
				}
			} else {
				ha->isp_abort_cnt = MAX_RETRIES_OF_ISP_ABORT;
				DEBUG(printk("qla2x00(%ld): ISP error recovery "
				    "- retrying (%d) more times\n",
				    vha->host_no, ha->isp_abort_cnt));
				set_bit(ISP_ABORT_RETRY, &vha->dpc_flags);
				status = 1;
			}
		}

	}

	if (!status) {
		DEBUG(printk(KERN_INFO
				"qla2x00_abort_isp(%ld): succeeded.\n",
				vha->host_no));

		spin_lock_irqsave(&ha->vport_slock, flags);
		list_for_each_entry(vp, &ha->vp_list, list) {
			if (vp->vp_idx) {
				atomic_inc(&vp->vref_count);
				spin_unlock_irqrestore(&ha->vport_slock, flags);

				qla2x00_vp_abort_isp(vp);

				spin_lock_irqsave(&ha->vport_slock, flags);
				atomic_dec(&vp->vref_count);
			}
		}
		spin_unlock_irqrestore(&ha->vport_slock, flags);

	} else {
		qla_printk(KERN_INFO, ha,
			"qla2x00_abort_isp: **** FAILED ****\n");
	}

	return(status);
}

/*
*  qla2x00_restart_isp
*      restarts the ISP after a reset
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success
*/
static int
qla2x00_restart_isp(scsi_qla_host_t *vha)
{
	int status = 0;
	uint32_t wait_time;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];
	struct rsp_que *rsp = ha->rsp_q_map[0];

	/* If firmware needs to be loaded */
	if (qla2x00_isp_firmware(vha)) {
		vha->flags.online = 0;
		status = ha->isp_ops->chip_diag(vha);
		if (!status)
			status = qla2x00_setup_chip(vha);
	}

	if (!status && !(status = qla2x00_init_rings(vha))) {
		clear_bit(RESET_MARKER_NEEDED, &vha->dpc_flags);
		ha->flags.chip_reset_done = 1;
		/* Initialize the queues in use */
		qla25xx_init_queues(ha);

		status = qla2x00_fw_ready(vha);
		if (!status) {
			DEBUG(printk("%s(): Start configure loop, "
			    "status = %d\n", __func__, status));

			/* Issue a marker after FW becomes ready. */
			qla2x00_marker(vha, req, rsp, 0, 0, MK_SYNC_ALL);

			vha->flags.online = 1;
			/* Wait at most MAX_TARGET RSCNs for a stable link. */
			wait_time = 256;
			do {
				clear_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
				qla2x00_configure_loop(vha);
				wait_time--;
			} while (!atomic_read(&vha->loop_down_timer) &&
				!(test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags))
				&& wait_time && (test_bit(LOOP_RESYNC_NEEDED,
				&vha->dpc_flags)));
		}

		/* if no cable then assume it's good */
		if ((vha->device_flags & DFLG_NO_CABLE))
			status = 0;

		DEBUG(printk("%s(): Configure loop done, status = 0x%x\n",
				__func__,
				status));
	}
	return (status);
}

static int
qla25xx_init_queues(struct qla_hw_data *ha)
{
	struct rsp_que *rsp = NULL;
	struct req_que *req = NULL;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);
	int ret = -1;
	int i;

	for (i = 1; i < ha->max_rsp_queues; i++) {
		rsp = ha->rsp_q_map[i];
		if (rsp) {
			rsp->options &= ~BIT_0;
			ret = qla25xx_init_rsp_que(base_vha, rsp);
			if (ret != QLA_SUCCESS)
				DEBUG2_17(printk(KERN_WARNING
					"%s Rsp que:%d init failed\n", __func__,
						rsp->id));
			else
				DEBUG2_17(printk(KERN_INFO
					"%s Rsp que:%d inited\n", __func__,
						rsp->id));
		}
	}
	for (i = 1; i < ha->max_req_queues; i++) {
		req = ha->req_q_map[i];
		if (req) {
		/* Clear outstanding commands array. */
			req->options &= ~BIT_0;
			ret = qla25xx_init_req_que(base_vha, req);
			if (ret != QLA_SUCCESS)
				DEBUG2_17(printk(KERN_WARNING
					"%s Req que:%d init failed\n", __func__,
						req->id));
			else
				DEBUG2_17(printk(KERN_WARNING
					"%s Req que:%d inited\n", __func__,
						req->id));
		}
	}
	return ret;
}

/*
* qla2x00_reset_adapter
*      Reset adapter.
*
* Input:
*      ha = adapter block pointer.
*/
void
qla2x00_reset_adapter(scsi_qla_host_t *vha)
{
	unsigned long flags = 0;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	vha->flags.online = 0;
	ha->isp_ops->disable_intrs(ha);

	spin_lock_irqsave(&ha->hardware_lock, flags);
	WRT_REG_WORD(&reg->hccr, HCCR_RESET_RISC);
	RD_REG_WORD(&reg->hccr);			/* PCI Posting. */
	WRT_REG_WORD(&reg->hccr, HCCR_RELEASE_RISC);
	RD_REG_WORD(&reg->hccr);			/* PCI Posting. */
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void
qla24xx_reset_adapter(scsi_qla_host_t *vha)
{
	unsigned long flags = 0;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	if (IS_QLA82XX(ha))
		return;

	vha->flags.online = 0;
	ha->isp_ops->disable_intrs(ha);

	spin_lock_irqsave(&ha->hardware_lock, flags);
	WRT_REG_DWORD(&reg->hccr, HCCRX_SET_RISC_RESET);
	RD_REG_DWORD(&reg->hccr);
	WRT_REG_DWORD(&reg->hccr, HCCRX_REL_RISC_PAUSE);
	RD_REG_DWORD(&reg->hccr);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (IS_NOPOLLING_TYPE(ha))
		ha->isp_ops->enable_intrs(ha);
}

/* On sparc systems, obtain port and node WWN from firmware
 * properties.
 */
static void qla24xx_nvram_wwn_from_ofw(scsi_qla_host_t *vha,
	struct nvram_24xx *nv)
{
#ifdef CONFIG_SPARC
	struct qla_hw_data *ha = vha->hw;
	struct pci_dev *pdev = ha->pdev;
	struct device_node *dp = pci_device_to_OF_node(pdev);
	const u8 *val;
	int len;

	val = of_get_property(dp, "port-wwn", &len);
	if (val && len >= WWN_SIZE)
		memcpy(nv->port_name, val, WWN_SIZE);

	val = of_get_property(dp, "node-wwn", &len);
	if (val && len >= WWN_SIZE)
		memcpy(nv->node_name, val, WWN_SIZE);
#endif
}

int
qla24xx_nvram_config(scsi_qla_host_t *vha)
{
	int   rval;
	struct init_cb_24xx *icb;
	struct nvram_24xx *nv;
	uint32_t *dptr;
	uint8_t  *dptr1, *dptr2;
	uint32_t chksum;
	uint16_t cnt;
	struct qla_hw_data *ha = vha->hw;

	rval = QLA_SUCCESS;
	icb = (struct init_cb_24xx *)ha->init_cb;
	nv = ha->nvram;

	/* Determine NVRAM starting address. */
	if (ha->flags.port0) {
		ha->nvram_base = FA_NVRAM_FUNC0_ADDR;
		ha->vpd_base = FA_NVRAM_VPD0_ADDR;
	} else {
		ha->nvram_base = FA_NVRAM_FUNC1_ADDR;
		ha->vpd_base = FA_NVRAM_VPD1_ADDR;
	}
	ha->nvram_size = sizeof(struct nvram_24xx);
	ha->vpd_size = FA_NVRAM_VPD_SIZE;
	if (IS_QLA82XX(ha))
		ha->vpd_size = FA_VPD_SIZE_82XX;

	/* Get VPD data into cache */
	ha->vpd = ha->nvram + VPD_OFFSET;
	ha->isp_ops->read_nvram(vha, (uint8_t *)ha->vpd,
	    ha->nvram_base - FA_NVRAM_FUNC0_ADDR, FA_NVRAM_VPD_SIZE * 4);

	/* Get NVRAM data into cache and calculate checksum. */
	dptr = (uint32_t *)nv;
	ha->isp_ops->read_nvram(vha, (uint8_t *)dptr, ha->nvram_base,
	    ha->nvram_size);
	for (cnt = 0, chksum = 0; cnt < ha->nvram_size >> 2; cnt++)
		chksum += le32_to_cpu(*dptr++);

	DEBUG5(printk("scsi(%ld): Contents of NVRAM\n", vha->host_no));
	DEBUG5(qla2x00_dump_buffer((uint8_t *)nv, ha->nvram_size));

	/* Bad NVRAM data, set defaults parameters. */
	if (chksum || nv->id[0] != 'I' || nv->id[1] != 'S' || nv->id[2] != 'P'
	    || nv->id[3] != ' ' ||
	    nv->nvram_version < __constant_cpu_to_le16(ICB_VERSION)) {
		/* Reset NVRAM data. */
		qla_printk(KERN_WARNING, ha, "Inconsistent NVRAM detected: "
		    "checksum=0x%x id=%c version=0x%x.\n", chksum, nv->id[0],
		    le16_to_cpu(nv->nvram_version));
		qla_printk(KERN_WARNING, ha, "Falling back to functioning (yet "
		    "invalid -- WWPN) defaults.\n");

		/*
		 * Set default initialization control block.
		 */
		memset(nv, 0, ha->nvram_size);
		nv->nvram_version = __constant_cpu_to_le16(ICB_VERSION);
		nv->version = __constant_cpu_to_le16(ICB_VERSION);
		nv->frame_payload_size = __constant_cpu_to_le16(2048);
		nv->execution_throttle = __constant_cpu_to_le16(0xFFFF);
		nv->exchange_count = __constant_cpu_to_le16(0);
		nv->hard_address = __constant_cpu_to_le16(124);
		nv->port_name[0] = 0x21;
		nv->port_name[1] = 0x00 + ha->port_no;
		nv->port_name[2] = 0x00;
		nv->port_name[3] = 0xe0;
		nv->port_name[4] = 0x8b;
		nv->port_name[5] = 0x1c;
		nv->port_name[6] = 0x55;
		nv->port_name[7] = 0x86;
		nv->node_name[0] = 0x20;
		nv->node_name[1] = 0x00;
		nv->node_name[2] = 0x00;
		nv->node_name[3] = 0xe0;
		nv->node_name[4] = 0x8b;
		nv->node_name[5] = 0x1c;
		nv->node_name[6] = 0x55;
		nv->node_name[7] = 0x86;
		qla24xx_nvram_wwn_from_ofw(vha, nv);
		nv->login_retry_count = __constant_cpu_to_le16(8);
		nv->interrupt_delay_timer = __constant_cpu_to_le16(0);
		nv->login_timeout = __constant_cpu_to_le16(0);
		nv->firmware_options_1 =
		    __constant_cpu_to_le32(BIT_14|BIT_13|BIT_2|BIT_1);
		nv->firmware_options_2 = __constant_cpu_to_le32(2 << 4);
		nv->firmware_options_2 |= __constant_cpu_to_le32(BIT_12);
		nv->firmware_options_3 = __constant_cpu_to_le32(2 << 13);
		nv->host_p = __constant_cpu_to_le32(BIT_11|BIT_10);
		nv->efi_parameters = __constant_cpu_to_le32(0);
		nv->reset_delay = 5;
		nv->max_luns_per_target = __constant_cpu_to_le16(128);
		nv->port_down_retry_count = __constant_cpu_to_le16(30);
		nv->link_down_timeout = __constant_cpu_to_le16(30);

		rval = 1;
	}

	/* Reset Initialization control block */
	memset(icb, 0, ha->init_cb_size);

	/* Copy 1st segment. */
	dptr1 = (uint8_t *)icb;
	dptr2 = (uint8_t *)&nv->version;
	cnt = (uint8_t *)&icb->response_q_inpointer - (uint8_t *)&icb->version;
	while (cnt--)
		*dptr1++ = *dptr2++;

	icb->login_retry_count = nv->login_retry_count;
	icb->link_down_on_nos = nv->link_down_on_nos;

	/* Copy 2nd segment. */
	dptr1 = (uint8_t *)&icb->interrupt_delay_timer;
	dptr2 = (uint8_t *)&nv->interrupt_delay_timer;
	cnt = (uint8_t *)&icb->reserved_3 -
	    (uint8_t *)&icb->interrupt_delay_timer;
	while (cnt--)
		*dptr1++ = *dptr2++;

	/*
	 * Setup driver NVRAM options.
	 */
	qla2x00_set_model_info(vha, nv->model_name, sizeof(nv->model_name),
	    "QLA2462");

	/* Use alternate WWN? */
	if (nv->host_p & __constant_cpu_to_le32(BIT_15)) {
		memcpy(icb->node_name, nv->alternate_node_name, WWN_SIZE);
		memcpy(icb->port_name, nv->alternate_port_name, WWN_SIZE);
	}

	/* Prepare nodename */
	if ((icb->firmware_options_1 & __constant_cpu_to_le32(BIT_14)) == 0) {
		/*
		 * Firmware will apply the following mask if the nodename was
		 * not provided.
		 */
		memcpy(icb->node_name, icb->port_name, WWN_SIZE);
		icb->node_name[0] &= 0xF0;
	}

	/* Set host adapter parameters. */
	ha->flags.disable_risc_code_load = 0;
	ha->flags.enable_lip_reset = 0;
	ha->flags.enable_lip_full_login =
	    le32_to_cpu(nv->host_p) & BIT_10 ? 1: 0;
	ha->flags.enable_target_reset =
	    le32_to_cpu(nv->host_p) & BIT_11 ? 1: 0;
	ha->flags.enable_led_scheme = 0;
	ha->flags.disable_serdes = le32_to_cpu(nv->host_p) & BIT_5 ? 1: 0;

	ha->operating_mode = (le32_to_cpu(icb->firmware_options_2) &
	    (BIT_6 | BIT_5 | BIT_4)) >> 4;

	memcpy(ha->fw_seriallink_options24, nv->seriallink_options,
	    sizeof(ha->fw_seriallink_options24));

	/* save HBA serial number */
	ha->serial0 = icb->port_name[5];
	ha->serial1 = icb->port_name[6];
	ha->serial2 = icb->port_name[7];
	memcpy(vha->node_name, icb->node_name, WWN_SIZE);
	memcpy(vha->port_name, icb->port_name, WWN_SIZE);

	icb->execution_throttle = __constant_cpu_to_le16(0xFFFF);

	ha->retry_count = le16_to_cpu(nv->login_retry_count);

	/* Set minimum login_timeout to 4 seconds. */
	if (le16_to_cpu(nv->login_timeout) < ql2xlogintimeout)
		nv->login_timeout = cpu_to_le16(ql2xlogintimeout);
	if (le16_to_cpu(nv->login_timeout) < 4)
		nv->login_timeout = __constant_cpu_to_le16(4);
	ha->login_timeout = le16_to_cpu(nv->login_timeout);
	icb->login_timeout = nv->login_timeout;

	/* Set minimum RATOV to 100 tenths of a second. */
	ha->r_a_tov = 100;

	ha->loop_reset_delay = nv->reset_delay;

	/* Link Down Timeout = 0:
	 *
	 * 	When Port Down timer expires we will start returning
	 *	I/O's to OS with "DID_NO_CONNECT".
	 *
	 * Link Down Timeout != 0:
	 *
	 *	 The driver waits for the link to come up after link down
	 *	 before returning I/Os to OS with "DID_NO_CONNECT".
	 */
	if (le16_to_cpu(nv->link_down_timeout) == 0) {
		ha->loop_down_abort_time =
		    (LOOP_DOWN_TIME - LOOP_DOWN_TIMEOUT);
	} else {
		ha->link_down_timeout =	le16_to_cpu(nv->link_down_timeout);
		ha->loop_down_abort_time =
		    (LOOP_DOWN_TIME - ha->link_down_timeout);
	}

	/* Need enough time to try and get the port back. */
	ha->port_down_retry_count = le16_to_cpu(nv->port_down_retry_count);
	if (qlport_down_retry)
		ha->port_down_retry_count = qlport_down_retry;

	/* Set login_retry_count */
	ha->login_retry_count  = le16_to_cpu(nv->login_retry_count);
	if (ha->port_down_retry_count ==
	    le16_to_cpu(nv->port_down_retry_count) &&
	    ha->port_down_retry_count > 3)
		ha->login_retry_count = ha->port_down_retry_count;
	else if (ha->port_down_retry_count > (int)ha->login_retry_count)
		ha->login_retry_count = ha->port_down_retry_count;
	if (ql2xloginretrycount)
		ha->login_retry_count = ql2xloginretrycount;

	/* Enable ZIO. */
	if (!vha->flags.init_done) {
		ha->zio_mode = le32_to_cpu(icb->firmware_options_2) &
		    (BIT_3 | BIT_2 | BIT_1 | BIT_0);
		ha->zio_timer = le16_to_cpu(icb->interrupt_delay_timer) ?
		    le16_to_cpu(icb->interrupt_delay_timer): 2;
	}
	icb->firmware_options_2 &= __constant_cpu_to_le32(
	    ~(BIT_3 | BIT_2 | BIT_1 | BIT_0));
	vha->flags.process_response_queue = 0;
	if (ha->zio_mode != QLA_ZIO_DISABLED) {
		ha->zio_mode = QLA_ZIO_MODE_6;

		DEBUG2(printk("scsi(%ld): ZIO mode %d enabled; timer delay "
		    "(%d us).\n", vha->host_no, ha->zio_mode,
		    ha->zio_timer * 100));
		qla_printk(KERN_INFO, ha,
		    "ZIO mode %d enabled; timer delay (%d us).\n",
		    ha->zio_mode, ha->zio_timer * 100);

		icb->firmware_options_2 |= cpu_to_le32(
		    (uint32_t)ha->zio_mode);
		icb->interrupt_delay_timer = cpu_to_le16(ha->zio_timer);
		vha->flags.process_response_queue = 1;
	}

	if (rval) {
		DEBUG2_3(printk(KERN_WARNING
		    "scsi(%ld): NVRAM configuration failed!\n", vha->host_no));
	}
	return (rval);
}

static int
qla24xx_load_risc_flash(scsi_qla_host_t *vha, uint32_t *srisc_addr,
    uint32_t faddr)
{
	int	rval = QLA_SUCCESS;
	int	segments, fragment;
	uint32_t *dcode, dlen;
	uint32_t risc_addr;
	uint32_t risc_size;
	uint32_t i;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];

	qla_printk(KERN_INFO, ha,
	    "FW: Loading from flash (%x)...\n", faddr);

	rval = QLA_SUCCESS;

	segments = FA_RISC_CODE_SEGMENTS;
	dcode = (uint32_t *)req->ring;
	*srisc_addr = 0;

	/* Validate firmware image by checking version. */
	qla24xx_read_flash_data(vha, dcode, faddr + 4, 4);
	for (i = 0; i < 4; i++)
		dcode[i] = be32_to_cpu(dcode[i]);
	if ((dcode[0] == 0xffffffff && dcode[1] == 0xffffffff &&
	    dcode[2] == 0xffffffff && dcode[3] == 0xffffffff) ||
	    (dcode[0] == 0 && dcode[1] == 0 && dcode[2] == 0 &&
		dcode[3] == 0)) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to verify integrity of flash firmware image!\n");
		qla_printk(KERN_WARNING, ha,
		    "Firmware data: %08x %08x %08x %08x!\n", dcode[0],
		    dcode[1], dcode[2], dcode[3]);

		return QLA_FUNCTION_FAILED;
	}

	while (segments && rval == QLA_SUCCESS) {
		/* Read segment's load information. */
		qla24xx_read_flash_data(vha, dcode, faddr, 4);

		risc_addr = be32_to_cpu(dcode[2]);
		*srisc_addr = *srisc_addr == 0 ? risc_addr : *srisc_addr;
		risc_size = be32_to_cpu(dcode[3]);

		fragment = 0;
		while (risc_size > 0 && rval == QLA_SUCCESS) {
			dlen = (uint32_t)(ha->fw_transfer_size >> 2);
			if (dlen > risc_size)
				dlen = risc_size;

			DEBUG7(printk("scsi(%ld): Loading risc segment@ risc "
			    "addr %x, number of dwords 0x%x, offset 0x%x.\n",
			    vha->host_no, risc_addr, dlen, faddr));

			qla24xx_read_flash_data(vha, dcode, faddr, dlen);
			for (i = 0; i < dlen; i++)
				dcode[i] = swab32(dcode[i]);

			rval = qla2x00_load_ram(vha, req->dma, risc_addr,
			    dlen);
			if (rval) {
				DEBUG(printk("scsi(%ld):[ERROR] Failed to load "
				    "segment %d of firmware\n", vha->host_no,
				    fragment));
				qla_printk(KERN_WARNING, ha,
				    "[ERROR] Failed to load segment %d of "
				    "firmware\n", fragment);
				break;
			}

			faddr += dlen;
			risc_addr += dlen;
			risc_size -= dlen;
			fragment++;
		}

		/* Next segment. */
		segments--;
	}

	return rval;
}

#define QLA_FW_URL "ftp://ftp.qlogic.com/outgoing/linux/firmware/"

int
qla2x00_load_risc(scsi_qla_host_t *vha, uint32_t *srisc_addr)
{
	int	rval;
	int	i, fragment;
	uint16_t *wcode, *fwcode;
	uint32_t risc_addr, risc_size, fwclen, wlen, *seg;
	struct fw_blob *blob;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];

	/* Load firmware blob. */
	blob = qla2x00_request_firmware(vha);
	if (!blob) {
		qla_printk(KERN_ERR, ha, "Firmware image unavailable.\n");
		qla_printk(KERN_ERR, ha, "Firmware images can be retrieved "
		    "from: " QLA_FW_URL ".\n");
		return QLA_FUNCTION_FAILED;
	}

	rval = QLA_SUCCESS;

	wcode = (uint16_t *)req->ring;
	*srisc_addr = 0;
	fwcode = (uint16_t *)blob->fw->data;
	fwclen = 0;

	/* Validate firmware image by checking version. */
	if (blob->fw->size < 8 * sizeof(uint16_t)) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to verify integrity of firmware image (%Zd)!\n",
		    blob->fw->size);
		goto fail_fw_integrity;
	}
	for (i = 0; i < 4; i++)
		wcode[i] = be16_to_cpu(fwcode[i + 4]);
	if ((wcode[0] == 0xffff && wcode[1] == 0xffff && wcode[2] == 0xffff &&
	    wcode[3] == 0xffff) || (wcode[0] == 0 && wcode[1] == 0 &&
		wcode[2] == 0 && wcode[3] == 0)) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to verify integrity of firmware image!\n");
		qla_printk(KERN_WARNING, ha,
		    "Firmware data: %04x %04x %04x %04x!\n", wcode[0],
		    wcode[1], wcode[2], wcode[3]);
		goto fail_fw_integrity;
	}

	seg = blob->segs;
	while (*seg && rval == QLA_SUCCESS) {
		risc_addr = *seg;
		*srisc_addr = *srisc_addr == 0 ? *seg : *srisc_addr;
		risc_size = be16_to_cpu(fwcode[3]);

		/* Validate firmware image size. */
		fwclen += risc_size * sizeof(uint16_t);
		if (blob->fw->size < fwclen) {
			qla_printk(KERN_WARNING, ha,
			    "Unable to verify integrity of firmware image "
			    "(%Zd)!\n", blob->fw->size);
			goto fail_fw_integrity;
		}

		fragment = 0;
		while (risc_size > 0 && rval == QLA_SUCCESS) {
			wlen = (uint16_t)(ha->fw_transfer_size >> 1);
			if (wlen > risc_size)
				wlen = risc_size;

			DEBUG7(printk("scsi(%ld): Loading risc segment@ risc "
			    "addr %x, number of words 0x%x.\n", vha->host_no,
			    risc_addr, wlen));

			for (i = 0; i < wlen; i++)
				wcode[i] = swab16(fwcode[i]);

			rval = qla2x00_load_ram(vha, req->dma, risc_addr,
			    wlen);
			if (rval) {
				DEBUG(printk("scsi(%ld):[ERROR] Failed to load "
				    "segment %d of firmware\n", vha->host_no,
				    fragment));
				qla_printk(KERN_WARNING, ha,
				    "[ERROR] Failed to load segment %d of "
				    "firmware\n", fragment);
				break;
			}

			fwcode += wlen;
			risc_addr += wlen;
			risc_size -= wlen;
			fragment++;
		}

		/* Next segment. */
		seg++;
	}
	return rval;

fail_fw_integrity:
	return QLA_FUNCTION_FAILED;
}

static int
qla24xx_load_risc_blob(scsi_qla_host_t *vha, uint32_t *srisc_addr)
{
	int	rval;
	int	segments, fragment;
	uint32_t *dcode, dlen;
	uint32_t risc_addr;
	uint32_t risc_size;
	uint32_t i;
	struct fw_blob *blob;
	uint32_t *fwcode, fwclen;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];

	/* Load firmware blob. */
	blob = qla2x00_request_firmware(vha);
	if (!blob) {
		qla_printk(KERN_ERR, ha, "Firmware image unavailable.\n");
		qla_printk(KERN_ERR, ha, "Firmware images can be retrieved "
		    "from: " QLA_FW_URL ".\n");

		return QLA_FUNCTION_FAILED;
	}

	qla_printk(KERN_INFO, ha,
	    "FW: Loading via request-firmware...\n");

	rval = QLA_SUCCESS;

	segments = FA_RISC_CODE_SEGMENTS;
	dcode = (uint32_t *)req->ring;
	*srisc_addr = 0;
	fwcode = (uint32_t *)blob->fw->data;
	fwclen = 0;

	/* Validate firmware image by checking version. */
	if (blob->fw->size < 8 * sizeof(uint32_t)) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to verify integrity of firmware image (%Zd)!\n",
		    blob->fw->size);
		goto fail_fw_integrity;
	}
	for (i = 0; i < 4; i++)
		dcode[i] = be32_to_cpu(fwcode[i + 4]);
	if ((dcode[0] == 0xffffffff && dcode[1] == 0xffffffff &&
	    dcode[2] == 0xffffffff && dcode[3] == 0xffffffff) ||
	    (dcode[0] == 0 && dcode[1] == 0 && dcode[2] == 0 &&
		dcode[3] == 0)) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to verify integrity of firmware image!\n");
		qla_printk(KERN_WARNING, ha,
		    "Firmware data: %08x %08x %08x %08x!\n", dcode[0],
		    dcode[1], dcode[2], dcode[3]);
		goto fail_fw_integrity;
	}

	while (segments && rval == QLA_SUCCESS) {
		risc_addr = be32_to_cpu(fwcode[2]);
		*srisc_addr = *srisc_addr == 0 ? risc_addr : *srisc_addr;
		risc_size = be32_to_cpu(fwcode[3]);

		/* Validate firmware image size. */
		fwclen += risc_size * sizeof(uint32_t);
		if (blob->fw->size < fwclen) {
			qla_printk(KERN_WARNING, ha,
			    "Unable to verify integrity of firmware image "
			    "(%Zd)!\n", blob->fw->size);

			goto fail_fw_integrity;
		}

		fragment = 0;
		while (risc_size > 0 && rval == QLA_SUCCESS) {
			dlen = (uint32_t)(ha->fw_transfer_size >> 2);
			if (dlen > risc_size)
				dlen = risc_size;

			DEBUG7(printk("scsi(%ld): Loading risc segment@ risc "
			    "addr %x, number of dwords 0x%x.\n", vha->host_no,
			    risc_addr, dlen));

			for (i = 0; i < dlen; i++)
				dcode[i] = swab32(fwcode[i]);

			rval = qla2x00_load_ram(vha, req->dma, risc_addr,
			    dlen);
			if (rval) {
				DEBUG(printk("scsi(%ld):[ERROR] Failed to load "
				    "segment %d of firmware\n", vha->host_no,
				    fragment));
				qla_printk(KERN_WARNING, ha,
				    "[ERROR] Failed to load segment %d of "
				    "firmware\n", fragment);
				break;
			}

			fwcode += dlen;
			risc_addr += dlen;
			risc_size -= dlen;
			fragment++;
		}

		/* Next segment. */
		segments--;
	}
	return rval;

fail_fw_integrity:
	return QLA_FUNCTION_FAILED;
}

int
qla24xx_load_risc(scsi_qla_host_t *vha, uint32_t *srisc_addr)
{
	int rval;

	if (ql2xfwloadbin == 1)
		return qla81xx_load_risc(vha, srisc_addr);

	/*
	 * FW Load priority:
	 * 1) Firmware via request-firmware interface (.bin file).
	 * 2) Firmware residing in flash.
	 */
	rval = qla24xx_load_risc_blob(vha, srisc_addr);
	if (rval == QLA_SUCCESS)
		return rval;

	return qla24xx_load_risc_flash(vha, srisc_addr,
	    vha->hw->flt_region_fw);
}

int
qla81xx_load_risc(scsi_qla_host_t *vha, uint32_t *srisc_addr)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;

	if (ql2xfwloadbin == 2)
		goto try_blob_fw;

	/*
	 * FW Load priority:
	 * 1) Firmware residing in flash.
	 * 2) Firmware via request-firmware interface (.bin file).
	 * 3) Golden-Firmware residing in flash -- limited operation.
	 */
	rval = qla24xx_load_risc_flash(vha, srisc_addr, ha->flt_region_fw);
	if (rval == QLA_SUCCESS)
		return rval;

try_blob_fw:
	rval = qla24xx_load_risc_blob(vha, srisc_addr);
	if (rval == QLA_SUCCESS || !ha->flt_region_gold_fw)
		return rval;

	qla_printk(KERN_ERR, ha,
	    "FW: Attempting to fallback to golden firmware...\n");
	rval = qla24xx_load_risc_flash(vha, srisc_addr, ha->flt_region_gold_fw);
	if (rval != QLA_SUCCESS)
		return rval;

	qla_printk(KERN_ERR, ha,
	    "FW: Please update operational firmware...\n");
	ha->flags.running_gold_fw = 1;

	return rval;
}

void
qla2x00_try_to_stop_firmware(scsi_qla_host_t *vha)
{
	int ret, retries;
	struct qla_hw_data *ha = vha->hw;

	if (ha->flags.pci_channel_io_perm_failure)
		return;
	if (!IS_FWI2_CAPABLE(ha))
		return;
	if (!ha->fw_major_version)
		return;

	ret = qla2x00_stop_firmware(vha);
	for (retries = 5; ret != QLA_SUCCESS && ret != QLA_FUNCTION_TIMEOUT &&
	    ret != QLA_INVALID_COMMAND && retries ; retries--) {
		ha->isp_ops->reset_chip(vha);
		if (ha->isp_ops->chip_diag(vha) != QLA_SUCCESS)
			continue;
		if (qla2x00_setup_chip(vha) != QLA_SUCCESS)
			continue;
		qla_printk(KERN_INFO, ha,
		    "Attempting retry of stop-firmware command...\n");
		ret = qla2x00_stop_firmware(vha);
	}
}

int
qla24xx_configure_vhba(scsi_qla_host_t *vha)
{
	int rval = QLA_SUCCESS;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);
	struct req_que *req;
	struct rsp_que *rsp;

	if (!vha->vp_idx)
		return -EINVAL;

	rval = qla2x00_fw_ready(base_vha);
	if (ha->flags.cpu_affinity_enabled)
		req = ha->req_q_map[0];
	else
		req = vha->req;
	rsp = req->rsp;

	if (rval == QLA_SUCCESS) {
		clear_bit(RESET_MARKER_NEEDED, &vha->dpc_flags);
		qla2x00_marker(vha, req, rsp, 0, 0, MK_SYNC_ALL);
	}

	vha->flags.management_server_logged_in = 0;

	/* Login to SNS first */
	ha->isp_ops->fabric_login(vha, NPH_SNS, 0xff, 0xff, 0xfc, mb, BIT_1);
	if (mb[0] != MBS_COMMAND_COMPLETE) {
		DEBUG15(qla_printk(KERN_INFO, ha,
		    "Failed SNS login: loop_id=%x mb[0]=%x mb[1]=%x "
		    "mb[2]=%x mb[6]=%x mb[7]=%x\n", NPH_SNS,
		    mb[0], mb[1], mb[2], mb[6], mb[7]));
		return (QLA_FUNCTION_FAILED);
	}

	atomic_set(&vha->loop_down_timer, 0);
	atomic_set(&vha->loop_state, LOOP_UP);
	set_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
	set_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags);
	rval = qla2x00_loop_resync(base_vha);

	return rval;
}

/* 84XX Support **************************************************************/

static LIST_HEAD(qla_cs84xx_list);
static DEFINE_MUTEX(qla_cs84xx_mutex);

static struct qla_chip_state_84xx *
qla84xx_get_chip(struct scsi_qla_host *vha)
{
	struct qla_chip_state_84xx *cs84xx;
	struct qla_hw_data *ha = vha->hw;

	mutex_lock(&qla_cs84xx_mutex);

	/* Find any shared 84xx chip. */
	list_for_each_entry(cs84xx, &qla_cs84xx_list, list) {
		if (cs84xx->bus == ha->pdev->bus) {
			kref_get(&cs84xx->kref);
			goto done;
		}
	}

	cs84xx = kzalloc(sizeof(*cs84xx), GFP_KERNEL);
	if (!cs84xx)
		goto done;

	kref_init(&cs84xx->kref);
	spin_lock_init(&cs84xx->access_lock);
	mutex_init(&cs84xx->fw_update_mutex);
	cs84xx->bus = ha->pdev->bus;

	list_add_tail(&cs84xx->list, &qla_cs84xx_list);
done:
	mutex_unlock(&qla_cs84xx_mutex);
	return cs84xx;
}

static void
__qla84xx_chip_release(struct kref *kref)
{
	struct qla_chip_state_84xx *cs84xx =
	    container_of(kref, struct qla_chip_state_84xx, kref);

	mutex_lock(&qla_cs84xx_mutex);
	list_del(&cs84xx->list);
	mutex_unlock(&qla_cs84xx_mutex);
	kfree(cs84xx);
}

void
qla84xx_put_chip(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	if (ha->cs84xx)
		kref_put(&ha->cs84xx->kref, __qla84xx_chip_release);
}

static int
qla84xx_init_chip(scsi_qla_host_t *vha)
{
	int rval;
	uint16_t status[2];
	struct qla_hw_data *ha = vha->hw;

	mutex_lock(&ha->cs84xx->fw_update_mutex);

	rval = qla84xx_verify_chip(vha, status);

	mutex_unlock(&ha->cs84xx->fw_update_mutex);

	return rval != QLA_SUCCESS || status[0] ? QLA_FUNCTION_FAILED:
	    QLA_SUCCESS;
}

/* 81XX Support **************************************************************/

int
qla81xx_nvram_config(scsi_qla_host_t *vha)
{
	int   rval;
	struct init_cb_81xx *icb;
	struct nvram_81xx *nv;
	uint32_t *dptr;
	uint8_t  *dptr1, *dptr2;
	uint32_t chksum;
	uint16_t cnt;
	struct qla_hw_data *ha = vha->hw;

	rval = QLA_SUCCESS;
	icb = (struct init_cb_81xx *)ha->init_cb;
	nv = ha->nvram;

	/* Determine NVRAM starting address. */
	ha->nvram_size = sizeof(struct nvram_81xx);
	ha->vpd_size = FA_NVRAM_VPD_SIZE;

	/* Get VPD data into cache */
	ha->vpd = ha->nvram + VPD_OFFSET;
	ha->isp_ops->read_optrom(vha, ha->vpd, ha->flt_region_vpd << 2,
	    ha->vpd_size);

	/* Get NVRAM data into cache and calculate checksum. */
	ha->isp_ops->read_optrom(vha, ha->nvram, ha->flt_region_nvram << 2,
	    ha->nvram_size);
	dptr = (uint32_t *)nv;
	for (cnt = 0, chksum = 0; cnt < ha->nvram_size >> 2; cnt++)
		chksum += le32_to_cpu(*dptr++);

	DEBUG5(printk("scsi(%ld): Contents of NVRAM\n", vha->host_no));
	DEBUG5(qla2x00_dump_buffer((uint8_t *)nv, ha->nvram_size));

	/* Bad NVRAM data, set defaults parameters. */
	if (chksum || nv->id[0] != 'I' || nv->id[1] != 'S' || nv->id[2] != 'P'
	    || nv->id[3] != ' ' ||
	    nv->nvram_version < __constant_cpu_to_le16(ICB_VERSION)) {
		/* Reset NVRAM data. */
		qla_printk(KERN_WARNING, ha, "Inconsistent NVRAM detected: "
		    "checksum=0x%x id=%c version=0x%x.\n", chksum, nv->id[0],
		    le16_to_cpu(nv->nvram_version));
		qla_printk(KERN_WARNING, ha, "Falling back to functioning (yet "
		    "invalid -- WWPN) defaults.\n");

		/*
		 * Set default initialization control block.
		 */
		memset(nv, 0, ha->nvram_size);
		nv->nvram_version = __constant_cpu_to_le16(ICB_VERSION);
		nv->version = __constant_cpu_to_le16(ICB_VERSION);
		nv->frame_payload_size = __constant_cpu_to_le16(2048);
		nv->execution_throttle = __constant_cpu_to_le16(0xFFFF);
		nv->exchange_count = __constant_cpu_to_le16(0);
		nv->port_name[0] = 0x21;
		nv->port_name[1] = 0x00 + ha->port_no;
		nv->port_name[2] = 0x00;
		nv->port_name[3] = 0xe0;
		nv->port_name[4] = 0x8b;
		nv->port_name[5] = 0x1c;
		nv->port_name[6] = 0x55;
		nv->port_name[7] = 0x86;
		nv->node_name[0] = 0x20;
		nv->node_name[1] = 0x00;
		nv->node_name[2] = 0x00;
		nv->node_name[3] = 0xe0;
		nv->node_name[4] = 0x8b;
		nv->node_name[5] = 0x1c;
		nv->node_name[6] = 0x55;
		nv->node_name[7] = 0x86;
		nv->login_retry_count = __constant_cpu_to_le16(8);
		nv->interrupt_delay_timer = __constant_cpu_to_le16(0);
		nv->login_timeout = __constant_cpu_to_le16(0);
		nv->firmware_options_1 =
		    __constant_cpu_to_le32(BIT_14|BIT_13|BIT_2|BIT_1);
		nv->firmware_options_2 = __constant_cpu_to_le32(2 << 4);
		nv->firmware_options_2 |= __constant_cpu_to_le32(BIT_12);
		nv->firmware_options_3 = __constant_cpu_to_le32(2 << 13);
		nv->host_p = __constant_cpu_to_le32(BIT_11|BIT_10);
		nv->efi_parameters = __constant_cpu_to_le32(0);
		nv->reset_delay = 5;
		nv->max_luns_per_target = __constant_cpu_to_le16(128);
		nv->port_down_retry_count = __constant_cpu_to_le16(30);
		nv->link_down_timeout = __constant_cpu_to_le16(30);
		nv->enode_mac[0] = 0x00;
		nv->enode_mac[1] = 0x02;
		nv->enode_mac[2] = 0x03;
		nv->enode_mac[3] = 0x04;
		nv->enode_mac[4] = 0x05;
		nv->enode_mac[5] = 0x06 + ha->port_no;

		rval = 1;
	}

	/* Reset Initialization control block */
	memset(icb, 0, sizeof(struct init_cb_81xx));

	/* Copy 1st segment. */
	dptr1 = (uint8_t *)icb;
	dptr2 = (uint8_t *)&nv->version;
	cnt = (uint8_t *)&icb->response_q_inpointer - (uint8_t *)&icb->version;
	while (cnt--)
		*dptr1++ = *dptr2++;

	icb->login_retry_count = nv->login_retry_count;

	/* Copy 2nd segment. */
	dptr1 = (uint8_t *)&icb->interrupt_delay_timer;
	dptr2 = (uint8_t *)&nv->interrupt_delay_timer;
	cnt = (uint8_t *)&icb->reserved_5 -
	    (uint8_t *)&icb->interrupt_delay_timer;
	while (cnt--)
		*dptr1++ = *dptr2++;

	memcpy(icb->enode_mac, nv->enode_mac, sizeof(icb->enode_mac));
	/* Some boards (with valid NVRAMs) still have NULL enode_mac!! */
	if (!memcmp(icb->enode_mac, "\0\0\0\0\0\0", sizeof(icb->enode_mac))) {
		icb->enode_mac[0] = 0x01;
		icb->enode_mac[1] = 0x02;
		icb->enode_mac[2] = 0x03;
		icb->enode_mac[3] = 0x04;
		icb->enode_mac[4] = 0x05;
		icb->enode_mac[5] = 0x06 + ha->port_no;
	}

	/* Use extended-initialization control block. */
	memcpy(ha->ex_init_cb, &nv->ex_version, sizeof(*ha->ex_init_cb));

	/*
	 * Setup driver NVRAM options.
	 */
	qla2x00_set_model_info(vha, nv->model_name, sizeof(nv->model_name),
	    "QLE8XXX");

	/* Use alternate WWN? */
	if (nv->host_p & __constant_cpu_to_le32(BIT_15)) {
		memcpy(icb->node_name, nv->alternate_node_name, WWN_SIZE);
		memcpy(icb->port_name, nv->alternate_port_name, WWN_SIZE);
	}

	/* Prepare nodename */
	if ((icb->firmware_options_1 & __constant_cpu_to_le32(BIT_14)) == 0) {
		/*
		 * Firmware will apply the following mask if the nodename was
		 * not provided.
		 */
		memcpy(icb->node_name, icb->port_name, WWN_SIZE);
		icb->node_name[0] &= 0xF0;
	}

	/* Set host adapter parameters. */
	ha->flags.disable_risc_code_load = 0;
	ha->flags.enable_lip_reset = 0;
	ha->flags.enable_lip_full_login =
	    le32_to_cpu(nv->host_p) & BIT_10 ? 1: 0;
	ha->flags.enable_target_reset =
	    le32_to_cpu(nv->host_p) & BIT_11 ? 1: 0;
	ha->flags.enable_led_scheme = 0;
	ha->flags.disable_serdes = le32_to_cpu(nv->host_p) & BIT_5 ? 1: 0;

	ha->operating_mode = (le32_to_cpu(icb->firmware_options_2) &
	    (BIT_6 | BIT_5 | BIT_4)) >> 4;

	/* save HBA serial number */
	ha->serial0 = icb->port_name[5];
	ha->serial1 = icb->port_name[6];
	ha->serial2 = icb->port_name[7];
	memcpy(vha->node_name, icb->node_name, WWN_SIZE);
	memcpy(vha->port_name, icb->port_name, WWN_SIZE);

	icb->execution_throttle = __constant_cpu_to_le16(0xFFFF);

	ha->retry_count = le16_to_cpu(nv->login_retry_count);

	/* Set minimum login_timeout to 4 seconds. */
	if (le16_to_cpu(nv->login_timeout) < ql2xlogintimeout)
		nv->login_timeout = cpu_to_le16(ql2xlogintimeout);
	if (le16_to_cpu(nv->login_timeout) < 4)
		nv->login_timeout = __constant_cpu_to_le16(4);
	ha->login_timeout = le16_to_cpu(nv->login_timeout);
	icb->login_timeout = nv->login_timeout;

	/* Set minimum RATOV to 100 tenths of a second. */
	ha->r_a_tov = 100;

	ha->loop_reset_delay = nv->reset_delay;

	/* Link Down Timeout = 0:
	 *
	 * 	When Port Down timer expires we will start returning
	 *	I/O's to OS with "DID_NO_CONNECT".
	 *
	 * Link Down Timeout != 0:
	 *
	 *	 The driver waits for the link to come up after link down
	 *	 before returning I/Os to OS with "DID_NO_CONNECT".
	 */
	if (le16_to_cpu(nv->link_down_timeout) == 0) {
		ha->loop_down_abort_time =
		    (LOOP_DOWN_TIME - LOOP_DOWN_TIMEOUT);
	} else {
		ha->link_down_timeout =	le16_to_cpu(nv->link_down_timeout);
		ha->loop_down_abort_time =
		    (LOOP_DOWN_TIME - ha->link_down_timeout);
	}

	/* Need enough time to try and get the port back. */
	ha->port_down_retry_count = le16_to_cpu(nv->port_down_retry_count);
	if (qlport_down_retry)
		ha->port_down_retry_count = qlport_down_retry;

	/* Set login_retry_count */
	ha->login_retry_count  = le16_to_cpu(nv->login_retry_count);
	if (ha->port_down_retry_count ==
	    le16_to_cpu(nv->port_down_retry_count) &&
	    ha->port_down_retry_count > 3)
		ha->login_retry_count = ha->port_down_retry_count;
	else if (ha->port_down_retry_count > (int)ha->login_retry_count)
		ha->login_retry_count = ha->port_down_retry_count;
	if (ql2xloginretrycount)
		ha->login_retry_count = ql2xloginretrycount;

	/* Enable ZIO. */
	if (!vha->flags.init_done) {
		ha->zio_mode = le32_to_cpu(icb->firmware_options_2) &
		    (BIT_3 | BIT_2 | BIT_1 | BIT_0);
		ha->zio_timer = le16_to_cpu(icb->interrupt_delay_timer) ?
		    le16_to_cpu(icb->interrupt_delay_timer): 2;
	}
	icb->firmware_options_2 &= __constant_cpu_to_le32(
	    ~(BIT_3 | BIT_2 | BIT_1 | BIT_0));
	vha->flags.process_response_queue = 0;
	if (ha->zio_mode != QLA_ZIO_DISABLED) {
		ha->zio_mode = QLA_ZIO_MODE_6;

		DEBUG2(printk("scsi(%ld): ZIO mode %d enabled; timer delay "
		    "(%d us).\n", vha->host_no, ha->zio_mode,
		    ha->zio_timer * 100));
		qla_printk(KERN_INFO, ha,
		    "ZIO mode %d enabled; timer delay (%d us).\n",
		    ha->zio_mode, ha->zio_timer * 100);

		icb->firmware_options_2 |= cpu_to_le32(
		    (uint32_t)ha->zio_mode);
		icb->interrupt_delay_timer = cpu_to_le16(ha->zio_timer);
		vha->flags.process_response_queue = 1;
	}

	if (rval) {
		DEBUG2_3(printk(KERN_WARNING
		    "scsi(%ld): NVRAM configuration failed!\n", vha->host_no));
	}
	return (rval);
}

int
qla82xx_restart_isp(scsi_qla_host_t *vha)
{
	int status, rval;
	uint32_t wait_time;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];
	struct rsp_que *rsp = ha->rsp_q_map[0];
	struct scsi_qla_host *vp;
	unsigned long flags;

	status = qla2x00_init_rings(vha);
	if (!status) {
		clear_bit(RESET_MARKER_NEEDED, &vha->dpc_flags);
		ha->flags.chip_reset_done = 1;

		status = qla2x00_fw_ready(vha);
		if (!status) {
			qla_printk(KERN_INFO, ha,
			"%s(): Start configure loop, "
			"status = %d\n", __func__, status);

			/* Issue a marker after FW becomes ready. */
			qla2x00_marker(vha, req, rsp, 0, 0, MK_SYNC_ALL);

			vha->flags.online = 1;
			/* Wait at most MAX_TARGET RSCNs for a stable link. */
			wait_time = 256;
			do {
				clear_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
				qla2x00_configure_loop(vha);
				wait_time--;
			} while (!atomic_read(&vha->loop_down_timer) &&
			    !(test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags)) &&
			    wait_time &&
			    (test_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags)));
		}

		/* if no cable then assume it's good */
		if ((vha->device_flags & DFLG_NO_CABLE))
			status = 0;

		qla_printk(KERN_INFO, ha,
			"%s(): Configure loop done, status = 0x%x\n",
			__func__, status);
	}

	if (!status) {
		clear_bit(RESET_MARKER_NEEDED, &vha->dpc_flags);

		if (!atomic_read(&vha->loop_down_timer)) {
			/*
			 * Issue marker command only when we are going
			 * to start the I/O .
			 */
			vha->marker_needed = 1;
		}

		vha->flags.online = 1;

		ha->isp_ops->enable_intrs(ha);

		ha->isp_abort_cnt = 0;
		clear_bit(ISP_ABORT_RETRY, &vha->dpc_flags);

		if (ha->fce) {
			ha->flags.fce_enabled = 1;
			memset(ha->fce, 0,
			    fce_calc_size(ha->fce_bufs));
			rval = qla2x00_enable_fce_trace(vha,
			    ha->fce_dma, ha->fce_bufs, ha->fce_mb,
			    &ha->fce_bufs);
			if (rval) {
				qla_printk(KERN_WARNING, ha,
				    "Unable to reinitialize FCE "
				    "(%d).\n", rval);
				ha->flags.fce_enabled = 0;
			}
		}

		if (ha->eft) {
			memset(ha->eft, 0, EFT_SIZE);
			rval = qla2x00_enable_eft_trace(vha,
			    ha->eft_dma, EFT_NUM_BUFFERS);
			if (rval) {
				qla_printk(KERN_WARNING, ha,
				    "Unable to reinitialize EFT "
				    "(%d).\n", rval);
			}
		}
	}

	if (!status) {
		DEBUG(printk(KERN_INFO
			"qla82xx_restart_isp(%ld): succeeded.\n",
			vha->host_no));

		spin_lock_irqsave(&ha->vport_slock, flags);
		list_for_each_entry(vp, &ha->vp_list, list) {
			if (vp->vp_idx) {
				atomic_inc(&vp->vref_count);
				spin_unlock_irqrestore(&ha->vport_slock, flags);

				qla2x00_vp_abort_isp(vp);

				spin_lock_irqsave(&ha->vport_slock, flags);
				atomic_dec(&vp->vref_count);
			}
		}
		spin_unlock_irqrestore(&ha->vport_slock, flags);

	} else {
		qla_printk(KERN_INFO, ha,
			"qla82xx_restart_isp: **** FAILED ****\n");
	}

	return status;
}

void
qla81xx_update_fw_options(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;

	if (!ql2xetsenable)
		return;

	/* Enable ETS Burst. */
	memset(ha->fw_options, 0, sizeof(ha->fw_options));
	ha->fw_options[2] |= BIT_9;
	qla2x00_set_fw_options(vha, ha->fw_options);
}

/*
 * qla24xx_get_fcp_prio
 *	Gets the fcp cmd priority value for the logged in port.
 *	Looks for a match of the port descriptors within
 *	each of the fcp prio config entries. If a match is found,
 *	the tag (priority) value is returned.
 *
 * Input:
 *	vha = scsi host structure pointer.
 *	fcport = port structure pointer.
 *
 * Return:
 *	non-zero (if found)
 * 	0 (if not found)
 *
 * Context:
 * 	Kernel context
 */
uint8_t
qla24xx_get_fcp_prio(scsi_qla_host_t *vha, fc_port_t *fcport)
{
	int i, entries;
	uint8_t pid_match, wwn_match;
	uint8_t priority;
	uint32_t pid1, pid2;
	uint64_t wwn1, wwn2;
	struct qla_fcp_prio_entry *pri_entry;
	struct qla_hw_data *ha = vha->hw;

	if (!ha->fcp_prio_cfg || !ha->flags.fcp_prio_enabled)
		return 0;

	priority = 0;
	entries = ha->fcp_prio_cfg->num_entries;
	pri_entry = &ha->fcp_prio_cfg->entry[0];

	for (i = 0; i < entries; i++) {
		pid_match = wwn_match = 0;

		if (!(pri_entry->flags & FCP_PRIO_ENTRY_VALID)) {
			pri_entry++;
			continue;
		}

		/* check source pid for a match */
		if (pri_entry->flags & FCP_PRIO_ENTRY_SPID_VALID) {
			pid1 = pri_entry->src_pid & INVALID_PORT_ID;
			pid2 = vha->d_id.b24 & INVALID_PORT_ID;
			if (pid1 == INVALID_PORT_ID)
				pid_match++;
			else if (pid1 == pid2)
				pid_match++;
		}

		/* check destination pid for a match */
		if (pri_entry->flags & FCP_PRIO_ENTRY_DPID_VALID) {
			pid1 = pri_entry->dst_pid & INVALID_PORT_ID;
			pid2 = fcport->d_id.b24 & INVALID_PORT_ID;
			if (pid1 == INVALID_PORT_ID)
				pid_match++;
			else if (pid1 == pid2)
				pid_match++;
		}

		/* check source WWN for a match */
		if (pri_entry->flags & FCP_PRIO_ENTRY_SWWN_VALID) {
			wwn1 = wwn_to_u64(vha->port_name);
			wwn2 = wwn_to_u64(pri_entry->src_wwpn);
			if (wwn2 == (uint64_t)-1)
				wwn_match++;
			else if (wwn1 == wwn2)
				wwn_match++;
		}

		/* check destination WWN for a match */
		if (pri_entry->flags & FCP_PRIO_ENTRY_DWWN_VALID) {
			wwn1 = wwn_to_u64(fcport->port_name);
			wwn2 = wwn_to_u64(pri_entry->dst_wwpn);
			if (wwn2 == (uint64_t)-1)
				wwn_match++;
			else if (wwn1 == wwn2)
				wwn_match++;
		}

		if (pid_match == 2 || wwn_match == 2) {
			/* Found a matching entry */
			if (pri_entry->flags & FCP_PRIO_ENTRY_TAG_VALID)
				priority = pri_entry->tag;
			break;
		}

		pri_entry++;
	}

	return priority;
}

/*
 * qla24xx_update_fcport_fcp_prio
 *	Activates fcp priority for the logged in fc port
 *
 * Input:
 *	vha = scsi host structure pointer.
 *	fcp = port structure pointer.
 *
 * Return:
 *	QLA_SUCCESS or QLA_FUNCTION_FAILED
 *
 * Context:
 *	Kernel context.
 */
int
qla24xx_update_fcport_fcp_prio(scsi_qla_host_t *vha, fc_port_t *fcport)
{
	int ret;
	uint8_t priority;
	uint16_t mb[5];

	if (fcport->port_type != FCT_TARGET ||
	    fcport->loop_id == FC_NO_LOOP_ID)
		return QLA_FUNCTION_FAILED;

	priority = qla24xx_get_fcp_prio(vha, fcport);
	ret = qla24xx_set_fcp_prio(vha, fcport->loop_id, priority, mb);
	if (ret == QLA_SUCCESS)
		fcport->fcp_prio = priority;
	else
		DEBUG2(printk(KERN_WARNING
			"scsi(%ld): Unable to activate fcp priority, "
			" ret=0x%x\n", vha->host_no, ret));

	return  ret;
}

/*
 * qla24xx_update_all_fcp_prio
 *	Activates fcp priority for all the logged in ports
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Return:
 *	QLA_SUCCESS or QLA_FUNCTION_FAILED
 *
 * Context:
 *	Kernel context.
 */
int
qla24xx_update_all_fcp_prio(scsi_qla_host_t *vha)
{
	int ret;
	fc_port_t *fcport;

	ret = QLA_FUNCTION_FAILED;
	/* We need to set priority for all logged in ports */
	list_for_each_entry(fcport, &vha->vp_fcports, list)
		ret = qla24xx_update_fcport_fcp_prio(vha, fcport);

	return ret;
}
