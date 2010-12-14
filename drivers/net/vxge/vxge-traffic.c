/******************************************************************************
 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 *
 * vxge-traffic.c: Driver for Exar Corp's X3100 Series 10GbE PCIe I/O
 *                 Virtualized Server Adapter.
 * Copyright(c) 2002-2010 Exar Corp.
 ******************************************************************************/
#include <linux/etherdevice.h>

#include "vxge-traffic.h"
#include "vxge-config.h"
#include "vxge-main.h"

static enum vxge_hw_status
__vxge_hw_device_handle_error(struct __vxge_hw_device *hldev,
			      u32 vp_id, enum vxge_hw_event type);
static enum vxge_hw_status
__vxge_hw_vpath_alarm_process(struct __vxge_hw_virtualpath *vpath,
			      u32 skip_alarms);

/*
 * vxge_hw_vpath_intr_enable - Enable vpath interrupts.
 * @vp: Virtual Path handle.
 *
 * Enable vpath interrupts. The function is to be executed the last in
 * vpath initialization sequence.
 *
 * See also: vxge_hw_vpath_intr_disable()
 */
enum vxge_hw_status vxge_hw_vpath_intr_enable(struct __vxge_hw_vpath_handle *vp)
{
	u64 val64;

	struct __vxge_hw_virtualpath *vpath;
	struct vxge_hw_vpath_reg __iomem *vp_reg;
	enum vxge_hw_status status = VXGE_HW_OK;
	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	vpath = vp->vpath;

	if (vpath->vp_open == VXGE_HW_VP_NOT_OPEN) {
		status = VXGE_HW_ERR_VPATH_NOT_OPEN;
		goto exit;
	}

	vp_reg = vpath->vp_reg;

	writeq(VXGE_HW_INTR_MASK_ALL, &vp_reg->kdfcctl_errors_reg);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->general_errors_reg);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->pci_config_errors_reg);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->mrpcim_to_vpath_alarm_reg);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->srpcim_to_vpath_alarm_reg);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->vpath_ppif_int_status);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->srpcim_msg_to_vpath_reg);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->vpath_pcipif_int_status);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->prc_alarm_reg);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->wrdma_alarm_status);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->asic_ntwk_vp_err_reg);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->xgmac_vp_int_status);

	val64 = readq(&vp_reg->vpath_general_int_status);

	/* Mask unwanted interrupts */

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->vpath_pcipif_int_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->srpcim_msg_to_vpath_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->srpcim_to_vpath_alarm_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->mrpcim_to_vpath_alarm_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->pci_config_errors_mask);

	/* Unmask the individual interrupts */

	writeq((u32)vxge_bVALn((VXGE_HW_GENERAL_ERRORS_REG_DBLGEN_FIFO1_OVRFLOW|
		VXGE_HW_GENERAL_ERRORS_REG_DBLGEN_FIFO2_OVRFLOW|
		VXGE_HW_GENERAL_ERRORS_REG_STATSB_DROP_TIMEOUT_REQ|
		VXGE_HW_GENERAL_ERRORS_REG_STATSB_PIF_CHAIN_ERR), 0, 32),
		&vp_reg->general_errors_mask);

	__vxge_hw_pio_mem_write32_upper(
		(u32)vxge_bVALn((VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO1_OVRWR|
		VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO2_OVRWR|
		VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO1_POISON|
		VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO2_POISON|
		VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO1_DMA_ERR|
		VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO2_DMA_ERR), 0, 32),
		&vp_reg->kdfcctl_errors_mask);

	__vxge_hw_pio_mem_write32_upper(0, &vp_reg->vpath_ppif_int_mask);

	__vxge_hw_pio_mem_write32_upper(
		(u32)vxge_bVALn(VXGE_HW_PRC_ALARM_REG_PRC_RING_BUMP, 0, 32),
		&vp_reg->prc_alarm_mask);

	__vxge_hw_pio_mem_write32_upper(0, &vp_reg->wrdma_alarm_mask);
	__vxge_hw_pio_mem_write32_upper(0, &vp_reg->xgmac_vp_int_mask);

	if (vpath->hldev->first_vp_id != vpath->vp_id)
		__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->asic_ntwk_vp_err_mask);
	else
		__vxge_hw_pio_mem_write32_upper((u32)vxge_bVALn((
		VXGE_HW_ASIC_NTWK_VP_ERR_REG_XMACJ_NTWK_REAFFIRMED_FAULT |
		VXGE_HW_ASIC_NTWK_VP_ERR_REG_XMACJ_NTWK_REAFFIRMED_OK), 0, 32),
		&vp_reg->asic_ntwk_vp_err_mask);

	__vxge_hw_pio_mem_write32_upper(0,
		&vp_reg->vpath_general_int_mask);
exit:
	return status;

}

/*
 * vxge_hw_vpath_intr_disable - Disable vpath interrupts.
 * @vp: Virtual Path handle.
 *
 * Disable vpath interrupts. The function is to be executed the last in
 * vpath initialization sequence.
 *
 * See also: vxge_hw_vpath_intr_enable()
 */
enum vxge_hw_status vxge_hw_vpath_intr_disable(
			struct __vxge_hw_vpath_handle *vp)
{
	u64 val64;

	struct __vxge_hw_virtualpath *vpath;
	enum vxge_hw_status status = VXGE_HW_OK;
	struct vxge_hw_vpath_reg __iomem *vp_reg;
	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	vpath = vp->vpath;

	if (vpath->vp_open == VXGE_HW_VP_NOT_OPEN) {
		status = VXGE_HW_ERR_VPATH_NOT_OPEN;
		goto exit;
	}
	vp_reg = vpath->vp_reg;

	__vxge_hw_pio_mem_write32_upper(
		(u32)VXGE_HW_INTR_MASK_ALL,
		&vp_reg->vpath_general_int_mask);

	val64 = VXGE_HW_TIM_CLR_INT_EN_VP(1 << (16 - vpath->vp_id));

	writeq(VXGE_HW_INTR_MASK_ALL, &vp_reg->kdfcctl_errors_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->general_errors_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->pci_config_errors_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->mrpcim_to_vpath_alarm_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->srpcim_to_vpath_alarm_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->vpath_ppif_int_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->srpcim_msg_to_vpath_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->vpath_pcipif_int_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->wrdma_alarm_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->prc_alarm_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->xgmac_vp_int_mask);

	__vxge_hw_pio_mem_write32_upper((u32)VXGE_HW_INTR_MASK_ALL,
			&vp_reg->asic_ntwk_vp_err_mask);

exit:
	return status;
}

/**
 * vxge_hw_channel_msix_mask - Mask MSIX Vector.
 * @channeh: Channel for rx or tx handle
 * @msix_id:  MSIX ID
 *
 * The function masks the msix interrupt for the given msix_id
 *
 * Returns: 0
 */
void vxge_hw_channel_msix_mask(struct __vxge_hw_channel *channel, int msix_id)
{

	__vxge_hw_pio_mem_write32_upper(
		(u32)vxge_bVALn(vxge_mBIT(msix_id >> 2), 0, 32),
		&channel->common_reg->set_msix_mask_vect[msix_id%4]);
}

/**
 * vxge_hw_channel_msix_unmask - Unmask the MSIX Vector.
 * @channeh: Channel for rx or tx handle
 * @msix_id:  MSI ID
 *
 * The function unmasks the msix interrupt for the given msix_id
 *
 * Returns: 0
 */
void
vxge_hw_channel_msix_unmask(struct __vxge_hw_channel *channel, int msix_id)
{

	__vxge_hw_pio_mem_write32_upper(
		(u32)vxge_bVALn(vxge_mBIT(msix_id >> 2), 0, 32),
		&channel->common_reg->clear_msix_mask_vect[msix_id%4]);
}

/**
 * vxge_hw_device_set_intr_type - Updates the configuration
 *		with new interrupt type.
 * @hldev: HW device handle.
 * @intr_mode: New interrupt type
 */
u32 vxge_hw_device_set_intr_type(struct __vxge_hw_device *hldev, u32 intr_mode)
{

	if ((intr_mode != VXGE_HW_INTR_MODE_IRQLINE) &&
	   (intr_mode != VXGE_HW_INTR_MODE_MSIX) &&
	   (intr_mode != VXGE_HW_INTR_MODE_MSIX_ONE_SHOT) &&
	   (intr_mode != VXGE_HW_INTR_MODE_DEF))
		intr_mode = VXGE_HW_INTR_MODE_IRQLINE;

	hldev->config.intr_mode = intr_mode;
	return intr_mode;
}

/**
 * vxge_hw_device_intr_enable - Enable interrupts.
 * @hldev: HW device handle.
 * @op: One of the enum vxge_hw_device_intr enumerated values specifying
 *      the type(s) of interrupts to enable.
 *
 * Enable Titan interrupts. The function is to be executed the last in
 * Titan initialization sequence.
 *
 * See also: vxge_hw_device_intr_disable()
 */
void vxge_hw_device_intr_enable(struct __vxge_hw_device *hldev)
{
	u32 i;
	u64 val64;
	u32 val32;

	vxge_hw_device_mask_all(hldev);

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {

		if (!(hldev->vpaths_deployed & vxge_mBIT(i)))
			continue;

		vxge_hw_vpath_intr_enable(
			VXGE_HW_VIRTUAL_PATH_HANDLE(&hldev->virtual_paths[i]));
	}

	if (hldev->config.intr_mode == VXGE_HW_INTR_MODE_IRQLINE) {
		val64 = hldev->tim_int_mask0[VXGE_HW_VPATH_INTR_TX] |
			hldev->tim_int_mask0[VXGE_HW_VPATH_INTR_RX];

		if (val64 != 0) {
			writeq(val64, &hldev->common_reg->tim_int_status0);

			writeq(~val64, &hldev->common_reg->tim_int_mask0);
		}

		val32 = hldev->tim_int_mask1[VXGE_HW_VPATH_INTR_TX] |
			hldev->tim_int_mask1[VXGE_HW_VPATH_INTR_RX];

		if (val32 != 0) {
			__vxge_hw_pio_mem_write32_upper(val32,
					&hldev->common_reg->tim_int_status1);

			__vxge_hw_pio_mem_write32_upper(~val32,
					&hldev->common_reg->tim_int_mask1);
		}
	}

	val64 = readq(&hldev->common_reg->titan_general_int_status);

	vxge_hw_device_unmask_all(hldev);
}

/**
 * vxge_hw_device_intr_disable - Disable Titan interrupts.
 * @hldev: HW device handle.
 * @op: One of the enum vxge_hw_device_intr enumerated values specifying
 *      the type(s) of interrupts to disable.
 *
 * Disable Titan interrupts.
 *
 * See also: vxge_hw_device_intr_enable()
 */
void vxge_hw_device_intr_disable(struct __vxge_hw_device *hldev)
{
	u32 i;

	vxge_hw_device_mask_all(hldev);

	/* mask all the tim interrupts */
	writeq(VXGE_HW_INTR_MASK_ALL, &hldev->common_reg->tim_int_mask0);
	__vxge_hw_pio_mem_write32_upper(VXGE_HW_DEFAULT_32,
		&hldev->common_reg->tim_int_mask1);

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {

		if (!(hldev->vpaths_deployed & vxge_mBIT(i)))
			continue;

		vxge_hw_vpath_intr_disable(
			VXGE_HW_VIRTUAL_PATH_HANDLE(&hldev->virtual_paths[i]));
	}
}

/**
 * vxge_hw_device_mask_all - Mask all device interrupts.
 * @hldev: HW device handle.
 *
 * Mask	all device interrupts.
 *
 * See also: vxge_hw_device_unmask_all()
 */
void vxge_hw_device_mask_all(struct __vxge_hw_device *hldev)
{
	u64 val64;

	val64 = VXGE_HW_TITAN_MASK_ALL_INT_ALARM |
		VXGE_HW_TITAN_MASK_ALL_INT_TRAFFIC;

	__vxge_hw_pio_mem_write32_upper((u32)vxge_bVALn(val64, 0, 32),
				&hldev->common_reg->titan_mask_all_int);
}

/**
 * vxge_hw_device_unmask_all - Unmask all device interrupts.
 * @hldev: HW device handle.
 *
 * Unmask all device interrupts.
 *
 * See also: vxge_hw_device_mask_all()
 */
void vxge_hw_device_unmask_all(struct __vxge_hw_device *hldev)
{
	u64 val64 = 0;

	if (hldev->config.intr_mode == VXGE_HW_INTR_MODE_IRQLINE)
		val64 =  VXGE_HW_TITAN_MASK_ALL_INT_TRAFFIC;

	__vxge_hw_pio_mem_write32_upper((u32)vxge_bVALn(val64, 0, 32),
			&hldev->common_reg->titan_mask_all_int);
}

/**
 * vxge_hw_device_flush_io - Flush io writes.
 * @hldev: HW device handle.
 *
 * The function	performs a read operation to flush io writes.
 *
 * Returns: void
 */
void vxge_hw_device_flush_io(struct __vxge_hw_device *hldev)
{
	u32 val32;

	val32 = readl(&hldev->common_reg->titan_general_int_status);
}

/**
 * vxge_hw_device_begin_irq - Begin IRQ processing.
 * @hldev: HW device handle.
 * @skip_alarms: Do not clear the alarms
 * @reason: "Reason" for the interrupt, the value of Titan's
 *	general_int_status register.
 *
 * The function	performs two actions, It first checks whether (shared IRQ) the
 * interrupt was raised	by the device. Next, it	masks the device interrupts.
 *
 * Note:
 * vxge_hw_device_begin_irq() does not flush MMIO writes through the
 * bridge. Therefore, two back-to-back interrupts are potentially possible.
 *
 * Returns: 0, if the interrupt	is not "ours" (note that in this case the
 * device remain enabled).
 * Otherwise, vxge_hw_device_begin_irq() returns 64bit general adapter
 * status.
 */
enum vxge_hw_status vxge_hw_device_begin_irq(struct __vxge_hw_device *hldev,
					     u32 skip_alarms, u64 *reason)
{
	u32 i;
	u64 val64;
	u64 adapter_status;
	u64 vpath_mask;
	enum vxge_hw_status ret = VXGE_HW_OK;

	val64 = readq(&hldev->common_reg->titan_general_int_status);

	if (unlikely(!val64)) {
		/* not Titan interrupt	*/
		*reason	= 0;
		ret = VXGE_HW_ERR_WRONG_IRQ;
		goto exit;
	}

	if (unlikely(val64 == VXGE_HW_ALL_FOXES)) {

		adapter_status = readq(&hldev->common_reg->adapter_status);

		if (adapter_status == VXGE_HW_ALL_FOXES) {

			__vxge_hw_device_handle_error(hldev,
				NULL_VPID, VXGE_HW_EVENT_SLOT_FREEZE);
			*reason	= 0;
			ret = VXGE_HW_ERR_SLOT_FREEZE;
			goto exit;
		}
	}

	hldev->stats.sw_dev_info_stats.total_intr_cnt++;

	*reason	= val64;

	vpath_mask = hldev->vpaths_deployed >>
				(64 - VXGE_HW_MAX_VIRTUAL_PATHS);

	if (val64 &
	    VXGE_HW_TITAN_GENERAL_INT_STATUS_VPATH_TRAFFIC_INT(vpath_mask)) {
		hldev->stats.sw_dev_info_stats.traffic_intr_cnt++;

		return VXGE_HW_OK;
	}

	hldev->stats.sw_dev_info_stats.not_traffic_intr_cnt++;

	if (unlikely(val64 &
			VXGE_HW_TITAN_GENERAL_INT_STATUS_VPATH_ALARM_INT)) {

		enum vxge_hw_status error_level = VXGE_HW_OK;

		hldev->stats.sw_dev_err_stats.vpath_alarms++;

		for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {

			if (!(hldev->vpaths_deployed & vxge_mBIT(i)))
				continue;

			ret = __vxge_hw_vpath_alarm_process(
				&hldev->virtual_paths[i], skip_alarms);

			error_level = VXGE_HW_SET_LEVEL(ret, error_level);

			if (unlikely((ret == VXGE_HW_ERR_CRITICAL) ||
				(ret == VXGE_HW_ERR_SLOT_FREEZE)))
				break;
		}

		ret = error_level;
	}
exit:
	return ret;
}

/*
 * __vxge_hw_device_handle_link_up_ind
 * @hldev: HW device handle.
 *
 * Link up indication handler. The function is invoked by HW when
 * Titan indicates that the link is up for programmable amount of time.
 */
static enum vxge_hw_status
__vxge_hw_device_handle_link_up_ind(struct __vxge_hw_device *hldev)
{
	/*
	 * If the previous link state is not down, return.
	 */
	if (hldev->link_state == VXGE_HW_LINK_UP)
		goto exit;

	hldev->link_state = VXGE_HW_LINK_UP;

	/* notify driver */
	if (hldev->uld_callbacks.link_up)
		hldev->uld_callbacks.link_up(hldev);
exit:
	return VXGE_HW_OK;
}

/*
 * __vxge_hw_device_handle_link_down_ind
 * @hldev: HW device handle.
 *
 * Link down indication handler. The function is invoked by HW when
 * Titan indicates that the link is down.
 */
static enum vxge_hw_status
__vxge_hw_device_handle_link_down_ind(struct __vxge_hw_device *hldev)
{
	/*
	 * If the previous link state is not down, return.
	 */
	if (hldev->link_state == VXGE_HW_LINK_DOWN)
		goto exit;

	hldev->link_state = VXGE_HW_LINK_DOWN;

	/* notify driver */
	if (hldev->uld_callbacks.link_down)
		hldev->uld_callbacks.link_down(hldev);
exit:
	return VXGE_HW_OK;
}

/**
 * __vxge_hw_device_handle_error - Handle error
 * @hldev: HW device
 * @vp_id: Vpath Id
 * @type: Error type. Please see enum vxge_hw_event{}
 *
 * Handle error.
 */
static enum vxge_hw_status
__vxge_hw_device_handle_error(
		struct __vxge_hw_device *hldev,
		u32 vp_id,
		enum vxge_hw_event type)
{
	switch (type) {
	case VXGE_HW_EVENT_UNKNOWN:
		break;
	case VXGE_HW_EVENT_RESET_START:
	case VXGE_HW_EVENT_RESET_COMPLETE:
	case VXGE_HW_EVENT_LINK_DOWN:
	case VXGE_HW_EVENT_LINK_UP:
		goto out;
	case VXGE_HW_EVENT_ALARM_CLEARED:
		goto out;
	case VXGE_HW_EVENT_ECCERR:
	case VXGE_HW_EVENT_MRPCIM_ECCERR:
		goto out;
	case VXGE_HW_EVENT_FIFO_ERR:
	case VXGE_HW_EVENT_VPATH_ERR:
	case VXGE_HW_EVENT_CRITICAL_ERR:
	case VXGE_HW_EVENT_SERR:
		break;
	case VXGE_HW_EVENT_SRPCIM_SERR:
	case VXGE_HW_EVENT_MRPCIM_SERR:
		goto out;
	case VXGE_HW_EVENT_SLOT_FREEZE:
		break;
	default:
		vxge_assert(0);
		goto out;
	}

	/* notify driver */
	if (hldev->uld_callbacks.crit_err)
		hldev->uld_callbacks.crit_err(
			(struct __vxge_hw_device *)hldev,
			type, vp_id);
out:

	return VXGE_HW_OK;
}

/**
 * vxge_hw_device_clear_tx_rx - Acknowledge (that is, clear) the
 * condition that has caused the Tx and RX interrupt.
 * @hldev: HW device.
 *
 * Acknowledge (that is, clear) the condition that has caused
 * the Tx and Rx interrupt.
 * See also: vxge_hw_device_begin_irq(),
 * vxge_hw_device_mask_tx_rx(), vxge_hw_device_unmask_tx_rx().
 */
void vxge_hw_device_clear_tx_rx(struct __vxge_hw_device *hldev)
{

	if ((hldev->tim_int_mask0[VXGE_HW_VPATH_INTR_TX] != 0) ||
	   (hldev->tim_int_mask0[VXGE_HW_VPATH_INTR_RX] != 0)) {
		writeq((hldev->tim_int_mask0[VXGE_HW_VPATH_INTR_TX] |
				 hldev->tim_int_mask0[VXGE_HW_VPATH_INTR_RX]),
				&hldev->common_reg->tim_int_status0);
	}

	if ((hldev->tim_int_mask1[VXGE_HW_VPATH_INTR_TX] != 0) ||
	   (hldev->tim_int_mask1[VXGE_HW_VPATH_INTR_RX] != 0)) {
		__vxge_hw_pio_mem_write32_upper(
				(hldev->tim_int_mask1[VXGE_HW_VPATH_INTR_TX] |
				 hldev->tim_int_mask1[VXGE_HW_VPATH_INTR_RX]),
				&hldev->common_reg->tim_int_status1);
	}
}

/*
 * vxge_hw_channel_dtr_alloc - Allocate a dtr from the channel
 * @channel: Channel
 * @dtrh: Buffer to return the DTR pointer
 *
 * Allocates a dtr from the reserve array. If the reserve array is empty,
 * it swaps the reserve and free arrays.
 *
 */
static enum vxge_hw_status
vxge_hw_channel_dtr_alloc(struct __vxge_hw_channel *channel, void **dtrh)
{
	void **tmp_arr;

	if (channel->reserve_ptr - channel->reserve_top > 0) {
_alloc_after_swap:
		*dtrh =	channel->reserve_arr[--channel->reserve_ptr];

		return VXGE_HW_OK;
	}

	/* switch between empty	and full arrays	*/

	/* the idea behind such	a design is that by having free	and reserved
	 * arrays separated we basically separated irq and non-irq parts.
	 * i.e.	no additional lock need	to be done when	we free	a resource */

	if (channel->length - channel->free_ptr > 0) {

		tmp_arr	= channel->reserve_arr;
		channel->reserve_arr = channel->free_arr;
		channel->free_arr = tmp_arr;
		channel->reserve_ptr = channel->length;
		channel->reserve_top = channel->free_ptr;
		channel->free_ptr = channel->length;

		channel->stats->reserve_free_swaps_cnt++;

		goto _alloc_after_swap;
	}

	channel->stats->full_cnt++;

	*dtrh =	NULL;
	return VXGE_HW_INF_OUT_OF_DESCRIPTORS;
}

/*
 * vxge_hw_channel_dtr_post - Post a dtr to the channel
 * @channelh: Channel
 * @dtrh: DTR pointer
 *
 * Posts a dtr to work array.
 *
 */
static void vxge_hw_channel_dtr_post(struct __vxge_hw_channel *channel,
				     void *dtrh)
{
	vxge_assert(channel->work_arr[channel->post_index] == NULL);

	channel->work_arr[channel->post_index++] = dtrh;

	/* wrap-around */
	if (channel->post_index	== channel->length)
		channel->post_index = 0;
}

/*
 * vxge_hw_channel_dtr_try_complete - Returns next completed dtr
 * @channel: Channel
 * @dtr: Buffer to return the next completed DTR pointer
 *
 * Returns the next completed dtr with out removing it from work array
 *
 */
void
vxge_hw_channel_dtr_try_complete(struct __vxge_hw_channel *channel, void **dtrh)
{
	vxge_assert(channel->compl_index < channel->length);

	*dtrh =	channel->work_arr[channel->compl_index];
	prefetch(*dtrh);
}

/*
 * vxge_hw_channel_dtr_complete - Removes next completed dtr from the work array
 * @channel: Channel handle
 *
 * Removes the next completed dtr from work array
 *
 */
void vxge_hw_channel_dtr_complete(struct __vxge_hw_channel *channel)
{
	channel->work_arr[channel->compl_index]	= NULL;

	/* wrap-around */
	if (++channel->compl_index == channel->length)
		channel->compl_index = 0;

	channel->stats->total_compl_cnt++;
}

/*
 * vxge_hw_channel_dtr_free - Frees a dtr
 * @channel: Channel handle
 * @dtr:  DTR pointer
 *
 * Returns the dtr to free array
 *
 */
void vxge_hw_channel_dtr_free(struct __vxge_hw_channel *channel, void *dtrh)
{
	channel->free_arr[--channel->free_ptr] = dtrh;
}

/*
 * vxge_hw_channel_dtr_count
 * @channel: Channel handle. Obtained via vxge_hw_channel_open().
 *
 * Retreive number of DTRs available. This function can not be called
 * from data path. ring_initial_replenishi() is the only user.
 */
int vxge_hw_channel_dtr_count(struct __vxge_hw_channel *channel)
{
	return (channel->reserve_ptr - channel->reserve_top) +
		(channel->length - channel->free_ptr);
}

/**
 * vxge_hw_ring_rxd_reserve	- Reserve ring descriptor.
 * @ring: Handle to the ring object used for receive
 * @rxdh: Reserved descriptor. On success HW fills this "out" parameter
 * with a valid handle.
 *
 * Reserve Rx descriptor for the subsequent filling-in driver
 * and posting on the corresponding channel (@channelh)
 * via vxge_hw_ring_rxd_post().
 *
 * Returns: VXGE_HW_OK - success.
 * VXGE_HW_INF_OUT_OF_DESCRIPTORS - Currently no descriptors available.
 *
 */
enum vxge_hw_status vxge_hw_ring_rxd_reserve(struct __vxge_hw_ring *ring,
	void **rxdh)
{
	enum vxge_hw_status status;
	struct __vxge_hw_channel *channel;

	channel = &ring->channel;

	status = vxge_hw_channel_dtr_alloc(channel, rxdh);

	if (status == VXGE_HW_OK) {
		struct vxge_hw_ring_rxd_1 *rxdp =
			(struct vxge_hw_ring_rxd_1 *)*rxdh;

		rxdp->control_0	= rxdp->control_1 = 0;
	}

	return status;
}

/**
 * vxge_hw_ring_rxd_free - Free descriptor.
 * @ring: Handle to the ring object used for receive
 * @rxdh: Descriptor handle.
 *
 * Free	the reserved descriptor. This operation is "symmetrical" to
 * vxge_hw_ring_rxd_reserve. The "free-ing" completes the descriptor's
 * lifecycle.
 *
 * After free-ing (see vxge_hw_ring_rxd_free()) the descriptor again can
 * be:
 *
 * - reserved (vxge_hw_ring_rxd_reserve);
 *
 * - posted	(vxge_hw_ring_rxd_post);
 *
 * - completed (vxge_hw_ring_rxd_next_completed);
 *
 * - and recycled again	(vxge_hw_ring_rxd_free).
 *
 * For alternative state transitions and more details please refer to
 * the design doc.
 *
 */
void vxge_hw_ring_rxd_free(struct __vxge_hw_ring *ring, void *rxdh)
{
	struct __vxge_hw_channel *channel;

	channel = &ring->channel;

	vxge_hw_channel_dtr_free(channel, rxdh);

}

/**
 * vxge_hw_ring_rxd_pre_post - Prepare rxd and post
 * @ring: Handle to the ring object used for receive
 * @rxdh: Descriptor handle.
 *
 * This routine prepares a rxd and posts
 */
void vxge_hw_ring_rxd_pre_post(struct __vxge_hw_ring *ring, void *rxdh)
{
	struct __vxge_hw_channel *channel;

	channel = &ring->channel;

	vxge_hw_channel_dtr_post(channel, rxdh);
}

/**
 * vxge_hw_ring_rxd_post_post - Process rxd after post.
 * @ring: Handle to the ring object used for receive
 * @rxdh: Descriptor handle.
 *
 * Processes rxd after post
 */
void vxge_hw_ring_rxd_post_post(struct __vxge_hw_ring *ring, void *rxdh)
{
	struct vxge_hw_ring_rxd_1 *rxdp = (struct vxge_hw_ring_rxd_1 *)rxdh;
	struct __vxge_hw_channel *channel;

	channel = &ring->channel;

	rxdp->control_0	= VXGE_HW_RING_RXD_LIST_OWN_ADAPTER;

	if (ring->stats->common_stats.usage_cnt > 0)
		ring->stats->common_stats.usage_cnt--;
}

/**
 * vxge_hw_ring_rxd_post - Post descriptor on the ring.
 * @ring: Handle to the ring object used for receive
 * @rxdh: Descriptor obtained via vxge_hw_ring_rxd_reserve().
 *
 * Post	descriptor on the ring.
 * Prior to posting the	descriptor should be filled in accordance with
 * Host/Titan interface specification for a given service (LL, etc.).
 *
 */
void vxge_hw_ring_rxd_post(struct __vxge_hw_ring *ring, void *rxdh)
{
	struct vxge_hw_ring_rxd_1 *rxdp = (struct vxge_hw_ring_rxd_1 *)rxdh;
	struct __vxge_hw_channel *channel;

	channel = &ring->channel;

	wmb();
	rxdp->control_0	= VXGE_HW_RING_RXD_LIST_OWN_ADAPTER;

	vxge_hw_channel_dtr_post(channel, rxdh);

	if (ring->stats->common_stats.usage_cnt > 0)
		ring->stats->common_stats.usage_cnt--;
}

/**
 * vxge_hw_ring_rxd_post_post_wmb - Process rxd after post with memory barrier.
 * @ring: Handle to the ring object used for receive
 * @rxdh: Descriptor handle.
 *
 * Processes rxd after post with memory barrier.
 */
void vxge_hw_ring_rxd_post_post_wmb(struct __vxge_hw_ring *ring, void *rxdh)
{
	struct __vxge_hw_channel *channel;

	channel = &ring->channel;

	wmb();
	vxge_hw_ring_rxd_post_post(ring, rxdh);
}

/**
 * vxge_hw_ring_rxd_next_completed - Get the _next_ completed descriptor.
 * @ring: Handle to the ring object used for receive
 * @rxdh: Descriptor handle. Returned by HW.
 * @t_code:	Transfer code, as per Titan User Guide,
 *	 Receive Descriptor Format. Returned by HW.
 *
 * Retrieve the	_next_ completed descriptor.
 * HW uses ring callback (*vxge_hw_ring_callback_f) to notifiy
 * driver of new completed descriptors. After that
 * the driver can use vxge_hw_ring_rxd_next_completed to retrieve the rest
 * completions (the very first completion is passed by HW via
 * vxge_hw_ring_callback_f).
 *
 * Implementation-wise, the driver is free to call
 * vxge_hw_ring_rxd_next_completed either immediately from inside the
 * ring callback, or in a deferred fashion and separate (from HW)
 * context.
 *
 * Non-zero @t_code means failure to fill-in receive buffer(s)
 * of the descriptor.
 * For instance, parity	error detected during the data transfer.
 * In this case	Titan will complete the descriptor and indicate
 * for the host	that the received data is not to be used.
 * For details please refer to Titan User Guide.
 *
 * Returns: VXGE_HW_OK - success.
 * VXGE_HW_INF_NO_MORE_COMPLETED_DESCRIPTORS - No completed descriptors
 * are currently available for processing.
 *
 * See also: vxge_hw_ring_callback_f{},
 * vxge_hw_fifo_rxd_next_completed(), enum vxge_hw_status{}.
 */
enum vxge_hw_status vxge_hw_ring_rxd_next_completed(
	struct __vxge_hw_ring *ring, void **rxdh, u8 *t_code)
{
	struct __vxge_hw_channel *channel;
	struct vxge_hw_ring_rxd_1 *rxdp;
	enum vxge_hw_status status = VXGE_HW_OK;
	u64 control_0, own;

	channel = &ring->channel;

	vxge_hw_channel_dtr_try_complete(channel, rxdh);

	rxdp = (struct vxge_hw_ring_rxd_1 *)*rxdh;
	if (rxdp == NULL) {
		status = VXGE_HW_INF_NO_MORE_COMPLETED_DESCRIPTORS;
		goto exit;
	}

	control_0 = rxdp->control_0;
	own = control_0 & VXGE_HW_RING_RXD_LIST_OWN_ADAPTER;
	*t_code	= (u8)VXGE_HW_RING_RXD_T_CODE_GET(control_0);

	/* check whether it is not the end */
	if (!own || ((*t_code == VXGE_HW_RING_T_CODE_FRM_DROP) && own)) {

		vxge_assert(((struct vxge_hw_ring_rxd_1 *)rxdp)->host_control !=
				0);

		++ring->cmpl_cnt;
		vxge_hw_channel_dtr_complete(channel);

		vxge_assert(*t_code != VXGE_HW_RING_RXD_T_CODE_UNUSED);

		ring->stats->common_stats.usage_cnt++;
		if (ring->stats->common_stats.usage_max <
				ring->stats->common_stats.usage_cnt)
			ring->stats->common_stats.usage_max =
				ring->stats->common_stats.usage_cnt;

		status = VXGE_HW_OK;
		goto exit;
	}

	/* reset it. since we don't want to return
	 * garbage to the driver */
	*rxdh =	NULL;
	status = VXGE_HW_INF_NO_MORE_COMPLETED_DESCRIPTORS;
exit:
	return status;
}

/**
 * vxge_hw_ring_handle_tcode - Handle transfer code.
 * @ring: Handle to the ring object used for receive
 * @rxdh: Descriptor handle.
 * @t_code: One of the enumerated (and documented in the Titan user guide)
 * "transfer codes".
 *
 * Handle descriptor's transfer code. The latter comes with each completed
 * descriptor.
 *
 * Returns: one of the enum vxge_hw_status{} enumerated types.
 * VXGE_HW_OK			- for success.
 * VXGE_HW_ERR_CRITICAL         - when encounters critical error.
 */
enum vxge_hw_status vxge_hw_ring_handle_tcode(
	struct __vxge_hw_ring *ring, void *rxdh, u8 t_code)
{
	struct __vxge_hw_channel *channel;
	enum vxge_hw_status status = VXGE_HW_OK;

	channel = &ring->channel;

	/* If the t_code is not supported and if the
	 * t_code is other than 0x5 (unparseable packet
	 * such as unknown UPV6 header), Drop it !!!
	 */

	if (t_code ==  VXGE_HW_RING_T_CODE_OK ||
		t_code == VXGE_HW_RING_T_CODE_L3_PKT_ERR) {
		status = VXGE_HW_OK;
		goto exit;
	}

	if (t_code > VXGE_HW_RING_T_CODE_MULTI_ERR) {
		status = VXGE_HW_ERR_INVALID_TCODE;
		goto exit;
	}

	ring->stats->rxd_t_code_err_cnt[t_code]++;
exit:
	return status;
}

/**
 * __vxge_hw_non_offload_db_post - Post non offload doorbell
 *
 * @fifo: fifohandle
 * @txdl_ptr: The starting location of the TxDL in host memory
 * @num_txds: The highest TxD in this TxDL (0 to 255 means 1 to 256)
 * @no_snoop: No snoop flags
 *
 * This function posts a non-offload doorbell to doorbell FIFO
 *
 */
static void __vxge_hw_non_offload_db_post(struct __vxge_hw_fifo *fifo,
	u64 txdl_ptr, u32 num_txds, u32 no_snoop)
{
	struct __vxge_hw_channel *channel;

	channel = &fifo->channel;

	writeq(VXGE_HW_NODBW_TYPE(VXGE_HW_NODBW_TYPE_NODBW) |
		VXGE_HW_NODBW_LAST_TXD_NUMBER(num_txds) |
		VXGE_HW_NODBW_GET_NO_SNOOP(no_snoop),
		&fifo->nofl_db->control_0);

	mmiowb();

	writeq(txdl_ptr, &fifo->nofl_db->txdl_ptr);

	mmiowb();
}

/**
 * vxge_hw_fifo_free_txdl_count_get - returns the number of txdls available in
 * the fifo
 * @fifoh: Handle to the fifo object used for non offload send
 */
u32 vxge_hw_fifo_free_txdl_count_get(struct __vxge_hw_fifo *fifoh)
{
	return vxge_hw_channel_dtr_count(&fifoh->channel);
}

/**
 * vxge_hw_fifo_txdl_reserve - Reserve fifo descriptor.
 * @fifoh: Handle to the fifo object used for non offload send
 * @txdlh: Reserved descriptor. On success HW fills this "out" parameter
 *        with a valid handle.
 * @txdl_priv: Buffer to return the pointer to per txdl space
 *
 * Reserve a single TxDL (that is, fifo descriptor)
 * for the subsequent filling-in by driver)
 * and posting on the corresponding channel (@channelh)
 * via vxge_hw_fifo_txdl_post().
 *
 * Note: it is the responsibility of driver to reserve multiple descriptors
 * for lengthy (e.g., LSO) transmit operation. A single fifo descriptor
 * carries up to configured number (fifo.max_frags) of contiguous buffers.
 *
 * Returns: VXGE_HW_OK - success;
 * VXGE_HW_INF_OUT_OF_DESCRIPTORS - Currently no descriptors available
 *
 */
enum vxge_hw_status vxge_hw_fifo_txdl_reserve(
	struct __vxge_hw_fifo *fifo,
	void **txdlh, void **txdl_priv)
{
	struct __vxge_hw_channel *channel;
	enum vxge_hw_status status;
	int i;

	channel = &fifo->channel;

	status = vxge_hw_channel_dtr_alloc(channel, txdlh);

	if (status == VXGE_HW_OK) {
		struct vxge_hw_fifo_txd *txdp =
			(struct vxge_hw_fifo_txd *)*txdlh;
		struct __vxge_hw_fifo_txdl_priv *priv;

		priv = __vxge_hw_fifo_txdl_priv(fifo, txdp);

		/* reset the TxDL's private */
		priv->align_dma_offset = 0;
		priv->align_vaddr_start = priv->align_vaddr;
		priv->align_used_frags = 0;
		priv->frags = 0;
		priv->alloc_frags = fifo->config->max_frags;
		priv->next_txdl_priv = NULL;

		*txdl_priv = (void *)(size_t)txdp->host_control;

		for (i = 0; i < fifo->config->max_frags; i++) {
			txdp = ((struct vxge_hw_fifo_txd *)*txdlh) + i;
			txdp->control_0 = txdp->control_1 = 0;
		}
	}

	return status;
}

/**
 * vxge_hw_fifo_txdl_buffer_set - Set transmit buffer pointer in the
 * descriptor.
 * @fifo: Handle to the fifo object used for non offload send
 * @txdlh: Descriptor handle.
 * @frag_idx: Index of the data buffer in the caller's scatter-gather list
 *            (of buffers).
 * @dma_pointer: DMA address of the data buffer referenced by @frag_idx.
 * @size: Size of the data buffer (in bytes).
 *
 * This API is part of the preparation of the transmit descriptor for posting
 * (via vxge_hw_fifo_txdl_post()). The related "preparation" APIs include
 * vxge_hw_fifo_txdl_mss_set() and vxge_hw_fifo_txdl_cksum_set_bits().
 * All three APIs fill in the fields of the fifo descriptor,
 * in accordance with the Titan specification.
 *
 */
void vxge_hw_fifo_txdl_buffer_set(struct __vxge_hw_fifo *fifo,
				  void *txdlh, u32 frag_idx,
				  dma_addr_t dma_pointer, u32 size)
{
	struct __vxge_hw_fifo_txdl_priv *txdl_priv;
	struct vxge_hw_fifo_txd *txdp, *txdp_last;
	struct __vxge_hw_channel *channel;

	channel = &fifo->channel;

	txdl_priv = __vxge_hw_fifo_txdl_priv(fifo, txdlh);
	txdp = (struct vxge_hw_fifo_txd *)txdlh  +  txdl_priv->frags;

	if (frag_idx != 0)
		txdp->control_0 = txdp->control_1 = 0;
	else {
		txdp->control_0 |= VXGE_HW_FIFO_TXD_GATHER_CODE(
			VXGE_HW_FIFO_TXD_GATHER_CODE_FIRST);
		txdp->control_1 |= fifo->interrupt_type;
		txdp->control_1 |= VXGE_HW_FIFO_TXD_INT_NUMBER(
			fifo->tx_intr_num);
		if (txdl_priv->frags) {
			txdp_last = (struct vxge_hw_fifo_txd *)txdlh  +
			(txdl_priv->frags - 1);
			txdp_last->control_0 |= VXGE_HW_FIFO_TXD_GATHER_CODE(
				VXGE_HW_FIFO_TXD_GATHER_CODE_LAST);
		}
	}

	vxge_assert(frag_idx < txdl_priv->alloc_frags);

	txdp->buffer_pointer = (u64)dma_pointer;
	txdp->control_0 |= VXGE_HW_FIFO_TXD_BUFFER_SIZE(size);
	fifo->stats->total_buffers++;
	txdl_priv->frags++;
}

/**
 * vxge_hw_fifo_txdl_post - Post descriptor on the fifo channel.
 * @fifo: Handle to the fifo object used for non offload send
 * @txdlh: Descriptor obtained via vxge_hw_fifo_txdl_reserve()
 * @frags: Number of contiguous buffers that are part of a single
 *         transmit operation.
 *
 * Post descriptor on the 'fifo' type channel for transmission.
 * Prior to posting the descriptor should be filled in accordance with
 * Host/Titan interface specification for a given service (LL, etc.).
 *
 */
void vxge_hw_fifo_txdl_post(struct __vxge_hw_fifo *fifo, void *txdlh)
{
	struct __vxge_hw_fifo_txdl_priv *txdl_priv;
	struct vxge_hw_fifo_txd *txdp_last;
	struct vxge_hw_fifo_txd *txdp_first;
	struct __vxge_hw_channel *channel;

	channel = &fifo->channel;

	txdl_priv = __vxge_hw_fifo_txdl_priv(fifo, txdlh);
	txdp_first = (struct vxge_hw_fifo_txd *)txdlh;

	txdp_last = (struct vxge_hw_fifo_txd *)txdlh  +  (txdl_priv->frags - 1);
	txdp_last->control_0 |=
	      VXGE_HW_FIFO_TXD_GATHER_CODE(VXGE_HW_FIFO_TXD_GATHER_CODE_LAST);
	txdp_first->control_0 |= VXGE_HW_FIFO_TXD_LIST_OWN_ADAPTER;

	vxge_hw_channel_dtr_post(&fifo->channel, txdlh);

	__vxge_hw_non_offload_db_post(fifo,
		(u64)txdl_priv->dma_addr,
		txdl_priv->frags - 1,
		fifo->no_snoop_bits);

	fifo->stats->total_posts++;
	fifo->stats->common_stats.usage_cnt++;
	if (fifo->stats->common_stats.usage_max <
		fifo->stats->common_stats.usage_cnt)
		fifo->stats->common_stats.usage_max =
			fifo->stats->common_stats.usage_cnt;
}

/**
 * vxge_hw_fifo_txdl_next_completed - Retrieve next completed descriptor.
 * @fifo: Handle to the fifo object used for non offload send
 * @txdlh: Descriptor handle. Returned by HW.
 * @t_code: Transfer code, as per Titan User Guide,
 *          Transmit Descriptor Format.
 *          Returned by HW.
 *
 * Retrieve the _next_ completed descriptor.
 * HW uses channel callback (*vxge_hw_channel_callback_f) to notifiy
 * driver of new completed descriptors. After that
 * the driver can use vxge_hw_fifo_txdl_next_completed to retrieve the rest
 * completions (the very first completion is passed by HW via
 * vxge_hw_channel_callback_f).
 *
 * Implementation-wise, the driver is free to call
 * vxge_hw_fifo_txdl_next_completed either immediately from inside the
 * channel callback, or in a deferred fashion and separate (from HW)
 * context.
 *
 * Non-zero @t_code means failure to process the descriptor.
 * The failure could happen, for instance, when the link is
 * down, in which case Titan completes the descriptor because it
 * is not able to send the data out.
 *
 * For details please refer to Titan User Guide.
 *
 * Returns: VXGE_HW_OK - success.
 * VXGE_HW_INF_NO_MORE_COMPLETED_DESCRIPTORS - No completed descriptors
 * are currently available for processing.
 *
 */
enum vxge_hw_status vxge_hw_fifo_txdl_next_completed(
	struct __vxge_hw_fifo *fifo, void **txdlh,
	enum vxge_hw_fifo_tcode *t_code)
{
	struct __vxge_hw_channel *channel;
	struct vxge_hw_fifo_txd *txdp;
	enum vxge_hw_status status = VXGE_HW_OK;

	channel = &fifo->channel;

	vxge_hw_channel_dtr_try_complete(channel, txdlh);

	txdp = (struct vxge_hw_fifo_txd *)*txdlh;
	if (txdp == NULL) {
		status = VXGE_HW_INF_NO_MORE_COMPLETED_DESCRIPTORS;
		goto exit;
	}

	/* check whether host owns it */
	if (!(txdp->control_0 & VXGE_HW_FIFO_TXD_LIST_OWN_ADAPTER)) {

		vxge_assert(txdp->host_control != 0);

		vxge_hw_channel_dtr_complete(channel);

		*t_code = (u8)VXGE_HW_FIFO_TXD_T_CODE_GET(txdp->control_0);

		if (fifo->stats->common_stats.usage_cnt > 0)
			fifo->stats->common_stats.usage_cnt--;

		status = VXGE_HW_OK;
		goto exit;
	}

	/* no more completions */
	*txdlh = NULL;
	status = VXGE_HW_INF_NO_MORE_COMPLETED_DESCRIPTORS;
exit:
	return status;
}

/**
 * vxge_hw_fifo_handle_tcode - Handle transfer code.
 * @fifo: Handle to the fifo object used for non offload send
 * @txdlh: Descriptor handle.
 * @t_code: One of the enumerated (and documented in the Titan user guide)
 *          "transfer codes".
 *
 * Handle descriptor's transfer code. The latter comes with each completed
 * descriptor.
 *
 * Returns: one of the enum vxge_hw_status{} enumerated types.
 * VXGE_HW_OK - for success.
 * VXGE_HW_ERR_CRITICAL - when encounters critical error.
 */
enum vxge_hw_status vxge_hw_fifo_handle_tcode(struct __vxge_hw_fifo *fifo,
					      void *txdlh,
					      enum vxge_hw_fifo_tcode t_code)
{
	struct __vxge_hw_channel *channel;

	enum vxge_hw_status status = VXGE_HW_OK;
	channel = &fifo->channel;

	if (((t_code & 0x7) < 0) || ((t_code & 0x7) > 0x4)) {
		status = VXGE_HW_ERR_INVALID_TCODE;
		goto exit;
	}

	fifo->stats->txd_t_code_err_cnt[t_code]++;
exit:
	return status;
}

/**
 * vxge_hw_fifo_txdl_free - Free descriptor.
 * @fifo: Handle to the fifo object used for non offload send
 * @txdlh: Descriptor handle.
 *
 * Free the reserved descriptor. This operation is "symmetrical" to
 * vxge_hw_fifo_txdl_reserve. The "free-ing" completes the descriptor's
 * lifecycle.
 *
 * After free-ing (see vxge_hw_fifo_txdl_free()) the descriptor again can
 * be:
 *
 * - reserved (vxge_hw_fifo_txdl_reserve);
 *
 * - posted (vxge_hw_fifo_txdl_post);
 *
 * - completed (vxge_hw_fifo_txdl_next_completed);
 *
 * - and recycled again (vxge_hw_fifo_txdl_free).
 *
 * For alternative state transitions and more details please refer to
 * the design doc.
 *
 */
void vxge_hw_fifo_txdl_free(struct __vxge_hw_fifo *fifo, void *txdlh)
{
	struct __vxge_hw_fifo_txdl_priv *txdl_priv;
	u32 max_frags;
	struct __vxge_hw_channel *channel;

	channel = &fifo->channel;

	txdl_priv = __vxge_hw_fifo_txdl_priv(fifo,
			(struct vxge_hw_fifo_txd *)txdlh);

	max_frags = fifo->config->max_frags;

	vxge_hw_channel_dtr_free(channel, txdlh);
}

/**
 * vxge_hw_vpath_mac_addr_add - Add the mac address entry for this vpath
 *               to MAC address table.
 * @vp: Vpath handle.
 * @macaddr: MAC address to be added for this vpath into the list
 * @macaddr_mask: MAC address mask for macaddr
 * @duplicate_mode: Duplicate MAC address add mode. Please see
 *             enum vxge_hw_vpath_mac_addr_add_mode{}
 *
 * Adds the given mac address and mac address mask into the list for this
 * vpath.
 * see also: vxge_hw_vpath_mac_addr_delete, vxge_hw_vpath_mac_addr_get and
 * vxge_hw_vpath_mac_addr_get_next
 *
 */
enum vxge_hw_status
vxge_hw_vpath_mac_addr_add(
	struct __vxge_hw_vpath_handle *vp,
	u8 (macaddr)[ETH_ALEN],
	u8 (macaddr_mask)[ETH_ALEN],
	enum vxge_hw_vpath_mac_addr_add_mode duplicate_mode)
{
	u32 i;
	u64 data1 = 0ULL;
	u64 data2 = 0ULL;
	enum vxge_hw_status status = VXGE_HW_OK;

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	for (i = 0; i < ETH_ALEN; i++) {
		data1 <<= 8;
		data1 |= (u8)macaddr[i];

		data2 <<= 8;
		data2 |= (u8)macaddr_mask[i];
	}

	switch (duplicate_mode) {
	case VXGE_HW_VPATH_MAC_ADDR_ADD_DUPLICATE:
		i = 0;
		break;
	case VXGE_HW_VPATH_MAC_ADDR_DISCARD_DUPLICATE:
		i = 1;
		break;
	case VXGE_HW_VPATH_MAC_ADDR_REPLACE_DUPLICATE:
		i = 2;
		break;
	default:
		i = 0;
		break;
	}

	status = __vxge_hw_vpath_rts_table_set(vp,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_ADD_ENTRY,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_DA,
			0,
			VXGE_HW_RTS_ACCESS_STEER_DATA0_DA_MAC_ADDR(data1),
			VXGE_HW_RTS_ACCESS_STEER_DATA1_DA_MAC_ADDR_MASK(data2)|
			VXGE_HW_RTS_ACCESS_STEER_DATA1_DA_MAC_ADDR_MODE(i));
exit:
	return status;
}

/**
 * vxge_hw_vpath_mac_addr_get - Get the first mac address entry for this vpath
 *               from MAC address table.
 * @vp: Vpath handle.
 * @macaddr: First MAC address entry for this vpath in the list
 * @macaddr_mask: MAC address mask for macaddr
 *
 * Returns the first mac address and mac address mask in the list for this
 * vpath.
 * see also: vxge_hw_vpath_mac_addr_get_next
 *
 */
enum vxge_hw_status
vxge_hw_vpath_mac_addr_get(
	struct __vxge_hw_vpath_handle *vp,
	u8 (macaddr)[ETH_ALEN],
	u8 (macaddr_mask)[ETH_ALEN])
{
	u32 i;
	u64 data1 = 0ULL;
	u64 data2 = 0ULL;
	enum vxge_hw_status status = VXGE_HW_OK;

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	status = __vxge_hw_vpath_rts_table_get(vp,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_LIST_FIRST_ENTRY,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_DA,
			0, &data1, &data2);

	if (status != VXGE_HW_OK)
		goto exit;

	data1 = VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_DA_MAC_ADDR(data1);

	data2 = VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_DA_MAC_ADDR_MASK(data2);

	for (i = ETH_ALEN; i > 0; i--) {
		macaddr[i-1] = (u8)(data1 & 0xFF);
		data1 >>= 8;

		macaddr_mask[i-1] = (u8)(data2 & 0xFF);
		data2 >>= 8;
	}
exit:
	return status;
}

/**
 * vxge_hw_vpath_mac_addr_get_next - Get the next mac address entry for this
 * vpath
 *               from MAC address table.
 * @vp: Vpath handle.
 * @macaddr: Next MAC address entry for this vpath in the list
 * @macaddr_mask: MAC address mask for macaddr
 *
 * Returns the next mac address and mac address mask in the list for this
 * vpath.
 * see also: vxge_hw_vpath_mac_addr_get
 *
 */
enum vxge_hw_status
vxge_hw_vpath_mac_addr_get_next(
	struct __vxge_hw_vpath_handle *vp,
	u8 (macaddr)[ETH_ALEN],
	u8 (macaddr_mask)[ETH_ALEN])
{
	u32 i;
	u64 data1 = 0ULL;
	u64 data2 = 0ULL;
	enum vxge_hw_status status = VXGE_HW_OK;

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	status = __vxge_hw_vpath_rts_table_get(vp,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_LIST_NEXT_ENTRY,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_DA,
			0, &data1, &data2);

	if (status != VXGE_HW_OK)
		goto exit;

	data1 = VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_DA_MAC_ADDR(data1);

	data2 = VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_DA_MAC_ADDR_MASK(data2);

	for (i = ETH_ALEN; i > 0; i--) {
		macaddr[i-1] = (u8)(data1 & 0xFF);
		data1 >>= 8;

		macaddr_mask[i-1] = (u8)(data2 & 0xFF);
		data2 >>= 8;
	}

exit:
	return status;
}

/**
 * vxge_hw_vpath_mac_addr_delete - Delete the mac address entry for this vpath
 *               to MAC address table.
 * @vp: Vpath handle.
 * @macaddr: MAC address to be added for this vpath into the list
 * @macaddr_mask: MAC address mask for macaddr
 *
 * Delete the given mac address and mac address mask into the list for this
 * vpath.
 * see also: vxge_hw_vpath_mac_addr_add, vxge_hw_vpath_mac_addr_get and
 * vxge_hw_vpath_mac_addr_get_next
 *
 */
enum vxge_hw_status
vxge_hw_vpath_mac_addr_delete(
	struct __vxge_hw_vpath_handle *vp,
	u8 (macaddr)[ETH_ALEN],
	u8 (macaddr_mask)[ETH_ALEN])
{
	u32 i;
	u64 data1 = 0ULL;
	u64 data2 = 0ULL;
	enum vxge_hw_status status = VXGE_HW_OK;

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	for (i = 0; i < ETH_ALEN; i++) {
		data1 <<= 8;
		data1 |= (u8)macaddr[i];

		data2 <<= 8;
		data2 |= (u8)macaddr_mask[i];
	}

	status = __vxge_hw_vpath_rts_table_set(vp,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_DELETE_ENTRY,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_DA,
			0,
			VXGE_HW_RTS_ACCESS_STEER_DATA0_DA_MAC_ADDR(data1),
			VXGE_HW_RTS_ACCESS_STEER_DATA1_DA_MAC_ADDR_MASK(data2));
exit:
	return status;
}

/**
 * vxge_hw_vpath_vid_add - Add the vlan id entry for this vpath
 *               to vlan id table.
 * @vp: Vpath handle.
 * @vid: vlan id to be added for this vpath into the list
 *
 * Adds the given vlan id into the list for this  vpath.
 * see also: vxge_hw_vpath_vid_delete, vxge_hw_vpath_vid_get and
 * vxge_hw_vpath_vid_get_next
 *
 */
enum vxge_hw_status
vxge_hw_vpath_vid_add(struct __vxge_hw_vpath_handle *vp, u64 vid)
{
	enum vxge_hw_status status = VXGE_HW_OK;

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	status = __vxge_hw_vpath_rts_table_set(vp,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_ADD_ENTRY,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_VID,
			0, VXGE_HW_RTS_ACCESS_STEER_DATA0_VLAN_ID(vid), 0);
exit:
	return status;
}

/**
 * vxge_hw_vpath_vid_get - Get the first vid entry for this vpath
 *               from vlan id table.
 * @vp: Vpath handle.
 * @vid: Buffer to return vlan id
 *
 * Returns the first vlan id in the list for this vpath.
 * see also: vxge_hw_vpath_vid_get_next
 *
 */
enum vxge_hw_status
vxge_hw_vpath_vid_get(struct __vxge_hw_vpath_handle *vp, u64 *vid)
{
	u64 data;
	enum vxge_hw_status status = VXGE_HW_OK;

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	status = __vxge_hw_vpath_rts_table_get(vp,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_LIST_FIRST_ENTRY,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_VID,
			0, vid, &data);

	*vid = VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_VLAN_ID(*vid);
exit:
	return status;
}

/**
 * vxge_hw_vpath_vid_delete - Delete the vlan id entry for this vpath
 *               to vlan id table.
 * @vp: Vpath handle.
 * @vid: vlan id to be added for this vpath into the list
 *
 * Adds the given vlan id into the list for this  vpath.
 * see also: vxge_hw_vpath_vid_add, vxge_hw_vpath_vid_get and
 * vxge_hw_vpath_vid_get_next
 *
 */
enum vxge_hw_status
vxge_hw_vpath_vid_delete(struct __vxge_hw_vpath_handle *vp, u64 vid)
{
	enum vxge_hw_status status = VXGE_HW_OK;

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	status = __vxge_hw_vpath_rts_table_set(vp,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_DELETE_ENTRY,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_VID,
			0, VXGE_HW_RTS_ACCESS_STEER_DATA0_VLAN_ID(vid), 0);
exit:
	return status;
}

/**
 * vxge_hw_vpath_promisc_enable - Enable promiscuous mode.
 * @vp: Vpath handle.
 *
 * Enable promiscuous mode of Titan-e operation.
 *
 * See also: vxge_hw_vpath_promisc_disable().
 */
enum vxge_hw_status vxge_hw_vpath_promisc_enable(
			struct __vxge_hw_vpath_handle *vp)
{
	u64 val64;
	struct __vxge_hw_virtualpath *vpath;
	enum vxge_hw_status status = VXGE_HW_OK;

	if ((vp == NULL) || (vp->vpath->ringh == NULL)) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	vpath = vp->vpath;

	/* Enable promiscous mode for function 0 only */
	if (!(vpath->hldev->access_rights &
		VXGE_HW_DEVICE_ACCESS_RIGHT_MRPCIM))
		return VXGE_HW_OK;

	val64 = readq(&vpath->vp_reg->rxmac_vcfg0);

	if (!(val64 & VXGE_HW_RXMAC_VCFG0_UCAST_ALL_ADDR_EN)) {

		val64 |= VXGE_HW_RXMAC_VCFG0_UCAST_ALL_ADDR_EN |
			 VXGE_HW_RXMAC_VCFG0_MCAST_ALL_ADDR_EN |
			 VXGE_HW_RXMAC_VCFG0_BCAST_EN |
			 VXGE_HW_RXMAC_VCFG0_ALL_VID_EN;

		writeq(val64, &vpath->vp_reg->rxmac_vcfg0);
	}
exit:
	return status;
}

/**
 * vxge_hw_vpath_promisc_disable - Disable promiscuous mode.
 * @vp: Vpath handle.
 *
 * Disable promiscuous mode of Titan-e operation.
 *
 * See also: vxge_hw_vpath_promisc_enable().
 */
enum vxge_hw_status vxge_hw_vpath_promisc_disable(
			struct __vxge_hw_vpath_handle *vp)
{
	u64 val64;
	struct __vxge_hw_virtualpath *vpath;
	enum vxge_hw_status status = VXGE_HW_OK;

	if ((vp == NULL) || (vp->vpath->ringh == NULL)) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	vpath = vp->vpath;

	val64 = readq(&vpath->vp_reg->rxmac_vcfg0);

	if (val64 & VXGE_HW_RXMAC_VCFG0_UCAST_ALL_ADDR_EN) {

		val64 &= ~(VXGE_HW_RXMAC_VCFG0_UCAST_ALL_ADDR_EN |
			   VXGE_HW_RXMAC_VCFG0_MCAST_ALL_ADDR_EN |
			   VXGE_HW_RXMAC_VCFG0_ALL_VID_EN);

		writeq(val64, &vpath->vp_reg->rxmac_vcfg0);
	}
exit:
	return status;
}

/*
 * vxge_hw_vpath_bcast_enable - Enable broadcast
 * @vp: Vpath handle.
 *
 * Enable receiving broadcasts.
 */
enum vxge_hw_status vxge_hw_vpath_bcast_enable(
			struct __vxge_hw_vpath_handle *vp)
{
	u64 val64;
	struct __vxge_hw_virtualpath *vpath;
	enum vxge_hw_status status = VXGE_HW_OK;

	if ((vp == NULL) || (vp->vpath->ringh == NULL)) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	vpath = vp->vpath;

	val64 = readq(&vpath->vp_reg->rxmac_vcfg0);

	if (!(val64 & VXGE_HW_RXMAC_VCFG0_BCAST_EN)) {
		val64 |= VXGE_HW_RXMAC_VCFG0_BCAST_EN;
		writeq(val64, &vpath->vp_reg->rxmac_vcfg0);
	}
exit:
	return status;
}

/**
 * vxge_hw_vpath_mcast_enable - Enable multicast addresses.
 * @vp: Vpath handle.
 *
 * Enable Titan-e multicast addresses.
 * Returns: VXGE_HW_OK on success.
 *
 */
enum vxge_hw_status vxge_hw_vpath_mcast_enable(
			struct __vxge_hw_vpath_handle *vp)
{
	u64 val64;
	struct __vxge_hw_virtualpath *vpath;
	enum vxge_hw_status status = VXGE_HW_OK;

	if ((vp == NULL) || (vp->vpath->ringh == NULL)) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	vpath = vp->vpath;

	val64 = readq(&vpath->vp_reg->rxmac_vcfg0);

	if (!(val64 & VXGE_HW_RXMAC_VCFG0_MCAST_ALL_ADDR_EN)) {
		val64 |= VXGE_HW_RXMAC_VCFG0_MCAST_ALL_ADDR_EN;
		writeq(val64, &vpath->vp_reg->rxmac_vcfg0);
	}
exit:
	return status;
}

/**
 * vxge_hw_vpath_mcast_disable - Disable  multicast addresses.
 * @vp: Vpath handle.
 *
 * Disable Titan-e multicast addresses.
 * Returns: VXGE_HW_OK - success.
 * VXGE_HW_ERR_INVALID_HANDLE - Invalid handle
 *
 */
enum vxge_hw_status
vxge_hw_vpath_mcast_disable(struct __vxge_hw_vpath_handle *vp)
{
	u64 val64;
	struct __vxge_hw_virtualpath *vpath;
	enum vxge_hw_status status = VXGE_HW_OK;

	if ((vp == NULL) || (vp->vpath->ringh == NULL)) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	vpath = vp->vpath;

	val64 = readq(&vpath->vp_reg->rxmac_vcfg0);

	if (val64 & VXGE_HW_RXMAC_VCFG0_MCAST_ALL_ADDR_EN) {
		val64 &= ~VXGE_HW_RXMAC_VCFG0_MCAST_ALL_ADDR_EN;
		writeq(val64, &vpath->vp_reg->rxmac_vcfg0);
	}
exit:
	return status;
}

/*
 * __vxge_hw_vpath_alarm_process - Process Alarms.
 * @vpath: Virtual Path.
 * @skip_alarms: Do not clear the alarms
 *
 * Process vpath alarms.
 *
 */
static enum vxge_hw_status
__vxge_hw_vpath_alarm_process(struct __vxge_hw_virtualpath *vpath,
			      u32 skip_alarms)
{
	u64 val64;
	u64 alarm_status;
	u64 pic_status;
	struct __vxge_hw_device *hldev = NULL;
	enum vxge_hw_event alarm_event = VXGE_HW_EVENT_UNKNOWN;
	u64 mask64;
	struct vxge_hw_vpath_stats_sw_info *sw_stats;
	struct vxge_hw_vpath_reg __iomem *vp_reg;

	if (vpath == NULL) {
		alarm_event = VXGE_HW_SET_LEVEL(VXGE_HW_EVENT_UNKNOWN,
			alarm_event);
		goto out2;
	}

	hldev = vpath->hldev;
	vp_reg = vpath->vp_reg;
	alarm_status = readq(&vp_reg->vpath_general_int_status);

	if (alarm_status == VXGE_HW_ALL_FOXES) {
		alarm_event = VXGE_HW_SET_LEVEL(VXGE_HW_EVENT_SLOT_FREEZE,
			alarm_event);
		goto out;
	}

	sw_stats = vpath->sw_stats;

	if (alarm_status & ~(
		VXGE_HW_VPATH_GENERAL_INT_STATUS_PIC_INT |
		VXGE_HW_VPATH_GENERAL_INT_STATUS_PCI_INT |
		VXGE_HW_VPATH_GENERAL_INT_STATUS_WRDMA_INT |
		VXGE_HW_VPATH_GENERAL_INT_STATUS_XMAC_INT)) {
		sw_stats->error_stats.unknown_alarms++;

		alarm_event = VXGE_HW_SET_LEVEL(VXGE_HW_EVENT_UNKNOWN,
			alarm_event);
		goto out;
	}

	if (alarm_status & VXGE_HW_VPATH_GENERAL_INT_STATUS_XMAC_INT) {

		val64 = readq(&vp_reg->xgmac_vp_int_status);

		if (val64 &
		VXGE_HW_XGMAC_VP_INT_STATUS_ASIC_NTWK_VP_ERR_ASIC_NTWK_VP_INT) {

			val64 = readq(&vp_reg->asic_ntwk_vp_err_reg);

			if (((val64 &
			      VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_FLT) &&
			     (!(val64 &
				VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_OK))) ||
			    ((val64 &
			      VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_FLT_OCCURR) &&
			     (!(val64 &
				VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_OK_OCCURR)
				     ))) {
				sw_stats->error_stats.network_sustained_fault++;

				writeq(
				VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_FLT,
					&vp_reg->asic_ntwk_vp_err_mask);

				__vxge_hw_device_handle_link_down_ind(hldev);
				alarm_event = VXGE_HW_SET_LEVEL(
					VXGE_HW_EVENT_LINK_DOWN, alarm_event);
			}

			if (((val64 &
			      VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_OK) &&
			     (!(val64 &
				VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_FLT))) ||
			    ((val64 &
			      VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_OK_OCCURR) &&
			     (!(val64 &
				VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_FLT_OCCURR)
				     ))) {

				sw_stats->error_stats.network_sustained_ok++;

				writeq(
				VXGE_HW_ASIC_NW_VP_ERR_REG_XMACJ_STN_OK,
					&vp_reg->asic_ntwk_vp_err_mask);

				__vxge_hw_device_handle_link_up_ind(hldev);
				alarm_event = VXGE_HW_SET_LEVEL(
					VXGE_HW_EVENT_LINK_UP, alarm_event);
			}

			writeq(VXGE_HW_INTR_MASK_ALL,
				&vp_reg->asic_ntwk_vp_err_reg);

			alarm_event = VXGE_HW_SET_LEVEL(
				VXGE_HW_EVENT_ALARM_CLEARED, alarm_event);

			if (skip_alarms)
				return VXGE_HW_OK;
		}
	}

	if (alarm_status & VXGE_HW_VPATH_GENERAL_INT_STATUS_PIC_INT) {

		pic_status = readq(&vp_reg->vpath_ppif_int_status);

		if (pic_status &
		    VXGE_HW_VPATH_PPIF_INT_STATUS_GENERAL_ERRORS_GENERAL_INT) {

			val64 = readq(&vp_reg->general_errors_reg);
			mask64 = readq(&vp_reg->general_errors_mask);

			if ((val64 &
				VXGE_HW_GENERAL_ERRORS_REG_INI_SERR_DET) &
				~mask64) {
				sw_stats->error_stats.ini_serr_det++;

				alarm_event = VXGE_HW_SET_LEVEL(
					VXGE_HW_EVENT_SERR, alarm_event);
			}

			if ((val64 &
			    VXGE_HW_GENERAL_ERRORS_REG_DBLGEN_FIFO0_OVRFLOW) &
				~mask64) {
				sw_stats->error_stats.dblgen_fifo0_overflow++;

				alarm_event = VXGE_HW_SET_LEVEL(
					VXGE_HW_EVENT_FIFO_ERR, alarm_event);
			}

			if ((val64 &
			    VXGE_HW_GENERAL_ERRORS_REG_STATSB_PIF_CHAIN_ERR) &
				~mask64)
				sw_stats->error_stats.statsb_pif_chain_error++;

			if ((val64 &
			   VXGE_HW_GENERAL_ERRORS_REG_STATSB_DROP_TIMEOUT_REQ) &
				~mask64)
				sw_stats->error_stats.statsb_drop_timeout++;

			if ((val64 &
				VXGE_HW_GENERAL_ERRORS_REG_TGT_ILLEGAL_ACCESS) &
				~mask64)
				sw_stats->error_stats.target_illegal_access++;

			if (!skip_alarms) {
				writeq(VXGE_HW_INTR_MASK_ALL,
					&vp_reg->general_errors_reg);
				alarm_event = VXGE_HW_SET_LEVEL(
					VXGE_HW_EVENT_ALARM_CLEARED,
					alarm_event);
			}
		}

		if (pic_status &
		    VXGE_HW_VPATH_PPIF_INT_STATUS_KDFCCTL_ERRORS_KDFCCTL_INT) {

			val64 = readq(&vp_reg->kdfcctl_errors_reg);
			mask64 = readq(&vp_reg->kdfcctl_errors_mask);

			if ((val64 &
			    VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO0_OVRWR) &
				~mask64) {
				sw_stats->error_stats.kdfcctl_fifo0_overwrite++;

				alarm_event = VXGE_HW_SET_LEVEL(
					VXGE_HW_EVENT_FIFO_ERR,
					alarm_event);
			}

			if ((val64 &
			    VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO0_POISON) &
				~mask64) {
				sw_stats->error_stats.kdfcctl_fifo0_poison++;

				alarm_event = VXGE_HW_SET_LEVEL(
					VXGE_HW_EVENT_FIFO_ERR,
					alarm_event);
			}

			if ((val64 &
			    VXGE_HW_KDFCCTL_ERRORS_REG_KDFCCTL_FIFO0_DMA_ERR) &
				~mask64) {
				sw_stats->error_stats.kdfcctl_fifo0_dma_error++;

				alarm_event = VXGE_HW_SET_LEVEL(
					VXGE_HW_EVENT_FIFO_ERR,
					alarm_event);
			}

			if (!skip_alarms) {
				writeq(VXGE_HW_INTR_MASK_ALL,
					&vp_reg->kdfcctl_errors_reg);
				alarm_event = VXGE_HW_SET_LEVEL(
					VXGE_HW_EVENT_ALARM_CLEARED,
					alarm_event);
			}
		}

	}

	if (alarm_status & VXGE_HW_VPATH_GENERAL_INT_STATUS_WRDMA_INT) {

		val64 = readq(&vp_reg->wrdma_alarm_status);

		if (val64 & VXGE_HW_WRDMA_ALARM_STATUS_PRC_ALARM_PRC_INT) {

			val64 = readq(&vp_reg->prc_alarm_reg);
			mask64 = readq(&vp_reg->prc_alarm_mask);

			if ((val64 & VXGE_HW_PRC_ALARM_REG_PRC_RING_BUMP)&
				~mask64)
				sw_stats->error_stats.prc_ring_bumps++;

			if ((val64 & VXGE_HW_PRC_ALARM_REG_PRC_RXDCM_SC_ERR) &
				~mask64) {
				sw_stats->error_stats.prc_rxdcm_sc_err++;

				alarm_event = VXGE_HW_SET_LEVEL(
					VXGE_HW_EVENT_VPATH_ERR,
					alarm_event);
			}

			if ((val64 & VXGE_HW_PRC_ALARM_REG_PRC_RXDCM_SC_ABORT)
				& ~mask64) {
				sw_stats->error_stats.prc_rxdcm_sc_abort++;

				alarm_event = VXGE_HW_SET_LEVEL(
						VXGE_HW_EVENT_VPATH_ERR,
						alarm_event);
			}

			if ((val64 & VXGE_HW_PRC_ALARM_REG_PRC_QUANTA_SIZE_ERR)
				 & ~mask64) {
				sw_stats->error_stats.prc_quanta_size_err++;

				alarm_event = VXGE_HW_SET_LEVEL(
					VXGE_HW_EVENT_VPATH_ERR,
					alarm_event);
			}

			if (!skip_alarms) {
				writeq(VXGE_HW_INTR_MASK_ALL,
					&vp_reg->prc_alarm_reg);
				alarm_event = VXGE_HW_SET_LEVEL(
						VXGE_HW_EVENT_ALARM_CLEARED,
						alarm_event);
			}
		}
	}
out:
	hldev->stats.sw_dev_err_stats.vpath_alarms++;
out2:
	if ((alarm_event == VXGE_HW_EVENT_ALARM_CLEARED) ||
		(alarm_event == VXGE_HW_EVENT_UNKNOWN))
		return VXGE_HW_OK;

	__vxge_hw_device_handle_error(hldev, vpath->vp_id, alarm_event);

	if (alarm_event == VXGE_HW_EVENT_SERR)
		return VXGE_HW_ERR_CRITICAL;

	return (alarm_event == VXGE_HW_EVENT_SLOT_FREEZE) ?
		VXGE_HW_ERR_SLOT_FREEZE :
		(alarm_event == VXGE_HW_EVENT_FIFO_ERR) ? VXGE_HW_ERR_FIFO :
		VXGE_HW_ERR_VPATH;
}

/*
 * vxge_hw_vpath_alarm_process - Process Alarms.
 * @vpath: Virtual Path.
 * @skip_alarms: Do not clear the alarms
 *
 * Process vpath alarms.
 *
 */
enum vxge_hw_status vxge_hw_vpath_alarm_process(
			struct __vxge_hw_vpath_handle *vp,
			u32 skip_alarms)
{
	enum vxge_hw_status status = VXGE_HW_OK;

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	status = __vxge_hw_vpath_alarm_process(vp->vpath, skip_alarms);
exit:
	return status;
}

/**
 * vxge_hw_vpath_msix_set - Associate MSIX vectors with TIM interrupts and
 *                            alrms
 * @vp: Virtual Path handle.
 * @tim_msix_id: MSIX vectors associated with VXGE_HW_MAX_INTR_PER_VP number of
 *             interrupts(Can be repeated). If fifo or ring are not enabled
 *             the MSIX vector for that should be set to 0
 * @alarm_msix_id: MSIX vector for alarm.
 *
 * This API will associate a given MSIX vector numbers with the four TIM
 * interrupts and alarm interrupt.
 */
void
vxge_hw_vpath_msix_set(struct __vxge_hw_vpath_handle *vp, int *tim_msix_id,
		       int alarm_msix_id)
{
	u64 val64;
	struct __vxge_hw_virtualpath *vpath = vp->vpath;
	struct vxge_hw_vpath_reg __iomem *vp_reg = vpath->vp_reg;
	u32 vp_id = vp->vpath->vp_id;

	val64 =  VXGE_HW_INTERRUPT_CFG0_GROUP0_MSIX_FOR_TXTI(
		  (vp_id * 4) + tim_msix_id[0]) |
		 VXGE_HW_INTERRUPT_CFG0_GROUP1_MSIX_FOR_TXTI(
		  (vp_id * 4) + tim_msix_id[1]);

	writeq(val64, &vp_reg->interrupt_cfg0);

	writeq(VXGE_HW_INTERRUPT_CFG2_ALARM_MAP_TO_MSG(
			(vpath->hldev->first_vp_id * 4) + alarm_msix_id),
			&vp_reg->interrupt_cfg2);

	if (vpath->hldev->config.intr_mode ==
					VXGE_HW_INTR_MODE_MSIX_ONE_SHOT) {
		__vxge_hw_pio_mem_write32_upper((u32)vxge_bVALn(
				VXGE_HW_ONE_SHOT_VECT1_EN_ONE_SHOT_VECT1_EN,
				0, 32), &vp_reg->one_shot_vect1_en);
	}

	if (vpath->hldev->config.intr_mode ==
		VXGE_HW_INTR_MODE_MSIX_ONE_SHOT) {
		__vxge_hw_pio_mem_write32_upper((u32)vxge_bVALn(
				VXGE_HW_ONE_SHOT_VECT2_EN_ONE_SHOT_VECT2_EN,
				0, 32), &vp_reg->one_shot_vect2_en);

		__vxge_hw_pio_mem_write32_upper((u32)vxge_bVALn(
				VXGE_HW_ONE_SHOT_VECT3_EN_ONE_SHOT_VECT3_EN,
				0, 32), &vp_reg->one_shot_vect3_en);
	}
}

/**
 * vxge_hw_vpath_msix_mask - Mask MSIX Vector.
 * @vp: Virtual Path handle.
 * @msix_id:  MSIX ID
 *
 * The function masks the msix interrupt for the given msix_id
 *
 * Returns: 0,
 * Otherwise, VXGE_HW_ERR_WRONG_IRQ if the msix index is out of range
 * status.
 * See also:
 */
void
vxge_hw_vpath_msix_mask(struct __vxge_hw_vpath_handle *vp, int msix_id)
{
	struct __vxge_hw_device *hldev = vp->vpath->hldev;
	__vxge_hw_pio_mem_write32_upper(
		(u32) vxge_bVALn(vxge_mBIT(msix_id  >> 2), 0, 32),
		&hldev->common_reg->set_msix_mask_vect[msix_id % 4]);
}

/**
 * vxge_hw_vpath_msix_unmask - Unmask the MSIX Vector.
 * @vp: Virtual Path handle.
 * @msix_id:  MSI ID
 *
 * The function unmasks the msix interrupt for the given msix_id
 *
 * Returns: 0,
 * Otherwise, VXGE_HW_ERR_WRONG_IRQ if the msix index is out of range
 * status.
 * See also:
 */
void
vxge_hw_vpath_msix_unmask(struct __vxge_hw_vpath_handle *vp, int msix_id)
{
	struct __vxge_hw_device *hldev = vp->vpath->hldev;
	__vxge_hw_pio_mem_write32_upper(
			(u32)vxge_bVALn(vxge_mBIT(msix_id >> 2), 0, 32),
			&hldev->common_reg->clear_msix_mask_vect[msix_id%4]);
}

/**
 * vxge_hw_vpath_inta_mask_tx_rx - Mask Tx and Rx interrupts.
 * @vp: Virtual Path handle.
 *
 * Mask Tx and Rx vpath interrupts.
 *
 * See also: vxge_hw_vpath_inta_mask_tx_rx()
 */
void vxge_hw_vpath_inta_mask_tx_rx(struct __vxge_hw_vpath_handle *vp)
{
	u64	tim_int_mask0[4] = {[0 ...3] = 0};
	u32	tim_int_mask1[4] = {[0 ...3] = 0};
	u64	val64;
	struct __vxge_hw_device *hldev = vp->vpath->hldev;

	VXGE_HW_DEVICE_TIM_INT_MASK_SET(tim_int_mask0,
		tim_int_mask1, vp->vpath->vp_id);

	val64 = readq(&hldev->common_reg->tim_int_mask0);

	if ((tim_int_mask0[VXGE_HW_VPATH_INTR_TX] != 0) ||
		(tim_int_mask0[VXGE_HW_VPATH_INTR_RX] != 0)) {
		writeq((tim_int_mask0[VXGE_HW_VPATH_INTR_TX] |
			tim_int_mask0[VXGE_HW_VPATH_INTR_RX] | val64),
			&hldev->common_reg->tim_int_mask0);
	}

	val64 = readl(&hldev->common_reg->tim_int_mask1);

	if ((tim_int_mask1[VXGE_HW_VPATH_INTR_TX] != 0) ||
		(tim_int_mask1[VXGE_HW_VPATH_INTR_RX] != 0)) {
		__vxge_hw_pio_mem_write32_upper(
			(tim_int_mask1[VXGE_HW_VPATH_INTR_TX] |
			tim_int_mask1[VXGE_HW_VPATH_INTR_RX] | val64),
			&hldev->common_reg->tim_int_mask1);
	}
}

/**
 * vxge_hw_vpath_inta_unmask_tx_rx - Unmask Tx and Rx interrupts.
 * @vp: Virtual Path handle.
 *
 * Unmask Tx and Rx vpath interrupts.
 *
 * See also: vxge_hw_vpath_inta_mask_tx_rx()
 */
void vxge_hw_vpath_inta_unmask_tx_rx(struct __vxge_hw_vpath_handle *vp)
{
	u64	tim_int_mask0[4] = {[0 ...3] = 0};
	u32	tim_int_mask1[4] = {[0 ...3] = 0};
	u64	val64;
	struct __vxge_hw_device *hldev = vp->vpath->hldev;

	VXGE_HW_DEVICE_TIM_INT_MASK_SET(tim_int_mask0,
		tim_int_mask1, vp->vpath->vp_id);

	val64 = readq(&hldev->common_reg->tim_int_mask0);

	if ((tim_int_mask0[VXGE_HW_VPATH_INTR_TX] != 0) ||
	   (tim_int_mask0[VXGE_HW_VPATH_INTR_RX] != 0)) {
		writeq((~(tim_int_mask0[VXGE_HW_VPATH_INTR_TX] |
			tim_int_mask0[VXGE_HW_VPATH_INTR_RX])) & val64,
			&hldev->common_reg->tim_int_mask0);
	}

	if ((tim_int_mask1[VXGE_HW_VPATH_INTR_TX] != 0) ||
	   (tim_int_mask1[VXGE_HW_VPATH_INTR_RX] != 0)) {
		__vxge_hw_pio_mem_write32_upper(
			(~(tim_int_mask1[VXGE_HW_VPATH_INTR_TX] |
			  tim_int_mask1[VXGE_HW_VPATH_INTR_RX])) & val64,
			&hldev->common_reg->tim_int_mask1);
	}
}

/**
 * vxge_hw_vpath_poll_rx - Poll Rx Virtual Path for completed
 * descriptors and process the same.
 * @ring: Handle to the ring object used for receive
 *
 * The function	polls the Rx for the completed	descriptors and	calls
 * the driver via supplied completion	callback.
 *
 * Returns: VXGE_HW_OK, if the polling is completed successful.
 * VXGE_HW_COMPLETIONS_REMAIN: There are still more completed
 * descriptors available which are yet to be processed.
 *
 * See also: vxge_hw_vpath_poll_rx()
 */
enum vxge_hw_status vxge_hw_vpath_poll_rx(struct __vxge_hw_ring *ring)
{
	u8 t_code;
	enum vxge_hw_status status = VXGE_HW_OK;
	void *first_rxdh;
	u64 val64 = 0;
	int new_count = 0;

	ring->cmpl_cnt = 0;

	status = vxge_hw_ring_rxd_next_completed(ring, &first_rxdh, &t_code);
	if (status == VXGE_HW_OK)
		ring->callback(ring, first_rxdh,
			t_code, ring->channel.userdata);

	if (ring->cmpl_cnt != 0) {
		ring->doorbell_cnt += ring->cmpl_cnt;
		if (ring->doorbell_cnt >= ring->rxds_limit) {
			/*
			 * Each RxD is of 4 qwords, update the number of
			 * qwords replenished
			 */
			new_count = (ring->doorbell_cnt * 4);

			/* For each block add 4 more qwords */
			ring->total_db_cnt += ring->doorbell_cnt;
			if (ring->total_db_cnt >= ring->rxds_per_block) {
				new_count += 4;
				/* Reset total count */
				ring->total_db_cnt %= ring->rxds_per_block;
			}
			writeq(VXGE_HW_PRC_RXD_DOORBELL_NEW_QW_CNT(new_count),
				&ring->vp_reg->prc_rxd_doorbell);
			val64 =
			  readl(&ring->common_reg->titan_general_int_status);
			ring->doorbell_cnt = 0;
		}
	}

	return status;
}

/**
 * vxge_hw_vpath_poll_tx - Poll Tx for completed descriptors and process
 * the same.
 * @fifo: Handle to the fifo object used for non offload send
 *
 * The function polls the Tx for the completed descriptors and calls
 * the driver via supplied completion callback.
 *
 * Returns: VXGE_HW_OK, if the polling is completed successful.
 * VXGE_HW_COMPLETIONS_REMAIN: There are still more completed
 * descriptors available which are yet to be processed.
 */
enum vxge_hw_status vxge_hw_vpath_poll_tx(struct __vxge_hw_fifo *fifo,
					struct sk_buff ***skb_ptr, int nr_skb,
					int *more)
{
	enum vxge_hw_fifo_tcode t_code;
	void *first_txdlh;
	enum vxge_hw_status status = VXGE_HW_OK;
	struct __vxge_hw_channel *channel;

	channel = &fifo->channel;

	status = vxge_hw_fifo_txdl_next_completed(fifo,
				&first_txdlh, &t_code);
	if (status == VXGE_HW_OK)
		if (fifo->callback(fifo, first_txdlh, t_code,
			channel->userdata, skb_ptr, nr_skb, more) != VXGE_HW_OK)
			status = VXGE_HW_COMPLETIONS_REMAIN;

	return status;
}
