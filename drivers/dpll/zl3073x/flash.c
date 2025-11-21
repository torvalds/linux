// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/minmax.h>
#include <linux/netlink.h>
#include <linux/sched/signal.h>
#include <linux/sizes.h>
#include <linux/sprintf.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <net/devlink.h>

#include "core.h"
#include "devlink.h"
#include "flash.h"

#define ZL_FLASH_ERR_PFX "FW update failed: "
#define ZL_FLASH_ERR_MSG(_extack, _msg, ...)				\
	NL_SET_ERR_MSG_FMT_MOD((_extack), ZL_FLASH_ERR_PFX _msg,	\
			       ## __VA_ARGS__)

/**
 * zl3073x_flash_download - Download image block to device memory
 * @zldev: zl3073x device structure
 * @component: name of the component to be downloaded
 * @addr: device memory target address
 * @data: pointer to data to download
 * @size: size of data to download
 * @extack: netlink extack pointer to report errors
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_flash_download(struct zl3073x_dev *zldev, const char *component,
		       u32 addr, const void *data, size_t size,
		       struct netlink_ext_ack *extack)
{
#define ZL_CHECK_DELAY	5000 /* Check for interrupt each 5 seconds */
	unsigned long check_time;
	const void *ptr, *end;
	int rc = 0;

	dev_dbg(zldev->dev, "Downloading %zu bytes to device memory at 0x%0x\n",
		size, addr);

	check_time = jiffies + msecs_to_jiffies(ZL_CHECK_DELAY);

	for (ptr = data, end = data + size; ptr < end; ptr += 4, addr += 4) {
		/* Write current word to HW memory */
		rc = zl3073x_write_hwreg(zldev, addr,
					 get_unaligned((u32 *)ptr));
		if (rc) {
			ZL_FLASH_ERR_MSG(extack,
					 "failed to write to memory at 0x%0x",
					 addr);
			return rc;
		}

		if (time_is_before_jiffies(check_time)) {
			if (signal_pending(current)) {
				ZL_FLASH_ERR_MSG(extack,
						 "Flashing interrupted");
				return -EINTR;
			}

			check_time = jiffies + msecs_to_jiffies(ZL_CHECK_DELAY);
		}

		/* Report status each 1 kB block */
		if ((ptr - data) % 1024 == 0)
			zl3073x_devlink_flash_notify(zldev, "Downloading image",
						     component, ptr - data,
						     size);
	}

	zl3073x_devlink_flash_notify(zldev, "Downloading image", component,
				     ptr - data, size);

	dev_dbg(zldev->dev, "%zu bytes downloaded to device memory\n", size);

	return rc;
}

/**
 * zl3073x_flash_error_check - Check for flash utility errors
 * @zldev: zl3073x device structure
 * @extack: netlink extack pointer to report errors
 *
 * The function checks for errors detected by the flash utility and
 * reports them if any were found.
 *
 * Return: 0 on success, -EIO when errors are detected
 */
static int
zl3073x_flash_error_check(struct zl3073x_dev *zldev,
			  struct netlink_ext_ack *extack)
{
	u32 count, cause;
	int rc;

	rc = zl3073x_read_u32(zldev, ZL_REG_ERROR_COUNT, &count);
	if (rc)
		return rc;
	else if (!count)
		return 0; /* No error */

	rc = zl3073x_read_u32(zldev, ZL_REG_ERROR_CAUSE, &cause);
	if (rc)
		return rc;

	/* Report errors */
	ZL_FLASH_ERR_MSG(extack,
			 "utility error occurred: count=%u cause=0x%x", count,
			 cause);

	return -EIO;
}

/**
 * zl3073x_flash_wait_ready - Check or wait for utility to be ready to flash
 * @zldev: zl3073x device structure
 * @timeout_ms: timeout for the waiting
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_flash_wait_ready(struct zl3073x_dev *zldev, unsigned int timeout_ms)
{
#define ZL_FLASH_POLL_DELAY_MS	100
	unsigned long timeout;
	int rc, i;

	dev_dbg(zldev->dev, "Waiting for flashing to be ready\n");

	timeout = jiffies + msecs_to_jiffies(timeout_ms);

	for (i = 0; time_is_after_jiffies(timeout); i++) {
		u8 value;

		/* Check for interrupt each 1s */
		if (i > 9) {
			if (signal_pending(current))
				return -EINTR;
			i = 0;
		}

		rc = zl3073x_read_u8(zldev, ZL_REG_WRITE_FLASH, &value);
		if (rc)
			return rc;

		value = FIELD_GET(ZL_WRITE_FLASH_OP, value);

		if (value == ZL_WRITE_FLASH_OP_DONE)
			return 0; /* Successfully done */

		msleep(ZL_FLASH_POLL_DELAY_MS);
	}

	return -ETIMEDOUT;
}

/**
 * zl3073x_flash_cmd_wait - Perform flash operation and wait for finish
 * @zldev: zl3073x device structure
 * @operation: operation to perform
 * @extack: netlink extack pointer to report errors
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_flash_cmd_wait(struct zl3073x_dev *zldev, u32 operation,
		       struct netlink_ext_ack *extack)
{
#define ZL_FLASH_PHASE1_TIMEOUT_MS 60000	/* up to 1 minute */
#define ZL_FLASH_PHASE2_TIMEOUT_MS 120000	/* up to 2 minutes */
	u8 value;
	int rc;

	dev_dbg(zldev->dev, "Sending flash command: 0x%x\n", operation);

	rc = zl3073x_flash_wait_ready(zldev, ZL_FLASH_PHASE1_TIMEOUT_MS);
	if (rc)
		return rc;

	/* Issue the requested operation */
	rc = zl3073x_read_u8(zldev, ZL_REG_WRITE_FLASH, &value);
	if (rc)
		return rc;

	value &= ~ZL_WRITE_FLASH_OP;
	value |= FIELD_PREP(ZL_WRITE_FLASH_OP, operation);

	rc = zl3073x_write_u8(zldev, ZL_REG_WRITE_FLASH, value);
	if (rc)
		return rc;

	/* Wait for command completion */
	rc = zl3073x_flash_wait_ready(zldev, ZL_FLASH_PHASE2_TIMEOUT_MS);
	if (rc)
		return rc;

	return zl3073x_flash_error_check(zldev, extack);
}

/**
 * zl3073x_flash_get_sector_size - Get flash sector size
 * @zldev: zl3073x device structure
 * @sector_size: sector size returned by the function
 *
 * The function reads the flash sector size detected by flash utility and
 * stores it into @sector_size.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_flash_get_sector_size(struct zl3073x_dev *zldev, size_t *sector_size)
{
	u8 flash_info;
	int rc;

	rc = zl3073x_read_u8(zldev, ZL_REG_FLASH_INFO, &flash_info);
	if (rc)
		return rc;

	switch (FIELD_GET(ZL_FLASH_INFO_SECTOR_SIZE, flash_info)) {
	case ZL_FLASH_INFO_SECTOR_4K:
		*sector_size = SZ_4K;
		break;
	case ZL_FLASH_INFO_SECTOR_64K:
		*sector_size = SZ_64K;
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

/**
 * zl3073x_flash_block - Download and flash memory block
 * @zldev: zl3073x device structure
 * @component: component name
 * @operation: flash operation to perform
 * @page: destination flash page
 * @addr: device memory address to load data
 * @data: pointer to data to be flashed
 * @size: size of data
 * @extack: netlink extack pointer to report errors
 *
 * The function downloads the memory block given by the @data pointer and
 * the size @size and flashes it into internal memory on flash page @page.
 * The internal flash operation performed by the firmware is specified by
 * the @operation parameter.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_flash_block(struct zl3073x_dev *zldev, const char *component,
		    u32 operation, u32 page, u32 addr, const void *data,
		    size_t size, struct netlink_ext_ack *extack)
{
	int rc;

	/* Download block to device memory */
	rc = zl3073x_flash_download(zldev, component, addr, data, size, extack);
	if (rc)
		return rc;

	/* Set address to flash from */
	rc = zl3073x_write_u32(zldev, ZL_REG_IMAGE_START_ADDR, addr);
	if (rc)
		return rc;

	/* Set size of block to flash */
	rc = zl3073x_write_u32(zldev, ZL_REG_IMAGE_SIZE, size);
	if (rc)
		return rc;

	/* Set destination page to flash */
	rc = zl3073x_write_u32(zldev, ZL_REG_FLASH_INDEX_WRITE, page);
	if (rc)
		return rc;

	/* Set filling pattern */
	rc = zl3073x_write_u32(zldev, ZL_REG_FILL_PATTERN, U32_MAX);
	if (rc)
		return rc;

	zl3073x_devlink_flash_notify(zldev, "Flashing image", component, 0,
				     size);

	dev_dbg(zldev->dev, "Flashing %zu bytes to page %u\n", size, page);

	/* Execute sectors flash operation */
	rc = zl3073x_flash_cmd_wait(zldev, operation, extack);
	if (rc)
		return rc;

	zl3073x_devlink_flash_notify(zldev, "Flashing image", component, size,
				     size);

	return 0;
}

/**
 * zl3073x_flash_sectors - Flash sectors
 * @zldev: zl3073x device structure
 * @component: component name
 * @page: destination flash page
 * @addr: device memory address to load data
 * @data: pointer to data to be flashed
 * @size: size of data
 * @extack: netlink extack pointer to report errors
 *
 * The function flashes given @data with size of @size to the internal flash
 * memory block starting from page @page. The function uses sector flash
 * method and has to take into account the flash sector size reported by
 * flashing utility. Input data are spliced into blocks according this
 * sector size and each block is flashed separately.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_flash_sectors(struct zl3073x_dev *zldev, const char *component,
			  u32 page, u32 addr, const void *data, size_t size,
			  struct netlink_ext_ack *extack)
{
#define ZL_FLASH_MAX_BLOCK_SIZE	0x0001E000
#define ZL_FLASH_PAGE_SIZE	256
	size_t max_block_size, block_size, sector_size;
	const void *ptr, *end;
	int rc;

	/* Get flash sector size */
	rc = zl3073x_flash_get_sector_size(zldev, &sector_size);
	if (rc) {
		ZL_FLASH_ERR_MSG(extack, "Failed to get flash sector size");
		return rc;
	}

	/* Determine max block size depending on sector size */
	max_block_size = ALIGN_DOWN(ZL_FLASH_MAX_BLOCK_SIZE, sector_size);

	for (ptr = data, end = data + size; ptr < end; ptr += block_size) {
		char comp_str[32];

		block_size = min_t(size_t, max_block_size, end - ptr);

		/* Add suffix '-partN' if the requested component size is
		 * greater than max_block_size.
		 */
		if (max_block_size < size)
			snprintf(comp_str, sizeof(comp_str), "%s-part%zu",
				 component, (ptr - data) / max_block_size + 1);
		else
			strscpy(comp_str, component);

		/* Flash the memory block */
		rc = zl3073x_flash_block(zldev, comp_str,
					 ZL_WRITE_FLASH_OP_SECTORS, page, addr,
					 ptr, block_size, extack);
		if (rc)
			goto finish;

		/* Move to next page */
		page += block_size / ZL_FLASH_PAGE_SIZE;
	}

finish:
	zl3073x_devlink_flash_notify(zldev,
				     rc ?  "Flashing failed" : "Flashing done",
				     component, 0, 0);

	return rc;
}

/**
 * zl3073x_flash_page - Flash page
 * @zldev: zl3073x device structure
 * @component: component name
 * @page: destination flash page
 * @addr: device memory address to load data
 * @data: pointer to data to be flashed
 * @size: size of data
 * @extack: netlink extack pointer to report errors
 *
 * The function flashes given @data with size of @size to the internal flash
 * memory block starting with page @page.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_flash_page(struct zl3073x_dev *zldev, const char *component,
		       u32 page, u32 addr, const void *data, size_t size,
		       struct netlink_ext_ack *extack)
{
	int rc;

	/* Flash the memory block */
	rc = zl3073x_flash_block(zldev, component, ZL_WRITE_FLASH_OP_PAGE, page,
				 addr, data, size, extack);

	zl3073x_devlink_flash_notify(zldev,
				     rc ?  "Flashing failed" : "Flashing done",
				     component, 0, 0);

	return rc;
}

/**
 * zl3073x_flash_page_copy - Copy flash page
 * @zldev: zl3073x device structure
 * @component: component name
 * @src_page: source page to copy
 * @dst_page: destination page
 * @extack: netlink extack pointer to report errors
 *
 * The function copies one flash page specified by @src_page into the flash
 * page specified by @dst_page.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_flash_page_copy(struct zl3073x_dev *zldev, const char *component,
			    u32 src_page, u32 dst_page,
			    struct netlink_ext_ack *extack)
{
	int rc;

	/* Set source page to be copied */
	rc = zl3073x_write_u32(zldev, ZL_REG_FLASH_INDEX_READ, src_page);
	if (rc)
		return rc;

	/* Set destination page for the copy */
	rc = zl3073x_write_u32(zldev, ZL_REG_FLASH_INDEX_WRITE, dst_page);
	if (rc)
		return rc;

	/* Perform copy operation */
	rc = zl3073x_flash_cmd_wait(zldev, ZL_WRITE_FLASH_OP_COPY_PAGE, extack);
	if (rc)
		ZL_FLASH_ERR_MSG(extack, "Failed to copy page %u to page %u",
				 src_page, dst_page);

	return rc;
}

/**
 * zl3073x_flash_mode_verify - Check flash utility
 * @zldev: zl3073x device structure
 *
 * Return: 0 if the flash utility is ready, <0 on error
 */
static int
zl3073x_flash_mode_verify(struct zl3073x_dev *zldev)
{
	u8 family, release;
	u32 hash;
	int rc;

	rc = zl3073x_read_u32(zldev, ZL_REG_FLASH_HASH, &hash);
	if (rc)
		return rc;

	rc = zl3073x_read_u8(zldev, ZL_REG_FLASH_FAMILY, &family);
	if (rc)
		return rc;

	rc = zl3073x_read_u8(zldev, ZL_REG_FLASH_RELEASE, &release);
	if (rc)
		return rc;

	dev_dbg(zldev->dev,
		"Flash utility check: hash 0x%08x, fam 0x%02x, rel 0x%02x\n",
		hash, family, release);

	/* Return success for correct family */
	return (family == 0x21) ? 0 : -ENODEV;
}

static int
zl3073x_flash_host_ctrl_enable(struct zl3073x_dev *zldev)
{
	u8 host_ctrl;
	int rc;

	/* Enable host control */
	rc = zl3073x_read_u8(zldev, ZL_REG_HOST_CONTROL, &host_ctrl);
	if (rc)
		return rc;

	host_ctrl |= ZL_HOST_CONTROL_ENABLE;

	return zl3073x_write_u8(zldev, ZL_REG_HOST_CONTROL, host_ctrl);
}

/**
 * zl3073x_flash_mode_enter - Switch the device to flash mode
 * @zldev: zl3073x device structure
 * @util_ptr: buffer with flash utility
 * @util_size: size of buffer with flash utility
 * @extack: netlink extack pointer to report errors
 *
 * The function prepares and switches the device into flash mode.
 *
 * The procedure:
 * 1) Stop device CPU by specific HW register sequence
 * 2) Download flash utility to device memory
 * 3) Resume device CPU by specific HW register sequence
 * 4) Check communication with flash utility
 * 5) Enable host control necessary to access flash API
 * 6) Check for potential error detected by the utility
 *
 * The API provided by normal firmware is not available in flash mode
 * so the caller has to ensure that this API is not used in this mode.
 *
 * After performing flash operation the caller should call
 * @zl3073x_flash_mode_leave to return back to normal operation.
 *
 * Return: 0 on success, <0 on error.
 */
int zl3073x_flash_mode_enter(struct zl3073x_dev *zldev, const void *util_ptr,
			     size_t util_size, struct netlink_ext_ack *extack)
{
	/* Sequence to be written prior utility download */
	static const struct zl3073x_hwreg_seq_item pre_seq[] = {
		HWREG_SEQ_ITEM(0x80000400, 1, BIT(0), 0),
		HWREG_SEQ_ITEM(0x80206340, 1, BIT(4), 0),
		HWREG_SEQ_ITEM(0x10000000, 1, BIT(2), 0),
		HWREG_SEQ_ITEM(0x10000024, 0x00000001, U32_MAX, 0),
		HWREG_SEQ_ITEM(0x10000020, 0x00000001, U32_MAX, 0),
		HWREG_SEQ_ITEM(0x10000000, 1, BIT(10), 1000),
	};
	/* Sequence to be written after utility download */
	static const struct zl3073x_hwreg_seq_item post_seq[] = {
		HWREG_SEQ_ITEM(0x10400004, 0x000000C0, U32_MAX, 0),
		HWREG_SEQ_ITEM(0x10400008, 0x00000000, U32_MAX, 0),
		HWREG_SEQ_ITEM(0x10400010, 0x20000000, U32_MAX, 0),
		HWREG_SEQ_ITEM(0x10400014, 0x20000004, U32_MAX, 0),
		HWREG_SEQ_ITEM(0x10000000, 1, GENMASK(10, 9), 0),
		HWREG_SEQ_ITEM(0x10000020, 0x00000000, U32_MAX, 0),
		HWREG_SEQ_ITEM(0x10000000, 0, BIT(0), 1000),
	};
	int rc;

	zl3073x_devlink_flash_notify(zldev, "Prepare flash mode", "utility",
				     0, 0);

	/* Execure pre-load sequence */
	rc = zl3073x_write_hwreg_seq(zldev, pre_seq, ARRAY_SIZE(pre_seq));
	if (rc) {
		ZL_FLASH_ERR_MSG(extack, "cannot execute pre-load sequence");
		goto error;
	}

	/* Download utility image to device memory */
	rc = zl3073x_flash_download(zldev, "utility", 0x20000000, util_ptr,
				    util_size, extack);
	if (rc) {
		ZL_FLASH_ERR_MSG(extack, "cannot download flash utility");
		goto error;
	}

	/* Execute post-load sequence */
	rc = zl3073x_write_hwreg_seq(zldev, post_seq, ARRAY_SIZE(post_seq));
	if (rc) {
		ZL_FLASH_ERR_MSG(extack, "cannot execute post-load sequence");
		goto error;
	}

	/* Check that utility identifies itself correctly */
	rc = zl3073x_flash_mode_verify(zldev);
	if (rc) {
		ZL_FLASH_ERR_MSG(extack, "flash utility check failed");
		goto error;
	}

	/* Enable host control */
	rc = zl3073x_flash_host_ctrl_enable(zldev);
	if (rc) {
		ZL_FLASH_ERR_MSG(extack, "cannot enable host control");
		goto error;
	}

	zl3073x_devlink_flash_notify(zldev, "Flash mode enabled", "utility",
				     0, 0);

	return 0;

error:
	zl3073x_flash_mode_leave(zldev, extack);

	return rc;
}

/**
 * zl3073x_flash_mode_leave - Leave flash mode
 * @zldev: zl3073x device structure
 * @extack: netlink extack pointer to report errors
 *
 * The function instructs the device to leave the flash mode and
 * to return back to normal operation.
 *
 * The procedure:
 * 1) Set reset flag
 * 2) Reset the device CPU by specific HW register sequence
 * 3) Wait for the device to be ready
 * 4) Check the reset flag was cleared
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_flash_mode_leave(struct zl3073x_dev *zldev,
			     struct netlink_ext_ack *extack)
{
	/* Sequence to be written after flash */
	static const struct zl3073x_hwreg_seq_item fw_reset_seq[] = {
		HWREG_SEQ_ITEM(0x80000404, 1, BIT(0), 0),
		HWREG_SEQ_ITEM(0x80000410, 1, BIT(0), 0),
	};
	u8 reset_status;
	int rc;

	zl3073x_devlink_flash_notify(zldev, "Leaving flash mode", "utility",
				     0, 0);

	/* Read reset status register */
	rc = zl3073x_read_u8(zldev, ZL_REG_RESET_STATUS, &reset_status);
	if (rc)
		return rc;

	/* Set reset bit */
	reset_status |= ZL_REG_RESET_STATUS_RESET;

	/* Update reset status register */
	rc = zl3073x_write_u8(zldev, ZL_REG_RESET_STATUS, reset_status);
	if (rc)
		return rc;

	/* We do not check the return value here as the sequence resets
	 * the device CPU and the last write always return an error.
	 */
	zl3073x_write_hwreg_seq(zldev, fw_reset_seq, ARRAY_SIZE(fw_reset_seq));

	/* Wait for the device to be ready */
	msleep(500);

	/* Read again the reset status register */
	rc = zl3073x_read_u8(zldev, ZL_REG_RESET_STATUS, &reset_status);
	if (rc)
		return rc;

	/* Check the reset bit was cleared */
	if (reset_status & ZL_REG_RESET_STATUS_RESET) {
		dev_err(zldev->dev,
			"Reset not confirmed after switch to normal mode\n");
		return -EINVAL;
	}

	return 0;
}
