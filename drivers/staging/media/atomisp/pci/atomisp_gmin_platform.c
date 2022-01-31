// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/dmi.h>
#include <linux/efi.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <media/v4l2-subdev.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include "../../include/linux/atomisp_platform.h"
#include "../../include/linux/atomisp_gmin_platform.h"

#define MAX_SUBDEVS 8

enum clock_rate {
	VLV2_CLK_XTAL_25_0MHz = 0,
	VLV2_CLK_PLL_19P2MHZ = 1
};

#define CLK_RATE_19_2MHZ	19200000
#define CLK_RATE_25_0MHZ	25000000

/* Valid clock number range from 0 to 5 */
#define MAX_CLK_COUNT                   5

/* X-Powers AXP288 register set */
#define ALDO1_SEL_REG	0x28
#define ALDO1_CTRL3_REG	0x13
#define ALDO1_2P8V	0x16
#define ALDO1_CTRL3_SHIFT 0x05

#define ELDO_CTRL_REG   0x12

#define ELDO1_SEL_REG	0x19
#define ELDO1_1P8V	0x16
#define ELDO1_CTRL_SHIFT 0x00

#define ELDO2_SEL_REG	0x1a
#define ELDO2_1P8V	0x16
#define ELDO2_CTRL_SHIFT 0x01

/* TI SND9039 PMIC register set */
#define LDO9_REG	0x49
#define LDO10_REG	0x4a
#define LDO11_REG	0x4b

#define LDO_2P8V_ON	0x2f /* 0x2e selects 2.85V ...      */
#define LDO_2P8V_OFF	0x2e /* ... bottom bit is "enabled" */

#define LDO_1P8V_ON	0x59 /* 0x58 selects 1.80V ...      */
#define LDO_1P8V_OFF	0x58 /* ... bottom bit is "enabled" */

/* CRYSTAL COVE PMIC register set */
#define CRYSTAL_1P8V_REG	0x57
#define CRYSTAL_2P8V_REG	0x5d
#define CRYSTAL_ON		0x63
#define CRYSTAL_OFF		0x62

struct gmin_subdev {
	struct v4l2_subdev *subdev;
	enum clock_rate clock_src;
	struct clk *pmc_clk;
	struct gpio_desc *gpio0;
	struct gpio_desc *gpio1;
	struct regulator *v1p8_reg;
	struct regulator *v2p8_reg;
	struct regulator *v1p2_reg;
	struct regulator *v2p8_vcm_reg;
	enum atomisp_camera_port csi_port;
	unsigned int csi_lanes;
	enum atomisp_input_format csi_fmt;
	enum atomisp_bayer_order csi_bayer;

	bool clock_on;
	bool v1p8_on;
	bool v2p8_on;
	bool v1p2_on;
	bool v2p8_vcm_on;

	int v1p8_gpio;
	int v2p8_gpio;

	u8 pwm_i2c_addr;

	/* For PMIC AXP */
	int eldo1_sel_reg, eldo1_1p8v, eldo1_ctrl_shift;
	int eldo2_sel_reg, eldo2_1p8v, eldo2_ctrl_shift;
};

static struct gmin_subdev gmin_subdevs[MAX_SUBDEVS];

/* ACPI HIDs for the PMICs that could be used by this driver */
#define PMIC_ACPI_AXP		"INT33F4"	/* XPower AXP288 PMIC */
#define PMIC_ACPI_TI		"INT33F5"	/* Dollar Cove TI PMIC */
#define PMIC_ACPI_CRYSTALCOVE	"INT33FD"	/* Crystal Cove PMIC */

#define PMIC_PLATFORM_TI	"intel_soc_pmic_chtdc_ti"

static enum {
	PMIC_UNSET = 0,
	PMIC_REGULATOR,
	PMIC_AXP,
	PMIC_TI,
	PMIC_CRYSTALCOVE
} pmic_id;

static const char *pmic_name[] = {
	[PMIC_UNSET]		= "ACPI device PM",
	[PMIC_REGULATOR]	= "regulator driver",
	[PMIC_AXP]		= "XPower AXP288 PMIC",
	[PMIC_TI]		= "Dollar Cove TI PMIC",
	[PMIC_CRYSTALCOVE]	= "Crystal Cove PMIC",
};

/* The atomisp uses type==0 for the end-of-list marker, so leave space. */
static struct intel_v4l2_subdev_table pdata_subdevs[MAX_SUBDEVS + 1];

static const struct atomisp_platform_data pdata = {
	.subdevs = pdata_subdevs,
};

static LIST_HEAD(vcm_devices);
static DEFINE_MUTEX(vcm_lock);

static struct gmin_subdev *find_gmin_subdev(struct v4l2_subdev *subdev);

/*
 * Legacy/stub behavior copied from upstream platform_camera.c.  The
 * atomisp driver relies on these values being non-NULL in a few
 * places, even though they are hard-coded in all current
 * implementations.
 */
const struct atomisp_camera_caps *atomisp_get_default_camera_caps(void)
{
	static const struct atomisp_camera_caps caps = {
		.sensor_num = 1,
		.sensor = {
			{ .stream_num = 1, },
		},
	};
	return &caps;
}
EXPORT_SYMBOL_GPL(atomisp_get_default_camera_caps);

const struct atomisp_platform_data *atomisp_get_platform_data(void)
{
	return &pdata;
}
EXPORT_SYMBOL_GPL(atomisp_get_platform_data);

int atomisp_register_i2c_module(struct v4l2_subdev *subdev,
				struct camera_sensor_platform_data *plat_data,
				enum intel_v4l2_subdev_type type)
{
	int i;
	struct i2c_board_info *bi;
	struct gmin_subdev *gs;
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct acpi_device *adev = ACPI_COMPANION(&client->dev);

	dev_info(&client->dev, "register atomisp i2c module type %d\n", type);

	/* The windows driver model (and thus most BIOSes by default)
	 * uses ACPI runtime power management for camera devices, but
	 * we don't.  Disable it, or else the rails will be needlessly
	 * tickled during suspend/resume.  This has caused power and
	 * performance issues on multiple devices.
	 */
	adev->power.flags.power_resources = 0;

	for (i = 0; i < MAX_SUBDEVS; i++)
		if (!pdata.subdevs[i].type)
			break;

	if (pdata.subdevs[i].type)
		return -ENOMEM;

	/* Note subtlety of initialization order: at the point where
	 * this registration API gets called, the platform data
	 * callbacks have probably already been invoked, so the
	 * gmin_subdev struct is already initialized for us.
	 */
	gs = find_gmin_subdev(subdev);
	if (!gs)
		return -ENODEV;

	pdata.subdevs[i].type = type;
	pdata.subdevs[i].port = gs->csi_port;
	pdata.subdevs[i].subdev = subdev;
	pdata.subdevs[i].v4l2_subdev.i2c_adapter_id = client->adapter->nr;

	/* Convert i2c_client to i2c_board_info */
	bi = &pdata.subdevs[i].v4l2_subdev.board_info;
	memcpy(bi->type, client->name, I2C_NAME_SIZE);
	bi->flags = client->flags;
	bi->addr = client->addr;
	bi->irq = client->irq;
	bi->platform_data = plat_data;

	return 0;
}
EXPORT_SYMBOL_GPL(atomisp_register_i2c_module);

struct v4l2_subdev *atomisp_gmin_find_subdev(struct i2c_adapter *adapter,
	struct i2c_board_info *board_info)
{
	int i;

	for (i = 0; i < MAX_SUBDEVS && pdata.subdevs[i].type; i++) {
		struct intel_v4l2_subdev_table *sd = &pdata.subdevs[i];

		if (sd->v4l2_subdev.i2c_adapter_id == adapter->nr &&
		    sd->v4l2_subdev.board_info.addr == board_info->addr)
			return sd->subdev;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(atomisp_gmin_find_subdev);

int atomisp_gmin_remove_subdev(struct v4l2_subdev *sd)
{
	int i, j;

	if (!sd)
		return 0;

	for (i = 0; i < MAX_SUBDEVS; i++) {
		if (pdata.subdevs[i].subdev == sd) {
			for (j = i + 1; j <= MAX_SUBDEVS; j++)
				pdata.subdevs[j - 1] = pdata.subdevs[j];
		}
		if (gmin_subdevs[i].subdev == sd) {
			if (gmin_subdevs[i].gpio0)
				gpiod_put(gmin_subdevs[i].gpio0);
			gmin_subdevs[i].gpio0 = NULL;
			if (gmin_subdevs[i].gpio1)
				gpiod_put(gmin_subdevs[i].gpio1);
			gmin_subdevs[i].gpio1 = NULL;
			if (pmic_id == PMIC_REGULATOR) {
				regulator_put(gmin_subdevs[i].v1p8_reg);
				regulator_put(gmin_subdevs[i].v2p8_reg);
				regulator_put(gmin_subdevs[i].v1p2_reg);
				regulator_put(gmin_subdevs[i].v2p8_vcm_reg);
			}
			gmin_subdevs[i].subdev = NULL;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(atomisp_gmin_remove_subdev);

struct gmin_cfg_var {
	const char *name, *val;
};

static struct gmin_cfg_var ffrd8_vars[] = {
	{ "INTCF1B:00_ImxId",    "0x134" },
	{ "INTCF1B:00_CsiPort",  "1" },
	{ "INTCF1B:00_CsiLanes", "4" },
	{ "INTCF1B:00_CamClk", "0" },
	{},
};

/* Cribbed from MCG defaults in the mt9m114 driver, not actually verified
 * vs. T100 hardware
 */
static struct gmin_cfg_var t100_vars[] = {
	{ "INT33F0:00_CsiPort",  "0" },
	{ "INT33F0:00_CsiLanes", "1" },
	{ "INT33F0:00_CamClk",   "1" },
	{},
};

static struct gmin_cfg_var mrd7_vars[] = {
	{"INT33F8:00_CamType", "1"},
	{"INT33F8:00_CsiPort", "1"},
	{"INT33F8:00_CsiLanes", "2"},
	{"INT33F8:00_CsiFmt", "13"},
	{"INT33F8:00_CsiBayer", "0"},
	{"INT33F8:00_CamClk", "0"},

	{"INT33F9:00_CamType", "1"},
	{"INT33F9:00_CsiPort", "0"},
	{"INT33F9:00_CsiLanes", "1"},
	{"INT33F9:00_CsiFmt", "13"},
	{"INT33F9:00_CsiBayer", "0"},
	{"INT33F9:00_CamClk", "1"},
	{},
};

static struct gmin_cfg_var ecs7_vars[] = {
	{"INT33BE:00_CsiPort", "1"},
	{"INT33BE:00_CsiLanes", "2"},
	{"INT33BE:00_CsiFmt", "13"},
	{"INT33BE:00_CsiBayer", "2"},
	{"INT33BE:00_CamClk", "0"},

	{"INT33F0:00_CsiPort", "0"},
	{"INT33F0:00_CsiLanes", "1"},
	{"INT33F0:00_CsiFmt", "13"},
	{"INT33F0:00_CsiBayer", "0"},
	{"INT33F0:00_CamClk", "1"},
	{"gmin_V2P8GPIO", "402"},
	{},
};

static struct gmin_cfg_var i8880_vars[] = {
	{"XXOV2680:00_CsiPort", "1"},
	{"XXOV2680:00_CsiLanes", "1"},
	{"XXOV2680:00_CamClk", "0"},

	{"XXGC0310:00_CsiPort", "0"},
	{"XXGC0310:00_CsiLanes", "1"},
	{"XXGC0310:00_CamClk", "1"},
	{},
};

static const struct dmi_system_id gmin_vars[] = {
	{
		.ident = "BYT-T FFD8",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "BYT-T FFD8"),
		},
		.driver_data = ffrd8_vars,
	},
	{
		.ident = "T100TA",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "T100TA"),
		},
		.driver_data = t100_vars,
	},
	{
		.ident = "MRD7",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "TABLET"),
			DMI_MATCH(DMI_BOARD_VERSION, "MRD 7"),
		},
		.driver_data = mrd7_vars,
	},
	{
		.ident = "ST70408",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "ST70408"),
		},
		.driver_data = ecs7_vars,
	},
	{
		.ident = "VTA0803",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "VTA0803"),
		},
		.driver_data = i8880_vars,
	},
	{}
};

#define GMIN_CFG_VAR_EFI_GUID EFI_GUID(0xecb54cd9, 0xe5ae, 0x4fdc, \
				       0xa9, 0x71, 0xe8, 0x77,	   \
				       0x75, 0x60, 0x68, 0xf7)

static const guid_t atomisp_dsm_guid = GUID_INIT(0xdc2f6c4f, 0x045b, 0x4f1d,
						 0x97, 0xb9, 0x88, 0x2a,
						 0x68, 0x60, 0xa4, 0xbe);

#define CFG_VAR_NAME_MAX 64

#define GMIN_PMC_CLK_NAME 14 /* "pmc_plt_clk_[0..5]" */
static char gmin_pmc_clk_name[GMIN_PMC_CLK_NAME];

static struct i2c_client *gmin_i2c_dev_exists(struct device *dev, char *name,
					      struct i2c_client **client)
{
	struct acpi_device *adev;
	struct device *d;

	adev = acpi_dev_get_first_match_dev(name, NULL, -1);
	if (!adev)
		return NULL;

	d = bus_find_device_by_acpi_dev(&i2c_bus_type, adev);
	acpi_dev_put(adev);
	if (!d)
		return NULL;

	*client = i2c_verify_client(d);
	put_device(d);

	dev_dbg(dev, "found '%s' at address 0x%02x, adapter %d\n",
		(*client)->name, (*client)->addr, (*client)->adapter->nr);
	return *client;
}

static int gmin_i2c_write(struct device *dev, u16 i2c_addr, u8 reg,
			  u32 value, u32 mask)
{
	int ret;

	/*
	 * FIXME: Right now, the intel_pmic driver just write values
	 * directly at the regmap, instead of properly implementing
	 * i2c_transfer() mechanism. Let's use the same interface here,
	 * as otherwise we may face issues.
	 */

	dev_dbg(dev,
		"I2C write, addr: 0x%02x, reg: 0x%02x, value: 0x%02x, mask: 0x%02x\n",
		i2c_addr, reg, value, mask);

	ret = intel_soc_pmic_exec_mipi_pmic_seq_element(i2c_addr, reg, value, mask);
	if (ret == -EOPNOTSUPP)
		dev_err(dev,
			"ACPI didn't mapped the OpRegion needed to access I2C address 0x%02x.\n"
			"Need to compile the kernel using CONFIG_*_PMIC_OPREGION settings\n",
			i2c_addr);

	return ret;
}

static int atomisp_get_acpi_power(struct device *dev)
{
	char name[5];
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer b_name = { sizeof(name), name };
	union acpi_object *package, *element;
	acpi_handle handle = ACPI_HANDLE(dev);
	acpi_handle rhandle;
	acpi_status status;
	int clock_num = -1;
	int i;

	status = acpi_evaluate_object(handle, "_PR0", NULL, &buffer);
	if (!ACPI_SUCCESS(status))
		return -1;

	package = buffer.pointer;

	if (!buffer.length || !package
	    || package->type != ACPI_TYPE_PACKAGE
	    || !package->package.count)
		goto fail;

	for (i = 0; i < package->package.count; i++) {
		element = &package->package.elements[i];

		if (element->type != ACPI_TYPE_LOCAL_REFERENCE)
			continue;

		rhandle = element->reference.handle;
		if (!rhandle)
			goto fail;

		acpi_get_name(rhandle, ACPI_SINGLE_NAME, &b_name);

		dev_dbg(dev, "Found PM resource '%s'\n", name);
		if (strlen(name) == 4 && !strncmp(name, "CLK", 3)) {
			if (name[3] >= '0' && name[3] <= '4')
				clock_num = name[3] - '0';
#if 0
			/*
			 * We could abort here, but let's parse all resources,
			 * as this is helpful for debugging purposes
			 */
			if (clock_num >= 0)
				break;
#endif
		}
	}

fail:
	ACPI_FREE(buffer.pointer);

	return clock_num;
}

static u8 gmin_get_pmic_id_and_addr(struct device *dev)
{
	struct i2c_client *power = NULL;
	static u8 pmic_i2c_addr;

	if (pmic_id)
		return pmic_i2c_addr;

	if (gmin_i2c_dev_exists(dev, PMIC_ACPI_TI, &power))
		pmic_id = PMIC_TI;
	else if (gmin_i2c_dev_exists(dev, PMIC_ACPI_AXP, &power))
		pmic_id = PMIC_AXP;
	else if (gmin_i2c_dev_exists(dev, PMIC_ACPI_CRYSTALCOVE, &power))
		pmic_id = PMIC_CRYSTALCOVE;
	else
		pmic_id = PMIC_REGULATOR;

	pmic_i2c_addr = power ? power->addr : 0;
	return pmic_i2c_addr;
}

static int gmin_detect_pmic(struct v4l2_subdev *subdev)
{
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct device *dev = &client->dev;
	u8 pmic_i2c_addr;

	pmic_i2c_addr = gmin_get_pmic_id_and_addr(dev);
	dev_info(dev, "gmin: power management provided via %s (i2c addr 0x%02x)\n",
		 pmic_name[pmic_id], pmic_i2c_addr);
	return pmic_i2c_addr;
}

static int gmin_subdev_add(struct gmin_subdev *gs)
{
	struct i2c_client *client = v4l2_get_subdevdata(gs->subdev);
	struct device *dev = &client->dev;
	struct acpi_device *adev = ACPI_COMPANION(dev);
	int ret, clock_num = -1;

	dev_info(dev, "%s: ACPI path is %pfw\n", __func__, dev_fwnode(dev));

	/*WA:CHT requires XTAL clock as PLL is not stable.*/
	gs->clock_src = gmin_get_var_int(dev, false, "ClkSrc",
				         VLV2_CLK_PLL_19P2MHZ);

	gs->csi_port = gmin_get_var_int(dev, false, "CsiPort", 0);
	gs->csi_lanes = gmin_get_var_int(dev, false, "CsiLanes", 1);

	gs->gpio0 = gpiod_get_index(dev, NULL, 0, GPIOD_OUT_LOW);
	if (IS_ERR(gs->gpio0))
		gs->gpio0 = NULL;
	else
		dev_info(dev, "will handle gpio0 via ACPI\n");

	gs->gpio1 = gpiod_get_index(dev, NULL, 1, GPIOD_OUT_LOW);
	if (IS_ERR(gs->gpio1))
		gs->gpio1 = NULL;
	else
		dev_info(dev, "will handle gpio1 via ACPI\n");

	/*
	 * Those are used only when there is an external regulator apart
	 * from the PMIC that would be providing power supply, like on the
	 * two cases below:
	 *
	 * The ECS E7 board drives camera 2.8v from an external regulator
	 * instead of the PMIC.  There's a gmin_CamV2P8 config variable
	 * that specifies the GPIO to handle this particular case,
	 * but this needs a broader architecture for handling camera power.
	 *
	 * The CHT RVP board drives camera 1.8v from an* external regulator
	 * instead of the PMIC just like ECS E7 board.
	 */

	gs->v1p8_gpio = gmin_get_var_int(dev, true, "V1P8GPIO", -1);
	gs->v2p8_gpio = gmin_get_var_int(dev, true, "V2P8GPIO", -1);

	/*
	 * FIXME:
	 *
	 * The ACPI handling code checks for the _PR? tables in order to
	 * know what is required to switch the device from power state
	 * D0 (_PR0) up to D3COLD (_PR3).
	 *
	 * The adev->flags.power_manageable is set to true if the device
	 * has a _PR0 table, which can be checked by calling
	 * acpi_device_power_manageable(adev).
	 *
	 * However, this only says that the device can be set to power off
	 * mode.
	 *
	 * At least on the DSDT tables we've seen so far, there's no _PR3,
	 * nor _PS3 (which would have a somewhat similar effect).
	 * So, using ACPI for power management won't work, except if adding
	 * an ACPI override logic somewhere.
	 *
	 * So, at least for the existing devices we know, the check below
	 * will always be false.
	 */
	if (acpi_device_can_wakeup(adev) &&
	    acpi_device_can_poweroff(adev)) {
		dev_info(dev,
			 "gmin: power management provided via device PM\n");
		return 0;
	}

	/*
	 * The code below is here due to backward compatibility with devices
	 * whose ACPI BIOS may not contain everything that would be needed
	 * in order to set clocks and do power management.
	 */

	/*
	 * According with :
	 *   https://github.com/projectceladon/hardware-intel-kernelflinger/blob/master/doc/fastboot.md
	 *
	 * The "CamClk" EFI var is set via fastboot on some Android devices,
	 * and seems to contain the number of the clock used to feed the
	 * sensor.
	 *
	 * On systems with a proper ACPI table, this is given via the _PR0
	 * power resource table. The logic below should first check if there
	 * is a power resource already, falling back to the EFI vars detection
	 * otherwise.
	 */

	/* Try first to use ACPI to get the clock resource */
	if (acpi_device_power_manageable(adev))
		clock_num = atomisp_get_acpi_power(dev);

	/* Fall-back use EFI and/or DMI match */
	if (clock_num < 0)
		clock_num = gmin_get_var_int(dev, false, "CamClk", 0);

	if (clock_num < 0 || clock_num > MAX_CLK_COUNT) {
		dev_err(dev, "Invalid clock number\n");
		return -EINVAL;
	}

	snprintf(gmin_pmc_clk_name, sizeof(gmin_pmc_clk_name),
		 "%s_%d", "pmc_plt_clk", clock_num);

	gs->pmc_clk = devm_clk_get(dev, gmin_pmc_clk_name);
	if (IS_ERR(gs->pmc_clk)) {
		ret = PTR_ERR(gs->pmc_clk);
		dev_err(dev, "Failed to get clk from %s: %d\n", gmin_pmc_clk_name, ret);
		return ret;
	}
	dev_info(dev, "Will use CLK%d (%s)\n", clock_num, gmin_pmc_clk_name);

	/*
	 * The firmware might enable the clock at
	 * boot (this information may or may not
	 * be reflected in the enable clock register).
	 * To change the rate we must disable the clock
	 * first to cover these cases. Due to common
	 * clock framework restrictions that do not allow
	 * to disable a clock that has not been enabled,
	 * we need to enable the clock first.
	 */
	ret = clk_prepare_enable(gs->pmc_clk);
	if (!ret)
		clk_disable_unprepare(gs->pmc_clk);

	switch (pmic_id) {
	case PMIC_REGULATOR:
		gs->v1p8_reg = regulator_get(dev, "V1P8SX");
		gs->v2p8_reg = regulator_get(dev, "V2P8SX");

		gs->v1p2_reg = regulator_get(dev, "V1P2A");
		gs->v2p8_vcm_reg = regulator_get(dev, "VPROG4B");

		/* Note: ideally we would initialize v[12]p8_on to the
		 * output of regulator_is_enabled(), but sadly that
		 * API is broken with the current drivers, returning
		 * "1" for a regulator that will then emit a
		 * "unbalanced disable" WARNing if we try to disable
		 * it.
		 */
		break;

	case PMIC_AXP:
		gs->eldo1_1p8v = gmin_get_var_int(dev, false,
						  "eldo1_1p8v",
						  ELDO1_1P8V);
		gs->eldo1_sel_reg = gmin_get_var_int(dev, false,
						     "eldo1_sel_reg",
						     ELDO1_SEL_REG);
		gs->eldo1_ctrl_shift = gmin_get_var_int(dev, false,
							"eldo1_ctrl_shift",
							ELDO1_CTRL_SHIFT);
		gs->eldo2_1p8v = gmin_get_var_int(dev, false,
						  "eldo2_1p8v",
						  ELDO2_1P8V);
		gs->eldo2_sel_reg = gmin_get_var_int(dev, false,
						     "eldo2_sel_reg",
						     ELDO2_SEL_REG);
		gs->eldo2_ctrl_shift = gmin_get_var_int(dev, false,
							"eldo2_ctrl_shift",
							ELDO2_CTRL_SHIFT);
		break;

	default:
		break;
	}

	return 0;
}

static struct gmin_subdev *find_gmin_subdev(struct v4l2_subdev *subdev)
{
	int i;

	for (i = 0; i < MAX_SUBDEVS; i++)
		if (gmin_subdevs[i].subdev == subdev)
			return &gmin_subdevs[i];
	return NULL;
}

static struct gmin_subdev *find_free_gmin_subdev_slot(void)
{
	unsigned int i;

	for (i = 0; i < MAX_SUBDEVS; i++)
		if (gmin_subdevs[i].subdev == NULL)
			return &gmin_subdevs[i];
	return NULL;
}

static int axp_regulator_set(struct device *dev, struct gmin_subdev *gs,
			     int sel_reg, u8 setting,
			     int ctrl_reg, int shift, bool on)
{
	int ret;
	int val;

	ret = gmin_i2c_write(dev, gs->pwm_i2c_addr, sel_reg, setting, 0xff);
	if (ret)
		return ret;

	val = on ? 1 << shift : 0;

	ret = gmin_i2c_write(dev, gs->pwm_i2c_addr, sel_reg, val, 1 << shift);
	if (ret)
		return ret;

	return 0;
}

static int axp_v1p8_on(struct device *dev, struct gmin_subdev *gs)
{
	int ret;

	ret = axp_regulator_set(dev, gs, gs->eldo2_sel_reg, gs->eldo2_1p8v,
				ELDO_CTRL_REG, gs->eldo2_ctrl_shift, true);
	if (ret)
		return ret;

	/*
	 * This sleep comes out of the gc2235 driver, which is the
	 * only one I currently see that wants to set both 1.8v rails.
	 */
	usleep_range(110, 150);

	ret = axp_regulator_set(dev, gs, gs->eldo1_sel_reg, gs->eldo1_1p8v,
		ELDO_CTRL_REG, gs->eldo1_ctrl_shift, true);
	if (ret)
		return ret;

	ret = axp_regulator_set(dev, gs, gs->eldo2_sel_reg, gs->eldo2_1p8v,
				ELDO_CTRL_REG, gs->eldo2_ctrl_shift, false);
	return ret;
}

static int axp_v1p8_off(struct device *dev, struct gmin_subdev *gs)
{
	int ret;

	ret = axp_regulator_set(dev, gs, gs->eldo1_sel_reg, gs->eldo1_1p8v,
				ELDO_CTRL_REG, gs->eldo1_ctrl_shift, false);
	if (ret)
		return ret;

	ret = axp_regulator_set(dev, gs, gs->eldo2_sel_reg, gs->eldo2_1p8v,
				ELDO_CTRL_REG, gs->eldo2_ctrl_shift, false);
	return ret;
}

static int gmin_gpio0_ctrl(struct v4l2_subdev *subdev, int on)
{
	struct gmin_subdev *gs = find_gmin_subdev(subdev);

	if (gs) {
		gpiod_set_value(gs->gpio0, on);
		return 0;
	}
	return -EINVAL;
}

static int gmin_gpio1_ctrl(struct v4l2_subdev *subdev, int on)
{
	struct gmin_subdev *gs = find_gmin_subdev(subdev);

	if (gs) {
		gpiod_set_value(gs->gpio1, on);
		return 0;
	}
	return -EINVAL;
}

static int gmin_v1p2_ctrl(struct v4l2_subdev *subdev, int on)
{
	struct gmin_subdev *gs = find_gmin_subdev(subdev);

	if (!gs || gs->v1p2_on == on)
		return 0;
	gs->v1p2_on = on;

	/* use regulator for PMIC */
	if (gs->v1p2_reg) {
		if (on)
			return regulator_enable(gs->v1p2_reg);
		else
			return regulator_disable(gs->v1p2_reg);
	}

	/* TODO:v1p2 may need to extend to other PMICs */

	return -EINVAL;
}

static int gmin_v1p8_ctrl(struct v4l2_subdev *subdev, int on)
{
	struct gmin_subdev *gs = find_gmin_subdev(subdev);
	int ret;
	int value;

	if (!gs || gs->v1p8_on == on)
		return 0;

	if (gs->v1p8_gpio >= 0) {
		pr_info("atomisp_gmin_platform: 1.8v power on GPIO %d\n",
			gs->v1p8_gpio);
		ret = gpio_request(gs->v1p8_gpio, "camera_v1p8_en");
		if (!ret)
			ret = gpio_direction_output(gs->v1p8_gpio, 0);
		if (ret)
			pr_err("V1P8 GPIO initialization failed\n");
	}

	gs->v1p8_on = on;

	if (gs->v1p8_gpio >= 0)
		gpio_set_value(gs->v1p8_gpio, on);

	if (gs->v1p8_reg) {
		regulator_set_voltage(gs->v1p8_reg, 1800000, 1800000);
		if (on)
			return regulator_enable(gs->v1p8_reg);
		else
			return regulator_disable(gs->v1p8_reg);
	}

	switch (pmic_id) {
	case PMIC_AXP:
		if (on)
			return axp_v1p8_on(subdev->dev, gs);
		else
			return axp_v1p8_off(subdev->dev, gs);
	case PMIC_TI:
		value = on ? LDO_1P8V_ON : LDO_1P8V_OFF;

		return gmin_i2c_write(subdev->dev, gs->pwm_i2c_addr,
				      LDO10_REG, value, 0xff);
	case PMIC_CRYSTALCOVE:
		value = on ? CRYSTAL_ON : CRYSTAL_OFF;

		return gmin_i2c_write(subdev->dev, gs->pwm_i2c_addr,
				      CRYSTAL_1P8V_REG, value, 0xff);
	default:
		dev_err(subdev->dev, "Couldn't set power mode for v1p2\n");
	}

	return -EINVAL;
}

static int gmin_v2p8_ctrl(struct v4l2_subdev *subdev, int on)
{
	struct gmin_subdev *gs = find_gmin_subdev(subdev);
	int ret;
	int value;

	if (WARN_ON(!gs))
		return -ENODEV;

	if (gs->v2p8_gpio >= 0) {
		pr_info("atomisp_gmin_platform: 2.8v power on GPIO %d\n",
			gs->v2p8_gpio);
		ret = gpio_request(gs->v2p8_gpio, "camera_v2p8");
		if (!ret)
			ret = gpio_direction_output(gs->v2p8_gpio, 0);
		if (ret)
			pr_err("V2P8 GPIO initialization failed\n");
	}

	if (gs->v2p8_on == on)
		return 0;
	gs->v2p8_on = on;

	if (gs->v2p8_gpio >= 0)
		gpio_set_value(gs->v2p8_gpio, on);

	if (gs->v2p8_reg) {
		regulator_set_voltage(gs->v2p8_reg, 2900000, 2900000);
		if (on)
			return regulator_enable(gs->v2p8_reg);
		else
			return regulator_disable(gs->v2p8_reg);
	}

	switch (pmic_id) {
	case PMIC_AXP:
		return axp_regulator_set(subdev->dev, gs, ALDO1_SEL_REG,
					 ALDO1_2P8V, ALDO1_CTRL3_REG,
					 ALDO1_CTRL3_SHIFT, on);
	case PMIC_TI:
		value = on ? LDO_2P8V_ON : LDO_2P8V_OFF;

		return gmin_i2c_write(subdev->dev, gs->pwm_i2c_addr,
				      LDO9_REG, value, 0xff);
	case PMIC_CRYSTALCOVE:
		value = on ? CRYSTAL_ON : CRYSTAL_OFF;

		return gmin_i2c_write(subdev->dev, gs->pwm_i2c_addr,
				      CRYSTAL_2P8V_REG, value, 0xff);
	default:
		dev_err(subdev->dev, "Couldn't set power mode for v1p2\n");
	}

	return -EINVAL;
}

static int gmin_acpi_pm_ctrl(struct v4l2_subdev *subdev, int on)
{
	int ret = 0;
	struct gmin_subdev *gs = find_gmin_subdev(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct acpi_device *adev = ACPI_COMPANION(&client->dev);

	/* Use the ACPI power management to control it */
	on = !!on;
	if (gs->clock_on == on)
		return 0;

	dev_dbg(subdev->dev, "Setting power state to %s\n",
		on ? "on" : "off");

	if (on)
		ret = acpi_device_set_power(adev,
					    ACPI_STATE_D0);
	else
		ret = acpi_device_set_power(adev,
					    ACPI_STATE_D3_COLD);

	if (!ret)
		gs->clock_on = on;
	else
		dev_err(subdev->dev, "Couldn't set power state to %s\n",
			on ? "on" : "off");

	return ret;
}

static int gmin_flisclk_ctrl(struct v4l2_subdev *subdev, int on)
{
	int ret = 0;
	struct gmin_subdev *gs = find_gmin_subdev(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);

	if (gs->clock_on == !!on)
		return 0;

	if (on) {
		ret = clk_set_rate(gs->pmc_clk,
				   gs->clock_src ? CLK_RATE_19_2MHZ : CLK_RATE_25_0MHZ);

		if (ret)
			dev_err(&client->dev, "unable to set PMC rate %d\n",
				gs->clock_src);

		ret = clk_prepare_enable(gs->pmc_clk);
		if (ret == 0)
			gs->clock_on = true;
	} else {
		clk_disable_unprepare(gs->pmc_clk);
		gs->clock_on = false;
	}

	return ret;
}

static int gmin_csi_cfg(struct v4l2_subdev *sd, int flag)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gmin_subdev *gs = find_gmin_subdev(sd);

	if (!client || !gs)
		return -ENODEV;

	return camera_sensor_csi(sd, gs->csi_port, gs->csi_lanes,
				 gs->csi_fmt, gs->csi_bayer, flag);
}

static struct camera_vcm_control *gmin_get_vcm_ctrl(struct v4l2_subdev *subdev,
	char *camera_module)
{
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct gmin_subdev *gs = find_gmin_subdev(subdev);
	struct camera_vcm_control *vcm;

	if (!client || !gs)
		return NULL;

	if (!camera_module)
		return NULL;

	mutex_lock(&vcm_lock);
	list_for_each_entry(vcm, &vcm_devices, list) {
		if (!strcmp(camera_module, vcm->camera_module)) {
			mutex_unlock(&vcm_lock);
			return vcm;
		}
	}

	mutex_unlock(&vcm_lock);
	return NULL;
}

static struct camera_sensor_platform_data pmic_gmin_plat = {
	.gpio0_ctrl = gmin_gpio0_ctrl,
	.gpio1_ctrl = gmin_gpio1_ctrl,
	.v1p8_ctrl = gmin_v1p8_ctrl,
	.v2p8_ctrl = gmin_v2p8_ctrl,
	.v1p2_ctrl = gmin_v1p2_ctrl,
	.flisclk_ctrl = gmin_flisclk_ctrl,
	.csi_cfg = gmin_csi_cfg,
	.get_vcm_ctrl = gmin_get_vcm_ctrl,
};

static struct camera_sensor_platform_data acpi_gmin_plat = {
	.gpio0_ctrl = gmin_gpio0_ctrl,
	.gpio1_ctrl = gmin_gpio1_ctrl,
	.v1p8_ctrl = gmin_acpi_pm_ctrl,
	.v2p8_ctrl = gmin_acpi_pm_ctrl,
	.v1p2_ctrl = gmin_acpi_pm_ctrl,
	.flisclk_ctrl = gmin_acpi_pm_ctrl,
	.csi_cfg = gmin_csi_cfg,
	.get_vcm_ctrl = gmin_get_vcm_ctrl,
};

struct camera_sensor_platform_data *
gmin_camera_platform_data(struct v4l2_subdev *subdev,
			  enum atomisp_input_format csi_format,
			  enum atomisp_bayer_order csi_bayer)
{
	u8 pmic_i2c_addr = gmin_detect_pmic(subdev);
	struct gmin_subdev *gs;

	gs = find_free_gmin_subdev_slot();
	gs->subdev = subdev;
	gs->csi_fmt = csi_format;
	gs->csi_bayer = csi_bayer;
	gs->pwm_i2c_addr = pmic_i2c_addr;

	gmin_subdev_add(gs);
	if (gs->pmc_clk)
		return &pmic_gmin_plat;
	else
		return &acpi_gmin_plat;
}
EXPORT_SYMBOL_GPL(gmin_camera_platform_data);

int atomisp_gmin_register_vcm_control(struct camera_vcm_control *vcmCtrl)
{
	if (!vcmCtrl)
		return -EINVAL;

	mutex_lock(&vcm_lock);
	list_add_tail(&vcmCtrl->list, &vcm_devices);
	mutex_unlock(&vcm_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(atomisp_gmin_register_vcm_control);

static int gmin_get_hardcoded_var(struct device *dev,
				  struct gmin_cfg_var *varlist,
				  const char *var8, char *out, size_t *out_len)
{
	struct gmin_cfg_var *gv;

	for (gv = varlist; gv->name; gv++) {
		size_t vl;

		if (strcmp(var8, gv->name))
			continue;

		dev_info(dev, "Found DMI entry for '%s'\n", var8);

		vl = strlen(gv->val);
		if (vl > *out_len - 1)
			return -ENOSPC;

		strscpy(out, gv->val, *out_len);
		*out_len = vl;
		return 0;
	}

	return -EINVAL;
}


static int gmin_get_config_dsm_var(struct device *dev,
				   const char *var,
				   char *out, size_t *out_len)
{
	acpi_handle handle = ACPI_HANDLE(dev);
	union acpi_object *obj, *cur = NULL;
	int i;

	/*
	 * The data reported by "CamClk" seems to be either 0 or 1 at the
	 * _DSM table.
	 *
	 * At the ACPI tables we looked so far, this is not related to the
	 * actual clock source for the sensor, which is given by the
	 * _PR0 ACPI table. So, ignore it, as otherwise this will be
	 * set to a wrong value.
	 */
	if (!strcmp(var, "CamClk"))
		return -EINVAL;

	obj = acpi_evaluate_dsm(handle, &atomisp_dsm_guid, 0, 0, NULL);
	if (!obj) {
		dev_info_once(dev, "Didn't find ACPI _DSM table.\n");
		return -EINVAL;
	}

	/* Return on unexpected object type */
	if (obj->type != ACPI_TYPE_PACKAGE)
		return -EINVAL;

#if 0 /* Just for debugging purposes */
	for (i = 0; i < obj->package.count; i++) {
		union acpi_object *cur = &obj->package.elements[i];

		if (cur->type == ACPI_TYPE_INTEGER)
			dev_info(dev, "object #%d, type %d, value: %lld\n",
				 i, cur->type, cur->integer.value);
		else if (cur->type == ACPI_TYPE_STRING)
			dev_info(dev, "object #%d, type %d, string: %s\n",
				 i, cur->type, cur->string.pointer);
		else
			dev_info(dev, "object #%d, type %d\n",
				 i, cur->type);
	}
#endif

	/* Seek for the desired var */
	for (i = 0; i < obj->package.count - 1; i += 2) {
		if (obj->package.elements[i].type == ACPI_TYPE_STRING &&
		    !strcmp(obj->package.elements[i].string.pointer, var)) {
			/* Next element should be the required value */
			cur = &obj->package.elements[i + 1];
			break;
		}
	}

	if (!cur) {
		dev_info(dev, "didn't found _DSM entry for '%s'\n", var);
		ACPI_FREE(obj);
		return -EINVAL;
	}

	/*
	 * While it could be possible to have an ACPI_TYPE_INTEGER,
	 * and read the value from cur->integer.value, the table
	 * seen so far uses the string type. So, produce a warning
	 * if it founds something different than string, letting it
	 * to fall back to the old code.
	 */
	if (cur && cur->type != ACPI_TYPE_STRING) {
		dev_info(dev, "found non-string _DSM entry for '%s'\n", var);
		ACPI_FREE(obj);
		return -EINVAL;
	}

	dev_info(dev, "found _DSM entry for '%s': %s\n", var,
		 cur->string.pointer);
	strscpy(out, cur->string.pointer, *out_len);
	*out_len = strlen(cur->string.pointer);

	ACPI_FREE(obj);
	return 0;
}

/* Retrieves a device-specific configuration variable.  The dev
 * argument should be a device with an ACPI companion, as all
 * configuration is based on firmware ID.
 */
static int gmin_get_config_var(struct device *maindev,
			       bool is_gmin,
			       const char *var,
			       char *out, size_t *out_len)
{
	efi_char16_t var16[CFG_VAR_NAME_MAX];
	const struct dmi_system_id *id;
	struct device *dev = maindev;
	char var8[CFG_VAR_NAME_MAX];
	struct efivar_entry *ev;
	int i, ret;

	/* For sensors, try first to use the _DSM table */
	if (!is_gmin) {
		ret = gmin_get_config_dsm_var(maindev, var, out, out_len);
		if (!ret)
			return 0;
	}

	/* Fall-back to other approaches */

	if (!is_gmin && ACPI_COMPANION(dev))
		dev = &ACPI_COMPANION(dev)->dev;

	if (!is_gmin)
		ret = snprintf(var8, sizeof(var8), "%s_%s", dev_name(dev), var);
	else
		ret = snprintf(var8, sizeof(var8), "gmin_%s", var);

	if (ret < 0 || ret >= sizeof(var8) - 1)
		return -EINVAL;

	/* First check a hard-coded list of board-specific variables.
	 * Some device firmwares lack the ability to set EFI variables at
	 * runtime.
	 */
	id = dmi_first_match(gmin_vars);
	if (id) {
		ret = gmin_get_hardcoded_var(maindev, id->driver_data, var8,
					     out, out_len);
		if (!ret)
			return 0;
	}

	/* Our variable names are ASCII by construction, but EFI names
	 * are wide chars.  Convert and zero-pad.
	 */
	memset(var16, 0, sizeof(var16));
	for (i = 0; i < sizeof(var8) && var8[i]; i++)
		var16[i] = var8[i];

	/* Not sure this API usage is kosher; efivar_entry_get()'s
	 * implementation simply uses VariableName and VendorGuid from
	 * the struct and ignores the rest, but it seems like there
	 * ought to be an "official" efivar_entry registered
	 * somewhere?
	 */
	ev = kzalloc(sizeof(*ev), GFP_KERNEL);
	if (!ev)
		return -ENOMEM;
	memcpy(&ev->var.VariableName, var16, sizeof(var16));
	ev->var.VendorGuid = GMIN_CFG_VAR_EFI_GUID;
	ev->var.DataSize = *out_len;

	ret = efivar_entry_get(ev, &ev->var.Attributes,
			       &ev->var.DataSize, ev->var.Data);
	if (ret == 0) {
		memcpy(out, ev->var.Data, ev->var.DataSize);
		*out_len = ev->var.DataSize;
		dev_info(maindev, "found EFI entry for '%s'\n", var8);
	} else if (is_gmin) {
		dev_info(maindev, "Failed to find EFI gmin variable %s\n", var8);
	} else {
		dev_info(maindev, "Failed to find EFI variable %s\n", var8);
	}

	kfree(ev);

	return ret;
}

int gmin_get_var_int(struct device *dev, bool is_gmin, const char *var, int def)
{
	char val[CFG_VAR_NAME_MAX];
	size_t len = sizeof(val);
	long result;
	int ret;

	ret = gmin_get_config_var(dev, is_gmin, var, val, &len);
	if (!ret) {
		val[len] = 0;
		ret = kstrtol(val, 0, &result);
	} else {
		dev_info(dev, "%s: using default (%d)\n", var, def);
	}

	return ret ? def : result;
}
EXPORT_SYMBOL_GPL(gmin_get_var_int);

int camera_sensor_csi(struct v4l2_subdev *sd, u32 port,
		      u32 lanes, u32 format, u32 bayer_order, int flag)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_mipi_info *csi = NULL;

	if (flag) {
		csi = kzalloc(sizeof(*csi), GFP_KERNEL);
		if (!csi)
			return -ENOMEM;
		csi->port = port;
		csi->num_lanes = lanes;
		csi->input_format = format;
		csi->raw_bayer_order = bayer_order;
		v4l2_set_subdev_hostdata(sd, (void *)csi);
		csi->metadata_format = ATOMISP_INPUT_FORMAT_EMBEDDED;
		csi->metadata_effective_width = NULL;
		dev_info(&client->dev,
			 "camera pdata: port: %d lanes: %d order: %8.8x\n",
			 port, lanes, bayer_order);
	} else {
		csi = v4l2_get_subdev_hostdata(sd);
		kfree(csi);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(camera_sensor_csi);

/* PCI quirk: The BYT ISP advertises PCI runtime PM but it doesn't
 * work.  Disable so the kernel framework doesn't hang the device
 * trying.  The driver itself does direct calls to the PUNIT to manage
 * ISP power.
 */
static void isp_pm_cap_fixup(struct pci_dev *pdev)
{
	dev_info(&pdev->dev, "Disabling PCI power management on camera ISP\n");
	pdev->pm_cap = 0;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0f38, isp_pm_cap_fixup);

MODULE_DESCRIPTION("Ancillary routines for binding ACPI devices");
MODULE_LICENSE("GPL");
