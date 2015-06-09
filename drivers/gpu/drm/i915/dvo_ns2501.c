/*
 *
 * Copyright (c) 2012 Gilles Dartiguelongue, Thomas Richter
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "dvo.h"
#include "i915_reg.h"
#include "i915_drv.h"

#define NS2501_VID 0x1305
#define NS2501_DID 0x6726

#define NS2501_VID_LO 0x00
#define NS2501_VID_HI 0x01
#define NS2501_DID_LO 0x02
#define NS2501_DID_HI 0x03
#define NS2501_REV 0x04
#define NS2501_RSVD 0x05
#define NS2501_FREQ_LO 0x06
#define NS2501_FREQ_HI 0x07

#define NS2501_REG8 0x08
#define NS2501_8_VEN (1<<5)
#define NS2501_8_HEN (1<<4)
#define NS2501_8_DSEL (1<<3)
#define NS2501_8_BPAS (1<<2)
#define NS2501_8_RSVD (1<<1)
#define NS2501_8_PD (1<<0)

#define NS2501_REG9 0x09
#define NS2501_9_VLOW (1<<7)
#define NS2501_9_MSEL_MASK (0x7<<4)
#define NS2501_9_TSEL (1<<3)
#define NS2501_9_RSEN (1<<2)
#define NS2501_9_RSVD (1<<1)
#define NS2501_9_MDI (1<<0)

#define NS2501_REGC 0x0c

enum {
	MODE_640x480,
	MODE_800x600,
	MODE_1024x768,
};

struct ns2501_reg {
	 uint8_t offset;
	 uint8_t value;
};

/*
 * Magic values based on what the BIOS on
 * Fujitsu-Siemens Lifebook S6010 programs (1024x768 panel).
 */
static const struct ns2501_reg regs_1024x768[][86] = {
	[MODE_640x480] = {
		[0] = { .offset = 0x0a, .value = 0x81, },
		[1] = { .offset = 0x18, .value = 0x07, },
		[2] = { .offset = 0x19, .value = 0x00, },
		[3] = { .offset = 0x1a, .value = 0x00, },
		[4] = { .offset = 0x1b, .value = 0x11, },
		[5] = { .offset = 0x1c, .value = 0x54, },
		[6] = { .offset = 0x1d, .value = 0x03, },
		[7] = { .offset = 0x1e, .value = 0x02, },
		[8] = { .offset = 0xf3, .value = 0x90, },
		[9] = { .offset = 0xf9, .value = 0x00, },
		[10] = { .offset = 0xc1, .value = 0x90, },
		[11] = { .offset = 0xc2, .value = 0x00, },
		[12] = { .offset = 0xc3, .value = 0x0f, },
		[13] = { .offset = 0xc4, .value = 0x03, },
		[14] = { .offset = 0xc5, .value = 0x16, },
		[15] = { .offset = 0xc6, .value = 0x00, },
		[16] = { .offset = 0xc7, .value = 0x02, },
		[17] = { .offset = 0xc8, .value = 0x02, },
		[18] = { .offset = 0xf4, .value = 0x00, },
		[19] = { .offset = 0x80, .value = 0xff, },
		[20] = { .offset = 0x81, .value = 0x07, },
		[21] = { .offset = 0x82, .value = 0x3d, },
		[22] = { .offset = 0x83, .value = 0x05, },
		[23] = { .offset = 0x94, .value = 0x00, },
		[24] = { .offset = 0x95, .value = 0x00, },
		[25] = { .offset = 0x96, .value = 0x05, },
		[26] = { .offset = 0x97, .value = 0x00, },
		[27] = { .offset = 0x9a, .value = 0x88, },
		[28] = { .offset = 0x9b, .value = 0x00, },
		[29] = { .offset = 0x98, .value = 0x00, },
		[30] = { .offset = 0x99, .value = 0x00, },
		[31] = { .offset = 0xf7, .value = 0x88, },
		[32] = { .offset = 0xf8, .value = 0x0a, },
		[33] = { .offset = 0x9c, .value = 0x24, },
		[34] = { .offset = 0x9d, .value = 0x00, },
		[35] = { .offset = 0x9e, .value = 0x25, },
		[36] = { .offset = 0x9f, .value = 0x03, },
		[37] = { .offset = 0xa0, .value = 0x28, },
		[38] = { .offset = 0xa1, .value = 0x01, },
		[39] = { .offset = 0xa2, .value = 0x28, },
		[40] = { .offset = 0xa3, .value = 0x05, },
		[41] = { .offset = 0xb6, .value = 0x09, },
		[42] = { .offset = 0xb8, .value = 0x00, },
		[43] = { .offset = 0xb9, .value = 0xa0, },
		[44] = { .offset = 0xba, .value = 0x00, },
		[45] = { .offset = 0xbb, .value = 0x20, },
		[46] = { .offset = 0x10, .value = 0x00, },
		[47] = { .offset = 0x11, .value = 0xa0, },
		[48] = { .offset = 0x12, .value = 0x02, },
		[49] = { .offset = 0x20, .value = 0x00, },
		[50] = { .offset = 0x22, .value = 0x00, },
		[51] = { .offset = 0x23, .value = 0x00, },
		[52] = { .offset = 0x24, .value = 0x00, },
		[53] = { .offset = 0x25, .value = 0x00, },
		[54] = { .offset = 0x8c, .value = 0x10, },
		[55] = { .offset = 0x8d, .value = 0x02, },
		[56] = { .offset = 0x8e, .value = 0x10, },
		[57] = { .offset = 0x8f, .value = 0x00, },
		[58] = { .offset = 0x90, .value = 0xff, },
		[59] = { .offset = 0x91, .value = 0x07, },
		[60] = { .offset = 0x92, .value = 0xa0, },
		[61] = { .offset = 0x93, .value = 0x02, },
		[62] = { .offset = 0xa5, .value = 0x00, },
		[63] = { .offset = 0xa6, .value = 0x00, },
		[64] = { .offset = 0xa7, .value = 0x00, },
		[65] = { .offset = 0xa8, .value = 0x00, },
		[66] = { .offset = 0xa9, .value = 0x04, },
		[67] = { .offset = 0xaa, .value = 0x70, },
		[68] = { .offset = 0xab, .value = 0x4f, },
		[69] = { .offset = 0xac, .value = 0x00, },
		[70] = { .offset = 0xa4, .value = 0x84, },
		[71] = { .offset = 0x7e, .value = 0x18, },
		[72] = { .offset = 0x84, .value = 0x00, },
		[73] = { .offset = 0x85, .value = 0x00, },
		[74] = { .offset = 0x86, .value = 0x00, },
		[75] = { .offset = 0x87, .value = 0x00, },
		[76] = { .offset = 0x88, .value = 0x00, },
		[77] = { .offset = 0x89, .value = 0x00, },
		[78] = { .offset = 0x8a, .value = 0x00, },
		[79] = { .offset = 0x8b, .value = 0x00, },
		[80] = { .offset = 0x26, .value = 0x00, },
		[81] = { .offset = 0x27, .value = 0x00, },
		[82] = { .offset = 0xad, .value = 0x00, },
		[83] = { .offset = 0x08, .value = 0x30, }, /* 0x31 */
		[84] = { .offset = 0x41, .value = 0x00, },
		[85] = { .offset = 0xc0, .value = 0x05, },
	},
	[MODE_800x600] = {
		[0] = { .offset = 0x0a, .value = 0x81, },
		[1] = { .offset = 0x18, .value = 0x07, },
		[2] = { .offset = 0x19, .value = 0x00, },
		[3] = { .offset = 0x1a, .value = 0x00, },
		[4] = { .offset = 0x1b, .value = 0x19, },
		[5] = { .offset = 0x1c, .value = 0x64, },
		[6] = { .offset = 0x1d, .value = 0x02, },
		[7] = { .offset = 0x1e, .value = 0x02, },
		[8] = { .offset = 0xf3, .value = 0x90, },
		[9] = { .offset = 0xf9, .value = 0x00, },
		[10] = { .offset = 0xc1, .value = 0xd7, },
		[11] = { .offset = 0xc2, .value = 0x00, },
		[12] = { .offset = 0xc3, .value = 0xf8, },
		[13] = { .offset = 0xc4, .value = 0x03, },
		[14] = { .offset = 0xc5, .value = 0x1a, },
		[15] = { .offset = 0xc6, .value = 0x00, },
		[16] = { .offset = 0xc7, .value = 0x73, },
		[17] = { .offset = 0xc8, .value = 0x02, },
		[18] = { .offset = 0xf4, .value = 0x00, },
		[19] = { .offset = 0x80, .value = 0x27, },
		[20] = { .offset = 0x81, .value = 0x03, },
		[21] = { .offset = 0x82, .value = 0x41, },
		[22] = { .offset = 0x83, .value = 0x05, },
		[23] = { .offset = 0x94, .value = 0x00, },
		[24] = { .offset = 0x95, .value = 0x00, },
		[25] = { .offset = 0x96, .value = 0x05, },
		[26] = { .offset = 0x97, .value = 0x00, },
		[27] = { .offset = 0x9a, .value = 0x88, },
		[28] = { .offset = 0x9b, .value = 0x00, },
		[29] = { .offset = 0x98, .value = 0x00, },
		[30] = { .offset = 0x99, .value = 0x00, },
		[31] = { .offset = 0xf7, .value = 0x88, },
		[32] = { .offset = 0xf8, .value = 0x06, },
		[33] = { .offset = 0x9c, .value = 0x23, },
		[34] = { .offset = 0x9d, .value = 0x00, },
		[35] = { .offset = 0x9e, .value = 0x25, },
		[36] = { .offset = 0x9f, .value = 0x03, },
		[37] = { .offset = 0xa0, .value = 0x28, },
		[38] = { .offset = 0xa1, .value = 0x01, },
		[39] = { .offset = 0xa2, .value = 0x28, },
		[40] = { .offset = 0xa3, .value = 0x05, },
		[41] = { .offset = 0xb6, .value = 0x09, },
		[42] = { .offset = 0xb8, .value = 0x30, },
		[43] = { .offset = 0xb9, .value = 0xc8, },
		[44] = { .offset = 0xba, .value = 0x00, },
		[45] = { .offset = 0xbb, .value = 0x20, },
		[46] = { .offset = 0x10, .value = 0x20, },
		[47] = { .offset = 0x11, .value = 0xc8, },
		[48] = { .offset = 0x12, .value = 0x02, },
		[49] = { .offset = 0x20, .value = 0x00, },
		[50] = { .offset = 0x22, .value = 0x00, },
		[51] = { .offset = 0x23, .value = 0x00, },
		[52] = { .offset = 0x24, .value = 0x00, },
		[53] = { .offset = 0x25, .value = 0x00, },
		[54] = { .offset = 0x8c, .value = 0x10, },
		[55] = { .offset = 0x8d, .value = 0x02, },
		[56] = { .offset = 0x8e, .value = 0x04, },
		[57] = { .offset = 0x8f, .value = 0x00, },
		[58] = { .offset = 0x90, .value = 0xff, },
		[59] = { .offset = 0x91, .value = 0x07, },
		[60] = { .offset = 0x92, .value = 0xa0, },
		[61] = { .offset = 0x93, .value = 0x02, },
		[62] = { .offset = 0xa5, .value = 0x00, },
		[63] = { .offset = 0xa6, .value = 0x00, },
		[64] = { .offset = 0xa7, .value = 0x00, },
		[65] = { .offset = 0xa8, .value = 0x00, },
		[66] = { .offset = 0xa9, .value = 0x83, },
		[67] = { .offset = 0xaa, .value = 0x40, },
		[68] = { .offset = 0xab, .value = 0x32, },
		[69] = { .offset = 0xac, .value = 0x00, },
		[70] = { .offset = 0xa4, .value = 0x80, },
		[71] = { .offset = 0x7e, .value = 0x18, },
		[72] = { .offset = 0x84, .value = 0x00, },
		[73] = { .offset = 0x85, .value = 0x00, },
		[74] = { .offset = 0x86, .value = 0x00, },
		[75] = { .offset = 0x87, .value = 0x00, },
		[76] = { .offset = 0x88, .value = 0x00, },
		[77] = { .offset = 0x89, .value = 0x00, },
		[78] = { .offset = 0x8a, .value = 0x00, },
		[79] = { .offset = 0x8b, .value = 0x00, },
		[80] = { .offset = 0x26, .value = 0x00, },
		[81] = { .offset = 0x27, .value = 0x00, },
		[82] = { .offset = 0xad, .value = 0x00, },
		[83] = { .offset = 0x08, .value = 0x30, }, /* 0x31 */
		[84] = { .offset = 0x41, .value = 0x00, },
		[85] = { .offset = 0xc0, .value = 0x07, },
	},
	[MODE_1024x768] = {
		[0] = { .offset = 0x0a, .value = 0x81, },
		[1] = { .offset = 0x18, .value = 0x07, },
		[2] = { .offset = 0x19, .value = 0x00, },
		[3] = { .offset = 0x1a, .value = 0x00, },
		[4] = { .offset = 0x1b, .value = 0x11, },
		[5] = { .offset = 0x1c, .value = 0x54, },
		[6] = { .offset = 0x1d, .value = 0x03, },
		[7] = { .offset = 0x1e, .value = 0x02, },
		[8] = { .offset = 0xf3, .value = 0x90, },
		[9] = { .offset = 0xf9, .value = 0x00, },
		[10] = { .offset = 0xc1, .value = 0x90, },
		[11] = { .offset = 0xc2, .value = 0x00, },
		[12] = { .offset = 0xc3, .value = 0x0f, },
		[13] = { .offset = 0xc4, .value = 0x03, },
		[14] = { .offset = 0xc5, .value = 0x16, },
		[15] = { .offset = 0xc6, .value = 0x00, },
		[16] = { .offset = 0xc7, .value = 0x02, },
		[17] = { .offset = 0xc8, .value = 0x02, },
		[18] = { .offset = 0xf4, .value = 0x00, },
		[19] = { .offset = 0x80, .value = 0xff, },
		[20] = { .offset = 0x81, .value = 0x07, },
		[21] = { .offset = 0x82, .value = 0x3d, },
		[22] = { .offset = 0x83, .value = 0x05, },
		[23] = { .offset = 0x94, .value = 0x00, },
		[24] = { .offset = 0x95, .value = 0x00, },
		[25] = { .offset = 0x96, .value = 0x05, },
		[26] = { .offset = 0x97, .value = 0x00, },
		[27] = { .offset = 0x9a, .value = 0x88, },
		[28] = { .offset = 0x9b, .value = 0x00, },
		[29] = { .offset = 0x98, .value = 0x00, },
		[30] = { .offset = 0x99, .value = 0x00, },
		[31] = { .offset = 0xf7, .value = 0x88, },
		[32] = { .offset = 0xf8, .value = 0x0a, },
		[33] = { .offset = 0x9c, .value = 0x24, },
		[34] = { .offset = 0x9d, .value = 0x00, },
		[35] = { .offset = 0x9e, .value = 0x25, },
		[36] = { .offset = 0x9f, .value = 0x03, },
		[37] = { .offset = 0xa0, .value = 0x28, },
		[38] = { .offset = 0xa1, .value = 0x01, },
		[39] = { .offset = 0xa2, .value = 0x28, },
		[40] = { .offset = 0xa3, .value = 0x05, },
		[41] = { .offset = 0xb6, .value = 0x09, },
		[42] = { .offset = 0xb8, .value = 0x00, },
		[43] = { .offset = 0xb9, .value = 0xa0, },
		[44] = { .offset = 0xba, .value = 0x00, },
		[45] = { .offset = 0xbb, .value = 0x20, },
		[46] = { .offset = 0x10, .value = 0x00, },
		[47] = { .offset = 0x11, .value = 0xa0, },
		[48] = { .offset = 0x12, .value = 0x02, },
		[49] = { .offset = 0x20, .value = 0x00, },
		[50] = { .offset = 0x22, .value = 0x00, },
		[51] = { .offset = 0x23, .value = 0x00, },
		[52] = { .offset = 0x24, .value = 0x00, },
		[53] = { .offset = 0x25, .value = 0x00, },
		[54] = { .offset = 0x8c, .value = 0x10, },
		[55] = { .offset = 0x8d, .value = 0x02, },
		[56] = { .offset = 0x8e, .value = 0x10, },
		[57] = { .offset = 0x8f, .value = 0x00, },
		[58] = { .offset = 0x90, .value = 0xff, },
		[59] = { .offset = 0x91, .value = 0x07, },
		[60] = { .offset = 0x92, .value = 0xa0, },
		[61] = { .offset = 0x93, .value = 0x02, },
		[62] = { .offset = 0xa5, .value = 0x00, },
		[63] = { .offset = 0xa6, .value = 0x00, },
		[64] = { .offset = 0xa7, .value = 0x00, },
		[65] = { .offset = 0xa8, .value = 0x00, },
		[66] = { .offset = 0xa9, .value = 0x04, },
		[67] = { .offset = 0xaa, .value = 0x70, },
		[68] = { .offset = 0xab, .value = 0x4f, },
		[69] = { .offset = 0xac, .value = 0x00, },
		[70] = { .offset = 0xa4, .value = 0x84, },
		[71] = { .offset = 0x7e, .value = 0x18, },
		[72] = { .offset = 0x84, .value = 0x00, },
		[73] = { .offset = 0x85, .value = 0x00, },
		[74] = { .offset = 0x86, .value = 0x00, },
		[75] = { .offset = 0x87, .value = 0x00, },
		[76] = { .offset = 0x88, .value = 0x00, },
		[77] = { .offset = 0x89, .value = 0x00, },
		[78] = { .offset = 0x8a, .value = 0x00, },
		[79] = { .offset = 0x8b, .value = 0x00, },
		[80] = { .offset = 0x26, .value = 0x00, },
		[81] = { .offset = 0x27, .value = 0x00, },
		[82] = { .offset = 0xad, .value = 0x00, },
		[83] = { .offset = 0x08, .value = 0x34, }, /* 0x35 */
		[84] = { .offset = 0x41, .value = 0x00, },
		[85] = { .offset = 0xc0, .value = 0x01, },
	},
};

static const struct ns2501_reg regs_init[] = {
	[0] = { .offset = 0x35, .value = 0xff, },
	[1] = { .offset = 0x34, .value = 0x00, },
	[2] = { .offset = 0x08, .value = 0x30, },
};

struct ns2501_priv {
	bool quiet;
	const struct ns2501_reg *regs;
};

#define NSPTR(d) ((NS2501Ptr)(d->DriverPrivate.ptr))

/*
 * For reasons unclear to me, the ns2501 at least on the Fujitsu/Siemens
 * laptops does not react on the i2c bus unless
 * both the PLL is running and the display is configured in its native
 * resolution.
 * This function forces the DVO on, and stores the registers it touches.
 * Afterwards, registers are restored to regular values.
 *
 * This is pretty much a hack, though it works.
 * Without that, ns2501_readb and ns2501_writeb fail
 * when switching the resolution.
 */

/*
** Read a register from the ns2501.
** Returns true if successful, false otherwise.
** If it returns false, it might be wise to enable the
** DVO with the above function.
*/
static bool ns2501_readb(struct intel_dvo_device *dvo, int addr, uint8_t * ch)
{
	struct ns2501_priv *ns = dvo->dev_priv;
	struct i2c_adapter *adapter = dvo->i2c_bus;
	u8 out_buf[2];
	u8 in_buf[2];

	struct i2c_msg msgs[] = {
		{
		 .addr = dvo->slave_addr,
		 .flags = 0,
		 .len = 1,
		 .buf = out_buf,
		 },
		{
		 .addr = dvo->slave_addr,
		 .flags = I2C_M_RD,
		 .len = 1,
		 .buf = in_buf,
		 }
	};

	out_buf[0] = addr;
	out_buf[1] = 0;

	if (i2c_transfer(adapter, msgs, 2) == 2) {
		*ch = in_buf[0];
		return true;
	}

	if (!ns->quiet) {
		DRM_DEBUG_KMS
		    ("Unable to read register 0x%02x from %s:0x%02x.\n", addr,
		     adapter->name, dvo->slave_addr);
	}

	return false;
}

/*
** Write a register to the ns2501.
** Returns true if successful, false otherwise.
** If it returns false, it might be wise to enable the
** DVO with the above function.
*/
static bool ns2501_writeb(struct intel_dvo_device *dvo, int addr, uint8_t ch)
{
	struct ns2501_priv *ns = dvo->dev_priv;
	struct i2c_adapter *adapter = dvo->i2c_bus;
	uint8_t out_buf[2];

	struct i2c_msg msg = {
		.addr = dvo->slave_addr,
		.flags = 0,
		.len = 2,
		.buf = out_buf,
	};

	out_buf[0] = addr;
	out_buf[1] = ch;

	if (i2c_transfer(adapter, &msg, 1) == 1) {
		return true;
	}

	if (!ns->quiet) {
		DRM_DEBUG_KMS("Unable to write register 0x%02x to %s:%d\n",
			      addr, adapter->name, dvo->slave_addr);
	}

	return false;
}

/* National Semiconductor 2501 driver for chip on i2c bus
 * scan for the chip on the bus.
 * Hope the VBIOS initialized the PLL correctly so we can
 * talk to it. If not, it will not be seen and not detected.
 * Bummer!
 */
static bool ns2501_init(struct intel_dvo_device *dvo,
			struct i2c_adapter *adapter)
{
	/* this will detect the NS2501 chip on the specified i2c bus */
	struct ns2501_priv *ns;
	unsigned char ch;

	ns = kzalloc(sizeof(struct ns2501_priv), GFP_KERNEL);
	if (ns == NULL)
		return false;

	dvo->i2c_bus = adapter;
	dvo->dev_priv = ns;
	ns->quiet = true;

	if (!ns2501_readb(dvo, NS2501_VID_LO, &ch))
		goto out;

	if (ch != (NS2501_VID & 0xff)) {
		DRM_DEBUG_KMS("ns2501 not detected got %d: from %s Slave %d.\n",
			      ch, adapter->name, dvo->slave_addr);
		goto out;
	}

	if (!ns2501_readb(dvo, NS2501_DID_LO, &ch))
		goto out;

	if (ch != (NS2501_DID & 0xff)) {
		DRM_DEBUG_KMS("ns2501 not detected got %d: from %s Slave %d.\n",
			      ch, adapter->name, dvo->slave_addr);
		goto out;
	}
	ns->quiet = false;

	DRM_DEBUG_KMS("init ns2501 dvo controller successfully!\n");

	return true;

out:
	kfree(ns);
	return false;
}

static enum drm_connector_status ns2501_detect(struct intel_dvo_device *dvo)
{
	/*
	 * This is a Laptop display, it doesn't have hotplugging.
	 * Even if not, the detection bit of the 2501 is unreliable as
	 * it only works for some display types.
	 * It is even more unreliable as the PLL must be active for
	 * allowing reading from the chiop.
	 */
	return connector_status_connected;
}

static enum drm_mode_status ns2501_mode_valid(struct intel_dvo_device *dvo,
					      struct drm_display_mode *mode)
{
	DRM_DEBUG_KMS
	    ("is mode valid (hdisplay=%d,htotal=%d,vdisplay=%d,vtotal=%d)\n",
	     mode->hdisplay, mode->htotal, mode->vdisplay, mode->vtotal);

	/*
	 * Currently, these are all the modes I have data from.
	 * More might exist. Unclear how to find the native resolution
	 * of the panel in here so we could always accept it
	 * by disabling the scaler.
	 */
	if ((mode->hdisplay == 640 && mode->vdisplay == 480 && mode->clock == 25175) ||
	    (mode->hdisplay == 800 && mode->vdisplay == 600 && mode->clock == 40000) ||
	    (mode->hdisplay == 1024 && mode->vdisplay == 768 && mode->clock == 65000)) {
		return MODE_OK;
	} else {
		return MODE_ONE_SIZE;	/* Is this a reasonable error? */
	}
}

static void ns2501_mode_set(struct intel_dvo_device *dvo,
			    struct drm_display_mode *mode,
			    struct drm_display_mode *adjusted_mode)
{
	struct ns2501_priv *ns = (struct ns2501_priv *)(dvo->dev_priv);
	int mode_idx, i;

	DRM_DEBUG_KMS
	    ("set mode (hdisplay=%d,htotal=%d,vdisplay=%d,vtotal=%d).\n",
	     mode->hdisplay, mode->htotal, mode->vdisplay, mode->vtotal);

	if (mode->hdisplay == 640 && mode->vdisplay == 480)
		mode_idx = MODE_640x480;
	else if (mode->hdisplay == 800 && mode->vdisplay == 600)
		mode_idx = MODE_800x600;
	else if (mode->hdisplay == 1024 && mode->vdisplay == 768)
		mode_idx = MODE_1024x768;
	else
		return;

	/* Hopefully doing it every time won't hurt... */
	for (i = 0; i < ARRAY_SIZE(regs_init); i++)
		ns2501_writeb(dvo, regs_init[i].offset, regs_init[i].value);

	ns->regs = regs_1024x768[mode_idx];

	for (i = 0; i < 84; i++)
		ns2501_writeb(dvo, ns->regs[i].offset, ns->regs[i].value);
}

/* set the NS2501 power state */
static bool ns2501_get_hw_state(struct intel_dvo_device *dvo)
{
	unsigned char ch;

	if (!ns2501_readb(dvo, NS2501_REG8, &ch))
		return false;

	return ch & NS2501_8_PD;
}

/* set the NS2501 power state */
static void ns2501_dpms(struct intel_dvo_device *dvo, bool enable)
{
	struct ns2501_priv *ns = (struct ns2501_priv *)(dvo->dev_priv);

	DRM_DEBUG_KMS("Trying set the dpms of the DVO to %i\n", enable);

	if (enable) {
		if (WARN_ON(ns->regs[83].offset != 0x08 ||
			    ns->regs[84].offset != 0x41 ||
			    ns->regs[85].offset != 0xc0))
			return;

		ns2501_writeb(dvo, 0xc0, ns->regs[85].value | 0x08);

		ns2501_writeb(dvo, 0x41, ns->regs[84].value);

		ns2501_writeb(dvo, 0x34, 0x01);
		msleep(15);

		ns2501_writeb(dvo, 0x08, 0x35);
		if (!(ns->regs[83].value & NS2501_8_BPAS))
			ns2501_writeb(dvo, 0x08, 0x31);
		msleep(200);

		ns2501_writeb(dvo, 0x34, 0x03);

		ns2501_writeb(dvo, 0xc0, ns->regs[85].value);
	} else {
		ns2501_writeb(dvo, 0x34, 0x01);
		msleep(200);

		ns2501_writeb(dvo, 0x08, 0x34);
		msleep(15);

		ns2501_writeb(dvo, 0x34, 0x00);
	}
}

static void ns2501_destroy(struct intel_dvo_device *dvo)
{
	struct ns2501_priv *ns = dvo->dev_priv;

	if (ns) {
		kfree(ns);
		dvo->dev_priv = NULL;
	}
}

struct intel_dvo_dev_ops ns2501_ops = {
	.init = ns2501_init,
	.detect = ns2501_detect,
	.mode_valid = ns2501_mode_valid,
	.mode_set = ns2501_mode_set,
	.dpms = ns2501_dpms,
	.get_hw_state = ns2501_get_hw_state,
	.destroy = ns2501_destroy,
};
