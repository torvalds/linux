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

#define VLV2_CLK_PLL_19P2MHZ 1 /* XTAL on CHT */
#define ELDO1_SEL_REG	0x19
#define ELDO1_1P8V	0x16
#define ELDO1_CTRL_SHIFT 0x00
#define ELDO2_SEL_REG	0x1a
#define ELDO2_1P8V	0x16
#define ELDO2_CTRL_SHIFT 0x01

struct gmin_subdev {
	struct v4l2_subdev *subdev;
	int clock_num;
	int clock_src;
	bool clock_on;
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
	bool v1p8_on;
	bool v2p8_on;
	bool v1p2_on;
	bool v2p8_vcm_on;
};

static struct gmin_subdev gmin_subdevs[MAX_SUBDEVS];

static enum { PMIC_UNSET = 0, PMIC_REGULATOR, PMIC_AXP, PMIC_TI,
	PMIC_CRYSTALCOVE } pmic_id;

/* The atomisp uses type==0 for the end-of-list marker, so leave space. */
static struct intel_v4l2_subdev_table pdata_subdevs[MAX_SUBDEVS + 1];

static const struct atomisp_platform_data pdata = {
	.subdevs = pdata_subdevs,
};

/*
 * Something of a hack.  The ECS E7 board drives camera 2.8v from an
 * external regulator instead of the PMIC.  There's a gmin_CamV2P8
 * config variable that specifies the GPIO to handle this particular
 * case, but this needs a broader architecture for handling camera
 * power.
 */
enum { V2P8_GPIO_UNSET = -2, V2P8_GPIO_NONE = -1 };
static int v2p8_gpio = V2P8_GPIO_UNSET;

/*
 * Something of a hack. The CHT RVP board drives camera 1.8v from an
 * external regulator instead of the PMIC just like ECS E7 board, see the
 * comments above.
 */
enum { V1P8_GPIO_UNSET = -2, V1P8_GPIO_NONE = -1 };
static int v1p8_gpio = V1P8_GPIO_UNSET;

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

#define CFG_VAR_NAME_MAX 64

#define GMIN_PMC_CLK_NAME 14 /* "pmc_plt_clk_[0..5]" */
static char gmin_pmc_clk_name[GMIN_PMC_CLK_NAME];

static struct gmin_subdev *gmin_subdev_add(struct v4l2_subdev *subdev)
{
	int i, ret;
	struct device *dev;
	struct i2c_client *client = v4l2_get_subdevdata(subdev);

	if (!pmic_id)
		pmic_id = PMIC_REGULATOR;

	if (!client)
		return NULL;

	dev = &client->dev;

	for (i = 0; i < MAX_SUBDEVS && gmin_subdevs[i].subdev; i++)
		;
	if (i >= MAX_SUBDEVS)
		return NULL;

	dev_info(dev,
		"gmin: initializing atomisp module subdev data.PMIC ID %d\n",
		pmic_id);

	gmin_subdevs[i].subdev = subdev;
	gmin_subdevs[i].clock_num = gmin_get_var_int(dev, "CamClk", 0);
	/*WA:CHT requires XTAL clock as PLL is not stable.*/
	gmin_subdevs[i].clock_src = gmin_get_var_int(dev, "ClkSrc",
							VLV2_CLK_PLL_19P2MHZ);
	gmin_subdevs[i].csi_port = gmin_get_var_int(dev, "CsiPort", 0);
	gmin_subdevs[i].csi_lanes = gmin_get_var_int(dev, "CsiLanes", 1);

	/* get PMC clock with clock framework */
	snprintf(gmin_pmc_clk_name,
		 sizeof(gmin_pmc_clk_name),
		 "%s_%d", "pmc_plt_clk", gmin_subdevs[i].clock_num);

	gmin_subdevs[i].pmc_clk = devm_clk_get(dev, gmin_pmc_clk_name);
	if (IS_ERR(gmin_subdevs[i].pmc_clk)) {
		ret = PTR_ERR(gmin_subdevs[i].pmc_clk);

		dev_err(dev,
			"Failed to get clk from %s : %d\n",
			gmin_pmc_clk_name,
			ret);

		return NULL;
	}

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
	ret = clk_prepare_enable(gmin_subdevs[i].pmc_clk);
	if (!ret)
		clk_disable_unprepare(gmin_subdevs[i].pmc_clk);

	gmin_subdevs[i].gpio0 = gpiod_get_index(dev, NULL, 0, GPIOD_OUT_LOW);
	if (IS_ERR(gmin_subdevs[i].gpio0))
		gmin_subdevs[i].gpio0 = NULL;

	gmin_subdevs[i].gpio1 = gpiod_get_index(dev, NULL, 1, GPIOD_OUT_LOW);
	if (IS_ERR(gmin_subdevs[i].gpio1))
		gmin_subdevs[i].gpio1 = NULL;

	if (pmic_id == PMIC_REGULATOR) {
		gmin_subdevs[i].v1p8_reg = regulator_get(dev, "V1P8SX");
		gmin_subdevs[i].v2p8_reg = regulator_get(dev, "V2P8SX");
		gmin_subdevs[i].v1p2_reg = regulator_get(dev, "V1P2A");
		gmin_subdevs[i].v2p8_vcm_reg = regulator_get(dev, "VPROG4B");

		/* Note: ideally we would initialize v[12]p8_on to the
		 * output of regulator_is_enabled(), but sadly that
		 * API is broken with the current drivers, returning
		 * "1" for a regulator that will then emit a
		 * "unbalanced disable" WARNing if we try to disable
		 * it.
		 */
	}

	return &gmin_subdevs[i];
}

static struct gmin_subdev *find_gmin_subdev(struct v4l2_subdev *subdev)
{
	int i;

	for (i = 0; i < MAX_SUBDEVS; i++)
		if (gmin_subdevs[i].subdev == subdev)
			return &gmin_subdevs[i];
	return gmin_subdev_add(subdev);
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

	if (gs->v1p2_reg) {
		if (on)
			return regulator_enable(gs->v1p2_reg);
		else
			return regulator_disable(gs->v1p2_reg);
	}

	/*TODO:v1p2 needs to extend to other PMICs*/

	return -EINVAL;
}

static int gmin_v1p8_ctrl(struct v4l2_subdev *subdev, int on)
{
	struct gmin_subdev *gs = find_gmin_subdev(subdev);
	int ret;

	if (v1p8_gpio == V1P8_GPIO_UNSET) {
		v1p8_gpio = gmin_get_var_int(NULL, "V1P8GPIO", V1P8_GPIO_NONE);
		if (v1p8_gpio != V1P8_GPIO_NONE) {
			pr_info("atomisp_gmin_platform: 1.8v power on GPIO %d\n",
				v1p8_gpio);
			ret = gpio_request(v1p8_gpio, "camera_v1p8_en");
			if (!ret)
				ret = gpio_direction_output(v1p8_gpio, 0);
			if (ret)
				pr_err("V1P8 GPIO initialization failed\n");
		}
	}

	if (!gs || gs->v1p8_on == on)
		return 0;
	gs->v1p8_on = on;

	if (v1p8_gpio >= 0)
		gpio_set_value(v1p8_gpio, on);

	if (gs->v1p8_reg) {
		regulator_set_voltage(gs->v1p8_reg, 1800000, 1800000);
		if (on)
			return regulator_enable(gs->v1p8_reg);
		else
			return regulator_disable(gs->v1p8_reg);
	}

	return -EINVAL;
}

static int gmin_v2p8_ctrl(struct v4l2_subdev *subdev, int on)
{
	struct gmin_subdev *gs = find_gmin_subdev(subdev);
	int ret;

	if (v2p8_gpio == V2P8_GPIO_UNSET) {
		v2p8_gpio = gmin_get_var_int(NULL, "V2P8GPIO", V2P8_GPIO_NONE);
		if (v2p8_gpio != V2P8_GPIO_NONE) {
			pr_info("atomisp_gmin_platform: 2.8v power on GPIO %d\n",
				v2p8_gpio);
			ret = gpio_request(v2p8_gpio, "camera_v2p8");
			if (!ret)
				ret = gpio_direction_output(v2p8_gpio, 0);
			if (ret)
				pr_err("V2P8 GPIO initialization failed\n");
		}
	}

	if (!gs || gs->v2p8_on == on)
		return 0;
	gs->v2p8_on = on;

	if (v2p8_gpio >= 0)
		gpio_set_value(v2p8_gpio, on);

	if (gs->v2p8_reg) {
		regulator_set_voltage(gs->v2p8_reg, 2900000, 2900000);
		if (on)
			return regulator_enable(gs->v2p8_reg);
		else
			return regulator_disable(gs->v2p8_reg);
	}

	return -EINVAL;
}

static int gmin_flisclk_ctrl(struct v4l2_subdev *subdev, int on)
{
	int ret = 0;
	struct gmin_subdev *gs = find_gmin_subdev(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);

	if (gs->clock_on == !!on)
		return 0;

	if (on) {
		ret = clk_set_rate(gs->pmc_clk, gs->clock_src);

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

	if (client == NULL || gs == NULL)
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

static struct camera_sensor_platform_data gmin_plat = {
	.gpio0_ctrl = gmin_gpio0_ctrl,
	.gpio1_ctrl = gmin_gpio1_ctrl,
	.v1p8_ctrl = gmin_v1p8_ctrl,
	.v2p8_ctrl = gmin_v2p8_ctrl,
	.v1p2_ctrl = gmin_v1p2_ctrl,
	.flisclk_ctrl = gmin_flisclk_ctrl,
	.csi_cfg = gmin_csi_cfg,
	.get_vcm_ctrl = gmin_get_vcm_ctrl,
};

struct camera_sensor_platform_data *gmin_camera_platform_data(
		struct v4l2_subdev *subdev,
		enum atomisp_input_format csi_format,
		enum atomisp_bayer_order csi_bayer)
{
	struct gmin_subdev *gs = find_gmin_subdev(subdev);

	gs->csi_fmt = csi_format;
	gs->csi_bayer = csi_bayer;

	return &gmin_plat;
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

static int gmin_get_hardcoded_var(struct gmin_cfg_var *varlist,
				  const char *var8, char *out, size_t *out_len)
{
	struct gmin_cfg_var *gv;

	for (gv = varlist; gv->name; gv++) {
		size_t vl;

		if (strcmp(var8, gv->name))
			continue;

		vl = strlen(gv->val);
		if (vl > *out_len - 1)
			return -ENOSPC;

		strcpy(out, gv->val);
		*out_len = vl;
		return 0;
	}

	return -EINVAL;
}

/* Retrieves a device-specific configuration variable.  The dev
 * argument should be a device with an ACPI companion, as all
 * configuration is based on firmware ID.
 */
static int gmin_get_config_var(struct device *dev, const char *var,
			       char *out, size_t *out_len)
{
	char var8[CFG_VAR_NAME_MAX];
	efi_char16_t var16[CFG_VAR_NAME_MAX];
	struct efivar_entry *ev;
	const struct dmi_system_id *id;
	int i, ret;

	if (dev && ACPI_COMPANION(dev))
		dev = &ACPI_COMPANION(dev)->dev;

	if (dev)
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
	if (id)
		return gmin_get_hardcoded_var(id->driver_data, var8, out, out_len);

	/* Our variable names are ASCII by construction, but EFI names
	 * are wide chars.  Convert and zero-pad.
	 */
	memset(var16, 0, sizeof(var16));
	for (i = 0; i < sizeof(var8) && var8[i]; i++)
		var16[i] = var8[i];

	/* To avoid owerflows when calling the efivar API */
	if (*out_len > ULONG_MAX)
		return -EINVAL;

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
	} else if (dev) {
		dev_warn(dev, "Failed to find gmin variable %s\n", var8);
	}

	kfree(ev);

	return ret;
}

int gmin_get_var_int(struct device *dev, const char *var, int def)
{
	char val[CFG_VAR_NAME_MAX];
	size_t len = sizeof(val);
	long result;
	int ret;

	ret = gmin_get_config_var(dev, var, val, &len);
	if (!ret) {
		val[len] = 0;
		ret = kstrtol(val, 0, &result);
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
static void isp_pm_cap_fixup(struct pci_dev *dev)
{
	dev_info(&dev->dev, "Disabling PCI power management on camera ISP\n");
	dev->pm_cap = 0;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0f38, isp_pm_cap_fixup);
