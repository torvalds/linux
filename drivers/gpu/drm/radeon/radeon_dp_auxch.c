/*
 * Copyright 2015 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 */
#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon.h"
#include "nid.h"

#define AUX_RX_ERROR_FLAGS (AUX_SW_RX_OVERFLOW |	     \
			    AUX_SW_RX_HPD_DISCON |	     \
			    AUX_SW_RX_PARTIAL_BYTE |	     \
			    AUX_SW_NON_AUX_MODE |	     \
			    AUX_SW_RX_SYNC_INVALID_L |	     \
			    AUX_SW_RX_SYNC_INVALID_H |	     \
			    AUX_SW_RX_INVALID_START |	     \
			    AUX_SW_RX_RECV_NO_DET |	     \
			    AUX_SW_RX_RECV_INVALID_H |	     \
			    AUX_SW_RX_RECV_INVALID_V)

#define AUX_SW_REPLY_GET_BYTE_COUNT(x) (((x) >> 24) & 0x1f)

#define BARE_ADDRESS_SIZE 3

static const u32 aux_offset[] =
{
	0x6200 - 0x6200,
	0x6250 - 0x6200,
	0x62a0 - 0x6200,
	0x6300 - 0x6200,
	0x6350 - 0x6200,
	0x63a0 - 0x6200,
};

ssize_t
radeon_dp_aux_transfer_native(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	struct radeon_i2c_chan *chan =
		container_of(aux, struct radeon_i2c_chan, aux);
	struct drm_device *dev = chan->dev;
	struct radeon_device *rdev = dev->dev_private;
	int ret = 0, i;
	uint32_t tmp, ack = 0;
	int instance = chan->rec.i2c_id & 0xf;
	u8 byte;
	u8 *buf = msg->buffer;
	int retry_count = 0;
	int bytes;
	int msize;
	bool is_write = false;

	if (WARN_ON(msg->size > 16))
		return -E2BIG;

	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
		is_write = true;
		break;
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		break;
	default:
		return -EINVAL;
	}

	/* work out two sizes required */
	msize = 0;
	bytes = BARE_ADDRESS_SIZE;
	if (msg->size) {
		msize = msg->size - 1;
		bytes++;
		if (is_write)
			bytes += msg->size;
	}

	mutex_lock(&chan->mutex);

	/* switch the pad to aux mode */
	tmp = RREG32(chan->rec.mask_clk_reg);
	tmp |= (1 << 16);
	WREG32(chan->rec.mask_clk_reg, tmp);

	/* setup AUX control register with correct HPD pin */
	tmp = RREG32(AUX_CONTROL + aux_offset[instance]);

	tmp &= AUX_HPD_SEL(0x7);
	tmp |= AUX_HPD_SEL(chan->rec.hpd);
	tmp |= AUX_EN | AUX_LS_READ_EN | AUX_HPD_DISCON(0x1);

	WREG32(AUX_CONTROL + aux_offset[instance], tmp);

	/* atombios appears to write this twice lets copy it */
	WREG32(AUX_SW_CONTROL + aux_offset[instance],
	       AUX_SW_WR_BYTES(bytes));
	WREG32(AUX_SW_CONTROL + aux_offset[instance],
	       AUX_SW_WR_BYTES(bytes));

	/* write the data header into the registers */
	/* request, address, msg size */
	byte = (msg->request << 4) | ((msg->address >> 16) & 0xf);
	WREG32(AUX_SW_DATA + aux_offset[instance],
	       AUX_SW_DATA_MASK(byte) | AUX_SW_AUTOINCREMENT_DISABLE);

	byte = (msg->address >> 8) & 0xff;
	WREG32(AUX_SW_DATA + aux_offset[instance],
	       AUX_SW_DATA_MASK(byte));

	byte = msg->address & 0xff;
	WREG32(AUX_SW_DATA + aux_offset[instance],
	       AUX_SW_DATA_MASK(byte));

	byte = msize;
	WREG32(AUX_SW_DATA + aux_offset[instance],
	       AUX_SW_DATA_MASK(byte));

	/* if we are writing - write the msg buffer */
	if (is_write) {
		for (i = 0; i < msg->size; i++) {
			WREG32(AUX_SW_DATA + aux_offset[instance],
			       AUX_SW_DATA_MASK(buf[i]));
		}
	}

	/* clear the ACK */
	WREG32(AUX_SW_INTERRUPT_CONTROL + aux_offset[instance], AUX_SW_DONE_ACK);

	/* write the size and GO bits */
	WREG32(AUX_SW_CONTROL + aux_offset[instance],
	       AUX_SW_WR_BYTES(bytes) | AUX_SW_GO);

	/* poll the status registers - TODO irq support */
	do {
		tmp = RREG32(AUX_SW_STATUS + aux_offset[instance]);
		if (tmp & AUX_SW_DONE) {
			break;
		}
		usleep_range(100, 200);
	} while (retry_count++ < 1000);

	if (retry_count >= 1000) {
		DRM_ERROR("auxch hw never signalled completion, error %08x\n", tmp);
		ret = -EIO;
		goto done;
	}

	if (tmp & AUX_SW_RX_TIMEOUT) {
		DRM_DEBUG_KMS("dp_aux_ch timed out\n");
		ret = -ETIMEDOUT;
		goto done;
	}
	if (tmp & AUX_RX_ERROR_FLAGS) {
		DRM_DEBUG_KMS("dp_aux_ch flags not zero: %08x\n", tmp);
		ret = -EIO;
		goto done;
	}

	bytes = AUX_SW_REPLY_GET_BYTE_COUNT(tmp);
	if (bytes) {
		WREG32(AUX_SW_DATA + aux_offset[instance],
		       AUX_SW_DATA_RW | AUX_SW_AUTOINCREMENT_DISABLE);

		tmp = RREG32(AUX_SW_DATA + aux_offset[instance]);
		ack = (tmp >> 8) & 0xff;

		for (i = 0; i < bytes - 1; i++) {
			tmp = RREG32(AUX_SW_DATA + aux_offset[instance]);
			if (buf)
				buf[i] = (tmp >> 8) & 0xff;
		}
		if (buf)
			ret = bytes - 1;
	}

	WREG32(AUX_SW_INTERRUPT_CONTROL + aux_offset[instance], AUX_SW_DONE_ACK);

	if (is_write)
		ret = msg->size;
done:
	mutex_unlock(&chan->mutex);

	if (ret >= 0)
		msg->reply = ack >> 4;
	return ret;
}
