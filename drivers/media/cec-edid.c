/*
 * cec-edid - HDMI Consumer Electronics Control EDID & CEC helper functions
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <media/cec-edid.h>

/*
 * This EDID is expected to be a CEA-861 compliant, which means that there are
 * at least two blocks and one or more of the extensions blocks are CEA-861
 * blocks.
 *
 * The returned location is guaranteed to be < size - 1.
 */
static unsigned int cec_get_edid_spa_location(const u8 *edid, unsigned int size)
{
	unsigned int blocks = size / 128;
	unsigned int block;
	u8 d;

	/* Sanity check: at least 2 blocks and a multiple of the block size */
	if (blocks < 2 || size % 128)
		return 0;

	/*
	 * If there are fewer extension blocks than the size, then update
	 * 'blocks'. It is allowed to have more extension blocks than the size,
	 * since some hardware can only read e.g. 256 bytes of the EDID, even
	 * though more blocks are present. The first CEA-861 extension block
	 * should normally be in block 1 anyway.
	 */
	if (edid[0x7e] + 1 < blocks)
		blocks = edid[0x7e] + 1;

	for (block = 1; block < blocks; block++) {
		unsigned int offset = block * 128;

		/* Skip any non-CEA-861 extension blocks */
		if (edid[offset] != 0x02 || edid[offset + 1] != 0x03)
			continue;

		/* search Vendor Specific Data Block (tag 3) */
		d = edid[offset + 2] & 0x7f;
		/* Check if there are Data Blocks */
		if (d <= 4)
			continue;
		if (d > 4) {
			unsigned int i = offset + 4;
			unsigned int end = offset + d;

			/* Note: 'end' is always < 'size' */
			do {
				u8 tag = edid[i] >> 5;
				u8 len = edid[i] & 0x1f;

				if (tag == 3 && len >= 5 && i + len <= end)
					return i + 4;
				i += len + 1;
			} while (i < end);
		}
	}
	return 0;
}

u16 cec_get_edid_phys_addr(const u8 *edid, unsigned int size,
			   unsigned int *offset)
{
	unsigned int loc = cec_get_edid_spa_location(edid, size);

	if (offset)
		*offset = loc;
	if (loc == 0)
		return CEC_PHYS_ADDR_INVALID;
	return (edid[loc] << 8) | edid[loc + 1];
}
EXPORT_SYMBOL_GPL(cec_get_edid_phys_addr);

void cec_set_edid_phys_addr(u8 *edid, unsigned int size, u16 phys_addr)
{
	unsigned int loc = cec_get_edid_spa_location(edid, size);
	u8 sum = 0;
	unsigned int i;

	if (loc == 0)
		return;
	edid[loc] = phys_addr >> 8;
	edid[loc + 1] = phys_addr & 0xff;
	loc &= ~0x7f;

	/* update the checksum */
	for (i = loc; i < loc + 127; i++)
		sum += edid[i];
	edid[i] = 256 - sum;
}
EXPORT_SYMBOL_GPL(cec_set_edid_phys_addr);

u16 cec_phys_addr_for_input(u16 phys_addr, u8 input)
{
	/* Check if input is sane */
	if (WARN_ON(input == 0 || input > 0xf))
		return CEC_PHYS_ADDR_INVALID;

	if (phys_addr == 0)
		return input << 12;

	if ((phys_addr & 0x0fff) == 0)
		return phys_addr | (input << 8);

	if ((phys_addr & 0x00ff) == 0)
		return phys_addr | (input << 4);

	if ((phys_addr & 0x000f) == 0)
		return phys_addr | input;

	/*
	 * All nibbles are used so no valid physical addresses can be assigned
	 * to the input.
	 */
	return CEC_PHYS_ADDR_INVALID;
}
EXPORT_SYMBOL_GPL(cec_phys_addr_for_input);

int cec_phys_addr_validate(u16 phys_addr, u16 *parent, u16 *port)
{
	int i;

	if (parent)
		*parent = phys_addr;
	if (port)
		*port = 0;
	if (phys_addr == CEC_PHYS_ADDR_INVALID)
		return 0;
	for (i = 0; i < 16; i += 4)
		if (phys_addr & (0xf << i))
			break;
	if (i == 16)
		return 0;
	if (parent)
		*parent = phys_addr & (0xfff0 << i);
	if (port)
		*port = (phys_addr >> i) & 0xf;
	for (i += 4; i < 16; i += 4)
		if ((phys_addr & (0xf << i)) == 0)
			return -EINVAL;
	return 0;
}
EXPORT_SYMBOL_GPL(cec_phys_addr_validate);

MODULE_AUTHOR("Hans Verkuil <hans.verkuil@cisco.com>");
MODULE_DESCRIPTION("CEC EDID helper functions");
MODULE_LICENSE("GPL");
