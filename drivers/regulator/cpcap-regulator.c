/*
 * Copyright (C) 2009 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include <linux/spi/spi.h>
#include <linux/spi/cpcap.h>
#include <linux/spi/cpcap-regbits.h>

#define CPCAP_REGULATOR(_name, _id)		\
	{					\
		.name = _name,			\
		.id = _id,			\
		.ops = &cpcap_regulator_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.owner = THIS_MODULE,		\
	}


#define SW2_SW4_VAL_TBL_SIZE 69
#define SW2_SW4_VAL_TBL_STEP 12500

static int sw2_sw4_val_tbl[SW2_SW4_VAL_TBL_SIZE];
static const int sw5_val_tbl[] = {0, 5050000};
static const int vcam_val_tbl[] = {2600000, 2700000, 2800000, 2900000};
static const int vcsi_val_tbl[] = {1200000, 1800000};
static const int vdac_val_tbl[] = {1200000, 1500000, 1800000, 2500000};
static const int vdig_val_tbl[] = {1200000, 1350000, 1500000, 1875000};
static const int vfuse_val_tbl[] = {1500000, 1600000, 1700000, 1800000, 1900000,
				    2000000, 2100000, 2200000, 2300000, 2400000,
				    2500000, 2600000, 2700000, 3150000};
static const int vhvio_val_tbl[] = {2775000};
static const int vsdio_val_tbl[] = {1500000, 1600000, 1800000, 2600000,
				    2700000, 2800000, 2900000, 3000000};
static const int vpll_val_tbl[] = {1200000, 1300000, 1400000, 1800000};
static const int vrf1_val_tbl[] = {2775000, 2500000}; /* Yes, this is correct */
static const int vrf2_val_tbl[] = {0, 2775000};
static const int vrfref_val_tbl[] = {2500000, 2775000};
static const int vwlan1_val_tbl[] = {1800000, 1900000};
static const int vwlan2_val_tbl[] = {2775000, 3000000, 3300000, 3300000};
static const int vsim_val_tbl[] = {1800000, 2900000};
static const int vsimcard_val_tbl[] = {1800000, 2900000};
static const int vvib_val_tbl[] = {1300000, 1800000, 2000000, 3000000};
static const int vusb_val_tbl[] = {0, 3300000};
static const int vaudio_val_tbl[] = {0, 2775000};

static struct {
	const enum cpcap_reg reg;
	const unsigned short mode_mask;
	const unsigned short volt_mask;
	const unsigned char volt_shft;
	unsigned short mode_val;
	unsigned short off_mode_val;
	const int val_tbl_sz;
	const int *val_tbl;
	unsigned int mode_cntr;
	const unsigned int volt_trans_time; /* in micro seconds */
	const unsigned int turn_on_time; /* in micro seconds */
} cpcap_regltr_data[CPCAP_NUM_REGULATORS] = {
	[CPCAP_SW2]      = {CPCAP_REG_S2C1,
			    0x0F00,
			    0x007F,
			    0,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(sw2_sw4_val_tbl),
			    sw2_sw4_val_tbl,
			    0,
			    120,
			    1500},

	[CPCAP_SW4]      = {CPCAP_REG_S4C1,
			    0x0F00,
			    0x007F,
			    0,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(sw2_sw4_val_tbl),
			    sw2_sw4_val_tbl,
			    0,
			    100,
			    1500},

	[CPCAP_SW5]      = {CPCAP_REG_S5C,
			    0x002A,
			    0x0000,
			    0,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(sw5_val_tbl),
			    sw5_val_tbl,
			    0,
			    0,
			    1500},

	[CPCAP_VCAM]     = {CPCAP_REG_VCAMC,
			    0x0087,
			    0x0030,
			    4,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vcam_val_tbl),
			    vcam_val_tbl,
			    0,
			    420,
			    1000},

	[CPCAP_VCSI]     = {CPCAP_REG_VCSIC,
			    0x0047,
			    0x0010,
			    4,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vcsi_val_tbl),
			    vcsi_val_tbl,
			    0,
			    350,
			    1000},

	[CPCAP_VDAC]     = {CPCAP_REG_VDACC,
			    0x0087,
			    0x0030,
			    4,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vdac_val_tbl),
			    vdac_val_tbl,
			    0,
			    420,
			    1000},

	[CPCAP_VDIG]     = {CPCAP_REG_VDIGC,
			    0x0087,
			    0x0030,
			    4,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vdig_val_tbl),
			    vdig_val_tbl,
			    0,
			    420,
			    1000},

	[CPCAP_VFUSE]    = {CPCAP_REG_VFUSEC,
			    0x0080,
			    0x000F,
			    0,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vfuse_val_tbl),
			    vfuse_val_tbl,
			    0,
			    420,
			    1000},

	[CPCAP_VHVIO]    = {CPCAP_REG_VHVIOC,
			    0x0017,
			    0x0000,
			    0,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vhvio_val_tbl),
			    vhvio_val_tbl,
			    0,
			    0,
			    1000},

	[CPCAP_VSDIO]    = {CPCAP_REG_VSDIOC,
			    0x0087,
			    0x0038,
			    3,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vsdio_val_tbl),
			    vsdio_val_tbl,
			    0,
			    420,
			    1000},

	[CPCAP_VPLL]     = {CPCAP_REG_VPLLC,
			    0x0043,
			    0x0018,
			    3,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vpll_val_tbl),
			    vpll_val_tbl,
			    0,
			    420,
			    100},

	[CPCAP_VRF1]     = {CPCAP_REG_VRF1C,
			    0x00AC,
			    0x0002,
			    1,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vrf1_val_tbl),
			    vrf1_val_tbl,
			    0,
			    10,
			    1000},

	[CPCAP_VRF2]     = {CPCAP_REG_VRF2C,
			    0x0023,
			    0x0008,
			    3,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vrf2_val_tbl),
			    vrf2_val_tbl,
			    0,
			    10,
			    1000},

	[CPCAP_VRFREF]   = {CPCAP_REG_VRFREFC,
			    0x0023,
			    0x0008,
			    3,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vrfref_val_tbl),
			    vrfref_val_tbl,
			    0,
			    420,
			    100},

	[CPCAP_VWLAN1]   = {CPCAP_REG_VWLAN1C,
			    0x0047,
			    0x0010,
			    4,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vwlan1_val_tbl),
			    vwlan1_val_tbl,
			    0,
			    420,
			    1000},

	[CPCAP_VWLAN2]   = {CPCAP_REG_VWLAN2C,
			    0x020C,
			    0x00C0,
			    6,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vwlan2_val_tbl),
			    vwlan2_val_tbl,
			    0,
			    420,
			    1000},

	[CPCAP_VSIM]     = {CPCAP_REG_VSIMC,
			    0x0023,
			    0x0008,
			    3,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vsim_val_tbl),
			    vsim_val_tbl,
			    0,
			    420,
			    1000},

	[CPCAP_VSIMCARD] = {CPCAP_REG_VSIMC,
			    0x1E80,
			    0x0008,
			    3,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vsimcard_val_tbl),
			    vsimcard_val_tbl,
			    0,
			    420,
			    1000},

	[CPCAP_VVIB]     = {CPCAP_REG_VVIBC,
			    0x0001,
			    0x000C,
			    2,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vvib_val_tbl),
			    vvib_val_tbl,
			    0,
			    500,
			    500},

	[CPCAP_VUSB]     = {CPCAP_REG_VUSBC,
			    0x011C,
			    0x0040,
			    6,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vusb_val_tbl),
			    vusb_val_tbl,
			    0,
			    0,
			    1000},

	[CPCAP_VAUDIO]   = {CPCAP_REG_VAUDIOC,
			    0x0016,
			    0x0001,
			    0,
			    0x0000,
			    0x0000,
			    ARRAY_SIZE(vaudio_val_tbl),
			    vaudio_val_tbl,
			    0,
			    0,
			    1000},
};

static int cpcap_regulator_set_voltage(struct regulator_dev *rdev,
				       int min_uV, int max_uV)
{
	struct cpcap_device *cpcap;
	int regltr_id;
	int retval;
	enum cpcap_reg regnr;
	int i;

	cpcap = rdev_get_drvdata(rdev);

	regltr_id = rdev_get_id(rdev);
	if (regltr_id >= CPCAP_NUM_REGULATORS)
		return -EINVAL;

	regnr = cpcap_regltr_data[regltr_id].reg;

	if (regltr_id == CPCAP_VRF1) {
		if (min_uV > 2500000)
			i = 0;
		else
			i = cpcap_regltr_data[regltr_id].volt_mask;
	} else {
		for (i = 0; i < cpcap_regltr_data[regltr_id].val_tbl_sz; i++)
			if (cpcap_regltr_data[regltr_id].val_tbl[i] >= min_uV)
				break;

		if (i >= cpcap_regltr_data[regltr_id].val_tbl_sz)
			i--;

		i <<= cpcap_regltr_data[regltr_id].volt_shft;
	}

	retval = cpcap_regacc_write(cpcap, regnr, i,
				    cpcap_regltr_data[regltr_id].volt_mask);

	if ((cpcap_regltr_data[regltr_id].volt_trans_time) && (retval == 0))
		udelay(cpcap_regltr_data[regltr_id].volt_trans_time);

	return retval;
}

static int cpcap_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct cpcap_device *cpcap;
	int regltr_id;
	unsigned short volt_bits;
	enum cpcap_reg regnr;
	unsigned int shift;

	cpcap = rdev_get_drvdata(rdev);

	regltr_id = rdev_get_id(rdev);
	if (regltr_id >= CPCAP_NUM_REGULATORS)
		return -EINVAL;

	regnr = cpcap_regltr_data[regltr_id].reg;

	if (cpcap_regacc_read(cpcap, regnr, &volt_bits) < 0)
		return -1;

	if (!(volt_bits & cpcap_regltr_data[regltr_id].mode_mask))
		return 0;

	volt_bits &= cpcap_regltr_data[regltr_id].volt_mask;
	shift = cpcap_regltr_data[regltr_id].volt_shft;

	return cpcap_regltr_data[regltr_id].val_tbl[volt_bits >> shift];
}

static int cpcap_regulator_enable(struct regulator_dev *rdev)
{
	struct cpcap_device *cpcap = rdev_get_drvdata(rdev);
	int regltr_id;
	int retval;
	enum cpcap_reg regnr;

	regltr_id = rdev_get_id(rdev);
	if (regltr_id >= CPCAP_NUM_REGULATORS)
		return -EINVAL;

	regnr = cpcap_regltr_data[regltr_id].reg;

	retval = cpcap_regacc_write(cpcap, regnr,
				    cpcap_regltr_data[regltr_id].mode_val,
				    cpcap_regltr_data[regltr_id].mode_mask);

	if ((cpcap_regltr_data[regltr_id].turn_on_time) && (retval == 0))
		udelay(cpcap_regltr_data[regltr_id].turn_on_time);

	return retval;
}

static int cpcap_regulator_disable(struct regulator_dev *rdev)
{
	struct cpcap_device *cpcap = rdev_get_drvdata(rdev);
	int regltr_id;
	enum cpcap_reg regnr;

	regltr_id = rdev_get_id(rdev);
	if (regltr_id >= CPCAP_NUM_REGULATORS)
		return -EINVAL;

	regnr = cpcap_regltr_data[regltr_id].reg;

	return cpcap_regacc_write(cpcap, regnr,
				  cpcap_regltr_data[regltr_id].off_mode_val,
				  cpcap_regltr_data[regltr_id].mode_mask);
}

static int cpcap_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct cpcap_device *cpcap = rdev_get_drvdata(rdev);
	int regltr_id;
	enum cpcap_reg regnr;
	unsigned short value;

	regltr_id = rdev_get_id(rdev);
	if (regltr_id >= CPCAP_NUM_REGULATORS)
		return -EINVAL;

	regnr = cpcap_regltr_data[regltr_id].reg;

	if (cpcap_regacc_read(cpcap, regnr, &value))
		return -1;

	return (value & cpcap_regltr_data[regltr_id].mode_mask) ? 1 : 0;
}

static int cpcap_regulator_set_mode(struct regulator_dev *rdev,
				    unsigned int mode)
{
	struct cpcap_device *cpcap = rdev_get_drvdata(rdev);
	int regltr_id;
	enum cpcap_reg regnr;
	int ret = 0;

	regltr_id = rdev_get_id(rdev);
	if (regltr_id != CPCAP_VAUDIO)
		return -EINVAL;

	regnr = cpcap_regltr_data[regltr_id].reg;

	if (mode == REGULATOR_MODE_NORMAL) {
		if (cpcap_regltr_data[regltr_id].mode_cntr == 0) {
			ret = cpcap_regacc_write(cpcap, regnr,
						 0,
						 CPCAP_BIT_AUDIO_LOW_PWR);
		}
		if (ret == 0)
			cpcap_regltr_data[regltr_id].mode_cntr++;
	} else if (mode == REGULATOR_MODE_STANDBY) {
		if (cpcap_regltr_data[regltr_id].mode_cntr == 1) {
			ret = cpcap_regacc_write(cpcap, regnr,
						 CPCAP_BIT_AUDIO_LOW_PWR,
						 CPCAP_BIT_AUDIO_LOW_PWR);
		} else if (WARN((cpcap_regltr_data[regltr_id].mode_cntr == 0),
				"Unbalanced modes for supply vaudio\n"))
			ret = -EIO;

		if (ret == 0)
			cpcap_regltr_data[regltr_id].mode_cntr--;
	}

	return ret;
}

static struct regulator_ops cpcap_regulator_ops = {
	.set_voltage = cpcap_regulator_set_voltage,
	.get_voltage = cpcap_regulator_get_voltage,
	.enable = cpcap_regulator_enable,
	.disable = cpcap_regulator_disable,
	.is_enabled = cpcap_regulator_is_enabled,
	.set_mode = cpcap_regulator_set_mode,
};

static struct regulator_desc regulators[] = {
	[CPCAP_SW2]      = CPCAP_REGULATOR("sw2", CPCAP_SW2),
	[CPCAP_SW4]      = CPCAP_REGULATOR("sw4", CPCAP_SW4),
	[CPCAP_SW5]      = CPCAP_REGULATOR("sw5", CPCAP_SW5),
	[CPCAP_VCAM]     = CPCAP_REGULATOR("vcam", CPCAP_VCAM),
	[CPCAP_VCSI]     = CPCAP_REGULATOR("vcsi", CPCAP_VCSI),
	[CPCAP_VDAC]     = CPCAP_REGULATOR("vdac", CPCAP_VDAC),
	[CPCAP_VDIG]     = CPCAP_REGULATOR("vdig", CPCAP_VDIG),
	[CPCAP_VFUSE]    = CPCAP_REGULATOR("vfuse", CPCAP_VFUSE),
	[CPCAP_VHVIO]    = CPCAP_REGULATOR("vhvio", CPCAP_VHVIO),
	[CPCAP_VSDIO]    = CPCAP_REGULATOR("vsdio", CPCAP_VSDIO),
	[CPCAP_VPLL]     = CPCAP_REGULATOR("vpll", CPCAP_VPLL),
	[CPCAP_VRF1]     = CPCAP_REGULATOR("vrf1", CPCAP_VRF1),
	[CPCAP_VRF2]     = CPCAP_REGULATOR("vrf2", CPCAP_VRF2),
	[CPCAP_VRFREF]   = CPCAP_REGULATOR("vrfref", CPCAP_VRFREF),
	[CPCAP_VWLAN1]   = CPCAP_REGULATOR("vwlan1", CPCAP_VWLAN1),
	[CPCAP_VWLAN2]   = CPCAP_REGULATOR("vwlan2", CPCAP_VWLAN2),
	[CPCAP_VSIM]     = CPCAP_REGULATOR("vsim", CPCAP_VSIM),
	[CPCAP_VSIMCARD] = CPCAP_REGULATOR("vsimcard", CPCAP_VSIMCARD),
	[CPCAP_VVIB]     = CPCAP_REGULATOR("vvib", CPCAP_VVIB),
	[CPCAP_VUSB]     = CPCAP_REGULATOR("vusb", CPCAP_VUSB),
	[CPCAP_VAUDIO]   = CPCAP_REGULATOR("vaudio", CPCAP_VAUDIO),
};

static int __devinit cpcap_regulator_probe(struct platform_device *pdev)
{
	struct regulator_dev *rdev;
	struct cpcap_device *cpcap;
	struct cpcap_platform_data *data;
	struct regulator_init_data *init;
	int i;

	/* Already set by core driver */
	cpcap = platform_get_drvdata(pdev);
	data = cpcap->spi->controller_data;
	init = pdev->dev.platform_data;

	for (i = 0; i < CPCAP_NUM_REGULATORS; i++) {
		cpcap_regltr_data[i].mode_val = data->regulator_mode_values[i];
		cpcap_regltr_data[i].off_mode_val =
			data->regulator_off_mode_values[i];
	}

	rdev = regulator_register(&regulators[pdev->id], &pdev->dev,
				  init, cpcap);
	if (IS_ERR(rdev))
		return PTR_ERR(rdev);
	/* this is ok since the cpcap is still reachable from the rdev */
	platform_set_drvdata(pdev, rdev);

	if (pdev->id == CPCAP_SW5) {
		init = cpcap->regulator_pdev[CPCAP_VUSB]->dev.platform_data;
		init->supply_regulator_dev = rdev_get_dev(rdev);
		platform_device_add(cpcap->regulator_pdev[CPCAP_VUSB]);
	}

	return 0;
}

static int __devexit cpcap_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	regulator_unregister(rdev);

	return 0;
}

static struct platform_driver cpcap_regulator_driver = {
	.driver = {
		.name = "cpcap-regltr",
	},
	.probe = cpcap_regulator_probe,
	.remove = __devexit_p(cpcap_regulator_remove),
};

static int __init cpcap_regulator_init(void)
{
	int i;

	for (i = 0; i < SW2_SW4_VAL_TBL_SIZE; i++)
		sw2_sw4_val_tbl[i] = 600000 + (i * SW2_SW4_VAL_TBL_STEP);

	return platform_driver_register(&cpcap_regulator_driver);
}
subsys_initcall(cpcap_regulator_init);

static void __exit cpcap_regulator_exit(void)
{
	platform_driver_unregister(&cpcap_regulator_driver);
}
module_exit(cpcap_regulator_exit);

MODULE_ALIAS("platform:cpcap-regulator");
MODULE_DESCRIPTION("CPCAP regulator driver");
MODULE_AUTHOR("Motorola");
MODULE_LICENSE("GPL");
