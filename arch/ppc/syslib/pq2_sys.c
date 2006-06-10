/*
 * PQ2 System descriptions
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>

#include <asm/ppc_sys.h>

struct ppc_sys_spec *cur_ppc_sys_spec;
struct ppc_sys_spec ppc_sys_specs[] = {
	/* below is a list of the 8260 family of processors */
	{
		.ppc_sys_name	= "8250",
		.mask		= 0x0000ff00,
		.value		= 0x00000000,
		.num_devices	= 12,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_FCC3,
			MPC82xx_CPM_SCC1, MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3,
			MPC82xx_CPM_SCC4, MPC82xx_CPM_MCC1, MPC82xx_CPM_SMC1,
			MPC82xx_CPM_SMC2, MPC82xx_CPM_SPI, MPC82xx_CPM_I2C,
		}
	},
	{
		.ppc_sys_name	= "8255",
		.mask		= 0x0000ff00,
		.value		= 0x00000000,
		.num_devices	= 11,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_SCC1,
			MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3, MPC82xx_CPM_SCC4,
			MPC82xx_CPM_MCC1, MPC82xx_CPM_SMC1, MPC82xx_CPM_SMC2,
			MPC82xx_CPM_SPI, MPC82xx_CPM_I2C,
		}
	},
	{
		.ppc_sys_name	= "8260",
		.mask		= 0x0000ff00,
		.value		= 0x00000000,
		.num_devices	= 12,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_FCC3,
			MPC82xx_CPM_SCC1, MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3,
			MPC82xx_CPM_SCC4, MPC82xx_CPM_MCC1, MPC82xx_CPM_SMC1,
			MPC82xx_CPM_SMC2, MPC82xx_CPM_SPI, MPC82xx_CPM_I2C,
		}
	},
	{
		.ppc_sys_name	= "8264",
		.mask		= 0x0000ff00,
		.value		= 0x00000000,
		.num_devices	= 12,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_FCC3,
			MPC82xx_CPM_SCC1, MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3,
			MPC82xx_CPM_SCC4, MPC82xx_CPM_MCC1, MPC82xx_CPM_SMC1,
			MPC82xx_CPM_SMC2, MPC82xx_CPM_SPI, MPC82xx_CPM_I2C,
		}
	},
	{
		.ppc_sys_name	= "8265",
		.mask		= 0x0000ff00,
		.value		= 0x00000000,
		.num_devices	= 12,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_FCC3,
			MPC82xx_CPM_SCC1, MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3,
			MPC82xx_CPM_SCC4, MPC82xx_CPM_MCC1, MPC82xx_CPM_SMC1,
			MPC82xx_CPM_SMC2, MPC82xx_CPM_SPI, MPC82xx_CPM_I2C,
		}
	},
	{
		.ppc_sys_name	= "8266",
		.mask		= 0x0000ff00,
		.value		= 0x00000000,
		.num_devices	= 12,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_FCC3,
			MPC82xx_CPM_SCC1, MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3,
			MPC82xx_CPM_SCC4, MPC82xx_CPM_MCC1, MPC82xx_CPM_SMC1,
			MPC82xx_CPM_SMC2, MPC82xx_CPM_SPI, MPC82xx_CPM_I2C,
		}
	},
	/* below is a list of the 8272 family of processors */
	{
		.ppc_sys_name	= "8247",
		.mask		= 0x0000ff00,
		.value		= 0x00000d00,
		.num_devices	= 10,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_SCC1,
			MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3, MPC82xx_CPM_SMC1,
			MPC82xx_CPM_SMC2, MPC82xx_CPM_SPI, MPC82xx_CPM_I2C,
			MPC82xx_CPM_USB,
		},
	},
	{
		.ppc_sys_name	= "8248",
		.mask		= 0x0000ff00,
		.value		= 0x00000c00,
		.num_devices	= 12,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_SCC1,
			MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3, MPC82xx_CPM_SCC4,
			MPC82xx_CPM_SMC1, MPC82xx_CPM_SMC2, MPC82xx_CPM_SPI,
			MPC82xx_CPM_I2C, MPC82xx_CPM_USB, MPC82xx_SEC1,
		},
	},
	{
		.ppc_sys_name	= "8271",
		.mask		= 0x0000ff00,
		.value		= 0x00000d00,
		.num_devices	= 10,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_SCC1,
			MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3, MPC82xx_CPM_SMC1,
			MPC82xx_CPM_SMC2, MPC82xx_CPM_SPI, MPC82xx_CPM_I2C,
			MPC82xx_CPM_USB,
		},
	},
	{
		.ppc_sys_name	= "8272",
		.mask		= 0x0000ff00,
		.value		= 0x00000c00,
		.num_devices	= 12,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_SCC1,
			MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3, MPC82xx_CPM_SCC4,
			MPC82xx_CPM_SMC1, MPC82xx_CPM_SMC2, MPC82xx_CPM_SPI,
			MPC82xx_CPM_I2C, MPC82xx_CPM_USB, MPC82xx_SEC1,
		},
	},
	/* below is a list of the 8280 family of processors */
	{
		.ppc_sys_name	= "8270",
		.mask 		= 0x0000ff00,
		.value 		= 0x00000a00,
		.num_devices 	= 12,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_FCC3,
			MPC82xx_CPM_SCC1, MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3,
			MPC82xx_CPM_SCC4, MPC82xx_CPM_MCC1, MPC82xx_CPM_SMC1,
			MPC82xx_CPM_SMC2, MPC82xx_CPM_SPI, MPC82xx_CPM_I2C,
		},
	},
	{
		.ppc_sys_name	= "8275",
		.mask 		= 0x0000ff00,
		.value 		= 0x00000a00,
		.num_devices 	= 12,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_FCC3,
			MPC82xx_CPM_SCC1, MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3,
			MPC82xx_CPM_SCC4, MPC82xx_CPM_MCC1, MPC82xx_CPM_SMC1,
			MPC82xx_CPM_SMC2, MPC82xx_CPM_SPI, MPC82xx_CPM_I2C,
		},
	},
	{
		.ppc_sys_name	= "8280",
		.mask 		= 0x0000ff00,
		.value 		= 0x00000a00,
		.num_devices 	= 13,
		.device_list = (enum ppc_sys_devices[])
		{
			MPC82xx_CPM_FCC1, MPC82xx_CPM_FCC2, MPC82xx_CPM_FCC3,
			MPC82xx_CPM_SCC1, MPC82xx_CPM_SCC2, MPC82xx_CPM_SCC3,
			MPC82xx_CPM_SCC4, MPC82xx_CPM_MCC1, MPC82xx_CPM_MCC2,
			MPC82xx_CPM_SMC1, MPC82xx_CPM_SMC2, MPC82xx_CPM_SPI,
			MPC82xx_CPM_I2C,
		},
	},
	{
		/* default match */
		.ppc_sys_name	= "",
		.mask 		= 0x00000000,
		.value 		= 0x00000000,
	},
};
