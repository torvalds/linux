/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _I8042_X86IA64IO_H
#define _I8042_X86IA64IO_H


#ifdef CONFIG_X86
#include <asm/x86_init.h>
#endif

/*
 * Names.
 */

#define I8042_KBD_PHYS_DESC "isa0060/serio0"
#define I8042_AUX_PHYS_DESC "isa0060/serio1"
#define I8042_MUX_PHYS_DESC "isa0060/serio%d"

/*
 * IRQs.
 */

#if defined(__ia64__)
# define I8042_MAP_IRQ(x)	isa_irq_to_vector((x))
#else
# define I8042_MAP_IRQ(x)	(x)
#endif

#define I8042_KBD_IRQ	i8042_kbd_irq
#define I8042_AUX_IRQ	i8042_aux_irq

static int i8042_kbd_irq;
static int i8042_aux_irq;

/*
 * Register numbers.
 */

#define I8042_COMMAND_REG	i8042_command_reg
#define I8042_STATUS_REG	i8042_command_reg
#define I8042_DATA_REG		i8042_data_reg

static int i8042_command_reg = 0x64;
static int i8042_data_reg = 0x60;


static inline int i8042_read_data(void)
{
	return inb(I8042_DATA_REG);
}

static inline int i8042_read_status(void)
{
	return inb(I8042_STATUS_REG);
}

static inline void i8042_write_data(int val)
{
	outb(val, I8042_DATA_REG);
}

static inline void i8042_write_command(int val)
{
	outb(val, I8042_COMMAND_REG);
}

#ifdef CONFIG_X86

#include <linux/dmi.h>

static const struct dmi_system_id __initconst i8042_dmi_noloop_table[] = {
	{
		/*
		 * Arima-Rioworks HDAMB -
		 * AUX LOOP command does not raise AUX IRQ
		 */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "RIOWORKS"),
			DMI_MATCH(DMI_BOARD_NAME, "HDAMB"),
			DMI_MATCH(DMI_BOARD_VERSION, "Rev E"),
		},
	},
	{
		/* ASUS G1S */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer Inc."),
			DMI_MATCH(DMI_BOARD_NAME, "G1S"),
			DMI_MATCH(DMI_BOARD_VERSION, "1.0"),
		},
	},
	{
		/* ASUS P65UP5 - AUX LOOP command does not raise AUX IRQ */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "P/I-P65UP5"),
			DMI_MATCH(DMI_BOARD_VERSION, "REV 2.X"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "X750LN"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Compaq"),
			DMI_MATCH(DMI_PRODUCT_NAME , "ProLiant"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "8500"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Compaq"),
			DMI_MATCH(DMI_PRODUCT_NAME , "ProLiant"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "DL760"),
		},
	},
	{
		/* Dell Embedded Box PC 3000 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Embedded Box PC 3000"),
		},
	},
	{
		/* OQO Model 01 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "OQO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ZEPTO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "00"),
		},
	},
	{
		/* ULI EV4873 - AUX LOOP does not work properly */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ULI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EV4873"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "5a"),
		},
	},
	{
		/* Microsoft Virtual Machine */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Virtual Machine"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "VS2005R2"),
		},
	},
	{
		/* Medion MAM 2070 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Notebook"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MAM 2070"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "5a"),
		},
	},
	{
		/* Medion Akoya E7225 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Medion"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Akoya E7225"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "1.0"),
		},
	},
	{
		/* Blue FB5601 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "blue"),
			DMI_MATCH(DMI_PRODUCT_NAME, "FB5601"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "M606"),
		},
	},
	{
		/* Gigabyte M912 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GIGABYTE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "M912"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "01"),
		},
	},
	{
		/* Gigabyte M1022M netbook */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Gigabyte Technology Co.,Ltd."),
			DMI_MATCH(DMI_BOARD_NAME, "M1022E"),
			DMI_MATCH(DMI_BOARD_VERSION, "1.02"),
		},
	},
	{
		/* Gigabyte Spring Peak - defines wrong chassis type */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GIGABYTE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Spring Peak"),
		},
	},
	{
		/* Gigabyte T1005 - defines wrong chassis type ("Other") */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GIGABYTE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "T1005"),
		},
	},
	{
		/* Gigabyte T1005M/P - defines wrong chassis type ("Other") */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GIGABYTE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "T1005M/P"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion dv9700"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Rev 1"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "PEGATRON CORPORATION"),
			DMI_MATCH(DMI_PRODUCT_NAME, "C15B"),
		},
	},
	{ }
};

/*
 * Some Fujitsu notebooks are having trouble with touchpads if
 * active multiplexing mode is activated. Luckily they don't have
 * external PS/2 ports so we can safely disable it.
 * ... apparently some Toshibas don't like MUX mode either and
 * die horrible death on reboot.
 */
static const struct dmi_system_id __initconst i8042_dmi_nomux_table[] = {
	{
		/* Fujitsu Lifebook P7010/P7010D */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "P7010"),
		},
	},
	{
		/* Fujitsu Lifebook P7010 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "0000000000"),
		},
	},
	{
		/* Fujitsu Lifebook P5020D */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook P Series"),
		},
	},
	{
		/* Fujitsu Lifebook S2000 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook S Series"),
		},
	},
	{
		/* Fujitsu Lifebook S6230 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook S6230"),
		},
	},
	{
		/* Fujitsu Lifebook U745 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK U745"),
		},
	},
	{
		/* Fujitsu T70H */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "FMVLT70H"),
		},
	},
	{
		/* Fujitsu-Siemens Lifebook T3010 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK T3010"),
		},
	},
	{
		/* Fujitsu-Siemens Lifebook E4010 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK E4010"),
		},
	},
	{
		/* Fujitsu-Siemens Amilo Pro 2010 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pro V2010"),
		},
	},
	{
		/* Fujitsu-Siemens Amilo Pro 2030 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO PRO V2030"),
		},
	},
	{
		/*
		 * No data is coming from the touchscreen unless KBC
		 * is in legacy mode.
		 */
		/* Panasonic CF-29 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Matsushita"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CF-29"),
		},
	},
	{
		/*
		 * HP Pavilion DV4017EA -
		 * errors on MUX ports are reported without raising AUXDATA
		 * causing "spurious NAK" messages.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Pavilion dv4000 (EA032EA#ABF)"),
		},
	},
	{
		/*
		 * HP Pavilion ZT1000 -
		 * like DV4017EA does not raise AUXERR for errors on MUX ports.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion Notebook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP Pavilion Notebook ZT1000"),
		},
	},
	{
		/*
		 * HP Pavilion DV4270ca -
		 * like DV4017EA does not raise AUXERR for errors on MUX ports.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Pavilion dv4000 (EH476UA#ABL)"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite P10"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EQUIUM A110"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SATELLITE C850D"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ALIENWARE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Sentia"),
		},
	},
	{
		/* Sharp Actius MM20 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SHARP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PC-MM20 Series"),
		},
	},
	{
		/* Sony Vaio FS-115b */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "VGN-FS115B"),
		},
	},
	{
		/*
		 * Sony Vaio FZ-240E -
		 * reset and GET ID commands issued via KBD port are
		 * sometimes being delivered to AUX3.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "VGN-FZ240E"),
		},
	},
	{
		/*
		 * Most (all?) VAIOs do not have external PS/2 ports nor
		 * they implement active multiplexing properly, and
		 * MUX discovery usually messes up keyboard/touchpad.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "VAIO"),
		},
	},
	{
		/* Amoi M636/A737 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Amoi Electronics CO.,LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "M636/A737 platform"),
		},
	},
	{
		/* Lenovo 3000 n100 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "076804U"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 1360"),
		},
	},
	{
		/* Acer Aspire 5710 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5710"),
		},
	},
	{
		/* Acer Aspire 7738 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 7738"),
		},
	},
	{
		/* Gericom Bellagio */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Gericom"),
			DMI_MATCH(DMI_PRODUCT_NAME, "N34AS6"),
		},
	},
	{
		/* IBM 2656 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "2656"),
		},
	},
	{
		/* Dell XPS M1530 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "XPS M1530"),
		},
	},
	{
		/* Compal HEL80I */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "COMPAL"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HEL80I"),
		},
	},
	{
		/* Dell Vostro 1510 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro1510"),
		},
	},
	{
		/* Acer Aspire 5536 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5536"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "0100"),
		},
	},
	{
		/* Dell Vostro V13 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro V13"),
		},
	},
	{
		/* Newer HP Pavilion dv4 models */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion dv4 Notebook PC"),
		},
	},
	{
		/* Asus X450LCP */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "X450LCP"),
		},
	},
	{
		/* Avatar AVIU-145A6 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel"),
			DMI_MATCH(DMI_PRODUCT_NAME, "IC4I"),
		},
	},
	{
		/* TUXEDO BU1406 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Notebook"),
			DMI_MATCH(DMI_PRODUCT_NAME, "N24_25BU"),
		},
	},
	{
		/* Lenovo LaVie Z */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Lenovo LaVie Z"),
		},
	},
	{
		/*
		 * Acer Aspire 5738z
		 * Touchpad stops working in mux mode when dis- + re-enabled
		 * with the touchpad enable/disable toggle hotkey
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5738"),
		},
	},
	{ }
};

static const struct dmi_system_id i8042_dmi_forcemux_table[] __initconst = {
	{
		/*
		 * Sony Vaio VGN-CS series require MUX or the touch sensor
		 * buttons will disturb touchpad operation
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "VGN-CS"),
		},
	},
	{ }
};

/*
 * On some Asus laptops, just running self tests cause problems.
 */
static const struct dmi_system_id i8042_dmi_noselftest_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_CHASSIS_TYPE, "10"), /* Notebook */
		},
	},
	{ }
};
static const struct dmi_system_id __initconst i8042_dmi_reset_table[] = {
	{
		/* MSI Wind U-100 */
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "U-100"),
			DMI_MATCH(DMI_BOARD_VENDOR, "MICRO-STAR INTERNATIONAL CO., LTD"),
		},
	},
	{
		/* LG Electronics X110 */
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "X110"),
			DMI_MATCH(DMI_BOARD_VENDOR, "LG Electronics Inc."),
		},
	},
	{
		/* Acer Aspire One 150 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AOA150"),
		},
	},
	{
		/* Advent 4211 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "DIXONSXP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Advent 4211"),
		},
	},
	{
		/* Medion Akoya Mini E1210 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDION"),
			DMI_MATCH(DMI_PRODUCT_NAME, "E1210"),
		},
	},
	{
		/* Medion Akoya E1222 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDION"),
			DMI_MATCH(DMI_PRODUCT_NAME, "E122X"),
		},
	},
	{
		/* Mivvy M310 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "VIOOO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "N10"),
		},
	},
	{
		/* Dell Vostro 1320 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro 1320"),
		},
	},
	{
		/* Dell Vostro 1520 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro 1520"),
		},
	},
	{
		/* Dell Vostro 1720 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro 1720"),
		},
	},
	{
		/* Lenovo Ideapad U455 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "20046"),
		},
	},
	{
		/* Lenovo ThinkPad L460 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad L460"),
		},
	},
	{
		/* Clevo P650RS, 650RP6, Sager NP8152-S, and others */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Notebook"),
			DMI_MATCH(DMI_PRODUCT_NAME, "P65xRP"),
		},
	},
	{ }
};

#ifdef CONFIG_PNP
static const struct dmi_system_id __initconst i8042_dmi_nopnp_table[] = {
	{
		/* Intel MBO Desktop D845PESV */
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "D845PESV"),
			DMI_MATCH(DMI_BOARD_VENDOR, "Intel Corporation"),
		},
	},
	{
		/*
		 * Intel NUC D54250WYK - does not have i8042 controller but
		 * declares PS/2 devices in DSDT.
		 */
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "D54250WYK"),
			DMI_MATCH(DMI_BOARD_VENDOR, "Intel Corporation"),
		},
	},
	{
		/* MSI Wind U-100 */
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "U-100"),
			DMI_MATCH(DMI_BOARD_VENDOR, "MICRO-STAR INTERNATIONAL CO., LTD"),
		},
	},
	{ }
};

static const struct dmi_system_id __initconst i8042_dmi_laptop_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_CHASSIS_TYPE, "8"), /* Portable */
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_CHASSIS_TYPE, "9"), /* Laptop */
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_CHASSIS_TYPE, "10"), /* Notebook */
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_CHASSIS_TYPE, "14"), /* Sub-Notebook */
		},
	},
	{ }
};
#endif

static const struct dmi_system_id __initconst i8042_dmi_notimeout_table[] = {
	{
		/* Dell Vostro V13 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro V13"),
		},
	},
	{
		/* Newer HP Pavilion dv4 models */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion dv4 Notebook PC"),
		},
	},
	{
		/* Fujitsu A544 laptop */
		/* https://bugzilla.redhat.com/show_bug.cgi?id=1111138 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK A544"),
		},
	},
	{
		/* Fujitsu AH544 laptop */
		/* https://bugzilla.kernel.org/show_bug.cgi?id=69731 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK AH544"),
		},
	},
	{
		/* Fujitsu U574 laptop */
		/* https://bugzilla.kernel.org/show_bug.cgi?id=69731 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK U574"),
		},
	},
	{
		/* Fujitsu UH554 laptop */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK UH544"),
		},
	},
	{ }
};

/*
 * Some Wistron based laptops need us to explicitly enable the 'Dritek
 * keyboard extension' to make their extra keys start generating scancodes.
 * Originally, this was just confined to older laptops, but a few Acer laptops
 * have turned up in 2007 that also need this again.
 */
static const struct dmi_system_id __initconst i8042_dmi_dritek_table[] = {
	{
		/* Acer Aspire 5100 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5100"),
		},
	},
	{
		/* Acer Aspire 5610 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5610"),
		},
	},
	{
		/* Acer Aspire 5630 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5630"),
		},
	},
	{
		/* Acer Aspire 5650 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5650"),
		},
	},
	{
		/* Acer Aspire 5680 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5680"),
		},
	},
	{
		/* Acer Aspire 5720 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5720"),
		},
	},
	{
		/* Acer Aspire 9110 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 9110"),
		},
	},
	{
		/* Acer TravelMate 660 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 660"),
		},
	},
	{
		/* Acer TravelMate 2490 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 2490"),
		},
	},
	{
		/* Acer TravelMate 4280 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 4280"),
		},
	},
	{ }
};

/*
 * Some laptops need keyboard reset before probing for the trackpad to get
 * it detected, initialised & finally work.
 */
static const struct dmi_system_id __initconst i8042_dmi_kbdreset_table[] = {
	{
		/* Gigabyte P35 v2 - Elantech touchpad */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GIGABYTE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "P35V2"),
		},
	},
		{
		/* Aorus branded Gigabyte X3 Plus - Elantech touchpad */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GIGABYTE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "X3"),
		},
	},
	{
		/* Gigabyte P34 - Elantech touchpad */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GIGABYTE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "P34"),
		},
	},
	{
		/* Gigabyte P57 - Elantech touchpad */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GIGABYTE"),
			DMI_MATCH(DMI_PRODUCT_NAME, "P57"),
		},
	},
	{
		/* Schenker XMG C504 - Elantech touchpad */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "XMG"),
			DMI_MATCH(DMI_PRODUCT_NAME, "C504"),
		},
	},
	{ }
};

#endif /* CONFIG_X86 */

#ifdef CONFIG_PNP
#include <linux/pnp.h>

static bool i8042_pnp_kbd_registered;
static unsigned int i8042_pnp_kbd_devices;
static bool i8042_pnp_aux_registered;
static unsigned int i8042_pnp_aux_devices;

static int i8042_pnp_command_reg;
static int i8042_pnp_data_reg;
static int i8042_pnp_kbd_irq;
static int i8042_pnp_aux_irq;

static char i8042_pnp_kbd_name[32];
static char i8042_pnp_aux_name[32];

static void i8042_pnp_id_to_string(struct pnp_id *id, char *dst, int dst_size)
{
	strlcpy(dst, "PNP:", dst_size);

	while (id) {
		strlcat(dst, " ", dst_size);
		strlcat(dst, id->id, dst_size);
		id = id->next;
	}
}

static int i8042_pnp_kbd_probe(struct pnp_dev *dev, const struct pnp_device_id *did)
{
	if (pnp_port_valid(dev, 0) && pnp_port_len(dev, 0) == 1)
		i8042_pnp_data_reg = pnp_port_start(dev,0);

	if (pnp_port_valid(dev, 1) && pnp_port_len(dev, 1) == 1)
		i8042_pnp_command_reg = pnp_port_start(dev, 1);

	if (pnp_irq_valid(dev,0))
		i8042_pnp_kbd_irq = pnp_irq(dev, 0);

	strlcpy(i8042_pnp_kbd_name, did->id, sizeof(i8042_pnp_kbd_name));
	if (strlen(pnp_dev_name(dev))) {
		strlcat(i8042_pnp_kbd_name, ":", sizeof(i8042_pnp_kbd_name));
		strlcat(i8042_pnp_kbd_name, pnp_dev_name(dev), sizeof(i8042_pnp_kbd_name));
	}
	i8042_pnp_id_to_string(dev->id, i8042_kbd_firmware_id,
			       sizeof(i8042_kbd_firmware_id));
	i8042_kbd_fwnode = dev_fwnode(&dev->dev);

	/* Keyboard ports are always supposed to be wakeup-enabled */
	device_set_wakeup_enable(&dev->dev, true);

	i8042_pnp_kbd_devices++;
	return 0;
}

static int i8042_pnp_aux_probe(struct pnp_dev *dev, const struct pnp_device_id *did)
{
	if (pnp_port_valid(dev, 0) && pnp_port_len(dev, 0) == 1)
		i8042_pnp_data_reg = pnp_port_start(dev,0);

	if (pnp_port_valid(dev, 1) && pnp_port_len(dev, 1) == 1)
		i8042_pnp_command_reg = pnp_port_start(dev, 1);

	if (pnp_irq_valid(dev, 0))
		i8042_pnp_aux_irq = pnp_irq(dev, 0);

	strlcpy(i8042_pnp_aux_name, did->id, sizeof(i8042_pnp_aux_name));
	if (strlen(pnp_dev_name(dev))) {
		strlcat(i8042_pnp_aux_name, ":", sizeof(i8042_pnp_aux_name));
		strlcat(i8042_pnp_aux_name, pnp_dev_name(dev), sizeof(i8042_pnp_aux_name));
	}
	i8042_pnp_id_to_string(dev->id, i8042_aux_firmware_id,
			       sizeof(i8042_aux_firmware_id));

	i8042_pnp_aux_devices++;
	return 0;
}

static const struct pnp_device_id pnp_kbd_devids[] = {
	{ .id = "PNP0300", .driver_data = 0 },
	{ .id = "PNP0301", .driver_data = 0 },
	{ .id = "PNP0302", .driver_data = 0 },
	{ .id = "PNP0303", .driver_data = 0 },
	{ .id = "PNP0304", .driver_data = 0 },
	{ .id = "PNP0305", .driver_data = 0 },
	{ .id = "PNP0306", .driver_data = 0 },
	{ .id = "PNP0309", .driver_data = 0 },
	{ .id = "PNP030a", .driver_data = 0 },
	{ .id = "PNP030b", .driver_data = 0 },
	{ .id = "PNP0320", .driver_data = 0 },
	{ .id = "PNP0343", .driver_data = 0 },
	{ .id = "PNP0344", .driver_data = 0 },
	{ .id = "PNP0345", .driver_data = 0 },
	{ .id = "CPQA0D7", .driver_data = 0 },
	{ .id = "", },
};
MODULE_DEVICE_TABLE(pnp, pnp_kbd_devids);

static struct pnp_driver i8042_pnp_kbd_driver = {
	.name           = "i8042 kbd",
	.id_table       = pnp_kbd_devids,
	.probe          = i8042_pnp_kbd_probe,
	.driver         = {
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
		.suppress_bind_attrs = true,
	},
};

static const struct pnp_device_id pnp_aux_devids[] = {
	{ .id = "AUI0200", .driver_data = 0 },
	{ .id = "FJC6000", .driver_data = 0 },
	{ .id = "FJC6001", .driver_data = 0 },
	{ .id = "PNP0f03", .driver_data = 0 },
	{ .id = "PNP0f0b", .driver_data = 0 },
	{ .id = "PNP0f0e", .driver_data = 0 },
	{ .id = "PNP0f12", .driver_data = 0 },
	{ .id = "PNP0f13", .driver_data = 0 },
	{ .id = "PNP0f19", .driver_data = 0 },
	{ .id = "PNP0f1c", .driver_data = 0 },
	{ .id = "SYN0801", .driver_data = 0 },
	{ .id = "", },
};
MODULE_DEVICE_TABLE(pnp, pnp_aux_devids);

static struct pnp_driver i8042_pnp_aux_driver = {
	.name           = "i8042 aux",
	.id_table       = pnp_aux_devids,
	.probe          = i8042_pnp_aux_probe,
	.driver         = {
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
		.suppress_bind_attrs = true,
	},
};

static void i8042_pnp_exit(void)
{
	if (i8042_pnp_kbd_registered) {
		i8042_pnp_kbd_registered = false;
		pnp_unregister_driver(&i8042_pnp_kbd_driver);
	}

	if (i8042_pnp_aux_registered) {
		i8042_pnp_aux_registered = false;
		pnp_unregister_driver(&i8042_pnp_aux_driver);
	}
}

static int __init i8042_pnp_init(void)
{
	char kbd_irq_str[4] = { 0 }, aux_irq_str[4] = { 0 };
	bool pnp_data_busted = false;
	int err;

#ifdef CONFIG_X86
	if (dmi_check_system(i8042_dmi_nopnp_table))
		i8042_nopnp = true;
#endif

	if (i8042_nopnp) {
		pr_info("PNP detection disabled\n");
		return 0;
	}

	err = pnp_register_driver(&i8042_pnp_kbd_driver);
	if (!err)
		i8042_pnp_kbd_registered = true;

	err = pnp_register_driver(&i8042_pnp_aux_driver);
	if (!err)
		i8042_pnp_aux_registered = true;

	if (!i8042_pnp_kbd_devices && !i8042_pnp_aux_devices) {
		i8042_pnp_exit();
#if defined(__ia64__)
		return -ENODEV;
#else
		pr_info("PNP: No PS/2 controller found.\n");
		if (x86_platform.legacy.i8042 !=
				X86_LEGACY_I8042_EXPECTED_PRESENT)
			return -ENODEV;
		pr_info("Probing ports directly.\n");
		return 0;
#endif
	}

	if (i8042_pnp_kbd_devices)
		snprintf(kbd_irq_str, sizeof(kbd_irq_str),
			"%d", i8042_pnp_kbd_irq);
	if (i8042_pnp_aux_devices)
		snprintf(aux_irq_str, sizeof(aux_irq_str),
			"%d", i8042_pnp_aux_irq);

	pr_info("PNP: PS/2 Controller [%s%s%s] at %#x,%#x irq %s%s%s\n",
		i8042_pnp_kbd_name, (i8042_pnp_kbd_devices && i8042_pnp_aux_devices) ? "," : "",
		i8042_pnp_aux_name,
		i8042_pnp_data_reg, i8042_pnp_command_reg,
		kbd_irq_str, (i8042_pnp_kbd_devices && i8042_pnp_aux_devices) ? "," : "",
		aux_irq_str);

#if defined(__ia64__)
	if (!i8042_pnp_kbd_devices)
		i8042_nokbd = true;
	if (!i8042_pnp_aux_devices)
		i8042_noaux = true;
#endif

	if (((i8042_pnp_data_reg & ~0xf) == (i8042_data_reg & ~0xf) &&
	      i8042_pnp_data_reg != i8042_data_reg) ||
	    !i8042_pnp_data_reg) {
		pr_warn("PNP: PS/2 controller has invalid data port %#x; using default %#x\n",
			i8042_pnp_data_reg, i8042_data_reg);
		i8042_pnp_data_reg = i8042_data_reg;
		pnp_data_busted = true;
	}

	if (((i8042_pnp_command_reg & ~0xf) == (i8042_command_reg & ~0xf) &&
	      i8042_pnp_command_reg != i8042_command_reg) ||
	    !i8042_pnp_command_reg) {
		pr_warn("PNP: PS/2 controller has invalid command port %#x; using default %#x\n",
			i8042_pnp_command_reg, i8042_command_reg);
		i8042_pnp_command_reg = i8042_command_reg;
		pnp_data_busted = true;
	}

	if (!i8042_nokbd && !i8042_pnp_kbd_irq) {
		pr_warn("PNP: PS/2 controller doesn't have KBD irq; using default %d\n",
			i8042_kbd_irq);
		i8042_pnp_kbd_irq = i8042_kbd_irq;
		pnp_data_busted = true;
	}

	if (!i8042_noaux && !i8042_pnp_aux_irq) {
		if (!pnp_data_busted && i8042_pnp_kbd_irq) {
			pr_warn("PNP: PS/2 appears to have AUX port disabled, "
				"if this is incorrect please boot with i8042.nopnp\n");
			i8042_noaux = true;
		} else {
			pr_warn("PNP: PS/2 controller doesn't have AUX irq; using default %d\n",
				i8042_aux_irq);
			i8042_pnp_aux_irq = i8042_aux_irq;
		}
	}

	i8042_data_reg = i8042_pnp_data_reg;
	i8042_command_reg = i8042_pnp_command_reg;
	i8042_kbd_irq = i8042_pnp_kbd_irq;
	i8042_aux_irq = i8042_pnp_aux_irq;

#ifdef CONFIG_X86
	i8042_bypass_aux_irq_test = !pnp_data_busted &&
				    dmi_check_system(i8042_dmi_laptop_table);
#endif

	return 0;
}

#else  /* !CONFIG_PNP */
static inline int i8042_pnp_init(void) { return 0; }
static inline void i8042_pnp_exit(void) { }
#endif /* CONFIG_PNP */

static int __init i8042_platform_init(void)
{
	int retval;

#ifdef CONFIG_X86
	u8 a20_on = 0xdf;
	/* Just return if platform does not have i8042 controller */
	if (x86_platform.legacy.i8042 == X86_LEGACY_I8042_PLATFORM_ABSENT)
		return -ENODEV;
#endif

/*
 * On ix86 platforms touching the i8042 data register region can do really
 * bad things. Because of this the region is always reserved on ix86 boxes.
 *
 *	if (!request_region(I8042_DATA_REG, 16, "i8042"))
 *		return -EBUSY;
 */

	i8042_kbd_irq = I8042_MAP_IRQ(1);
	i8042_aux_irq = I8042_MAP_IRQ(12);

	retval = i8042_pnp_init();
	if (retval)
		return retval;

#if defined(__ia64__)
        i8042_reset = I8042_RESET_ALWAYS;
#endif

#ifdef CONFIG_X86
	/* Honor module parameter when value is not default */
	if (i8042_reset == I8042_RESET_DEFAULT) {
		if (dmi_check_system(i8042_dmi_reset_table))
			i8042_reset = I8042_RESET_ALWAYS;

		if (dmi_check_system(i8042_dmi_noselftest_table))
			i8042_reset = I8042_RESET_NEVER;
	}

	if (dmi_check_system(i8042_dmi_noloop_table))
		i8042_noloop = true;

	if (dmi_check_system(i8042_dmi_nomux_table))
		i8042_nomux = true;

	if (dmi_check_system(i8042_dmi_forcemux_table))
		i8042_nomux = false;

	if (dmi_check_system(i8042_dmi_notimeout_table))
		i8042_notimeout = true;

	if (dmi_check_system(i8042_dmi_dritek_table))
		i8042_dritek = true;

	if (dmi_check_system(i8042_dmi_kbdreset_table))
		i8042_kbdreset = true;

	/*
	 * A20 was already enabled during early kernel init. But some buggy
	 * BIOSes (in MSI Laptops) require A20 to be enabled using 8042 to
	 * resume from S3. So we do it here and hope that nothing breaks.
	 */
	i8042_command(&a20_on, 0x10d1);
	i8042_command(NULL, 0x00ff);	/* Null command for SMM firmware */
#endif /* CONFIG_X86 */

	return retval;
}

static inline void i8042_platform_exit(void)
{
	i8042_pnp_exit();
}

#endif /* _I8042_X86IA64IO_H */
