/*
 * Copyright (c) 2013 Intel Corporation. All rights reserved.
 * Copyright (c) 2006 - 2012 QLogic Corporation. All rights reserved.
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
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
 *        disclaimer in the documentation and/or other materials
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
/*
 * This file contains all of the code that is specific to the SerDes
 * on the QLogic_IB 7220 chip.
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/firmware.h>

#include "qib.h"
#include "qib_7220.h"

#define SD7220_FW_NAME "qlogic/sd7220.fw"
MODULE_FIRMWARE(SD7220_FW_NAME);

/*
 * Same as in qib_iba7220.c, but just the registers needed here.
 * Could move whole set to qib_7220.h, but decided better to keep
 * local.
 */
#define KREG_IDX(regname) (QIB_7220_##regname##_OFFS / sizeof(u64))
#define kr_hwerrclear KREG_IDX(HwErrClear)
#define kr_hwerrmask KREG_IDX(HwErrMask)
#define kr_hwerrstatus KREG_IDX(HwErrStatus)
#define kr_ibcstatus KREG_IDX(IBCStatus)
#define kr_ibserdesctrl KREG_IDX(IBSerDesCtrl)
#define kr_scratch KREG_IDX(Scratch)
#define kr_xgxs_cfg KREG_IDX(XGXSCfg)
/* these are used only here, not in qib_iba7220.c */
#define kr_ibsd_epb_access_ctrl KREG_IDX(ibsd_epb_access_ctrl)
#define kr_ibsd_epb_transaction_reg KREG_IDX(ibsd_epb_transaction_reg)
#define kr_pciesd_epb_transaction_reg KREG_IDX(pciesd_epb_transaction_reg)
#define kr_pciesd_epb_access_ctrl KREG_IDX(pciesd_epb_access_ctrl)
#define kr_serdes_ddsrxeq0 KREG_IDX(SerDes_DDSRXEQ0)

/*
 * The IBSerDesMappTable is a memory that holds values to be stored in
 * various SerDes registers by IBC.
 */
#define kr_serdes_maptable KREG_IDX(IBSerDesMappTable)

/*
 * Below used for sdnum parameter, selecting one of the two sections
 * used for PCIe, or the single SerDes used for IB.
 */
#define PCIE_SERDES0 0
#define PCIE_SERDES1 1

/*
 * The EPB requires addressing in a particular form. EPB_LOC() is intended
 * to make #definitions a little more readable.
 */
#define EPB_ADDR_SHF 8
#define EPB_LOC(chn, elt, reg) \
	(((elt & 0xf) | ((chn & 7) << 4) | ((reg & 0x3f) << 9)) << \
	 EPB_ADDR_SHF)
#define EPB_IB_QUAD0_CS_SHF (25)
#define EPB_IB_QUAD0_CS (1U <<  EPB_IB_QUAD0_CS_SHF)
#define EPB_IB_UC_CS_SHF (26)
#define EPB_PCIE_UC_CS_SHF (27)
#define EPB_GLOBAL_WR (1U << (EPB_ADDR_SHF + 8))

/* Forward declarations. */
static int qib_sd7220_reg_mod(struct qib_devdata *dd, int sdnum, u32 loc,
			      u32 data, u32 mask);
static int ibsd_mod_allchnls(struct qib_devdata *dd, int loc, int val,
			     int mask);
static int qib_sd_trimdone_poll(struct qib_devdata *dd);
static void qib_sd_trimdone_monitor(struct qib_devdata *dd, const char *where);
static int qib_sd_setvals(struct qib_devdata *dd);
static int qib_sd_early(struct qib_devdata *dd);
static int qib_sd_dactrim(struct qib_devdata *dd);
static int qib_internal_presets(struct qib_devdata *dd);
/* Tweak the register (CMUCTRL5) that contains the TRIMSELF controls */
static int qib_sd_trimself(struct qib_devdata *dd, int val);
static int epb_access(struct qib_devdata *dd, int sdnum, int claim);
static int qib_sd7220_ib_load(struct qib_devdata *dd,
			      const struct firmware *fw);
static int qib_sd7220_ib_vfy(struct qib_devdata *dd,
			     const struct firmware *fw);

/*
 * Below keeps track of whether the "once per power-on" initialization has
 * been done, because uC code Version 1.32.17 or higher allows the uC to
 * be reset at will, and Automatic Equalization may require it. So the
 * state of the reset "pin", is no longer valid. Instead, we check for the
 * actual uC code having been loaded.
 */
static int qib_ibsd_ucode_loaded(struct qib_pportdata *ppd,
				 const struct firmware *fw)
{
	struct qib_devdata *dd = ppd->dd;

	if (!dd->cspec->serdes_first_init_done &&
	    qib_sd7220_ib_vfy(dd, fw) > 0)
		dd->cspec->serdes_first_init_done = 1;
	return dd->cspec->serdes_first_init_done;
}

/* repeat #define for local use. "Real" #define is in qib_iba7220.c */
#define QLOGIC_IB_HWE_IB_UC_MEMORYPARITYERR      0x0000004000000000ULL
#define IB_MPREG5 (EPB_LOC(6, 0, 0xE) | (1L << EPB_IB_UC_CS_SHF))
#define IB_MPREG6 (EPB_LOC(6, 0, 0xF) | (1U << EPB_IB_UC_CS_SHF))
#define UC_PAR_CLR_D 8
#define UC_PAR_CLR_M 0xC
#define IB_CTRL2(chn) (EPB_LOC(chn, 7, 3) | EPB_IB_QUAD0_CS)
#define START_EQ1(chan) EPB_LOC(chan, 7, 0x27)

void qib_sd7220_clr_ibpar(struct qib_devdata *dd)
{
	int ret;

	/* clear, then re-enable parity errs */
	ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES, IB_MPREG6,
		UC_PAR_CLR_D, UC_PAR_CLR_M);
	if (ret < 0) {
		qib_dev_err(dd, "Failed clearing IBSerDes Parity err\n");
		goto bail;
	}
	ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES, IB_MPREG6, 0,
		UC_PAR_CLR_M);

	qib_read_kreg32(dd, kr_scratch);
	udelay(4);
	qib_write_kreg(dd, kr_hwerrclear,
		QLOGIC_IB_HWE_IB_UC_MEMORYPARITYERR);
	qib_read_kreg32(dd, kr_scratch);
bail:
	return;
}

/*
 * After a reset or other unusual event, the epb interface may need
 * to be re-synchronized, between the host and the uC.
 * returns <0 for failure to resync within IBSD_RESYNC_TRIES (not expected)
 */
#define IBSD_RESYNC_TRIES 3
#define IB_PGUDP(chn) (EPB_LOC((chn), 2, 1) | EPB_IB_QUAD0_CS)
#define IB_CMUDONE(chn) (EPB_LOC((chn), 7, 0xF) | EPB_IB_QUAD0_CS)

static int qib_resync_ibepb(struct qib_devdata *dd)
{
	int ret, pat, tries, chn;
	u32 loc;

	ret = -1;
	chn = 0;
	for (tries = 0; tries < (4 * IBSD_RESYNC_TRIES); ++tries) {
		loc = IB_PGUDP(chn);
		ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES, loc, 0, 0);
		if (ret < 0) {
			qib_dev_err(dd, "Failed read in resync\n");
			continue;
		}
		if (ret != 0xF0 && ret != 0x55 && tries == 0)
			qib_dev_err(dd, "unexpected pattern in resync\n");
		pat = ret ^ 0xA5; /* alternate F0 and 55 */
		ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES, loc, pat, 0xFF);
		if (ret < 0) {
			qib_dev_err(dd, "Failed write in resync\n");
			continue;
		}
		ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES, loc, 0, 0);
		if (ret < 0) {
			qib_dev_err(dd, "Failed re-read in resync\n");
			continue;
		}
		if (ret != pat) {
			qib_dev_err(dd, "Failed compare1 in resync\n");
			continue;
		}
		loc = IB_CMUDONE(chn);
		ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES, loc, 0, 0);
		if (ret < 0) {
			qib_dev_err(dd, "Failed CMUDONE rd in resync\n");
			continue;
		}
		if ((ret & 0x70) != ((chn << 4) | 0x40)) {
			qib_dev_err(dd, "Bad CMUDONE value %02X, chn %d\n",
				    ret, chn);
			continue;
		}
		if (++chn == 4)
			break;  /* Success */
	}
	return (ret > 0) ? 0 : ret;
}

/*
 * Localize the stuff that should be done to change IB uC reset
 * returns <0 for errors.
 */
static int qib_ibsd_reset(struct qib_devdata *dd, int assert_rst)
{
	u64 rst_val;
	int ret = 0;
	unsigned long flags;

	rst_val = qib_read_kreg64(dd, kr_ibserdesctrl);
	if (assert_rst) {
		/*
		 * Vendor recommends "interrupting" uC before reset, to
		 * minimize possible glitches.
		 */
		spin_lock_irqsave(&dd->cspec->sdepb_lock, flags);
		epb_access(dd, IB_7220_SERDES, 1);
		rst_val |= 1ULL;
		/* Squelch possible parity error from _asserting_ reset */
		qib_write_kreg(dd, kr_hwerrmask,
			       dd->cspec->hwerrmask &
			       ~QLOGIC_IB_HWE_IB_UC_MEMORYPARITYERR);
		qib_write_kreg(dd, kr_ibserdesctrl, rst_val);
		/* flush write, delay to ensure it took effect */
		qib_read_kreg32(dd, kr_scratch);
		udelay(2);
		/* once it's reset, can remove interrupt */
		epb_access(dd, IB_7220_SERDES, -1);
		spin_unlock_irqrestore(&dd->cspec->sdepb_lock, flags);
	} else {
		/*
		 * Before we de-assert reset, we need to deal with
		 * possible glitch on the Parity-error line.
		 * Suppress it around the reset, both in chip-level
		 * hwerrmask and in IB uC control reg. uC will allow
		 * it again during startup.
		 */
		u64 val;

		rst_val &= ~(1ULL);
		qib_write_kreg(dd, kr_hwerrmask,
			       dd->cspec->hwerrmask &
			       ~QLOGIC_IB_HWE_IB_UC_MEMORYPARITYERR);

		ret = qib_resync_ibepb(dd);
		if (ret < 0)
			qib_dev_err(dd, "unable to re-sync IB EPB\n");

		/* set uC control regs to suppress parity errs */
		ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES, IB_MPREG5, 1, 1);
		if (ret < 0)
			goto bail;
		/* IB uC code past Version 1.32.17 allow suppression of wdog */
		ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES, IB_MPREG6, 0x80,
			0x80);
		if (ret < 0) {
			qib_dev_err(dd, "Failed to set WDOG disable\n");
			goto bail;
		}
		qib_write_kreg(dd, kr_ibserdesctrl, rst_val);
		/* flush write, delay for startup */
		qib_read_kreg32(dd, kr_scratch);
		udelay(1);
		/* clear, then re-enable parity errs */
		qib_sd7220_clr_ibpar(dd);
		val = qib_read_kreg64(dd, kr_hwerrstatus);
		if (val & QLOGIC_IB_HWE_IB_UC_MEMORYPARITYERR) {
			qib_dev_err(dd, "IBUC Parity still set after RST\n");
			dd->cspec->hwerrmask &=
				~QLOGIC_IB_HWE_IB_UC_MEMORYPARITYERR;
		}
		qib_write_kreg(dd, kr_hwerrmask,
			dd->cspec->hwerrmask);
	}

bail:
	return ret;
}

static void qib_sd_trimdone_monitor(struct qib_devdata *dd,
	const char *where)
{
	int ret, chn, baduns;
	u64 val;

	if (!where)
		where = "?";

	/* give time for reset to settle out in EPB */
	udelay(2);

	ret = qib_resync_ibepb(dd);
	if (ret < 0)
		qib_dev_err(dd, "not able to re-sync IB EPB (%s)\n", where);

	/* Do "sacrificial read" to get EPB in sane state after reset */
	ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES, IB_CTRL2(0), 0, 0);
	if (ret < 0)
		qib_dev_err(dd, "Failed TRIMDONE 1st read, (%s)\n", where);

	/* Check/show "summary" Trim-done bit in IBCStatus */
	val = qib_read_kreg64(dd, kr_ibcstatus);
	if (!(val & (1ULL << 11)))
		qib_dev_err(dd, "IBCS TRIMDONE clear (%s)\n", where);
	/*
	 * Do "dummy read/mod/wr" to get EPB in sane state after reset
	 * The default value for MPREG6 is 0.
	 */
	udelay(2);

	ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES, IB_MPREG6, 0x80, 0x80);
	if (ret < 0)
		qib_dev_err(dd, "Failed Dummy RMW, (%s)\n", where);
	udelay(10);

	baduns = 0;

	for (chn = 3; chn >= 0; --chn) {
		/* Read CTRL reg for each channel to check TRIMDONE */
		ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES,
			IB_CTRL2(chn), 0, 0);
		if (ret < 0)
			qib_dev_err(dd,
				"Failed checking TRIMDONE, chn %d (%s)\n",
				chn, where);

		if (!(ret & 0x10)) {
			int probe;

			baduns |= (1 << chn);
			qib_dev_err(dd,
				"TRIMDONE cleared on chn %d (%02X). (%s)\n",
				chn, ret, where);
			probe = qib_sd7220_reg_mod(dd, IB_7220_SERDES,
				IB_PGUDP(0), 0, 0);
			qib_dev_err(dd, "probe is %d (%02X)\n",
				probe, probe);
			probe = qib_sd7220_reg_mod(dd, IB_7220_SERDES,
				IB_CTRL2(chn), 0, 0);
			qib_dev_err(dd, "re-read: %d (%02X)\n",
				probe, probe);
			ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES,
				IB_CTRL2(chn), 0x10, 0x10);
			if (ret < 0)
				qib_dev_err(dd,
					"Err on TRIMDONE rewrite1\n");
		}
	}
	for (chn = 3; chn >= 0; --chn) {
		/* Read CTRL reg for each channel to check TRIMDONE */
		if (baduns & (1 << chn)) {
			qib_dev_err(dd,
				"Resetting TRIMDONE on chn %d (%s)\n",
				chn, where);
			ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES,
				IB_CTRL2(chn), 0x10, 0x10);
			if (ret < 0)
				qib_dev_err(dd,
					"Failed re-setting TRIMDONE, chn %d (%s)\n",
					chn, where);
		}
	}
}

/*
 * Below is portion of IBA7220-specific bringup_serdes() that actually
 * deals with registers and memory within the SerDes itself.
 * Post IB uC code version 1.32.17, was_reset being 1 is not really
 * informative, so we double-check.
 */
int qib_sd7220_init(struct qib_devdata *dd)
{
	const struct firmware *fw;
	int ret = 1; /* default to failure */
	int first_reset, was_reset;

	/* SERDES MPU reset recorded in D0 */
	was_reset = (qib_read_kreg64(dd, kr_ibserdesctrl) & 1);
	if (!was_reset) {
		/* entered with reset not asserted, we need to do it */
		qib_ibsd_reset(dd, 1);
		qib_sd_trimdone_monitor(dd, "Driver-reload");
	}

	ret = request_firmware(&fw, SD7220_FW_NAME, &dd->pcidev->dev);
	if (ret) {
		qib_dev_err(dd, "Failed to load IB SERDES image\n");
		goto done;
	}

	/* Substitute our deduced value for was_reset */
	ret = qib_ibsd_ucode_loaded(dd->pport, fw);
	if (ret < 0)
		goto bail;

	first_reset = !ret; /* First reset if IBSD uCode not yet loaded */
	/*
	 * Alter some regs per vendor latest doc, reset-defaults
	 * are not right for IB.
	 */
	ret = qib_sd_early(dd);
	if (ret < 0) {
		qib_dev_err(dd, "Failed to set IB SERDES early defaults\n");
		goto bail;
	}
	/*
	 * Set DAC manual trim IB.
	 * We only do this once after chip has been reset (usually
	 * same as once per system boot).
	 */
	if (first_reset) {
		ret = qib_sd_dactrim(dd);
		if (ret < 0) {
			qib_dev_err(dd, "Failed IB SERDES DAC trim\n");
			goto bail;
		}
	}
	/*
	 * Set various registers (DDS and RXEQ) that will be
	 * controlled by IBC (in 1.2 mode) to reasonable preset values
	 * Calling the "internal" version avoids the "check for needed"
	 * and "trimdone monitor" that might be counter-productive.
	 */
	ret = qib_internal_presets(dd);
	if (ret < 0) {
		qib_dev_err(dd, "Failed to set IB SERDES presets\n");
		goto bail;
	}
	ret = qib_sd_trimself(dd, 0x80);
	if (ret < 0) {
		qib_dev_err(dd, "Failed to set IB SERDES TRIMSELF\n");
		goto bail;
	}

	/* Load image, then try to verify */
	ret = 0;        /* Assume success */
	if (first_reset) {
		int vfy;
		int trim_done;

		ret = qib_sd7220_ib_load(dd, fw);
		if (ret < 0) {
			qib_dev_err(dd, "Failed to load IB SERDES image\n");
			goto bail;
		} else {
			/* Loaded image, try to verify */
			vfy = qib_sd7220_ib_vfy(dd, fw);
			if (vfy != ret) {
				qib_dev_err(dd, "SERDES PRAM VFY failed\n");
				goto bail;
			} /* end if verified */
		} /* end if loaded */

		/*
		 * Loaded and verified. Almost good...
		 * hold "success" in ret
		 */
		ret = 0;
		/*
		 * Prev steps all worked, continue bringup
		 * De-assert RESET to uC, only in first reset, to allow
		 * trimming.
		 *
		 * Since our default setup sets START_EQ1 to
		 * PRESET, we need to clear that for this very first run.
		 */
		ret = ibsd_mod_allchnls(dd, START_EQ1(0), 0, 0x38);
		if (ret < 0) {
			qib_dev_err(dd, "Failed clearing START_EQ1\n");
			goto bail;
		}

		qib_ibsd_reset(dd, 0);
		/*
		 * If this is not the first reset, trimdone should be set
		 * already. We may need to check about this.
		 */
		trim_done = qib_sd_trimdone_poll(dd);
		/*
		 * Whether or not trimdone succeeded, we need to put the
		 * uC back into reset to avoid a possible fight with the
		 * IBC state-machine.
		 */
		qib_ibsd_reset(dd, 1);

		if (!trim_done) {
			qib_dev_err(dd, "No TRIMDONE seen\n");
			goto bail;
		}
		/*
		 * DEBUG: check each time we reset if trimdone bits have
		 * gotten cleared, and re-set them.
		 */
		qib_sd_trimdone_monitor(dd, "First-reset");
		/* Remember so we do not re-do the load, dactrim, etc. */
		dd->cspec->serdes_first_init_done = 1;
	}
	/*
	 * setup for channel training and load values for
	 * RxEq and DDS in tables used by IBC in IB1.2 mode
	 */
	ret = 0;
	if (qib_sd_setvals(dd) >= 0)
		goto done;
bail:
	ret = 1;
done:
	/* start relock timer regardless, but start at 1 second */
	set_7220_relock_poll(dd, -1);

	release_firmware(fw);
	return ret;
}

#define EPB_ACC_REQ 1
#define EPB_ACC_GNT 0x100
#define EPB_DATA_MASK 0xFF
#define EPB_RD (1ULL << 24)
#define EPB_TRANS_RDY (1ULL << 31)
#define EPB_TRANS_ERR (1ULL << 30)
#define EPB_TRANS_TRIES 5

/*
 * query, claim, release ownership of the EPB (External Parallel Bus)
 * for a specified SERDES.
 * the "claim" parameter is >0 to claim, <0 to release, 0 to query.
 * Returns <0 for errors, >0 if we had ownership, else 0.
 */
static int epb_access(struct qib_devdata *dd, int sdnum, int claim)
{
	u16 acc;
	u64 accval;
	int owned = 0;
	u64 oct_sel = 0;

	switch (sdnum) {
	case IB_7220_SERDES:
		/*
		 * The IB SERDES "ownership" is fairly simple. A single each
		 * request/grant.
		 */
		acc = kr_ibsd_epb_access_ctrl;
		break;

	case PCIE_SERDES0:
	case PCIE_SERDES1:
		/* PCIe SERDES has two "octants", need to select which */
		acc = kr_pciesd_epb_access_ctrl;
		oct_sel = (2 << (sdnum - PCIE_SERDES0));
		break;

	default:
		return 0;
	}

	/* Make sure any outstanding transaction was seen */
	qib_read_kreg32(dd, kr_scratch);
	udelay(15);

	accval = qib_read_kreg32(dd, acc);

	owned = !!(accval & EPB_ACC_GNT);
	if (claim < 0) {
		/* Need to release */
		u64 pollval;
		/*
		 * The only writeable bits are the request and CS.
		 * Both should be clear
		 */
		u64 newval = 0;

		qib_write_kreg(dd, acc, newval);
		/* First read after write is not trustworthy */
		pollval = qib_read_kreg32(dd, acc);
		udelay(5);
		pollval = qib_read_kreg32(dd, acc);
		if (pollval & EPB_ACC_GNT)
			owned = -1;
	} else if (claim > 0) {
		/* Need to claim */
		u64 pollval;
		u64 newval = EPB_ACC_REQ | oct_sel;

		qib_write_kreg(dd, acc, newval);
		/* First read after write is not trustworthy */
		pollval = qib_read_kreg32(dd, acc);
		udelay(5);
		pollval = qib_read_kreg32(dd, acc);
		if (!(pollval & EPB_ACC_GNT))
			owned = -1;
	}
	return owned;
}

/*
 * Lemma to deal with race condition of write..read to epb regs
 */
static int epb_trans(struct qib_devdata *dd, u16 reg, u64 i_val, u64 *o_vp)
{
	int tries;
	u64 transval;

	qib_write_kreg(dd, reg, i_val);
	/* Throw away first read, as RDY bit may be stale */
	transval = qib_read_kreg64(dd, reg);

	for (tries = EPB_TRANS_TRIES; tries; --tries) {
		transval = qib_read_kreg32(dd, reg);
		if (transval & EPB_TRANS_RDY)
			break;
		udelay(5);
	}
	if (transval & EPB_TRANS_ERR)
		return -1;
	if (tries > 0 && o_vp)
		*o_vp = transval;
	return tries;
}

/**
 * qib_sd7220_reg_mod - modify SERDES register
 * @dd: the qlogic_ib device
 * @sdnum: which SERDES to access
 * @loc: location - channel, element, register, as packed by EPB_LOC() macro.
 * @wd: Write Data - value to set in register
 * @mask: ones where data should be spliced into reg.
 *
 * Basic register read/modify/write, with un-needed acesses elided. That is,
 * a mask of zero will prevent write, while a mask of 0xFF will prevent read.
 * returns current (presumed, if a write was done) contents of selected
 * register, or <0 if errors.
 */
static int qib_sd7220_reg_mod(struct qib_devdata *dd, int sdnum, u32 loc,
			      u32 wd, u32 mask)
{
	u16 trans;
	u64 transval;
	int owned;
	int tries, ret;
	unsigned long flags;

	switch (sdnum) {
	case IB_7220_SERDES:
		trans = kr_ibsd_epb_transaction_reg;
		break;

	case PCIE_SERDES0:
	case PCIE_SERDES1:
		trans = kr_pciesd_epb_transaction_reg;
		break;

	default:
		return -1;
	}

	/*
	 * All access is locked in software (vs other host threads) and
	 * hardware (vs uC access).
	 */
	spin_lock_irqsave(&dd->cspec->sdepb_lock, flags);

	owned = epb_access(dd, sdnum, 1);
	if (owned < 0) {
		spin_unlock_irqrestore(&dd->cspec->sdepb_lock, flags);
		return -1;
	}
	ret = 0;
	for (tries = EPB_TRANS_TRIES; tries; --tries) {
		transval = qib_read_kreg32(dd, trans);
		if (transval & EPB_TRANS_RDY)
			break;
		udelay(5);
	}

	if (tries > 0) {
		tries = 1;      /* to make read-skip work */
		if (mask != 0xFF) {
			/*
			 * Not a pure write, so need to read.
			 * loc encodes chip-select as well as address
			 */
			transval = loc | EPB_RD;
			tries = epb_trans(dd, trans, transval, &transval);
		}
		if (tries > 0 && mask != 0) {
			/*
			 * Not a pure read, so need to write.
			 */
			wd = (wd & mask) | (transval & ~mask);
			transval = loc | (wd & EPB_DATA_MASK);
			tries = epb_trans(dd, trans, transval, &transval);
		}
	}
	/* else, failed to see ready, what error-handling? */

	/*
	 * Release bus. Failure is an error.
	 */
	if (epb_access(dd, sdnum, -1) < 0)
		ret = -1;
	else
		ret = transval & EPB_DATA_MASK;

	spin_unlock_irqrestore(&dd->cspec->sdepb_lock, flags);
	if (tries <= 0)
		ret = -1;
	return ret;
}

#define EPB_ROM_R (2)
#define EPB_ROM_W (1)
/*
 * Below, all uC-related, use appropriate UC_CS, depending
 * on which SerDes is used.
 */
#define EPB_UC_CTL EPB_LOC(6, 0, 0)
#define EPB_MADDRL EPB_LOC(6, 0, 2)
#define EPB_MADDRH EPB_LOC(6, 0, 3)
#define EPB_ROMDATA EPB_LOC(6, 0, 4)
#define EPB_RAMDATA EPB_LOC(6, 0, 5)

/* Transfer date to/from uC Program RAM of IB or PCIe SerDes */
static int qib_sd7220_ram_xfer(struct qib_devdata *dd, int sdnum, u32 loc,
			       u8 *buf, int cnt, int rd_notwr)
{
	u16 trans;
	u64 transval;
	u64 csbit;
	int owned;
	int tries;
	int sofar;
	int addr;
	int ret;
	unsigned long flags;

	/* Pick appropriate transaction reg and "Chip select" for this serdes */
	switch (sdnum) {
	case IB_7220_SERDES:
		csbit = 1ULL << EPB_IB_UC_CS_SHF;
		trans = kr_ibsd_epb_transaction_reg;
		break;

	case PCIE_SERDES0:
	case PCIE_SERDES1:
		/* PCIe SERDES has uC "chip select" in different bit, too */
		csbit = 1ULL << EPB_PCIE_UC_CS_SHF;
		trans = kr_pciesd_epb_transaction_reg;
		break;

	default:
		return -1;
	}

	spin_lock_irqsave(&dd->cspec->sdepb_lock, flags);

	owned = epb_access(dd, sdnum, 1);
	if (owned < 0) {
		spin_unlock_irqrestore(&dd->cspec->sdepb_lock, flags);
		return -1;
	}

	/*
	 * In future code, we may need to distinguish several address ranges,
	 * and select various memories based on this. For now, just trim
	 * "loc" (location including address and memory select) to
	 * "addr" (address within memory). we will only support PRAM
	 * The memory is 8KB.
	 */
	addr = loc & 0x1FFF;
	for (tries = EPB_TRANS_TRIES; tries; --tries) {
		transval = qib_read_kreg32(dd, trans);
		if (transval & EPB_TRANS_RDY)
			break;
		udelay(5);
	}

	sofar = 0;
	if (tries > 0) {
		/*
		 * Every "memory" access is doubly-indirect.
		 * We set two bytes of address, then read/write
		 * one or mores bytes of data.
		 */

		/* First, we set control to "Read" or "Write" */
		transval = csbit | EPB_UC_CTL |
			(rd_notwr ? EPB_ROM_R : EPB_ROM_W);
		tries = epb_trans(dd, trans, transval, &transval);
		while (tries > 0 && sofar < cnt) {
			if (!sofar) {
				/* Only set address at start of chunk */
				int addrbyte = (addr + sofar) >> 8;

				transval = csbit | EPB_MADDRH | addrbyte;
				tries = epb_trans(dd, trans, transval,
						  &transval);
				if (tries <= 0)
					break;
				addrbyte = (addr + sofar) & 0xFF;
				transval = csbit | EPB_MADDRL | addrbyte;
				tries = epb_trans(dd, trans, transval,
						 &transval);
				if (tries <= 0)
					break;
			}

			if (rd_notwr)
				transval = csbit | EPB_ROMDATA | EPB_RD;
			else
				transval = csbit | EPB_ROMDATA | buf[sofar];
			tries = epb_trans(dd, trans, transval, &transval);
			if (tries <= 0)
				break;
			if (rd_notwr)
				buf[sofar] = transval & EPB_DATA_MASK;
			++sofar;
		}
		/* Finally, clear control-bit for Read or Write */
		transval = csbit | EPB_UC_CTL;
		tries = epb_trans(dd, trans, transval, &transval);
	}

	ret = sofar;
	/* Release bus. Failure is an error */
	if (epb_access(dd, sdnum, -1) < 0)
		ret = -1;

	spin_unlock_irqrestore(&dd->cspec->sdepb_lock, flags);
	if (tries <= 0)
		ret = -1;
	return ret;
}

#define PROG_CHUNK 64

static int qib_sd7220_prog_ld(struct qib_devdata *dd, int sdnum,
			      const u8 *img, int len, int offset)
{
	int cnt, sofar, req;

	sofar = 0;
	while (sofar < len) {
		req = len - sofar;
		if (req > PROG_CHUNK)
			req = PROG_CHUNK;
		cnt = qib_sd7220_ram_xfer(dd, sdnum, offset + sofar,
					  (u8 *)img + sofar, req, 0);
		if (cnt < req) {
			sofar = -1;
			break;
		}
		sofar += req;
	}
	return sofar;
}

#define VFY_CHUNK 64
#define SD_PRAM_ERROR_LIMIT 42

static int qib_sd7220_prog_vfy(struct qib_devdata *dd, int sdnum,
			       const u8 *img, int len, int offset)
{
	int cnt, sofar, req, idx, errors;
	unsigned char readback[VFY_CHUNK];

	errors = 0;
	sofar = 0;
	while (sofar < len) {
		req = len - sofar;
		if (req > VFY_CHUNK)
			req = VFY_CHUNK;
		cnt = qib_sd7220_ram_xfer(dd, sdnum, sofar + offset,
					  readback, req, 1);
		if (cnt < req) {
			/* failed in read itself */
			sofar = -1;
			break;
		}
		for (idx = 0; idx < cnt; ++idx) {
			if (readback[idx] != img[idx+sofar])
				++errors;
		}
		sofar += cnt;
	}
	return errors ? -errors : sofar;
}

static int
qib_sd7220_ib_load(struct qib_devdata *dd, const struct firmware *fw)
{
	return qib_sd7220_prog_ld(dd, IB_7220_SERDES, fw->data, fw->size, 0);
}

static int
qib_sd7220_ib_vfy(struct qib_devdata *dd, const struct firmware *fw)
{
	return qib_sd7220_prog_vfy(dd, IB_7220_SERDES, fw->data, fw->size, 0);
}

/*
 * IRQ not set up at this point in init, so we poll.
 */
#define IB_SERDES_TRIM_DONE (1ULL << 11)
#define TRIM_TMO (15)

static int qib_sd_trimdone_poll(struct qib_devdata *dd)
{
	int trim_tmo, ret;
	uint64_t val;

	/*
	 * Default to failure, so IBC will not start
	 * without IB_SERDES_TRIM_DONE.
	 */
	ret = 0;
	for (trim_tmo = 0; trim_tmo < TRIM_TMO; ++trim_tmo) {
		val = qib_read_kreg64(dd, kr_ibcstatus);
		if (val & IB_SERDES_TRIM_DONE) {
			ret = 1;
			break;
		}
		msleep(20);
	}
	if (trim_tmo >= TRIM_TMO) {
		qib_dev_err(dd, "No TRIMDONE in %d tries\n", trim_tmo);
		ret = 0;
	}
	return ret;
}

#define TX_FAST_ELT (9)

/*
 * Set the "negotiation" values for SERDES. These are used by the IB1.2
 * link negotiation. Macros below are attempt to keep the values a
 * little more human-editable.
 * First, values related to Drive De-emphasis Settings.
 */

#define NUM_DDS_REGS 6
#define DDS_REG_MAP 0x76A910 /* LSB-first list of regs (in elt 9) to mod */

#define DDS_VAL(amp_d, main_d, ipst_d, ipre_d, amp_s, main_s, ipst_s, ipre_s) \
	{ { ((amp_d & 0x1F) << 1) | 1, ((amp_s & 0x1F) << 1) | 1, \
	  (main_d << 3) | 4 | (ipre_d >> 2), \
	  (main_s << 3) | 4 | (ipre_s >> 2), \
	  ((ipst_d & 0xF) << 1) | ((ipre_d & 3) << 6) | 0x21, \
	  ((ipst_s & 0xF) << 1) | ((ipre_s & 3) << 6) | 0x21 } }

static struct dds_init {
	uint8_t reg_vals[NUM_DDS_REGS];
} dds_init_vals[] = {
	/*       DDR(FDR)       SDR(HDR)   */
	/* Vendor recommends below for 3m cable */
#define DDS_3M 0
	DDS_VAL(31, 19, 12, 0, 29, 22,  9, 0),
	DDS_VAL(31, 12, 15, 4, 31, 15, 15, 1),
	DDS_VAL(31, 13, 15, 3, 31, 16, 15, 0),
	DDS_VAL(31, 14, 15, 2, 31, 17, 14, 0),
	DDS_VAL(31, 15, 15, 1, 31, 18, 13, 0),
	DDS_VAL(31, 16, 15, 0, 31, 19, 12, 0),
	DDS_VAL(31, 17, 14, 0, 31, 20, 11, 0),
	DDS_VAL(31, 18, 13, 0, 30, 21, 10, 0),
	DDS_VAL(31, 20, 11, 0, 28, 23,  8, 0),
	DDS_VAL(31, 21, 10, 0, 27, 24,  7, 0),
	DDS_VAL(31, 22,  9, 0, 26, 25,  6, 0),
	DDS_VAL(30, 23,  8, 0, 25, 26,  5, 0),
	DDS_VAL(29, 24,  7, 0, 23, 27,  4, 0),
	/* Vendor recommends below for 1m cable */
#define DDS_1M 13
	DDS_VAL(28, 25,  6, 0, 21, 28,  3, 0),
	DDS_VAL(27, 26,  5, 0, 19, 29,  2, 0),
	DDS_VAL(25, 27,  4, 0, 17, 30,  1, 0)
};

/*
 * Now the RXEQ section of the table.
 */
/* Hardware packs an element number and register address thus: */
#define RXEQ_INIT_RDESC(elt, addr) (((elt) & 0xF) | ((addr) << 4))
#define RXEQ_VAL(elt, adr, val0, val1, val2, val3) \
	{RXEQ_INIT_RDESC((elt), (adr)), {(val0), (val1), (val2), (val3)} }

#define RXEQ_VAL_ALL(elt, adr, val)  \
	{RXEQ_INIT_RDESC((elt), (adr)), {(val), (val), (val), (val)} }

#define RXEQ_SDR_DFELTH 0
#define RXEQ_SDR_TLTH 0
#define RXEQ_SDR_G1CNT_Z1CNT 0x11
#define RXEQ_SDR_ZCNT 23

static struct rxeq_init {
	u16 rdesc;      /* in form used in SerDesDDSRXEQ */
	u8  rdata[4];
} rxeq_init_vals[] = {
	/* Set Rcv Eq. to Preset node */
	RXEQ_VAL_ALL(7, 0x27, 0x10),
	/* Set DFELTHFDR/HDR thresholds */
	RXEQ_VAL(7, 8,    0, 0, 0, 0), /* FDR, was 0, 1, 2, 3 */
	RXEQ_VAL(7, 0x21, 0, 0, 0, 0), /* HDR */
	/* Set TLTHFDR/HDR theshold */
	RXEQ_VAL(7, 9,    2, 2, 2, 2), /* FDR, was 0, 2, 4, 6 */
	RXEQ_VAL(7, 0x23, 2, 2, 2, 2), /* HDR, was  0, 1, 2, 3 */
	/* Set Preamp setting 2 (ZFR/ZCNT) */
	RXEQ_VAL(7, 0x1B, 12, 12, 12, 12), /* FDR, was 12, 16, 20, 24 */
	RXEQ_VAL(7, 0x1C, 12, 12, 12, 12), /* HDR, was 12, 16, 20, 24 */
	/* Set Preamp DC gain and Setting 1 (GFR/GHR) */
	RXEQ_VAL(7, 0x1E, 16, 16, 16, 16), /* FDR, was 16, 17, 18, 20 */
	RXEQ_VAL(7, 0x1F, 16, 16, 16, 16), /* HDR, was 16, 17, 18, 20 */
	/* Toggle RELOCK (in VCDL_CTRL0) to lock to data */
	RXEQ_VAL_ALL(6, 6, 0x20), /* Set D5 High */
	RXEQ_VAL_ALL(6, 6, 0), /* Set D5 Low */
};

/* There are 17 values from vendor, but IBC only accesses the first 16 */
#define DDS_ROWS (16)
#define RXEQ_ROWS ARRAY_SIZE(rxeq_init_vals)

static int qib_sd_setvals(struct qib_devdata *dd)
{
	int idx, midx;
	int min_idx;     /* Minimum index for this portion of table */
	uint32_t dds_reg_map;
	u64 __iomem *taddr, *iaddr;
	uint64_t data;
	uint64_t sdctl;

	taddr = dd->kregbase + kr_serdes_maptable;
	iaddr = dd->kregbase + kr_serdes_ddsrxeq0;

	/*
	 * Init the DDS section of the table.
	 * Each "row" of the table provokes NUM_DDS_REG writes, to the
	 * registers indicated in DDS_REG_MAP.
	 */
	sdctl = qib_read_kreg64(dd, kr_ibserdesctrl);
	sdctl = (sdctl & ~(0x1f << 8)) | (NUM_DDS_REGS << 8);
	sdctl = (sdctl & ~(0x1f << 13)) | (RXEQ_ROWS << 13);
	qib_write_kreg(dd, kr_ibserdesctrl, sdctl);

	/*
	 * Iterate down table within loop for each register to store.
	 */
	dds_reg_map = DDS_REG_MAP;
	for (idx = 0; idx < NUM_DDS_REGS; ++idx) {
		data = ((dds_reg_map & 0xF) << 4) | TX_FAST_ELT;
		writeq(data, iaddr + idx);
		mmiowb();
		qib_read_kreg32(dd, kr_scratch);
		dds_reg_map >>= 4;
		for (midx = 0; midx < DDS_ROWS; ++midx) {
			u64 __iomem *daddr = taddr + ((midx << 4) + idx);

			data = dds_init_vals[midx].reg_vals[idx];
			writeq(data, daddr);
			mmiowb();
			qib_read_kreg32(dd, kr_scratch);
		} /* End inner for (vals for this reg, each row) */
	} /* end outer for (regs to be stored) */

	/*
	 * Init the RXEQ section of the table.
	 * This runs in a different order, as the pattern of
	 * register references is more complex, but there are only
	 * four "data" values per register.
	 */
	min_idx = idx; /* RXEQ indices pick up where DDS left off */
	taddr += 0x100; /* RXEQ data is in second half of table */
	/* Iterate through RXEQ register addresses */
	for (idx = 0; idx < RXEQ_ROWS; ++idx) {
		int didx; /* "destination" */
		int vidx;

		/* didx is offset by min_idx to address RXEQ range of regs */
		didx = idx + min_idx;
		/* Store the next RXEQ register address */
		writeq(rxeq_init_vals[idx].rdesc, iaddr + didx);
		mmiowb();
		qib_read_kreg32(dd, kr_scratch);
		/* Iterate through RXEQ values */
		for (vidx = 0; vidx < 4; vidx++) {
			data = rxeq_init_vals[idx].rdata[vidx];
			writeq(data, taddr + (vidx << 6) + idx);
			mmiowb();
			qib_read_kreg32(dd, kr_scratch);
		}
	} /* end outer for (Reg-writes for RXEQ) */
	return 0;
}

#define CMUCTRL5 EPB_LOC(7, 0, 0x15)
#define RXHSCTRL0(chan) EPB_LOC(chan, 6, 0)
#define VCDL_DAC2(chan) EPB_LOC(chan, 6, 5)
#define VCDL_CTRL0(chan) EPB_LOC(chan, 6, 6)
#define VCDL_CTRL2(chan) EPB_LOC(chan, 6, 8)
#define START_EQ2(chan) EPB_LOC(chan, 7, 0x28)

/*
 * Repeat a "store" across all channels of the IB SerDes.
 * Although nominally it inherits the "read value" of the last
 * channel it modified, the only really useful return is <0 for
 * failure, >= 0 for success. The parameter 'loc' is assumed to
 * be the location in some channel of the register to be modified
 * The caller can specify use of the "gang write" option of EPB,
 * in which case we use the specified channel data for any fields
 * not explicitely written.
 */
static int ibsd_mod_allchnls(struct qib_devdata *dd, int loc, int val,
			     int mask)
{
	int ret = -1;
	int chnl;

	if (loc & EPB_GLOBAL_WR) {
		/*
		 * Our caller has assured us that we can set all four
		 * channels at once. Trust that. If mask is not 0xFF,
		 * we will read the _specified_ channel for our starting
		 * value.
		 */
		loc |= (1U << EPB_IB_QUAD0_CS_SHF);
		chnl = (loc >> (4 + EPB_ADDR_SHF)) & 7;
		if (mask != 0xFF) {
			ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES,
						 loc & ~EPB_GLOBAL_WR, 0, 0);
			if (ret < 0) {
				int sloc = loc >> EPB_ADDR_SHF;

				qib_dev_err(dd,
					"pre-read failed: elt %d, addr 0x%X, chnl %d\n",
					(sloc & 0xF),
					(sloc >> 9) & 0x3f, chnl);
				return ret;
			}
			val = (ret & ~mask) | (val & mask);
		}
		loc &=  ~(7 << (4+EPB_ADDR_SHF));
		ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES, loc, val, 0xFF);
		if (ret < 0) {
			int sloc = loc >> EPB_ADDR_SHF;

			qib_dev_err(dd,
				"Global WR failed: elt %d, addr 0x%X, val %02X\n",
				(sloc & 0xF), (sloc >> 9) & 0x3f, val);
		}
		return ret;
	}
	/* Clear "channel" and set CS so we can simply iterate */
	loc &=  ~(7 << (4+EPB_ADDR_SHF));
	loc |= (1U << EPB_IB_QUAD0_CS_SHF);
	for (chnl = 0; chnl < 4; ++chnl) {
		int cloc = loc | (chnl << (4+EPB_ADDR_SHF));

		ret = qib_sd7220_reg_mod(dd, IB_7220_SERDES, cloc, val, mask);
		if (ret < 0) {
			int sloc = loc >> EPB_ADDR_SHF;

			qib_dev_err(dd,
				"Write failed: elt %d, addr 0x%X, chnl %d, val 0x%02X, mask 0x%02X\n",
				(sloc & 0xF), (sloc >> 9) & 0x3f, chnl,
				val & 0xFF, mask & 0xFF);
			break;
		}
	}
	return ret;
}

/*
 * Set the Tx values normally modified by IBC in IB1.2 mode to default
 * values, as gotten from first row of init table.
 */
static int set_dds_vals(struct qib_devdata *dd, struct dds_init *ddi)
{
	int ret;
	int idx, reg, data;
	uint32_t regmap;

	regmap = DDS_REG_MAP;
	for (idx = 0; idx < NUM_DDS_REGS; ++idx) {
		reg = (regmap & 0xF);
		regmap >>= 4;
		data = ddi->reg_vals[idx];
		/* Vendor says RMW not needed for these regs, use 0xFF mask */
		ret = ibsd_mod_allchnls(dd, EPB_LOC(0, 9, reg), data, 0xFF);
		if (ret < 0)
			break;
	}
	return ret;
}

/*
 * Set the Rx values normally modified by IBC in IB1.2 mode to default
 * values, as gotten from selected column of init table.
 */
static int set_rxeq_vals(struct qib_devdata *dd, int vsel)
{
	int ret;
	int ridx;
	int cnt = ARRAY_SIZE(rxeq_init_vals);

	for (ridx = 0; ridx < cnt; ++ridx) {
		int elt, reg, val, loc;

		elt = rxeq_init_vals[ridx].rdesc & 0xF;
		reg = rxeq_init_vals[ridx].rdesc >> 4;
		loc = EPB_LOC(0, elt, reg);
		val = rxeq_init_vals[ridx].rdata[vsel];
		/* mask of 0xFF, because hardware does full-byte store. */
		ret = ibsd_mod_allchnls(dd, loc, val, 0xFF);
		if (ret < 0)
			break;
	}
	return ret;
}

/*
 * Set the default values (row 0) for DDR Driver Demphasis.
 * we do this initially and whenever we turn off IB-1.2
 *
 * The "default" values for Rx equalization are also stored to
 * SerDes registers. Formerly (and still default), we used set 2.
 * For experimenting with cables and link-partners, we allow changing
 * that via a module parameter.
 */
static unsigned qib_rxeq_set = 2;
module_param_named(rxeq_default_set, qib_rxeq_set, uint,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(rxeq_default_set,
		 "Which set [0..3] of Rx Equalization values is default");

static int qib_internal_presets(struct qib_devdata *dd)
{
	int ret = 0;

	ret = set_dds_vals(dd, dds_init_vals + DDS_3M);

	if (ret < 0)
		qib_dev_err(dd, "Failed to set default DDS values\n");
	ret = set_rxeq_vals(dd, qib_rxeq_set & 3);
	if (ret < 0)
		qib_dev_err(dd, "Failed to set default RXEQ values\n");
	return ret;
}

int qib_sd7220_presets(struct qib_devdata *dd)
{
	int ret = 0;

	if (!dd->cspec->presets_needed)
		return ret;
	dd->cspec->presets_needed = 0;
	/* Assert uC reset, so we don't clash with it. */
	qib_ibsd_reset(dd, 1);
	udelay(2);
	qib_sd_trimdone_monitor(dd, "link-down");

	ret = qib_internal_presets(dd);
	return ret;
}

static int qib_sd_trimself(struct qib_devdata *dd, int val)
{
	int loc = CMUCTRL5 | (1U << EPB_IB_QUAD0_CS_SHF);

	return qib_sd7220_reg_mod(dd, IB_7220_SERDES, loc, val, 0xFF);
}

static int qib_sd_early(struct qib_devdata *dd)
{
	int ret;

	ret = ibsd_mod_allchnls(dd, RXHSCTRL0(0) | EPB_GLOBAL_WR, 0xD4, 0xFF);
	if (ret < 0)
		goto bail;
	ret = ibsd_mod_allchnls(dd, START_EQ1(0) | EPB_GLOBAL_WR, 0x10, 0xFF);
	if (ret < 0)
		goto bail;
	ret = ibsd_mod_allchnls(dd, START_EQ2(0) | EPB_GLOBAL_WR, 0x30, 0xFF);
bail:
	return ret;
}

#define BACTRL(chnl) EPB_LOC(chnl, 6, 0x0E)
#define LDOUTCTRL1(chnl) EPB_LOC(chnl, 7, 6)
#define RXHSSTATUS(chnl) EPB_LOC(chnl, 6, 0xF)

static int qib_sd_dactrim(struct qib_devdata *dd)
{
	int ret;

	ret = ibsd_mod_allchnls(dd, VCDL_DAC2(0) | EPB_GLOBAL_WR, 0x2D, 0xFF);
	if (ret < 0)
		goto bail;

	/* more fine-tuning of what will be default */
	ret = ibsd_mod_allchnls(dd, VCDL_CTRL2(0), 3, 0xF);
	if (ret < 0)
		goto bail;

	ret = ibsd_mod_allchnls(dd, BACTRL(0) | EPB_GLOBAL_WR, 0x40, 0xFF);
	if (ret < 0)
		goto bail;

	ret = ibsd_mod_allchnls(dd, LDOUTCTRL1(0) | EPB_GLOBAL_WR, 0x04, 0xFF);
	if (ret < 0)
		goto bail;

	ret = ibsd_mod_allchnls(dd, RXHSSTATUS(0) | EPB_GLOBAL_WR, 0x04, 0xFF);
	if (ret < 0)
		goto bail;

	/*
	 * Delay for max possible number of steps, with slop.
	 * Each step is about 4usec.
	 */
	udelay(415);

	ret = ibsd_mod_allchnls(dd, LDOUTCTRL1(0) | EPB_GLOBAL_WR, 0x00, 0xFF);

bail:
	return ret;
}

#define RELOCK_FIRST_MS 3
#define RXLSPPM(chan) EPB_LOC(chan, 0, 2)
void toggle_7220_rclkrls(struct qib_devdata *dd)
{
	int loc = RXLSPPM(0) | EPB_GLOBAL_WR;
	int ret;

	ret = ibsd_mod_allchnls(dd, loc, 0, 0x80);
	if (ret < 0)
		qib_dev_err(dd, "RCLKRLS failed to clear D7\n");
	else {
		udelay(1);
		ibsd_mod_allchnls(dd, loc, 0x80, 0x80);
	}
	/* And again for good measure */
	udelay(1);
	ret = ibsd_mod_allchnls(dd, loc, 0, 0x80);
	if (ret < 0)
		qib_dev_err(dd, "RCLKRLS failed to clear D7\n");
	else {
		udelay(1);
		ibsd_mod_allchnls(dd, loc, 0x80, 0x80);
	}
	/* Now reset xgxs and IBC to complete the recovery */
	dd->f_xgxs_reset(dd->pport);
}

/*
 * Shut down the timer that polls for relock occasions, if needed
 * this is "hooked" from qib_7220_quiet_serdes(), which is called
 * just before qib_shutdown_device() in qib_driver.c shuts down all
 * the other timers
 */
void shutdown_7220_relock_poll(struct qib_devdata *dd)
{
	if (dd->cspec->relock_timer_active)
		del_timer_sync(&dd->cspec->relock_timer);
}

static unsigned qib_relock_by_timer = 1;
module_param_named(relock_by_timer, qib_relock_by_timer, uint,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(relock_by_timer, "Allow relock attempt if link not up");

static void qib_run_relock(struct timer_list *t)
{
	struct qib_chip_specific *cs = from_timer(cs, t, relock_timer);
	struct qib_devdata *dd = cs->dd;
	struct qib_pportdata *ppd = dd->pport;
	int timeoff;

	/*
	 * Check link-training state for "stuck" state, when down.
	 * if found, try relock and schedule another try at
	 * exponentially growing delay, maxed at one second.
	 * if not stuck, our work is done.
	 */
	if ((dd->flags & QIB_INITTED) && !(ppd->lflags &
	    (QIBL_IB_AUTONEG_INPROG | QIBL_LINKINIT | QIBL_LINKARMED |
	     QIBL_LINKACTIVE))) {
		if (qib_relock_by_timer) {
			if (!(ppd->lflags & QIBL_IB_LINK_DISABLED))
				toggle_7220_rclkrls(dd);
		}
		/* re-set timer for next check */
		timeoff = cs->relock_interval << 1;
		if (timeoff > HZ)
			timeoff = HZ;
		cs->relock_interval = timeoff;
	} else
		timeoff = HZ;
	mod_timer(&cs->relock_timer, jiffies + timeoff);
}

void set_7220_relock_poll(struct qib_devdata *dd, int ibup)
{
	struct qib_chip_specific *cs = dd->cspec;

	if (ibup) {
		/* We are now up, relax timer to 1 second interval */
		if (cs->relock_timer_active) {
			cs->relock_interval = HZ;
			mod_timer(&cs->relock_timer, jiffies + HZ);
		}
	} else {
		/* Transition to down, (re-)set timer to short interval. */
		unsigned int timeout;

		timeout = msecs_to_jiffies(RELOCK_FIRST_MS);
		if (timeout == 0)
			timeout = 1;
		/* If timer has not yet been started, do so. */
		if (!cs->relock_timer_active) {
			cs->relock_timer_active = 1;
			timer_setup(&cs->relock_timer, qib_run_relock, 0);
			cs->relock_interval = timeout;
			cs->relock_timer.expires = jiffies + timeout;
			add_timer(&cs->relock_timer);
		} else {
			cs->relock_interval = timeout;
			mod_timer(&cs->relock_timer, jiffies + timeout);
		}
	}
}
