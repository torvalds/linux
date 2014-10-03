/*
 * ACPI support for PNP bus type
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Zhang Rui <rui.zhang@intel.com>
 *          Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/ctype.h>

static const struct acpi_device_id acpi_pnp_device_ids[] = {
	/* pata_isapnp */
	{"PNP0600"},		/* Generic ESDI/IDE/ATA compatible hard disk controller */
	/* floppy */
	{"PNP0700"},
	/* ipmi_si */
	{"IPI0001"},
	/* tpm_inf_pnp */
	{"IFX0101"},		/* Infineon TPMs */
	{"IFX0102"},		/* Infineon TPMs */
	/*tpm_tis */
	{"PNP0C31"},		/* TPM */
	{"ATM1200"},		/* Atmel */
	{"IFX0102"},		/* Infineon */
	{"BCM0101"},		/* Broadcom */
	{"BCM0102"},		/* Broadcom */
	{"NSC1200"},		/* National */
	{"ICO0102"},		/* Intel */
	/* ide   */
	{"PNP0600"},		/* Generic ESDI/IDE/ATA compatible hard disk controller */
	/* ns558 */
	{"ASB16fd"},		/* AdLib NSC16 */
	{"AZT3001"},		/* AZT1008 */
	{"CDC0001"},		/* Opl3-SAx */
	{"CSC0001"},		/* CS4232 */
	{"CSC000f"},		/* CS4236 */
	{"CSC0101"},		/* CS4327 */
	{"CTL7001"},		/* SB16 */
	{"CTL7002"},		/* AWE64 */
	{"CTL7005"},		/* Vibra16 */
	{"ENS2020"},		/* SoundscapeVIVO */
	{"ESS0001"},		/* ES1869 */
	{"ESS0005"},		/* ES1878 */
	{"ESS6880"},		/* ES688 */
	{"IBM0012"},		/* CS4232 */
	{"OPT0001"},		/* OPTi Audio16 */
	{"YMH0006"},		/* Opl3-SA */
	{"YMH0022"},		/* Opl3-SAx */
	{"PNPb02f"},		/* Generic */
	/* i8042 kbd */
	{"PNP0300"},
	{"PNP0301"},
	{"PNP0302"},
	{"PNP0303"},
	{"PNP0304"},
	{"PNP0305"},
	{"PNP0306"},
	{"PNP0309"},
	{"PNP030a"},
	{"PNP030b"},
	{"PNP0320"},
	{"PNP0343"},
	{"PNP0344"},
	{"PNP0345"},
	{"CPQA0D7"},
	/* i8042 aux */
	{"AUI0200"},
	{"FJC6000"},
	{"FJC6001"},
	{"PNP0f03"},
	{"PNP0f0b"},
	{"PNP0f0e"},
	{"PNP0f12"},
	{"PNP0f13"},
	{"PNP0f19"},
	{"PNP0f1c"},
	{"SYN0801"},
	/* fcpnp */
	{"AVM0900"},
	/* radio-cadet */
	{"MSM0c24"},		/* ADS Cadet AM/FM Radio Card */
	/* radio-gemtek */
	{"ADS7183"},		/* AOpen FX-3D/Pro Radio */
	/* radio-sf16fmr2 */
	{"MFRad13"},		/* tuner subdevice of SF16-FMD2 */
	/* ene_ir */
	{"ENE0100"},
	{"ENE0200"},
	{"ENE0201"},
	{"ENE0202"},
	/* fintek-cir */
	{"FIT0002"},		/* CIR */
	/* ite-cir */
	{"ITE8704"},		/* Default model */
	{"ITE8713"},		/* CIR found in EEEBox 1501U */
	{"ITE8708"},		/* Bridged IT8512 */
	{"ITE8709"},		/* SRAM-Bridged IT8512 */
	/* nuvoton-cir */
	{"WEC0530"},		/* CIR */
	{"NTN0530"},		/* CIR for new chip's pnp id */
	/* Winbond CIR */
	{"WEC1022"},
	/* wbsd */
	{"WEC0517"},
	{"WEC0518"},
	/* Winbond CIR */
	{"TCM5090"},		/* 3Com Etherlink III (TP) */
	{"TCM5091"},		/* 3Com Etherlink III */
	{"TCM5094"},		/* 3Com Etherlink III (combo) */
	{"TCM5095"},		/* 3Com Etherlink III (TPO) */
	{"TCM5098"},		/* 3Com Etherlink III (TPC) */
	{"PNP80f7"},		/* 3Com Etherlink III compatible */
	{"PNP80f8"},		/* 3Com Etherlink III compatible */
	/* nsc-ircc */
	{"NSC6001"},
	{"HWPC224"},
	{"IBM0071"},
	/* smsc-ircc2 */
	{"SMCf010"},
	/* sb1000 */
	{"GIC1000"},
	/* parport_pc */
	{"PNP0400"},		/* Standard LPT Printer Port */
	{"PNP0401"},		/* ECP Printer Port */
	/* apple-gmux */
	{"APP000B"},
	/* fujitsu-laptop.c */
	{"FUJ02bf"},
	{"FUJ02B1"},
	{"FUJ02E3"},
	/* system */
	{"PNP0c02"},		/* General ID for reserving resources */
	{"PNP0c01"},		/* memory controller */
	/* rtc_cmos */
	{"PNP0b00"},
	{"PNP0b01"},
	{"PNP0b02"},
	/* c6xdigio */
	{"PNP0400"},		/* Standard LPT Printer Port */
	{"PNP0401"},		/* ECP Printer Port */
	/* ni_atmio.c */
	{"NIC1900"},
	{"NIC2400"},
	{"NIC2500"},
	{"NIC2600"},
	{"NIC2700"},
	/* serial */
	{"AAC000F"},		/* Archtek America Corp. Archtek SmartLink Modem 3334BT Plug & Play */
	{"ADC0001"},		/* Anchor Datacomm BV. SXPro 144 External Data Fax Modem Plug & Play */
	{"ADC0002"},		/* SXPro 288 External Data Fax Modem Plug & Play */
	{"AEI0250"},		/* PROLiNK 1456VH ISA PnP K56flex Fax Modem */
	{"AEI1240"},		/* Actiontec ISA PNP 56K X2 Fax Modem */
	{"AKY1021"},		/* Rockwell 56K ACF II Fax+Data+Voice Modem */
	{"AZT4001"},		/* AZT3005 PnP SOUND DEVICE */
	{"BDP3336"},		/* Best Data Products Inc. Smart One 336F PnP Modem */
	{"BRI0A49"},		/* Boca Complete Ofc Communicator 14.4 Data-FAX */
	{"BRI1400"},		/* Boca Research 33,600 ACF Modem */
	{"BRI3400"},		/* Boca 33.6 Kbps Internal FD34FSVD */
	{"BRI0A49"},		/* Boca 33.6 Kbps Internal FD34FSVD */
	{"BDP3336"},		/* Best Data Products Inc. Smart One 336F PnP Modem */
	{"CPI4050"},		/* Computer Peripherals Inc. EuroViVa CommCenter-33.6 SP PnP */
	{"CTL3001"},		/* Creative Labs Phone Blaster 28.8 DSVD PnP Voice */
	{"CTL3011"},		/* Creative Labs Modem Blaster 28.8 DSVD PnP Voice */
	{"DAV0336"},		/* Davicom ISA 33.6K Modem */
	{"DMB1032"},		/* Creative Modem Blaster Flash56 DI5601-1 */
	{"DMB2001"},		/* Creative Modem Blaster V.90 DI5660 */
	{"ETT0002"},		/* E-Tech CyberBULLET PC56RVP */
	{"FUJ0202"},		/* Fujitsu 33600 PnP-I2 R Plug & Play */
	{"FUJ0205"},		/* Fujitsu FMV-FX431 Plug & Play */
	{"FUJ0206"},		/* Fujitsu 33600 PnP-I4 R Plug & Play */
	{"FUJ0209"},		/* Fujitsu Fax Voice 33600 PNP-I5 R Plug & Play */
	{"GVC000F"},		/* Archtek SmartLink Modem 3334BT Plug & Play */
	{"GVC0303"},		/* Archtek SmartLink Modem 3334BRV 33.6K Data Fax Voice */
	{"HAY0001"},		/* Hayes Optima 288 V.34-V.FC + FAX + Voice Plug & Play */
	{"HAY000C"},		/* Hayes Optima 336 V.34 + FAX + Voice PnP */
	{"HAY000D"},		/* Hayes Optima 336B V.34 + FAX + Voice PnP */
	{"HAY5670"},		/* Hayes Accura 56K Ext Fax Modem PnP */
	{"HAY5674"},		/* Hayes Accura 56K Ext Fax Modem PnP */
	{"HAY5675"},		/* Hayes Accura 56K Fax Modem PnP */
	{"HAYF000"},		/* Hayes 288, V.34 + FAX */
	{"HAYF001"},		/* Hayes Optima 288 V.34 + FAX + Voice, Plug & Play */
	{"IBM0033"},		/* IBM Thinkpad 701 Internal Modem Voice */
	{"PNP4972"},		/* Intermec CV60 touchscreen port */
	{"IXDC801"},		/* Intertex 28k8 33k6 Voice EXT PnP */
	{"IXDC901"},		/* Intertex 33k6 56k Voice EXT PnP */
	{"IXDD801"},		/* Intertex 28k8 33k6 Voice SP EXT PnP */
	{"IXDD901"},		/* Intertex 33k6 56k Voice SP EXT PnP */
	{"IXDF401"},		/* Intertex 28k8 33k6 Voice SP INT PnP */
	{"IXDF801"},		/* Intertex 28k8 33k6 Voice SP EXT PnP */
	{"IXDF901"},		/* Intertex 33k6 56k Voice SP EXT PnP */
	{"KOR4522"},		/* KORTEX 28800 Externe PnP */
	{"KORF661"},		/* KXPro 33.6 Vocal ASVD PnP */
	{"LAS4040"},		/* LASAT Internet 33600 PnP */
	{"LAS4540"},		/* Lasat Safire 560 PnP */
	{"LAS5440"},		/* Lasat Safire 336  PnP */
	{"MNP0281"},		/* Microcom TravelPorte FAST V.34 Plug & Play */
	{"MNP0336"},		/* Microcom DeskPorte V.34 FAST or FAST+ Plug & Play */
	{"MNP0339"},		/* Microcom DeskPorte FAST EP 28.8 Plug & Play */
	{"MNP0342"},		/* Microcom DeskPorte 28.8P Plug & Play */
	{"MNP0500"},		/* Microcom DeskPorte FAST ES 28.8 Plug & Play */
	{"MNP0501"},		/* Microcom DeskPorte FAST ES 28.8 Plug & Play */
	{"MNP0502"},		/* Microcom DeskPorte 28.8S Internal Plug & Play */
	{"MOT1105"},		/* Motorola BitSURFR Plug & Play */
	{"MOT1111"},		/* Motorola TA210 Plug & Play */
	{"MOT1114"},		/* Motorola HMTA 200 (ISDN) Plug & Play */
	{"MOT1115"},		/* Motorola BitSURFR Plug & Play */
	{"MOT1190"},		/* Motorola Lifestyle 28.8 Internal */
	{"MOT1501"},		/* Motorola V.3400 Plug & Play */
	{"MOT1502"},		/* Motorola Lifestyle 28.8 V.34 Plug & Play */
	{"MOT1505"},		/* Motorola Power 28.8 V.34 Plug & Play */
	{"MOT1509"},		/* Motorola ModemSURFR External 28.8 Plug & Play */
	{"MOT150A"},		/* Motorola Premier 33.6 Desktop Plug & Play */
	{"MOT150F"},		/* Motorola VoiceSURFR 56K External PnP */
	{"MOT1510"},		/* Motorola ModemSURFR 56K External PnP */
	{"MOT1550"},		/* Motorola ModemSURFR 56K Internal PnP */
	{"MOT1560"},		/* Motorola ModemSURFR Internal 28.8 Plug & Play */
	{"MOT1580"},		/* Motorola Premier 33.6 Internal Plug & Play */
	{"MOT15B0"},		/* Motorola OnlineSURFR 28.8 Internal Plug & Play */
	{"MOT15F0"},		/* Motorola VoiceSURFR 56K Internal PnP */
	{"MVX00A1"},		/*  Deskline K56 Phone System PnP */
	{"MVX00F2"},		/* PC Rider K56 Phone System PnP */
	{"nEC8241"},		/* NEC 98NOTE SPEAKER PHONE FAX MODEM(33600bps) */
	{"PMC2430"},		/* Pace 56 Voice Internal Plug & Play Modem */
	{"PNP0500"},		/* Generic standard PC COM port     */
	{"PNP0501"},		/* Generic 16550A-compatible COM port */
	{"PNPC000"},		/* Compaq 14400 Modem */
	{"PNPC001"},		/* Compaq 2400/9600 Modem */
	{"PNPC031"},		/* Dial-Up Networking Serial Cable between 2 PCs */
	{"PNPC032"},		/* Dial-Up Networking Parallel Cable between 2 PCs */
	{"PNPC100"},		/* Standard 9600 bps Modem */
	{"PNPC101"},		/* Standard 14400 bps Modem */
	{"PNPC102"},		/*  Standard 28800 bps Modem */
	{"PNPC103"},		/*  Standard Modem */
	{"PNPC104"},		/*  Standard 9600 bps Modem */
	{"PNPC105"},		/*  Standard 14400 bps Modem */
	{"PNPC106"},		/*  Standard 28800 bps Modem */
	{"PNPC107"},		/*  Standard Modem */
	{"PNPC108"},		/* Standard 9600 bps Modem */
	{"PNPC109"},		/* Standard 14400 bps Modem */
	{"PNPC10A"},		/* Standard 28800 bps Modem */
	{"PNPC10B"},		/* Standard Modem */
	{"PNPC10C"},		/* Standard 9600 bps Modem */
	{"PNPC10D"},		/* Standard 14400 bps Modem */
	{"PNPC10E"},		/* Standard 28800 bps Modem */
	{"PNPC10F"},		/* Standard Modem */
	{"PNP2000"},		/* Standard PCMCIA Card Modem */
	{"ROK0030"},		/* Rockwell 33.6 DPF Internal PnP, Modular Technology 33.6 Internal PnP */
	{"ROK0100"},		/* KORTEX 14400 Externe PnP */
	{"ROK4120"},		/* Rockwell 28.8 */
	{"ROK4920"},		/* Viking 28.8 INTERNAL Fax+Data+Voice PnP */
	{"RSS00A0"},		/* Rockwell 33.6 DPF External PnP, BT Prologue 33.6 External PnP, Modular Technology 33.6 External PnP */
	{"RSS0262"},		/* Viking 56K FAX INT */
	{"RSS0250"},		/* K56 par,VV,Voice,Speakphone,AudioSpan,PnP */
	{"SUP1310"},		/* SupraExpress 28.8 Data/Fax PnP modem */
	{"SUP1381"},		/* SupraExpress 336i PnP Voice Modem */
	{"SUP1421"},		/* SupraExpress 33.6 Data/Fax PnP modem */
	{"SUP1590"},		/* SupraExpress 33.6 Data/Fax PnP modem */
	{"SUP1620"},		/* SupraExpress 336i Sp ASVD */
	{"SUP1760"},		/* SupraExpress 33.6 Data/Fax PnP modem */
	{"SUP2171"},		/* SupraExpress 56i Sp Intl */
	{"TEX0011"},		/* Phoebe Micro 33.6 Data Fax 1433VQH Plug & Play */
	{"UAC000F"},		/* Archtek SmartLink Modem 3334BT Plug & Play */
	{"USR0000"},		/* 3Com Corp. Gateway Telepath IIvi 33.6 */
	{"USR0002"},		/* U.S. Robotics Sporster 33.6K Fax INT PnP */
	{"USR0004"},		/*  Sportster Vi 14.4 PnP FAX Voicemail */
	{"USR0006"},		/* U.S. Robotics 33.6K Voice INT PnP */
	{"USR0007"},		/* U.S. Robotics 33.6K Voice EXT PnP */
	{"USR0009"},		/* U.S. Robotics Courier V.Everything INT PnP */
	{"USR2002"},		/* U.S. Robotics 33.6K Voice INT PnP */
	{"USR2070"},		/* U.S. Robotics 56K Voice INT PnP */
	{"USR2080"},		/* U.S. Robotics 56K Voice EXT PnP */
	{"USR3031"},		/* U.S. Robotics 56K FAX INT */
	{"USR3050"},		/* U.S. Robotics 56K FAX INT */
	{"USR3070"},		/* U.S. Robotics 56K Voice INT PnP */
	{"USR3080"},		/* U.S. Robotics 56K Voice EXT PnP */
	{"USR3090"},		/* U.S. Robotics 56K Voice INT PnP */
	{"USR9100"},		/* U.S. Robotics 56K Message  */
	{"USR9160"},		/* U.S. Robotics 56K FAX EXT PnP */
	{"USR9170"},		/* U.S. Robotics 56K FAX INT PnP */
	{"USR9180"},		/* U.S. Robotics 56K Voice EXT PnP */
	{"USR9190"},		/* U.S. Robotics 56K Voice INT PnP */
	{"WACFXXX"},		/* Wacom tablets */
	{"FPI2002"},		/* Compaq touchscreen */
	{"FUJ02B2"},		/* Fujitsu Stylistic touchscreens */
	{"FUJ02B3"},
	{"FUJ02B4"},		/* Fujitsu Stylistic LT touchscreens */
	{"FUJ02B6"},		/* Passive Fujitsu Stylistic touchscreens */
	{"FUJ02B7"},
	{"FUJ02B8"},
	{"FUJ02B9"},
	{"FUJ02BC"},
	{"FUJ02E5"},		/* Fujitsu Wacom Tablet PC device */
	{"FUJ02E6"},		/* Fujitsu P-series tablet PC device */
	{"FUJ02E7"},		/* Fujitsu Wacom 2FGT Tablet PC device */
	{"FUJ02E9"},		/* Fujitsu Wacom 1FGT Tablet PC device */
	{"LTS0001"},		/* LG C1 EXPRESS DUAL (C1-PB11A3) touch screen (actually a FUJ02E6 in disguise) */
	{"WCI0003"},		/* Rockwell's (PORALiNK) 33600 INT PNP */
	{"WEC1022"},		/* Winbond CIR port, should not be probed. We should keep track of it to prevent the legacy serial driver from probing it */
	/* scl200wdt */
	{"NSC0800"},		/* National Semiconductor PC87307/PC97307 watchdog component */
	/* mpu401 */
	{"PNPb006"},
	/* cs423x-pnpbios */
	{"CSC0100"},
	{"CSC0000"},
	{"GIM0100"},		/* Guillemot Turtlebeach something appears to be cs4232 compatible */
	/* es18xx-pnpbios */
	{"ESS1869"},
	{"ESS1879"},
	/* snd-opl3sa2-pnpbios */
	{"YMH0021"},
	{"NMX2210"},		/* Gateway Solo 2500 */
	{""},
};

static bool matching_id(char *idstr, char *list_id)
{
	int i;

	if (memcmp(idstr, list_id, 3))
		return false;

	for (i = 3; i < 7; i++) {
		char c = toupper(idstr[i]);

		if (!isxdigit(c)
		    || (list_id[i] != 'X' && c != toupper(list_id[i])))
			return false;
	}
	return true;
}

static bool acpi_pnp_match(char *idstr, const struct acpi_device_id **matchid)
{
	const struct acpi_device_id *devid;

	for (devid = acpi_pnp_device_ids; devid->id[0]; devid++)
		if (matching_id(idstr, (char *)devid->id)) {
			if (matchid)
				*matchid = devid;

			return true;
		}

	return false;
}

static int acpi_pnp_attach(struct acpi_device *adev,
			   const struct acpi_device_id *id)
{
	return 1;
}

static struct acpi_scan_handler acpi_pnp_handler = {
	.ids = acpi_pnp_device_ids,
	.match = acpi_pnp_match,
	.attach = acpi_pnp_attach,
};

/*
 * For CMOS RTC devices, the PNP ACPI scan handler does not work, because
 * there is a CMOS RTC ACPI scan handler installed already, so we need to
 * check those devices and enumerate them to the PNP bus directly.
 */
static int is_cmos_rtc_device(struct acpi_device *adev)
{
	struct acpi_device_id ids[] = {
		{ "PNP0B00" },
		{ "PNP0B01" },
		{ "PNP0B02" },
		{""},
	};
	return !acpi_match_device_ids(adev, ids);
}

bool acpi_is_pnp_device(struct acpi_device *adev)
{
	return adev->handler == &acpi_pnp_handler || is_cmos_rtc_device(adev);
}
EXPORT_SYMBOL_GPL(acpi_is_pnp_device);

void __init acpi_pnp_init(void)
{
	acpi_scan_add_handler(&acpi_pnp_handler);
}
