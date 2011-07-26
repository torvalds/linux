/* linux/arch/arm/mach-msm/board-trout-mddi.c
** Author: Brian Swetland <swetland@google.com>
*/
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <asm/io.h>
#include <asm/mach-types.h>

#include <mach/msm_fb.h>
#include <mach/vreg.h>

#include "board-trout.h"
#include "proc_comm.h"
#include "devices.h"

#define TROUT_DEFAULT_BACKLIGHT_BRIGHTNESS 255

#define MDDI_CLIENT_CORE_BASE  0x108000
#define LCD_CONTROL_BLOCK_BASE 0x110000
#define SPI_BLOCK_BASE         0x120000
#define I2C_BLOCK_BASE         0x130000
#define PWM_BLOCK_BASE         0x140000
#define GPIO_BLOCK_BASE        0x150000
#define SYSTEM_BLOCK1_BASE     0x160000
#define SYSTEM_BLOCK2_BASE     0x170000


#define	DPSUS       (MDDI_CLIENT_CORE_BASE|0x24)
#define	SYSCLKENA   (MDDI_CLIENT_CORE_BASE|0x2C)
#define	PWM0OFF	      (PWM_BLOCK_BASE|0x1C)

#define V_VDDE2E_VDD2_GPIO 0
#define MDDI_RST_N 82

#define	MDDICAP0    (MDDI_CLIENT_CORE_BASE|0x00)
#define	MDDICAP1    (MDDI_CLIENT_CORE_BASE|0x04)
#define	MDDICAP2    (MDDI_CLIENT_CORE_BASE|0x08)
#define	MDDICAP3    (MDDI_CLIENT_CORE_BASE|0x0C)
#define	MDCAPCHG    (MDDI_CLIENT_CORE_BASE|0x10)
#define	MDCRCERC    (MDDI_CLIENT_CORE_BASE|0x14)
#define	TTBUSSEL    (MDDI_CLIENT_CORE_BASE|0x18)
#define	DPSET0      (MDDI_CLIENT_CORE_BASE|0x1C)
#define	DPSET1      (MDDI_CLIENT_CORE_BASE|0x20)
#define	DPSUS       (MDDI_CLIENT_CORE_BASE|0x24)
#define	DPRUN       (MDDI_CLIENT_CORE_BASE|0x28)
#define	SYSCKENA    (MDDI_CLIENT_CORE_BASE|0x2C)
#define	TESTMODE    (MDDI_CLIENT_CORE_BASE|0x30)
#define	FIFOMONI    (MDDI_CLIENT_CORE_BASE|0x34)
#define	INTMONI     (MDDI_CLIENT_CORE_BASE|0x38)
#define	MDIOBIST    (MDDI_CLIENT_CORE_BASE|0x3C)
#define	MDIOPSET    (MDDI_CLIENT_CORE_BASE|0x40)
#define	BITMAP0     (MDDI_CLIENT_CORE_BASE|0x44)
#define	BITMAP1     (MDDI_CLIENT_CORE_BASE|0x48)
#define	BITMAP2     (MDDI_CLIENT_CORE_BASE|0x4C)
#define	BITMAP3     (MDDI_CLIENT_CORE_BASE|0x50)
#define	BITMAP4     (MDDI_CLIENT_CORE_BASE|0x54)

#define	SRST        (LCD_CONTROL_BLOCK_BASE|0x00)
#define	PORT_ENB    (LCD_CONTROL_BLOCK_BASE|0x04)
#define	START       (LCD_CONTROL_BLOCK_BASE|0x08)
#define	PORT        (LCD_CONTROL_BLOCK_BASE|0x0C)
#define	CMN         (LCD_CONTROL_BLOCK_BASE|0x10)
#define	GAMMA       (LCD_CONTROL_BLOCK_BASE|0x14)
#define	INTFLG      (LCD_CONTROL_BLOCK_BASE|0x18)
#define	INTMSK      (LCD_CONTROL_BLOCK_BASE|0x1C)
#define	MPLFBUF     (LCD_CONTROL_BLOCK_BASE|0x20)
#define	HDE_LEFT    (LCD_CONTROL_BLOCK_BASE|0x24)
#define	VDE_TOP     (LCD_CONTROL_BLOCK_BASE|0x28)
#define	PXL         (LCD_CONTROL_BLOCK_BASE|0x30)
#define	HCYCLE      (LCD_CONTROL_BLOCK_BASE|0x34)
#define	HSW         (LCD_CONTROL_BLOCK_BASE|0x38)
#define	HDE_START   (LCD_CONTROL_BLOCK_BASE|0x3C)
#define	HDE_SIZE    (LCD_CONTROL_BLOCK_BASE|0x40)
#define	VCYCLE      (LCD_CONTROL_BLOCK_BASE|0x44)
#define	VSW         (LCD_CONTROL_BLOCK_BASE|0x48)
#define	VDE_START   (LCD_CONTROL_BLOCK_BASE|0x4C)
#define	VDE_SIZE    (LCD_CONTROL_BLOCK_BASE|0x50)
#define	WAKEUP      (LCD_CONTROL_BLOCK_BASE|0x54)
#define	WSYN_DLY    (LCD_CONTROL_BLOCK_BASE|0x58)
#define	REGENB      (LCD_CONTROL_BLOCK_BASE|0x5C)
#define	VSYNIF      (LCD_CONTROL_BLOCK_BASE|0x60)
#define	WRSTB       (LCD_CONTROL_BLOCK_BASE|0x64)
#define	RDSTB       (LCD_CONTROL_BLOCK_BASE|0x68)
#define	ASY_DATA    (LCD_CONTROL_BLOCK_BASE|0x6C)
#define	ASY_DATB    (LCD_CONTROL_BLOCK_BASE|0x70)
#define	ASY_DATC    (LCD_CONTROL_BLOCK_BASE|0x74)
#define	ASY_DATD    (LCD_CONTROL_BLOCK_BASE|0x78)
#define	ASY_DATE    (LCD_CONTROL_BLOCK_BASE|0x7C)
#define	ASY_DATF    (LCD_CONTROL_BLOCK_BASE|0x80)
#define	ASY_DATG    (LCD_CONTROL_BLOCK_BASE|0x84)
#define	ASY_DATH    (LCD_CONTROL_BLOCK_BASE|0x88)
#define	ASY_CMDSET  (LCD_CONTROL_BLOCK_BASE|0x8C)

#define	SSICTL      (SPI_BLOCK_BASE|0x00)
#define	SSITIME     (SPI_BLOCK_BASE|0x04)
#define	SSITX       (SPI_BLOCK_BASE|0x08)
#define	SSIRX       (SPI_BLOCK_BASE|0x0C)
#define	SSIINTC     (SPI_BLOCK_BASE|0x10)
#define	SSIINTS     (SPI_BLOCK_BASE|0x14)
#define	SSIDBG1     (SPI_BLOCK_BASE|0x18)
#define	SSIDBG2     (SPI_BLOCK_BASE|0x1C)
#define	SSIID       (SPI_BLOCK_BASE|0x20)

#define	WKREQ       (SYSTEM_BLOCK1_BASE|0x00)
#define	CLKENB      (SYSTEM_BLOCK1_BASE|0x04)
#define	DRAMPWR     (SYSTEM_BLOCK1_BASE|0x08)
#define	INTMASK     (SYSTEM_BLOCK1_BASE|0x0C)
#define	GPIOSEL     (SYSTEM_BLOCK2_BASE|0x00)

#define	GPIODATA    (GPIO_BLOCK_BASE|0x00)
#define	GPIODIR     (GPIO_BLOCK_BASE|0x04)
#define	GPIOIS      (GPIO_BLOCK_BASE|0x08)
#define	GPIOIBE     (GPIO_BLOCK_BASE|0x0C)
#define	GPIOIEV     (GPIO_BLOCK_BASE|0x10)
#define	GPIOIE      (GPIO_BLOCK_BASE|0x14)
#define	GPIORIS     (GPIO_BLOCK_BASE|0x18)
#define	GPIOMIS     (GPIO_BLOCK_BASE|0x1C)
#define	GPIOIC      (GPIO_BLOCK_BASE|0x20)
#define	GPIOOMS     (GPIO_BLOCK_BASE|0x24)
#define	GPIOPC      (GPIO_BLOCK_BASE|0x28)
#define	GPIOID      (GPIO_BLOCK_BASE|0x30)

#define SPI_WRITE(reg, val) \
	{ SSITX,        0x00010000 | (((reg) & 0xff) << 8) | ((val) & 0xff) }, \
	{ 0, 5 },

#define SPI_WRITE1(reg) \
	{ SSITX,        (reg) & 0xff }, \
	{ 0, 5 },

struct mddi_table {
	uint32_t reg;
	uint32_t value;
};
static struct mddi_table mddi_toshiba_init_table[] = {
	{ DPSET0,       0x09e90046 },
	{ DPSET1,       0x00000118 },
	{ DPSUS,        0x00000000 },
	{ DPRUN,        0x00000001 },
	{ 1,            14         }, /* msleep 14 */
	{ SYSCKENA,     0x00000001 },
	{ CLKENB,       0x0000A1EF },  /*    # SYS.CLKENB  # Enable clocks for each module (without DCLK , i2cCLK) */

	{ GPIODATA,     0x02000200 },  /*   # GPI .GPIODATA  # GPIO2(RESET_LCD_N) set to 0 , GPIO3(eDRAM_Power) set to 0 */
	{ GPIODIR,      0x000030D  },  /* 24D   # GPI .GPIODIR  # Select direction of GPIO port (0,2,3,6,9 output) */
	{ GPIOSEL,      0/*0x00000173*/},  /*   # SYS.GPIOSEL  # GPIO port multiplexing control */
	{ GPIOPC,       0x03C300C0 },  /*   # GPI .GPIOPC  # GPIO2,3 PD cut */
	{ WKREQ,        0x00000000 },  /*   # SYS.WKREQ  # Wake-up request event is VSYNC alignment */

	{ GPIOIBE,      0x000003FF },
	{ GPIOIS,       0x00000000 },
	{ GPIOIC,       0x000003FF },
	{ GPIOIE,       0x00000000 },

	{ GPIODATA,     0x00040004 },  /*   # GPI .GPIODATA  # eDRAM VD supply */
	{ 1,            1          }, /* msleep 1 */
	{ GPIODATA,     0x02040004 },  /*   # GPI .GPIODATA  # eDRAM VD supply */
	{ DRAMPWR,      0x00000001 }, /* eDRAM power */
};

#define GPIOSEL_VWAKEINT (1U << 0)
#define INTMASK_VWAKEOUT (1U << 0)


static struct clk *gp_clk;
static int trout_new_backlight = 1;
static struct vreg *vreg_mddi_1v5;
static struct vreg *vreg_lcm_2v85;

static void trout_process_mddi_table(struct msm_mddi_client_data *client_data,
				     struct mddi_table *table, size_t count)
{
	int i;
	for (i = 0; i < count; i++) {
		uint32_t reg = table[i].reg;
		uint32_t value = table[i].value;

		if (reg == 0)
			udelay(value);
		else if (reg == 1)
			msleep(value);
		else
			client_data->remote_write(client_data, value, reg);
	}
}

static int trout_mddi_toshiba_client_init(
	struct msm_mddi_bridge_platform_data *bridge_data,
	struct msm_mddi_client_data *client_data)
{
	int panel_id;

	client_data->auto_hibernate(client_data, 0);
	trout_process_mddi_table(client_data, mddi_toshiba_init_table,
				 ARRAY_SIZE(mddi_toshiba_init_table));
	client_data->auto_hibernate(client_data, 1);
	panel_id = (client_data->remote_read(client_data, GPIODATA) >> 4) & 3;
	if (panel_id > 1) {
		printk(KERN_WARNING "unknown panel id at mddi_enable\n");
		return -1;
	}
	return 0;
}

static int trout_mddi_toshiba_client_uninit(
	struct msm_mddi_bridge_platform_data *bridge_data,
	struct msm_mddi_client_data *client_data)
{
	return 0;
}

static struct resource resources_msm_fb[] = {
	{
		.start = MSM_FB_BASE,
		.end = MSM_FB_BASE + MSM_FB_SIZE,
		.flags = IORESOURCE_MEM,
	},
};

struct msm_mddi_bridge_platform_data toshiba_client_data = {
	.init = trout_mddi_toshiba_client_init,
	.uninit = trout_mddi_toshiba_client_uninit,
	.fb_data = {
		.xres = 320,
		.yres = 480,
		.width = 45,
		.height = 67,
		.output_format = 0,
	},
};

static struct msm_mddi_platform_data mddi_pdata = {
	.clk_rate = 122880000,
	.fb_resource = resources_msm_fb,
	.num_clients = 1,
	.client_platform_data = {
		{
			.product_id = (0xd263 << 16 | 0),
			.name = "mddi_c_d263_0000",
			.id = 0,
			.client_data = &toshiba_client_data,
			.clk_rate = 0,
		},
	},
};

int __init trout_init_panel(void)
{
	int rc;

	if (!machine_is_trout())
		return 0;
	vreg_mddi_1v5 = vreg_get(0, "gp2");
	if (IS_ERR(vreg_mddi_1v5))
		return PTR_ERR(vreg_mddi_1v5);
	vreg_lcm_2v85 = vreg_get(0, "gp4");
	if (IS_ERR(vreg_lcm_2v85))
		return PTR_ERR(vreg_lcm_2v85);

	trout_new_backlight = system_rev >= 5;
	if (trout_new_backlight) {
		uint32_t config = PCOM_GPIO_CFG(27, 0, GPIO_OUTPUT,
						GPIO_NO_PULL, GPIO_8MA);
		msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &config, 0);
	} else {
		uint32_t config = PCOM_GPIO_CFG(27, 1, GPIO_OUTPUT,
						GPIO_NO_PULL, GPIO_8MA);
		msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &config, 0);

		gp_clk = clk_get(NULL, "gp_clk");
		if (IS_ERR(gp_clk)) {
			printk(KERN_ERR "trout_init_panel: could not get gp"
			       "clock\n");
			gp_clk = NULL;
		}
		rc = clk_set_rate(gp_clk, 19200000);
		if (rc)
			printk(KERN_ERR "trout_init_panel: set clock rate "
			       "failed\n");
	}

	rc = platform_device_register(&msm_device_mdp);
	if (rc)
		return rc;
	msm_device_mddi0.dev.platform_data = &mddi_pdata;
	return platform_device_register(&msm_device_mddi0);
}

device_initcall(trout_init_panel);
