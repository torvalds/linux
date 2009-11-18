/*
 * TI DA830/OMAP L137 EVM board
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 * Derived from: arch/arm/mach-davinci/board-dm644x-evm.c
 *
 * 2007, 2009 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/cp_intc.h>
#include <mach/da8xx.h>
#include <mach/asp.h>

#define DA830_EVM_PHY_MASK		0x0
#define DA830_EVM_MDIO_FREQUENCY	2200000	/* PHY bus frequency */

static struct at24_platform_data da830_evm_i2c_eeprom_info = {
	.byte_len	= SZ_256K / 8,
	.page_size	= 64,
	.flags		= AT24_FLAG_ADDR16,
	.setup		= davinci_get_mac_addr,
	.context	= (void *)0x7f00,
};

static struct i2c_board_info __initdata da830_evm_i2c_devices[] = {
	{
		I2C_BOARD_INFO("24c256", 0x50),
		.platform_data	= &da830_evm_i2c_eeprom_info,
	},
	{
		I2C_BOARD_INFO("tlv320aic3x", 0x18),
	}
};

static struct davinci_i2c_platform_data da830_evm_i2c_0_pdata = {
	.bus_freq	= 100,	/* kHz */
	.bus_delay	= 0,	/* usec */
};

static struct davinci_uart_config da830_evm_uart_config __initdata = {
	.enabled_uarts = 0x7,
};

static u8 da830_iis_serializer_direction[] = {
	RX_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	TX_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
};

static struct snd_platform_data da830_evm_snd_data = {
	.tx_dma_offset  = 0x2000,
	.rx_dma_offset  = 0x2000,
	.op_mode        = DAVINCI_MCASP_IIS_MODE,
	.num_serializer = ARRAY_SIZE(da830_iis_serializer_direction),
	.tdm_slots      = 2,
	.serial_dir     = da830_iis_serializer_direction,
	.eventq_no      = EVENTQ_0,
	.version	= MCASP_VERSION_2,
	.txnumevt	= 1,
	.rxnumevt	= 1,
};

static __init void da830_evm_init(void)
{
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	int ret;

	ret = da8xx_register_edma();
	if (ret)
		pr_warning("da830_evm_init: edma registration failed: %d\n",
				ret);

	ret = da8xx_pinmux_setup(da830_i2c0_pins);
	if (ret)
		pr_warning("da830_evm_init: i2c0 mux setup failed: %d\n",
				ret);

	ret = da8xx_register_i2c(0, &da830_evm_i2c_0_pdata);
	if (ret)
		pr_warning("da830_evm_init: i2c0 registration failed: %d\n",
				ret);

	soc_info->emac_pdata->phy_mask = DA830_EVM_PHY_MASK;
	soc_info->emac_pdata->mdio_max_freq = DA830_EVM_MDIO_FREQUENCY;
	soc_info->emac_pdata->rmii_en = 1;

	ret = da8xx_pinmux_setup(da830_cpgmac_pins);
	if (ret)
		pr_warning("da830_evm_init: cpgmac mux setup failed: %d\n",
				ret);

	ret = da8xx_register_emac();
	if (ret)
		pr_warning("da830_evm_init: emac registration failed: %d\n",
				ret);

	ret = da8xx_register_watchdog();
	if (ret)
		pr_warning("da830_evm_init: watchdog registration failed: %d\n",
				ret);

	davinci_serial_init(&da830_evm_uart_config);
	i2c_register_board_info(1, da830_evm_i2c_devices,
			ARRAY_SIZE(da830_evm_i2c_devices));

	ret = da8xx_pinmux_setup(da830_mcasp1_pins);
	if (ret)
		pr_warning("da830_evm_init: mcasp1 mux setup failed: %d\n",
				ret);

	da8xx_init_mcasp(1, &da830_evm_snd_data);
}

#ifdef CONFIG_SERIAL_8250_CONSOLE
static int __init da830_evm_console_init(void)
{
	return add_preferred_console("ttyS", 2, "115200");
}
console_initcall(da830_evm_console_init);
#endif

static __init void da830_evm_irq_init(void)
{
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	cp_intc_init((void __iomem *)DA8XX_CP_INTC_VIRT, DA830_N_CP_INTC_IRQ,
			soc_info->intc_irq_prios);
}

static void __init da830_evm_map_io(void)
{
	da830_init();
}

MACHINE_START(DAVINCI_DA830_EVM, "DaVinci DA830/OMAP L137 EVM")
	.phys_io	= IO_PHYS,
	.io_pg_offst	= (__IO_ADDRESS(IO_PHYS) >> 18) & 0xfffc,
	.boot_params	= (DA8XX_DDR_BASE + 0x100),
	.map_io		= da830_evm_map_io,
	.init_irq	= da830_evm_irq_init,
	.timer		= &davinci_timer,
	.init_machine	= da830_evm_init,
MACHINE_END
