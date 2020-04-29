// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for mt2063 Micronas tuner
 *
 * Copyright (c) 2011 Mauro Carvalho Chehab
 *
 * This driver came from a driver originally written by:
 *		Henry Wang <Henry.wang@AzureWave.com>
 * Made publicly available by Terratec, at:
 *	http://linux.terratec.de/files/TERRATEC_H7/20110323_TERRATEC_H7_Linux.tar.gz
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/videodev2.h>
#include <linux/gcd.h>

#include "mt2063.h"

static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Set Verbosity level");

#define dprintk(level, fmt, arg...) do {				\
if (debug >= level)							\
	printk(KERN_DEBUG "mt2063 %s: " fmt, __func__, ## arg);	\
} while (0)


/* positive error codes used internally */

/*  Info: Unavoidable LO-related spur may be present in the output  */
#define MT2063_SPUR_PRESENT_ERR             (0x00800000)

/*  Info: Mask of bits used for # of LO-related spurs that were avoided during tuning  */
#define MT2063_SPUR_CNT_MASK                (0x001f0000)
#define MT2063_SPUR_SHIFT                   (16)

/*  Info: Upconverter frequency is out of range (may be reason for MT_UPC_UNLOCK) */
#define MT2063_UPC_RANGE                    (0x04000000)

/*  Info: Downconverter frequency is out of range (may be reason for MT_DPC_UNLOCK) */
#define MT2063_DNC_RANGE                    (0x08000000)

/*
 *  Constant defining the version of the following structure
 *  and therefore the API for this code.
 *
 *  When compiling the tuner driver, the preprocessor will
 *  check against this version number to make sure that
 *  it matches the version that the tuner driver knows about.
 */

/* DECT Frequency Avoidance */
#define MT2063_DECT_AVOID_US_FREQS      0x00000001

#define MT2063_DECT_AVOID_EURO_FREQS    0x00000002

#define MT2063_EXCLUDE_US_DECT_FREQUENCIES(s) (((s) & MT2063_DECT_AVOID_US_FREQS) != 0)

#define MT2063_EXCLUDE_EURO_DECT_FREQUENCIES(s) (((s) & MT2063_DECT_AVOID_EURO_FREQS) != 0)

enum MT2063_DECT_Avoid_Type {
	MT2063_NO_DECT_AVOIDANCE = 0,				/* Do not create DECT exclusion zones.     */
	MT2063_AVOID_US_DECT = MT2063_DECT_AVOID_US_FREQS,	/* Avoid US DECT frequencies.              */
	MT2063_AVOID_EURO_DECT = MT2063_DECT_AVOID_EURO_FREQS,	/* Avoid European DECT frequencies.        */
	MT2063_AVOID_BOTH					/* Avoid both regions. Not typically used. */
};

#define MT2063_MAX_ZONES 48

struct MT2063_ExclZone_t {
	u32 min_;
	u32 max_;
	struct MT2063_ExclZone_t *next_;
};

/*
 *  Structure of data needed for Spur Avoidance
 */
struct MT2063_AvoidSpursData_t {
	u32 f_ref;
	u32 f_in;
	u32 f_LO1;
	u32 f_if1_Center;
	u32 f_if1_Request;
	u32 f_if1_bw;
	u32 f_LO2;
	u32 f_out;
	u32 f_out_bw;
	u32 f_LO1_Step;
	u32 f_LO2_Step;
	u32 f_LO1_FracN_Avoid;
	u32 f_LO2_FracN_Avoid;
	u32 f_zif_bw;
	u32 f_min_LO_Separation;
	u32 maxH1;
	u32 maxH2;
	enum MT2063_DECT_Avoid_Type avoidDECT;
	u32 bSpurPresent;
	u32 bSpurAvoided;
	u32 nSpursFound;
	u32 nZones;
	struct MT2063_ExclZone_t *freeZones;
	struct MT2063_ExclZone_t *usedZones;
	struct MT2063_ExclZone_t MT2063_ExclZones[MT2063_MAX_ZONES];
};

/*
 * Parameter for function MT2063_SetPowerMask that specifies the power down
 * of various sections of the MT2063.
 */
enum MT2063_Mask_Bits {
	MT2063_REG_SD = 0x0040,		/* Shutdown regulator                 */
	MT2063_SRO_SD = 0x0020,		/* Shutdown SRO                       */
	MT2063_AFC_SD = 0x0010,		/* Shutdown AFC A/D                   */
	MT2063_PD_SD = 0x0002,		/* Enable power detector shutdown     */
	MT2063_PDADC_SD = 0x0001,	/* Enable power detector A/D shutdown */
	MT2063_VCO_SD = 0x8000,		/* Enable VCO shutdown                */
	MT2063_LTX_SD = 0x4000,		/* Enable LTX shutdown                */
	MT2063_LT1_SD = 0x2000,		/* Enable LT1 shutdown                */
	MT2063_LNA_SD = 0x1000,		/* Enable LNA shutdown                */
	MT2063_UPC_SD = 0x0800,		/* Enable upconverter shutdown        */
	MT2063_DNC_SD = 0x0400,		/* Enable downconverter shutdown      */
	MT2063_VGA_SD = 0x0200,		/* Enable VGA shutdown                */
	MT2063_AMP_SD = 0x0100,		/* Enable AMP shutdown                */
	MT2063_ALL_SD = 0xFF73,		/* All shutdown bits for this tuner   */
	MT2063_NONE_SD = 0x0000		/* No shutdown bits                   */
};

/*
 *  Possible values for MT2063_DNC_OUTPUT
 */
enum MT2063_DNC_Output_Enable {
	MT2063_DNC_NONE = 0,
	MT2063_DNC_1,
	MT2063_DNC_2,
	MT2063_DNC_BOTH
};

/*
 *  Two-wire serial bus subaddresses of the tuner registers.
 *  Also known as the tuner's register addresses.
 */
enum MT2063_Register_Offsets {
	MT2063_REG_PART_REV = 0,	/*  0x00: Part/Rev Code         */
	MT2063_REG_LO1CQ_1,		/*  0x01: LO1C Queued Byte 1    */
	MT2063_REG_LO1CQ_2,		/*  0x02: LO1C Queued Byte 2    */
	MT2063_REG_LO2CQ_1,		/*  0x03: LO2C Queued Byte 1    */
	MT2063_REG_LO2CQ_2,		/*  0x04: LO2C Queued Byte 2    */
	MT2063_REG_LO2CQ_3,		/*  0x05: LO2C Queued Byte 3    */
	MT2063_REG_RSVD_06,		/*  0x06: Reserved              */
	MT2063_REG_LO_STATUS,		/*  0x07: LO Status             */
	MT2063_REG_FIFFC,		/*  0x08: FIFF Center           */
	MT2063_REG_CLEARTUNE,		/*  0x09: ClearTune Filter      */
	MT2063_REG_ADC_OUT,		/*  0x0A: ADC_OUT               */
	MT2063_REG_LO1C_1,		/*  0x0B: LO1C Byte 1           */
	MT2063_REG_LO1C_2,		/*  0x0C: LO1C Byte 2           */
	MT2063_REG_LO2C_1,		/*  0x0D: LO2C Byte 1           */
	MT2063_REG_LO2C_2,		/*  0x0E: LO2C Byte 2           */
	MT2063_REG_LO2C_3,		/*  0x0F: LO2C Byte 3           */
	MT2063_REG_RSVD_10,		/*  0x10: Reserved              */
	MT2063_REG_PWR_1,		/*  0x11: PWR Byte 1            */
	MT2063_REG_PWR_2,		/*  0x12: PWR Byte 2            */
	MT2063_REG_TEMP_STATUS,		/*  0x13: Temp Status           */
	MT2063_REG_XO_STATUS,		/*  0x14: Crystal Status        */
	MT2063_REG_RF_STATUS,		/*  0x15: RF Attn Status        */
	MT2063_REG_FIF_STATUS,		/*  0x16: FIF Attn Status       */
	MT2063_REG_LNA_OV,		/*  0x17: LNA Attn Override     */
	MT2063_REG_RF_OV,		/*  0x18: RF Attn Override      */
	MT2063_REG_FIF_OV,		/*  0x19: FIF Attn Override     */
	MT2063_REG_LNA_TGT,		/*  0x1A: Reserved              */
	MT2063_REG_PD1_TGT,		/*  0x1B: Pwr Det 1 Target      */
	MT2063_REG_PD2_TGT,		/*  0x1C: Pwr Det 2 Target      */
	MT2063_REG_RSVD_1D,		/*  0x1D: Reserved              */
	MT2063_REG_RSVD_1E,		/*  0x1E: Reserved              */
	MT2063_REG_RSVD_1F,		/*  0x1F: Reserved              */
	MT2063_REG_RSVD_20,		/*  0x20: Reserved              */
	MT2063_REG_BYP_CTRL,		/*  0x21: Bypass Control        */
	MT2063_REG_RSVD_22,		/*  0x22: Reserved              */
	MT2063_REG_RSVD_23,		/*  0x23: Reserved              */
	MT2063_REG_RSVD_24,		/*  0x24: Reserved              */
	MT2063_REG_RSVD_25,		/*  0x25: Reserved              */
	MT2063_REG_RSVD_26,		/*  0x26: Reserved              */
	MT2063_REG_RSVD_27,		/*  0x27: Reserved              */
	MT2063_REG_FIFF_CTRL,		/*  0x28: FIFF Control          */
	MT2063_REG_FIFF_OFFSET,		/*  0x29: FIFF Offset           */
	MT2063_REG_CTUNE_CTRL,		/*  0x2A: Reserved              */
	MT2063_REG_CTUNE_OV,		/*  0x2B: Reserved              */
	MT2063_REG_CTRL_2C,		/*  0x2C: Reserved              */
	MT2063_REG_FIFF_CTRL2,		/*  0x2D: Fiff Control          */
	MT2063_REG_RSVD_2E,		/*  0x2E: Reserved              */
	MT2063_REG_DNC_GAIN,		/*  0x2F: DNC Control           */
	MT2063_REG_VGA_GAIN,		/*  0x30: VGA Gain Ctrl         */
	MT2063_REG_RSVD_31,		/*  0x31: Reserved              */
	MT2063_REG_TEMP_SEL,		/*  0x32: Temperature Selection */
	MT2063_REG_RSVD_33,		/*  0x33: Reserved              */
	MT2063_REG_RSVD_34,		/*  0x34: Reserved              */
	MT2063_REG_RSVD_35,		/*  0x35: Reserved              */
	MT2063_REG_RSVD_36,		/*  0x36: Reserved              */
	MT2063_REG_RSVD_37,		/*  0x37: Reserved              */
	MT2063_REG_RSVD_38,		/*  0x38: Reserved              */
	MT2063_REG_RSVD_39,		/*  0x39: Reserved              */
	MT2063_REG_RSVD_3A,		/*  0x3A: Reserved              */
	MT2063_REG_RSVD_3B,		/*  0x3B: Reserved              */
	MT2063_REG_RSVD_3C,		/*  0x3C: Reserved              */
	MT2063_REG_END_REGS
};

struct mt2063_state {
	struct i2c_adapter *i2c;

	bool init;

	const struct mt2063_config *config;
	struct dvb_tuner_ops ops;
	struct dvb_frontend *frontend;

	u32 frequency;
	u32 srate;
	u32 bandwidth;
	u32 reference;

	u32 tuner_id;
	struct MT2063_AvoidSpursData_t AS_Data;
	u32 f_IF1_actual;
	u32 rcvr_mode;
	u32 ctfilt_sw;
	u32 CTFiltMax[31];
	u32 num_regs;
	u8 reg[MT2063_REG_END_REGS];
};

/*
 * mt2063_write - Write data into the I2C bus
 */
static int mt2063_write(struct mt2063_state *state, u8 reg, u8 *data, u32 len)
{
	struct dvb_frontend *fe = state->frontend;
	int ret;
	u8 buf[60];
	struct i2c_msg msg = {
		.addr = state->config->tuner_address,
		.flags = 0,
		.buf = buf,
		.len = len + 1
	};

	dprintk(2, "\n");

	msg.buf[0] = reg;
	memcpy(msg.buf + 1, data, len);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	ret = i2c_transfer(state->i2c, &msg, 1);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (ret < 0)
		printk(KERN_ERR "%s error ret=%d\n", __func__, ret);

	return ret;
}

/*
 * mt2063_write - Write register data into the I2C bus, caching the value
 */
static int mt2063_setreg(struct mt2063_state *state, u8 reg, u8 val)
{
	int status;

	dprintk(2, "\n");

	if (reg >= MT2063_REG_END_REGS)
		return -ERANGE;

	status = mt2063_write(state, reg, &val, 1);
	if (status < 0)
		return status;

	state->reg[reg] = val;

	return 0;
}

/*
 * mt2063_read - Read data from the I2C bus
 */
static int mt2063_read(struct mt2063_state *state,
			   u8 subAddress, u8 *pData, u32 cnt)
{
	int status = 0;	/* Status to be returned        */
	struct dvb_frontend *fe = state->frontend;
	u32 i = 0;

	dprintk(2, "addr 0x%02x, cnt %d\n", subAddress, cnt);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	for (i = 0; i < cnt; i++) {
		u8 b0[] = { subAddress + i };
		struct i2c_msg msg[] = {
			{
				.addr = state->config->tuner_address,
				.flags = 0,
				.buf = b0,
				.len = 1
			}, {
				.addr = state->config->tuner_address,
				.flags = I2C_M_RD,
				.buf = pData + i,
				.len = 1
			}
		};

		status = i2c_transfer(state->i2c, msg, 2);
		dprintk(2, "addr 0x%02x, ret = %d, val = 0x%02x\n",
			   subAddress + i, status, *(pData + i));
		if (status < 0)
			break;
	}
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (status < 0)
		printk(KERN_ERR "Can't read from address 0x%02x,\n",
		       subAddress + i);

	return status;
}

/*
 * FIXME: Is this really needed?
 */
static int MT2063_Sleep(struct dvb_frontend *fe)
{
	/*
	 *  ToDo:  Add code here to implement a OS blocking
	 */
	msleep(100);

	return 0;
}

/*
 * Microtune spur avoidance
 */

/*  Implement ceiling, floor functions.  */
#define ceil(n, d) (((n) < 0) ? (-((-(n))/(d))) : (n)/(d) + ((n)%(d) != 0))
#define floor(n, d) (((n) < 0) ? (-((-(n))/(d))) - ((n)%(d) != 0) : (n)/(d))

struct MT2063_FIFZone_t {
	s32 min_;
	s32 max_;
};

static struct MT2063_ExclZone_t *InsertNode(struct MT2063_AvoidSpursData_t
					    *pAS_Info,
					    struct MT2063_ExclZone_t *pPrevNode)
{
	struct MT2063_ExclZone_t *pNode;

	dprintk(2, "\n");

	/*  Check for a node in the free list  */
	if (pAS_Info->freeZones != NULL) {
		/*  Use one from the free list  */
		pNode = pAS_Info->freeZones;
		pAS_Info->freeZones = pNode->next_;
	} else {
		/*  Grab a node from the array  */
		pNode = &pAS_Info->MT2063_ExclZones[pAS_Info->nZones];
	}

	if (pPrevNode != NULL) {
		pNode->next_ = pPrevNode->next_;
		pPrevNode->next_ = pNode;
	} else {		/*  insert at the beginning of the list  */

		pNode->next_ = pAS_Info->usedZones;
		pAS_Info->usedZones = pNode;
	}

	pAS_Info->nZones++;
	return pNode;
}

static struct MT2063_ExclZone_t *RemoveNode(struct MT2063_AvoidSpursData_t
					    *pAS_Info,
					    struct MT2063_ExclZone_t *pPrevNode,
					    struct MT2063_ExclZone_t
					    *pNodeToRemove)
{
	struct MT2063_ExclZone_t *pNext = pNodeToRemove->next_;

	dprintk(2, "\n");

	/*  Make previous node point to the subsequent node  */
	if (pPrevNode != NULL)
		pPrevNode->next_ = pNext;

	/*  Add pNodeToRemove to the beginning of the freeZones  */
	pNodeToRemove->next_ = pAS_Info->freeZones;
	pAS_Info->freeZones = pNodeToRemove;

	/*  Decrement node count  */
	pAS_Info->nZones--;

	return pNext;
}

/*
 * MT_AddExclZone()
 *
 * Add (and merge) an exclusion zone into the list.
 * If the range (f_min, f_max) is totally outside the
 * 1st IF BW, ignore the entry.
 * If the range (f_min, f_max) is negative, ignore the entry.
 */
static void MT2063_AddExclZone(struct MT2063_AvoidSpursData_t *pAS_Info,
			       u32 f_min, u32 f_max)
{
	struct MT2063_ExclZone_t *pNode = pAS_Info->usedZones;
	struct MT2063_ExclZone_t *pPrev = NULL;
	struct MT2063_ExclZone_t *pNext = NULL;

	dprintk(2, "\n");

	/*  Check to see if this overlaps the 1st IF filter  */
	if ((f_max > (pAS_Info->f_if1_Center - (pAS_Info->f_if1_bw / 2)))
	    && (f_min < (pAS_Info->f_if1_Center + (pAS_Info->f_if1_bw / 2)))
	    && (f_min < f_max)) {
		/*
		 *                1        2         3      4       5        6
		 *
		 *   New entry:  |---|    |--|      |--|    |-|    |---|    |--|
		 *                or       or        or     or      or
		 *   Existing:  |--|      |--|      |--|    |---|  |-|      |--|
		 */

		/*  Check for our place in the list  */
		while ((pNode != NULL) && (pNode->max_ < f_min)) {
			pPrev = pNode;
			pNode = pNode->next_;
		}

		if ((pNode != NULL) && (pNode->min_ < f_max)) {
			/*  Combine me with pNode  */
			if (f_min < pNode->min_)
				pNode->min_ = f_min;
			if (f_max > pNode->max_)
				pNode->max_ = f_max;
		} else {
			pNode = InsertNode(pAS_Info, pPrev);
			pNode->min_ = f_min;
			pNode->max_ = f_max;
		}

		/*  Look for merging possibilities  */
		pNext = pNode->next_;
		while ((pNext != NULL) && (pNext->min_ < pNode->max_)) {
			if (pNext->max_ > pNode->max_)
				pNode->max_ = pNext->max_;
			/*  Remove pNext, return ptr to pNext->next  */
			pNext = RemoveNode(pAS_Info, pNode, pNext);
		}
	}
}

/*
 *  Reset all exclusion zones.
 *  Add zones to protect the PLL FracN regions near zero
 */
static void MT2063_ResetExclZones(struct MT2063_AvoidSpursData_t *pAS_Info)
{
	u32 center;

	dprintk(2, "\n");

	pAS_Info->nZones = 0;	/*  this clears the used list  */
	pAS_Info->usedZones = NULL;	/*  reset ptr                  */
	pAS_Info->freeZones = NULL;	/*  reset ptr                  */

	center =
	    pAS_Info->f_ref *
	    ((pAS_Info->f_if1_Center - pAS_Info->f_if1_bw / 2 +
	      pAS_Info->f_in) / pAS_Info->f_ref) - pAS_Info->f_in;
	while (center <
	       pAS_Info->f_if1_Center + pAS_Info->f_if1_bw / 2 +
	       pAS_Info->f_LO1_FracN_Avoid) {
		/*  Exclude LO1 FracN  */
		MT2063_AddExclZone(pAS_Info,
				   center - pAS_Info->f_LO1_FracN_Avoid,
				   center - 1);
		MT2063_AddExclZone(pAS_Info, center + 1,
				   center + pAS_Info->f_LO1_FracN_Avoid);
		center += pAS_Info->f_ref;
	}

	center =
	    pAS_Info->f_ref *
	    ((pAS_Info->f_if1_Center - pAS_Info->f_if1_bw / 2 -
	      pAS_Info->f_out) / pAS_Info->f_ref) + pAS_Info->f_out;
	while (center <
	       pAS_Info->f_if1_Center + pAS_Info->f_if1_bw / 2 +
	       pAS_Info->f_LO2_FracN_Avoid) {
		/*  Exclude LO2 FracN  */
		MT2063_AddExclZone(pAS_Info,
				   center - pAS_Info->f_LO2_FracN_Avoid,
				   center - 1);
		MT2063_AddExclZone(pAS_Info, center + 1,
				   center + pAS_Info->f_LO2_FracN_Avoid);
		center += pAS_Info->f_ref;
	}

	if (MT2063_EXCLUDE_US_DECT_FREQUENCIES(pAS_Info->avoidDECT)) {
		/*  Exclude LO1 values that conflict with DECT channels */
		MT2063_AddExclZone(pAS_Info, 1920836000 - pAS_Info->f_in, 1922236000 - pAS_Info->f_in);	/* Ctr = 1921.536 */
		MT2063_AddExclZone(pAS_Info, 1922564000 - pAS_Info->f_in, 1923964000 - pAS_Info->f_in);	/* Ctr = 1923.264 */
		MT2063_AddExclZone(pAS_Info, 1924292000 - pAS_Info->f_in, 1925692000 - pAS_Info->f_in);	/* Ctr = 1924.992 */
		MT2063_AddExclZone(pAS_Info, 1926020000 - pAS_Info->f_in, 1927420000 - pAS_Info->f_in);	/* Ctr = 1926.720 */
		MT2063_AddExclZone(pAS_Info, 1927748000 - pAS_Info->f_in, 1929148000 - pAS_Info->f_in);	/* Ctr = 1928.448 */
	}

	if (MT2063_EXCLUDE_EURO_DECT_FREQUENCIES(pAS_Info->avoidDECT)) {
		MT2063_AddExclZone(pAS_Info, 1896644000 - pAS_Info->f_in, 1898044000 - pAS_Info->f_in);	/* Ctr = 1897.344 */
		MT2063_AddExclZone(pAS_Info, 1894916000 - pAS_Info->f_in, 1896316000 - pAS_Info->f_in);	/* Ctr = 1895.616 */
		MT2063_AddExclZone(pAS_Info, 1893188000 - pAS_Info->f_in, 1894588000 - pAS_Info->f_in);	/* Ctr = 1893.888 */
		MT2063_AddExclZone(pAS_Info, 1891460000 - pAS_Info->f_in, 1892860000 - pAS_Info->f_in);	/* Ctr = 1892.16  */
		MT2063_AddExclZone(pAS_Info, 1889732000 - pAS_Info->f_in, 1891132000 - pAS_Info->f_in);	/* Ctr = 1890.432 */
		MT2063_AddExclZone(pAS_Info, 1888004000 - pAS_Info->f_in, 1889404000 - pAS_Info->f_in);	/* Ctr = 1888.704 */
		MT2063_AddExclZone(pAS_Info, 1886276000 - pAS_Info->f_in, 1887676000 - pAS_Info->f_in);	/* Ctr = 1886.976 */
		MT2063_AddExclZone(pAS_Info, 1884548000 - pAS_Info->f_in, 1885948000 - pAS_Info->f_in);	/* Ctr = 1885.248 */
		MT2063_AddExclZone(pAS_Info, 1882820000 - pAS_Info->f_in, 1884220000 - pAS_Info->f_in);	/* Ctr = 1883.52  */
		MT2063_AddExclZone(pAS_Info, 1881092000 - pAS_Info->f_in, 1882492000 - pAS_Info->f_in);	/* Ctr = 1881.792 */
	}
}

/*
 * MT_ChooseFirstIF - Choose the best available 1st IF
 *                    If f_Desired is not excluded, choose that first.
 *                    Otherwise, return the value closest to f_Center that is
 *                    not excluded
 */
static u32 MT2063_ChooseFirstIF(struct MT2063_AvoidSpursData_t *pAS_Info)
{
	/*
	 * Update "f_Desired" to be the nearest "combinational-multiple" of
	 * "f_LO1_Step".
	 * The resulting number, F_LO1 must be a multiple of f_LO1_Step.
	 * And F_LO1 is the arithmetic sum of f_in + f_Center.
	 * Neither f_in, nor f_Center must be a multiple of f_LO1_Step.
	 * However, the sum must be.
	 */
	const u32 f_Desired =
	    pAS_Info->f_LO1_Step *
	    ((pAS_Info->f_if1_Request + pAS_Info->f_in +
	      pAS_Info->f_LO1_Step / 2) / pAS_Info->f_LO1_Step) -
	    pAS_Info->f_in;
	const u32 f_Step =
	    (pAS_Info->f_LO1_Step >
	     pAS_Info->f_LO2_Step) ? pAS_Info->f_LO1_Step : pAS_Info->
	    f_LO2_Step;
	u32 f_Center;
	s32 i;
	s32 j = 0;
	u32 bDesiredExcluded = 0;
	u32 bZeroExcluded = 0;
	s32 tmpMin, tmpMax;
	s32 bestDiff;
	struct MT2063_ExclZone_t *pNode = pAS_Info->usedZones;
	struct MT2063_FIFZone_t zones[MT2063_MAX_ZONES];

	dprintk(2, "\n");

	if (pAS_Info->nZones == 0)
		return f_Desired;

	/*
	 *  f_Center needs to be an integer multiple of f_Step away
	 *  from f_Desired
	 */
	if (pAS_Info->f_if1_Center > f_Desired)
		f_Center =
		    f_Desired +
		    f_Step *
		    ((pAS_Info->f_if1_Center - f_Desired +
		      f_Step / 2) / f_Step);
	else
		f_Center =
		    f_Desired -
		    f_Step *
		    ((f_Desired - pAS_Info->f_if1_Center +
		      f_Step / 2) / f_Step);

	/*
	 * Take MT_ExclZones, center around f_Center and change the
	 * resolution to f_Step
	 */
	while (pNode != NULL) {
		/*  floor function  */
		tmpMin =
		    floor((s32) (pNode->min_ - f_Center), (s32) f_Step);

		/*  ceil function  */
		tmpMax =
		    ceil((s32) (pNode->max_ - f_Center), (s32) f_Step);

		if ((pNode->min_ < f_Desired) && (pNode->max_ > f_Desired))
			bDesiredExcluded = 1;

		if ((tmpMin < 0) && (tmpMax > 0))
			bZeroExcluded = 1;

		/*  See if this zone overlaps the previous  */
		if ((j > 0) && (tmpMin < zones[j - 1].max_))
			zones[j - 1].max_ = tmpMax;
		else {
			/*  Add new zone  */
			zones[j].min_ = tmpMin;
			zones[j].max_ = tmpMax;
			j++;
		}
		pNode = pNode->next_;
	}

	/*
	 *  If the desired is okay, return with it
	 */
	if (bDesiredExcluded == 0)
		return f_Desired;

	/*
	 *  If the desired is excluded and the center is okay, return with it
	 */
	if (bZeroExcluded == 0)
		return f_Center;

	/*  Find the value closest to 0 (f_Center)  */
	bestDiff = zones[0].min_;
	for (i = 0; i < j; i++) {
		if (abs(zones[i].min_) < abs(bestDiff))
			bestDiff = zones[i].min_;
		if (abs(zones[i].max_) < abs(bestDiff))
			bestDiff = zones[i].max_;
	}

	if (bestDiff < 0)
		return f_Center - ((u32) (-bestDiff) * f_Step);

	return f_Center + (bestDiff * f_Step);
}

/**
 * IsSpurInBand() - Checks to see if a spur will be present within the IF's
 *                  bandwidth. (fIFOut +/- fIFBW, -fIFOut +/- fIFBW)
 *
 *                    ma   mb                                     mc   md
 *                  <--+-+-+-------------------+-------------------+-+-+-->
 *                     |   ^                   0                   ^   |
 *                     ^   b=-fIFOut+fIFBW/2      -b=+fIFOut-fIFBW/2   ^
 *                     a=-fIFOut-fIFBW/2              -a=+fIFOut+fIFBW/2
 *
 *                  Note that some equations are doubled to prevent round-off
 *                  problems when calculating fIFBW/2
 *
 * @pAS_Info:	Avoid Spurs information block
 * @fm:		If spur, amount f_IF1 has to move negative
 * @fp:		If spur, amount f_IF1 has to move positive
 *
 *  Returns 1 if an LO spur would be present, otherwise 0.
 */
static u32 IsSpurInBand(struct MT2063_AvoidSpursData_t *pAS_Info,
			u32 *fm, u32 * fp)
{
	/*
	 **  Calculate LO frequency settings.
	 */
	u32 n, n0;
	const u32 f_LO1 = pAS_Info->f_LO1;
	const u32 f_LO2 = pAS_Info->f_LO2;
	const u32 d = pAS_Info->f_out + pAS_Info->f_out_bw / 2;
	const u32 c = d - pAS_Info->f_out_bw;
	const u32 f = pAS_Info->f_zif_bw / 2;
	const u32 f_Scale = (f_LO1 / (UINT_MAX / 2 / pAS_Info->maxH1)) + 1;
	s32 f_nsLO1, f_nsLO2;
	s32 f_Spur;
	u32 ma, mb, mc, md, me, mf;
	u32 lo_gcd, gd_Scale, gc_Scale, gf_Scale, hgds, hgfs, hgcs;

	dprintk(2, "\n");

	*fm = 0;

	/*
	 ** For each edge (d, c & f), calculate a scale, based on the gcd
	 ** of f_LO1, f_LO2 and the edge value.  Use the larger of this
	 ** gcd-based scale factor or f_Scale.
	 */
	lo_gcd = gcd(f_LO1, f_LO2);
	gd_Scale = max((u32) gcd(lo_gcd, d), f_Scale);
	hgds = gd_Scale / 2;
	gc_Scale = max((u32) gcd(lo_gcd, c), f_Scale);
	hgcs = gc_Scale / 2;
	gf_Scale = max((u32) gcd(lo_gcd, f), f_Scale);
	hgfs = gf_Scale / 2;

	n0 = DIV_ROUND_UP(f_LO2 - d, f_LO1 - f_LO2);

	/*  Check out all multiples of LO1 from n0 to m_maxLOSpurHarmonic  */
	for (n = n0; n <= pAS_Info->maxH1; ++n) {
		md = (n * ((f_LO1 + hgds) / gd_Scale) -
		      ((d + hgds) / gd_Scale)) / ((f_LO2 + hgds) / gd_Scale);

		/*  If # fLO2 harmonics > m_maxLOSpurHarmonic, then no spurs present  */
		if (md >= pAS_Info->maxH1)
			break;

		ma = (n * ((f_LO1 + hgds) / gd_Scale) +
		      ((d + hgds) / gd_Scale)) / ((f_LO2 + hgds) / gd_Scale);

		/*  If no spurs between +/- (f_out + f_IFBW/2), then try next harmonic  */
		if (md == ma)
			continue;

		mc = (n * ((f_LO1 + hgcs) / gc_Scale) -
		      ((c + hgcs) / gc_Scale)) / ((f_LO2 + hgcs) / gc_Scale);
		if (mc != md) {
			f_nsLO1 = (s32) (n * (f_LO1 / gc_Scale));
			f_nsLO2 = (s32) (mc * (f_LO2 / gc_Scale));
			f_Spur =
			    (gc_Scale * (f_nsLO1 - f_nsLO2)) +
			    n * (f_LO1 % gc_Scale) - mc * (f_LO2 % gc_Scale);

			*fp = ((f_Spur - (s32) c) / (mc - n)) + 1;
			*fm = (((s32) d - f_Spur) / (mc - n)) + 1;
			return 1;
		}

		/*  Location of Zero-IF-spur to be checked  */
		me = (n * ((f_LO1 + hgfs) / gf_Scale) +
		      ((f + hgfs) / gf_Scale)) / ((f_LO2 + hgfs) / gf_Scale);
		mf = (n * ((f_LO1 + hgfs) / gf_Scale) -
		      ((f + hgfs) / gf_Scale)) / ((f_LO2 + hgfs) / gf_Scale);
		if (me != mf) {
			f_nsLO1 = n * (f_LO1 / gf_Scale);
			f_nsLO2 = me * (f_LO2 / gf_Scale);
			f_Spur =
			    (gf_Scale * (f_nsLO1 - f_nsLO2)) +
			    n * (f_LO1 % gf_Scale) - me * (f_LO2 % gf_Scale);

			*fp = ((f_Spur + (s32) f) / (me - n)) + 1;
			*fm = (((s32) f - f_Spur) / (me - n)) + 1;
			return 1;
		}

		mb = (n * ((f_LO1 + hgcs) / gc_Scale) +
		      ((c + hgcs) / gc_Scale)) / ((f_LO2 + hgcs) / gc_Scale);
		if (ma != mb) {
			f_nsLO1 = n * (f_LO1 / gc_Scale);
			f_nsLO2 = ma * (f_LO2 / gc_Scale);
			f_Spur =
			    (gc_Scale * (f_nsLO1 - f_nsLO2)) +
			    n * (f_LO1 % gc_Scale) - ma * (f_LO2 % gc_Scale);

			*fp = (((s32) d + f_Spur) / (ma - n)) + 1;
			*fm = (-(f_Spur + (s32) c) / (ma - n)) + 1;
			return 1;
		}
	}

	/*  No spurs found  */
	return 0;
}

/*
 * MT_AvoidSpurs() - Main entry point to avoid spurs.
 *                   Checks for existing spurs in present LO1, LO2 freqs
 *                   and if present, chooses spur-free LO1, LO2 combination
 *                   that tunes the same input/output frequencies.
 */
static u32 MT2063_AvoidSpurs(struct MT2063_AvoidSpursData_t *pAS_Info)
{
	int status = 0;
	u32 fm, fp;		/*  restricted range on LO's        */
	pAS_Info->bSpurAvoided = 0;
	pAS_Info->nSpursFound = 0;

	dprintk(2, "\n");

	if (pAS_Info->maxH1 == 0)
		return 0;

	/*
	 * Avoid LO Generated Spurs
	 *
	 * Make sure that have no LO-related spurs within the IF output
	 * bandwidth.
	 *
	 * If there is an LO spur in this band, start at the current IF1 frequency
	 * and work out until we find a spur-free frequency or run up against the
	 * 1st IF SAW band edge.  Use temporary copies of fLO1 and fLO2 so that they
	 * will be unchanged if a spur-free setting is not found.
	 */
	pAS_Info->bSpurPresent = IsSpurInBand(pAS_Info, &fm, &fp);
	if (pAS_Info->bSpurPresent) {
		u32 zfIF1 = pAS_Info->f_LO1 - pAS_Info->f_in;	/*  current attempt at a 1st IF  */
		u32 zfLO1 = pAS_Info->f_LO1;	/*  current attempt at an LO1 freq  */
		u32 zfLO2 = pAS_Info->f_LO2;	/*  current attempt at an LO2 freq  */
		u32 delta_IF1;
		u32 new_IF1;

		/*
		 **  Spur was found, attempt to find a spur-free 1st IF
		 */
		do {
			pAS_Info->nSpursFound++;

			/*  Raise f_IF1_upper, if needed  */
			MT2063_AddExclZone(pAS_Info, zfIF1 - fm, zfIF1 + fp);

			/*  Choose next IF1 that is closest to f_IF1_CENTER              */
			new_IF1 = MT2063_ChooseFirstIF(pAS_Info);

			if (new_IF1 > zfIF1) {
				pAS_Info->f_LO1 += (new_IF1 - zfIF1);
				pAS_Info->f_LO2 += (new_IF1 - zfIF1);
			} else {
				pAS_Info->f_LO1 -= (zfIF1 - new_IF1);
				pAS_Info->f_LO2 -= (zfIF1 - new_IF1);
			}
			zfIF1 = new_IF1;

			if (zfIF1 > pAS_Info->f_if1_Center)
				delta_IF1 = zfIF1 - pAS_Info->f_if1_Center;
			else
				delta_IF1 = pAS_Info->f_if1_Center - zfIF1;

			pAS_Info->bSpurPresent = IsSpurInBand(pAS_Info, &fm, &fp);
		/*
		 *  Continue while the new 1st IF is still within the 1st IF bandwidth
		 *  and there is a spur in the band (again)
		 */
		} while ((2 * delta_IF1 + pAS_Info->f_out_bw <= pAS_Info->f_if1_bw) && pAS_Info->bSpurPresent);

		/*
		 * Use the LO-spur free values found.  If the search went all
		 * the way to the 1st IF band edge and always found spurs, just
		 * leave the original choice.  It's as "good" as any other.
		 */
		if (pAS_Info->bSpurPresent == 1) {
			status |= MT2063_SPUR_PRESENT_ERR;
			pAS_Info->f_LO1 = zfLO1;
			pAS_Info->f_LO2 = zfLO2;
		} else
			pAS_Info->bSpurAvoided = 1;
	}

	status |=
	    ((pAS_Info->
	      nSpursFound << MT2063_SPUR_SHIFT) & MT2063_SPUR_CNT_MASK);

	return status;
}

/*
 * Constants used by the tuning algorithm
 */
#define MT2063_REF_FREQ          (16000000UL)	/* Reference oscillator Frequency (in Hz) */
#define MT2063_IF1_BW            (22000000UL)	/* The IF1 filter bandwidth (in Hz) */
#define MT2063_TUNE_STEP_SIZE       (50000UL)	/* Tune in steps of 50 kHz */
#define MT2063_SPUR_STEP_HZ        (250000UL)	/* Step size (in Hz) to move IF1 when avoiding spurs */
#define MT2063_ZIF_BW             (2000000UL)	/* Zero-IF spur-free bandwidth (in Hz) */
#define MT2063_MAX_HARMONICS_1         (15UL)	/* Highest intra-tuner LO Spur Harmonic to be avoided */
#define MT2063_MAX_HARMONICS_2          (5UL)	/* Highest inter-tuner LO Spur Harmonic to be avoided */
#define MT2063_MIN_LO_SEP         (1000000UL)	/* Minimum inter-tuner LO frequency separation */
#define MT2063_LO1_FRACN_AVOID          (0UL)	/* LO1 FracN numerator avoid region (in Hz) */
#define MT2063_LO2_FRACN_AVOID     (199999UL)	/* LO2 FracN numerator avoid region (in Hz) */
#define MT2063_MIN_FIN_FREQ      (44000000UL)	/* Minimum input frequency (in Hz) */
#define MT2063_MAX_FIN_FREQ    (1100000000UL)	/* Maximum input frequency (in Hz) */
#define MT2063_MIN_FOUT_FREQ     (36000000UL)	/* Minimum output frequency (in Hz) */
#define MT2063_MAX_FOUT_FREQ     (57000000UL)	/* Maximum output frequency (in Hz) */
#define MT2063_MIN_DNC_FREQ    (1293000000UL)	/* Minimum LO2 frequency (in Hz) */
#define MT2063_MAX_DNC_FREQ    (1614000000UL)	/* Maximum LO2 frequency (in Hz) */
#define MT2063_MIN_UPC_FREQ    (1396000000UL)	/* Minimum LO1 frequency (in Hz) */
#define MT2063_MAX_UPC_FREQ    (2750000000UL)	/* Maximum LO1 frequency (in Hz) */

/*
 *  Define the supported Part/Rev codes for the MT2063
 */
#define MT2063_B0       (0x9B)
#define MT2063_B1       (0x9C)
#define MT2063_B2       (0x9D)
#define MT2063_B3       (0x9E)

/**
 * mt2063_lockStatus - Checks to see if LO1 and LO2 are locked
 *
 * @state:	struct mt2063_state pointer
 *
 * This function returns 0, if no lock, 1 if locked and a value < 1 if error
 */
static int mt2063_lockStatus(struct mt2063_state *state)
{
	const u32 nMaxWait = 100;	/*  wait a maximum of 100 msec   */
	const u32 nPollRate = 2;	/*  poll status bits every 2 ms */
	const u32 nMaxLoops = nMaxWait / nPollRate;
	const u8 LO1LK = 0x80;
	u8 LO2LK = 0x08;
	int status;
	u32 nDelays = 0;

	dprintk(2, "\n");

	/*  LO2 Lock bit was in a different place for B0 version  */
	if (state->tuner_id == MT2063_B0)
		LO2LK = 0x40;

	do {
		status = mt2063_read(state, MT2063_REG_LO_STATUS,
				     &state->reg[MT2063_REG_LO_STATUS], 1);

		if (status < 0)
			return status;

		if ((state->reg[MT2063_REG_LO_STATUS] & (LO1LK | LO2LK)) ==
		    (LO1LK | LO2LK)) {
			return TUNER_STATUS_LOCKED | TUNER_STATUS_STEREO;
		}
		msleep(nPollRate);	/*  Wait between retries  */
	} while (++nDelays < nMaxLoops);

	/*
	 * Got no lock or partial lock
	 */
	return 0;
}

/*
 *  Constants for setting receiver modes.
 *  (6 modes defined at this time, enumerated by mt2063_delivery_sys)
 *  (DNC1GC & DNC2GC are the values, which are used, when the specific
 *   DNC Output is selected, the other is always off)
 *
 *                enum mt2063_delivery_sys
 * -------------+----------------------------------------------
 * Mode 0 :     | MT2063_CABLE_QAM
 * Mode 1 :     | MT2063_CABLE_ANALOG
 * Mode 2 :     | MT2063_OFFAIR_COFDM
 * Mode 3 :     | MT2063_OFFAIR_COFDM_SAWLESS
 * Mode 4 :     | MT2063_OFFAIR_ANALOG
 * Mode 5 :     | MT2063_OFFAIR_8VSB
 * --------------+----------------------------------------------
 *
 *                |<----------   Mode  -------------->|
 *    Reg Field   |  0  |  1  |  2  |  3  |  4  |  5  |
 *    ------------+-----+-----+-----+-----+-----+-----+
 *    RFAGCen     | OFF | OFF | OFF | OFF | OFF | OFF
 *    LNARin      |   0 |   0 |   3 |   3 |  3  |  3
 *    FIFFQen     |   1 |   1 |   1 |   1 |  1  |  1
 *    FIFFq       |   0 |   0 |   0 |   0 |  0  |  0
 *    DNC1gc      |   0 |   0 |   0 |   0 |  0  |  0
 *    DNC2gc      |   0 |   0 |   0 |   0 |  0  |  0
 *    GCU Auto    |   1 |   1 |   1 |   1 |  1  |  1
 *    LNA max Atn |  31 |  31 |  31 |  31 | 31  | 31
 *    LNA Target  |  44 |  43 |  43 |  43 | 43  | 43
 *    ign  RF Ovl |   0 |   0 |   0 |   0 |  0  |  0
 *    RF  max Atn |  31 |  31 |  31 |  31 | 31  | 31
 *    PD1 Target  |  36 |  36 |  38 |  38 | 36  | 38
 *    ign FIF Ovl |   0 |   0 |   0 |   0 |  0  |  0
 *    FIF max Atn |   5 |   5 |   5 |   5 |  5  |  5
 *    PD2 Target  |  40 |  33 |  42 |  42 | 33  | 42
 */

enum mt2063_delivery_sys {
	MT2063_CABLE_QAM = 0,
	MT2063_CABLE_ANALOG,
	MT2063_OFFAIR_COFDM,
	MT2063_OFFAIR_COFDM_SAWLESS,
	MT2063_OFFAIR_ANALOG,
	MT2063_OFFAIR_8VSB,
	MT2063_NUM_RCVR_MODES
};

static const char *mt2063_mode_name[] = {
	[MT2063_CABLE_QAM]		= "digital cable",
	[MT2063_CABLE_ANALOG]		= "analog cable",
	[MT2063_OFFAIR_COFDM]		= "digital offair",
	[MT2063_OFFAIR_COFDM_SAWLESS]	= "digital offair without SAW",
	[MT2063_OFFAIR_ANALOG]		= "analog offair",
	[MT2063_OFFAIR_8VSB]		= "analog offair 8vsb",
};

static const u8 RFAGCEN[]	= {  0,  0,  0,  0,  0,  0 };
static const u8 LNARIN[]	= {  0,  0,  3,  3,  3,  3 };
static const u8 FIFFQEN[]	= {  1,  1,  1,  1,  1,  1 };
static const u8 FIFFQ[]		= {  0,  0,  0,  0,  0,  0 };
static const u8 DNC1GC[]	= {  0,  0,  0,  0,  0,  0 };
static const u8 DNC2GC[]	= {  0,  0,  0,  0,  0,  0 };
static const u8 ACLNAMAX[]	= { 31, 31, 31, 31, 31, 31 };
static const u8 LNATGT[]	= { 44, 43, 43, 43, 43, 43 };
static const u8 RFOVDIS[]	= {  0,  0,  0,  0,  0,  0 };
static const u8 ACRFMAX[]	= { 31, 31, 31, 31, 31, 31 };
static const u8 PD1TGT[]	= { 36, 36, 38, 38, 36, 38 };
static const u8 FIFOVDIS[]	= {  0,  0,  0,  0,  0,  0 };
static const u8 ACFIFMAX[]	= { 29, 29, 29, 29, 29, 29 };
static const u8 PD2TGT[]	= { 40, 33, 38, 42, 30, 38 };

/*
 * mt2063_set_dnc_output_enable()
 */
static u32 mt2063_get_dnc_output_enable(struct mt2063_state *state,
					enum MT2063_DNC_Output_Enable *pValue)
{
	dprintk(2, "\n");

	if ((state->reg[MT2063_REG_DNC_GAIN] & 0x03) == 0x03) {	/* if DNC1 is off */
		if ((state->reg[MT2063_REG_VGA_GAIN] & 0x03) == 0x03)	/* if DNC2 is off */
			*pValue = MT2063_DNC_NONE;
		else
			*pValue = MT2063_DNC_2;
	} else {	/* DNC1 is on */
		if ((state->reg[MT2063_REG_VGA_GAIN] & 0x03) == 0x03)	/* if DNC2 is off */
			*pValue = MT2063_DNC_1;
		else
			*pValue = MT2063_DNC_BOTH;
	}
	return 0;
}

/*
 * mt2063_set_dnc_output_enable()
 */
static u32 mt2063_set_dnc_output_enable(struct mt2063_state *state,
					enum MT2063_DNC_Output_Enable nValue)
{
	int status = 0;	/* Status to be returned        */
	u8 val = 0;

	dprintk(2, "\n");

	/* selects, which DNC output is used */
	switch (nValue) {
	case MT2063_DNC_NONE:
		val = (state->reg[MT2063_REG_DNC_GAIN] & 0xFC) | 0x03;	/* Set DNC1GC=3 */
		if (state->reg[MT2063_REG_DNC_GAIN] !=
		    val)
			status |=
			    mt2063_setreg(state,
					  MT2063_REG_DNC_GAIN,
					  val);

		val = (state->reg[MT2063_REG_VGA_GAIN] & 0xFC) | 0x03;	/* Set DNC2GC=3 */
		if (state->reg[MT2063_REG_VGA_GAIN] !=
		    val)
			status |=
			    mt2063_setreg(state,
					  MT2063_REG_VGA_GAIN,
					  val);

		val = (state->reg[MT2063_REG_RSVD_20] & ~0x40);	/* Set PD2MUX=0 */
		if (state->reg[MT2063_REG_RSVD_20] !=
		    val)
			status |=
			    mt2063_setreg(state,
					  MT2063_REG_RSVD_20,
					  val);

		break;
	case MT2063_DNC_1:
		val = (state->reg[MT2063_REG_DNC_GAIN] & 0xFC) | (DNC1GC[state->rcvr_mode] & 0x03);	/* Set DNC1GC=x */
		if (state->reg[MT2063_REG_DNC_GAIN] !=
		    val)
			status |=
			    mt2063_setreg(state,
					  MT2063_REG_DNC_GAIN,
					  val);

		val = (state->reg[MT2063_REG_VGA_GAIN] & 0xFC) | 0x03;	/* Set DNC2GC=3 */
		if (state->reg[MT2063_REG_VGA_GAIN] !=
		    val)
			status |=
			    mt2063_setreg(state,
					  MT2063_REG_VGA_GAIN,
					  val);

		val = (state->reg[MT2063_REG_RSVD_20] & ~0x40);	/* Set PD2MUX=0 */
		if (state->reg[MT2063_REG_RSVD_20] !=
		    val)
			status |=
			    mt2063_setreg(state,
					  MT2063_REG_RSVD_20,
					  val);

		break;
	case MT2063_DNC_2:
		val = (state->reg[MT2063_REG_DNC_GAIN] & 0xFC) | 0x03;	/* Set DNC1GC=3 */
		if (state->reg[MT2063_REG_DNC_GAIN] !=
		    val)
			status |=
			    mt2063_setreg(state,
					  MT2063_REG_DNC_GAIN,
					  val);

		val = (state->reg[MT2063_REG_VGA_GAIN] & 0xFC) | (DNC2GC[state->rcvr_mode] & 0x03);	/* Set DNC2GC=x */
		if (state->reg[MT2063_REG_VGA_GAIN] !=
		    val)
			status |=
			    mt2063_setreg(state,
					  MT2063_REG_VGA_GAIN,
					  val);

		val = (state->reg[MT2063_REG_RSVD_20] | 0x40);	/* Set PD2MUX=1 */
		if (state->reg[MT2063_REG_RSVD_20] !=
		    val)
			status |=
			    mt2063_setreg(state,
					  MT2063_REG_RSVD_20,
					  val);

		break;
	case MT2063_DNC_BOTH:
		val = (state->reg[MT2063_REG_DNC_GAIN] & 0xFC) | (DNC1GC[state->rcvr_mode] & 0x03);	/* Set DNC1GC=x */
		if (state->reg[MT2063_REG_DNC_GAIN] !=
		    val)
			status |=
			    mt2063_setreg(state,
					  MT2063_REG_DNC_GAIN,
					  val);

		val = (state->reg[MT2063_REG_VGA_GAIN] & 0xFC) | (DNC2GC[state->rcvr_mode] & 0x03);	/* Set DNC2GC=x */
		if (state->reg[MT2063_REG_VGA_GAIN] !=
		    val)
			status |=
			    mt2063_setreg(state,
					  MT2063_REG_VGA_GAIN,
					  val);

		val = (state->reg[MT2063_REG_RSVD_20] | 0x40);	/* Set PD2MUX=1 */
		if (state->reg[MT2063_REG_RSVD_20] !=
		    val)
			status |=
			    mt2063_setreg(state,
					  MT2063_REG_RSVD_20,
					  val);

		break;
	default:
		break;
	}

	return status;
}

/*
 * MT2063_SetReceiverMode() - Set the MT2063 receiver mode, according with
 *			      the selected enum mt2063_delivery_sys type.
 *
 *  (DNC1GC & DNC2GC are the values, which are used, when the specific
 *   DNC Output is selected, the other is always off)
 *
 * @state:	ptr to mt2063_state structure
 * @Mode:	desired receiver delivery system
 *
 * Note: Register cache must be valid for it to work
 */

static u32 MT2063_SetReceiverMode(struct mt2063_state *state,
				  enum mt2063_delivery_sys Mode)
{
	int status = 0;	/* Status to be returned        */
	u8 val;
	u32 longval;

	dprintk(2, "\n");

	if (Mode >= MT2063_NUM_RCVR_MODES)
		status = -ERANGE;

	/* RFAGCen */
	if (status >= 0) {
		val =
		    (state->
		     reg[MT2063_REG_PD1_TGT] & ~0x40) | (RFAGCEN[Mode]
								   ? 0x40 :
								   0x00);
		if (state->reg[MT2063_REG_PD1_TGT] != val)
			status |= mt2063_setreg(state, MT2063_REG_PD1_TGT, val);
	}

	/* LNARin */
	if (status >= 0) {
		u8 val = (state->reg[MT2063_REG_CTRL_2C] & ~0x03) |
			 (LNARIN[Mode] & 0x03);
		if (state->reg[MT2063_REG_CTRL_2C] != val)
			status |= mt2063_setreg(state, MT2063_REG_CTRL_2C, val);
	}

	/* FIFFQEN and FIFFQ */
	if (status >= 0) {
		val =
		    (state->
		     reg[MT2063_REG_FIFF_CTRL2] & ~0xF0) |
		    (FIFFQEN[Mode] << 7) | (FIFFQ[Mode] << 4);
		if (state->reg[MT2063_REG_FIFF_CTRL2] != val) {
			status |=
			    mt2063_setreg(state, MT2063_REG_FIFF_CTRL2, val);
			/* trigger FIFF calibration, needed after changing FIFFQ */
			val =
			    (state->reg[MT2063_REG_FIFF_CTRL] | 0x01);
			status |=
			    mt2063_setreg(state, MT2063_REG_FIFF_CTRL, val);
			val =
			    (state->
			     reg[MT2063_REG_FIFF_CTRL] & ~0x01);
			status |=
			    mt2063_setreg(state, MT2063_REG_FIFF_CTRL, val);
		}
	}

	/* DNC1GC & DNC2GC */
	status |= mt2063_get_dnc_output_enable(state, &longval);
	status |= mt2063_set_dnc_output_enable(state, longval);

	/* acLNAmax */
	if (status >= 0) {
		u8 val = (state->reg[MT2063_REG_LNA_OV] & ~0x1F) |
			 (ACLNAMAX[Mode] & 0x1F);
		if (state->reg[MT2063_REG_LNA_OV] != val)
			status |= mt2063_setreg(state, MT2063_REG_LNA_OV, val);
	}

	/* LNATGT */
	if (status >= 0) {
		u8 val = (state->reg[MT2063_REG_LNA_TGT] & ~0x3F) |
			 (LNATGT[Mode] & 0x3F);
		if (state->reg[MT2063_REG_LNA_TGT] != val)
			status |= mt2063_setreg(state, MT2063_REG_LNA_TGT, val);
	}

	/* ACRF */
	if (status >= 0) {
		u8 val = (state->reg[MT2063_REG_RF_OV] & ~0x1F) |
			 (ACRFMAX[Mode] & 0x1F);
		if (state->reg[MT2063_REG_RF_OV] != val)
			status |= mt2063_setreg(state, MT2063_REG_RF_OV, val);
	}

	/* PD1TGT */
	if (status >= 0) {
		u8 val = (state->reg[MT2063_REG_PD1_TGT] & ~0x3F) |
			 (PD1TGT[Mode] & 0x3F);
		if (state->reg[MT2063_REG_PD1_TGT] != val)
			status |= mt2063_setreg(state, MT2063_REG_PD1_TGT, val);
	}

	/* FIFATN */
	if (status >= 0) {
		u8 val = ACFIFMAX[Mode];
		if (state->reg[MT2063_REG_PART_REV] != MT2063_B3 && val > 5)
			val = 5;
		val = (state->reg[MT2063_REG_FIF_OV] & ~0x1F) |
		      (val & 0x1F);
		if (state->reg[MT2063_REG_FIF_OV] != val)
			status |= mt2063_setreg(state, MT2063_REG_FIF_OV, val);
	}

	/* PD2TGT */
	if (status >= 0) {
		u8 val = (state->reg[MT2063_REG_PD2_TGT] & ~0x3F) |
		    (PD2TGT[Mode] & 0x3F);
		if (state->reg[MT2063_REG_PD2_TGT] != val)
			status |= mt2063_setreg(state, MT2063_REG_PD2_TGT, val);
	}

	/* Ignore ATN Overload */
	if (status >= 0) {
		val = (state->reg[MT2063_REG_LNA_TGT] & ~0x80) |
		      (RFOVDIS[Mode] ? 0x80 : 0x00);
		if (state->reg[MT2063_REG_LNA_TGT] != val)
			status |= mt2063_setreg(state, MT2063_REG_LNA_TGT, val);
	}

	/* Ignore FIF Overload */
	if (status >= 0) {
		val = (state->reg[MT2063_REG_PD1_TGT] & ~0x80) |
		      (FIFOVDIS[Mode] ? 0x80 : 0x00);
		if (state->reg[MT2063_REG_PD1_TGT] != val)
			status |= mt2063_setreg(state, MT2063_REG_PD1_TGT, val);
	}

	if (status >= 0) {
		state->rcvr_mode = Mode;
		dprintk(1, "mt2063 mode changed to %s\n",
			mt2063_mode_name[state->rcvr_mode]);
	}

	return status;
}

/*
 * MT2063_ClearPowerMaskBits () - Clears the power-down mask bits for various
 *				  sections of the MT2063
 *
 * @Bits:		Mask bits to be cleared.
 *
 * See definition of MT2063_Mask_Bits type for description
 * of each of the power bits.
 */
static u32 MT2063_ClearPowerMaskBits(struct mt2063_state *state,
				     enum MT2063_Mask_Bits Bits)
{
	int status = 0;

	dprintk(2, "\n");
	Bits = (enum MT2063_Mask_Bits)(Bits & MT2063_ALL_SD);	/* Only valid bits for this tuner */
	if ((Bits & 0xFF00) != 0) {
		state->reg[MT2063_REG_PWR_2] &= ~(u8) (Bits >> 8);
		status |=
		    mt2063_write(state,
				    MT2063_REG_PWR_2,
				    &state->reg[MT2063_REG_PWR_2], 1);
	}
	if ((Bits & 0xFF) != 0) {
		state->reg[MT2063_REG_PWR_1] &= ~(u8) (Bits & 0xFF);
		status |=
		    mt2063_write(state,
				    MT2063_REG_PWR_1,
				    &state->reg[MT2063_REG_PWR_1], 1);
	}

	return status;
}

/*
 * MT2063_SoftwareShutdown() - Enables or disables software shutdown function.
 *			       When Shutdown is 1, any section whose power
 *			       mask is set will be shutdown.
 */
static u32 MT2063_SoftwareShutdown(struct mt2063_state *state, u8 Shutdown)
{
	int status;

	dprintk(2, "\n");
	if (Shutdown == 1)
		state->reg[MT2063_REG_PWR_1] |= 0x04;
	else
		state->reg[MT2063_REG_PWR_1] &= ~0x04;

	status = mt2063_write(state,
			    MT2063_REG_PWR_1,
			    &state->reg[MT2063_REG_PWR_1], 1);

	if (Shutdown != 1) {
		state->reg[MT2063_REG_BYP_CTRL] =
		    (state->reg[MT2063_REG_BYP_CTRL] & 0x9F) | 0x40;
		status |=
		    mt2063_write(state,
				    MT2063_REG_BYP_CTRL,
				    &state->reg[MT2063_REG_BYP_CTRL],
				    1);
		state->reg[MT2063_REG_BYP_CTRL] =
		    (state->reg[MT2063_REG_BYP_CTRL] & 0x9F);
		status |=
		    mt2063_write(state,
				    MT2063_REG_BYP_CTRL,
				    &state->reg[MT2063_REG_BYP_CTRL],
				    1);
	}

	return status;
}

static u32 MT2063_Round_fLO(u32 f_LO, u32 f_LO_Step, u32 f_ref)
{
	return f_ref * (f_LO / f_ref)
	    + f_LO_Step * (((f_LO % f_ref) + (f_LO_Step / 2)) / f_LO_Step);
}

/**
 * fLO_FractionalTerm() - Calculates the portion contributed by FracN / denom.
 *                        This function preserves maximum precision without
 *                        risk of overflow.  It accurately calculates
 *                        f_ref * num / denom to within 1 HZ with fixed math.
 *
 * @f_ref:	SRO frequency.
 * @num:	Fractional portion of the multiplier
 * @denom:	denominator portion of the ratio
 *
 * This calculation handles f_ref as two separate 14-bit fields.
 * Therefore, a maximum value of 2^28-1 may safely be used for f_ref.
 * This is the genesis of the magic number "14" and the magic mask value of
 * 0x03FFF.
 *
 * This routine successfully handles denom values up to and including 2^18.
 *  Returns:        f_ref * num / denom
 */
static u32 MT2063_fLO_FractionalTerm(u32 f_ref, u32 num, u32 denom)
{
	u32 t1 = (f_ref >> 14) * num;
	u32 term1 = t1 / denom;
	u32 loss = t1 % denom;
	u32 term2 =
	    (((f_ref & 0x00003FFF) * num + (loss << 14)) + (denom / 2)) / denom;
	return (term1 << 14) + term2;
}

/*
 * CalcLO1Mult()- Calculates Integer divider value and the numerator
 *                value for a FracN PLL.
 *
 *                This function assumes that the f_LO and f_Ref are
 *                evenly divisible by f_LO_Step.
 *
 * @Div:	OUTPUT: Whole number portion of the multiplier
 * @FracN:	OUTPUT: Fractional portion of the multiplier
 * @f_LO:	desired LO frequency.
 * @f_LO_Step:	Minimum step size for the LO (in Hz).
 * @f_Ref:	SRO frequency.
 * @f_Avoid:	Range of PLL frequencies to avoid near integer multiples
 *		of f_Ref (in Hz).
 *
 * Returns:        Recalculated LO frequency.
 */
static u32 MT2063_CalcLO1Mult(u32 *Div,
			      u32 *FracN,
			      u32 f_LO,
			      u32 f_LO_Step, u32 f_Ref)
{
	/*  Calculate the whole number portion of the divider */
	*Div = f_LO / f_Ref;

	/*  Calculate the numerator value (round to nearest f_LO_Step) */
	*FracN =
	    (64 * (((f_LO % f_Ref) + (f_LO_Step / 2)) / f_LO_Step) +
	     (f_Ref / f_LO_Step / 2)) / (f_Ref / f_LO_Step);

	return (f_Ref * (*Div)) + MT2063_fLO_FractionalTerm(f_Ref, *FracN, 64);
}

/**
 * CalcLO2Mult() - Calculates Integer divider value and the numerator
 *                 value for a FracN PLL.
 *
 *                  This function assumes that the f_LO and f_Ref are
 *                  evenly divisible by f_LO_Step.
 *
 * @Div:	OUTPUT: Whole number portion of the multiplier
 * @FracN:	OUTPUT: Fractional portion of the multiplier
 * @f_LO:	desired LO frequency.
 * @f_LO_Step:	Minimum step size for the LO (in Hz).
 * @f_Ref:	SRO frequency.
 *
 * Returns: Recalculated LO frequency.
 */
static u32 MT2063_CalcLO2Mult(u32 *Div,
			      u32 *FracN,
			      u32 f_LO,
			      u32 f_LO_Step, u32 f_Ref)
{
	/*  Calculate the whole number portion of the divider */
	*Div = f_LO / f_Ref;

	/*  Calculate the numerator value (round to nearest f_LO_Step) */
	*FracN =
	    (8191 * (((f_LO % f_Ref) + (f_LO_Step / 2)) / f_LO_Step) +
	     (f_Ref / f_LO_Step / 2)) / (f_Ref / f_LO_Step);

	return (f_Ref * (*Div)) + MT2063_fLO_FractionalTerm(f_Ref, *FracN,
							    8191);
}

/*
 * FindClearTuneFilter() - Calculate the corrrect ClearTune filter to be
 *			   used for a given input frequency.
 *
 * @state:	ptr to tuner data structure
 * @f_in:	RF input center frequency (in Hz).
 *
 * Returns: ClearTune filter number (0-31)
 */
static u32 FindClearTuneFilter(struct mt2063_state *state, u32 f_in)
{
	u32 RFBand;
	u32 idx;		/*  index loop                      */

	/*
	 **  Find RF Band setting
	 */
	RFBand = 31;		/*  def when f_in > all    */
	for (idx = 0; idx < 31; ++idx) {
		if (state->CTFiltMax[idx] >= f_in) {
			RFBand = idx;
			break;
		}
	}
	return RFBand;
}

/*
 * MT2063_Tune() - Change the tuner's tuned frequency to RFin.
 */
static u32 MT2063_Tune(struct mt2063_state *state, u32 f_in)
{				/* RF input center frequency   */

	int status = 0;
	u32 LO1;		/*  1st LO register value           */
	u32 Num1;		/*  Numerator for LO1 reg. value    */
	u32 f_IF1;		/*  1st IF requested                */
	u32 LO2;		/*  2nd LO register value           */
	u32 Num2;		/*  Numerator for LO2 reg. value    */
	u32 ofLO1, ofLO2;	/*  last time's LO frequencies      */
	u8 fiffc = 0x80;	/*  FIFF center freq from tuner     */
	u32 fiffof;		/*  Offset from FIFF center freq    */
	const u8 LO1LK = 0x80;	/*  Mask for LO1 Lock bit           */
	u8 LO2LK = 0x08;	/*  Mask for LO2 Lock bit           */
	u8 val;
	u32 RFBand;

	dprintk(2, "\n");
	/*  Check the input and output frequency ranges                   */
	if ((f_in < MT2063_MIN_FIN_FREQ) || (f_in > MT2063_MAX_FIN_FREQ))
		return -EINVAL;

	if ((state->AS_Data.f_out < MT2063_MIN_FOUT_FREQ)
	    || (state->AS_Data.f_out > MT2063_MAX_FOUT_FREQ))
		return -EINVAL;

	/*
	 * Save original LO1 and LO2 register values
	 */
	ofLO1 = state->AS_Data.f_LO1;
	ofLO2 = state->AS_Data.f_LO2;

	/*
	 * Find and set RF Band setting
	 */
	if (state->ctfilt_sw == 1) {
		val = (state->reg[MT2063_REG_CTUNE_CTRL] | 0x08);
		if (state->reg[MT2063_REG_CTUNE_CTRL] != val) {
			status |=
			    mt2063_setreg(state, MT2063_REG_CTUNE_CTRL, val);
		}
		val = state->reg[MT2063_REG_CTUNE_OV];
		RFBand = FindClearTuneFilter(state, f_in);
		state->reg[MT2063_REG_CTUNE_OV] =
		    (u8) ((state->reg[MT2063_REG_CTUNE_OV] & ~0x1F)
			      | RFBand);
		if (state->reg[MT2063_REG_CTUNE_OV] != val) {
			status |=
			    mt2063_setreg(state, MT2063_REG_CTUNE_OV, val);
		}
	}

	/*
	 * Read the FIFF Center Frequency from the tuner
	 */
	if (status >= 0) {
		status |=
		    mt2063_read(state,
				   MT2063_REG_FIFFC,
				   &state->reg[MT2063_REG_FIFFC], 1);
		fiffc = state->reg[MT2063_REG_FIFFC];
	}
	/*
	 * Assign in the requested values
	 */
	state->AS_Data.f_in = f_in;
	/*  Request a 1st IF such that LO1 is on a step size */
	state->AS_Data.f_if1_Request =
	    MT2063_Round_fLO(state->AS_Data.f_if1_Request + f_in,
			     state->AS_Data.f_LO1_Step,
			     state->AS_Data.f_ref) - f_in;

	/*
	 * Calculate frequency settings.  f_IF1_FREQ + f_in is the
	 * desired LO1 frequency
	 */
	MT2063_ResetExclZones(&state->AS_Data);

	f_IF1 = MT2063_ChooseFirstIF(&state->AS_Data);

	state->AS_Data.f_LO1 =
	    MT2063_Round_fLO(f_IF1 + f_in, state->AS_Data.f_LO1_Step,
			     state->AS_Data.f_ref);

	state->AS_Data.f_LO2 =
	    MT2063_Round_fLO(state->AS_Data.f_LO1 - state->AS_Data.f_out - f_in,
			     state->AS_Data.f_LO2_Step, state->AS_Data.f_ref);

	/*
	 * Check for any LO spurs in the output bandwidth and adjust
	 * the LO settings to avoid them if needed
	 */
	status |= MT2063_AvoidSpurs(&state->AS_Data);
	/*
	 * MT_AvoidSpurs spurs may have changed the LO1 & LO2 values.
	 * Recalculate the LO frequencies and the values to be placed
	 * in the tuning registers.
	 */
	state->AS_Data.f_LO1 =
	    MT2063_CalcLO1Mult(&LO1, &Num1, state->AS_Data.f_LO1,
			       state->AS_Data.f_LO1_Step, state->AS_Data.f_ref);
	state->AS_Data.f_LO2 =
	    MT2063_Round_fLO(state->AS_Data.f_LO1 - state->AS_Data.f_out - f_in,
			     state->AS_Data.f_LO2_Step, state->AS_Data.f_ref);
	state->AS_Data.f_LO2 =
	    MT2063_CalcLO2Mult(&LO2, &Num2, state->AS_Data.f_LO2,
			       state->AS_Data.f_LO2_Step, state->AS_Data.f_ref);

	/*
	 *  Check the upconverter and downconverter frequency ranges
	 */
	if ((state->AS_Data.f_LO1 < MT2063_MIN_UPC_FREQ)
	    || (state->AS_Data.f_LO1 > MT2063_MAX_UPC_FREQ))
		status |= MT2063_UPC_RANGE;
	if ((state->AS_Data.f_LO2 < MT2063_MIN_DNC_FREQ)
	    || (state->AS_Data.f_LO2 > MT2063_MAX_DNC_FREQ))
		status |= MT2063_DNC_RANGE;
	/*  LO2 Lock bit was in a different place for B0 version  */
	if (state->tuner_id == MT2063_B0)
		LO2LK = 0x40;

	/*
	 *  If we have the same LO frequencies and we're already locked,
	 *  then skip re-programming the LO registers.
	 */
	if ((ofLO1 != state->AS_Data.f_LO1)
	    || (ofLO2 != state->AS_Data.f_LO2)
	    || ((state->reg[MT2063_REG_LO_STATUS] & (LO1LK | LO2LK)) !=
		(LO1LK | LO2LK))) {
		/*
		 * Calculate the FIFFOF register value
		 *
		 *           IF1_Actual
		 * FIFFOF = ------------ - 8 * FIFFC - 4992
		 *            f_ref/64
		 */
		fiffof =
		    (state->AS_Data.f_LO1 -
		     f_in) / (state->AS_Data.f_ref / 64) - 8 * (u32) fiffc -
		    4992;
		if (fiffof > 0xFF)
			fiffof = 0xFF;

		/*
		 * Place all of the calculated values into the local tuner
		 * register fields.
		 */
		if (status >= 0) {
			state->reg[MT2063_REG_LO1CQ_1] = (u8) (LO1 & 0xFF);	/* DIV1q */
			state->reg[MT2063_REG_LO1CQ_2] = (u8) (Num1 & 0x3F);	/* NUM1q */
			state->reg[MT2063_REG_LO2CQ_1] = (u8) (((LO2 & 0x7F) << 1)	/* DIV2q */
								   |(Num2 >> 12));	/* NUM2q (hi) */
			state->reg[MT2063_REG_LO2CQ_2] = (u8) ((Num2 & 0x0FF0) >> 4);	/* NUM2q (mid) */
			state->reg[MT2063_REG_LO2CQ_3] = (u8) (0xE0 | (Num2 & 0x000F));	/* NUM2q (lo) */

			/*
			 * Now write out the computed register values
			 * IMPORTANT: There is a required order for writing
			 *            (0x05 must follow all the others).
			 */
			status |= mt2063_write(state, MT2063_REG_LO1CQ_1, &state->reg[MT2063_REG_LO1CQ_1], 5);	/* 0x01 - 0x05 */
			if (state->tuner_id == MT2063_B0) {
				/* Re-write the one-shot bits to trigger the tune operation */
				status |= mt2063_write(state, MT2063_REG_LO2CQ_3, &state->reg[MT2063_REG_LO2CQ_3], 1);	/* 0x05 */
			}
			/* Write out the FIFF offset only if it's changing */
			if (state->reg[MT2063_REG_FIFF_OFFSET] !=
			    (u8) fiffof) {
				state->reg[MT2063_REG_FIFF_OFFSET] =
				    (u8) fiffof;
				status |=
				    mt2063_write(state,
						    MT2063_REG_FIFF_OFFSET,
						    &state->
						    reg[MT2063_REG_FIFF_OFFSET],
						    1);
			}
		}

		/*
		 * Check for LO's locking
		 */

		if (status < 0)
			return status;

		status = mt2063_lockStatus(state);
		if (status < 0)
			return status;
		if (!status)
			return -EINVAL;		/* Couldn't lock */

		/*
		 * If we locked OK, assign calculated data to mt2063_state structure
		 */
		state->f_IF1_actual = state->AS_Data.f_LO1 - f_in;
	}

	return status;
}

static const u8 MT2063B0_defaults[] = {
	/* Reg,  Value */
	0x19, 0x05,
	0x1B, 0x1D,
	0x1C, 0x1F,
	0x1D, 0x0F,
	0x1E, 0x3F,
	0x1F, 0x0F,
	0x20, 0x3F,
	0x22, 0x21,
	0x23, 0x3F,
	0x24, 0x20,
	0x25, 0x3F,
	0x27, 0xEE,
	0x2C, 0x27,	/*  bit at 0x20 is cleared below  */
	0x30, 0x03,
	0x2C, 0x07,	/*  bit at 0x20 is cleared here   */
	0x2D, 0x87,
	0x2E, 0xAA,
	0x28, 0xE1,	/*  Set the FIFCrst bit here      */
	0x28, 0xE0,	/*  Clear the FIFCrst bit here    */
	0x00
};

/* writing 0x05 0xf0 sw-resets all registers, so we write only needed changes */
static const u8 MT2063B1_defaults[] = {
	/* Reg,  Value */
	0x05, 0xF0,
	0x11, 0x10,	/* New Enable AFCsd */
	0x19, 0x05,
	0x1A, 0x6C,
	0x1B, 0x24,
	0x1C, 0x28,
	0x1D, 0x8F,
	0x1E, 0x14,
	0x1F, 0x8F,
	0x20, 0x57,
	0x22, 0x21,	/* New - ver 1.03 */
	0x23, 0x3C,	/* New - ver 1.10 */
	0x24, 0x20,	/* New - ver 1.03 */
	0x2C, 0x24,	/*  bit at 0x20 is cleared below  */
	0x2D, 0x87,	/*  FIFFQ=0  */
	0x2F, 0xF3,
	0x30, 0x0C,	/* New - ver 1.11 */
	0x31, 0x1B,	/* New - ver 1.11 */
	0x2C, 0x04,	/*  bit at 0x20 is cleared here  */
	0x28, 0xE1,	/*  Set the FIFCrst bit here      */
	0x28, 0xE0,	/*  Clear the FIFCrst bit here    */
	0x00
};

/* writing 0x05 0xf0 sw-resets all registers, so we write only needed changes */
static const u8 MT2063B3_defaults[] = {
	/* Reg,  Value */
	0x05, 0xF0,
	0x19, 0x3D,
	0x2C, 0x24,	/*  bit at 0x20 is cleared below  */
	0x2C, 0x04,	/*  bit at 0x20 is cleared here  */
	0x28, 0xE1,	/*  Set the FIFCrst bit here      */
	0x28, 0xE0,	/*  Clear the FIFCrst bit here    */
	0x00
};

static int mt2063_init(struct dvb_frontend *fe)
{
	int status;
	struct mt2063_state *state = fe->tuner_priv;
	u8 all_resets = 0xF0;	/* reset/load bits */
	const u8 *def = NULL;
	char *step;
	u32 FCRUN;
	s32 maxReads;
	u32 fcu_osc;
	u32 i;

	dprintk(2, "\n");

	state->rcvr_mode = MT2063_CABLE_QAM;

	/*  Read the Part/Rev code from the tuner */
	status = mt2063_read(state, MT2063_REG_PART_REV,
			     &state->reg[MT2063_REG_PART_REV], 1);
	if (status < 0) {
		printk(KERN_ERR "Can't read mt2063 part ID\n");
		return status;
	}

	/* Check the part/rev code */
	switch (state->reg[MT2063_REG_PART_REV]) {
	case MT2063_B0:
		step = "B0";
		break;
	case MT2063_B1:
		step = "B1";
		break;
	case MT2063_B2:
		step = "B2";
		break;
	case MT2063_B3:
		step = "B3";
		break;
	default:
		printk(KERN_ERR "mt2063: Unknown mt2063 device ID (0x%02x)\n",
		       state->reg[MT2063_REG_PART_REV]);
		return -ENODEV;	/*  Wrong tuner Part/Rev code */
	}

	/*  Check the 2nd byte of the Part/Rev code from the tuner */
	status = mt2063_read(state, MT2063_REG_RSVD_3B,
			     &state->reg[MT2063_REG_RSVD_3B], 1);

	/* b7 != 0 ==> NOT MT2063 */
	if (status < 0 || ((state->reg[MT2063_REG_RSVD_3B] & 0x80) != 0x00)) {
		printk(KERN_ERR "mt2063: Unknown part ID (0x%02x%02x)\n",
		       state->reg[MT2063_REG_PART_REV],
		       state->reg[MT2063_REG_RSVD_3B]);
		return -ENODEV;	/*  Wrong tuner Part/Rev code */
	}

	printk(KERN_INFO "mt2063: detected a mt2063 %s\n", step);

	/*  Reset the tuner  */
	status = mt2063_write(state, MT2063_REG_LO2CQ_3, &all_resets, 1);
	if (status < 0)
		return status;

	/* change all of the default values that vary from the HW reset values */
	/*  def = (state->reg[PART_REV] == MT2063_B0) ? MT2063B0_defaults : MT2063B1_defaults; */
	switch (state->reg[MT2063_REG_PART_REV]) {
	case MT2063_B3:
		def = MT2063B3_defaults;
		break;

	case MT2063_B1:
		def = MT2063B1_defaults;
		break;

	case MT2063_B0:
		def = MT2063B0_defaults;
		break;

	default:
		return -ENODEV;
		break;
	}

	while (status >= 0 && *def) {
		u8 reg = *def++;
		u8 val = *def++;
		status = mt2063_write(state, reg, &val, 1);
	}
	if (status < 0)
		return status;

	/*  Wait for FIFF location to complete.  */
	FCRUN = 1;
	maxReads = 10;
	while (status >= 0 && (FCRUN != 0) && (maxReads-- > 0)) {
		msleep(2);
		status = mt2063_read(state,
					 MT2063_REG_XO_STATUS,
					 &state->
					 reg[MT2063_REG_XO_STATUS], 1);
		FCRUN = (state->reg[MT2063_REG_XO_STATUS] & 0x40) >> 6;
	}

	if (FCRUN != 0 || status < 0)
		return -ENODEV;

	status = mt2063_read(state,
			   MT2063_REG_FIFFC,
			   &state->reg[MT2063_REG_FIFFC], 1);
	if (status < 0)
		return status;

	/* Read back all the registers from the tuner */
	status = mt2063_read(state,
				MT2063_REG_PART_REV,
				state->reg, MT2063_REG_END_REGS);
	if (status < 0)
		return status;

	/*  Initialize the tuner state.  */
	state->tuner_id = state->reg[MT2063_REG_PART_REV];
	state->AS_Data.f_ref = MT2063_REF_FREQ;
	state->AS_Data.f_if1_Center = (state->AS_Data.f_ref / 8) *
				      ((u32) state->reg[MT2063_REG_FIFFC] + 640);
	state->AS_Data.f_if1_bw = MT2063_IF1_BW;
	state->AS_Data.f_out = 43750000UL;
	state->AS_Data.f_out_bw = 6750000UL;
	state->AS_Data.f_zif_bw = MT2063_ZIF_BW;
	state->AS_Data.f_LO1_Step = state->AS_Data.f_ref / 64;
	state->AS_Data.f_LO2_Step = MT2063_TUNE_STEP_SIZE;
	state->AS_Data.maxH1 = MT2063_MAX_HARMONICS_1;
	state->AS_Data.maxH2 = MT2063_MAX_HARMONICS_2;
	state->AS_Data.f_min_LO_Separation = MT2063_MIN_LO_SEP;
	state->AS_Data.f_if1_Request = state->AS_Data.f_if1_Center;
	state->AS_Data.f_LO1 = 2181000000UL;
	state->AS_Data.f_LO2 = 1486249786UL;
	state->f_IF1_actual = state->AS_Data.f_if1_Center;
	state->AS_Data.f_in = state->AS_Data.f_LO1 - state->f_IF1_actual;
	state->AS_Data.f_LO1_FracN_Avoid = MT2063_LO1_FRACN_AVOID;
	state->AS_Data.f_LO2_FracN_Avoid = MT2063_LO2_FRACN_AVOID;
	state->num_regs = MT2063_REG_END_REGS;
	state->AS_Data.avoidDECT = MT2063_AVOID_BOTH;
	state->ctfilt_sw = 0;

	state->CTFiltMax[0] = 69230000;
	state->CTFiltMax[1] = 105770000;
	state->CTFiltMax[2] = 140350000;
	state->CTFiltMax[3] = 177110000;
	state->CTFiltMax[4] = 212860000;
	state->CTFiltMax[5] = 241130000;
	state->CTFiltMax[6] = 274370000;
	state->CTFiltMax[7] = 309820000;
	state->CTFiltMax[8] = 342450000;
	state->CTFiltMax[9] = 378870000;
	state->CTFiltMax[10] = 416210000;
	state->CTFiltMax[11] = 456500000;
	state->CTFiltMax[12] = 495790000;
	state->CTFiltMax[13] = 534530000;
	state->CTFiltMax[14] = 572610000;
	state->CTFiltMax[15] = 598970000;
	state->CTFiltMax[16] = 635910000;
	state->CTFiltMax[17] = 672130000;
	state->CTFiltMax[18] = 714840000;
	state->CTFiltMax[19] = 739660000;
	state->CTFiltMax[20] = 770410000;
	state->CTFiltMax[21] = 814660000;
	state->CTFiltMax[22] = 846950000;
	state->CTFiltMax[23] = 867820000;
	state->CTFiltMax[24] = 915980000;
	state->CTFiltMax[25] = 947450000;
	state->CTFiltMax[26] = 983110000;
	state->CTFiltMax[27] = 1021630000;
	state->CTFiltMax[28] = 1061870000;
	state->CTFiltMax[29] = 1098330000;
	state->CTFiltMax[30] = 1138990000;

	/*
	 **   Fetch the FCU osc value and use it and the fRef value to
	 **   scale all of the Band Max values
	 */

	state->reg[MT2063_REG_CTUNE_CTRL] = 0x0A;
	status = mt2063_write(state, MT2063_REG_CTUNE_CTRL,
			      &state->reg[MT2063_REG_CTUNE_CTRL], 1);
	if (status < 0)
		return status;

	/*  Read the ClearTune filter calibration value  */
	status = mt2063_read(state, MT2063_REG_FIFFC,
			     &state->reg[MT2063_REG_FIFFC], 1);
	if (status < 0)
		return status;

	fcu_osc = state->reg[MT2063_REG_FIFFC];

	state->reg[MT2063_REG_CTUNE_CTRL] = 0x00;
	status = mt2063_write(state, MT2063_REG_CTUNE_CTRL,
			      &state->reg[MT2063_REG_CTUNE_CTRL], 1);
	if (status < 0)
		return status;

	/*  Adjust each of the values in the ClearTune filter cross-over table  */
	for (i = 0; i < 31; i++)
		state->CTFiltMax[i] = (state->CTFiltMax[i] / 768) * (fcu_osc + 640);

	status = MT2063_SoftwareShutdown(state, 1);
	if (status < 0)
		return status;
	status = MT2063_ClearPowerMaskBits(state, MT2063_ALL_SD);
	if (status < 0)
		return status;

	state->init = true;

	return 0;
}

static int mt2063_get_status(struct dvb_frontend *fe, u32 *tuner_status)
{
	struct mt2063_state *state = fe->tuner_priv;
	int status;

	dprintk(2, "\n");

	if (!state->init)
		return -ENODEV;

	*tuner_status = 0;
	status = mt2063_lockStatus(state);
	if (status < 0)
		return status;
	if (status)
		*tuner_status = TUNER_STATUS_LOCKED;

	dprintk(1, "Tuner status: %d", *tuner_status);

	return 0;
}

static void mt2063_release(struct dvb_frontend *fe)
{
	struct mt2063_state *state = fe->tuner_priv;

	dprintk(2, "\n");

	fe->tuner_priv = NULL;
	kfree(state);
}

static int mt2063_set_analog_params(struct dvb_frontend *fe,
				    struct analog_parameters *params)
{
	struct mt2063_state *state = fe->tuner_priv;
	s32 pict_car;
	s32 pict2chanb_vsb;
	s32 ch_bw;
	s32 if_mid;
	s32 rcvr_mode;
	int status;

	dprintk(2, "\n");

	if (!state->init) {
		status = mt2063_init(fe);
		if (status < 0)
			return status;
	}

	switch (params->mode) {
	case V4L2_TUNER_RADIO:
		pict_car = 38900000;
		ch_bw = 8000000;
		pict2chanb_vsb = -(ch_bw / 2);
		rcvr_mode = MT2063_OFFAIR_ANALOG;
		break;
	case V4L2_TUNER_ANALOG_TV:
		rcvr_mode = MT2063_CABLE_ANALOG;
		if (params->std & ~V4L2_STD_MN) {
			pict_car = 38900000;
			ch_bw = 6000000;
			pict2chanb_vsb = -1250000;
		} else if (params->std & V4L2_STD_PAL_G) {
			pict_car = 38900000;
			ch_bw = 7000000;
			pict2chanb_vsb = -1250000;
		} else {		/* PAL/SECAM standards */
			pict_car = 38900000;
			ch_bw = 8000000;
			pict2chanb_vsb = -1250000;
		}
		break;
	default:
		return -EINVAL;
	}
	if_mid = pict_car - (pict2chanb_vsb + (ch_bw / 2));

	state->AS_Data.f_LO2_Step = 125000;	/* FIXME: probably 5000 for FM */
	state->AS_Data.f_out = if_mid;
	state->AS_Data.f_out_bw = ch_bw + 750000;
	status = MT2063_SetReceiverMode(state, rcvr_mode);
	if (status < 0)
		return status;

	dprintk(1, "Tuning to frequency: %d, bandwidth %d, foffset %d\n",
		params->frequency, ch_bw, pict2chanb_vsb);

	status = MT2063_Tune(state, (params->frequency + (pict2chanb_vsb + (ch_bw / 2))));
	if (status < 0)
		return status;

	state->frequency = params->frequency;
	return 0;
}

/*
 * As defined on EN 300 429, the DVB-C roll-off factor is 0.15.
 * So, the amount of the needed bandwidth is given by:
 *	Bw = Symbol_rate * (1 + 0.15)
 * As such, the maximum symbol rate supported by 6 MHz is given by:
 *	max_symbol_rate = 6 MHz / 1.15 = 5217391 Bauds
 */
#define MAX_SYMBOL_RATE_6MHz	5217391

static int mt2063_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct mt2063_state *state = fe->tuner_priv;
	int status;
	s32 pict_car;
	s32 pict2chanb_vsb;
	s32 ch_bw;
	s32 if_mid;
	s32 rcvr_mode;

	if (!state->init) {
		status = mt2063_init(fe);
		if (status < 0)
			return status;
	}

	dprintk(2, "\n");

	if (c->bandwidth_hz == 0)
		return -EINVAL;
	if (c->bandwidth_hz <= 6000000)
		ch_bw = 6000000;
	else if (c->bandwidth_hz <= 7000000)
		ch_bw = 7000000;
	else
		ch_bw = 8000000;

	switch (c->delivery_system) {
	case SYS_DVBT:
		rcvr_mode = MT2063_OFFAIR_COFDM;
		pict_car = 36125000;
		pict2chanb_vsb = -(ch_bw / 2);
		break;
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_C:
		rcvr_mode = MT2063_CABLE_QAM;
		pict_car = 36125000;
		pict2chanb_vsb = -(ch_bw / 2);
		break;
	default:
		return -EINVAL;
	}
	if_mid = pict_car - (pict2chanb_vsb + (ch_bw / 2));

	state->AS_Data.f_LO2_Step = 125000;	/* FIXME: probably 5000 for FM */
	state->AS_Data.f_out = if_mid;
	state->AS_Data.f_out_bw = ch_bw + 750000;
	status = MT2063_SetReceiverMode(state, rcvr_mode);
	if (status < 0)
		return status;

	dprintk(1, "Tuning to frequency: %d, bandwidth %d, foffset %d\n",
		c->frequency, ch_bw, pict2chanb_vsb);

	status = MT2063_Tune(state, (c->frequency + (pict2chanb_vsb + (ch_bw / 2))));

	if (status < 0)
		return status;

	state->frequency = c->frequency;
	return 0;
}

static int mt2063_get_if_frequency(struct dvb_frontend *fe, u32 *freq)
{
	struct mt2063_state *state = fe->tuner_priv;

	dprintk(2, "\n");

	if (!state->init)
		return -ENODEV;

	*freq = state->AS_Data.f_out;

	dprintk(1, "IF frequency: %d\n", *freq);

	return 0;
}

static int mt2063_get_bandwidth(struct dvb_frontend *fe, u32 *bw)
{
	struct mt2063_state *state = fe->tuner_priv;

	dprintk(2, "\n");

	if (!state->init)
		return -ENODEV;

	*bw = state->AS_Data.f_out_bw - 750000;

	dprintk(1, "bandwidth: %d\n", *bw);

	return 0;
}

static const struct dvb_tuner_ops mt2063_ops = {
	.info = {
		 .name = "MT2063 Silicon Tuner",
		 .frequency_min_hz  =  45 * MHz,
		 .frequency_max_hz  = 865 * MHz,
	 },

	.init = mt2063_init,
	.sleep = MT2063_Sleep,
	.get_status = mt2063_get_status,
	.set_analog_params = mt2063_set_analog_params,
	.set_params    = mt2063_set_params,
	.get_if_frequency = mt2063_get_if_frequency,
	.get_bandwidth = mt2063_get_bandwidth,
	.release = mt2063_release,
};

struct dvb_frontend *mt2063_attach(struct dvb_frontend *fe,
				   struct mt2063_config *config,
				   struct i2c_adapter *i2c)
{
	struct mt2063_state *state = NULL;

	dprintk(2, "\n");

	state = kzalloc(sizeof(struct mt2063_state), GFP_KERNEL);
	if (!state)
		return NULL;

	state->config = config;
	state->i2c = i2c;
	state->frontend = fe;
	state->reference = config->refclock / 1000;	/* kHz */
	fe->tuner_priv = state;
	fe->ops.tuner_ops = mt2063_ops;

	printk(KERN_INFO "%s: Attaching MT2063\n", __func__);
	return fe;
}
EXPORT_SYMBOL_GPL(mt2063_attach);

#if 0
/*
 * Ancillary routines visible outside mt2063
 * FIXME: Remove them in favor of using standard tuner callbacks
 */
static int tuner_MT2063_SoftwareShutdown(struct dvb_frontend *fe)
{
	struct mt2063_state *state = fe->tuner_priv;
	int err = 0;

	dprintk(2, "\n");

	err = MT2063_SoftwareShutdown(state, 1);
	if (err < 0)
		printk(KERN_ERR "%s: Couldn't shutdown\n", __func__);

	return err;
}

static int tuner_MT2063_ClearPowerMaskBits(struct dvb_frontend *fe)
{
	struct mt2063_state *state = fe->tuner_priv;
	int err = 0;

	dprintk(2, "\n");

	err = MT2063_ClearPowerMaskBits(state, MT2063_ALL_SD);
	if (err < 0)
		printk(KERN_ERR "%s: Invalid parameter\n", __func__);

	return err;
}
#endif

MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_DESCRIPTION("MT2063 Silicon tuner");
MODULE_LICENSE("GPL");
