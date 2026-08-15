// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/minmax.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "regs/xe_sysctrl_regs.h"
#include "xe_device.h"
#include "xe_mmio.h"
#include "xe_pm.h"
#include "xe_printk.h"
#include "xe_sysctrl.h"
#include "xe_sysctrl_mailbox.h"
#include "xe_sysctrl_mailbox_types.h"

struct xe_sysctrl_mailbox_msg_hdr {
	__le32 data;
} __packed;

#define XE_SYSCTRL_HDR_GROUP_ID(hdr) \
	FIELD_GET(SYSCTRL_HDR_GROUP_ID_MASK, le32_to_cpu((hdr)->data))

#define XE_SYSCTRL_HDR_COMMAND(hdr) \
	FIELD_GET(SYSCTRL_HDR_COMMAND_MASK, le32_to_cpu((hdr)->data))

#define XE_SYSCTRL_HDR_IS_RESPONSE(hdr) \
	FIELD_GET(SYSCTRL_HDR_IS_RESPONSE, le32_to_cpu((hdr)->data))

#define XE_SYSCTRL_HDR_RESULT(hdr) \
	FIELD_GET(SYSCTRL_HDR_RESULT_MASK, le32_to_cpu((hdr)->data))

static bool sysctrl_wait_bit_clear(struct xe_sysctrl *sc, u32 bit_mask,
				   unsigned int timeout_ms)
{
	int ret;

	ret = xe_mmio_wait32_not(sc->mmio, SYSCTRL_MB_CTRL, bit_mask, bit_mask,
				 timeout_ms * 1000, NULL, false);

	return ret == 0;
}

static bool sysctrl_wait_bit_set(struct xe_sysctrl *sc, u32 bit_mask,
				 unsigned int timeout_ms)
{
	int ret;

	ret = xe_mmio_wait32(sc->mmio, SYSCTRL_MB_CTRL, bit_mask, bit_mask,
			     timeout_ms * 1000, NULL, false);

	return ret == 0;
}

static int sysctrl_write_frame(struct xe_sysctrl *sc, const void *frame,
			       size_t len)
{
	static const struct xe_reg regs[] = {
		SYSCTRL_MB_DATA0, SYSCTRL_MB_DATA1, SYSCTRL_MB_DATA2, SYSCTRL_MB_DATA3
	};
	struct xe_device *xe = sc_to_xe(sc);
	u32 val[XE_SYSCTRL_MB_FRAME_SIZE / sizeof(u32)] = {0};
	u32 dw = DIV_ROUND_UP(len, sizeof(u32));
	u32 i;

	xe_assert(xe, len > 0 && len <= XE_SYSCTRL_MB_FRAME_SIZE);

	memcpy(val, frame, len);

	for (i = 0; i < dw; i++)
		xe_mmio_write32(sc->mmio, regs[i], val[i]);

	return 0;
}

static int sysctrl_read_frame(struct xe_sysctrl *sc, void *frame,
			      size_t len)
{
	static const struct xe_reg regs[] = {
		SYSCTRL_MB_DATA0, SYSCTRL_MB_DATA1, SYSCTRL_MB_DATA2, SYSCTRL_MB_DATA3
	};
	struct xe_device *xe = sc_to_xe(sc);
	u32 val[XE_SYSCTRL_MB_FRAME_SIZE / sizeof(u32)] = {0};
	u32 dw = DIV_ROUND_UP(len, sizeof(u32));
	u32 i;

	xe_assert(xe, len > 0 && len <= XE_SYSCTRL_MB_FRAME_SIZE);

	for (i = 0; i < dw; i++)
		val[i] = xe_mmio_read32(sc->mmio, regs[i]);

	memcpy(frame, val, len);

	return 0;
}

static void sysctrl_clear_response(struct xe_sysctrl *sc)
{
	xe_mmio_rmw32(sc->mmio, SYSCTRL_MB_CTRL, SYSCTRL_MB_CTRL_RUN_BUSY_OUT, 0);
}

static int sysctrl_prepare_command(struct xe_device *xe,
				   u8 group_id, u8 command,
				   const void *data_in, size_t data_in_len,
				   u8 **mbox_cmd, size_t *cmd_size)
{
	struct xe_sysctrl_mailbox_msg_hdr *hdr;
	size_t size;
	u8 *buffer;

	xe_assert(xe, command <= SYSCTRL_HDR_COMMAND_MAX);

	if (data_in_len > XE_SYSCTRL_MB_MAX_MESSAGE_SIZE - sizeof(*hdr)) {
		xe_err(xe, "sysctrl: Input data too large: %zu bytes\n", data_in_len);
		return -EINVAL;
	}

	size = sizeof(*hdr) + data_in_len;

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	hdr = (struct xe_sysctrl_mailbox_msg_hdr *)buffer;
	hdr->data = cpu_to_le32(FIELD_PREP(SYSCTRL_HDR_GROUP_ID_MASK, group_id) |
				     FIELD_PREP(SYSCTRL_HDR_COMMAND_MASK, command));

	if (data_in && data_in_len)
		memcpy(buffer + sizeof(*hdr), data_in, data_in_len);

	*mbox_cmd = buffer;
	*cmd_size = size;

	return 0;
}

static int sysctrl_send_frames(struct xe_sysctrl *sc,
			       const u8 *mbox_cmd,
			       size_t cmd_size, unsigned int timeout_ms)
{
	struct xe_device *xe = sc_to_xe(sc);
	u32 ctrl_reg, total_frames, frame;
	size_t bytes_sent, frame_size;

	total_frames = DIV_ROUND_UP(cmd_size, XE_SYSCTRL_MB_FRAME_SIZE);

	if (!sysctrl_wait_bit_clear(sc, SYSCTRL_MB_CTRL_RUN_BUSY, timeout_ms)) {
		xe_err(xe, "sysctrl: Mailbox busy\n");
		return -EBUSY;
	}

	sc->phase_bit ^= 1;
	bytes_sent = 0;

	for (frame = 0; frame < total_frames; frame++) {
		frame_size = min_t(size_t, cmd_size - bytes_sent, XE_SYSCTRL_MB_FRAME_SIZE);

		if (sysctrl_write_frame(sc, mbox_cmd + bytes_sent, frame_size)) {
			xe_err(xe, "sysctrl: Failed to write frame %u\n", frame);
			sc->phase_bit = 0;
			return -EIO;
		}

		ctrl_reg = SYSCTRL_MB_CTRL_RUN_BUSY |
			   REG_FIELD_PREP(SYSCTRL_FRAME_CURRENT_MASK, frame) |
			   REG_FIELD_PREP(SYSCTRL_FRAME_TOTAL_MASK, total_frames - 1) |
			   SYSCTRL_MB_CTRL_CMD |
			   (sc->phase_bit ? SYSCTRL_FRAME_PHASE : 0);

		xe_mmio_write32(sc->mmio, SYSCTRL_MB_CTRL, ctrl_reg);

		if (!sysctrl_wait_bit_clear(sc, SYSCTRL_MB_CTRL_RUN_BUSY, timeout_ms)) {
			xe_err(xe, "sysctrl: Frame %u acknowledgment timeout\n", frame);
			sc->phase_bit = 0;
			return -ETIMEDOUT;
		}

		bytes_sent += frame_size;
	}

	return 0;
}

static int sysctrl_process_frame(struct xe_sysctrl *sc, void *out,
				 size_t frame_size, unsigned int timeout_ms,
				 bool *done)
{
	u32 curr_frame, total_frames, ctrl_reg;
	struct xe_device *xe = sc_to_xe(sc);
	int ret;

	if (!sysctrl_wait_bit_set(sc, SYSCTRL_MB_CTRL_RUN_BUSY_OUT, timeout_ms)) {
		xe_err(xe, "sysctrl: Response frame timeout\n");
		return -ETIMEDOUT;
	}

	ctrl_reg = xe_mmio_read32(sc->mmio, SYSCTRL_MB_CTRL);
	total_frames = FIELD_GET(SYSCTRL_FRAME_TOTAL_MASK, ctrl_reg);
	curr_frame = FIELD_GET(SYSCTRL_FRAME_CURRENT_MASK, ctrl_reg);

	ret = sysctrl_read_frame(sc, out, frame_size);
	if (ret)
		return ret;

	sysctrl_clear_response(sc);

	if (curr_frame == total_frames)
		*done = true;

	return 0;
}

static int sysctrl_receive_frames(struct xe_sysctrl *sc,
				  const struct xe_sysctrl_mailbox_msg_hdr *req,
				  void *data_out, size_t data_out_len,
				  size_t *rdata_len, unsigned int timeout_ms)
{
	struct xe_sysctrl_mailbox_msg_hdr *hdr;
	struct xe_device *xe = sc_to_xe(sc);
	size_t remain = sizeof(*hdr) + data_out_len;
	u8 *buffer __free(kfree) = kzalloc(remain, GFP_KERNEL);
	size_t frame_size;
	bool done = false;
	int ret = 0;
	u8 *out;

	if (!buffer)
		return -ENOMEM;

	out = buffer;
	while (!done && remain) {
		frame_size = min_t(size_t, remain, XE_SYSCTRL_MB_FRAME_SIZE);

		ret = sysctrl_process_frame(sc, out, frame_size, timeout_ms,
					    &done);
		if (ret)
			return ret;

		remain -= frame_size;
		out += frame_size;
	}

	hdr = (struct xe_sysctrl_mailbox_msg_hdr *)buffer;

	if (!XE_SYSCTRL_HDR_IS_RESPONSE(hdr) ||
	    XE_SYSCTRL_HDR_GROUP_ID(hdr) != XE_SYSCTRL_HDR_GROUP_ID(req) ||
	    XE_SYSCTRL_HDR_COMMAND(hdr) != XE_SYSCTRL_HDR_COMMAND(req)) {
		xe_err(xe, "sysctrl: Response header mismatch\n");
		return -EPROTO;
	}

	if (XE_SYSCTRL_HDR_RESULT(hdr) != 0) {
		xe_err(xe, "sysctrl: Firmware error: 0x%02lx\n",
		       XE_SYSCTRL_HDR_RESULT(hdr));
		return -EIO;
	}

	memcpy(data_out, hdr + 1, data_out_len);
	*rdata_len = out - buffer - sizeof(*hdr);

	return 0;
}

static int sysctrl_send_command(struct xe_sysctrl *sc,
				const u8 *mbox_cmd, size_t cmd_size,
				void *data_out, size_t data_out_len,
				size_t *rdata_len, unsigned int timeout_ms)
{
	const struct xe_sysctrl_mailbox_msg_hdr *hdr;
	size_t received;
	int ret;

	ret = sysctrl_send_frames(sc, mbox_cmd, cmd_size, timeout_ms);
	if (ret)
		return ret;

	if (!data_out || !rdata_len)
		return 0;

	hdr = (const struct xe_sysctrl_mailbox_msg_hdr *)mbox_cmd;

	ret = sysctrl_receive_frames(sc, hdr, data_out, data_out_len,
				     &received, timeout_ms);
	if (ret)
		return ret;

	*rdata_len = received;

	return 0;
}

/**
 * xe_sysctrl_mailbox_init - Initialize System Controller mailbox interface
 * @sc: System controller structure
 *
 * Initialize system controller mailbox interface for communication.
 */
void xe_sysctrl_mailbox_init(struct xe_sysctrl *sc)
{
	u32 ctrl_reg;

	ctrl_reg = xe_mmio_read32(sc->mmio, SYSCTRL_MB_CTRL);
	sc->phase_bit = (ctrl_reg & SYSCTRL_FRAME_PHASE) ? 1 : 0;
}

/**
 * xe_sysctrl_send_command() - Send mailbox command to System Controller
 * @sc: System Controller instance
 * @cmd: Command descriptor containing request header and payload buffers
 * @rdata_len: Pointer to store actual response data length
 *
 * Sends a mailbox command to System Controller firmware using
 * System Controller mailbox and waits for a response.
 *
 * Request payload is provided via @cmd->data_in and @cmd->data_in_len.
 * If a response is expected, @cmd->data_out must point to a buffer of
 * size @cmd->data_out_len supplied by caller.
 *
 * On success, @rdata_len is updated with number of valid response bytes
 * returned by firmware, bounded by @cmd->data_out_len.
 *
 * Return: 0 on success, or negative errno on failure.
 */
int xe_sysctrl_send_command(struct xe_sysctrl *sc,
			    struct xe_sysctrl_mailbox_command *cmd,
			    size_t *rdata_len)
{
	struct xe_device *xe = sc_to_xe(sc);
	u8 group_id, command_code;
	u8 *mbox_cmd = NULL;
	size_t cmd_size = 0;
	int ret;

	guard(xe_pm_runtime_noresume)(xe);

	if (!xe->info.has_sysctrl)
		return -ENODEV;

	xe_assert(xe, cmd->data_in || cmd->data_out);
	xe_assert(xe, !cmd->data_in || cmd->data_in_len);
	xe_assert(xe, !cmd->data_out || cmd->data_out_len);

	group_id = XE_SYSCTRL_APP_HDR_GROUP_ID(&cmd->header);
	command_code = XE_SYSCTRL_APP_HDR_COMMAND(&cmd->header);

	might_sleep();

	ret = sysctrl_prepare_command(xe, group_id, command_code,
				      cmd->data_in, cmd->data_in_len,
				      &mbox_cmd, &cmd_size);
	if (ret) {
		xe_err(xe, "sysctrl: Failed to prepare command: %pe\n", ERR_PTR(ret));
		return ret;
	}

	guard(mutex)(&sc->cmd_lock);

	ret = sysctrl_send_command(sc, mbox_cmd, cmd_size,
				   cmd->data_out, cmd->data_out_len, rdata_len,
				   XE_SYSCTRL_MB_DEFAULT_TIMEOUT_MS);
	if (ret)
		xe_err(xe, "sysctrl: Mailbox command failed: %pe\n", ERR_PTR(ret));

	kfree(mbox_cmd);

	return ret;
}
