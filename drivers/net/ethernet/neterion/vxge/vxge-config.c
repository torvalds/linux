/******************************************************************************
 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 *
 * vxge-config.c: Driver for Exar Corp's X3100 Series 10GbE PCIe I/O
 *                Virtualized Server Adapter.
 * Copyright(c) 2002-2010 Exar Corp.
 ******************************************************************************/
#include <linux/vmalloc.h>
#include <linux/etherdevice.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "vxge-traffic.h"
#include "vxge-config.h"
#include "vxge-main.h"

#define VXGE_HW_VPATH_STATS_PIO_READ(offset) {				\
	status = __vxge_hw_vpath_stats_access(vpath,			\
					      VXGE_HW_STATS_OP_READ,	\
					      offset,			\
					      &val64);			\
	if (status != VXGE_HW_OK)					\
		return status;						\
}

static void
vxge_hw_vpath_set_zero_rx_frm_len(struct vxge_hw_vpath_reg __iomem *vp_reg)
{
	u64 val64;

	val64 = readq(&vp_reg->rxmac_vcfg0);
	val64 &= ~VXGE_HW_RXMAC_VCFG0_RTS_MAX_FRM_LEN(0x3fff);
	writeq(val64, &vp_reg->rxmac_vcfg0);
	val64 = readq(&vp_reg->rxmac_vcfg0);
}

/*
 * vxge_hw_vpath_wait_receive_idle - Wait for Rx to become idle
 */
int vxge_hw_vpath_wait_receive_idle(struct __vxge_hw_device *hldev, u32 vp_id)
{
	struct vxge_hw_vpath_reg __iomem *vp_reg;
	struct __vxge_hw_virtualpath *vpath;
	u64 val64, rxd_count, rxd_spat;
	int count = 0, total_count = 0;

	vpath = &hldev->virtual_paths[vp_id];
	vp_reg = vpath->vp_reg;

	vxge_hw_vpath_set_zero_rx_frm_len(vp_reg);

	/* Check that the ring controller for this vpath has enough free RxDs
	 * to send frames to the host.  This is done by reading the
	 * PRC_RXD_DOORBELL_VPn register and comparing the read value to the
	 * RXD_SPAT value for the vpath.
	 */
	val64 = readq(&vp_reg->prc_cfg6);
	rxd_spat = VXGE_HW_PRC_CFG6_GET_RXD_SPAT(val64) + 1;
	/* Use a factor of 2 when comparing rxd_count against rxd_spat for some
	 * leg room.
	 */
	rxd_spat *= 2;

	do {
		mdelay(1);

		rxd_count = readq(&vp_reg->prc_rxd_doorbell);

		/* Check that the ring controller for this vpath does
		 * not have any frame in its pipeline.
		 */
		val64 = readq(&vp_reg->frm_in_progress_cnt);
		if ((rxd_count <= rxd_spat) || (val64 > 0))
			count = 0;
		else
			count++;
		total_count++;
	} while ((count < VXGE_HW_MIN_SUCCESSIVE_IDLE_COUNT) &&
			(total_count < VXGE_HW_MAX_POLLING_COUNT));

	if (total_count >= VXGE_HW_MAX_POLLING_COUNT)
		printk(KERN_ALERT "%s: Still Receiving traffic. Abort wait\n",
			__func__);

	return total_count;
}

/* vxge_hw_device_wait_receive_idle - This function waits until all frames
 * stored in the frame buffer for each vpath assigned to the given
 * function (hldev) have been sent to the host.
 */
void vxge_hw_device_wait_receive_idle(struct __vxge_hw_device *hldev)
{
	int i, total_count = 0;

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {
		if (!(hldev->vpaths_deployed & vxge_mBIT(i)))
			continue;

		total_count += vxge_hw_vpath_wait_receive_idle(hldev, i);
		if (total_count >= VXGE_HW_MAX_POLLING_COUNT)
			break;
	}
}

/*
 * __vxge_hw_device_register_poll
 * Will poll certain register for specified amount of time.
 * Will poll until masked bit is not cleared.
 */
static enum vxge_hw_status
__vxge_hw_device_register_poll(void __iomem *reg, u64 mask, u32 max_millis)
{
	u64 val64;
	u32 i = 0;

	udelay(10);

	do {
		val64 = readq(reg);
		if (!(val64 & mask))
			return VXGE_HW_OK;
		udelay(100);
	} while (++i <= 9);

	i = 0;
	do {
		val64 = readq(reg);
		if (!(val64 & mask))
			return VXGE_HW_OK;
		mdelay(1);
	} while (++i <= max_millis);

	return VXGE_HW_FAIL;
}

static inline enum vxge_hw_status
__vxge_hw_pio_mem_write64(u64 val64, void __iomem *addr,
			  u64 mask, u32 max_millis)
{
	__vxge_hw_pio_mem_write32_lower((u32)vxge_bVALn(val64, 32, 32), addr);
	wmb();
	__vxge_hw_pio_mem_write32_upper((u32)vxge_bVALn(val64, 0, 32), addr);
	wmb();

	return __vxge_hw_device_register_poll(addr, mask, max_millis);
}

static enum vxge_hw_status
vxge_hw_vpath_fw_api(struct __vxge_hw_virtualpath *vpath, u32 action,
		     u32 fw_memo, u32 offset, u64 *data0, u64 *data1,
		     u64 *steer_ctrl)
{
	struct vxge_hw_vpath_reg __iomem *vp_reg = vpath->vp_reg;
	enum vxge_hw_status status;
	u64 val64;
	u32 retry = 0, max_retry = 3;

	spin_lock(&vpath->lock);
	if (!vpath->vp_open) {
		spin_unlock(&vpath->lock);
		max_retry = 100;
	}

	writeq(*data0, &vp_reg->rts_access_steer_data0);
	writeq(*data1, &vp_reg->rts_access_steer_data1);
	wmb();

	val64 = VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION(action) |
		VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL(fw_memo) |
		VXGE_HW_RTS_ACCESS_STEER_CTRL_OFFSET(offset) |
		VXGE_HW_RTS_ACCESS_STEER_CTRL_STROBE |
		*steer_ctrl;

	status = __vxge_hw_pio_mem_write64(val64,
					   &vp_reg->rts_access_steer_ctrl,
					   VXGE_HW_RTS_ACCESS_STEER_CTRL_STROBE,
					   VXGE_HW_DEF_DEVICE_POLL_MILLIS);

	/* The __vxge_hw_device_register_poll can udelay for a significant
	 * amount of time, blocking other process from the CPU.  If it delays
	 * for ~5secs, a NMI error can occur.  A way around this is to give up
	 * the processor via msleep, but this is not allowed is under lock.
	 * So, only allow it to sleep for ~4secs if open.  Otherwise, delay for
	 * 1sec and sleep for 10ms until the firmware operation has completed
	 * or timed-out.
	 */
	while ((status != VXGE_HW_OK) && retry++ < max_retry) {
		if (!vpath->vp_open)
			msleep(20);
		status = __vxge_hw_device_register_poll(
					&vp_reg->rts_access_steer_ctrl,
					VXGE_HW_RTS_ACCESS_STEER_CTRL_STROBE,
					VXGE_HW_DEF_DEVICE_POLL_MILLIS);
	}

	if (status != VXGE_HW_OK)
		goto out;

	val64 = readq(&vp_reg->rts_access_steer_ctrl);
	if (val64 & VXGE_HW_RTS_ACCESS_STEER_CTRL_RMACJ_STATUS) {
		*data0 = readq(&vp_reg->rts_access_steer_data0);
		*data1 = readq(&vp_reg->rts_access_steer_data1);
		*steer_ctrl = val64;
	} else
		status = VXGE_HW_FAIL;

out:
	if (vpath->vp_open)
		spin_unlock(&vpath->lock);
	return status;
}

enum vxge_hw_status
vxge_hw_upgrade_read_version(struct __vxge_hw_device *hldev, u32 *major,
			     u32 *minor, u32 *build)
{
	u64 data0 = 0, data1 = 0, steer_ctrl = 0;
	struct __vxge_hw_virtualpath *vpath;
	enum vxge_hw_status status;

	vpath = &hldev->virtual_paths[hldev->first_vp_id];

	status = vxge_hw_vpath_fw_api(vpath,
				      VXGE_HW_FW_UPGRADE_ACTION,
				      VXGE_HW_FW_UPGRADE_MEMO,
				      VXGE_HW_FW_UPGRADE_OFFSET_READ,
				      &data0, &data1, &steer_ctrl);
	if (status != VXGE_HW_OK)
		return status;

	*major = VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_MAJOR(data0);
	*minor = VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_MINOR(data0);
	*build = VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_BUILD(data0);

	return status;
}

enum vxge_hw_status vxge_hw_flash_fw(struct __vxge_hw_device *hldev)
{
	u64 data0 = 0, data1 = 0, steer_ctrl = 0;
	struct __vxge_hw_virtualpath *vpath;
	enum vxge_hw_status status;
	u32 ret;

	vpath = &hldev->virtual_paths[hldev->first_vp_id];

	status = vxge_hw_vpath_fw_api(vpath,
				      VXGE_HW_FW_UPGRADE_ACTION,
				      VXGE_HW_FW_UPGRADE_MEMO,
				      VXGE_HW_FW_UPGRADE_OFFSET_COMMIT,
				      &data0, &data1, &steer_ctrl);
	if (status != VXGE_HW_OK) {
		vxge_debug_init(VXGE_ERR, "%s: FW upgrade failed", __func__);
		goto exit;
	}

	ret = VXGE_HW_RTS_ACCESS_STEER_CTRL_GET_ACTION(steer_ctrl) & 0x7F;
	if (ret != 1) {
		vxge_debug_init(VXGE_ERR, "%s: FW commit failed with error %d",
				__func__, ret);
		status = VXGE_HW_FAIL;
	}

exit:
	return status;
}

enum vxge_hw_status
vxge_update_fw_image(struct __vxge_hw_device *hldev, const u8 *fwdata, int size)
{
	u64 data0 = 0, data1 = 0, steer_ctrl = 0;
	struct __vxge_hw_virtualpath *vpath;
	enum vxge_hw_status status;
	int ret_code, sec_code;

	vpath = &hldev->virtual_paths[hldev->first_vp_id];

	/* send upgrade start command */
	status = vxge_hw_vpath_fw_api(vpath,
				      VXGE_HW_FW_UPGRADE_ACTION,
				      VXGE_HW_FW_UPGRADE_MEMO,
				      VXGE_HW_FW_UPGRADE_OFFSET_START,
				      &data0, &data1, &steer_ctrl);
	if (status != VXGE_HW_OK) {
		vxge_debug_init(VXGE_ERR, " %s: Upgrade start cmd failed",
				__func__);
		return status;
	}

	/* Transfer fw image to adapter 16 bytes at a time */
	for (; size > 0; size -= VXGE_HW_FW_UPGRADE_BLK_SIZE) {
		steer_ctrl = 0;

		/* The next 128bits of fwdata to be loaded onto the adapter */
		data0 = *((u64 *)fwdata);
		data1 = *((u64 *)fwdata + 1);

		status = vxge_hw_vpath_fw_api(vpath,
					      VXGE_HW_FW_UPGRADE_ACTION,
					      VXGE_HW_FW_UPGRADE_MEMO,
					      VXGE_HW_FW_UPGRADE_OFFSET_SEND,
					      &data0, &data1, &steer_ctrl);
		if (status != VXGE_HW_OK) {
			vxge_debug_init(VXGE_ERR, "%s: Upgrade send failed",
					__func__);
			goto out;
		}

		ret_code = VXGE_HW_UPGRADE_GET_RET_ERR_CODE(data0);
		switch (ret_code) {
		case VXGE_HW_FW_UPGRADE_OK:
			/* All OK, send next 16 bytes. */
			break;
		case VXGE_FW_UPGRADE_BYTES2SKIP:
			/* skip bytes in the stream */
			fwdata += (data0 >> 8) & 0xFFFFFFFF;
			break;
		case VXGE_HW_FW_UPGRADE_DONE:
			goto out;
		case VXGE_HW_FW_UPGRADE_ERR:
			sec_code = VXGE_HW_UPGRADE_GET_SEC_ERR_CODE(data0);
			switch (sec_code) {
			case VXGE_HW_FW_UPGRADE_ERR_CORRUPT_DATA_1:
			case VXGE_HW_FW_UPGRADE_ERR_CORRUPT_DATA_7:
				printk(KERN_ERR
				       "corrupted data from .ncf file\n");
				break;
			case VXGE_HW_FW_UPGRADE_ERR_INV_NCF_FILE_3:
			case VXGE_HW_FW_UPGRADE_ERR_INV_NCF_FILE_4:
			case VXGE_HW_FW_UPGRADE_ERR_INV_NCF_FILE_5:
			case VXGE_HW_FW_UPGRADE_ERR_INV_NCF_FILE_6:
			case VXGE_HW_FW_UPGRADE_ERR_INV_NCF_FILE_8:
				printk(KERN_ERR "invalid .ncf file\n");
				break;
			case VXGE_HW_FW_UPGRADE_ERR_BUFFER_OVERFLOW:
				printk(KERN_ERR "buffer overflow\n");
				break;
			case VXGE_HW_FW_UPGRADE_ERR_FAILED_TO_FLASH:
				printk(KERN_ERR "failed to flash the image\n");
				break;
			case VXGE_HW_FW_UPGRADE_ERR_GENERIC_ERROR_UNKNOWN:
				printk(KERN_ERR
				       "generic error. Unknown error type\n");
				break;
			default:
				printk(KERN_ERR "Unknown error of type %d\n",
				       sec_code);
				break;
			}
			status = VXGE_HW_FAIL;
			goto out;
		default:
			printk(KERN_ERR "Unknown FW error: %d\n", ret_code);
			status = VXGE_HW_FAIL;
			goto out;
		}
		/* point to next 16 bytes */
		fwdata += VXGE_HW_FW_UPGRADE_BLK_SIZE;
	}
out:
	return status;
}

enum vxge_hw_status
vxge_hw_vpath_eprom_img_ver_get(struct __vxge_hw_device *hldev,
				struct eprom_image *img)
{
	u64 data0 = 0, data1 = 0, steer_ctrl = 0;
	struct __vxge_hw_virtualpath *vpath;
	enum vxge_hw_status status;
	int i;

	vpath = &hldev->virtual_paths[hldev->first_vp_id];

	for (i = 0; i < VXGE_HW_MAX_ROM_IMAGES; i++) {
		data0 = VXGE_HW_RTS_ACCESS_STEER_ROM_IMAGE_INDEX(i);
		data1 = steer_ctrl = 0;

		status = vxge_hw_vpath_fw_api(vpath,
			VXGE_HW_FW_API_GET_EPROM_REV,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_FW_MEMO,
			0, &data0, &data1, &steer_ctrl);
		if (status != VXGE_HW_OK)
			break;

		img[i].is_valid = VXGE_HW_GET_EPROM_IMAGE_VALID(data0);
		img[i].index = VXGE_HW_GET_EPROM_IMAGE_INDEX(data0);
		img[i].type = VXGE_HW_GET_EPROM_IMAGE_TYPE(data0);
		img[i].version = VXGE_HW_GET_EPROM_IMAGE_REV(data0);
	}

	return status;
}

/*
 * __vxge_hw_channel_free - Free memory allocated for channel
 * This function deallocates memory from the channel and various arrays
 * in the channel
 */
static void __vxge_hw_channel_free(struct __vxge_hw_channel *channel)
{
	kfree(channel->work_arr);
	kfree(channel->free_arr);
	kfree(channel->reserve_arr);
	kfree(channel->orig_arr);
	kfree(channel);
}

/*
 * __vxge_hw_channel_initialize - Initialize a channel
 * This function initializes a channel by properly setting the
 * various references
 */
static enum vxge_hw_status
__vxge_hw_channel_initialize(struct __vxge_hw_channel *channel)
{
	u32 i;
	struct __vxge_hw_virtualpath *vpath;

	vpath = channel->vph->vpath;

	if ((channel->reserve_arr != NULL) && (channel->orig_arr != NULL)) {
		for (i = 0; i < channel->length; i++)
			channel->orig_arr[i] = channel->reserve_arr[i];
	}

	switch (channel->type) {
	case VXGE_HW_CHANNEL_TYPE_FIFO:
		vpath->fifoh = (struct __vxge_hw_fifo *)channel;
		channel->stats = &((struct __vxge_hw_fifo *)
				channel)->stats->common_stats;
		break;
	case VXGE_HW_CHANNEL_TYPE_RING:
		vpath->ringh = (struct __vxge_hw_ring *)channel;
		channel->stats = &((struct __vxge_hw_ring *)
				channel)->stats->common_stats;
		break;
	default:
		break;
	}

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_channel_reset - Resets a channel
 * This function resets a channel by properly setting the various references
 */
static enum vxge_hw_status
__vxge_hw_channel_reset(struct __vxge_hw_channel *channel)
{
	u32 i;

	for (i = 0; i < channel->length; i++) {
		if (channel->reserve_arr != NULL)
			channel->reserve_arr[i] = channel->orig_arr[i];
		if (channel->free_arr != NULL)
			channel->free_arr[i] = NULL;
		if (channel->work_arr != NULL)
			channel->work_arr[i] = NULL;
	}
	channel->free_ptr = channel->length;
	channel->reserve_ptr = channel->length;
	channel->reserve_top = 0;
	channel->post_index = 0;
	channel->compl_index = 0;

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_device_pci_e_init
 * Initialize certain PCI/PCI-X configuration registers
 * with recommended values. Save config space for future hw resets.
 */
static void __vxge_hw_device_pci_e_init(struct __vxge_hw_device *hldev)
{
	u16 cmd = 0;

	/* Set the PErr Repconse bit and SERR in PCI command register. */
	pci_read_config_word(hldev->pdev, PCI_COMMAND, &cmd);
	cmd |= 0x140;
	pci_write_config_word(hldev->pdev, PCI_COMMAND, cmd);

	pci_save_state(hldev->pdev);
}

/* __vxge_hw_device_vpath_reset_in_prog_check - Check if vpath reset
 * in progress
 * This routine checks the vpath reset in progress register is turned zero
 */
static enum vxge_hw_status
__vxge_hw_device_vpath_reset_in_prog_check(u64 __iomem *vpath_rst_in_prog)
{
	enum vxge_hw_status status;
	status = __vxge_hw_device_register_poll(vpath_rst_in_prog,
			VXGE_HW_VPATH_RST_IN_PROG_VPATH_RST_IN_PROG(0x1ffff),
			VXGE_HW_DEF_DEVICE_POLL_MILLIS);
	return status;
}

/*
 * _hw_legacy_swapper_set - Set the swapper bits for the legacy secion.
 * Set the swapper bits appropriately for the lagacy section.
 */
static enum vxge_hw_status
__vxge_hw_legacy_swapper_set(struct vxge_hw_legacy_reg __iomem *legacy_reg)
{
	u64 val64;
	enum vxge_hw_status status = VXGE_HW_OK;

	val64 = readq(&legacy_reg->toc_swapper_fb);

	wmb();

	switch (val64) {
	case VXGE_HW_SWAPPER_INITIAL_VALUE:
		return status;

	case VXGE_HW_SWAPPER_BYTE_SWAPPED_BIT_FLIPPED:
		writeq(VXGE_HW_SWAPPER_READ_BYTE_SWAP_ENABLE,
			&legacy_reg->pifm_rd_swap_en);
		writeq(VXGE_HW_SWAPPER_READ_BIT_FLAP_ENABLE,
			&legacy_reg->pifm_rd_flip_en);
		writeq(VXGE_HW_SWAPPER_WRITE_BYTE_SWAP_ENABLE,
			&legacy_reg->pifm_wr_swap_en);
		writeq(VXGE_HW_SWAPPER_WRITE_BIT_FLAP_ENABLE,
			&legacy_reg->pifm_wr_flip_en);
		break;

	case VXGE_HW_SWAPPER_BYTE_SWAPPED:
		writeq(VXGE_HW_SWAPPER_READ_BYTE_SWAP_ENABLE,
			&legacy_reg->pifm_rd_swap_en);
		writeq(VXGE_HW_SWAPPER_WRITE_BYTE_SWAP_ENABLE,
			&legacy_reg->pifm_wr_swap_en);
		break;

	case VXGE_HW_SWAPPER_BIT_FLIPPED:
		writeq(VXGE_HW_SWAPPER_READ_BIT_FLAP_ENABLE,
			&legacy_reg->pifm_rd_flip_en);
		writeq(VXGE_HW_SWAPPER_WRITE_BIT_FLAP_ENABLE,
			&legacy_reg->pifm_wr_flip_en);
		break;
	}

	wmb();

	val64 = readq(&legacy_reg->toc_swapper_fb);

	if (val64 != VXGE_HW_SWAPPER_INITIAL_VALUE)
		status = VXGE_HW_ERR_SWAPPER_CTRL;

	return status;
}

/*
 * __vxge_hw_device_toc_get
 * This routine sets the swapper and reads the toc pointer and returns the
 * memory mapped address of the toc
 */
static struct vxge_hw_toc_reg __iomem *
__vxge_hw_device_toc_get(void __iomem *bar0)
{
	u64 val64;
	struct vxge_hw_toc_reg __iomem *toc = NULL;
	enum vxge_hw_status status;

	struct vxge_hw_legacy_reg __iomem *legacy_reg =
		(struct vxge_hw_legacy_reg __iomem *)bar0;

	status = __vxge_hw_legacy_swapper_set(legacy_reg);
	if (status != VXGE_HW_OK)
		goto exit;

	val64 =	readq(&legacy_reg->toc_first_pointer);
	toc = bar0 + val64;
exit:
	return toc;
}

/*
 * __vxge_hw_device_reg_addr_get
 * This routine sets the swapper and reads the toc pointer and initializes the
 * register location pointers in the device object. It waits until the ric is
 * completed initializing registers.
 */
static enum vxge_hw_status
__vxge_hw_device_reg_addr_get(struct __vxge_hw_device *hldev)
{
	u64 val64;
	u32 i;
	enum vxge_hw_status status = VXGE_HW_OK;

	hldev->legacy_reg = hldev->bar0;

	hldev->toc_reg = __vxge_hw_device_toc_get(hldev->bar0);
	if (hldev->toc_reg  == NULL) {
		status = VXGE_HW_FAIL;
		goto exit;
	}

	val64 = readq(&hldev->toc_reg->toc_common_pointer);
	hldev->common_reg = hldev->bar0 + val64;

	val64 = readq(&hldev->toc_reg->toc_mrpcim_pointer);
	hldev->mrpcim_reg = hldev->bar0 + val64;

	for (i = 0; i < VXGE_HW_TITAN_SRPCIM_REG_SPACES; i++) {
		val64 = readq(&hldev->toc_reg->toc_srpcim_pointer[i]);
		hldev->srpcim_reg[i] = hldev->bar0 + val64;
	}

	for (i = 0; i < VXGE_HW_TITAN_VPMGMT_REG_SPACES; i++) {
		val64 = readq(&hldev->toc_reg->toc_vpmgmt_pointer[i]);
		hldev->vpmgmt_reg[i] = hldev->bar0 + val64;
	}

	for (i = 0; i < VXGE_HW_TITAN_VPATH_REG_SPACES; i++) {
		val64 = readq(&hldev->toc_reg->toc_vpath_pointer[i]);
		hldev->vpath_reg[i] = hldev->bar0 + val64;
	}

	val64 = readq(&hldev->toc_reg->toc_kdfc);

	switch (VXGE_HW_TOC_GET_KDFC_INITIAL_BIR(val64)) {
	case 0:
		hldev->kdfc = hldev->bar0 + VXGE_HW_TOC_GET_KDFC_INITIAL_OFFSET(val64) ;
		break;
	default:
		break;
	}

	status = __vxge_hw_device_vpath_reset_in_prog_check(
			(u64 __iomem *)&hldev->common_reg->vpath_rst_in_prog);
exit:
	return status;
}

/*
 * __vxge_hw_device_access_rights_get: Get Access Rights of the driver
 * This routine returns the Access Rights of the driver
 */
static u32
__vxge_hw_device_access_rights_get(u32 host_type, u32 func_id)
{
	u32 access_rights = VXGE_HW_DEVICE_ACCESS_RIGHT_VPATH;

	switch (host_type) {
	case VXGE_HW_NO_MR_NO_SR_NORMAL_FUNCTION:
		if (func_id == 0) {
			access_rights |= VXGE_HW_DEVICE_ACCESS_RIGHT_MRPCIM |
					VXGE_HW_DEVICE_ACCESS_RIGHT_SRPCIM;
		}
		break;
	case VXGE_HW_MR_NO_SR_VH0_BASE_FUNCTION:
		access_rights |= VXGE_HW_DEVICE_ACCESS_RIGHT_MRPCIM |
				VXGE_HW_DEVICE_ACCESS_RIGHT_SRPCIM;
		break;
	case VXGE_HW_NO_MR_SR_VH0_FUNCTION0:
		access_rights |= VXGE_HW_DEVICE_ACCESS_RIGHT_MRPCIM |
				VXGE_HW_DEVICE_ACCESS_RIGHT_SRPCIM;
		break;
	case VXGE_HW_NO_MR_SR_VH0_VIRTUAL_FUNCTION:
	case VXGE_HW_SR_VH_VIRTUAL_FUNCTION:
	case VXGE_HW_MR_SR_VH0_INVALID_CONFIG:
		break;
	case VXGE_HW_SR_VH_FUNCTION0:
	case VXGE_HW_VH_NORMAL_FUNCTION:
		access_rights |= VXGE_HW_DEVICE_ACCESS_RIGHT_SRPCIM;
		break;
	}

	return access_rights;
}
/*
 * __vxge_hw_device_is_privilaged
 * This routine checks if the device function is privilaged or not
 */

enum vxge_hw_status
__vxge_hw_device_is_privilaged(u32 host_type, u32 func_id)
{
	if (__vxge_hw_device_access_rights_get(host_type,
		func_id) &
		VXGE_HW_DEVICE_ACCESS_RIGHT_MRPCIM)
		return VXGE_HW_OK;
	else
		return VXGE_HW_ERR_PRIVILEGED_OPERATION;
}

/*
 * __vxge_hw_vpath_func_id_get - Get the function id of the vpath.
 * Returns the function number of the vpath.
 */
static u32
__vxge_hw_vpath_func_id_get(struct vxge_hw_vpmgmt_reg __iomem *vpmgmt_reg)
{
	u64 val64;

	val64 = readq(&vpmgmt_reg->vpath_to_func_map_cfg1);

	return
	 (u32)VXGE_HW_VPATH_TO_FUNC_MAP_CFG1_GET_VPATH_TO_FUNC_MAP_CFG1(val64);
}

/*
 * __vxge_hw_device_host_info_get
 * This routine returns the host type assignments
 */
static void __vxge_hw_device_host_info_get(struct __vxge_hw_device *hldev)
{
	u64 val64;
	u32 i;

	val64 = readq(&hldev->common_reg->host_type_assignments);

	hldev->host_type =
	   (u32)VXGE_HW_HOST_TYPE_ASSIGNMENTS_GET_HOST_TYPE_ASSIGNMENTS(val64);

	hldev->vpath_assignments = readq(&hldev->common_reg->vpath_assignments);

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {
		if (!(hldev->vpath_assignments & vxge_mBIT(i)))
			continue;

		hldev->func_id =
			__vxge_hw_vpath_func_id_get(hldev->vpmgmt_reg[i]);

		hldev->access_rights = __vxge_hw_device_access_rights_get(
			hldev->host_type, hldev->func_id);

		hldev->virtual_paths[i].vp_open = VXGE_HW_VP_NOT_OPEN;
		hldev->virtual_paths[i].vp_reg = hldev->vpath_reg[i];

		hldev->first_vp_id = i;
		break;
	}
}

/*
 * __vxge_hw_verify_pci_e_info - Validate the pci-e link parameters such as
 * link width and signalling rate.
 */
static enum vxge_hw_status
__vxge_hw_verify_pci_e_info(struct __vxge_hw_device *hldev)
{
	struct pci_dev *dev = hldev->pdev;
	u16 lnk;

	/* Get the negotiated link width and speed from PCI config space */
	pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &lnk);

	if ((lnk & PCI_EXP_LNKSTA_CLS) != 1)
		return VXGE_HW_ERR_INVALID_PCI_INFO;

	switch ((lnk & PCI_EXP_LNKSTA_NLW) >> 4) {
	case PCIE_LNK_WIDTH_RESRV:
	case PCIE_LNK_X1:
	case PCIE_LNK_X2:
	case PCIE_LNK_X4:
	case PCIE_LNK_X8:
		break;
	default:
		return VXGE_HW_ERR_INVALID_PCI_INFO;
	}

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_device_initialize
 * Initialize Titan-V hardware.
 */
static enum vxge_hw_status
__vxge_hw_device_initialize(struct __vxge_hw_device *hldev)
{
	enum vxge_hw_status status = VXGE_HW_OK;

	if (VXGE_HW_OK == __vxge_hw_device_is_privilaged(hldev->host_type,
				hldev->func_id)) {
		/* Validate the pci-e link width and speed */
		status = __vxge_hw_verify_pci_e_info(hldev);
		if (status != VXGE_HW_OK)
			goto exit;
	}

exit:
	return status;
}

/*
 * __vxge_hw_vpath_fw_ver_get - Get the fw version
 * Returns FW Version
 */
static enum vxge_hw_status
__vxge_hw_vpath_fw_ver_get(struct __vxge_hw_virtualpath *vpath,
			   struct vxge_hw_device_hw_info *hw_info)
{
	struct vxge_hw_device_version *fw_version = &hw_info->fw_version;
	struct vxge_hw_device_date *fw_date = &hw_info->fw_date;
	struct vxge_hw_device_version *flash_version = &hw_info->flash_version;
	struct vxge_hw_device_date *flash_date = &hw_info->flash_date;
	u64 data0 = 0, data1 = 0, steer_ctrl = 0;
	enum vxge_hw_status status;

	status = vxge_hw_vpath_fw_api(vpath,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_READ_ENTRY,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_FW_MEMO,
			0, &data0, &data1, &steer_ctrl);
	if (status != VXGE_HW_OK)
		goto exit;

	fw_date->day =
	    (u32) VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_DAY(data0);
	fw_date->month =
	    (u32) VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_MONTH(data0);
	fw_date->year =
	    (u32) VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_YEAR(data0);

	snprintf(fw_date->date, VXGE_HW_FW_STRLEN, "%2.2d/%2.2d/%4.4d",
		 fw_date->month, fw_date->day, fw_date->year);

	fw_version->major =
	    (u32) VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_MAJOR(data0);
	fw_version->minor =
	    (u32) VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_MINOR(data0);
	fw_version->build =
	    (u32) VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_FW_VER_BUILD(data0);

	snprintf(fw_version->version, VXGE_HW_FW_STRLEN, "%d.%d.%d",
		 fw_version->major, fw_version->minor, fw_version->build);

	flash_date->day =
	    (u32) VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_FLASH_VER_DAY(data1);
	flash_date->month =
	    (u32) VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_FLASH_VER_MONTH(data1);
	flash_date->year =
	    (u32) VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_FLASH_VER_YEAR(data1);

	snprintf(flash_date->date, VXGE_HW_FW_STRLEN, "%2.2d/%2.2d/%4.4d",
		 flash_date->month, flash_date->day, flash_date->year);

	flash_version->major =
	    (u32) VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_FLASH_VER_MAJOR(data1);
	flash_version->minor =
	    (u32) VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_FLASH_VER_MINOR(data1);
	flash_version->build =
	    (u32) VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_FLASH_VER_BUILD(data1);

	snprintf(flash_version->version, VXGE_HW_FW_STRLEN, "%d.%d.%d",
		 flash_version->major, flash_version->minor,
		 flash_version->build);

exit:
	return status;
}

/*
 * __vxge_hw_vpath_card_info_get - Get the serial numbers,
 * part number and product description.
 */
static enum vxge_hw_status
__vxge_hw_vpath_card_info_get(struct __vxge_hw_virtualpath *vpath,
			      struct vxge_hw_device_hw_info *hw_info)
{
	enum vxge_hw_status status;
	u64 data0, data1 = 0, steer_ctrl = 0;
	u8 *serial_number = hw_info->serial_number;
	u8 *part_number = hw_info->part_number;
	u8 *product_desc = hw_info->product_desc;
	u32 i, j = 0;

	data0 = VXGE_HW_RTS_ACCESS_STEER_DATA0_MEMO_ITEM_SERIAL_NUMBER;

	status = vxge_hw_vpath_fw_api(vpath,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_READ_MEMO_ENTRY,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_FW_MEMO,
			0, &data0, &data1, &steer_ctrl);
	if (status != VXGE_HW_OK)
		return status;

	((u64 *)serial_number)[0] = be64_to_cpu(data0);
	((u64 *)serial_number)[1] = be64_to_cpu(data1);

	data0 = VXGE_HW_RTS_ACCESS_STEER_DATA0_MEMO_ITEM_PART_NUMBER;
	data1 = steer_ctrl = 0;

	status = vxge_hw_vpath_fw_api(vpath,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_READ_MEMO_ENTRY,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_FW_MEMO,
			0, &data0, &data1, &steer_ctrl);
	if (status != VXGE_HW_OK)
		return status;

	((u64 *)part_number)[0] = be64_to_cpu(data0);
	((u64 *)part_number)[1] = be64_to_cpu(data1);

	for (i = VXGE_HW_RTS_ACCESS_STEER_DATA0_MEMO_ITEM_DESC_0;
	     i <= VXGE_HW_RTS_ACCESS_STEER_DATA0_MEMO_ITEM_DESC_3; i++) {
		data0 = i;
		data1 = steer_ctrl = 0;

		status = vxge_hw_vpath_fw_api(vpath,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_READ_MEMO_ENTRY,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_FW_MEMO,
			0, &data0, &data1, &steer_ctrl);
		if (status != VXGE_HW_OK)
			return status;

		((u64 *)product_desc)[j++] = be64_to_cpu(data0);
		((u64 *)product_desc)[j++] = be64_to_cpu(data1);
	}

	return status;
}

/*
 * __vxge_hw_vpath_pci_func_mode_get - Get the pci mode
 * Returns pci function mode
 */
static enum vxge_hw_status
__vxge_hw_vpath_pci_func_mode_get(struct __vxge_hw_virtualpath *vpath,
				  struct vxge_hw_device_hw_info *hw_info)
{
	u64 data0, data1 = 0, steer_ctrl = 0;
	enum vxge_hw_status status;

	data0 = 0;

	status = vxge_hw_vpath_fw_api(vpath,
			VXGE_HW_FW_API_GET_FUNC_MODE,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_FW_MEMO,
			0, &data0, &data1, &steer_ctrl);
	if (status != VXGE_HW_OK)
		return status;

	hw_info->function_mode = VXGE_HW_GET_FUNC_MODE_VAL(data0);
	return status;
}

/*
 * __vxge_hw_vpath_addr_get - Get the hw address entry for this vpath
 *               from MAC address table.
 */
static enum vxge_hw_status
__vxge_hw_vpath_addr_get(struct __vxge_hw_virtualpath *vpath,
			 u8 *macaddr, u8 *macaddr_mask)
{
	u64 action = VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_LIST_FIRST_ENTRY,
	    data0 = 0, data1 = 0, steer_ctrl = 0;
	enum vxge_hw_status status;
	int i;

	do {
		status = vxge_hw_vpath_fw_api(vpath, action,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_DA,
			0, &data0, &data1, &steer_ctrl);
		if (status != VXGE_HW_OK)
			goto exit;

		data0 = VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_DA_MAC_ADDR(data0);
		data1 = VXGE_HW_RTS_ACCESS_STEER_DATA1_GET_DA_MAC_ADDR_MASK(
									data1);

		for (i = ETH_ALEN; i > 0; i--) {
			macaddr[i - 1] = (u8) (data0 & 0xFF);
			data0 >>= 8;

			macaddr_mask[i - 1] = (u8) (data1 & 0xFF);
			data1 >>= 8;
		}

		action = VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_LIST_NEXT_ENTRY;
		data0 = 0, data1 = 0, steer_ctrl = 0;

	} while (!is_valid_ether_addr(macaddr));
exit:
	return status;
}

/**
 * vxge_hw_device_hw_info_get - Get the hw information
 * Returns the vpath mask that has the bits set for each vpath allocated
 * for the driver, FW version information, and the first mac address for
 * each vpath
 */
enum vxge_hw_status
vxge_hw_device_hw_info_get(void __iomem *bar0,
			   struct vxge_hw_device_hw_info *hw_info)
{
	u32 i;
	u64 val64;
	struct vxge_hw_toc_reg __iomem *toc;
	struct vxge_hw_mrpcim_reg __iomem *mrpcim_reg;
	struct vxge_hw_common_reg __iomem *common_reg;
	struct vxge_hw_vpmgmt_reg __iomem *vpmgmt_reg;
	enum vxge_hw_status status;
	struct __vxge_hw_virtualpath vpath;

	memset(hw_info, 0, sizeof(struct vxge_hw_device_hw_info));

	toc = __vxge_hw_device_toc_get(bar0);
	if (toc == NULL) {
		status = VXGE_HW_ERR_CRITICAL;
		goto exit;
	}

	val64 = readq(&toc->toc_common_pointer);
	common_reg = bar0 + val64;

	status = __vxge_hw_device_vpath_reset_in_prog_check(
		(u64 __iomem *)&common_reg->vpath_rst_in_prog);
	if (status != VXGE_HW_OK)
		goto exit;

	hw_info->vpath_mask = readq(&common_reg->vpath_assignments);

	val64 = readq(&common_reg->host_type_assignments);

	hw_info->host_type =
	   (u32)VXGE_HW_HOST_TYPE_ASSIGNMENTS_GET_HOST_TYPE_ASSIGNMENTS(val64);

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {
		if (!((hw_info->vpath_mask) & vxge_mBIT(i)))
			continue;

		val64 = readq(&toc->toc_vpmgmt_pointer[i]);

		vpmgmt_reg = bar0 + val64;

		hw_info->func_id = __vxge_hw_vpath_func_id_get(vpmgmt_reg);
		if (__vxge_hw_device_access_rights_get(hw_info->host_type,
			hw_info->func_id) &
			VXGE_HW_DEVICE_ACCESS_RIGHT_MRPCIM) {

			val64 = readq(&toc->toc_mrpcim_pointer);

			mrpcim_reg = bar0 + val64;

			writeq(0, &mrpcim_reg->xgmac_gen_fw_memo_mask);
			wmb();
		}

		val64 = readq(&toc->toc_vpath_pointer[i]);

		spin_lock_init(&vpath.lock);
		vpath.vp_reg = bar0 + val64;
		vpath.vp_open = VXGE_HW_VP_NOT_OPEN;

		status = __vxge_hw_vpath_pci_func_mode_get(&vpath, hw_info);
		if (status != VXGE_HW_OK)
			goto exit;

		status = __vxge_hw_vpath_fw_ver_get(&vpath, hw_info);
		if (status != VXGE_HW_OK)
			goto exit;

		status = __vxge_hw_vpath_card_info_get(&vpath, hw_info);
		if (status != VXGE_HW_OK)
			goto exit;

		break;
	}

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {
		if (!((hw_info->vpath_mask) & vxge_mBIT(i)))
			continue;

		val64 = readq(&toc->toc_vpath_pointer[i]);
		vpath.vp_reg = bar0 + val64;
		vpath.vp_open = VXGE_HW_VP_NOT_OPEN;

		status =  __vxge_hw_vpath_addr_get(&vpath,
				hw_info->mac_addrs[i],
				hw_info->mac_addr_masks[i]);
		if (status != VXGE_HW_OK)
			goto exit;
	}
exit:
	return status;
}

/*
 * __vxge_hw_blockpool_destroy - Deallocates the block pool
 */
static void __vxge_hw_blockpool_destroy(struct __vxge_hw_blockpool *blockpool)
{
	struct __vxge_hw_device *hldev;
	struct list_head *p, *n;

	if (!blockpool)
		return;

	hldev = blockpool->hldev;

	list_for_each_safe(p, n, &blockpool->free_block_list) {
		pci_unmap_single(hldev->pdev,
			((struct __vxge_hw_blockpool_entry *)p)->dma_addr,
			((struct __vxge_hw_blockpool_entry *)p)->length,
			PCI_DMA_BIDIRECTIONAL);

		vxge_os_dma_free(hldev->pdev,
			((struct __vxge_hw_blockpool_entry *)p)->memblock,
			&((struct __vxge_hw_blockpool_entry *)p)->acc_handle);

		list_del(&((struct __vxge_hw_blockpool_entry *)p)->item);
		kfree(p);
		blockpool->pool_size--;
	}

	list_for_each_safe(p, n, &blockpool->free_entry_list) {
		list_del(&((struct __vxge_hw_blockpool_entry *)p)->item);
		kfree((void *)p);
	}

	return;
}

/*
 * __vxge_hw_blockpool_create - Create block pool
 */
static enum vxge_hw_status
__vxge_hw_blockpool_create(struct __vxge_hw_device *hldev,
			   struct __vxge_hw_blockpool *blockpool,
			   u32 pool_size,
			   u32 pool_max)
{
	u32 i;
	struct __vxge_hw_blockpool_entry *entry = NULL;
	void *memblock;
	dma_addr_t dma_addr;
	struct pci_dev *dma_handle;
	struct pci_dev *acc_handle;
	enum vxge_hw_status status = VXGE_HW_OK;

	if (blockpool == NULL) {
		status = VXGE_HW_FAIL;
		goto blockpool_create_exit;
	}

	blockpool->hldev = hldev;
	blockpool->block_size = VXGE_HW_BLOCK_SIZE;
	blockpool->pool_size = 0;
	blockpool->pool_max = pool_max;
	blockpool->req_out = 0;

	INIT_LIST_HEAD(&blockpool->free_block_list);
	INIT_LIST_HEAD(&blockpool->free_entry_list);

	for (i = 0; i < pool_size + pool_max; i++) {
		entry = kzalloc(sizeof(struct __vxge_hw_blockpool_entry),
				GFP_KERNEL);
		if (entry == NULL) {
			__vxge_hw_blockpool_destroy(blockpool);
			status = VXGE_HW_ERR_OUT_OF_MEMORY;
			goto blockpool_create_exit;
		}
		list_add(&entry->item, &blockpool->free_entry_list);
	}

	for (i = 0; i < pool_size; i++) {
		memblock = vxge_os_dma_malloc(
				hldev->pdev,
				VXGE_HW_BLOCK_SIZE,
				&dma_handle,
				&acc_handle);
		if (memblock == NULL) {
			__vxge_hw_blockpool_destroy(blockpool);
			status = VXGE_HW_ERR_OUT_OF_MEMORY;
			goto blockpool_create_exit;
		}

		dma_addr = pci_map_single(hldev->pdev, memblock,
				VXGE_HW_BLOCK_SIZE, PCI_DMA_BIDIRECTIONAL);
		if (unlikely(pci_dma_mapping_error(hldev->pdev,
				dma_addr))) {
			vxge_os_dma_free(hldev->pdev, memblock, &acc_handle);
			__vxge_hw_blockpool_destroy(blockpool);
			status = VXGE_HW_ERR_OUT_OF_MEMORY;
			goto blockpool_create_exit;
		}

		if (!list_empty(&blockpool->free_entry_list))
			entry = (struct __vxge_hw_blockpool_entry *)
				list_first_entry(&blockpool->free_entry_list,
					struct __vxge_hw_blockpool_entry,
					item);

		if (entry == NULL)
			entry =
			    kzalloc(sizeof(struct __vxge_hw_blockpool_entry),
					GFP_KERNEL);
		if (entry != NULL) {
			list_del(&entry->item);
			entry->length = VXGE_HW_BLOCK_SIZE;
			entry->memblock = memblock;
			entry->dma_addr = dma_addr;
			entry->acc_handle = acc_handle;
			entry->dma_handle = dma_handle;
			list_add(&entry->item,
					  &blockpool->free_block_list);
			blockpool->pool_size++;
		} else {
			__vxge_hw_blockpool_destroy(blockpool);
			status = VXGE_HW_ERR_OUT_OF_MEMORY;
			goto blockpool_create_exit;
		}
	}

blockpool_create_exit:
	return status;
}

/*
 * __vxge_hw_device_fifo_config_check - Check fifo configuration.
 * Check the fifo configuration
 */
static enum vxge_hw_status
__vxge_hw_device_fifo_config_check(struct vxge_hw_fifo_config *fifo_config)
{
	if ((fifo_config->fifo_blocks < VXGE_HW_MIN_FIFO_BLOCKS) ||
	    (fifo_config->fifo_blocks > VXGE_HW_MAX_FIFO_BLOCKS))
		return VXGE_HW_BADCFG_FIFO_BLOCKS;

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_device_vpath_config_check - Check vpath configuration.
 * Check the vpath configuration
 */
static enum vxge_hw_status
__vxge_hw_device_vpath_config_check(struct vxge_hw_vp_config *vp_config)
{
	enum vxge_hw_status status;

	if ((vp_config->min_bandwidth < VXGE_HW_VPATH_BANDWIDTH_MIN) ||
	    (vp_config->min_bandwidth >	VXGE_HW_VPATH_BANDWIDTH_MAX))
		return VXGE_HW_BADCFG_VPATH_MIN_BANDWIDTH;

	status = __vxge_hw_device_fifo_config_check(&vp_config->fifo);
	if (status != VXGE_HW_OK)
		return status;

	if ((vp_config->mtu != VXGE_HW_VPATH_USE_FLASH_DEFAULT_INITIAL_MTU) &&
		((vp_config->mtu < VXGE_HW_VPATH_MIN_INITIAL_MTU) ||
		(vp_config->mtu > VXGE_HW_VPATH_MAX_INITIAL_MTU)))
		return VXGE_HW_BADCFG_VPATH_MTU;

	if ((vp_config->rpa_strip_vlan_tag !=
		VXGE_HW_VPATH_RPA_STRIP_VLAN_TAG_USE_FLASH_DEFAULT) &&
		(vp_config->rpa_strip_vlan_tag !=
		VXGE_HW_VPATH_RPA_STRIP_VLAN_TAG_ENABLE) &&
		(vp_config->rpa_strip_vlan_tag !=
		VXGE_HW_VPATH_RPA_STRIP_VLAN_TAG_DISABLE))
		return VXGE_HW_BADCFG_VPATH_RPA_STRIP_VLAN_TAG;

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_device_config_check - Check device configuration.
 * Check the device configuration
 */
static enum vxge_hw_status
__vxge_hw_device_config_check(struct vxge_hw_device_config *new_config)
{
	u32 i;
	enum vxge_hw_status status;

	if ((new_config->intr_mode != VXGE_HW_INTR_MODE_IRQLINE) &&
	    (new_config->intr_mode != VXGE_HW_INTR_MODE_MSIX) &&
	    (new_config->intr_mode != VXGE_HW_INTR_MODE_MSIX_ONE_SHOT) &&
	    (new_config->intr_mode != VXGE_HW_INTR_MODE_DEF))
		return VXGE_HW_BADCFG_INTR_MODE;

	if ((new_config->rts_mac_en != VXGE_HW_RTS_MAC_DISABLE) &&
	    (new_config->rts_mac_en != VXGE_HW_RTS_MAC_ENABLE))
		return VXGE_HW_BADCFG_RTS_MAC_EN;

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {
		status = __vxge_hw_device_vpath_config_check(
				&new_config->vp_config[i]);
		if (status != VXGE_HW_OK)
			return status;
	}

	return VXGE_HW_OK;
}

/*
 * vxge_hw_device_initialize - Initialize Titan device.
 * Initialize Titan device. Note that all the arguments of this public API
 * are 'IN', including @hldev. Driver cooperates with
 * OS to find new Titan device, locate its PCI and memory spaces.
 *
 * When done, the driver allocates sizeof(struct __vxge_hw_device) bytes for HW
 * to enable the latter to perform Titan hardware initialization.
 */
enum vxge_hw_status
vxge_hw_device_initialize(
	struct __vxge_hw_device **devh,
	struct vxge_hw_device_attr *attr,
	struct vxge_hw_device_config *device_config)
{
	u32 i;
	u32 nblocks = 0;
	struct __vxge_hw_device *hldev = NULL;
	enum vxge_hw_status status = VXGE_HW_OK;

	status = __vxge_hw_device_config_check(device_config);
	if (status != VXGE_HW_OK)
		goto exit;

	hldev = vzalloc(sizeof(struct __vxge_hw_device));
	if (hldev == NULL) {
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		goto exit;
	}

	hldev->magic = VXGE_HW_DEVICE_MAGIC;

	vxge_hw_device_debug_set(hldev, VXGE_ERR, VXGE_COMPONENT_ALL);

	/* apply config */
	memcpy(&hldev->config, device_config,
		sizeof(struct vxge_hw_device_config));

	hldev->bar0 = attr->bar0;
	hldev->pdev = attr->pdev;

	hldev->uld_callbacks = attr->uld_callbacks;

	__vxge_hw_device_pci_e_init(hldev);

	status = __vxge_hw_device_reg_addr_get(hldev);
	if (status != VXGE_HW_OK) {
		vfree(hldev);
		goto exit;
	}

	__vxge_hw_device_host_info_get(hldev);

	/* Incrementing for stats blocks */
	nblocks++;

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {
		if (!(hldev->vpath_assignments & vxge_mBIT(i)))
			continue;

		if (device_config->vp_config[i].ring.enable ==
			VXGE_HW_RING_ENABLE)
			nblocks += device_config->vp_config[i].ring.ring_blocks;

		if (device_config->vp_config[i].fifo.enable ==
			VXGE_HW_FIFO_ENABLE)
			nblocks += device_config->vp_config[i].fifo.fifo_blocks;
		nblocks++;
	}

	if (__vxge_hw_blockpool_create(hldev,
		&hldev->block_pool,
		device_config->dma_blockpool_initial + nblocks,
		device_config->dma_blockpool_max + nblocks) != VXGE_HW_OK) {

		vxge_hw_device_terminate(hldev);
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		goto exit;
	}

	status = __vxge_hw_device_initialize(hldev);
	if (status != VXGE_HW_OK) {
		vxge_hw_device_terminate(hldev);
		goto exit;
	}

	*devh = hldev;
exit:
	return status;
}

/*
 * vxge_hw_device_terminate - Terminate Titan device.
 * Terminate HW device.
 */
void
vxge_hw_device_terminate(struct __vxge_hw_device *hldev)
{
	vxge_assert(hldev->magic == VXGE_HW_DEVICE_MAGIC);

	hldev->magic = VXGE_HW_DEVICE_DEAD;
	__vxge_hw_blockpool_destroy(&hldev->block_pool);
	vfree(hldev);
}

/*
 * __vxge_hw_vpath_stats_access - Get the statistics from the given location
 *                           and offset and perform an operation
 */
static enum vxge_hw_status
__vxge_hw_vpath_stats_access(struct __vxge_hw_virtualpath *vpath,
			     u32 operation, u32 offset, u64 *stat)
{
	u64 val64;
	enum vxge_hw_status status = VXGE_HW_OK;
	struct vxge_hw_vpath_reg __iomem *vp_reg;

	if (vpath->vp_open == VXGE_HW_VP_NOT_OPEN) {
		status = VXGE_HW_ERR_VPATH_NOT_OPEN;
		goto vpath_stats_access_exit;
	}

	vp_reg = vpath->vp_reg;

	val64 =  VXGE_HW_XMAC_STATS_ACCESS_CMD_OP(operation) |
		 VXGE_HW_XMAC_STATS_ACCESS_CMD_STROBE |
		 VXGE_HW_XMAC_STATS_ACCESS_CMD_OFFSET_SEL(offset);

	status = __vxge_hw_pio_mem_write64(val64,
				&vp_reg->xmac_stats_access_cmd,
				VXGE_HW_XMAC_STATS_ACCESS_CMD_STROBE,
				vpath->hldev->config.device_poll_millis);
	if ((status == VXGE_HW_OK) && (operation == VXGE_HW_STATS_OP_READ))
		*stat = readq(&vp_reg->xmac_stats_access_data);
	else
		*stat = 0;

vpath_stats_access_exit:
	return status;
}

/*
 * __vxge_hw_vpath_xmac_tx_stats_get - Get the TX Statistics of a vpath
 */
static enum vxge_hw_status
__vxge_hw_vpath_xmac_tx_stats_get(struct __vxge_hw_virtualpath *vpath,
			struct vxge_hw_xmac_vpath_tx_stats *vpath_tx_stats)
{
	u64 *val64;
	int i;
	u32 offset = VXGE_HW_STATS_VPATH_TX_OFFSET;
	enum vxge_hw_status status = VXGE_HW_OK;

	val64 = (u64 *)vpath_tx_stats;

	if (vpath->vp_open == VXGE_HW_VP_NOT_OPEN) {
		status = VXGE_HW_ERR_VPATH_NOT_OPEN;
		goto exit;
	}

	for (i = 0; i < sizeof(struct vxge_hw_xmac_vpath_tx_stats) / 8; i++) {
		status = __vxge_hw_vpath_stats_access(vpath,
					VXGE_HW_STATS_OP_READ,
					offset, val64);
		if (status != VXGE_HW_OK)
			goto exit;
		offset++;
		val64++;
	}
exit:
	return status;
}

/*
 * __vxge_hw_vpath_xmac_rx_stats_get - Get the RX Statistics of a vpath
 */
static enum vxge_hw_status
__vxge_hw_vpath_xmac_rx_stats_get(struct __vxge_hw_virtualpath *vpath,
			struct vxge_hw_xmac_vpath_rx_stats *vpath_rx_stats)
{
	u64 *val64;
	enum vxge_hw_status status = VXGE_HW_OK;
	int i;
	u32 offset = VXGE_HW_STATS_VPATH_RX_OFFSET;
	val64 = (u64 *) vpath_rx_stats;

	if (vpath->vp_open == VXGE_HW_VP_NOT_OPEN) {
		status = VXGE_HW_ERR_VPATH_NOT_OPEN;
		goto exit;
	}
	for (i = 0; i < sizeof(struct vxge_hw_xmac_vpath_rx_stats) / 8; i++) {
		status = __vxge_hw_vpath_stats_access(vpath,
					VXGE_HW_STATS_OP_READ,
					offset >> 3, val64);
		if (status != VXGE_HW_OK)
			goto exit;

		offset += 8;
		val64++;
	}
exit:
	return status;
}

/*
 * __vxge_hw_vpath_stats_get - Get the vpath hw statistics.
 */
static enum vxge_hw_status
__vxge_hw_vpath_stats_get(struct __vxge_hw_virtualpath *vpath,
			  struct vxge_hw_vpath_stats_hw_info *hw_stats)
{
	u64 val64;
	enum vxge_hw_status status = VXGE_HW_OK;
	struct vxge_hw_vpath_reg __iomem *vp_reg;

	if (vpath->vp_open == VXGE_HW_VP_NOT_OPEN) {
		status = VXGE_HW_ERR_VPATH_NOT_OPEN;
		goto exit;
	}
	vp_reg = vpath->vp_reg;

	val64 = readq(&vp_reg->vpath_debug_stats0);
	hw_stats->ini_num_mwr_sent =
		(u32)VXGE_HW_VPATH_DEBUG_STATS0_GET_INI_NUM_MWR_SENT(val64);

	val64 = readq(&vp_reg->vpath_debug_stats1);
	hw_stats->ini_num_mrd_sent =
		(u32)VXGE_HW_VPATH_DEBUG_STATS1_GET_INI_NUM_MRD_SENT(val64);

	val64 = readq(&vp_reg->vpath_debug_stats2);
	hw_stats->ini_num_cpl_rcvd =
		(u32)VXGE_HW_VPATH_DEBUG_STATS2_GET_INI_NUM_CPL_RCVD(val64);

	val64 = readq(&vp_reg->vpath_debug_stats3);
	hw_stats->ini_num_mwr_byte_sent =
		VXGE_HW_VPATH_DEBUG_STATS3_GET_INI_NUM_MWR_BYTE_SENT(val64);

	val64 = readq(&vp_reg->vpath_debug_stats4);
	hw_stats->ini_num_cpl_byte_rcvd =
		VXGE_HW_VPATH_DEBUG_STATS4_GET_INI_NUM_CPL_BYTE_RCVD(val64);

	val64 = readq(&vp_reg->vpath_debug_stats5);
	hw_stats->wrcrdtarb_xoff =
		(u32)VXGE_HW_VPATH_DEBUG_STATS5_GET_WRCRDTARB_XOFF(val64);

	val64 = readq(&vp_reg->vpath_debug_stats6);
	hw_stats->rdcrdtarb_xoff =
		(u32)VXGE_HW_VPATH_DEBUG_STATS6_GET_RDCRDTARB_XOFF(val64);

	val64 = readq(&vp_reg->vpath_genstats_count01);
	hw_stats->vpath_genstats_count0 =
	(u32)VXGE_HW_VPATH_GENSTATS_COUNT01_GET_PPIF_VPATH_GENSTATS_COUNT0(
		val64);

	val64 = readq(&vp_reg->vpath_genstats_count01);
	hw_stats->vpath_genstats_count1 =
	(u32)VXGE_HW_VPATH_GENSTATS_COUNT01_GET_PPIF_VPATH_GENSTATS_COUNT1(
		val64);

	val64 = readq(&vp_reg->vpath_genstats_count23);
	hw_stats->vpath_genstats_count2 =
	(u32)VXGE_HW_VPATH_GENSTATS_COUNT23_GET_PPIF_VPATH_GENSTATS_COUNT2(
		val64);

	val64 = readq(&vp_reg->vpath_genstats_count01);
	hw_stats->vpath_genstats_count3 =
	(u32)VXGE_HW_VPATH_GENSTATS_COUNT23_GET_PPIF_VPATH_GENSTATS_COUNT3(
		val64);

	val64 = readq(&vp_reg->vpath_genstats_count4);
	hw_stats->vpath_genstats_count4 =
	(u32)VXGE_HW_VPATH_GENSTATS_COUNT4_GET_PPIF_VPATH_GENSTATS_COUNT4(
		val64);

	val64 = readq(&vp_reg->vpath_genstats_count5);
	hw_stats->vpath_genstats_count5 =
	(u32)VXGE_HW_VPATH_GENSTATS_COUNT5_GET_PPIF_VPATH_GENSTATS_COUNT5(
		val64);

	status = __vxge_hw_vpath_xmac_tx_stats_get(vpath, &hw_stats->tx_stats);
	if (status != VXGE_HW_OK)
		goto exit;

	status = __vxge_hw_vpath_xmac_rx_stats_get(vpath, &hw_stats->rx_stats);
	if (status != VXGE_HW_OK)
		goto exit;

	VXGE_HW_VPATH_STATS_PIO_READ(
		VXGE_HW_STATS_VPATH_PROG_EVENT_VNUM0_OFFSET);

	hw_stats->prog_event_vnum0 =
			(u32)VXGE_HW_STATS_GET_VPATH_PROG_EVENT_VNUM0(val64);

	hw_stats->prog_event_vnum1 =
			(u32)VXGE_HW_STATS_GET_VPATH_PROG_EVENT_VNUM1(val64);

	VXGE_HW_VPATH_STATS_PIO_READ(
		VXGE_HW_STATS_VPATH_PROG_EVENT_VNUM2_OFFSET);

	hw_stats->prog_event_vnum2 =
			(u32)VXGE_HW_STATS_GET_VPATH_PROG_EVENT_VNUM2(val64);

	hw_stats->prog_event_vnum3 =
			(u32)VXGE_HW_STATS_GET_VPATH_PROG_EVENT_VNUM3(val64);

	val64 = readq(&vp_reg->rx_multi_cast_stats);
	hw_stats->rx_multi_cast_frame_discard =
		(u16)VXGE_HW_RX_MULTI_CAST_STATS_GET_FRAME_DISCARD(val64);

	val64 = readq(&vp_reg->rx_frm_transferred);
	hw_stats->rx_frm_transferred =
		(u32)VXGE_HW_RX_FRM_TRANSFERRED_GET_RX_FRM_TRANSFERRED(val64);

	val64 = readq(&vp_reg->rxd_returned);
	hw_stats->rxd_returned =
		(u16)VXGE_HW_RXD_RETURNED_GET_RXD_RETURNED(val64);

	val64 = readq(&vp_reg->dbg_stats_rx_mpa);
	hw_stats->rx_mpa_len_fail_frms =
		(u16)VXGE_HW_DBG_STATS_GET_RX_MPA_LEN_FAIL_FRMS(val64);
	hw_stats->rx_mpa_mrk_fail_frms =
		(u16)VXGE_HW_DBG_STATS_GET_RX_MPA_MRK_FAIL_FRMS(val64);
	hw_stats->rx_mpa_crc_fail_frms =
		(u16)VXGE_HW_DBG_STATS_GET_RX_MPA_CRC_FAIL_FRMS(val64);

	val64 = readq(&vp_reg->dbg_stats_rx_fau);
	hw_stats->rx_permitted_frms =
		(u16)VXGE_HW_DBG_STATS_GET_RX_FAU_RX_PERMITTED_FRMS(val64);
	hw_stats->rx_vp_reset_discarded_frms =
	(u16)VXGE_HW_DBG_STATS_GET_RX_FAU_RX_VP_RESET_DISCARDED_FRMS(val64);
	hw_stats->rx_wol_frms =
		(u16)VXGE_HW_DBG_STATS_GET_RX_FAU_RX_WOL_FRMS(val64);

	val64 = readq(&vp_reg->tx_vp_reset_discarded_frms);
	hw_stats->tx_vp_reset_discarded_frms =
	(u16)VXGE_HW_TX_VP_RESET_DISCARDED_FRMS_GET_TX_VP_RESET_DISCARDED_FRMS(
		val64);
exit:
	return status;
}

/*
 * vxge_hw_device_stats_get - Get the device hw statistics.
 * Returns the vpath h/w stats for the device.
 */
enum vxge_hw_status
vxge_hw_device_stats_get(struct __vxge_hw_device *hldev,
			struct vxge_hw_device_stats_hw_info *hw_stats)
{
	u32 i;
	enum vxge_hw_status status = VXGE_HW_OK;

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {
		if (!(hldev->vpaths_deployed & vxge_mBIT(i)) ||
			(hldev->virtual_paths[i].vp_open ==
				VXGE_HW_VP_NOT_OPEN))
			continue;

		memcpy(hldev->virtual_paths[i].hw_stats_sav,
				hldev->virtual_paths[i].hw_stats,
				sizeof(struct vxge_hw_vpath_stats_hw_info));

		status = __vxge_hw_vpath_stats_get(
			&hldev->virtual_paths[i],
			hldev->virtual_paths[i].hw_stats);
	}

	memcpy(hw_stats, &hldev->stats.hw_dev_info_stats,
			sizeof(struct vxge_hw_device_stats_hw_info));

	return status;
}

/*
 * vxge_hw_driver_stats_get - Get the device sw statistics.
 * Returns the vpath s/w stats for the device.
 */
enum vxge_hw_status vxge_hw_driver_stats_get(
			struct __vxge_hw_device *hldev,
			struct vxge_hw_device_stats_sw_info *sw_stats)
{
	memcpy(sw_stats, &hldev->stats.sw_dev_info_stats,
		sizeof(struct vxge_hw_device_stats_sw_info));

	return VXGE_HW_OK;
}

/*
 * vxge_hw_mrpcim_stats_access - Access the statistics from the given location
 *                           and offset and perform an operation
 * Get the statistics from the given location and offset.
 */
enum vxge_hw_status
vxge_hw_mrpcim_stats_access(struct __vxge_hw_device *hldev,
			    u32 operation, u32 location, u32 offset, u64 *stat)
{
	u64 val64;
	enum vxge_hw_status status = VXGE_HW_OK;

	status = __vxge_hw_device_is_privilaged(hldev->host_type,
			hldev->func_id);
	if (status != VXGE_HW_OK)
		goto exit;

	val64 = VXGE_HW_XMAC_STATS_SYS_CMD_OP(operation) |
		VXGE_HW_XMAC_STATS_SYS_CMD_STROBE |
		VXGE_HW_XMAC_STATS_SYS_CMD_LOC_SEL(location) |
		VXGE_HW_XMAC_STATS_SYS_CMD_OFFSET_SEL(offset);

	status = __vxge_hw_pio_mem_write64(val64,
				&hldev->mrpcim_reg->xmac_stats_sys_cmd,
				VXGE_HW_XMAC_STATS_SYS_CMD_STROBE,
				hldev->config.device_poll_millis);

	if ((status == VXGE_HW_OK) && (operation == VXGE_HW_STATS_OP_READ))
		*stat = readq(&hldev->mrpcim_reg->xmac_stats_sys_data);
	else
		*stat = 0;
exit:
	return status;
}

/*
 * vxge_hw_device_xmac_aggr_stats_get - Get the Statistics on aggregate port
 * Get the Statistics on aggregate port
 */
static enum vxge_hw_status
vxge_hw_device_xmac_aggr_stats_get(struct __vxge_hw_device *hldev, u32 port,
				   struct vxge_hw_xmac_aggr_stats *aggr_stats)
{
	u64 *val64;
	int i;
	u32 offset = VXGE_HW_STATS_AGGRn_OFFSET;
	enum vxge_hw_status status = VXGE_HW_OK;

	val64 = (u64 *)aggr_stats;

	status = __vxge_hw_device_is_privilaged(hldev->host_type,
			hldev->func_id);
	if (status != VXGE_HW_OK)
		goto exit;

	for (i = 0; i < sizeof(struct vxge_hw_xmac_aggr_stats) / 8; i++) {
		status = vxge_hw_mrpcim_stats_access(hldev,
					VXGE_HW_STATS_OP_READ,
					VXGE_HW_STATS_LOC_AGGR,
					((offset + (104 * port)) >> 3), val64);
		if (status != VXGE_HW_OK)
			goto exit;

		offset += 8;
		val64++;
	}
exit:
	return status;
}

/*
 * vxge_hw_device_xmac_port_stats_get - Get the Statistics on a port
 * Get the Statistics on port
 */
static enum vxge_hw_status
vxge_hw_device_xmac_port_stats_get(struct __vxge_hw_device *hldev, u32 port,
				   struct vxge_hw_xmac_port_stats *port_stats)
{
	u64 *val64;
	enum vxge_hw_status status = VXGE_HW_OK;
	int i;
	u32 offset = 0x0;
	val64 = (u64 *) port_stats;

	status = __vxge_hw_device_is_privilaged(hldev->host_type,
			hldev->func_id);
	if (status != VXGE_HW_OK)
		goto exit;

	for (i = 0; i < sizeof(struct vxge_hw_xmac_port_stats) / 8; i++) {
		status = vxge_hw_mrpcim_stats_access(hldev,
					VXGE_HW_STATS_OP_READ,
					VXGE_HW_STATS_LOC_AGGR,
					((offset + (608 * port)) >> 3), val64);
		if (status != VXGE_HW_OK)
			goto exit;

		offset += 8;
		val64++;
	}

exit:
	return status;
}

/*
 * vxge_hw_device_xmac_stats_get - Get the XMAC Statistics
 * Get the XMAC Statistics
 */
enum vxge_hw_status
vxge_hw_device_xmac_stats_get(struct __vxge_hw_device *hldev,
			      struct vxge_hw_xmac_stats *xmac_stats)
{
	enum vxge_hw_status status = VXGE_HW_OK;
	u32 i;

	status = vxge_hw_device_xmac_aggr_stats_get(hldev,
					0, &xmac_stats->aggr_stats[0]);
	if (status != VXGE_HW_OK)
		goto exit;

	status = vxge_hw_device_xmac_aggr_stats_get(hldev,
				1, &xmac_stats->aggr_stats[1]);
	if (status != VXGE_HW_OK)
		goto exit;

	for (i = 0; i <= VXGE_HW_MAC_MAX_MAC_PORT_ID; i++) {

		status = vxge_hw_device_xmac_port_stats_get(hldev,
					i, &xmac_stats->port_stats[i]);
		if (status != VXGE_HW_OK)
			goto exit;
	}

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {

		if (!(hldev->vpaths_deployed & vxge_mBIT(i)))
			continue;

		status = __vxge_hw_vpath_xmac_tx_stats_get(
					&hldev->virtual_paths[i],
					&xmac_stats->vpath_tx_stats[i]);
		if (status != VXGE_HW_OK)
			goto exit;

		status = __vxge_hw_vpath_xmac_rx_stats_get(
					&hldev->virtual_paths[i],
					&xmac_stats->vpath_rx_stats[i]);
		if (status != VXGE_HW_OK)
			goto exit;
	}
exit:
	return status;
}

/*
 * vxge_hw_device_debug_set - Set the debug module, level and timestamp
 * This routine is used to dynamically change the debug output
 */
void vxge_hw_device_debug_set(struct __vxge_hw_device *hldev,
			      enum vxge_debug_level level, u32 mask)
{
	if (hldev == NULL)
		return;

#if defined(VXGE_DEBUG_TRACE_MASK) || \
	defined(VXGE_DEBUG_ERR_MASK)
	hldev->debug_module_mask = mask;
	hldev->debug_level = level;
#endif

#if defined(VXGE_DEBUG_ERR_MASK)
	hldev->level_err = level & VXGE_ERR;
#endif

#if defined(VXGE_DEBUG_TRACE_MASK)
	hldev->level_trace = level & VXGE_TRACE;
#endif
}

/*
 * vxge_hw_device_error_level_get - Get the error level
 * This routine returns the current error level set
 */
u32 vxge_hw_device_error_level_get(struct __vxge_hw_device *hldev)
{
#if defined(VXGE_DEBUG_ERR_MASK)
	if (hldev == NULL)
		return VXGE_ERR;
	else
		return hldev->level_err;
#else
	return 0;
#endif
}

/*
 * vxge_hw_device_trace_level_get - Get the trace level
 * This routine returns the current trace level set
 */
u32 vxge_hw_device_trace_level_get(struct __vxge_hw_device *hldev)
{
#if defined(VXGE_DEBUG_TRACE_MASK)
	if (hldev == NULL)
		return VXGE_TRACE;
	else
		return hldev->level_trace;
#else
	return 0;
#endif
}

/*
 * vxge_hw_getpause_data -Pause frame frame generation and reception.
 * Returns the Pause frame generation and reception capability of the NIC.
 */
enum vxge_hw_status vxge_hw_device_getpause_data(struct __vxge_hw_device *hldev,
						 u32 port, u32 *tx, u32 *rx)
{
	u64 val64;
	enum vxge_hw_status status = VXGE_HW_OK;

	if ((hldev == NULL) || (hldev->magic != VXGE_HW_DEVICE_MAGIC)) {
		status = VXGE_HW_ERR_INVALID_DEVICE;
		goto exit;
	}

	if (port > VXGE_HW_MAC_MAX_MAC_PORT_ID) {
		status = VXGE_HW_ERR_INVALID_PORT;
		goto exit;
	}

	if (!(hldev->access_rights & VXGE_HW_DEVICE_ACCESS_RIGHT_MRPCIM)) {
		status = VXGE_HW_ERR_PRIVILEGED_OPERATION;
		goto exit;
	}

	val64 = readq(&hldev->mrpcim_reg->rxmac_pause_cfg_port[port]);
	if (val64 & VXGE_HW_RXMAC_PAUSE_CFG_PORT_GEN_EN)
		*tx = 1;
	if (val64 & VXGE_HW_RXMAC_PAUSE_CFG_PORT_RCV_EN)
		*rx = 1;
exit:
	return status;
}

/*
 * vxge_hw_device_setpause_data -  set/reset pause frame generation.
 * It can be used to set or reset Pause frame generation or reception
 * support of the NIC.
 */
enum vxge_hw_status vxge_hw_device_setpause_data(struct __vxge_hw_device *hldev,
						 u32 port, u32 tx, u32 rx)
{
	u64 val64;
	enum vxge_hw_status status = VXGE_HW_OK;

	if ((hldev == NULL) || (hldev->magic != VXGE_HW_DEVICE_MAGIC)) {
		status = VXGE_HW_ERR_INVALID_DEVICE;
		goto exit;
	}

	if (port > VXGE_HW_MAC_MAX_MAC_PORT_ID) {
		status = VXGE_HW_ERR_INVALID_PORT;
		goto exit;
	}

	status = __vxge_hw_device_is_privilaged(hldev->host_type,
			hldev->func_id);
	if (status != VXGE_HW_OK)
		goto exit;

	val64 = readq(&hldev->mrpcim_reg->rxmac_pause_cfg_port[port]);
	if (tx)
		val64 |= VXGE_HW_RXMAC_PAUSE_CFG_PORT_GEN_EN;
	else
		val64 &= ~VXGE_HW_RXMAC_PAUSE_CFG_PORT_GEN_EN;
	if (rx)
		val64 |= VXGE_HW_RXMAC_PAUSE_CFG_PORT_RCV_EN;
	else
		val64 &= ~VXGE_HW_RXMAC_PAUSE_CFG_PORT_RCV_EN;

	writeq(val64, &hldev->mrpcim_reg->rxmac_pause_cfg_port[port]);
exit:
	return status;
}

u16 vxge_hw_device_link_width_get(struct __vxge_hw_device *hldev)
{
	struct pci_dev *dev = hldev->pdev;
	u16 lnk;

	pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &lnk);
	return (lnk & VXGE_HW_PCI_EXP_LNKCAP_LNK_WIDTH) >> 4;
}

/*
 * __vxge_hw_ring_block_memblock_idx - Return the memblock index
 * This function returns the index of memory block
 */
static inline u32
__vxge_hw_ring_block_memblock_idx(u8 *block)
{
	return (u32)*((u64 *)(block + VXGE_HW_RING_MEMBLOCK_IDX_OFFSET));
}

/*
 * __vxge_hw_ring_block_memblock_idx_set - Sets the memblock index
 * This function sets index to a memory block
 */
static inline void
__vxge_hw_ring_block_memblock_idx_set(u8 *block, u32 memblock_idx)
{
	*((u64 *)(block + VXGE_HW_RING_MEMBLOCK_IDX_OFFSET)) = memblock_idx;
}

/*
 * __vxge_hw_ring_block_next_pointer_set - Sets the next block pointer
 * in RxD block
 * Sets the next block pointer in RxD block
 */
static inline void
__vxge_hw_ring_block_next_pointer_set(u8 *block, dma_addr_t dma_next)
{
	*((u64 *)(block + VXGE_HW_RING_NEXT_BLOCK_POINTER_OFFSET)) = dma_next;
}

/*
 * __vxge_hw_ring_first_block_address_get - Returns the dma address of the
 *             first block
 * Returns the dma address of the first RxD block
 */
static u64 __vxge_hw_ring_first_block_address_get(struct __vxge_hw_ring *ring)
{
	struct vxge_hw_mempool_dma *dma_object;

	dma_object = ring->mempool->memblocks_dma_arr;
	vxge_assert(dma_object != NULL);

	return dma_object->addr;
}

/*
 * __vxge_hw_ring_item_dma_addr - Return the dma address of an item
 * This function returns the dma address of a given item
 */
static dma_addr_t __vxge_hw_ring_item_dma_addr(struct vxge_hw_mempool *mempoolh,
					       void *item)
{
	u32 memblock_idx;
	void *memblock;
	struct vxge_hw_mempool_dma *memblock_dma_object;
	ptrdiff_t dma_item_offset;

	/* get owner memblock index */
	memblock_idx = __vxge_hw_ring_block_memblock_idx(item);

	/* get owner memblock by memblock index */
	memblock = mempoolh->memblocks_arr[memblock_idx];

	/* get memblock DMA object by memblock index */
	memblock_dma_object = mempoolh->memblocks_dma_arr + memblock_idx;

	/* calculate offset in the memblock of this item */
	dma_item_offset = (u8 *)item - (u8 *)memblock;

	return memblock_dma_object->addr + dma_item_offset;
}

/*
 * __vxge_hw_ring_rxdblock_link - Link the RxD blocks
 * This function returns the dma address of a given item
 */
static void __vxge_hw_ring_rxdblock_link(struct vxge_hw_mempool *mempoolh,
					 struct __vxge_hw_ring *ring, u32 from,
					 u32 to)
{
	u8 *to_item , *from_item;
	dma_addr_t to_dma;

	/* get "from" RxD block */
	from_item = mempoolh->items_arr[from];
	vxge_assert(from_item);

	/* get "to" RxD block */
	to_item = mempoolh->items_arr[to];
	vxge_assert(to_item);

	/* return address of the beginning of previous RxD block */
	to_dma = __vxge_hw_ring_item_dma_addr(mempoolh, to_item);

	/* set next pointer for this RxD block to point on
	 * previous item's DMA start address */
	__vxge_hw_ring_block_next_pointer_set(from_item, to_dma);
}

/*
 * __vxge_hw_ring_mempool_item_alloc - Allocate List blocks for RxD
 * block callback
 * This function is callback passed to __vxge_hw_mempool_create to create memory
 * pool for RxD block
 */
static void
__vxge_hw_ring_mempool_item_alloc(struct vxge_hw_mempool *mempoolh,
				  u32 memblock_index,
				  struct vxge_hw_mempool_dma *dma_object,
				  u32 index, u32 is_last)
{
	u32 i;
	void *item = mempoolh->items_arr[index];
	struct __vxge_hw_ring *ring =
		(struct __vxge_hw_ring *)mempoolh->userdata;

	/* format rxds array */
	for (i = 0; i < ring->rxds_per_block; i++) {
		void *rxdblock_priv;
		void *uld_priv;
		struct vxge_hw_ring_rxd_1 *rxdp;

		u32 reserve_index = ring->channel.reserve_ptr -
				(index * ring->rxds_per_block + i + 1);
		u32 memblock_item_idx;

		ring->channel.reserve_arr[reserve_index] = ((u8 *)item) +
						i * ring->rxd_size;

		/* Note: memblock_item_idx is index of the item within
		 *       the memblock. For instance, in case of three RxD-blocks
		 *       per memblock this value can be 0, 1 or 2. */
		rxdblock_priv = __vxge_hw_mempool_item_priv(mempoolh,
					memblock_index, item,
					&memblock_item_idx);

		rxdp = ring->channel.reserve_arr[reserve_index];

		uld_priv = ((u8 *)rxdblock_priv + ring->rxd_priv_size * i);

		/* pre-format Host_Control */
		rxdp->host_control = (u64)(size_t)uld_priv;
	}

	__vxge_hw_ring_block_memblock_idx_set(item, memblock_index);

	if (is_last) {
		/* link last one with first one */
		__vxge_hw_ring_rxdblock_link(mempoolh, ring, index, 0);
	}

	if (index > 0) {
		/* link this RxD block with previous one */
		__vxge_hw_ring_rxdblock_link(mempoolh, ring, index - 1, index);
	}
}

/*
 * __vxge_hw_ring_replenish - Initial replenish of RxDs
 * This function replenishes the RxDs from reserve array to work array
 */
static enum vxge_hw_status
vxge_hw_ring_replenish(struct __vxge_hw_ring *ring)
{
	void *rxd;
	struct __vxge_hw_channel *channel;
	enum vxge_hw_status status = VXGE_HW_OK;

	channel = &ring->channel;

	while (vxge_hw_channel_dtr_count(channel) > 0) {

		status = vxge_hw_ring_rxd_reserve(ring, &rxd);

		vxge_assert(status == VXGE_HW_OK);

		if (ring->rxd_init) {
			status = ring->rxd_init(rxd, channel->userdata);
			if (status != VXGE_HW_OK) {
				vxge_hw_ring_rxd_free(ring, rxd);
				goto exit;
			}
		}

		vxge_hw_ring_rxd_post(ring, rxd);
	}
	status = VXGE_HW_OK;
exit:
	return status;
}

/*
 * __vxge_hw_channel_allocate - Allocate memory for channel
 * This function allocates required memory for the channel and various arrays
 * in the channel
 */
static struct __vxge_hw_channel *
__vxge_hw_channel_allocate(struct __vxge_hw_vpath_handle *vph,
			   enum __vxge_hw_channel_type type,
			   u32 length, u32 per_dtr_space,
			   void *userdata)
{
	struct __vxge_hw_channel *channel;
	struct __vxge_hw_device *hldev;
	int size = 0;
	u32 vp_id;

	hldev = vph->vpath->hldev;
	vp_id = vph->vpath->vp_id;

	switch (type) {
	case VXGE_HW_CHANNEL_TYPE_FIFO:
		size = sizeof(struct __vxge_hw_fifo);
		break;
	case VXGE_HW_CHANNEL_TYPE_RING:
		size = sizeof(struct __vxge_hw_ring);
		break;
	default:
		break;
	}

	channel = kzalloc(size, GFP_KERNEL);
	if (channel == NULL)
		goto exit0;
	INIT_LIST_HEAD(&channel->item);

	channel->common_reg = hldev->common_reg;
	channel->first_vp_id = hldev->first_vp_id;
	channel->type = type;
	channel->devh = hldev;
	channel->vph = vph;
	channel->userdata = userdata;
	channel->per_dtr_space = per_dtr_space;
	channel->length = length;
	channel->vp_id = vp_id;

	channel->work_arr = kcalloc(length, sizeof(void *), GFP_KERNEL);
	if (channel->work_arr == NULL)
		goto exit1;

	channel->free_arr = kcalloc(length, sizeof(void *), GFP_KERNEL);
	if (channel->free_arr == NULL)
		goto exit1;
	channel->free_ptr = length;

	channel->reserve_arr = kcalloc(length, sizeof(void *), GFP_KERNEL);
	if (channel->reserve_arr == NULL)
		goto exit1;
	channel->reserve_ptr = length;
	channel->reserve_top = 0;

	channel->orig_arr = kcalloc(length, sizeof(void *), GFP_KERNEL);
	if (channel->orig_arr == NULL)
		goto exit1;

	return channel;
exit1:
	__vxge_hw_channel_free(channel);

exit0:
	return NULL;
}

/*
 * vxge_hw_blockpool_block_add - callback for vxge_os_dma_malloc_async
 * Adds a block to block pool
 */
static void vxge_hw_blockpool_block_add(struct __vxge_hw_device *devh,
					void *block_addr,
					u32 length,
					struct pci_dev *dma_h,
					struct pci_dev *acc_handle)
{
	struct __vxge_hw_blockpool *blockpool;
	struct __vxge_hw_blockpool_entry *entry = NULL;
	dma_addr_t dma_addr;

	blockpool = &devh->block_pool;

	if (block_addr == NULL) {
		blockpool->req_out--;
		goto exit;
	}

	dma_addr = pci_map_single(devh->pdev, block_addr, length,
				PCI_DMA_BIDIRECTIONAL);

	if (unlikely(pci_dma_mapping_error(devh->pdev, dma_addr))) {
		vxge_os_dma_free(devh->pdev, block_addr, &acc_handle);
		blockpool->req_out--;
		goto exit;
	}

	if (!list_empty(&blockpool->free_entry_list))
		entry = (struct __vxge_hw_blockpool_entry *)
			list_first_entry(&blockpool->free_entry_list,
				struct __vxge_hw_blockpool_entry,
				item);

	if (entry == NULL)
		entry =	vmalloc(sizeof(struct __vxge_hw_blockpool_entry));
	else
		list_del(&entry->item);

	if (entry) {
		entry->length = length;
		entry->memblock = block_addr;
		entry->dma_addr = dma_addr;
		entry->acc_handle = acc_handle;
		entry->dma_handle = dma_h;
		list_add(&entry->item, &blockpool->free_block_list);
		blockpool->pool_size++;
	}

	blockpool->req_out--;

exit:
	return;
}

static inline void
vxge_os_dma_malloc_async(struct pci_dev *pdev, void *devh, unsigned long size)
{
	gfp_t flags;
	void *vaddr;

	if (in_interrupt())
		flags = GFP_ATOMIC | GFP_DMA;
	else
		flags = GFP_KERNEL | GFP_DMA;

	vaddr = kmalloc((size), flags);

	vxge_hw_blockpool_block_add(devh, vaddr, size, pdev, pdev);
}

/*
 * __vxge_hw_blockpool_blocks_add - Request additional blocks
 */
static
void __vxge_hw_blockpool_blocks_add(struct __vxge_hw_blockpool *blockpool)
{
	u32 nreq = 0, i;

	if ((blockpool->pool_size  +  blockpool->req_out) <
		VXGE_HW_MIN_DMA_BLOCK_POOL_SIZE) {
		nreq = VXGE_HW_INCR_DMA_BLOCK_POOL_SIZE;
		blockpool->req_out += nreq;
	}

	for (i = 0; i < nreq; i++)
		vxge_os_dma_malloc_async(
			(blockpool->hldev)->pdev,
			blockpool->hldev, VXGE_HW_BLOCK_SIZE);
}

/*
 * __vxge_hw_blockpool_malloc - Allocate a memory block from pool
 * Allocates a block of memory of given size, either from block pool
 * or by calling vxge_os_dma_malloc()
 */
static void *__vxge_hw_blockpool_malloc(struct __vxge_hw_device *devh, u32 size,
					struct vxge_hw_mempool_dma *dma_object)
{
	struct __vxge_hw_blockpool_entry *entry = NULL;
	struct __vxge_hw_blockpool  *blockpool;
	void *memblock = NULL;

	blockpool = &devh->block_pool;

	if (size != blockpool->block_size) {

		memblock = vxge_os_dma_malloc(devh->pdev, size,
						&dma_object->handle,
						&dma_object->acc_handle);

		if (!memblock)
			goto exit;

		dma_object->addr = pci_map_single(devh->pdev, memblock, size,
					PCI_DMA_BIDIRECTIONAL);

		if (unlikely(pci_dma_mapping_error(devh->pdev,
				dma_object->addr))) {
			vxge_os_dma_free(devh->pdev, memblock,
				&dma_object->acc_handle);
			memblock = NULL;
			goto exit;
		}

	} else {

		if (!list_empty(&blockpool->free_block_list))
			entry = (struct __vxge_hw_blockpool_entry *)
				list_first_entry(&blockpool->free_block_list,
					struct __vxge_hw_blockpool_entry,
					item);

		if (entry != NULL) {
			list_del(&entry->item);
			dma_object->addr = entry->dma_addr;
			dma_object->handle = entry->dma_handle;
			dma_object->acc_handle = entry->acc_handle;
			memblock = entry->memblock;

			list_add(&entry->item,
				&blockpool->free_entry_list);
			blockpool->pool_size--;
		}

		if (memblock != NULL)
			__vxge_hw_blockpool_blocks_add(blockpool);
	}
exit:
	return memblock;
}

/*
 * __vxge_hw_blockpool_blocks_remove - Free additional blocks
 */
static void
__vxge_hw_blockpool_blocks_remove(struct __vxge_hw_blockpool *blockpool)
{
	struct list_head *p, *n;

	list_for_each_safe(p, n, &blockpool->free_block_list) {

		if (blockpool->pool_size < blockpool->pool_max)
			break;

		pci_unmap_single(
			(blockpool->hldev)->pdev,
			((struct __vxge_hw_blockpool_entry *)p)->dma_addr,
			((struct __vxge_hw_blockpool_entry *)p)->length,
			PCI_DMA_BIDIRECTIONAL);

		vxge_os_dma_free(
			(blockpool->hldev)->pdev,
			((struct __vxge_hw_blockpool_entry *)p)->memblock,
			&((struct __vxge_hw_blockpool_entry *)p)->acc_handle);

		list_del(&((struct __vxge_hw_blockpool_entry *)p)->item);

		list_add(p, &blockpool->free_entry_list);

		blockpool->pool_size--;

	}
}

/*
 * __vxge_hw_blockpool_free - Frees the memory allcoated with
 *				__vxge_hw_blockpool_malloc
 */
static void __vxge_hw_blockpool_free(struct __vxge_hw_device *devh,
				     void *memblock, u32 size,
				     struct vxge_hw_mempool_dma *dma_object)
{
	struct __vxge_hw_blockpool_entry *entry = NULL;
	struct __vxge_hw_blockpool  *blockpool;
	enum vxge_hw_status status = VXGE_HW_OK;

	blockpool = &devh->block_pool;

	if (size != blockpool->block_size) {
		pci_unmap_single(devh->pdev, dma_object->addr, size,
			PCI_DMA_BIDIRECTIONAL);
		vxge_os_dma_free(devh->pdev, memblock, &dma_object->acc_handle);
	} else {

		if (!list_empty(&blockpool->free_entry_list))
			entry = (struct __vxge_hw_blockpool_entry *)
				list_first_entry(&blockpool->free_entry_list,
					struct __vxge_hw_blockpool_entry,
					item);

		if (entry == NULL)
			entry =	vmalloc(sizeof(
					struct __vxge_hw_blockpool_entry));
		else
			list_del(&entry->item);

		if (entry != NULL) {
			entry->length = size;
			entry->memblock = memblock;
			entry->dma_addr = dma_object->addr;
			entry->acc_handle = dma_object->acc_handle;
			entry->dma_handle = dma_object->handle;
			list_add(&entry->item,
					&blockpool->free_block_list);
			blockpool->pool_size++;
			status = VXGE_HW_OK;
		} else
			status = VXGE_HW_ERR_OUT_OF_MEMORY;

		if (status == VXGE_HW_OK)
			__vxge_hw_blockpool_blocks_remove(blockpool);
	}
}

/*
 * vxge_hw_mempool_destroy
 */
static void __vxge_hw_mempool_destroy(struct vxge_hw_mempool *mempool)
{
	u32 i, j;
	struct __vxge_hw_device *devh = mempool->devh;

	for (i = 0; i < mempool->memblocks_allocated; i++) {
		struct vxge_hw_mempool_dma *dma_object;

		vxge_assert(mempool->memblocks_arr[i]);
		vxge_assert(mempool->memblocks_dma_arr + i);

		dma_object = mempool->memblocks_dma_arr + i;

		for (j = 0; j < mempool->items_per_memblock; j++) {
			u32 index = i * mempool->items_per_memblock + j;

			/* to skip last partially filled(if any) memblock */
			if (index >= mempool->items_current)
				break;
		}

		vfree(mempool->memblocks_priv_arr[i]);

		__vxge_hw_blockpool_free(devh, mempool->memblocks_arr[i],
				mempool->memblock_size, dma_object);
	}

	vfree(mempool->items_arr);
	vfree(mempool->memblocks_dma_arr);
	vfree(mempool->memblocks_priv_arr);
	vfree(mempool->memblocks_arr);
	vfree(mempool);
}

/*
 * __vxge_hw_mempool_grow
 * Will resize mempool up to %num_allocate value.
 */
static enum vxge_hw_status
__vxge_hw_mempool_grow(struct vxge_hw_mempool *mempool, u32 num_allocate,
		       u32 *num_allocated)
{
	u32 i, first_time = mempool->memblocks_allocated == 0 ? 1 : 0;
	u32 n_items = mempool->items_per_memblock;
	u32 start_block_idx = mempool->memblocks_allocated;
	u32 end_block_idx = mempool->memblocks_allocated + num_allocate;
	enum vxge_hw_status status = VXGE_HW_OK;

	*num_allocated = 0;

	if (end_block_idx > mempool->memblocks_max) {
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		goto exit;
	}

	for (i = start_block_idx; i < end_block_idx; i++) {
		u32 j;
		u32 is_last = ((end_block_idx - 1) == i);
		struct vxge_hw_mempool_dma *dma_object =
			mempool->memblocks_dma_arr + i;
		void *the_memblock;

		/* allocate memblock's private part. Each DMA memblock
		 * has a space allocated for item's private usage upon
		 * mempool's user request. Each time mempool grows, it will
		 * allocate new memblock and its private part at once.
		 * This helps to minimize memory usage a lot. */
		mempool->memblocks_priv_arr[i] =
			vzalloc(array_size(mempool->items_priv_size, n_items));
		if (mempool->memblocks_priv_arr[i] == NULL) {
			status = VXGE_HW_ERR_OUT_OF_MEMORY;
			goto exit;
		}

		/* allocate DMA-capable memblock */
		mempool->memblocks_arr[i] =
			__vxge_hw_blockpool_malloc(mempool->devh,
				mempool->memblock_size, dma_object);
		if (mempool->memblocks_arr[i] == NULL) {
			vfree(mempool->memblocks_priv_arr[i]);
			status = VXGE_HW_ERR_OUT_OF_MEMORY;
			goto exit;
		}

		(*num_allocated)++;
		mempool->memblocks_allocated++;

		memset(mempool->memblocks_arr[i], 0, mempool->memblock_size);

		the_memblock = mempool->memblocks_arr[i];

		/* fill the items hash array */
		for (j = 0; j < n_items; j++) {
			u32 index = i * n_items + j;

			if (first_time && index >= mempool->items_initial)
				break;

			mempool->items_arr[index] =
				((char *)the_memblock + j*mempool->item_size);

			/* let caller to do more job on each item */
			if (mempool->item_func_alloc != NULL)
				mempool->item_func_alloc(mempool, i,
					dma_object, index, is_last);

			mempool->items_current = index + 1;
		}

		if (first_time && mempool->items_current ==
					mempool->items_initial)
			break;
	}
exit:
	return status;
}

/*
 * vxge_hw_mempool_create
 * This function will create memory pool object. Pool may grow but will
 * never shrink. Pool consists of number of dynamically allocated blocks
 * with size enough to hold %items_initial number of items. Memory is
 * DMA-able but client must map/unmap before interoperating with the device.
 */
static struct vxge_hw_mempool *
__vxge_hw_mempool_create(struct __vxge_hw_device *devh,
			 u32 memblock_size,
			 u32 item_size,
			 u32 items_priv_size,
			 u32 items_initial,
			 u32 items_max,
			 const struct vxge_hw_mempool_cbs *mp_callback,
			 void *userdata)
{
	enum vxge_hw_status status = VXGE_HW_OK;
	u32 memblocks_to_allocate;
	struct vxge_hw_mempool *mempool = NULL;
	u32 allocated;

	if (memblock_size < item_size) {
		status = VXGE_HW_FAIL;
		goto exit;
	}

	mempool = vzalloc(sizeof(struct vxge_hw_mempool));
	if (mempool == NULL) {
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		goto exit;
	}

	mempool->devh			= devh;
	mempool->memblock_size		= memblock_size;
	mempool->items_max		= items_max;
	mempool->items_initial		= items_initial;
	mempool->item_size		= item_size;
	mempool->items_priv_size	= items_priv_size;
	mempool->item_func_alloc	= mp_callback->item_func_alloc;
	mempool->userdata		= userdata;

	mempool->memblocks_allocated = 0;

	mempool->items_per_memblock = memblock_size / item_size;

	mempool->memblocks_max = (items_max + mempool->items_per_memblock - 1) /
					mempool->items_per_memblock;

	/* allocate array of memblocks */
	mempool->memblocks_arr =
		vzalloc(array_size(sizeof(void *), mempool->memblocks_max));
	if (mempool->memblocks_arr == NULL) {
		__vxge_hw_mempool_destroy(mempool);
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		mempool = NULL;
		goto exit;
	}

	/* allocate array of private parts of items per memblocks */
	mempool->memblocks_priv_arr =
		vzalloc(array_size(sizeof(void *), mempool->memblocks_max));
	if (mempool->memblocks_priv_arr == NULL) {
		__vxge_hw_mempool_destroy(mempool);
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		mempool = NULL;
		goto exit;
	}

	/* allocate array of memblocks DMA objects */
	mempool->memblocks_dma_arr =
		vzalloc(array_size(sizeof(struct vxge_hw_mempool_dma),
				   mempool->memblocks_max));
	if (mempool->memblocks_dma_arr == NULL) {
		__vxge_hw_mempool_destroy(mempool);
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		mempool = NULL;
		goto exit;
	}

	/* allocate hash array of items */
	mempool->items_arr = vzalloc(array_size(sizeof(void *),
						mempool->items_max));
	if (mempool->items_arr == NULL) {
		__vxge_hw_mempool_destroy(mempool);
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		mempool = NULL;
		goto exit;
	}

	/* calculate initial number of memblocks */
	memblocks_to_allocate = (mempool->items_initial +
				 mempool->items_per_memblock - 1) /
						mempool->items_per_memblock;

	/* pre-allocate the mempool */
	status = __vxge_hw_mempool_grow(mempool, memblocks_to_allocate,
					&allocated);
	if (status != VXGE_HW_OK) {
		__vxge_hw_mempool_destroy(mempool);
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		mempool = NULL;
		goto exit;
	}

exit:
	return mempool;
}

/*
 * __vxge_hw_ring_abort - Returns the RxD
 * This function terminates the RxDs of ring
 */
static enum vxge_hw_status __vxge_hw_ring_abort(struct __vxge_hw_ring *ring)
{
	void *rxdh;
	struct __vxge_hw_channel *channel;

	channel = &ring->channel;

	for (;;) {
		vxge_hw_channel_dtr_try_complete(channel, &rxdh);

		if (rxdh == NULL)
			break;

		vxge_hw_channel_dtr_complete(channel);

		if (ring->rxd_term)
			ring->rxd_term(rxdh, VXGE_HW_RXD_STATE_POSTED,
				channel->userdata);

		vxge_hw_channel_dtr_free(channel, rxdh);
	}

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_ring_reset - Resets the ring
 * This function resets the ring during vpath reset operation
 */
static enum vxge_hw_status __vxge_hw_ring_reset(struct __vxge_hw_ring *ring)
{
	enum vxge_hw_status status = VXGE_HW_OK;
	struct __vxge_hw_channel *channel;

	channel = &ring->channel;

	__vxge_hw_ring_abort(ring);

	status = __vxge_hw_channel_reset(channel);

	if (status != VXGE_HW_OK)
		goto exit;

	if (ring->rxd_init) {
		status = vxge_hw_ring_replenish(ring);
		if (status != VXGE_HW_OK)
			goto exit;
	}
exit:
	return status;
}

/*
 * __vxge_hw_ring_delete - Removes the ring
 * This function freeup the memory pool and removes the ring
 */
static enum vxge_hw_status
__vxge_hw_ring_delete(struct __vxge_hw_vpath_handle *vp)
{
	struct __vxge_hw_ring *ring = vp->vpath->ringh;

	__vxge_hw_ring_abort(ring);

	if (ring->mempool)
		__vxge_hw_mempool_destroy(ring->mempool);

	vp->vpath->ringh = NULL;
	__vxge_hw_channel_free(&ring->channel);

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_ring_create - Create a Ring
 * This function creates Ring and initializes it.
 */
static enum vxge_hw_status
__vxge_hw_ring_create(struct __vxge_hw_vpath_handle *vp,
		      struct vxge_hw_ring_attr *attr)
{
	enum vxge_hw_status status = VXGE_HW_OK;
	struct __vxge_hw_ring *ring;
	u32 ring_length;
	struct vxge_hw_ring_config *config;
	struct __vxge_hw_device *hldev;
	u32 vp_id;
	static const struct vxge_hw_mempool_cbs ring_mp_callback = {
		.item_func_alloc = __vxge_hw_ring_mempool_item_alloc,
	};

	if ((vp == NULL) || (attr == NULL)) {
		status = VXGE_HW_FAIL;
		goto exit;
	}

	hldev = vp->vpath->hldev;
	vp_id = vp->vpath->vp_id;

	config = &hldev->config.vp_config[vp_id].ring;

	ring_length = config->ring_blocks *
			vxge_hw_ring_rxds_per_block_get(config->buffer_mode);

	ring = (struct __vxge_hw_ring *)__vxge_hw_channel_allocate(vp,
						VXGE_HW_CHANNEL_TYPE_RING,
						ring_length,
						attr->per_rxd_space,
						attr->userdata);
	if (ring == NULL) {
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		goto exit;
	}

	vp->vpath->ringh = ring;
	ring->vp_id = vp_id;
	ring->vp_reg = vp->vpath->vp_reg;
	ring->common_reg = hldev->common_reg;
	ring->stats = &vp->vpath->sw_stats->ring_stats;
	ring->config = config;
	ring->callback = attr->callback;
	ring->rxd_init = attr->rxd_init;
	ring->rxd_term = attr->rxd_term;
	ring->buffer_mode = config->buffer_mode;
	ring->tim_rti_cfg1_saved = vp->vpath->tim_rti_cfg1_saved;
	ring->tim_rti_cfg3_saved = vp->vpath->tim_rti_cfg3_saved;
	ring->rxds_limit = config->rxds_limit;

	ring->rxd_size = vxge_hw_ring_rxd_size_get(config->buffer_mode);
	ring->rxd_priv_size =
		sizeof(struct __vxge_hw_ring_rxd_priv) + attr->per_rxd_space;
	ring->per_rxd_space = attr->per_rxd_space;

	ring->rxd_priv_size =
		((ring->rxd_priv_size + VXGE_CACHE_LINE_SIZE - 1) /
		VXGE_CACHE_LINE_SIZE) * VXGE_CACHE_LINE_SIZE;

	/* how many RxDs can fit into one block. Depends on configured
	 * buffer_mode. */
	ring->rxds_per_block =
		vxge_hw_ring_rxds_per_block_get(config->buffer_mode);

	/* calculate actual RxD block private size */
	ring->rxdblock_priv_size = ring->rxd_priv_size * ring->rxds_per_block;
	ring->mempool = __vxge_hw_mempool_create(hldev,
				VXGE_HW_BLOCK_SIZE,
				VXGE_HW_BLOCK_SIZE,
				ring->rxdblock_priv_size,
				ring->config->ring_blocks,
				ring->config->ring_blocks,
				&ring_mp_callback,
				ring);
	if (ring->mempool == NULL) {
		__vxge_hw_ring_delete(vp);
		return VXGE_HW_ERR_OUT_OF_MEMORY;
	}

	status = __vxge_hw_channel_initialize(&ring->channel);
	if (status != VXGE_HW_OK) {
		__vxge_hw_ring_delete(vp);
		goto exit;
	}

	/* Note:
	 * Specifying rxd_init callback means two things:
	 * 1) rxds need to be initialized by driver at channel-open time;
	 * 2) rxds need to be posted at channel-open time
	 *    (that's what the initial_replenish() below does)
	 * Currently we don't have a case when the 1) is done without the 2).
	 */
	if (ring->rxd_init) {
		status = vxge_hw_ring_replenish(ring);
		if (status != VXGE_HW_OK) {
			__vxge_hw_ring_delete(vp);
			goto exit;
		}
	}

	/* initial replenish will increment the counter in its post() routine,
	 * we have to reset it */
	ring->stats->common_stats.usage_cnt = 0;
exit:
	return status;
}

/*
 * vxge_hw_device_config_default_get - Initialize device config with defaults.
 * Initialize Titan device config with default values.
 */
enum vxge_hw_status
vxge_hw_device_config_default_get(struct vxge_hw_device_config *device_config)
{
	u32 i;

	device_config->dma_blockpool_initial =
					VXGE_HW_INITIAL_DMA_BLOCK_POOL_SIZE;
	device_config->dma_blockpool_max = VXGE_HW_MAX_DMA_BLOCK_POOL_SIZE;
	device_config->intr_mode = VXGE_HW_INTR_MODE_DEF;
	device_config->rth_en = VXGE_HW_RTH_DEFAULT;
	device_config->rth_it_type = VXGE_HW_RTH_IT_TYPE_DEFAULT;
	device_config->device_poll_millis =  VXGE_HW_DEF_DEVICE_POLL_MILLIS;
	device_config->rts_mac_en =  VXGE_HW_RTS_MAC_DEFAULT;

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {
		device_config->vp_config[i].vp_id = i;

		device_config->vp_config[i].min_bandwidth =
				VXGE_HW_VPATH_BANDWIDTH_DEFAULT;

		device_config->vp_config[i].ring.enable = VXGE_HW_RING_DEFAULT;

		device_config->vp_config[i].ring.ring_blocks =
				VXGE_HW_DEF_RING_BLOCKS;

		device_config->vp_config[i].ring.buffer_mode =
				VXGE_HW_RING_RXD_BUFFER_MODE_DEFAULT;

		device_config->vp_config[i].ring.scatter_mode =
				VXGE_HW_RING_SCATTER_MODE_USE_FLASH_DEFAULT;

		device_config->vp_config[i].ring.rxds_limit =
				VXGE_HW_DEF_RING_RXDS_LIMIT;

		device_config->vp_config[i].fifo.enable = VXGE_HW_FIFO_ENABLE;

		device_config->vp_config[i].fifo.fifo_blocks =
				VXGE_HW_MIN_FIFO_BLOCKS;

		device_config->vp_config[i].fifo.max_frags =
				VXGE_HW_MAX_FIFO_FRAGS;

		device_config->vp_config[i].fifo.memblock_size =
				VXGE_HW_DEF_FIFO_MEMBLOCK_SIZE;

		device_config->vp_config[i].fifo.alignment_size =
				VXGE_HW_DEF_FIFO_ALIGNMENT_SIZE;

		device_config->vp_config[i].fifo.intr =
				VXGE_HW_FIFO_QUEUE_INTR_DEFAULT;

		device_config->vp_config[i].fifo.no_snoop_bits =
				VXGE_HW_FIFO_NO_SNOOP_DEFAULT;
		device_config->vp_config[i].tti.intr_enable =
				VXGE_HW_TIM_INTR_DEFAULT;

		device_config->vp_config[i].tti.btimer_val =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.timer_ac_en =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.timer_ci_en =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.timer_ri_en =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.rtimer_val =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.util_sel =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.ltimer_val =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.urange_a =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.uec_a =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.urange_b =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.uec_b =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.urange_c =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.uec_c =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].tti.uec_d =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.intr_enable =
				VXGE_HW_TIM_INTR_DEFAULT;

		device_config->vp_config[i].rti.btimer_val =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.timer_ac_en =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.timer_ci_en =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.timer_ri_en =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.rtimer_val =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.util_sel =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.ltimer_val =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.urange_a =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.uec_a =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.urange_b =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.uec_b =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.urange_c =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.uec_c =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].rti.uec_d =
				VXGE_HW_USE_FLASH_DEFAULT;

		device_config->vp_config[i].mtu =
				VXGE_HW_VPATH_USE_FLASH_DEFAULT_INITIAL_MTU;

		device_config->vp_config[i].rpa_strip_vlan_tag =
			VXGE_HW_VPATH_RPA_STRIP_VLAN_TAG_USE_FLASH_DEFAULT;
	}

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_vpath_swapper_set - Set the swapper bits for the vpath.
 * Set the swapper bits appropriately for the vpath.
 */
static enum vxge_hw_status
__vxge_hw_vpath_swapper_set(struct vxge_hw_vpath_reg __iomem *vpath_reg)
{
#ifndef __BIG_ENDIAN
	u64 val64;

	val64 = readq(&vpath_reg->vpath_general_cfg1);
	wmb();
	val64 |= VXGE_HW_VPATH_GENERAL_CFG1_CTL_BYTE_SWAPEN;
	writeq(val64, &vpath_reg->vpath_general_cfg1);
	wmb();
#endif
	return VXGE_HW_OK;
}

/*
 * __vxge_hw_kdfc_swapper_set - Set the swapper bits for the kdfc.
 * Set the swapper bits appropriately for the vpath.
 */
static enum vxge_hw_status
__vxge_hw_kdfc_swapper_set(struct vxge_hw_legacy_reg __iomem *legacy_reg,
			   struct vxge_hw_vpath_reg __iomem *vpath_reg)
{
	u64 val64;

	val64 = readq(&legacy_reg->pifm_wr_swap_en);

	if (val64 == VXGE_HW_SWAPPER_WRITE_BYTE_SWAP_ENABLE) {
		val64 = readq(&vpath_reg->kdfcctl_cfg0);
		wmb();

		val64 |= VXGE_HW_KDFCCTL_CFG0_BYTE_SWAPEN_FIFO0	|
			VXGE_HW_KDFCCTL_CFG0_BYTE_SWAPEN_FIFO1	|
			VXGE_HW_KDFCCTL_CFG0_BYTE_SWAPEN_FIFO2;

		writeq(val64, &vpath_reg->kdfcctl_cfg0);
		wmb();
	}

	return VXGE_HW_OK;
}

/*
 * vxge_hw_mgmt_reg_read - Read Titan register.
 */
enum vxge_hw_status
vxge_hw_mgmt_reg_read(struct __vxge_hw_device *hldev,
		      enum vxge_hw_mgmt_reg_type type,
		      u32 index, u32 offset, u64 *value)
{
	enum vxge_hw_status status = VXGE_HW_OK;

	if ((hldev == NULL) || (hldev->magic != VXGE_HW_DEVICE_MAGIC)) {
		status = VXGE_HW_ERR_INVALID_DEVICE;
		goto exit;
	}

	switch (type) {
	case vxge_hw_mgmt_reg_type_legacy:
		if (offset > sizeof(struct vxge_hw_legacy_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		*value = readq((void __iomem *)hldev->legacy_reg + offset);
		break;
	case vxge_hw_mgmt_reg_type_toc:
		if (offset > sizeof(struct vxge_hw_toc_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		*value = readq((void __iomem *)hldev->toc_reg + offset);
		break;
	case vxge_hw_mgmt_reg_type_common:
		if (offset > sizeof(struct vxge_hw_common_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		*value = readq((void __iomem *)hldev->common_reg + offset);
		break;
	case vxge_hw_mgmt_reg_type_mrpcim:
		if (!(hldev->access_rights &
			VXGE_HW_DEVICE_ACCESS_RIGHT_MRPCIM)) {
			status = VXGE_HW_ERR_PRIVILEGED_OPERATION;
			break;
		}
		if (offset > sizeof(struct vxge_hw_mrpcim_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		*value = readq((void __iomem *)hldev->mrpcim_reg + offset);
		break;
	case vxge_hw_mgmt_reg_type_srpcim:
		if (!(hldev->access_rights &
			VXGE_HW_DEVICE_ACCESS_RIGHT_SRPCIM)) {
			status = VXGE_HW_ERR_PRIVILEGED_OPERATION;
			break;
		}
		if (index > VXGE_HW_TITAN_SRPCIM_REG_SPACES - 1) {
			status = VXGE_HW_ERR_INVALID_INDEX;
			break;
		}
		if (offset > sizeof(struct vxge_hw_srpcim_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		*value = readq((void __iomem *)hldev->srpcim_reg[index] +
				offset);
		break;
	case vxge_hw_mgmt_reg_type_vpmgmt:
		if ((index > VXGE_HW_TITAN_VPMGMT_REG_SPACES - 1) ||
			(!(hldev->vpath_assignments & vxge_mBIT(index)))) {
			status = VXGE_HW_ERR_INVALID_INDEX;
			break;
		}
		if (offset > sizeof(struct vxge_hw_vpmgmt_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		*value = readq((void __iomem *)hldev->vpmgmt_reg[index] +
				offset);
		break;
	case vxge_hw_mgmt_reg_type_vpath:
		if ((index > VXGE_HW_TITAN_VPATH_REG_SPACES - 1) ||
			(!(hldev->vpath_assignments & vxge_mBIT(index)))) {
			status = VXGE_HW_ERR_INVALID_INDEX;
			break;
		}
		if (index > VXGE_HW_TITAN_VPATH_REG_SPACES - 1) {
			status = VXGE_HW_ERR_INVALID_INDEX;
			break;
		}
		if (offset > sizeof(struct vxge_hw_vpath_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		*value = readq((void __iomem *)hldev->vpath_reg[index] +
				offset);
		break;
	default:
		status = VXGE_HW_ERR_INVALID_TYPE;
		break;
	}

exit:
	return status;
}

/*
 * vxge_hw_vpath_strip_fcs_check - Check for FCS strip.
 */
enum vxge_hw_status
vxge_hw_vpath_strip_fcs_check(struct __vxge_hw_device *hldev, u64 vpath_mask)
{
	struct vxge_hw_vpmgmt_reg       __iomem *vpmgmt_reg;
	int i = 0, j = 0;

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {
		if (!((vpath_mask) & vxge_mBIT(i)))
			continue;
		vpmgmt_reg = hldev->vpmgmt_reg[i];
		for (j = 0; j < VXGE_HW_MAC_MAX_MAC_PORT_ID; j++) {
			if (readq(&vpmgmt_reg->rxmac_cfg0_port_vpmgmt_clone[j])
			& VXGE_HW_RXMAC_CFG0_PORT_VPMGMT_CLONE_STRIP_FCS)
				return VXGE_HW_FAIL;
		}
	}
	return VXGE_HW_OK;
}
/*
 * vxge_hw_mgmt_reg_Write - Write Titan register.
 */
enum vxge_hw_status
vxge_hw_mgmt_reg_write(struct __vxge_hw_device *hldev,
		      enum vxge_hw_mgmt_reg_type type,
		      u32 index, u32 offset, u64 value)
{
	enum vxge_hw_status status = VXGE_HW_OK;

	if ((hldev == NULL) || (hldev->magic != VXGE_HW_DEVICE_MAGIC)) {
		status = VXGE_HW_ERR_INVALID_DEVICE;
		goto exit;
	}

	switch (type) {
	case vxge_hw_mgmt_reg_type_legacy:
		if (offset > sizeof(struct vxge_hw_legacy_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		writeq(value, (void __iomem *)hldev->legacy_reg + offset);
		break;
	case vxge_hw_mgmt_reg_type_toc:
		if (offset > sizeof(struct vxge_hw_toc_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		writeq(value, (void __iomem *)hldev->toc_reg + offset);
		break;
	case vxge_hw_mgmt_reg_type_common:
		if (offset > sizeof(struct vxge_hw_common_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		writeq(value, (void __iomem *)hldev->common_reg + offset);
		break;
	case vxge_hw_mgmt_reg_type_mrpcim:
		if (!(hldev->access_rights &
			VXGE_HW_DEVICE_ACCESS_RIGHT_MRPCIM)) {
			status = VXGE_HW_ERR_PRIVILEGED_OPERATION;
			break;
		}
		if (offset > sizeof(struct vxge_hw_mrpcim_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		writeq(value, (void __iomem *)hldev->mrpcim_reg + offset);
		break;
	case vxge_hw_mgmt_reg_type_srpcim:
		if (!(hldev->access_rights &
			VXGE_HW_DEVICE_ACCESS_RIGHT_SRPCIM)) {
			status = VXGE_HW_ERR_PRIVILEGED_OPERATION;
			break;
		}
		if (index > VXGE_HW_TITAN_SRPCIM_REG_SPACES - 1) {
			status = VXGE_HW_ERR_INVALID_INDEX;
			break;
		}
		if (offset > sizeof(struct vxge_hw_srpcim_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		writeq(value, (void __iomem *)hldev->srpcim_reg[index] +
			offset);

		break;
	case vxge_hw_mgmt_reg_type_vpmgmt:
		if ((index > VXGE_HW_TITAN_VPMGMT_REG_SPACES - 1) ||
			(!(hldev->vpath_assignments & vxge_mBIT(index)))) {
			status = VXGE_HW_ERR_INVALID_INDEX;
			break;
		}
		if (offset > sizeof(struct vxge_hw_vpmgmt_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		writeq(value, (void __iomem *)hldev->vpmgmt_reg[index] +
			offset);
		break;
	case vxge_hw_mgmt_reg_type_vpath:
		if ((index > VXGE_HW_TITAN_VPATH_REG_SPACES-1) ||
			(!(hldev->vpath_assignments & vxge_mBIT(index)))) {
			status = VXGE_HW_ERR_INVALID_INDEX;
			break;
		}
		if (offset > sizeof(struct vxge_hw_vpath_reg) - 8) {
			status = VXGE_HW_ERR_INVALID_OFFSET;
			break;
		}
		writeq(value, (void __iomem *)hldev->vpath_reg[index] +
			offset);
		break;
	default:
		status = VXGE_HW_ERR_INVALID_TYPE;
		break;
	}
exit:
	return status;
}

/*
 * __vxge_hw_fifo_abort - Returns the TxD
 * This function terminates the TxDs of fifo
 */
static enum vxge_hw_status __vxge_hw_fifo_abort(struct __vxge_hw_fifo *fifo)
{
	void *txdlh;

	for (;;) {
		vxge_hw_channel_dtr_try_complete(&fifo->channel, &txdlh);

		if (txdlh == NULL)
			break;

		vxge_hw_channel_dtr_complete(&fifo->channel);

		if (fifo->txdl_term) {
			fifo->txdl_term(txdlh,
			VXGE_HW_TXDL_STATE_POSTED,
			fifo->channel.userdata);
		}

		vxge_hw_channel_dtr_free(&fifo->channel, txdlh);
	}

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_fifo_reset - Resets the fifo
 * This function resets the fifo during vpath reset operation
 */
static enum vxge_hw_status __vxge_hw_fifo_reset(struct __vxge_hw_fifo *fifo)
{
	enum vxge_hw_status status = VXGE_HW_OK;

	__vxge_hw_fifo_abort(fifo);
	status = __vxge_hw_channel_reset(&fifo->channel);

	return status;
}

/*
 * __vxge_hw_fifo_delete - Removes the FIFO
 * This function freeup the memory pool and removes the FIFO
 */
static enum vxge_hw_status
__vxge_hw_fifo_delete(struct __vxge_hw_vpath_handle *vp)
{
	struct __vxge_hw_fifo *fifo = vp->vpath->fifoh;

	__vxge_hw_fifo_abort(fifo);

	if (fifo->mempool)
		__vxge_hw_mempool_destroy(fifo->mempool);

	vp->vpath->fifoh = NULL;

	__vxge_hw_channel_free(&fifo->channel);

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_fifo_mempool_item_alloc - Allocate List blocks for TxD
 * list callback
 * This function is callback passed to __vxge_hw_mempool_create to create memory
 * pool for TxD list
 */
static void
__vxge_hw_fifo_mempool_item_alloc(
	struct vxge_hw_mempool *mempoolh,
	u32 memblock_index, struct vxge_hw_mempool_dma *dma_object,
	u32 index, u32 is_last)
{
	u32 memblock_item_idx;
	struct __vxge_hw_fifo_txdl_priv *txdl_priv;
	struct vxge_hw_fifo_txd *txdp =
		(struct vxge_hw_fifo_txd *)mempoolh->items_arr[index];
	struct __vxge_hw_fifo *fifo =
			(struct __vxge_hw_fifo *)mempoolh->userdata;
	void *memblock = mempoolh->memblocks_arr[memblock_index];

	vxge_assert(txdp);

	txdp->host_control = (u64) (size_t)
	__vxge_hw_mempool_item_priv(mempoolh, memblock_index, txdp,
					&memblock_item_idx);

	txdl_priv = __vxge_hw_fifo_txdl_priv(fifo, txdp);

	vxge_assert(txdl_priv);

	fifo->channel.reserve_arr[fifo->channel.reserve_ptr - 1 - index] = txdp;

	/* pre-format HW's TxDL's private */
	txdl_priv->dma_offset = (char *)txdp - (char *)memblock;
	txdl_priv->dma_addr = dma_object->addr + txdl_priv->dma_offset;
	txdl_priv->dma_handle = dma_object->handle;
	txdl_priv->memblock   = memblock;
	txdl_priv->first_txdp = txdp;
	txdl_priv->next_txdl_priv = NULL;
	txdl_priv->alloc_frags = 0;
}

/*
 * __vxge_hw_fifo_create - Create a FIFO
 * This function creates FIFO and initializes it.
 */
static enum vxge_hw_status
__vxge_hw_fifo_create(struct __vxge_hw_vpath_handle *vp,
		      struct vxge_hw_fifo_attr *attr)
{
	enum vxge_hw_status status = VXGE_HW_OK;
	struct __vxge_hw_fifo *fifo;
	struct vxge_hw_fifo_config *config;
	u32 txdl_size, txdl_per_memblock;
	struct vxge_hw_mempool_cbs fifo_mp_callback;
	struct __vxge_hw_virtualpath *vpath;

	if ((vp == NULL) || (attr == NULL)) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}
	vpath = vp->vpath;
	config = &vpath->hldev->config.vp_config[vpath->vp_id].fifo;

	txdl_size = config->max_frags * sizeof(struct vxge_hw_fifo_txd);

	txdl_per_memblock = config->memblock_size / txdl_size;

	fifo = (struct __vxge_hw_fifo *)__vxge_hw_channel_allocate(vp,
					VXGE_HW_CHANNEL_TYPE_FIFO,
					config->fifo_blocks * txdl_per_memblock,
					attr->per_txdl_space, attr->userdata);

	if (fifo == NULL) {
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		goto exit;
	}

	vpath->fifoh = fifo;
	fifo->nofl_db = vpath->nofl_db;

	fifo->vp_id = vpath->vp_id;
	fifo->vp_reg = vpath->vp_reg;
	fifo->stats = &vpath->sw_stats->fifo_stats;

	fifo->config = config;

	/* apply "interrupts per txdl" attribute */
	fifo->interrupt_type = VXGE_HW_FIFO_TXD_INT_TYPE_UTILZ;
	fifo->tim_tti_cfg1_saved = vpath->tim_tti_cfg1_saved;
	fifo->tim_tti_cfg3_saved = vpath->tim_tti_cfg3_saved;

	if (fifo->config->intr)
		fifo->interrupt_type = VXGE_HW_FIFO_TXD_INT_TYPE_PER_LIST;

	fifo->no_snoop_bits = config->no_snoop_bits;

	/*
	 * FIFO memory management strategy:
	 *
	 * TxDL split into three independent parts:
	 *	- set of TxD's
	 *	- TxD HW private part
	 *	- driver private part
	 *
	 * Adaptative memory allocation used. i.e. Memory allocated on
	 * demand with the size which will fit into one memory block.
	 * One memory block may contain more than one TxDL.
	 *
	 * During "reserve" operations more memory can be allocated on demand
	 * for example due to FIFO full condition.
	 *
	 * Pool of memory memblocks never shrinks except in __vxge_hw_fifo_close
	 * routine which will essentially stop the channel and free resources.
	 */

	/* TxDL common private size == TxDL private  +  driver private */
	fifo->priv_size =
		sizeof(struct __vxge_hw_fifo_txdl_priv) + attr->per_txdl_space;
	fifo->priv_size = ((fifo->priv_size  +  VXGE_CACHE_LINE_SIZE - 1) /
			VXGE_CACHE_LINE_SIZE) * VXGE_CACHE_LINE_SIZE;

	fifo->per_txdl_space = attr->per_txdl_space;

	/* recompute txdl size to be cacheline aligned */
	fifo->txdl_size = txdl_size;
	fifo->txdl_per_memblock = txdl_per_memblock;

	fifo->txdl_term = attr->txdl_term;
	fifo->callback = attr->callback;

	if (fifo->txdl_per_memblock == 0) {
		__vxge_hw_fifo_delete(vp);
		status = VXGE_HW_ERR_INVALID_BLOCK_SIZE;
		goto exit;
	}

	fifo_mp_callback.item_func_alloc = __vxge_hw_fifo_mempool_item_alloc;

	fifo->mempool =
		__vxge_hw_mempool_create(vpath->hldev,
			fifo->config->memblock_size,
			fifo->txdl_size,
			fifo->priv_size,
			(fifo->config->fifo_blocks * fifo->txdl_per_memblock),
			(fifo->config->fifo_blocks * fifo->txdl_per_memblock),
			&fifo_mp_callback,
			fifo);

	if (fifo->mempool == NULL) {
		__vxge_hw_fifo_delete(vp);
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		goto exit;
	}

	status = __vxge_hw_channel_initialize(&fifo->channel);
	if (status != VXGE_HW_OK) {
		__vxge_hw_fifo_delete(vp);
		goto exit;
	}

	vxge_assert(fifo->channel.reserve_ptr);
exit:
	return status;
}

/*
 * __vxge_hw_vpath_pci_read - Read the content of given address
 *                          in pci config space.
 * Read from the vpath pci config space.
 */
static enum vxge_hw_status
__vxge_hw_vpath_pci_read(struct __vxge_hw_virtualpath *vpath,
			 u32 phy_func_0, u32 offset, u32 *val)
{
	u64 val64;
	enum vxge_hw_status status = VXGE_HW_OK;
	struct vxge_hw_vpath_reg __iomem *vp_reg = vpath->vp_reg;

	val64 =	VXGE_HW_PCI_CONFIG_ACCESS_CFG1_ADDRESS(offset);

	if (phy_func_0)
		val64 |= VXGE_HW_PCI_CONFIG_ACCESS_CFG1_SEL_FUNC0;

	writeq(val64, &vp_reg->pci_config_access_cfg1);
	wmb();
	writeq(VXGE_HW_PCI_CONFIG_ACCESS_CFG2_REQ,
			&vp_reg->pci_config_access_cfg2);
	wmb();

	status = __vxge_hw_device_register_poll(
			&vp_reg->pci_config_access_cfg2,
			VXGE_HW_INTR_MASK_ALL, VXGE_HW_DEF_DEVICE_POLL_MILLIS);

	if (status != VXGE_HW_OK)
		goto exit;

	val64 = readq(&vp_reg->pci_config_access_status);

	if (val64 & VXGE_HW_PCI_CONFIG_ACCESS_STATUS_ACCESS_ERR) {
		status = VXGE_HW_FAIL;
		*val = 0;
	} else
		*val = (u32)vxge_bVALn(val64, 32, 32);
exit:
	return status;
}

/**
 * vxge_hw_device_flick_link_led - Flick (blink) link LED.
 * @hldev: HW device.
 * @on_off: TRUE if flickering to be on, FALSE to be off
 *
 * Flicker the link LED.
 */
enum vxge_hw_status
vxge_hw_device_flick_link_led(struct __vxge_hw_device *hldev, u64 on_off)
{
	struct __vxge_hw_virtualpath *vpath;
	u64 data0, data1 = 0, steer_ctrl = 0;
	enum vxge_hw_status status;

	if (hldev == NULL) {
		status = VXGE_HW_ERR_INVALID_DEVICE;
		goto exit;
	}

	vpath = &hldev->virtual_paths[hldev->first_vp_id];

	data0 = on_off;
	status = vxge_hw_vpath_fw_api(vpath,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_LED_CONTROL,
			VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_FW_MEMO,
			0, &data0, &data1, &steer_ctrl);
exit:
	return status;
}

/*
 * __vxge_hw_vpath_rts_table_get - Get the entries from RTS access tables
 */
enum vxge_hw_status
__vxge_hw_vpath_rts_table_get(struct __vxge_hw_vpath_handle *vp,
			      u32 action, u32 rts_table, u32 offset,
			      u64 *data0, u64 *data1)
{
	enum vxge_hw_status status;
	u64 steer_ctrl = 0;

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	if ((rts_table ==
	     VXGE_HW_RTS_ACS_STEER_CTRL_DATA_STRUCT_SEL_RTH_SOLO_IT) ||
	    (rts_table ==
	     VXGE_HW_RTS_ACS_STEER_CTRL_DATA_STRUCT_SEL_RTH_MULTI_IT) ||
	    (rts_table ==
	     VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_RTH_MASK) ||
	    (rts_table ==
	     VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_RTH_KEY)) {
		steer_ctrl = VXGE_HW_RTS_ACCESS_STEER_CTRL_TABLE_SEL;
	}

	status = vxge_hw_vpath_fw_api(vp->vpath, action, rts_table, offset,
				      data0, data1, &steer_ctrl);
	if (status != VXGE_HW_OK)
		goto exit;

	if ((rts_table != VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_DA) &&
	    (rts_table !=
	     VXGE_HW_RTS_ACS_STEER_CTRL_DATA_STRUCT_SEL_RTH_MULTI_IT))
		*data1 = 0;
exit:
	return status;
}

/*
 * __vxge_hw_vpath_rts_table_set - Set the entries of RTS access tables
 */
enum vxge_hw_status
__vxge_hw_vpath_rts_table_set(struct __vxge_hw_vpath_handle *vp, u32 action,
			      u32 rts_table, u32 offset, u64 steer_data0,
			      u64 steer_data1)
{
	u64 data0, data1 = 0, steer_ctrl = 0;
	enum vxge_hw_status status;

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	data0 = steer_data0;

	if ((rts_table == VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_DA) ||
	    (rts_table ==
	     VXGE_HW_RTS_ACS_STEER_CTRL_DATA_STRUCT_SEL_RTH_MULTI_IT))
		data1 = steer_data1;

	status = vxge_hw_vpath_fw_api(vp->vpath, action, rts_table, offset,
				      &data0, &data1, &steer_ctrl);
exit:
	return status;
}

/*
 * vxge_hw_vpath_rts_rth_set - Set/configure RTS hashing.
 */
enum vxge_hw_status vxge_hw_vpath_rts_rth_set(
			struct __vxge_hw_vpath_handle *vp,
			enum vxge_hw_rth_algoritms algorithm,
			struct vxge_hw_rth_hash_types *hash_type,
			u16 bucket_size)
{
	u64 data0, data1;
	enum vxge_hw_status status = VXGE_HW_OK;

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	status = __vxge_hw_vpath_rts_table_get(vp,
		     VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_READ_ENTRY,
		     VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_RTH_GEN_CFG,
			0, &data0, &data1);
	if (status != VXGE_HW_OK)
		goto exit;

	data0 &= ~(VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_BUCKET_SIZE(0xf) |
			VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_ALG_SEL(0x3));

	data0 |= VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_EN |
	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_BUCKET_SIZE(bucket_size) |
	VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_ALG_SEL(algorithm);

	if (hash_type->hash_type_tcpipv4_en)
		data0 |= VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_TCP_IPV4_EN;

	if (hash_type->hash_type_ipv4_en)
		data0 |= VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_IPV4_EN;

	if (hash_type->hash_type_tcpipv6_en)
		data0 |= VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_TCP_IPV6_EN;

	if (hash_type->hash_type_ipv6_en)
		data0 |= VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_IPV6_EN;

	if (hash_type->hash_type_tcpipv6ex_en)
		data0 |=
		VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_TCP_IPV6_EX_EN;

	if (hash_type->hash_type_ipv6ex_en)
		data0 |= VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_RTH_IPV6_EX_EN;

	if (VXGE_HW_RTS_ACCESS_STEER_DATA0_GET_RTH_GEN_ACTIVE_TABLE(data0))
		data0 &= ~VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_ACTIVE_TABLE;
	else
		data0 |= VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_GEN_ACTIVE_TABLE;

	status = __vxge_hw_vpath_rts_table_set(vp,
		VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_WRITE_ENTRY,
		VXGE_HW_RTS_ACCESS_STEER_CTRL_DATA_STRUCT_SEL_RTH_GEN_CFG,
		0, data0, 0);
exit:
	return status;
}

static void
vxge_hw_rts_rth_data0_data1_get(u32 j, u64 *data0, u64 *data1,
				u16 flag, u8 *itable)
{
	switch (flag) {
	case 1:
		*data0 = VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_ITEM0_BUCKET_NUM(j)|
			VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_ITEM0_ENTRY_EN |
			VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_ITEM0_BUCKET_DATA(
			itable[j]);
		/* fall through */
	case 2:
		*data0 |=
			VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_ITEM1_BUCKET_NUM(j)|
			VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_ITEM1_ENTRY_EN |
			VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_ITEM1_BUCKET_DATA(
			itable[j]);
		/* fall through */
	case 3:
		*data1 = VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM0_BUCKET_NUM(j)|
			VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM0_ENTRY_EN |
			VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM0_BUCKET_DATA(
			itable[j]);
		/* fall through */
	case 4:
		*data1 |=
			VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM1_BUCKET_NUM(j)|
			VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM1_ENTRY_EN |
			VXGE_HW_RTS_ACCESS_STEER_DATA1_RTH_ITEM1_BUCKET_DATA(
			itable[j]);
	default:
		return;
	}
}
/*
 * vxge_hw_vpath_rts_rth_itable_set - Set/configure indirection table (IT).
 */
enum vxge_hw_status vxge_hw_vpath_rts_rth_itable_set(
			struct __vxge_hw_vpath_handle **vpath_handles,
			u32 vpath_count,
			u8 *mtable,
			u8 *itable,
			u32 itable_size)
{
	u32 i, j, action, rts_table;
	u64 data0;
	u64 data1;
	u32 max_entries;
	enum vxge_hw_status status = VXGE_HW_OK;
	struct __vxge_hw_vpath_handle *vp = vpath_handles[0];

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}

	max_entries = (((u32)1) << itable_size);

	if (vp->vpath->hldev->config.rth_it_type
				== VXGE_HW_RTH_IT_TYPE_SOLO_IT) {
		action = VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_WRITE_ENTRY;
		rts_table =
			VXGE_HW_RTS_ACS_STEER_CTRL_DATA_STRUCT_SEL_RTH_SOLO_IT;

		for (j = 0; j < max_entries; j++) {

			data1 = 0;

			data0 =
			VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_SOLO_IT_BUCKET_DATA(
				itable[j]);

			status = __vxge_hw_vpath_rts_table_set(vpath_handles[0],
				action, rts_table, j, data0, data1);

			if (status != VXGE_HW_OK)
				goto exit;
		}

		for (j = 0; j < max_entries; j++) {

			data1 = 0;

			data0 =
			VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_SOLO_IT_ENTRY_EN |
			VXGE_HW_RTS_ACCESS_STEER_DATA0_RTH_SOLO_IT_BUCKET_DATA(
				itable[j]);

			status = __vxge_hw_vpath_rts_table_set(
				vpath_handles[mtable[itable[j]]], action,
				rts_table, j, data0, data1);

			if (status != VXGE_HW_OK)
				goto exit;
		}
	} else {
		action = VXGE_HW_RTS_ACCESS_STEER_CTRL_ACTION_WRITE_ENTRY;
		rts_table =
			VXGE_HW_RTS_ACS_STEER_CTRL_DATA_STRUCT_SEL_RTH_MULTI_IT;
		for (i = 0; i < vpath_count; i++) {

			for (j = 0; j < max_entries;) {

				data0 = 0;
				data1 = 0;

				while (j < max_entries) {
					if (mtable[itable[j]] != i) {
						j++;
						continue;
					}
					vxge_hw_rts_rth_data0_data1_get(j,
						&data0, &data1, 1, itable);
					j++;
					break;
				}

				while (j < max_entries) {
					if (mtable[itable[j]] != i) {
						j++;
						continue;
					}
					vxge_hw_rts_rth_data0_data1_get(j,
						&data0, &data1, 2, itable);
					j++;
					break;
				}

				while (j < max_entries) {
					if (mtable[itable[j]] != i) {
						j++;
						continue;
					}
					vxge_hw_rts_rth_data0_data1_get(j,
						&data0, &data1, 3, itable);
					j++;
					break;
				}

				while (j < max_entries) {
					if (mtable[itable[j]] != i) {
						j++;
						continue;
					}
					vxge_hw_rts_rth_data0_data1_get(j,
						&data0, &data1, 4, itable);
					j++;
					break;
				}

				if (data0 != 0) {
					status = __vxge_hw_vpath_rts_table_set(
							vpath_handles[i],
							action, rts_table,
							0, data0, data1);

					if (status != VXGE_HW_OK)
						goto exit;
				}
			}
		}
	}
exit:
	return status;
}

/**
 * vxge_hw_vpath_check_leak - Check for memory leak
 * @ringh: Handle to the ring object used for receive
 *
 * If PRC_RXD_DOORBELL_VPn.NEW_QW_CNT is larger or equal to
 * PRC_CFG6_VPn.RXD_SPAT then a leak has occurred.
 * Returns: VXGE_HW_FAIL, if leak has occurred.
 *
 */
enum vxge_hw_status
vxge_hw_vpath_check_leak(struct __vxge_hw_ring *ring)
{
	enum vxge_hw_status status = VXGE_HW_OK;
	u64 rxd_new_count, rxd_spat;

	if (ring == NULL)
		return status;

	rxd_new_count = readl(&ring->vp_reg->prc_rxd_doorbell);
	rxd_spat = readq(&ring->vp_reg->prc_cfg6);
	rxd_spat = VXGE_HW_PRC_CFG6_RXD_SPAT(rxd_spat);

	if (rxd_new_count >= rxd_spat)
		status = VXGE_HW_FAIL;

	return status;
}

/*
 * __vxge_hw_vpath_mgmt_read
 * This routine reads the vpath_mgmt registers
 */
static enum vxge_hw_status
__vxge_hw_vpath_mgmt_read(
	struct __vxge_hw_device *hldev,
	struct __vxge_hw_virtualpath *vpath)
{
	u32 i, mtu = 0, max_pyld = 0;
	u64 val64;

	for (i = 0; i < VXGE_HW_MAC_MAX_MAC_PORT_ID; i++) {

		val64 = readq(&vpath->vpmgmt_reg->
				rxmac_cfg0_port_vpmgmt_clone[i]);
		max_pyld =
			(u32)
			VXGE_HW_RXMAC_CFG0_PORT_VPMGMT_CLONE_GET_MAX_PYLD_LEN
			(val64);
		if (mtu < max_pyld)
			mtu = max_pyld;
	}

	vpath->max_mtu = mtu + VXGE_HW_MAC_HEADER_MAX_SIZE;

	val64 = readq(&vpath->vpmgmt_reg->xmac_vsport_choices_vp);

	for (i = 0; i < VXGE_HW_MAX_VIRTUAL_PATHS; i++) {
		if (val64 & vxge_mBIT(i))
			vpath->vsport_number = i;
	}

	val64 = readq(&vpath->vpmgmt_reg->xgmac_gen_status_vpmgmt_clone);

	if (val64 & VXGE_HW_XGMAC_GEN_STATUS_VPMGMT_CLONE_XMACJ_NTWK_OK)
		VXGE_HW_DEVICE_LINK_STATE_SET(vpath->hldev, VXGE_HW_LINK_UP);
	else
		VXGE_HW_DEVICE_LINK_STATE_SET(vpath->hldev, VXGE_HW_LINK_DOWN);

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_vpath_reset_check - Check if resetting the vpath completed
 * This routine checks the vpath_rst_in_prog register to see if
 * adapter completed the reset process for the vpath
 */
static enum vxge_hw_status
__vxge_hw_vpath_reset_check(struct __vxge_hw_virtualpath *vpath)
{
	enum vxge_hw_status status;

	status = __vxge_hw_device_register_poll(
			&vpath->hldev->common_reg->vpath_rst_in_prog,
			VXGE_HW_VPATH_RST_IN_PROG_VPATH_RST_IN_PROG(
				1 << (16 - vpath->vp_id)),
			vpath->hldev->config.device_poll_millis);

	return status;
}

/*
 * __vxge_hw_vpath_reset
 * This routine resets the vpath on the device
 */
static enum vxge_hw_status
__vxge_hw_vpath_reset(struct __vxge_hw_device *hldev, u32 vp_id)
{
	u64 val64;

	val64 = VXGE_HW_CMN_RSTHDLR_CFG0_SW_RESET_VPATH(1 << (16 - vp_id));

	__vxge_hw_pio_mem_write32_upper((u32)vxge_bVALn(val64, 0, 32),
				&hldev->common_reg->cmn_rsthdlr_cfg0);

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_vpath_sw_reset
 * This routine resets the vpath structures
 */
static enum vxge_hw_status
__vxge_hw_vpath_sw_reset(struct __vxge_hw_device *hldev, u32 vp_id)
{
	enum vxge_hw_status status = VXGE_HW_OK;
	struct __vxge_hw_virtualpath *vpath;

	vpath = &hldev->virtual_paths[vp_id];

	if (vpath->ringh) {
		status = __vxge_hw_ring_reset(vpath->ringh);
		if (status != VXGE_HW_OK)
			goto exit;
	}

	if (vpath->fifoh)
		status = __vxge_hw_fifo_reset(vpath->fifoh);
exit:
	return status;
}

/*
 * __vxge_hw_vpath_prc_configure
 * This routine configures the prc registers of virtual path using the config
 * passed
 */
static void
__vxge_hw_vpath_prc_configure(struct __vxge_hw_device *hldev, u32 vp_id)
{
	u64 val64;
	struct __vxge_hw_virtualpath *vpath;
	struct vxge_hw_vp_config *vp_config;
	struct vxge_hw_vpath_reg __iomem *vp_reg;

	vpath = &hldev->virtual_paths[vp_id];
	vp_reg = vpath->vp_reg;
	vp_config = vpath->vp_config;

	if (vp_config->ring.enable == VXGE_HW_RING_DISABLE)
		return;

	val64 = readq(&vp_reg->prc_cfg1);
	val64 |= VXGE_HW_PRC_CFG1_RTI_TINT_DISABLE;
	writeq(val64, &vp_reg->prc_cfg1);

	val64 = readq(&vpath->vp_reg->prc_cfg6);
	val64 |= VXGE_HW_PRC_CFG6_DOORBELL_MODE_EN;
	writeq(val64, &vpath->vp_reg->prc_cfg6);

	val64 = readq(&vp_reg->prc_cfg7);

	if (vpath->vp_config->ring.scatter_mode !=
		VXGE_HW_RING_SCATTER_MODE_USE_FLASH_DEFAULT) {

		val64 &= ~VXGE_HW_PRC_CFG7_SCATTER_MODE(0x3);

		switch (vpath->vp_config->ring.scatter_mode) {
		case VXGE_HW_RING_SCATTER_MODE_A:
			val64 |= VXGE_HW_PRC_CFG7_SCATTER_MODE(
					VXGE_HW_PRC_CFG7_SCATTER_MODE_A);
			break;
		case VXGE_HW_RING_SCATTER_MODE_B:
			val64 |= VXGE_HW_PRC_CFG7_SCATTER_MODE(
					VXGE_HW_PRC_CFG7_SCATTER_MODE_B);
			break;
		case VXGE_HW_RING_SCATTER_MODE_C:
			val64 |= VXGE_HW_PRC_CFG7_SCATTER_MODE(
					VXGE_HW_PRC_CFG7_SCATTER_MODE_C);
			break;
		}
	}

	writeq(val64, &vp_reg->prc_cfg7);

	writeq(VXGE_HW_PRC_CFG5_RXD0_ADD(
				__vxge_hw_ring_first_block_address_get(
					vpath->ringh) >> 3), &vp_reg->prc_cfg5);

	val64 = readq(&vp_reg->prc_cfg4);
	val64 |= VXGE_HW_PRC_CFG4_IN_SVC;
	val64 &= ~VXGE_HW_PRC_CFG4_RING_MODE(0x3);

	val64 |= VXGE_HW_PRC_CFG4_RING_MODE(
			VXGE_HW_PRC_CFG4_RING_MODE_ONE_BUFFER);

	if (hldev->config.rth_en == VXGE_HW_RTH_DISABLE)
		val64 |= VXGE_HW_PRC_CFG4_RTH_DISABLE;
	else
		val64 &= ~VXGE_HW_PRC_CFG4_RTH_DISABLE;

	writeq(val64, &vp_reg->prc_cfg4);
}

/*
 * __vxge_hw_vpath_kdfc_configure
 * This routine configures the kdfc registers of virtual path using the
 * config passed
 */
static enum vxge_hw_status
__vxge_hw_vpath_kdfc_configure(struct __vxge_hw_device *hldev, u32 vp_id)
{
	u64 val64;
	u64 vpath_stride;
	enum vxge_hw_status status = VXGE_HW_OK;
	struct __vxge_hw_virtualpath *vpath;
	struct vxge_hw_vpath_reg __iomem *vp_reg;

	vpath = &hldev->virtual_paths[vp_id];
	vp_reg = vpath->vp_reg;
	status = __vxge_hw_kdfc_swapper_set(hldev->legacy_reg, vp_reg);

	if (status != VXGE_HW_OK)
		goto exit;

	val64 = readq(&vp_reg->kdfc_drbl_triplet_total);

	vpath->max_kdfc_db =
		(u32)VXGE_HW_KDFC_DRBL_TRIPLET_TOTAL_GET_KDFC_MAX_SIZE(
			val64+1)/2;

	if (vpath->vp_config->fifo.enable == VXGE_HW_FIFO_ENABLE) {

		vpath->max_nofl_db = vpath->max_kdfc_db;

		if (vpath->max_nofl_db <
			((vpath->vp_config->fifo.memblock_size /
			(vpath->vp_config->fifo.max_frags *
			sizeof(struct vxge_hw_fifo_txd))) *
			vpath->vp_config->fifo.fifo_blocks)) {

			return VXGE_HW_BADCFG_FIFO_BLOCKS;
		}
		val64 = VXGE_HW_KDFC_FIFO_TRPL_PARTITION_LENGTH_0(
				(vpath->max_nofl_db*2)-1);
	}

	writeq(val64, &vp_reg->kdfc_fifo_trpl_partition);

	writeq(VXGE_HW_KDFC_FIFO_TRPL_CTRL_TRIPLET_ENABLE,
		&vp_reg->kdfc_fifo_trpl_ctrl);

	val64 = readq(&vp_reg->kdfc_trpl_fifo_0_ctrl);

	val64 &= ~(VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_MODE(0x3) |
		   VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_SELECT(0xFF));

	val64 |= VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_MODE(
		 VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_MODE_NON_OFFLOAD_ONLY) |
#ifndef __BIG_ENDIAN
		 VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_SWAP_EN |
#endif
		 VXGE_HW_KDFC_TRPL_FIFO_0_CTRL_SELECT(0);

	writeq(val64, &vp_reg->kdfc_trpl_fifo_0_ctrl);
	writeq((u64)0, &vp_reg->kdfc_trpl_fifo_0_wb_address);
	wmb();
	vpath_stride = readq(&hldev->toc_reg->toc_kdfc_vpath_stride);

	vpath->nofl_db =
		(struct __vxge_hw_non_offload_db_wrapper __iomem *)
		(hldev->kdfc + (vp_id *
		VXGE_HW_TOC_KDFC_VPATH_STRIDE_GET_TOC_KDFC_VPATH_STRIDE(
					vpath_stride)));
exit:
	return status;
}

/*
 * __vxge_hw_vpath_mac_configure
 * This routine configures the mac of virtual path using the config passed
 */
static enum vxge_hw_status
__vxge_hw_vpath_mac_configure(struct __vxge_hw_device *hldev, u32 vp_id)
{
	u64 val64;
	struct __vxge_hw_virtualpath *vpath;
	struct vxge_hw_vp_config *vp_config;
	struct vxge_hw_vpath_reg __iomem *vp_reg;

	vpath = &hldev->virtual_paths[vp_id];
	vp_reg = vpath->vp_reg;
	vp_config = vpath->vp_config;

	writeq(VXGE_HW_XMAC_VSPORT_CHOICE_VSPORT_NUMBER(
			vpath->vsport_number), &vp_reg->xmac_vsport_choice);

	if (vp_config->ring.enable == VXGE_HW_RING_ENABLE) {

		val64 = readq(&vp_reg->xmac_rpa_vcfg);

		if (vp_config->rpa_strip_vlan_tag !=
			VXGE_HW_VPATH_RPA_STRIP_VLAN_TAG_USE_FLASH_DEFAULT) {
			if (vp_config->rpa_strip_vlan_tag)
				val64 |= VXGE_HW_XMAC_RPA_VCFG_STRIP_VLAN_TAG;
			else
				val64 &= ~VXGE_HW_XMAC_RPA_VCFG_STRIP_VLAN_TAG;
		}

		writeq(val64, &vp_reg->xmac_rpa_vcfg);
		val64 = readq(&vp_reg->rxmac_vcfg0);

		if (vp_config->mtu !=
				VXGE_HW_VPATH_USE_FLASH_DEFAULT_INITIAL_MTU) {
			val64 &= ~VXGE_HW_RXMAC_VCFG0_RTS_MAX_FRM_LEN(0x3fff);
			if ((vp_config->mtu  +
				VXGE_HW_MAC_HEADER_MAX_SIZE) < vpath->max_mtu)
				val64 |= VXGE_HW_RXMAC_VCFG0_RTS_MAX_FRM_LEN(
					vp_config->mtu  +
					VXGE_HW_MAC_HEADER_MAX_SIZE);
			else
				val64 |= VXGE_HW_RXMAC_VCFG0_RTS_MAX_FRM_LEN(
					vpath->max_mtu);
		}

		writeq(val64, &vp_reg->rxmac_vcfg0);

		val64 = readq(&vp_reg->rxmac_vcfg1);

		val64 &= ~(VXGE_HW_RXMAC_VCFG1_RTS_RTH_MULTI_IT_BD_MODE(0x3) |
			VXGE_HW_RXMAC_VCFG1_RTS_RTH_MULTI_IT_EN_MODE);

		if (hldev->config.rth_it_type ==
				VXGE_HW_RTH_IT_TYPE_MULTI_IT) {
			val64 |= VXGE_HW_RXMAC_VCFG1_RTS_RTH_MULTI_IT_BD_MODE(
				0x2) |
				VXGE_HW_RXMAC_VCFG1_RTS_RTH_MULTI_IT_EN_MODE;
		}

		writeq(val64, &vp_reg->rxmac_vcfg1);
	}
	return VXGE_HW_OK;
}

/*
 * __vxge_hw_vpath_tim_configure
 * This routine configures the tim registers of virtual path using the config
 * passed
 */
static enum vxge_hw_status
__vxge_hw_vpath_tim_configure(struct __vxge_hw_device *hldev, u32 vp_id)
{
	u64 val64;
	struct __vxge_hw_virtualpath *vpath;
	struct vxge_hw_vpath_reg __iomem *vp_reg;
	struct vxge_hw_vp_config *config;

	vpath = &hldev->virtual_paths[vp_id];
	vp_reg = vpath->vp_reg;
	config = vpath->vp_config;

	writeq(0, &vp_reg->tim_dest_addr);
	writeq(0, &vp_reg->tim_vpath_map);
	writeq(0, &vp_reg->tim_bitmap);
	writeq(0, &vp_reg->tim_remap);

	if (config->ring.enable == VXGE_HW_RING_ENABLE)
		writeq(VXGE_HW_TIM_RING_ASSN_INT_NUM(
			(vp_id * VXGE_HW_MAX_INTR_PER_VP) +
			VXGE_HW_VPATH_INTR_RX), &vp_reg->tim_ring_assn);

	val64 = readq(&vp_reg->tim_pci_cfg);
	val64 |= VXGE_HW_TIM_PCI_CFG_ADD_PAD;
	writeq(val64, &vp_reg->tim_pci_cfg);

	if (config->fifo.enable == VXGE_HW_FIFO_ENABLE) {

		val64 = readq(&vp_reg->tim_cfg1_int_num[VXGE_HW_VPATH_INTR_TX]);

		if (config->tti.btimer_val != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_BTIMER_VAL(
				0x3ffffff);
			val64 |= VXGE_HW_TIM_CFG1_INT_NUM_BTIMER_VAL(
					config->tti.btimer_val);
		}

		val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_BITMP_EN;

		if (config->tti.timer_ac_en != VXGE_HW_USE_FLASH_DEFAULT) {
			if (config->tti.timer_ac_en)
				val64 |= VXGE_HW_TIM_CFG1_INT_NUM_TIMER_AC;
			else
				val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_TIMER_AC;
		}

		if (config->tti.timer_ci_en != VXGE_HW_USE_FLASH_DEFAULT) {
			if (config->tti.timer_ci_en)
				val64 |= VXGE_HW_TIM_CFG1_INT_NUM_TIMER_CI;
			else
				val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_TIMER_CI;
		}

		if (config->tti.urange_a != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_URNG_A(0x3f);
			val64 |= VXGE_HW_TIM_CFG1_INT_NUM_URNG_A(
					config->tti.urange_a);
		}

		if (config->tti.urange_b != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_URNG_B(0x3f);
			val64 |= VXGE_HW_TIM_CFG1_INT_NUM_URNG_B(
					config->tti.urange_b);
		}

		if (config->tti.urange_c != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_URNG_C(0x3f);
			val64 |= VXGE_HW_TIM_CFG1_INT_NUM_URNG_C(
					config->tti.urange_c);
		}

		writeq(val64, &vp_reg->tim_cfg1_int_num[VXGE_HW_VPATH_INTR_TX]);
		vpath->tim_tti_cfg1_saved = val64;

		val64 = readq(&vp_reg->tim_cfg2_int_num[VXGE_HW_VPATH_INTR_TX]);

		if (config->tti.uec_a != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG2_INT_NUM_UEC_A(0xffff);
			val64 |= VXGE_HW_TIM_CFG2_INT_NUM_UEC_A(
						config->tti.uec_a);
		}

		if (config->tti.uec_b != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG2_INT_NUM_UEC_B(0xffff);
			val64 |= VXGE_HW_TIM_CFG2_INT_NUM_UEC_B(
						config->tti.uec_b);
		}

		if (config->tti.uec_c != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG2_INT_NUM_UEC_C(0xffff);
			val64 |= VXGE_HW_TIM_CFG2_INT_NUM_UEC_C(
						config->tti.uec_c);
		}

		if (config->tti.uec_d != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG2_INT_NUM_UEC_D(0xffff);
			val64 |= VXGE_HW_TIM_CFG2_INT_NUM_UEC_D(
						config->tti.uec_d);
		}

		writeq(val64, &vp_reg->tim_cfg2_int_num[VXGE_HW_VPATH_INTR_TX]);
		val64 = readq(&vp_reg->tim_cfg3_int_num[VXGE_HW_VPATH_INTR_TX]);

		if (config->tti.timer_ri_en != VXGE_HW_USE_FLASH_DEFAULT) {
			if (config->tti.timer_ri_en)
				val64 |= VXGE_HW_TIM_CFG3_INT_NUM_TIMER_RI;
			else
				val64 &= ~VXGE_HW_TIM_CFG3_INT_NUM_TIMER_RI;
		}

		if (config->tti.rtimer_val != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG3_INT_NUM_RTIMER_VAL(
					0x3ffffff);
			val64 |= VXGE_HW_TIM_CFG3_INT_NUM_RTIMER_VAL(
					config->tti.rtimer_val);
		}

		if (config->tti.util_sel != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG3_INT_NUM_UTIL_SEL(0x3f);
			val64 |= VXGE_HW_TIM_CFG3_INT_NUM_UTIL_SEL(vp_id);
		}

		if (config->tti.ltimer_val != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG3_INT_NUM_LTIMER_VAL(
					0x3ffffff);
			val64 |= VXGE_HW_TIM_CFG3_INT_NUM_LTIMER_VAL(
					config->tti.ltimer_val);
		}

		writeq(val64, &vp_reg->tim_cfg3_int_num[VXGE_HW_VPATH_INTR_TX]);
		vpath->tim_tti_cfg3_saved = val64;
	}

	if (config->ring.enable == VXGE_HW_RING_ENABLE) {

		val64 = readq(&vp_reg->tim_cfg1_int_num[VXGE_HW_VPATH_INTR_RX]);

		if (config->rti.btimer_val != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_BTIMER_VAL(
					0x3ffffff);
			val64 |= VXGE_HW_TIM_CFG1_INT_NUM_BTIMER_VAL(
					config->rti.btimer_val);
		}

		val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_BITMP_EN;

		if (config->rti.timer_ac_en != VXGE_HW_USE_FLASH_DEFAULT) {
			if (config->rti.timer_ac_en)
				val64 |= VXGE_HW_TIM_CFG1_INT_NUM_TIMER_AC;
			else
				val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_TIMER_AC;
		}

		if (config->rti.timer_ci_en != VXGE_HW_USE_FLASH_DEFAULT) {
			if (config->rti.timer_ci_en)
				val64 |= VXGE_HW_TIM_CFG1_INT_NUM_TIMER_CI;
			else
				val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_TIMER_CI;
		}

		if (config->rti.urange_a != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_URNG_A(0x3f);
			val64 |= VXGE_HW_TIM_CFG1_INT_NUM_URNG_A(
					config->rti.urange_a);
		}

		if (config->rti.urange_b != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_URNG_B(0x3f);
			val64 |= VXGE_HW_TIM_CFG1_INT_NUM_URNG_B(
					config->rti.urange_b);
		}

		if (config->rti.urange_c != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG1_INT_NUM_URNG_C(0x3f);
			val64 |= VXGE_HW_TIM_CFG1_INT_NUM_URNG_C(
					config->rti.urange_c);
		}

		writeq(val64, &vp_reg->tim_cfg1_int_num[VXGE_HW_VPATH_INTR_RX]);
		vpath->tim_rti_cfg1_saved = val64;

		val64 = readq(&vp_reg->tim_cfg2_int_num[VXGE_HW_VPATH_INTR_RX]);

		if (config->rti.uec_a != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG2_INT_NUM_UEC_A(0xffff);
			val64 |= VXGE_HW_TIM_CFG2_INT_NUM_UEC_A(
						config->rti.uec_a);
		}

		if (config->rti.uec_b != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG2_INT_NUM_UEC_B(0xffff);
			val64 |= VXGE_HW_TIM_CFG2_INT_NUM_UEC_B(
						config->rti.uec_b);
		}

		if (config->rti.uec_c != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG2_INT_NUM_UEC_C(0xffff);
			val64 |= VXGE_HW_TIM_CFG2_INT_NUM_UEC_C(
						config->rti.uec_c);
		}

		if (config->rti.uec_d != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG2_INT_NUM_UEC_D(0xffff);
			val64 |= VXGE_HW_TIM_CFG2_INT_NUM_UEC_D(
						config->rti.uec_d);
		}

		writeq(val64, &vp_reg->tim_cfg2_int_num[VXGE_HW_VPATH_INTR_RX]);
		val64 = readq(&vp_reg->tim_cfg3_int_num[VXGE_HW_VPATH_INTR_RX]);

		if (config->rti.timer_ri_en != VXGE_HW_USE_FLASH_DEFAULT) {
			if (config->rti.timer_ri_en)
				val64 |= VXGE_HW_TIM_CFG3_INT_NUM_TIMER_RI;
			else
				val64 &= ~VXGE_HW_TIM_CFG3_INT_NUM_TIMER_RI;
		}

		if (config->rti.rtimer_val != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG3_INT_NUM_RTIMER_VAL(
					0x3ffffff);
			val64 |= VXGE_HW_TIM_CFG3_INT_NUM_RTIMER_VAL(
					config->rti.rtimer_val);
		}

		if (config->rti.util_sel != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG3_INT_NUM_UTIL_SEL(0x3f);
			val64 |= VXGE_HW_TIM_CFG3_INT_NUM_UTIL_SEL(vp_id);
		}

		if (config->rti.ltimer_val != VXGE_HW_USE_FLASH_DEFAULT) {
			val64 &= ~VXGE_HW_TIM_CFG3_INT_NUM_LTIMER_VAL(
					0x3ffffff);
			val64 |= VXGE_HW_TIM_CFG3_INT_NUM_LTIMER_VAL(
					config->rti.ltimer_val);
		}

		writeq(val64, &vp_reg->tim_cfg3_int_num[VXGE_HW_VPATH_INTR_RX]);
		vpath->tim_rti_cfg3_saved = val64;
	}

	val64 = 0;
	writeq(val64, &vp_reg->tim_cfg1_int_num[VXGE_HW_VPATH_INTR_EINTA]);
	writeq(val64, &vp_reg->tim_cfg2_int_num[VXGE_HW_VPATH_INTR_EINTA]);
	writeq(val64, &vp_reg->tim_cfg3_int_num[VXGE_HW_VPATH_INTR_EINTA]);
	writeq(val64, &vp_reg->tim_cfg1_int_num[VXGE_HW_VPATH_INTR_BMAP]);
	writeq(val64, &vp_reg->tim_cfg2_int_num[VXGE_HW_VPATH_INTR_BMAP]);
	writeq(val64, &vp_reg->tim_cfg3_int_num[VXGE_HW_VPATH_INTR_BMAP]);

	val64 = VXGE_HW_TIM_WRKLD_CLC_WRKLD_EVAL_PRD(150);
	val64 |= VXGE_HW_TIM_WRKLD_CLC_WRKLD_EVAL_DIV(0);
	val64 |= VXGE_HW_TIM_WRKLD_CLC_CNT_RX_TX(3);
	writeq(val64, &vp_reg->tim_wrkld_clc);

	return VXGE_HW_OK;
}

/*
 * __vxge_hw_vpath_initialize
 * This routine is the final phase of init which initializes the
 * registers of the vpath using the configuration passed.
 */
static enum vxge_hw_status
__vxge_hw_vpath_initialize(struct __vxge_hw_device *hldev, u32 vp_id)
{
	u64 val64;
	u32 val32;
	enum vxge_hw_status status = VXGE_HW_OK;
	struct __vxge_hw_virtualpath *vpath;
	struct vxge_hw_vpath_reg __iomem *vp_reg;

	vpath = &hldev->virtual_paths[vp_id];

	if (!(hldev->vpath_assignments & vxge_mBIT(vp_id))) {
		status = VXGE_HW_ERR_VPATH_NOT_AVAILABLE;
		goto exit;
	}
	vp_reg = vpath->vp_reg;

	status =  __vxge_hw_vpath_swapper_set(vpath->vp_reg);
	if (status != VXGE_HW_OK)
		goto exit;

	status =  __vxge_hw_vpath_mac_configure(hldev, vp_id);
	if (status != VXGE_HW_OK)
		goto exit;

	status =  __vxge_hw_vpath_kdfc_configure(hldev, vp_id);
	if (status != VXGE_HW_OK)
		goto exit;

	status = __vxge_hw_vpath_tim_configure(hldev, vp_id);
	if (status != VXGE_HW_OK)
		goto exit;

	val64 = readq(&vp_reg->rtdma_rd_optimization_ctrl);

	/* Get MRRS value from device control */
	status  = __vxge_hw_vpath_pci_read(vpath, 1, 0x78, &val32);
	if (status == VXGE_HW_OK) {
		val32 = (val32 & VXGE_HW_PCI_EXP_DEVCTL_READRQ) >> 12;
		val64 &=
		    ~(VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_FB_FILL_THRESH(7));
		val64 |=
		    VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_FB_FILL_THRESH(val32);

		val64 |= VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_FB_WAIT_FOR_SPACE;
	}

	val64 &= ~(VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_FB_ADDR_BDRY(7));
	val64 |=
	    VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_FB_ADDR_BDRY(
		    VXGE_HW_MAX_PAYLOAD_SIZE_512);

	val64 |= VXGE_HW_RTDMA_RD_OPTIMIZATION_CTRL_FB_ADDR_BDRY_EN;
	writeq(val64, &vp_reg->rtdma_rd_optimization_ctrl);

exit:
	return status;
}

/*
 * __vxge_hw_vp_terminate - Terminate Virtual Path structure
 * This routine closes all channels it opened and freeup memory
 */
static void __vxge_hw_vp_terminate(struct __vxge_hw_device *hldev, u32 vp_id)
{
	struct __vxge_hw_virtualpath *vpath;

	vpath = &hldev->virtual_paths[vp_id];

	if (vpath->vp_open == VXGE_HW_VP_NOT_OPEN)
		goto exit;

	VXGE_HW_DEVICE_TIM_INT_MASK_RESET(vpath->hldev->tim_int_mask0,
		vpath->hldev->tim_int_mask1, vpath->vp_id);
	hldev->stats.hw_dev_info_stats.vpath_info[vpath->vp_id] = NULL;

	/* If the whole struct __vxge_hw_virtualpath is zeroed, nothing will
	 * work after the interface is brought down.
	 */
	spin_lock(&vpath->lock);
	vpath->vp_open = VXGE_HW_VP_NOT_OPEN;
	spin_unlock(&vpath->lock);

	vpath->vpmgmt_reg = NULL;
	vpath->nofl_db = NULL;
	vpath->max_mtu = 0;
	vpath->vsport_number = 0;
	vpath->max_kdfc_db = 0;
	vpath->max_nofl_db = 0;
	vpath->ringh = NULL;
	vpath->fifoh = NULL;
	memset(&vpath->vpath_handles, 0, sizeof(struct list_head));
	vpath->stats_block = NULL;
	vpath->hw_stats = NULL;
	vpath->hw_stats_sav = NULL;
	vpath->sw_stats = NULL;

exit:
	return;
}

/*
 * __vxge_hw_vp_initialize - Initialize Virtual Path structure
 * This routine is the initial phase of init which resets the vpath and
 * initializes the software support structures.
 */
static enum vxge_hw_status
__vxge_hw_vp_initialize(struct __vxge_hw_device *hldev, u32 vp_id,
			struct vxge_hw_vp_config *config)
{
	struct __vxge_hw_virtualpath *vpath;
	enum vxge_hw_status status = VXGE_HW_OK;

	if (!(hldev->vpath_assignments & vxge_mBIT(vp_id))) {
		status = VXGE_HW_ERR_VPATH_NOT_AVAILABLE;
		goto exit;
	}

	vpath = &hldev->virtual_paths[vp_id];

	spin_lock_init(&vpath->lock);
	vpath->vp_id = vp_id;
	vpath->vp_open = VXGE_HW_VP_OPEN;
	vpath->hldev = hldev;
	vpath->vp_config = config;
	vpath->vp_reg = hldev->vpath_reg[vp_id];
	vpath->vpmgmt_reg = hldev->vpmgmt_reg[vp_id];

	__vxge_hw_vpath_reset(hldev, vp_id);

	status = __vxge_hw_vpath_reset_check(vpath);
	if (status != VXGE_HW_OK) {
		memset(vpath, 0, sizeof(struct __vxge_hw_virtualpath));
		goto exit;
	}

	status = __vxge_hw_vpath_mgmt_read(hldev, vpath);
	if (status != VXGE_HW_OK) {
		memset(vpath, 0, sizeof(struct __vxge_hw_virtualpath));
		goto exit;
	}

	INIT_LIST_HEAD(&vpath->vpath_handles);

	vpath->sw_stats = &hldev->stats.sw_dev_info_stats.vpath_info[vp_id];

	VXGE_HW_DEVICE_TIM_INT_MASK_SET(hldev->tim_int_mask0,
		hldev->tim_int_mask1, vp_id);

	status = __vxge_hw_vpath_initialize(hldev, vp_id);
	if (status != VXGE_HW_OK)
		__vxge_hw_vp_terminate(hldev, vp_id);
exit:
	return status;
}

/*
 * vxge_hw_vpath_mtu_set - Set MTU.
 * Set new MTU value. Example, to use jumbo frames:
 * vxge_hw_vpath_mtu_set(my_device, 9600);
 */
enum vxge_hw_status
vxge_hw_vpath_mtu_set(struct __vxge_hw_vpath_handle *vp, u32 new_mtu)
{
	u64 val64;
	enum vxge_hw_status status = VXGE_HW_OK;
	struct __vxge_hw_virtualpath *vpath;

	if (vp == NULL) {
		status = VXGE_HW_ERR_INVALID_HANDLE;
		goto exit;
	}
	vpath = vp->vpath;

	new_mtu += VXGE_HW_MAC_HEADER_MAX_SIZE;

	if ((new_mtu < VXGE_HW_MIN_MTU) || (new_mtu > vpath->max_mtu))
		status = VXGE_HW_ERR_INVALID_MTU_SIZE;

	val64 = readq(&vpath->vp_reg->rxmac_vcfg0);

	val64 &= ~VXGE_HW_RXMAC_VCFG0_RTS_MAX_FRM_LEN(0x3fff);
	val64 |= VXGE_HW_RXMAC_VCFG0_RTS_MAX_FRM_LEN(new_mtu);

	writeq(val64, &vpath->vp_reg->rxmac_vcfg0);

	vpath->vp_config->mtu = new_mtu - VXGE_HW_MAC_HEADER_MAX_SIZE;

exit:
	return status;
}

/*
 * vxge_hw_vpath_stats_enable - Enable vpath h/wstatistics.
 * Enable the DMA vpath statistics. The function is to be called to re-enable
 * the adapter to update stats into the host memory
 */
static enum vxge_hw_status
vxge_hw_vpath_stats_enable(struct __vxge_hw_vpath_handle *vp)
{
	enum vxge_hw_status status = VXGE_HW_OK;
	struct __vxge_hw_virtualpath *vpath;

	vpath = vp->vpath;

	if (vpath->vp_open == VXGE_HW_VP_NOT_OPEN) {
		status = VXGE_HW_ERR_VPATH_NOT_OPEN;
		goto exit;
	}

	memcpy(vpath->hw_stats_sav, vpath->hw_stats,
			sizeof(struct vxge_hw_vpath_stats_hw_info));

	status = __vxge_hw_vpath_stats_get(vpath, vpath->hw_stats);
exit:
	return status;
}

/*
 * __vxge_hw_blockpool_block_allocate - Allocates a block from block pool
 * This function allocates a block from block pool or from the system
 */
static struct __vxge_hw_blockpool_entry *
__vxge_hw_blockpool_block_allocate(struct __vxge_hw_device *devh, u32 size)
{
	struct __vxge_hw_blockpool_entry *entry = NULL;
	struct __vxge_hw_blockpool  *blockpool;

	blockpool = &devh->block_pool;

	if (size == blockpool->block_size) {

		if (!list_empty(&blockpool->free_block_list))
			entry = (struct __vxge_hw_blockpool_entry *)
				list_first_entry(&blockpool->free_block_list,
					struct __vxge_hw_blockpool_entry,
					item);

		if (entry != NULL) {
			list_del(&entry->item);
			blockpool->pool_size--;
		}
	}

	if (entry != NULL)
		__vxge_hw_blockpool_blocks_add(blockpool);

	return entry;
}

/*
 * vxge_hw_vpath_open - Open a virtual path on a given adapter
 * This function is used to open access to virtual path of an
 * adapter for offload, GRO operations. This function returns
 * synchronously.
 */
enum vxge_hw_status
vxge_hw_vpath_open(struct __vxge_hw_device *hldev,
		   struct vxge_hw_vpath_attr *attr,
		   struct __vxge_hw_vpath_handle **vpath_handle)
{
	struct __vxge_hw_virtualpath *vpath;
	struct __vxge_hw_vpath_handle *vp;
	enum vxge_hw_status status;

	vpath = &hldev->virtual_paths[attr->vp_id];

	if (vpath->vp_open == VXGE_HW_VP_OPEN) {
		status = VXGE_HW_ERR_INVALID_STATE;
		goto vpath_open_exit1;
	}

	status = __vxge_hw_vp_initialize(hldev, attr->vp_id,
			&hldev->config.vp_config[attr->vp_id]);
	if (status != VXGE_HW_OK)
		goto vpath_open_exit1;

	vp = vzalloc(sizeof(struct __vxge_hw_vpath_handle));
	if (vp == NULL) {
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		goto vpath_open_exit2;
	}

	vp->vpath = vpath;

	if (vpath->vp_config->fifo.enable == VXGE_HW_FIFO_ENABLE) {
		status = __vxge_hw_fifo_create(vp, &attr->fifo_attr);
		if (status != VXGE_HW_OK)
			goto vpath_open_exit6;
	}

	if (vpath->vp_config->ring.enable == VXGE_HW_RING_ENABLE) {
		status = __vxge_hw_ring_create(vp, &attr->ring_attr);
		if (status != VXGE_HW_OK)
			goto vpath_open_exit7;

		__vxge_hw_vpath_prc_configure(hldev, attr->vp_id);
	}

	vpath->fifoh->tx_intr_num =
		(attr->vp_id * VXGE_HW_MAX_INTR_PER_VP)  +
			VXGE_HW_VPATH_INTR_TX;

	vpath->stats_block = __vxge_hw_blockpool_block_allocate(hldev,
				VXGE_HW_BLOCK_SIZE);
	if (vpath->stats_block == NULL) {
		status = VXGE_HW_ERR_OUT_OF_MEMORY;
		goto vpath_open_exit8;
	}

	vpath->hw_stats = vpath->stats_block->memblock;
	memset(vpath->hw_stats, 0,
		sizeof(struct vxge_hw_vpath_stats_hw_info));

	hldev->stats.hw_dev_info_stats.vpath_info[attr->vp_id] =
						vpath->hw_stats;

	vpath->hw_stats_sav =
		&hldev->stats.hw_dev_info_stats.vpath_info_sav[attr->vp_id];
	memset(vpath->hw_stats_sav, 0,
			sizeof(struct vxge_hw_vpath_stats_hw_info));

	writeq(vpath->stats_block->dma_addr, &vpath->vp_reg->stats_cfg);

	status = vxge_hw_vpath_stats_enable(vp);
	if (status != VXGE_HW_OK)
		goto vpath_open_exit8;

	list_add(&vp->item, &vpath->vpath_handles);

	hldev->vpaths_deployed |= vxge_mBIT(vpath->vp_id);

	*vpath_handle = vp;

	attr->fifo_attr.userdata = vpath->fifoh;
	attr->ring_attr.userdata = vpath->ringh;

	return VXGE_HW_OK;

vpath_open_exit8:
	if (vpath->ringh != NULL)
		__vxge_hw_ring_delete(vp);
vpath_open_exit7:
	if (vpath->fifoh != NULL)
		__vxge_hw_fifo_delete(vp);
vpath_open_exit6:
	vfree(vp);
vpath_open_exit2:
	__vxge_hw_vp_terminate(hldev, attr->vp_id);
vpath_open_exit1:

	return status;
}

/**
 * vxge_hw_vpath_rx_doorbell_post - Close the handle got from previous vpath
 * (vpath) open
 * @vp: Handle got from previous vpath open
 *
 * This function is used to close access to virtual path opened
 * earlier.
 */
void vxge_hw_vpath_rx_doorbell_init(struct __vxge_hw_vpath_handle *vp)
{
	struct __vxge_hw_virtualpath *vpath = vp->vpath;
	struct __vxge_hw_ring *ring = vpath->ringh;
	struct vxgedev *vdev = netdev_priv(vpath->hldev->ndev);
	u64 new_count, val64, val164;

	if (vdev->titan1) {
		new_count = readq(&vpath->vp_reg->rxdmem_size);
		new_count &= 0x1fff;
	} else
		new_count = ring->config->ring_blocks * VXGE_HW_BLOCK_SIZE / 8;

	val164 = VXGE_HW_RXDMEM_SIZE_PRC_RXDMEM_SIZE(new_count);

	writeq(VXGE_HW_PRC_RXD_DOORBELL_NEW_QW_CNT(val164),
		&vpath->vp_reg->prc_rxd_doorbell);
	readl(&vpath->vp_reg->prc_rxd_doorbell);

	val164 /= 2;
	val64 = readq(&vpath->vp_reg->prc_cfg6);
	val64 = VXGE_HW_PRC_CFG6_RXD_SPAT(val64);
	val64 &= 0x1ff;

	/*
	 * Each RxD is of 4 qwords
	 */
	new_count -= (val64 + 1);
	val64 = min(val164, new_count) / 4;

	ring->rxds_limit = min(ring->rxds_limit, val64);
	if (ring->rxds_limit < 4)
		ring->rxds_limit = 4;
}

/*
 * __vxge_hw_blockpool_block_free - Frees a block from block pool
 * @devh: Hal device
 * @entry: Entry of block to be freed
 *
 * This function frees a block from block pool
 */
static void
__vxge_hw_blockpool_block_free(struct __vxge_hw_device *devh,
			       struct __vxge_hw_blockpool_entry *entry)
{
	struct __vxge_hw_blockpool  *blockpool;

	blockpool = &devh->block_pool;

	if (entry->length == blockpool->block_size) {
		list_add(&entry->item, &blockpool->free_block_list);
		blockpool->pool_size++;
	}

	__vxge_hw_blockpool_blocks_remove(blockpool);
}

/*
 * vxge_hw_vpath_close - Close the handle got from previous vpath (vpath) open
 * This function is used to close access to virtual path opened
 * earlier.
 */
enum vxge_hw_status vxge_hw_vpath_close(struct __vxge_hw_vpath_handle *vp)
{
	struct __vxge_hw_virtualpath *vpath = NULL;
	struct __vxge_hw_device *devh = NULL;
	u32 vp_id = vp->vpath->vp_id;
	u32 is_empty = TRUE;
	enum vxge_hw_status status = VXGE_HW_OK;

	vpath = vp->vpath;
	devh = vpath->hldev;

	if (vpath->vp_open == VXGE_HW_VP_NOT_OPEN) {
		status = VXGE_HW_ERR_VPATH_NOT_OPEN;
		goto vpath_close_exit;
	}

	list_del(&vp->item);

	if (!list_empty(&vpath->vpath_handles)) {
		list_add(&vp->item, &vpath->vpath_handles);
		is_empty = FALSE;
	}

	if (!is_empty) {
		status = VXGE_HW_FAIL;
		goto vpath_close_exit;
	}

	devh->vpaths_deployed &= ~vxge_mBIT(vp_id);

	if (vpath->ringh != NULL)
		__vxge_hw_ring_delete(vp);

	if (vpath->fifoh != NULL)
		__vxge_hw_fifo_delete(vp);

	if (vpath->stats_block != NULL)
		__vxge_hw_blockpool_block_free(devh, vpath->stats_block);

	vfree(vp);

	__vxge_hw_vp_terminate(devh, vp_id);

vpath_close_exit:
	return status;
}

/*
 * vxge_hw_vpath_reset - Resets vpath
 * This function is used to request a reset of vpath
 */
enum vxge_hw_status vxge_hw_vpath_reset(struct __vxge_hw_vpath_handle *vp)
{
	enum vxge_hw_status status;
	u32 vp_id;
	struct __vxge_hw_virtualpath *vpath = vp->vpath;

	vp_id = vpath->vp_id;

	if (vpath->vp_open == VXGE_HW_VP_NOT_OPEN) {
		status = VXGE_HW_ERR_VPATH_NOT_OPEN;
		goto exit;
	}

	status = __vxge_hw_vpath_reset(vpath->hldev, vp_id);
	if (status == VXGE_HW_OK)
		vpath->sw_stats->soft_reset_cnt++;
exit:
	return status;
}

/*
 * vxge_hw_vpath_recover_from_reset - Poll for reset complete and re-initialize.
 * This function poll's for the vpath reset completion and re initializes
 * the vpath.
 */
enum vxge_hw_status
vxge_hw_vpath_recover_from_reset(struct __vxge_hw_vpath_handle *vp)
{
	struct __vxge_hw_virtualpath *vpath = NULL;
	enum vxge_hw_status status;
	struct __vxge_hw_device *hldev;
	u32 vp_id;

	vp_id = vp->vpath->vp_id;
	vpath = vp->vpath;
	hldev = vpath->hldev;

	if (vpath->vp_open == VXGE_HW_VP_NOT_OPEN) {
		status = VXGE_HW_ERR_VPATH_NOT_OPEN;
		goto exit;
	}

	status = __vxge_hw_vpath_reset_check(vpath);
	if (status != VXGE_HW_OK)
		goto exit;

	status = __vxge_hw_vpath_sw_reset(hldev, vp_id);
	if (status != VXGE_HW_OK)
		goto exit;

	status = __vxge_hw_vpath_initialize(hldev, vp_id);
	if (status != VXGE_HW_OK)
		goto exit;

	if (vpath->ringh != NULL)
		__vxge_hw_vpath_prc_configure(hldev, vp_id);

	memset(vpath->hw_stats, 0,
		sizeof(struct vxge_hw_vpath_stats_hw_info));

	memset(vpath->hw_stats_sav, 0,
		sizeof(struct vxge_hw_vpath_stats_hw_info));

	writeq(vpath->stats_block->dma_addr,
		&vpath->vp_reg->stats_cfg);

	status = vxge_hw_vpath_stats_enable(vp);

exit:
	return status;
}

/*
 * vxge_hw_vpath_enable - Enable vpath.
 * This routine clears the vpath reset thereby enabling a vpath
 * to start forwarding frames and generating interrupts.
 */
void
vxge_hw_vpath_enable(struct __vxge_hw_vpath_handle *vp)
{
	struct __vxge_hw_device *hldev;
	u64 val64;

	hldev = vp->vpath->hldev;

	val64 = VXGE_HW_CMN_RSTHDLR_CFG1_CLR_VPATH_RESET(
		1 << (16 - vp->vpath->vp_id));

	__vxge_hw_pio_mem_write32_upper((u32)vxge_bVALn(val64, 0, 32),
		&hldev->common_reg->cmn_rsthdlr_cfg1);
}
