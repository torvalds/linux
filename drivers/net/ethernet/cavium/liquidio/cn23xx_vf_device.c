/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
#include <linux/pci.h>
#include <linux/netdevice.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "cn23xx_vf_device.h"
#include "octeon_main.h"

static int cn23xx_vf_reset_io_queues(struct octeon_device *oct, u32 num_queues)
{
	u32 loop = BUSY_READING_REG_VF_LOOP_COUNT;
	int ret_val = 0;
	u32 q_no;
	u64 d64;

	for (q_no = 0; q_no < num_queues; q_no++) {
		/* set RST bit to 1. This bit applies to both IQ and OQ */
		d64 = octeon_read_csr64(oct,
					CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no));
		d64 |= CN23XX_PKT_INPUT_CTL_RST;
		octeon_write_csr64(oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no),
				   d64);
	}

	/* wait until the RST bit is clear or the RST and QUIET bits are set */
	for (q_no = 0; q_no < num_queues; q_no++) {
		u64 reg_val = octeon_read_csr64(oct,
					CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no));
		while ((READ_ONCE(reg_val) & CN23XX_PKT_INPUT_CTL_RST) &&
		       !(READ_ONCE(reg_val) & CN23XX_PKT_INPUT_CTL_QUIET) &&
		       loop) {
			WRITE_ONCE(reg_val, octeon_read_csr64(
			    oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no)));
			loop--;
		}
		if (!loop) {
			dev_err(&oct->pci_dev->dev,
				"clearing the reset reg failed or setting the quiet reg failed for qno: %u\n",
				q_no);
			return -1;
		}
		WRITE_ONCE(reg_val, READ_ONCE(reg_val) &
			   ~CN23XX_PKT_INPUT_CTL_RST);
		octeon_write_csr64(oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no),
				   READ_ONCE(reg_val));

		WRITE_ONCE(reg_val, octeon_read_csr64(
		    oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no)));
		if (READ_ONCE(reg_val) & CN23XX_PKT_INPUT_CTL_RST) {
			dev_err(&oct->pci_dev->dev,
				"clearing the reset failed for qno: %u\n",
				q_no);
			ret_val = -1;
		}
	}

	return ret_val;
}

static int cn23xx_enable_vf_io_queues(struct octeon_device *oct)
{
	u32 q_no;

	for (q_no = 0; q_no < oct->num_iqs; q_no++) {
		u64 reg_val;

		/* set the corresponding IQ IS_64B bit */
		if (oct->io_qmask.iq64B & BIT_ULL(q_no)) {
			reg_val = octeon_read_csr64(
			    oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no));
			reg_val |= CN23XX_PKT_INPUT_CTL_IS_64B;
			octeon_write_csr64(
			    oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no), reg_val);
		}

		/* set the corresponding IQ ENB bit */
		if (oct->io_qmask.iq & BIT_ULL(q_no)) {
			reg_val = octeon_read_csr64(
			    oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no));
			reg_val |= CN23XX_PKT_INPUT_CTL_RING_ENB;
			octeon_write_csr64(
			    oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(q_no), reg_val);
		}
	}
	for (q_no = 0; q_no < oct->num_oqs; q_no++) {
		u32 reg_val;

		/* set the corresponding OQ ENB bit */
		if (oct->io_qmask.oq & BIT_ULL(q_no)) {
			reg_val = octeon_read_csr(
			    oct, CN23XX_VF_SLI_OQ_PKT_CONTROL(q_no));
			reg_val |= CN23XX_PKT_OUTPUT_CTL_RING_ENB;
			octeon_write_csr(
			    oct, CN23XX_VF_SLI_OQ_PKT_CONTROL(q_no), reg_val);
		}
	}

	return 0;
}

static void cn23xx_disable_vf_io_queues(struct octeon_device *oct)
{
	u32 num_queues = oct->num_iqs;

	/* per HRM, rings can only be disabled via reset operation,
	 * NOT via SLI_PKT()_INPUT/OUTPUT_CONTROL[ENB]
	 */
	if (num_queues < oct->num_oqs)
		num_queues = oct->num_oqs;

	cn23xx_vf_reset_io_queues(oct, num_queues);
}

int cn23xx_setup_octeon_vf_device(struct octeon_device *oct)
{
	struct octeon_cn23xx_vf *cn23xx = (struct octeon_cn23xx_vf *)oct->chip;
	u32 rings_per_vf, ring_flag;
	u64 reg_val;

	if (octeon_map_pci_barx(oct, 0, 0))
		return 1;

	/* INPUT_CONTROL[RPVF] gives the VF IOq count */
	reg_val = octeon_read_csr64(oct, CN23XX_VF_SLI_IQ_PKT_CONTROL64(0));

	oct->pf_num = (reg_val >> CN23XX_PKT_INPUT_CTL_PF_NUM_POS) &
		      CN23XX_PKT_INPUT_CTL_PF_NUM_MASK;
	oct->vf_num = (reg_val >> CN23XX_PKT_INPUT_CTL_VF_NUM_POS) &
		      CN23XX_PKT_INPUT_CTL_VF_NUM_MASK;

	reg_val = reg_val >> CN23XX_PKT_INPUT_CTL_RPVF_POS;

	rings_per_vf = reg_val & CN23XX_PKT_INPUT_CTL_RPVF_MASK;

	ring_flag = 0;

	cn23xx->conf  = oct_get_config_info(oct, LIO_23XX);
	if (!cn23xx->conf) {
		dev_err(&oct->pci_dev->dev, "%s No Config found for CN23XX\n",
			__func__);
		octeon_unmap_pci_barx(oct, 0);
		return 1;
	}

	if (oct->sriov_info.rings_per_vf > rings_per_vf) {
		dev_warn(&oct->pci_dev->dev,
			 "num_queues:%d greater than PF configured rings_per_vf:%d. Reducing to %d.\n",
			 oct->sriov_info.rings_per_vf, rings_per_vf,
			 rings_per_vf);
		oct->sriov_info.rings_per_vf = rings_per_vf;
	} else {
		if (rings_per_vf > num_present_cpus()) {
			dev_warn(&oct->pci_dev->dev,
				 "PF configured rings_per_vf:%d greater than num_cpu:%d. Using rings_per_vf:%d equal to num cpus\n",
				 rings_per_vf,
				 num_present_cpus(),
				 num_present_cpus());
			oct->sriov_info.rings_per_vf =
				num_present_cpus();
		} else {
			oct->sriov_info.rings_per_vf = rings_per_vf;
		}
	}

	oct->fn_list.enable_io_queues = cn23xx_enable_vf_io_queues;
	oct->fn_list.disable_io_queues = cn23xx_disable_vf_io_queues;

	return 0;
}
