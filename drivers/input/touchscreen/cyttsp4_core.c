/*
 * cyttsp4_core.c
 * Cypress TrueTouch(TM) Standard Product V4 Core driver module.
 * For use with Cypress Txx4xx parts.
 * Supported parts include:
 * TMA4XX
 * TMA1036
 *
 * Copyright (C) 2012 Cypress Semiconductor
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contact Cypress Semiconductor at www.cypress.com <ttdrivers@cypress.com>
 *
 */

#include "cyttsp4_core.h"
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/slab.h>

/* Timeout in ms. */
#define CY_CORE_REQUEST_EXCLUSIVE_TIMEOUT	500
#define CY_CORE_SLEEP_REQUEST_EXCLUSIVE_TIMEOUT	5000
#define CY_CORE_MODE_CHANGE_TIMEOUT		1000
#define CY_CORE_RESET_AND_WAIT_TIMEOUT		500
#define CY_CORE_WAKEUP_TIMEOUT			500

#define CY_CORE_STARTUP_RETRY_COUNT		3

static const u8 ldr_exit[] = {
	0xFF, 0x01, 0x3B, 0x00, 0x00, 0x4F, 0x6D, 0x17
};

static const u8 ldr_err_app[] = {
	0x01, 0x02, 0x00, 0x00, 0x55, 0xDD, 0x17
};

static inline size_t merge_bytes(u8 high, u8 low)
{
	return (high << 8) + low;
}

#ifdef VERBOSE_DEBUG
static void cyttsp4_pr_buf(struct device *dev, u8 *pr_buf, u8 *dptr, int size,
		const char *data_name)
{
	int i, k;
	const char fmt[] = "%02X ";
	int max;

	if (!size)
		return;

	max = (CY_MAX_PRBUF_SIZE - 1) - sizeof(CY_PR_TRUNCATED);

	pr_buf[0] = 0;
	for (i = k = 0; i < size && k < max; i++, k += 3)
		scnprintf(pr_buf + k, CY_MAX_PRBUF_SIZE, fmt, dptr[i]);

	dev_vdbg(dev, "%s:  %s[0..%d]=%s%s\n", __func__, data_name, size - 1,
			pr_buf, size <= max ? "" : CY_PR_TRUNCATED);
}
#else
#define cyttsp4_pr_buf(dev, pr_buf, dptr, size, data_name) do { } while (0)
#endif

static int cyttsp4_load_status_regs(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	struct device *dev = cd->dev;
	int rc;

	rc = cyttsp4_adap_read(cd, CY_REG_BASE, si->si_ofs.mode_size,
			si->xy_mode);
	if (rc < 0)
		dev_err(dev, "%s: fail read mode regs r=%d\n",
			__func__, rc);
	else
		cyttsp4_pr_buf(dev, cd->pr_buf, si->xy_mode,
			si->si_ofs.mode_size, "xy_mode");

	return rc;
}

static int cyttsp4_handshake(struct cyttsp4 *cd, u8 mode)
{
	u8 cmd = mode ^ CY_HST_TOGGLE;
	int rc;

	/*
	 * Mode change issued, handshaking now will cause endless mode change
	 * requests, for sync mode modechange will do same with handshake
	 * */
	if (mode & CY_HST_MODE_CHANGE)
		return 0;

	rc = cyttsp4_adap_write(cd, CY_REG_BASE, sizeof(cmd), &cmd);
	if (rc < 0)
		dev_err(cd->dev, "%s: bus write fail on handshake (ret=%d)\n",
				__func__, rc);

	return rc;
}

static int cyttsp4_hw_soft_reset(struct cyttsp4 *cd)
{
	u8 cmd = CY_HST_RESET;
	int rc = cyttsp4_adap_write(cd, CY_REG_BASE, sizeof(cmd), &cmd);
	if (rc < 0) {
		dev_err(cd->dev, "%s: FAILED to execute SOFT reset\n",
				__func__);
		return rc;
	}
	return 0;
}

static int cyttsp4_hw_hard_reset(struct cyttsp4 *cd)
{
	if (cd->cpdata->xres) {
		cd->cpdata->xres(cd->cpdata, cd->dev);
		dev_dbg(cd->dev, "%s: execute HARD reset\n", __func__);
		return 0;
	}
	dev_err(cd->dev, "%s: FAILED to execute HARD reset\n", __func__);
	return -ENOSYS;
}

static int cyttsp4_hw_reset(struct cyttsp4 *cd)
{
	int rc = cyttsp4_hw_hard_reset(cd);
	if (rc == -ENOSYS)
		rc = cyttsp4_hw_soft_reset(cd);
	return rc;
}

/*
 * Gets number of bits for a touch filed as parameter,
 * sets maximum value for field which is used as bit mask
 * and returns number of bytes required for that field
 */
static int cyttsp4_bits_2_bytes(unsigned int nbits, size_t *max)
{
	*max = 1UL << nbits;
	return (nbits + 7) / 8;
}

static int cyttsp4_si_data_offsets(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int rc = cyttsp4_adap_read(cd, CY_REG_BASE, sizeof(si->si_data),
			&si->si_data);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail read sysinfo data offsets r=%d\n",
			__func__, rc);
		return rc;
	}

	/* Print sysinfo data offsets */
	cyttsp4_pr_buf(cd->dev, cd->pr_buf, (u8 *)&si->si_data,
		       sizeof(si->si_data), "sysinfo_data_offsets");

	/* convert sysinfo data offset bytes into integers */

	si->si_ofs.map_sz = merge_bytes(si->si_data.map_szh,
			si->si_data.map_szl);
	si->si_ofs.map_sz = merge_bytes(si->si_data.map_szh,
			si->si_data.map_szl);
	si->si_ofs.cydata_ofs = merge_bytes(si->si_data.cydata_ofsh,
			si->si_data.cydata_ofsl);
	si->si_ofs.test_ofs = merge_bytes(si->si_data.test_ofsh,
			si->si_data.test_ofsl);
	si->si_ofs.pcfg_ofs = merge_bytes(si->si_data.pcfg_ofsh,
			si->si_data.pcfg_ofsl);
	si->si_ofs.opcfg_ofs = merge_bytes(si->si_data.opcfg_ofsh,
			si->si_data.opcfg_ofsl);
	si->si_ofs.ddata_ofs = merge_bytes(si->si_data.ddata_ofsh,
			si->si_data.ddata_ofsl);
	si->si_ofs.mdata_ofs = merge_bytes(si->si_data.mdata_ofsh,
			si->si_data.mdata_ofsl);
	return rc;
}

static int cyttsp4_si_get_cydata(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int read_offset;
	int mfgid_sz, calc_mfgid_sz;
	void *p;
	int rc;

	si->si_ofs.cydata_size = si->si_ofs.test_ofs - si->si_ofs.cydata_ofs;
	dev_dbg(cd->dev, "%s: cydata size: %Zd\n", __func__,
			si->si_ofs.cydata_size);

	p = krealloc(si->si_ptrs.cydata, si->si_ofs.cydata_size, GFP_KERNEL);
	if (p == NULL) {
		dev_err(cd->dev, "%s: fail alloc cydata memory\n", __func__);
		return -ENOMEM;
	}
	si->si_ptrs.cydata = p;

	read_offset = si->si_ofs.cydata_ofs;

	/* Read the CYDA registers up to MFGID field */
	rc = cyttsp4_adap_read(cd, read_offset,
			offsetof(struct cyttsp4_cydata, mfgid_sz)
				+ sizeof(si->si_ptrs.cydata->mfgid_sz),
			si->si_ptrs.cydata);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail read cydata r=%d\n",
			__func__, rc);
		return rc;
	}

	/* Check MFGID size */
	mfgid_sz = si->si_ptrs.cydata->mfgid_sz;
	calc_mfgid_sz = si->si_ofs.cydata_size - sizeof(struct cyttsp4_cydata);
	if (mfgid_sz != calc_mfgid_sz) {
		dev_err(cd->dev, "%s: mismatch in MFGID size, reported:%d calculated:%d\n",
			__func__, mfgid_sz, calc_mfgid_sz);
		return -EINVAL;
	}

	read_offset += offsetof(struct cyttsp4_cydata, mfgid_sz)
			+ sizeof(si->si_ptrs.cydata->mfgid_sz);

	/* Read the CYDA registers for MFGID field */
	rc = cyttsp4_adap_read(cd, read_offset, si->si_ptrs.cydata->mfgid_sz,
			si->si_ptrs.cydata->mfg_id);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail read cydata r=%d\n",
			__func__, rc);
		return rc;
	}

	read_offset += si->si_ptrs.cydata->mfgid_sz;

	/* Read the rest of the CYDA registers */
	rc = cyttsp4_adap_read(cd, read_offset,
			sizeof(struct cyttsp4_cydata)
				- offsetof(struct cyttsp4_cydata, cyito_idh),
			&si->si_ptrs.cydata->cyito_idh);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail read cydata r=%d\n",
			__func__, rc);
		return rc;
	}

	cyttsp4_pr_buf(cd->dev, cd->pr_buf, (u8 *)si->si_ptrs.cydata,
		si->si_ofs.cydata_size, "sysinfo_cydata");
	return rc;
}

static int cyttsp4_si_get_test_data(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	void *p;
	int rc;

	si->si_ofs.test_size = si->si_ofs.pcfg_ofs - si->si_ofs.test_ofs;

	p = krealloc(si->si_ptrs.test, si->si_ofs.test_size, GFP_KERNEL);
	if (p == NULL) {
		dev_err(cd->dev, "%s: fail alloc test memory\n", __func__);
		return -ENOMEM;
	}
	si->si_ptrs.test = p;

	rc = cyttsp4_adap_read(cd, si->si_ofs.test_ofs, si->si_ofs.test_size,
			si->si_ptrs.test);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail read test data r=%d\n",
			__func__, rc);
		return rc;
	}

	cyttsp4_pr_buf(cd->dev, cd->pr_buf,
		       (u8 *)si->si_ptrs.test, si->si_ofs.test_size,
		       "sysinfo_test_data");
	if (si->si_ptrs.test->post_codel &
	    CY_POST_CODEL_WDG_RST)
		dev_info(cd->dev, "%s: %s codel=%02X\n",
			 __func__, "Reset was a WATCHDOG RESET",
			 si->si_ptrs.test->post_codel);

	if (!(si->si_ptrs.test->post_codel &
	      CY_POST_CODEL_CFG_DATA_CRC_FAIL))
		dev_info(cd->dev, "%s: %s codel=%02X\n", __func__,
			 "Config Data CRC FAIL",
			 si->si_ptrs.test->post_codel);

	if (!(si->si_ptrs.test->post_codel &
	      CY_POST_CODEL_PANEL_TEST_FAIL))
		dev_info(cd->dev, "%s: %s codel=%02X\n",
			 __func__, "PANEL TEST FAIL",
			 si->si_ptrs.test->post_codel);

	dev_info(cd->dev, "%s: SCANNING is %s codel=%02X\n",
		 __func__, si->si_ptrs.test->post_codel & 0x08 ?
		 "ENABLED" : "DISABLED",
		 si->si_ptrs.test->post_codel);
	return rc;
}

static int cyttsp4_si_get_pcfg_data(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	void *p;
	int rc;

	si->si_ofs.pcfg_size = si->si_ofs.opcfg_ofs - si->si_ofs.pcfg_ofs;

	p = krealloc(si->si_ptrs.pcfg, si->si_ofs.pcfg_size, GFP_KERNEL);
	if (p == NULL) {
		rc = -ENOMEM;
		dev_err(cd->dev, "%s: fail alloc pcfg memory r=%d\n",
			__func__, rc);
		return rc;
	}
	si->si_ptrs.pcfg = p;

	rc = cyttsp4_adap_read(cd, si->si_ofs.pcfg_ofs, si->si_ofs.pcfg_size,
			si->si_ptrs.pcfg);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail read pcfg data r=%d\n",
			__func__, rc);
		return rc;
	}

	si->si_ofs.max_x = merge_bytes((si->si_ptrs.pcfg->res_xh
			& CY_PCFG_RESOLUTION_X_MASK), si->si_ptrs.pcfg->res_xl);
	si->si_ofs.x_origin = !!(si->si_ptrs.pcfg->res_xh
			& CY_PCFG_ORIGIN_X_MASK);
	si->si_ofs.max_y = merge_bytes((si->si_ptrs.pcfg->res_yh
			& CY_PCFG_RESOLUTION_Y_MASK), si->si_ptrs.pcfg->res_yl);
	si->si_ofs.y_origin = !!(si->si_ptrs.pcfg->res_yh
			& CY_PCFG_ORIGIN_Y_MASK);
	si->si_ofs.max_p = merge_bytes(si->si_ptrs.pcfg->max_zh,
			si->si_ptrs.pcfg->max_zl);

	cyttsp4_pr_buf(cd->dev, cd->pr_buf,
		       (u8 *)si->si_ptrs.pcfg,
		       si->si_ofs.pcfg_size, "sysinfo_pcfg_data");
	return rc;
}

static int cyttsp4_si_get_opcfg_data(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	struct cyttsp4_tch_abs_params *tch;
	struct cyttsp4_tch_rec_params *tch_old, *tch_new;
	enum cyttsp4_tch_abs abs;
	int i;
	void *p;
	int rc;

	si->si_ofs.opcfg_size = si->si_ofs.ddata_ofs - si->si_ofs.opcfg_ofs;

	p = krealloc(si->si_ptrs.opcfg, si->si_ofs.opcfg_size, GFP_KERNEL);
	if (p == NULL) {
		dev_err(cd->dev, "%s: fail alloc opcfg memory\n", __func__);
		rc = -ENOMEM;
		goto cyttsp4_si_get_opcfg_data_exit;
	}
	si->si_ptrs.opcfg = p;

	rc = cyttsp4_adap_read(cd, si->si_ofs.opcfg_ofs, si->si_ofs.opcfg_size,
			si->si_ptrs.opcfg);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail read opcfg data r=%d\n",
			__func__, rc);
		goto cyttsp4_si_get_opcfg_data_exit;
	}
	si->si_ofs.cmd_ofs = si->si_ptrs.opcfg->cmd_ofs;
	si->si_ofs.rep_ofs = si->si_ptrs.opcfg->rep_ofs;
	si->si_ofs.rep_sz = (si->si_ptrs.opcfg->rep_szh * 256) +
		si->si_ptrs.opcfg->rep_szl;
	si->si_ofs.num_btns = si->si_ptrs.opcfg->num_btns;
	si->si_ofs.num_btn_regs = (si->si_ofs.num_btns +
		CY_NUM_BTN_PER_REG - 1) / CY_NUM_BTN_PER_REG;
	si->si_ofs.tt_stat_ofs = si->si_ptrs.opcfg->tt_stat_ofs;
	si->si_ofs.obj_cfg0 = si->si_ptrs.opcfg->obj_cfg0;
	si->si_ofs.max_tchs = si->si_ptrs.opcfg->max_tchs &
		CY_BYTE_OFS_MASK;
	si->si_ofs.tch_rec_size = si->si_ptrs.opcfg->tch_rec_size &
		CY_BYTE_OFS_MASK;

	/* Get the old touch fields */
	for (abs = CY_TCH_X; abs < CY_NUM_TCH_FIELDS; abs++) {
		tch = &si->si_ofs.tch_abs[abs];
		tch_old = &si->si_ptrs.opcfg->tch_rec_old[abs];

		tch->ofs = tch_old->loc & CY_BYTE_OFS_MASK;
		tch->size = cyttsp4_bits_2_bytes(tch_old->size,
						 &tch->max);
		tch->bofs = (tch_old->loc & CY_BOFS_MASK) >> CY_BOFS_SHIFT;
	}

	/* button fields */
	si->si_ofs.btn_rec_size = si->si_ptrs.opcfg->btn_rec_size;
	si->si_ofs.btn_diff_ofs = si->si_ptrs.opcfg->btn_diff_ofs;
	si->si_ofs.btn_diff_size = si->si_ptrs.opcfg->btn_diff_size;

	if (si->si_ofs.tch_rec_size > CY_TMA1036_TCH_REC_SIZE) {
		/* Get the extended touch fields */
		for (i = 0; i < CY_NUM_EXT_TCH_FIELDS; abs++, i++) {
			tch = &si->si_ofs.tch_abs[abs];
			tch_new = &si->si_ptrs.opcfg->tch_rec_new[i];

			tch->ofs = tch_new->loc & CY_BYTE_OFS_MASK;
			tch->size = cyttsp4_bits_2_bytes(tch_new->size,
							 &tch->max);
			tch->bofs = (tch_new->loc & CY_BOFS_MASK) >> CY_BOFS_SHIFT;
		}
	}

	for (abs = 0; abs < CY_TCH_NUM_ABS; abs++) {
		dev_dbg(cd->dev, "%s: tch_rec_%s\n", __func__,
			cyttsp4_tch_abs_string[abs]);
		dev_dbg(cd->dev, "%s:     ofs =%2Zd\n", __func__,
			si->si_ofs.tch_abs[abs].ofs);
		dev_dbg(cd->dev, "%s:     siz =%2Zd\n", __func__,
			si->si_ofs.tch_abs[abs].size);
		dev_dbg(cd->dev, "%s:     max =%2Zd\n", __func__,
			si->si_ofs.tch_abs[abs].max);
		dev_dbg(cd->dev, "%s:     bofs=%2Zd\n", __func__,
			si->si_ofs.tch_abs[abs].bofs);
	}

	si->si_ofs.mode_size = si->si_ofs.tt_stat_ofs + 1;
	si->si_ofs.data_size = si->si_ofs.max_tchs *
		si->si_ptrs.opcfg->tch_rec_size;

	cyttsp4_pr_buf(cd->dev, cd->pr_buf, (u8 *)si->si_ptrs.opcfg,
		si->si_ofs.opcfg_size, "sysinfo_opcfg_data");

cyttsp4_si_get_opcfg_data_exit:
	return rc;
}

static int cyttsp4_si_get_ddata(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	void *p;
	int rc;

	si->si_ofs.ddata_size = si->si_ofs.mdata_ofs - si->si_ofs.ddata_ofs;

	p = krealloc(si->si_ptrs.ddata, si->si_ofs.ddata_size, GFP_KERNEL);
	if (p == NULL) {
		dev_err(cd->dev, "%s: fail alloc ddata memory\n", __func__);
		return -ENOMEM;
	}
	si->si_ptrs.ddata = p;

	rc = cyttsp4_adap_read(cd, si->si_ofs.ddata_ofs, si->si_ofs.ddata_size,
			si->si_ptrs.ddata);
	if (rc < 0)
		dev_err(cd->dev, "%s: fail read ddata data r=%d\n",
			__func__, rc);
	else
		cyttsp4_pr_buf(cd->dev, cd->pr_buf,
			       (u8 *)si->si_ptrs.ddata,
			       si->si_ofs.ddata_size, "sysinfo_ddata");
	return rc;
}

static int cyttsp4_si_get_mdata(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	void *p;
	int rc;

	si->si_ofs.mdata_size = si->si_ofs.map_sz - si->si_ofs.mdata_ofs;

	p = krealloc(si->si_ptrs.mdata, si->si_ofs.mdata_size, GFP_KERNEL);
	if (p == NULL) {
		dev_err(cd->dev, "%s: fail alloc mdata memory\n", __func__);
		return -ENOMEM;
	}
	si->si_ptrs.mdata = p;

	rc = cyttsp4_adap_read(cd, si->si_ofs.mdata_ofs, si->si_ofs.mdata_size,
			si->si_ptrs.mdata);
	if (rc < 0)
		dev_err(cd->dev, "%s: fail read mdata data r=%d\n",
			__func__, rc);
	else
		cyttsp4_pr_buf(cd->dev, cd->pr_buf,
			       (u8 *)si->si_ptrs.mdata,
			       si->si_ofs.mdata_size, "sysinfo_mdata");
	return rc;
}

static int cyttsp4_si_get_btn_data(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int btn;
	int num_defined_keys;
	u16 *key_table;
	void *p;
	int rc = 0;

	if (si->si_ofs.num_btns) {
		si->si_ofs.btn_keys_size = si->si_ofs.num_btns *
			sizeof(struct cyttsp4_btn);

		p = krealloc(si->btn, si->si_ofs.btn_keys_size,
				GFP_KERNEL|__GFP_ZERO);
		if (p == NULL) {
			dev_err(cd->dev, "%s: %s\n", __func__,
				"fail alloc btn_keys memory");
			return -ENOMEM;
		}
		si->btn = p;

		if (cd->cpdata->sett[CY_IC_GRPNUM_BTN_KEYS] == NULL)
			num_defined_keys = 0;
		else if (cd->cpdata->sett[CY_IC_GRPNUM_BTN_KEYS]->data == NULL)
			num_defined_keys = 0;
		else
			num_defined_keys = cd->cpdata->sett
				[CY_IC_GRPNUM_BTN_KEYS]->size;

		for (btn = 0; btn < si->si_ofs.num_btns &&
			btn < num_defined_keys; btn++) {
			key_table = (u16 *)cd->cpdata->sett
				[CY_IC_GRPNUM_BTN_KEYS]->data;
			si->btn[btn].key_code = key_table[btn];
			si->btn[btn].state = CY_BTN_RELEASED;
			si->btn[btn].enabled = true;
		}
		for (; btn < si->si_ofs.num_btns; btn++) {
			si->btn[btn].key_code = KEY_RESERVED;
			si->btn[btn].state = CY_BTN_RELEASED;
			si->btn[btn].enabled = true;
		}

		return rc;
	}

	si->si_ofs.btn_keys_size = 0;
	kfree(si->btn);
	si->btn = NULL;
	return rc;
}

static int cyttsp4_si_get_op_data_ptrs(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	void *p;

	p = krealloc(si->xy_mode, si->si_ofs.mode_size, GFP_KERNEL|__GFP_ZERO);
	if (p == NULL)
		return -ENOMEM;
	si->xy_mode = p;

	p = krealloc(si->xy_data, si->si_ofs.data_size, GFP_KERNEL|__GFP_ZERO);
	if (p == NULL)
		return -ENOMEM;
	si->xy_data = p;

	p = krealloc(si->btn_rec_data,
			si->si_ofs.btn_rec_size * si->si_ofs.num_btns,
			GFP_KERNEL|__GFP_ZERO);
	if (p == NULL)
		return -ENOMEM;
	si->btn_rec_data = p;

	return 0;
}

static void cyttsp4_si_put_log_data(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	dev_dbg(cd->dev, "%s: cydata_ofs =%4Zd siz=%4Zd\n", __func__,
		si->si_ofs.cydata_ofs, si->si_ofs.cydata_size);
	dev_dbg(cd->dev, "%s: test_ofs   =%4Zd siz=%4Zd\n", __func__,
		si->si_ofs.test_ofs, si->si_ofs.test_size);
	dev_dbg(cd->dev, "%s: pcfg_ofs   =%4Zd siz=%4Zd\n", __func__,
		si->si_ofs.pcfg_ofs, si->si_ofs.pcfg_size);
	dev_dbg(cd->dev, "%s: opcfg_ofs  =%4Zd siz=%4Zd\n", __func__,
		si->si_ofs.opcfg_ofs, si->si_ofs.opcfg_size);
	dev_dbg(cd->dev, "%s: ddata_ofs  =%4Zd siz=%4Zd\n", __func__,
		si->si_ofs.ddata_ofs, si->si_ofs.ddata_size);
	dev_dbg(cd->dev, "%s: mdata_ofs  =%4Zd siz=%4Zd\n", __func__,
		si->si_ofs.mdata_ofs, si->si_ofs.mdata_size);

	dev_dbg(cd->dev, "%s: cmd_ofs       =%4Zd\n", __func__,
		si->si_ofs.cmd_ofs);
	dev_dbg(cd->dev, "%s: rep_ofs       =%4Zd\n", __func__,
		si->si_ofs.rep_ofs);
	dev_dbg(cd->dev, "%s: rep_sz        =%4Zd\n", __func__,
		si->si_ofs.rep_sz);
	dev_dbg(cd->dev, "%s: num_btns      =%4Zd\n", __func__,
		si->si_ofs.num_btns);
	dev_dbg(cd->dev, "%s: num_btn_regs  =%4Zd\n", __func__,
		si->si_ofs.num_btn_regs);
	dev_dbg(cd->dev, "%s: tt_stat_ofs   =%4Zd\n", __func__,
		si->si_ofs.tt_stat_ofs);
	dev_dbg(cd->dev, "%s: tch_rec_size  =%4Zd\n", __func__,
		si->si_ofs.tch_rec_size);
	dev_dbg(cd->dev, "%s: max_tchs      =%4Zd\n", __func__,
		si->si_ofs.max_tchs);
	dev_dbg(cd->dev, "%s: mode_size     =%4Zd\n", __func__,
		si->si_ofs.mode_size);
	dev_dbg(cd->dev, "%s: data_size     =%4Zd\n", __func__,
		si->si_ofs.data_size);
	dev_dbg(cd->dev, "%s: map_sz        =%4Zd\n", __func__,
		si->si_ofs.map_sz);

	dev_dbg(cd->dev, "%s: btn_rec_size   =%2Zd\n", __func__,
		si->si_ofs.btn_rec_size);
	dev_dbg(cd->dev, "%s: btn_diff_ofs   =%2Zd\n", __func__,
		si->si_ofs.btn_diff_ofs);
	dev_dbg(cd->dev, "%s: btn_diff_size  =%2Zd\n", __func__,
		si->si_ofs.btn_diff_size);

	dev_dbg(cd->dev, "%s: max_x    = 0x%04ZX (%Zd)\n", __func__,
		si->si_ofs.max_x, si->si_ofs.max_x);
	dev_dbg(cd->dev, "%s: x_origin = %Zd (%s)\n", __func__,
		si->si_ofs.x_origin,
		si->si_ofs.x_origin == CY_NORMAL_ORIGIN ?
		"left corner" : "right corner");
	dev_dbg(cd->dev, "%s: max_y    = 0x%04ZX (%Zd)\n", __func__,
		si->si_ofs.max_y, si->si_ofs.max_y);
	dev_dbg(cd->dev, "%s: y_origin = %Zd (%s)\n", __func__,
		si->si_ofs.y_origin,
		si->si_ofs.y_origin == CY_NORMAL_ORIGIN ?
		"upper corner" : "lower corner");
	dev_dbg(cd->dev, "%s: max_p    = 0x%04ZX (%Zd)\n", __func__,
		si->si_ofs.max_p, si->si_ofs.max_p);

	dev_dbg(cd->dev, "%s: xy_mode=%p xy_data=%p\n", __func__,
		si->xy_mode, si->xy_data);
}

static int cyttsp4_get_sysinfo_regs(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;
	int rc;

	rc = cyttsp4_si_data_offsets(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_cydata(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_test_data(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_pcfg_data(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_opcfg_data(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_ddata(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_mdata(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_btn_data(cd);
	if (rc < 0)
		return rc;

	rc = cyttsp4_si_get_op_data_ptrs(cd);
	if (rc < 0) {
		dev_err(cd->dev, "%s: failed to get_op_data\n",
			__func__);
		return rc;
	}

	cyttsp4_si_put_log_data(cd);

	/* provide flow control handshake */
	rc = cyttsp4_handshake(cd, si->si_data.hst_mode);
	if (rc < 0)
		dev_err(cd->dev, "%s: handshake fail on sysinfo reg\n",
			__func__);

	si->ready = true;
	return rc;
}

static void cyttsp4_queue_startup_(struct cyttsp4 *cd)
{
	if (cd->startup_state == STARTUP_NONE) {
		cd->startup_state = STARTUP_QUEUED;
		schedule_work(&cd->startup_work);
		dev_dbg(cd->dev, "%s: cyttsp4_startup queued\n", __func__);
	} else {
		dev_dbg(cd->dev, "%s: startup_state = %d\n", __func__,
			cd->startup_state);
	}
}

static void cyttsp4_report_slot_liftoff(struct cyttsp4_mt_data *md,
		int max_slots)
{
	int t;

	if (md->num_prv_tch == 0)
		return;

	for (t = 0; t < max_slots; t++) {
		input_mt_slot(md->input, t);
		input_mt_report_slot_state(md->input,
			MT_TOOL_FINGER, false);
	}
}

static void cyttsp4_lift_all(struct cyttsp4_mt_data *md)
{
	if (!md->si)
		return;

	if (md->num_prv_tch != 0) {
		cyttsp4_report_slot_liftoff(md,
				md->si->si_ofs.tch_abs[CY_TCH_T].max);
		input_sync(md->input);
		md->num_prv_tch = 0;
	}
}

static void cyttsp4_get_touch_axis(struct cyttsp4_mt_data *md,
	int *axis, int size, int max, u8 *xy_data, int bofs)
{
	int nbyte;
	int next;

	for (nbyte = 0, *axis = 0, next = 0; nbyte < size; nbyte++) {
		dev_vdbg(&md->input->dev,
			"%s: *axis=%02X(%d) size=%d max=%08X xy_data=%p"
			" xy_data[%d]=%02X(%d) bofs=%d\n",
			__func__, *axis, *axis, size, max, xy_data, next,
			xy_data[next], xy_data[next], bofs);
		*axis = (*axis * 256) + (xy_data[next] >> bofs);
		next++;
	}

	*axis &= max - 1;

	dev_vdbg(&md->input->dev,
		"%s: *axis=%02X(%d) size=%d max=%08X xy_data=%p"
		" xy_data[%d]=%02X(%d)\n",
		__func__, *axis, *axis, size, max, xy_data, next,
		xy_data[next], xy_data[next]);
}

static void cyttsp4_get_touch(struct cyttsp4_mt_data *md,
	struct cyttsp4_touch *touch, u8 *xy_data)
{
	struct device *dev = &md->input->dev;
	struct cyttsp4_sysinfo *si = md->si;
	enum cyttsp4_tch_abs abs;
	int tmp;
	bool flipped;

	for (abs = CY_TCH_X; abs < CY_TCH_NUM_ABS; abs++) {
		cyttsp4_get_touch_axis(md, &touch->abs[abs],
			si->si_ofs.tch_abs[abs].size,
			si->si_ofs.tch_abs[abs].max,
			xy_data + si->si_ofs.tch_abs[abs].ofs,
			si->si_ofs.tch_abs[abs].bofs);
		dev_vdbg(dev, "%s: get %s=%04X(%d)\n", __func__,
			cyttsp4_tch_abs_string[abs],
			touch->abs[abs], touch->abs[abs]);
	}

	if (md->pdata->flags & CY_FLAG_FLIP) {
		tmp = touch->abs[CY_TCH_X];
		touch->abs[CY_TCH_X] = touch->abs[CY_TCH_Y];
		touch->abs[CY_TCH_Y] = tmp;
		flipped = true;
	} else
		flipped = false;

	if (md->pdata->flags & CY_FLAG_INV_X) {
		if (flipped)
			touch->abs[CY_TCH_X] = md->si->si_ofs.max_y -
				touch->abs[CY_TCH_X];
		else
			touch->abs[CY_TCH_X] = md->si->si_ofs.max_x -
				touch->abs[CY_TCH_X];
	}
	if (md->pdata->flags & CY_FLAG_INV_Y) {
		if (flipped)
			touch->abs[CY_TCH_Y] = md->si->si_ofs.max_x -
				touch->abs[CY_TCH_Y];
		else
			touch->abs[CY_TCH_Y] = md->si->si_ofs.max_y -
				touch->abs[CY_TCH_Y];
	}

	dev_vdbg(dev, "%s: flip=%s inv-x=%s inv-y=%s x=%04X(%d) y=%04X(%d)\n",
		__func__, flipped ? "true" : "false",
		md->pdata->flags & CY_FLAG_INV_X ? "true" : "false",
		md->pdata->flags & CY_FLAG_INV_Y ? "true" : "false",
		touch->abs[CY_TCH_X], touch->abs[CY_TCH_X],
		touch->abs[CY_TCH_Y], touch->abs[CY_TCH_Y]);
}

static void cyttsp4_final_sync(struct input_dev *input, int max_slots, int *ids)
{
	int t;

	for (t = 0; t < max_slots; t++) {
		if (ids[t])
			continue;
		input_mt_slot(input, t);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, false);
	}

	input_sync(input);
}

static void cyttsp4_get_mt_touches(struct cyttsp4_mt_data *md, int num_cur_tch)
{
	struct device *dev = &md->input->dev;
	struct cyttsp4_sysinfo *si = md->si;
	struct cyttsp4_touch tch;
	int sig;
	int i, j, t = 0;
	int ids[max(CY_TMA1036_MAX_TCH, CY_TMA4XX_MAX_TCH)];

	memset(ids, 0, si->si_ofs.tch_abs[CY_TCH_T].max * sizeof(int));
	for (i = 0; i < num_cur_tch; i++) {
		cyttsp4_get_touch(md, &tch, si->xy_data +
			(i * si->si_ofs.tch_rec_size));
		if ((tch.abs[CY_TCH_T] < md->pdata->frmwrk->abs
			[(CY_ABS_ID_OST * CY_NUM_ABS_SET) + CY_MIN_OST]) ||
			(tch.abs[CY_TCH_T] > md->pdata->frmwrk->abs
			[(CY_ABS_ID_OST * CY_NUM_ABS_SET) + CY_MAX_OST])) {
			dev_err(dev, "%s: tch=%d -> bad trk_id=%d max_id=%d\n",
				__func__, i, tch.abs[CY_TCH_T],
				md->pdata->frmwrk->abs[(CY_ABS_ID_OST *
				CY_NUM_ABS_SET) + CY_MAX_OST]);
			continue;
		}

		/* use 0 based track id's */
		sig = md->pdata->frmwrk->abs
			[(CY_ABS_ID_OST * CY_NUM_ABS_SET) + 0];
		if (sig != CY_IGNORE_VALUE) {
			t = tch.abs[CY_TCH_T] - md->pdata->frmwrk->abs
				[(CY_ABS_ID_OST * CY_NUM_ABS_SET) + CY_MIN_OST];
			if (tch.abs[CY_TCH_E] == CY_EV_LIFTOFF) {
				dev_dbg(dev, "%s: t=%d e=%d lift-off\n",
					__func__, t, tch.abs[CY_TCH_E]);
				goto cyttsp4_get_mt_touches_pr_tch;
			}
			input_mt_slot(md->input, t);
			input_mt_report_slot_state(md->input, MT_TOOL_FINGER,
					true);
			ids[t] = true;
		}

		/* all devices: position and pressure fields */
		for (j = 0; j <= CY_ABS_W_OST; j++) {
			sig = md->pdata->frmwrk->abs[((CY_ABS_X_OST + j) *
				CY_NUM_ABS_SET) + 0];
			if (sig != CY_IGNORE_VALUE)
				input_report_abs(md->input, sig,
					tch.abs[CY_TCH_X + j]);
		}
		if (si->si_ofs.tch_rec_size > CY_TMA1036_TCH_REC_SIZE) {
			/*
			 * TMA400 size and orientation fields:
			 * if pressure is non-zero and major touch
			 * signal is zero, then set major and minor touch
			 * signals to minimum non-zero value
			 */
			if (tch.abs[CY_TCH_P] > 0 && tch.abs[CY_TCH_MAJ] == 0)
				tch.abs[CY_TCH_MAJ] = tch.abs[CY_TCH_MIN] = 1;

			/* Get the extended touch fields */
			for (j = 0; j < CY_NUM_EXT_TCH_FIELDS; j++) {
				sig = md->pdata->frmwrk->abs
					[((CY_ABS_MAJ_OST + j) *
					CY_NUM_ABS_SET) + 0];
				if (sig != CY_IGNORE_VALUE)
					input_report_abs(md->input, sig,
						tch.abs[CY_TCH_MAJ + j]);
			}
		}

cyttsp4_get_mt_touches_pr_tch:
		if (si->si_ofs.tch_rec_size > CY_TMA1036_TCH_REC_SIZE)
			dev_dbg(dev,
				"%s: t=%d x=%d y=%d z=%d M=%d m=%d o=%d e=%d\n",
				__func__, t,
				tch.abs[CY_TCH_X],
				tch.abs[CY_TCH_Y],
				tch.abs[CY_TCH_P],
				tch.abs[CY_TCH_MAJ],
				tch.abs[CY_TCH_MIN],
				tch.abs[CY_TCH_OR],
				tch.abs[CY_TCH_E]);
		else
			dev_dbg(dev,
				"%s: t=%d x=%d y=%d z=%d e=%d\n", __func__,
				t,
				tch.abs[CY_TCH_X],
				tch.abs[CY_TCH_Y],
				tch.abs[CY_TCH_P],
				tch.abs[CY_TCH_E]);
	}

	cyttsp4_final_sync(md->input, si->si_ofs.tch_abs[CY_TCH_T].max, ids);

	md->num_prv_tch = num_cur_tch;

	return;
}

/* read xy_data for all current touches */
static int cyttsp4_xy_worker(struct cyttsp4 *cd)
{
	struct cyttsp4_mt_data *md = &cd->md;
	struct device *dev = &md->input->dev;
	struct cyttsp4_sysinfo *si = md->si;
	u8 num_cur_tch;
	u8 hst_mode;
	u8 rep_len;
	u8 rep_stat;
	u8 tt_stat;
	int rc = 0;

	/*
	 * Get event data from cyttsp4 device.
	 * The event data includes all data
	 * for all active touches.
	 * Event data also includes button data
	 */
	/*
	 * Use 2 reads:
	 * 1st read to get mode + button bytes + touch count (core)
	 * 2nd read (optional) to get touch 1 - touch n data
	 */
	hst_mode = si->xy_mode[CY_REG_BASE];
	rep_len = si->xy_mode[si->si_ofs.rep_ofs];
	rep_stat = si->xy_mode[si->si_ofs.rep_ofs + 1];
	tt_stat = si->xy_mode[si->si_ofs.tt_stat_ofs];
	dev_vdbg(dev, "%s: %s%02X %s%d %s%02X %s%02X\n", __func__,
		"hst_mode=", hst_mode, "rep_len=", rep_len,
		"rep_stat=", rep_stat, "tt_stat=", tt_stat);

	num_cur_tch = GET_NUM_TOUCHES(tt_stat);
	dev_vdbg(dev, "%s: num_cur_tch=%d\n", __func__, num_cur_tch);

	if (rep_len == 0 && num_cur_tch > 0) {
		dev_err(dev, "%s: report length error rep_len=%d num_tch=%d\n",
			__func__, rep_len, num_cur_tch);
		goto cyttsp4_xy_worker_exit;
	}

	/* read touches */
	if (num_cur_tch > 0) {
		rc = cyttsp4_adap_read(cd, si->si_ofs.tt_stat_ofs + 1,
				num_cur_tch * si->si_ofs.tch_rec_size,
				si->xy_data);
		if (rc < 0) {
			dev_err(dev, "%s: read fail on touch regs r=%d\n",
				__func__, rc);
			goto cyttsp4_xy_worker_exit;
		}
	}

	/* print xy data */
	cyttsp4_pr_buf(dev, cd->pr_buf, si->xy_data, num_cur_tch *
		si->si_ofs.tch_rec_size, "xy_data");

	/* check any error conditions */
	if (IS_BAD_PKT(rep_stat)) {
		dev_dbg(dev, "%s: Invalid buffer detected\n", __func__);
		rc = 0;
		goto cyttsp4_xy_worker_exit;
	}

	if (IS_LARGE_AREA(tt_stat))
		dev_dbg(dev, "%s: Large area detected\n", __func__);

	if (num_cur_tch > si->si_ofs.max_tchs) {
		dev_err(dev, "%s: too many tch; set to max tch (n=%d c=%Zd)\n",
				__func__, num_cur_tch, si->si_ofs.max_tchs);
		num_cur_tch = si->si_ofs.max_tchs;
	}

	/* extract xy_data for all currently reported touches */
	dev_vdbg(dev, "%s: extract data num_cur_tch=%d\n", __func__,
		num_cur_tch);
	if (num_cur_tch)
		cyttsp4_get_mt_touches(md, num_cur_tch);
	else
		cyttsp4_lift_all(md);

	rc = 0;

cyttsp4_xy_worker_exit:
	return rc;
}

static int cyttsp4_mt_attention(struct cyttsp4 *cd)
{
	struct device *dev = cd->dev;
	struct cyttsp4_mt_data *md = &cd->md;
	int rc = 0;

	if (!md->si)
		return 0;

	mutex_lock(&md->report_lock);
	if (!md->is_suspended) {
		/* core handles handshake */
		rc = cyttsp4_xy_worker(cd);
	} else {
		dev_vdbg(dev, "%s: Ignoring report while suspended\n",
			__func__);
	}
	mutex_unlock(&md->report_lock);
	if (rc < 0)
		dev_err(dev, "%s: xy_worker error r=%d\n", __func__, rc);

	return rc;
}

static irqreturn_t cyttsp4_irq(int irq, void *handle)
{
	struct cyttsp4 *cd = handle;
	struct device *dev = cd->dev;
	enum cyttsp4_mode cur_mode;
	u8 cmd_ofs = cd->sysinfo.si_ofs.cmd_ofs;
	u8 mode[3];
	int rc;

	/*
	 * Check whether this IRQ should be ignored (external)
	 * This should be the very first thing to check since
	 * ignore_irq may be set for a very short period of time
	 */
	if (atomic_read(&cd->ignore_irq)) {
		dev_vdbg(dev, "%s: Ignoring IRQ\n", __func__);
		return IRQ_HANDLED;
	}

	dev_dbg(dev, "%s int:0x%x\n", __func__, cd->int_status);

	mutex_lock(&cd->system_lock);

	/* Just to debug */
	if (cd->sleep_state == SS_SLEEP_ON || cd->sleep_state == SS_SLEEPING)
		dev_vdbg(dev, "%s: Received IRQ while in sleep\n", __func__);

	rc = cyttsp4_adap_read(cd, CY_REG_BASE, sizeof(mode), mode);
	if (rc) {
		dev_err(cd->dev, "%s: Fail read adapter r=%d\n", __func__, rc);
		goto cyttsp4_irq_exit;
	}
	dev_vdbg(dev, "%s mode[0-2]:0x%X 0x%X 0x%X\n", __func__,
			mode[0], mode[1], mode[2]);

	if (IS_BOOTLOADER(mode[0], mode[1])) {
		cur_mode = CY_MODE_BOOTLOADER;
		dev_vdbg(dev, "%s: bl running\n", __func__);
		if (cd->mode == CY_MODE_BOOTLOADER) {
			/* Signal bootloader heartbeat heard */
			wake_up(&cd->wait_q);
			goto cyttsp4_irq_exit;
		}

		/* switch to bootloader */
		dev_dbg(dev, "%s: restart switch to bl m=%d -> m=%d\n",
			__func__, cd->mode, cur_mode);

		/* catch operation->bl glitch */
		if (cd->mode != CY_MODE_UNKNOWN) {
			/* Incase startup_state do not let startup_() */
			cd->mode = CY_MODE_UNKNOWN;
			cyttsp4_queue_startup_(cd);
			goto cyttsp4_irq_exit;
		}

		/*
		 * do not wake thread on this switch since
		 * it is possible to get an early heartbeat
		 * prior to performing the reset
		 */
		cd->mode = cur_mode;

		goto cyttsp4_irq_exit;
	}

	switch (mode[0] & CY_HST_MODE) {
	case CY_HST_OPERATE:
		cur_mode = CY_MODE_OPERATIONAL;
		dev_vdbg(dev, "%s: operational\n", __func__);
		break;
	case CY_HST_CAT:
		cur_mode = CY_MODE_CAT;
		dev_vdbg(dev, "%s: CaT\n", __func__);
		break;
	case CY_HST_SYSINFO:
		cur_mode = CY_MODE_SYSINFO;
		dev_vdbg(dev, "%s: sysinfo\n", __func__);
		break;
	default:
		cur_mode = CY_MODE_UNKNOWN;
		dev_err(dev, "%s: unknown HST mode 0x%02X\n", __func__,
			mode[0]);
		break;
	}

	/* Check whether this IRQ should be ignored (internal) */
	if (cd->int_status & CY_INT_IGNORE) {
		dev_vdbg(dev, "%s: Ignoring IRQ\n", __func__);
		goto cyttsp4_irq_exit;
	}

	/* Check for wake up interrupt */
	if (cd->int_status & CY_INT_AWAKE) {
		cd->int_status &= ~CY_INT_AWAKE;
		wake_up(&cd->wait_q);
		dev_vdbg(dev, "%s: Received wake up interrupt\n", __func__);
		goto cyttsp4_irq_handshake;
	}

	/* Expecting mode change interrupt */
	if ((cd->int_status & CY_INT_MODE_CHANGE)
			&& (mode[0] & CY_HST_MODE_CHANGE) == 0) {
		cd->int_status &= ~CY_INT_MODE_CHANGE;
		dev_dbg(dev, "%s: finish mode switch m=%d -> m=%d\n",
				__func__, cd->mode, cur_mode);
		cd->mode = cur_mode;
		wake_up(&cd->wait_q);
		goto cyttsp4_irq_handshake;
	}

	/* compare current core mode to current device mode */
	dev_vdbg(dev, "%s: cd->mode=%d cur_mode=%d\n",
			__func__, cd->mode, cur_mode);
	if ((mode[0] & CY_HST_MODE_CHANGE) == 0 && cd->mode != cur_mode) {
		/* Unexpected mode change occurred */
		dev_err(dev, "%s %d->%d 0x%x\n", __func__, cd->mode,
				cur_mode, cd->int_status);
		dev_dbg(dev, "%s: Unexpected mode change, startup\n",
				__func__);
		cyttsp4_queue_startup_(cd);
		goto cyttsp4_irq_exit;
	}

	/* Expecting command complete interrupt */
	dev_vdbg(dev, "%s: command byte:0x%x\n", __func__, mode[cmd_ofs]);
	if ((cd->int_status & CY_INT_EXEC_CMD)
			&& mode[cmd_ofs] & CY_CMD_COMPLETE) {
		cd->int_status &= ~CY_INT_EXEC_CMD;
		dev_vdbg(dev, "%s: Received command complete interrupt\n",
				__func__);
		wake_up(&cd->wait_q);
		/*
		 * It is possible to receive a single interrupt for
		 * command complete and touch/button status report.
		 * Continue processing for a possible status report.
		 */
	}

	/* This should be status report, read status regs */
	if (cd->mode == CY_MODE_OPERATIONAL) {
		dev_vdbg(dev, "%s: Read status registers\n", __func__);
		rc = cyttsp4_load_status_regs(cd);
		if (rc < 0)
			dev_err(dev, "%s: fail read mode regs r=%d\n",
				__func__, rc);
	}

	cyttsp4_mt_attention(cd);

cyttsp4_irq_handshake:
	/* handshake the event */
	dev_vdbg(dev, "%s: Handshake mode=0x%02X r=%d\n",
			__func__, mode[0], rc);
	rc = cyttsp4_handshake(cd, mode[0]);
	if (rc < 0)
		dev_err(dev, "%s: Fail handshake mode=0x%02X r=%d\n",
				__func__, mode[0], rc);

	/*
	 * a non-zero udelay period is required for using
	 * IRQF_TRIGGER_LOW in order to delay until the
	 * device completes isr deassert
	 */
	udelay(cd->cpdata->level_irq_udelay);

cyttsp4_irq_exit:
	mutex_unlock(&cd->system_lock);
	return IRQ_HANDLED;
}

static void cyttsp4_start_wd_timer(struct cyttsp4 *cd)
{
	if (!CY_WATCHDOG_TIMEOUT)
		return;

	mod_timer(&cd->watchdog_timer, jiffies +
			msecs_to_jiffies(CY_WATCHDOG_TIMEOUT));
}

static void cyttsp4_stop_wd_timer(struct cyttsp4 *cd)
{
	if (!CY_WATCHDOG_TIMEOUT)
		return;

	/*
	 * Ensure we wait until the watchdog timer
	 * running on a different CPU finishes
	 */
	del_timer_sync(&cd->watchdog_timer);
	cancel_work_sync(&cd->watchdog_work);
	del_timer_sync(&cd->watchdog_timer);
}

static void cyttsp4_watchdog_timer(unsigned long handle)
{
	struct cyttsp4 *cd = (struct cyttsp4 *)handle;

	dev_vdbg(cd->dev, "%s: Watchdog timer triggered\n", __func__);

	if (!work_pending(&cd->watchdog_work))
		schedule_work(&cd->watchdog_work);

	return;
}

static int cyttsp4_request_exclusive(struct cyttsp4 *cd, void *ownptr,
		int timeout_ms)
{
	int t = msecs_to_jiffies(timeout_ms);
	bool with_timeout = (timeout_ms != 0);

	mutex_lock(&cd->system_lock);
	if (!cd->exclusive_dev && cd->exclusive_waits == 0) {
		cd->exclusive_dev = ownptr;
		goto exit;
	}

	cd->exclusive_waits++;
wait:
	mutex_unlock(&cd->system_lock);
	if (with_timeout) {
		t = wait_event_timeout(cd->wait_q, !cd->exclusive_dev, t);
		if (IS_TMO(t)) {
			dev_err(cd->dev, "%s: tmo waiting exclusive access\n",
				__func__);
			mutex_lock(&cd->system_lock);
			cd->exclusive_waits--;
			mutex_unlock(&cd->system_lock);
			return -ETIME;
		}
	} else {
		wait_event(cd->wait_q, !cd->exclusive_dev);
	}
	mutex_lock(&cd->system_lock);
	if (cd->exclusive_dev)
		goto wait;
	cd->exclusive_dev = ownptr;
	cd->exclusive_waits--;
exit:
	mutex_unlock(&cd->system_lock);

	return 0;
}

/*
 * returns error if was not owned
 */
static int cyttsp4_release_exclusive(struct cyttsp4 *cd, void *ownptr)
{
	mutex_lock(&cd->system_lock);
	if (cd->exclusive_dev != ownptr) {
		mutex_unlock(&cd->system_lock);
		return -EINVAL;
	}

	dev_vdbg(cd->dev, "%s: exclusive_dev %p freed\n",
		__func__, cd->exclusive_dev);
	cd->exclusive_dev = NULL;
	wake_up(&cd->wait_q);
	mutex_unlock(&cd->system_lock);
	return 0;
}

static int cyttsp4_wait_bl_heartbeat(struct cyttsp4 *cd)
{
	long t;
	int rc = 0;

	/* wait heartbeat */
	dev_vdbg(cd->dev, "%s: wait heartbeat...\n", __func__);
	t = wait_event_timeout(cd->wait_q, cd->mode == CY_MODE_BOOTLOADER,
			msecs_to_jiffies(CY_CORE_RESET_AND_WAIT_TIMEOUT));
	if (IS_TMO(t)) {
		dev_err(cd->dev, "%s: tmo waiting bl heartbeat cd->mode=%d\n",
			__func__, cd->mode);
		rc = -ETIME;
	}

	return rc;
}

static int cyttsp4_wait_sysinfo_mode(struct cyttsp4 *cd)
{
	long t;

	dev_vdbg(cd->dev, "%s: wait sysinfo...\n", __func__);

	t = wait_event_timeout(cd->wait_q, cd->mode == CY_MODE_SYSINFO,
			msecs_to_jiffies(CY_CORE_MODE_CHANGE_TIMEOUT));
	if (IS_TMO(t)) {
		dev_err(cd->dev, "%s: tmo waiting exit bl cd->mode=%d\n",
			__func__, cd->mode);
		mutex_lock(&cd->system_lock);
		cd->int_status &= ~CY_INT_MODE_CHANGE;
		mutex_unlock(&cd->system_lock);
		return -ETIME;
	}

	return 0;
}

static int cyttsp4_reset_and_wait(struct cyttsp4 *cd)
{
	int rc;

	/* reset hardware */
	mutex_lock(&cd->system_lock);
	dev_dbg(cd->dev, "%s: reset hw...\n", __func__);
	rc = cyttsp4_hw_reset(cd);
	cd->mode = CY_MODE_UNKNOWN;
	mutex_unlock(&cd->system_lock);
	if (rc < 0) {
		dev_err(cd->dev, "%s:Fail hw reset r=%d\n", __func__, rc);
		return rc;
	}

	return cyttsp4_wait_bl_heartbeat(cd);
}

/*
 * returns err if refused or timeout; block until mode change complete
 * bit is set (mode change interrupt)
 */
static int cyttsp4_set_mode(struct cyttsp4 *cd, int new_mode)
{
	u8 new_dev_mode;
	u8 mode;
	long t;
	int rc;

	switch (new_mode) {
	case CY_MODE_OPERATIONAL:
		new_dev_mode = CY_HST_OPERATE;
		break;
	case CY_MODE_SYSINFO:
		new_dev_mode = CY_HST_SYSINFO;
		break;
	case CY_MODE_CAT:
		new_dev_mode = CY_HST_CAT;
		break;
	default:
		dev_err(cd->dev, "%s: invalid mode: %02X(%d)\n",
			__func__, new_mode, new_mode);
		return -EINVAL;
	}

	/* change mode */
	dev_dbg(cd->dev, "%s: %s=%p new_dev_mode=%02X new_mode=%d\n",
			__func__, "have exclusive", cd->exclusive_dev,
			new_dev_mode, new_mode);

	mutex_lock(&cd->system_lock);
	rc = cyttsp4_adap_read(cd, CY_REG_BASE, sizeof(mode), &mode);
	if (rc < 0) {
		mutex_unlock(&cd->system_lock);
		dev_err(cd->dev, "%s: Fail read mode r=%d\n",
			__func__, rc);
		goto exit;
	}

	/* Clear device mode bits and set to new mode */
	mode &= ~CY_HST_MODE;
	mode |= new_dev_mode | CY_HST_MODE_CHANGE;

	cd->int_status |= CY_INT_MODE_CHANGE;
	rc = cyttsp4_adap_write(cd, CY_REG_BASE, sizeof(mode), &mode);
	mutex_unlock(&cd->system_lock);
	if (rc < 0) {
		dev_err(cd->dev, "%s: Fail write mode change r=%d\n",
				__func__, rc);
		goto exit;
	}

	/* wait for mode change done interrupt */
	t = wait_event_timeout(cd->wait_q,
			(cd->int_status & CY_INT_MODE_CHANGE) == 0,
			msecs_to_jiffies(CY_CORE_MODE_CHANGE_TIMEOUT));
	dev_dbg(cd->dev, "%s: back from wait t=%ld cd->mode=%d\n",
			__func__, t, cd->mode);

	if (IS_TMO(t)) {
		dev_err(cd->dev, "%s: %s\n", __func__,
				"tmo waiting mode change");
		mutex_lock(&cd->system_lock);
		cd->int_status &= ~CY_INT_MODE_CHANGE;
		mutex_unlock(&cd->system_lock);
		rc = -EINVAL;
	}

exit:
	return rc;
}

static void cyttsp4_watchdog_work(struct work_struct *work)
{
	struct cyttsp4 *cd =
		container_of(work, struct cyttsp4, watchdog_work);
	u8 *mode;
	int retval;

	mutex_lock(&cd->system_lock);
	retval = cyttsp4_load_status_regs(cd);
	if (retval < 0) {
		dev_err(cd->dev,
			"%s: failed to access device in watchdog timer r=%d\n",
			__func__, retval);
		cyttsp4_queue_startup_(cd);
		goto cyttsp4_timer_watchdog_exit_error;
	}
	mode = &cd->sysinfo.xy_mode[CY_REG_BASE];
	if (IS_BOOTLOADER(mode[0], mode[1])) {
		dev_err(cd->dev,
			"%s: device found in bootloader mode when operational mode\n",
			__func__);
		cyttsp4_queue_startup_(cd);
		goto cyttsp4_timer_watchdog_exit_error;
	}

	cyttsp4_start_wd_timer(cd);
cyttsp4_timer_watchdog_exit_error:
	mutex_unlock(&cd->system_lock);
	return;
}

static int cyttsp4_core_sleep_(struct cyttsp4 *cd)
{
	enum cyttsp4_sleep_state ss = SS_SLEEP_ON;
	enum cyttsp4_int_state int_status = CY_INT_IGNORE;
	int rc = 0;
	u8 mode[2];

	/* Already in sleep mode? */
	mutex_lock(&cd->system_lock);
	if (cd->sleep_state == SS_SLEEP_ON) {
		mutex_unlock(&cd->system_lock);
		return 0;
	}
	cd->sleep_state = SS_SLEEPING;
	mutex_unlock(&cd->system_lock);

	cyttsp4_stop_wd_timer(cd);

	/* Wait until currently running IRQ handler exits and disable IRQ */
	disable_irq(cd->irq);

	dev_vdbg(cd->dev, "%s: write DEEP SLEEP...\n", __func__);
	mutex_lock(&cd->system_lock);
	rc = cyttsp4_adap_read(cd, CY_REG_BASE, sizeof(mode), &mode);
	if (rc) {
		mutex_unlock(&cd->system_lock);
		dev_err(cd->dev, "%s: Fail read adapter r=%d\n", __func__, rc);
		goto error;
	}

	if (IS_BOOTLOADER(mode[0], mode[1])) {
		mutex_unlock(&cd->system_lock);
		dev_err(cd->dev, "%s: Device in BOOTLADER mode.\n", __func__);
		rc = -EINVAL;
		goto error;
	}

	mode[0] |= CY_HST_SLEEP;
	rc = cyttsp4_adap_write(cd, CY_REG_BASE, sizeof(mode[0]), &mode[0]);
	mutex_unlock(&cd->system_lock);
	if (rc) {
		dev_err(cd->dev, "%s: Fail write adapter r=%d\n", __func__, rc);
		goto error;
	}
	dev_vdbg(cd->dev, "%s: write DEEP SLEEP succeeded\n", __func__);

	if (cd->cpdata->power) {
		dev_dbg(cd->dev, "%s: Power down HW\n", __func__);
		rc = cd->cpdata->power(cd->cpdata, 0, cd->dev, &cd->ignore_irq);
	} else {
		dev_dbg(cd->dev, "%s: No power function\n", __func__);
		rc = 0;
	}
	if (rc < 0) {
		dev_err(cd->dev, "%s: HW Power down fails r=%d\n",
				__func__, rc);
		goto error;
	}

	/* Give time to FW to sleep */
	msleep(50);

	goto exit;

error:
	ss = SS_SLEEP_OFF;
	int_status = CY_INT_NONE;
	cyttsp4_start_wd_timer(cd);

exit:
	mutex_lock(&cd->system_lock);
	cd->sleep_state = ss;
	cd->int_status |= int_status;
	mutex_unlock(&cd->system_lock);
	enable_irq(cd->irq);
	return rc;
}

static int cyttsp4_startup_(struct cyttsp4 *cd)
{
	int retry = CY_CORE_STARTUP_RETRY_COUNT;
	int rc;

	cyttsp4_stop_wd_timer(cd);

reset:
	if (retry != CY_CORE_STARTUP_RETRY_COUNT)
		dev_dbg(cd->dev, "%s: Retry %d\n", __func__,
			CY_CORE_STARTUP_RETRY_COUNT - retry);

	/* reset hardware and wait for heartbeat */
	rc = cyttsp4_reset_and_wait(cd);
	if (rc < 0) {
		dev_err(cd->dev, "%s: Error on h/w reset r=%d\n", __func__, rc);
		if (retry--)
			goto reset;
		goto exit;
	}

	/* exit bl into sysinfo mode */
	dev_vdbg(cd->dev, "%s: write exit ldr...\n", __func__);
	mutex_lock(&cd->system_lock);
	cd->int_status &= ~CY_INT_IGNORE;
	cd->int_status |= CY_INT_MODE_CHANGE;

	rc = cyttsp4_adap_write(cd, CY_REG_BASE, sizeof(ldr_exit),
			(u8 *)ldr_exit);
	mutex_unlock(&cd->system_lock);
	if (rc < 0) {
		dev_err(cd->dev, "%s: Fail write r=%d\n", __func__, rc);
		if (retry--)
			goto reset;
		goto exit;
	}

	rc = cyttsp4_wait_sysinfo_mode(cd);
	if (rc < 0) {
		u8 buf[sizeof(ldr_err_app)];
		int rc1;

		/* Check for invalid/corrupted touch application */
		rc1 = cyttsp4_adap_read(cd, CY_REG_BASE, sizeof(ldr_err_app),
				buf);
		if (rc1) {
			dev_err(cd->dev, "%s: Fail read r=%d\n", __func__, rc1);
		} else if (!memcmp(buf, ldr_err_app, sizeof(ldr_err_app))) {
			dev_err(cd->dev, "%s: Error launching touch application\n",
				__func__);
			mutex_lock(&cd->system_lock);
			cd->invalid_touch_app = true;
			mutex_unlock(&cd->system_lock);
			goto exit_no_wd;
		}

		if (retry--)
			goto reset;
		goto exit;
	}

	mutex_lock(&cd->system_lock);
	cd->invalid_touch_app = false;
	mutex_unlock(&cd->system_lock);

	/* read sysinfo data */
	dev_vdbg(cd->dev, "%s: get sysinfo regs..\n", __func__);
	rc = cyttsp4_get_sysinfo_regs(cd);
	if (rc < 0) {
		dev_err(cd->dev, "%s: failed to get sysinfo regs rc=%d\n",
			__func__, rc);
		if (retry--)
			goto reset;
		goto exit;
	}

	rc = cyttsp4_set_mode(cd, CY_MODE_OPERATIONAL);
	if (rc < 0) {
		dev_err(cd->dev, "%s: failed to set mode to operational rc=%d\n",
			__func__, rc);
		if (retry--)
			goto reset;
		goto exit;
	}

	cyttsp4_lift_all(&cd->md);

	/* restore to sleep if was suspended */
	mutex_lock(&cd->system_lock);
	if (cd->sleep_state == SS_SLEEP_ON) {
		cd->sleep_state = SS_SLEEP_OFF;
		mutex_unlock(&cd->system_lock);
		cyttsp4_core_sleep_(cd);
		goto exit_no_wd;
	}
	mutex_unlock(&cd->system_lock);

exit:
	cyttsp4_start_wd_timer(cd);
exit_no_wd:
	return rc;
}

static int cyttsp4_startup(struct cyttsp4 *cd)
{
	int rc;

	mutex_lock(&cd->system_lock);
	cd->startup_state = STARTUP_RUNNING;
	mutex_unlock(&cd->system_lock);

	rc = cyttsp4_request_exclusive(cd, cd->dev,
			CY_CORE_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail get exclusive ex=%p own=%p\n",
				__func__, cd->exclusive_dev, cd->dev);
		goto exit;
	}

	rc = cyttsp4_startup_(cd);

	if (cyttsp4_release_exclusive(cd, cd->dev) < 0)
		/* Don't return fail code, mode is already changed. */
		dev_err(cd->dev, "%s: fail to release exclusive\n", __func__);
	else
		dev_vdbg(cd->dev, "%s: pass release exclusive\n", __func__);

exit:
	mutex_lock(&cd->system_lock);
	cd->startup_state = STARTUP_NONE;
	mutex_unlock(&cd->system_lock);

	/* Wake the waiters for end of startup */
	wake_up(&cd->wait_q);

	return rc;
}

static void cyttsp4_startup_work_function(struct work_struct *work)
{
	struct cyttsp4 *cd =  container_of(work, struct cyttsp4, startup_work);
	int rc;

	rc = cyttsp4_startup(cd);
	if (rc < 0)
		dev_err(cd->dev, "%s: Fail queued startup r=%d\n",
			__func__, rc);
}

static void cyttsp4_free_si_ptrs(struct cyttsp4 *cd)
{
	struct cyttsp4_sysinfo *si = &cd->sysinfo;

	if (!si)
		return;

	kfree(si->si_ptrs.cydata);
	kfree(si->si_ptrs.test);
	kfree(si->si_ptrs.pcfg);
	kfree(si->si_ptrs.opcfg);
	kfree(si->si_ptrs.ddata);
	kfree(si->si_ptrs.mdata);
	kfree(si->btn);
	kfree(si->xy_mode);
	kfree(si->xy_data);
	kfree(si->btn_rec_data);
}

#if defined(CONFIG_PM_SLEEP) || defined(CONFIG_PM_RUNTIME)
static int cyttsp4_core_sleep(struct cyttsp4 *cd)
{
	int rc;

	rc = cyttsp4_request_exclusive(cd, cd->dev,
			CY_CORE_SLEEP_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail get exclusive ex=%p own=%p\n",
				__func__, cd->exclusive_dev, cd->dev);
		return 0;
	}

	rc = cyttsp4_core_sleep_(cd);

	if (cyttsp4_release_exclusive(cd, cd->dev) < 0)
		dev_err(cd->dev, "%s: fail to release exclusive\n", __func__);
	else
		dev_vdbg(cd->dev, "%s: pass release exclusive\n", __func__);

	return rc;
}

static int cyttsp4_core_wake_(struct cyttsp4 *cd)
{
	struct device *dev = cd->dev;
	int rc;
	u8 mode;
	int t;

	/* Already woken? */
	mutex_lock(&cd->system_lock);
	if (cd->sleep_state == SS_SLEEP_OFF) {
		mutex_unlock(&cd->system_lock);
		return 0;
	}
	cd->int_status &= ~CY_INT_IGNORE;
	cd->int_status |= CY_INT_AWAKE;
	cd->sleep_state = SS_WAKING;

	if (cd->cpdata->power) {
		dev_dbg(dev, "%s: Power up HW\n", __func__);
		rc = cd->cpdata->power(cd->cpdata, 1, dev, &cd->ignore_irq);
	} else {
		dev_dbg(dev, "%s: No power function\n", __func__);
		rc = -ENOSYS;
	}
	if (rc < 0) {
		dev_err(dev, "%s: HW Power up fails r=%d\n",
				__func__, rc);

		/* Initiate a read transaction to wake up */
		cyttsp4_adap_read(cd, CY_REG_BASE, sizeof(mode), &mode);
	} else
		dev_vdbg(cd->dev, "%s: HW power up succeeds\n",
			__func__);
	mutex_unlock(&cd->system_lock);

	t = wait_event_timeout(cd->wait_q,
			(cd->int_status & CY_INT_AWAKE) == 0,
			msecs_to_jiffies(CY_CORE_WAKEUP_TIMEOUT));
	if (IS_TMO(t)) {
		dev_err(dev, "%s: TMO waiting for wakeup\n", __func__);
		mutex_lock(&cd->system_lock);
		cd->int_status &= ~CY_INT_AWAKE;
		/* Try starting up */
		cyttsp4_queue_startup_(cd);
		mutex_unlock(&cd->system_lock);
	}

	mutex_lock(&cd->system_lock);
	cd->sleep_state = SS_SLEEP_OFF;
	mutex_unlock(&cd->system_lock);

	cyttsp4_start_wd_timer(cd);

	return 0;
}

static int cyttsp4_core_wake(struct cyttsp4 *cd)
{
	int rc;

	rc = cyttsp4_request_exclusive(cd, cd->dev,
			CY_CORE_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(cd->dev, "%s: fail get exclusive ex=%p own=%p\n",
				__func__, cd->exclusive_dev, cd->dev);
		return 0;
	}

	rc = cyttsp4_core_wake_(cd);

	if (cyttsp4_release_exclusive(cd, cd->dev) < 0)
		dev_err(cd->dev, "%s: fail to release exclusive\n", __func__);
	else
		dev_vdbg(cd->dev, "%s: pass release exclusive\n", __func__);

	return rc;
}

static int cyttsp4_core_suspend(struct device *dev)
{
	struct cyttsp4 *cd = dev_get_drvdata(dev);
	struct cyttsp4_mt_data *md = &cd->md;
	int rc;

	md->is_suspended = true;

	rc = cyttsp4_core_sleep(cd);
	if (rc < 0) {
		dev_err(dev, "%s: Error on sleep\n", __func__);
		return -EAGAIN;
	}
	return 0;
}

static int cyttsp4_core_resume(struct device *dev)
{
	struct cyttsp4 *cd = dev_get_drvdata(dev);
	struct cyttsp4_mt_data *md = &cd->md;
	int rc;

	md->is_suspended = false;

	rc = cyttsp4_core_wake(cd);
	if (rc < 0) {
		dev_err(dev, "%s: Error on wake\n", __func__);
		return -EAGAIN;
	}

	return 0;
}
#endif

const struct dev_pm_ops cyttsp4_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cyttsp4_core_suspend, cyttsp4_core_resume)
	SET_RUNTIME_PM_OPS(cyttsp4_core_suspend, cyttsp4_core_resume, NULL)
};
EXPORT_SYMBOL_GPL(cyttsp4_pm_ops);

static int cyttsp4_mt_open(struct input_dev *input)
{
	pm_runtime_get(input->dev.parent);
	return 0;
}

static void cyttsp4_mt_close(struct input_dev *input)
{
	struct cyttsp4_mt_data *md = input_get_drvdata(input);
	mutex_lock(&md->report_lock);
	if (!md->is_suspended)
		pm_runtime_put(input->dev.parent);
	mutex_unlock(&md->report_lock);
}


static int cyttsp4_setup_input_device(struct cyttsp4 *cd)
{
	struct device *dev = cd->dev;
	struct cyttsp4_mt_data *md = &cd->md;
	int signal = CY_IGNORE_VALUE;
	int max_x, max_y, max_p, min, max;
	int max_x_tmp, max_y_tmp;
	int i;
	int rc;

	dev_vdbg(dev, "%s: Initialize event signals\n", __func__);
	__set_bit(EV_ABS, md->input->evbit);
	__set_bit(EV_REL, md->input->evbit);
	__set_bit(EV_KEY, md->input->evbit);

	max_x_tmp = md->si->si_ofs.max_x;
	max_y_tmp = md->si->si_ofs.max_y;

	/* get maximum values from the sysinfo data */
	if (md->pdata->flags & CY_FLAG_FLIP) {
		max_x = max_y_tmp - 1;
		max_y = max_x_tmp - 1;
	} else {
		max_x = max_x_tmp - 1;
		max_y = max_y_tmp - 1;
	}
	max_p = md->si->si_ofs.max_p;

	/* set event signal capabilities */
	for (i = 0; i < (md->pdata->frmwrk->size / CY_NUM_ABS_SET); i++) {
		signal = md->pdata->frmwrk->abs
			[(i * CY_NUM_ABS_SET) + CY_SIGNAL_OST];
		if (signal != CY_IGNORE_VALUE) {
			__set_bit(signal, md->input->absbit);
			min = md->pdata->frmwrk->abs
				[(i * CY_NUM_ABS_SET) + CY_MIN_OST];
			max = md->pdata->frmwrk->abs
				[(i * CY_NUM_ABS_SET) + CY_MAX_OST];
			if (i == CY_ABS_ID_OST) {
				/* shift track ids down to start at 0 */
				max = max - min;
				min = min - min;
			} else if (i == CY_ABS_X_OST)
				max = max_x;
			else if (i == CY_ABS_Y_OST)
				max = max_y;
			else if (i == CY_ABS_P_OST)
				max = max_p;
			input_set_abs_params(md->input, signal, min, max,
				md->pdata->frmwrk->abs
				[(i * CY_NUM_ABS_SET) + CY_FUZZ_OST],
				md->pdata->frmwrk->abs
				[(i * CY_NUM_ABS_SET) + CY_FLAT_OST]);
			dev_dbg(dev, "%s: register signal=%02X min=%d max=%d\n",
				__func__, signal, min, max);
			if ((i == CY_ABS_ID_OST) &&
				(md->si->si_ofs.tch_rec_size <
				CY_TMA4XX_TCH_REC_SIZE))
				break;
		}
	}

	input_mt_init_slots(md->input, md->si->si_ofs.tch_abs[CY_TCH_T].max,
			INPUT_MT_DIRECT);
	rc = input_register_device(md->input);
	if (rc < 0)
		dev_err(dev, "%s: Error, failed register input device r=%d\n",
			__func__, rc);
	return rc;
}

static int cyttsp4_mt_probe(struct cyttsp4 *cd)
{
	struct device *dev = cd->dev;
	struct cyttsp4_mt_data *md = &cd->md;
	struct cyttsp4_mt_platform_data *pdata = cd->pdata->mt_pdata;
	int rc = 0;

	mutex_init(&md->report_lock);
	md->pdata = pdata;
	/* Create the input device and register it. */
	dev_vdbg(dev, "%s: Create the input device and register it\n",
		__func__);
	md->input = input_allocate_device();
	if (md->input == NULL) {
		dev_err(dev, "%s: Error, failed to allocate input device\n",
			__func__);
		rc = -ENOSYS;
		goto error_alloc_failed;
	}

	md->input->name = pdata->inp_dev_name;
	scnprintf(md->phys, sizeof(md->phys)-1, "%s", dev_name(dev));
	md->input->phys = md->phys;
	md->input->id.bustype = cd->bus_ops->bustype;
	md->input->dev.parent = dev;
	md->input->open = cyttsp4_mt_open;
	md->input->close = cyttsp4_mt_close;
	input_set_drvdata(md->input, md);

	/* get sysinfo */
	md->si = &cd->sysinfo;
	if (!md->si) {
		dev_err(dev, "%s: Fail get sysinfo pointer from core p=%p\n",
			__func__, md->si);
		goto error_get_sysinfo;
	}

	rc = cyttsp4_setup_input_device(cd);
	if (rc)
		goto error_init_input;

	return 0;

error_init_input:
	input_free_device(md->input);
error_get_sysinfo:
	input_set_drvdata(md->input, NULL);
error_alloc_failed:
	dev_err(dev, "%s failed.\n", __func__);
	return rc;
}

struct cyttsp4 *cyttsp4_probe(const struct cyttsp4_bus_ops *ops,
		struct device *dev, u16 irq, size_t xfer_buf_size)
{
	struct cyttsp4 *cd;
	struct cyttsp4_platform_data *pdata = dev_get_platdata(dev);
	unsigned long irq_flags;
	int rc = 0;

	if (!pdata || !pdata->core_pdata || !pdata->mt_pdata) {
		dev_err(dev, "%s: Missing platform data\n", __func__);
		rc = -ENODEV;
		goto error_no_pdata;
	}

	cd = kzalloc(sizeof(*cd), GFP_KERNEL);
	if (!cd) {
		dev_err(dev, "%s: Error, kzalloc\n", __func__);
		rc = -ENOMEM;
		goto error_alloc_data;
	}

	cd->xfer_buf = kzalloc(xfer_buf_size, GFP_KERNEL);
	if (!cd->xfer_buf) {
		dev_err(dev, "%s: Error, kzalloc\n", __func__);
		rc = -ENOMEM;
		goto error_free_cd;
	}

	/* Initialize device info */
	cd->dev = dev;
	cd->pdata = pdata;
	cd->cpdata = pdata->core_pdata;
	cd->bus_ops = ops;

	/* Initialize mutexes and spinlocks */
	mutex_init(&cd->system_lock);
	mutex_init(&cd->adap_lock);

	/* Initialize wait queue */
	init_waitqueue_head(&cd->wait_q);

	/* Initialize works */
	INIT_WORK(&cd->startup_work, cyttsp4_startup_work_function);
	INIT_WORK(&cd->watchdog_work, cyttsp4_watchdog_work);

	/* Initialize IRQ */
	cd->irq = gpio_to_irq(cd->cpdata->irq_gpio);
	if (cd->irq < 0) {
		rc = -EINVAL;
		goto error_free_xfer;
	}

	dev_set_drvdata(dev, cd);

	/* Call platform init function */
	if (cd->cpdata->init) {
		dev_dbg(cd->dev, "%s: Init HW\n", __func__);
		rc = cd->cpdata->init(cd->cpdata, 1, cd->dev);
	} else {
		dev_dbg(cd->dev, "%s: No HW INIT function\n", __func__);
		rc = 0;
	}
	if (rc < 0)
		dev_err(cd->dev, "%s: HW Init fail r=%d\n", __func__, rc);

	dev_dbg(dev, "%s: initialize threaded irq=%d\n", __func__, cd->irq);
	if (cd->cpdata->level_irq_udelay > 0)
		/* use level triggered interrupts */
		irq_flags = IRQF_TRIGGER_LOW | IRQF_ONESHOT;
	else
		/* use edge triggered interrupts */
		irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;

	rc = request_threaded_irq(cd->irq, NULL, cyttsp4_irq, irq_flags,
		dev_name(dev), cd);
	if (rc < 0) {
		dev_err(dev, "%s: Error, could not request irq\n", __func__);
		goto error_request_irq;
	}

	/* Setup watchdog timer */
	setup_timer(&cd->watchdog_timer, cyttsp4_watchdog_timer,
		(unsigned long)cd);

	/*
	 * call startup directly to ensure that the device
	 * is tested before leaving the probe
	 */
	rc = cyttsp4_startup(cd);

	/* Do not fail probe if startup fails but the device is detected */
	if (rc < 0 && cd->mode == CY_MODE_UNKNOWN) {
		dev_err(cd->dev, "%s: Fail initial startup r=%d\n",
			__func__, rc);
		goto error_startup;
	}

	rc = cyttsp4_mt_probe(cd);
	if (rc < 0) {
		dev_err(dev, "%s: Error, fail mt probe\n", __func__);
		goto error_startup;
	}

	pm_runtime_enable(dev);

	return cd;

error_startup:
	cancel_work_sync(&cd->startup_work);
	cyttsp4_stop_wd_timer(cd);
	pm_runtime_disable(dev);
	cyttsp4_free_si_ptrs(cd);
	free_irq(cd->irq, cd);
error_request_irq:
	if (cd->cpdata->init)
		cd->cpdata->init(cd->cpdata, 0, dev);
error_free_xfer:
	kfree(cd->xfer_buf);
error_free_cd:
	kfree(cd);
error_alloc_data:
error_no_pdata:
	dev_err(dev, "%s failed.\n", __func__);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(cyttsp4_probe);

static void cyttsp4_mt_release(struct cyttsp4_mt_data *md)
{
	input_unregister_device(md->input);
	input_set_drvdata(md->input, NULL);
}

int cyttsp4_remove(struct cyttsp4 *cd)
{
	struct device *dev = cd->dev;

	cyttsp4_mt_release(&cd->md);

	/*
	 * Suspend the device before freeing the startup_work and stopping
	 * the watchdog since sleep function restarts watchdog on failure
	 */
	pm_runtime_suspend(dev);
	pm_runtime_disable(dev);

	cancel_work_sync(&cd->startup_work);

	cyttsp4_stop_wd_timer(cd);

	free_irq(cd->irq, cd);
	if (cd->cpdata->init)
		cd->cpdata->init(cd->cpdata, 0, dev);
	cyttsp4_free_si_ptrs(cd);
	kfree(cd);
	return 0;
}
EXPORT_SYMBOL_GPL(cyttsp4_remove);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cypress TrueTouch(R) Standard touchscreen core driver");
MODULE_AUTHOR("Cypress");
