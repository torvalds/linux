/* QLogic qed NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
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

#include <linux/crc32.h>
#include "qed.h"
#include "qed_dev_api.h"
#include "qed_mcp.h"
#include "qed_sp.h"
#include "qed_selftest.h"

int qed_selftest_memory(struct qed_dev *cdev)
{
	int rc = 0, i;

	for_each_hwfn(cdev, i) {
		rc = qed_sp_heartbeat_ramrod(&cdev->hwfns[i]);
		if (rc)
			return rc;
	}

	return rc;
}

int qed_selftest_interrupt(struct qed_dev *cdev)
{
	int rc = 0, i;

	for_each_hwfn(cdev, i) {
		rc = qed_sp_heartbeat_ramrod(&cdev->hwfns[i]);
		if (rc)
			return rc;
	}

	return rc;
}

int qed_selftest_register(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn;
	struct qed_ptt *p_ptt;
	int rc = 0, i;

	/* although performed by MCP, this test is per engine */
	for_each_hwfn(cdev, i) {
		p_hwfn = &cdev->hwfns[i];
		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt) {
			DP_ERR(p_hwfn, "failed to acquire ptt\n");
			return -EBUSY;
		}
		rc = qed_mcp_bist_register_test(p_hwfn, p_ptt);
		qed_ptt_release(p_hwfn, p_ptt);
		if (rc)
			break;
	}

	return rc;
}

int qed_selftest_clock(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn;
	struct qed_ptt *p_ptt;
	int rc = 0, i;

	/* although performed by MCP, this test is per engine */
	for_each_hwfn(cdev, i) {
		p_hwfn = &cdev->hwfns[i];
		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt) {
			DP_ERR(p_hwfn, "failed to acquire ptt\n");
			return -EBUSY;
		}
		rc = qed_mcp_bist_clock_test(p_hwfn, p_ptt);
		qed_ptt_release(p_hwfn, p_ptt);
		if (rc)
			break;
	}

	return rc;
}

int qed_selftest_nvram(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt = qed_ptt_acquire(p_hwfn);
	u32 num_images, i, j, nvm_crc, calc_crc;
	struct bist_nvm_image_att image_att;
	u8 *buf = NULL;
	__be32 val;
	int rc;

	if (!p_ptt) {
		DP_ERR(p_hwfn, "failed to acquire ptt\n");
		return -EBUSY;
	}

	/* Acquire from MFW the amount of available images */
	rc = qed_mcp_bist_nvm_get_num_images(p_hwfn, p_ptt, &num_images);
	if (rc || !num_images) {
		DP_ERR(p_hwfn, "Failed getting number of images\n");
		rc = -EINVAL;
		goto err0;
	}

	/* Iterate over images and validate CRC */
	for (i = 0; i < num_images; i++) {
		/* This mailbox returns information about the image required for
		 * reading it.
		 */
		rc = qed_mcp_bist_nvm_get_image_att(p_hwfn, p_ptt,
						    &image_att, i);
		if (rc) {
			DP_ERR(p_hwfn,
			       "Failed getting image index %d attributes\n",
			       i);
			goto err0;
		}

		/* After MFW crash dump is collected - the image's CRC stops
		 * being valid.
		 */
		if (image_att.image_type == NVM_TYPE_MDUMP)
			continue;

		DP_VERBOSE(p_hwfn, QED_MSG_SP, "image index %d, size %x\n",
			   i, image_att.len);

		/* Allocate a buffer for holding the nvram image */
		buf = kzalloc(image_att.len, GFP_KERNEL);
		if (!buf) {
			rc = -ENOMEM;
			goto err0;
		}

		/* Read image into buffer */
		rc = qed_mcp_nvm_read(p_hwfn->cdev, image_att.nvm_start_addr,
				      buf, image_att.len);
		if (rc) {
			DP_ERR(p_hwfn,
			       "Failed reading image index %d from nvm.\n", i);
			goto err1;
		}

		/* Convert the buffer into big-endian format (excluding the
		 * closing 4 bytes of CRC).
		 */
		for (j = 0; j < image_att.len - 4; j += 4) {
			val = cpu_to_be32(*(u32 *)&buf[j]);
			*(u32 *)&buf[j] = (__force u32)val;
		}

		/* Calc CRC for the "actual" image buffer, i.e. not including
		 * the last 4 CRC bytes.
		 */
		nvm_crc = *(u32 *)(buf + image_att.len - 4);
		calc_crc = crc32(0xffffffff, buf, image_att.len - 4);
		calc_crc = (__force u32)~cpu_to_be32(calc_crc);
		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "nvm crc 0x%x, calc_crc 0x%x\n", nvm_crc, calc_crc);

		if (calc_crc != nvm_crc) {
			rc = -EINVAL;
			goto err1;
		}

		/* Done with this image; Free to prevent double release
		 * on subsequent failure.
		 */
		kfree(buf);
		buf = NULL;
	}

	qed_ptt_release(p_hwfn, p_ptt);
	return 0;

err1:
	kfree(buf);
err0:
	qed_ptt_release(p_hwfn, p_ptt);
	return rc;
}
