// SPDX-License-Identifier: GPL-2.0
/*
 *  Probe for 8250/16550-type ISAPNP serial ports.
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright (C) 2001 Russell King, All Rights Reserved.
 *
 *  Ported to the Linux PnP Layer - (C) Adam Belay.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/pnp.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/property.h>
#include <linux/serial_core.h>
#include <linux/bitops.h>

#include <asm/byteorder.h>

#include "8250.h"

#define UNKNOWN_DEV 0x3000
#define CIR_PORT	0x0800

static const struct pnp_device_id pnp_dev_table[] = {
	/* Archtek America Corp. */
	/* Archtek SmartLink Modem 3334BT Plug & Play */
	{ .id = "AAC000F", .driver_data = 0 },
	/* Anchor Datacomm BV */
	/* SXPro 144 External Data Fax Modem Plug & Play */
	{ .id = "ADC0001", .driver_data = 0 },
	/* SXPro 288 External Data Fax Modem Plug & Play */
	{ .id = "ADC0002", .driver_data = 0 },
	/* PROLiNK 1456VH ISA PnP K56flex Fax Modem */
	{ .id = "AEI0250", .driver_data = 0 },
	/* Actiontec ISA PNP 56K X2 Fax Modem */
	{ .id = "AEI1240", .driver_data = 0 },
	/* Rockwell 56K ACF II Fax+Data+Voice Modem */
	{ .id ="AKY1021", .driver_data = 0 /*SPCI_FL_NO_SHIRQ*/ },
	/*
	 * ALi Fast Infrared Controller
	 * Native driver (ali-ircc) is broken so at least
	 * it can be used with irtty-sir.
	 */
	{ .id = "ALI5123", .driver_data = 0 },
	/* AZT3005 PnP SOUND DEVICE */
	{ .id = "AZT4001", .driver_data = 0 },
	/* Best Data Products Inc. Smart One 336F PnP Modem */
	{ .id = "BDP3336", .driver_data = 0 },
	/*  Boca Research */
	/* Boca Complete Ofc Communicator 14.4 Data-FAX */
	{ .id = "BRI0A49", .driver_data = 0 },
	/* Boca Research 33,600 ACF Modem */
	{ .id = "BRI1400", .driver_data = 0 },
	/* Boca 33.6 Kbps Internal FD34FSVD */
	{ .id = "BRI3400", .driver_data = 0 },
	/* Computer Peripherals Inc */
	/* EuroViVa CommCenter-33.6 SP PnP */
	{ .id = "CPI4050", .driver_data = 0 },
	/* Creative Labs */
	/* Creative Labs Phone Blaster 28.8 DSVD PnP Voice */
	{ .id = "CTL3001", .driver_data = 0 },
	/* Creative Labs Modem Blaster 28.8 DSVD PnP Voice */
	{ .id = "CTL3011", .driver_data = 0 },
	/* Davicom ISA 33.6K Modem */
	{ .id = "DAV0336", .driver_data = 0 },
	/* Creative */
	/* Creative Modem Blaster Flash56 DI5601-1 */
	{ .id = "DMB1032", .driver_data = 0 },
	/* Creative Modem Blaster V.90 DI5660 */
	{ .id = "DMB2001", .driver_data = 0 },
	/* E-Tech */
	/* E-Tech CyberBULLET PC56RVP */
	{ .id = "ETT0002", .driver_data = 0 },
	/* FUJITSU */
	/* Fujitsu 33600 PnP-I2 R Plug & Play */
	{ .id = "FUJ0202", .driver_data = 0 },
	/* Fujitsu FMV-FX431 Plug & Play */
	{ .id = "FUJ0205", .driver_data = 0 },
	/* Fujitsu 33600 PnP-I4 R Plug & Play */
	{ .id = "FUJ0206", .driver_data = 0 },
	/* Fujitsu Fax Voice 33600 PNP-I5 R Plug & Play */
	{ .id = "FUJ0209", .driver_data = 0 },
	/* Archtek America Corp. */
	/* Archtek SmartLink Modem 3334BT Plug & Play */
	{ .id = "GVC000F", .driver_data = 0 },
	/* Archtek SmartLink Modem 3334BRV 33.6K Data Fax Voice */
	{ .id = "GVC0303", .driver_data = 0 },
	/* Hayes */
	/* Hayes Optima 288 V.34-V.FC + FAX + Voice Plug & Play */
	{ .id = "HAY0001", .driver_data = 0 },
	/* Hayes Optima 336 V.34 + FAX + Voice PnP */
	{ .id = "HAY000C", .driver_data = 0 },
	/* Hayes Optima 336B V.34 + FAX + Voice PnP */
	{ .id = "HAY000D", .driver_data = 0 },
	/* Hayes Accura 56K Ext Fax Modem PnP */
	{ .id = "HAY5670", .driver_data = 0 },
	/* Hayes Accura 56K Ext Fax Modem PnP */
	{ .id = "HAY5674", .driver_data = 0 },
	/* Hayes Accura 56K Fax Modem PnP */
	{ .id = "HAY5675", .driver_data = 0 },
	/* Hayes 288, V.34 + FAX */
	{ .id = "HAYF000", .driver_data = 0 },
	/* Hayes Optima 288 V.34 + FAX + Voice, Plug & Play */
	{ .id = "HAYF001", .driver_data = 0 },
	/* IBM */
	/* IBM Thinkpad 701 Internal Modem Voice */
	{ .id = "IBM0033", .driver_data = 0 },
	/* Intermec */
	/* Intermec CV60 touchscreen port */
	{ .id = "PNP4972", .driver_data = 0 },
	/* Intertex */
	/* Intertex 28k8 33k6 Voice EXT PnP */
	{ .id = "IXDC801", .driver_data = 0 },
	/* Intertex 33k6 56k Voice EXT PnP */
	{ .id = "IXDC901", .driver_data = 0 },
	/* Intertex 28k8 33k6 Voice SP EXT PnP */
	{ .id = "IXDD801", .driver_data = 0 },
	/* Intertex 33k6 56k Voice SP EXT PnP */
	{ .id = "IXDD901", .driver_data = 0 },
	/* Intertex 28k8 33k6 Voice SP INT PnP */
	{ .id = "IXDF401", .driver_data = 0 },
	/* Intertex 28k8 33k6 Voice SP EXT PnP */
	{ .id = "IXDF801", .driver_data = 0 },
	/* Intertex 33k6 56k Voice SP EXT PnP */
	{ .id = "IXDF901", .driver_data = 0 },
	/* Kortex International */
	/* KORTEX 28800 Externe PnP */
	{ .id = "KOR4522", .driver_data = 0 },
	/* KXPro 33.6 Vocal ASVD PnP */
	{ .id = "KORF661", .driver_data = 0 },
	/* Lasat */
	/* LASAT Internet 33600 PnP */
	{ .id = "LAS4040", .driver_data = 0 },
	/* Lasat Safire 560 PnP */
	{ .id = "LAS4540", .driver_data = 0 },
	/* Lasat Safire 336  PnP */
	{ .id = "LAS5440", .driver_data = 0 },
	/* Microcom, Inc. */
	/* Microcom TravelPorte FAST V.34 Plug & Play */
	{ .id = "MNP0281", .driver_data = 0 },
	/* Microcom DeskPorte V.34 FAST or FAST+ Plug & Play */
	{ .id = "MNP0336", .driver_data = 0 },
	/* Microcom DeskPorte FAST EP 28.8 Plug & Play */
	{ .id = "MNP0339", .driver_data = 0 },
	/* Microcom DeskPorte 28.8P Plug & Play */
	{ .id = "MNP0342", .driver_data = 0 },
	/* Microcom DeskPorte FAST ES 28.8 Plug & Play */
	{ .id = "MNP0500", .driver_data = 0 },
	/* Microcom DeskPorte FAST ES 28.8 Plug & Play */
	{ .id = "MNP0501", .driver_data = 0 },
	/* Microcom DeskPorte 28.8S Internal Plug & Play */
	{ .id = "MNP0502", .driver_data = 0 },
	/* Motorola */
	/* Motorola BitSURFR Plug & Play */
	{ .id = "MOT1105", .driver_data = 0 },
	/* Motorola TA210 Plug & Play */
	{ .id = "MOT1111", .driver_data = 0 },
	/* Motorola HMTA 200 (ISDN) Plug & Play */
	{ .id = "MOT1114", .driver_data = 0 },
	/* Motorola BitSURFR Plug & Play */
	{ .id = "MOT1115", .driver_data = 0 },
	/* Motorola Lifestyle 28.8 Internal */
	{ .id = "MOT1190", .driver_data = 0 },
	/* Motorola V.3400 Plug & Play */
	{ .id = "MOT1501", .driver_data = 0 },
	/* Motorola Lifestyle 28.8 V.34 Plug & Play */
	{ .id = "MOT1502", .driver_data = 0 },
	/* Motorola Power 28.8 V.34 Plug & Play */
	{ .id = "MOT1505", .driver_data = 0 },
	/* Motorola ModemSURFR External 28.8 Plug & Play */
	{ .id = "MOT1509", .driver_data = 0 },
	/* Motorola Premier 33.6 Desktop Plug & Play */
	{ .id = "MOT150A", .driver_data = 0 },
	/* Motorola VoiceSURFR 56K External PnP */
	{ .id = "MOT150F", .driver_data = 0 },
	/* Motorola ModemSURFR 56K External PnP */
	{ .id = "MOT1510", .driver_data = 0 },
	/* Motorola ModemSURFR 56K Internal PnP */
	{ .id = "MOT1550", .driver_data = 0 },
	/* Motorola ModemSURFR Internal 28.8 Plug & Play */
	{ .id = "MOT1560", .driver_data = 0 },
	/* Motorola Premier 33.6 Internal Plug & Play */
	{ .id = "MOT1580", .driver_data = 0 },
	/* Motorola OnlineSURFR 28.8 Internal Plug & Play */
	{ .id = "MOT15B0", .driver_data = 0 },
	/* Motorola VoiceSURFR 56K Internal PnP */
	{ .id = "MOT15F0", .driver_data = 0 },
	/* Com 1 */
	/*  Deskline K56 Phone System PnP */
	{ .id = "MVX00A1", .driver_data = 0 },
	/* PC Rider K56 Phone System PnP */
	{ .id = "MVX00F2", .driver_data = 0 },
	/* NEC 98NOTE SPEAKER PHONE FAX MODEM(33600bps) */
	{ .id = "nEC8241", .driver_data = 0 },
	/* Pace 56 Voice Internal Plug & Play Modem */
	{ .id = "PMC2430", .driver_data = 0 },
	/* Generic */
	/* Generic standard PC COM port	 */
	{ .id = "PNP0500", .driver_data = 0 },
	/* Generic 16550A-compatible COM port */
	{ .id = "PNP0501", .driver_data = 0 },
	/* Compaq 14400 Modem */
	{ .id = "PNPC000", .driver_data = 0 },
	/* Compaq 2400/9600 Modem */
	{ .id = "PNPC001", .driver_data = 0 },
	/* Dial-Up Networking Serial Cable between 2 PCs */
	{ .id = "PNPC031", .driver_data = 0 },
	/* Dial-Up Networking Parallel Cable between 2 PCs */
	{ .id = "PNPC032", .driver_data = 0 },
	/* Standard 9600 bps Modem */
	{ .id = "PNPC100", .driver_data = 0 },
	/* Standard 14400 bps Modem */
	{ .id = "PNPC101", .driver_data = 0 },
	/*  Standard 28800 bps Modem*/
	{ .id = "PNPC102", .driver_data = 0 },
	/*  Standard Modem*/
	{ .id = "PNPC103", .driver_data = 0 },
	/*  Standard 9600 bps Modem*/
	{ .id = "PNPC104", .driver_data = 0 },
	/*  Standard 14400 bps Modem*/
	{ .id = "PNPC105", .driver_data = 0 },
	/*  Standard 28800 bps Modem*/
	{ .id = "PNPC106", .driver_data = 0 },
	/*  Standard Modem */
	{ .id = "PNPC107", .driver_data = 0 },
	/* Standard 9600 bps Modem */
	{ .id = "PNPC108", .driver_data = 0 },
	/* Standard 14400 bps Modem */
	{ .id = "PNPC109", .driver_data = 0 },
	/* Standard 28800 bps Modem */
	{ .id = "PNPC10A", .driver_data = 0 },
	/* Standard Modem */
	{ .id = "PNPC10B", .driver_data = 0 },
	/* Standard 9600 bps Modem */
	{ .id = "PNPC10C", .driver_data = 0 },
	/* Standard 14400 bps Modem */
	{ .id = "PNPC10D", .driver_data = 0 },
	/* Standard 28800 bps Modem */
	{ .id = "PNPC10E", .driver_data = 0 },
	/* Standard Modem */
	{ .id = "PNPC10F", .driver_data = 0 },
	/* Standard PCMCIA Card Modem */
	{ .id = "PNP2000", .driver_data = 0 },
	/* Rockwell */
	/* Modular Technology */
	/* Rockwell 33.6 DPF Internal PnP */
	/* Modular Technology 33.6 Internal PnP */
	{ .id = "ROK0030", .driver_data = 0 },
	/* Kortex International */
	/* KORTEX 14400 Externe PnP */
	{ .id = "ROK0100", .driver_data = 0 },
	/* Rockwell 28.8 */
	{ .id = "ROK4120", .driver_data = 0 },
	/* Viking Components, Inc */
	/* Viking 28.8 INTERNAL Fax+Data+Voice PnP */
	{ .id = "ROK4920", .driver_data = 0 },
	/* Rockwell */
	/* British Telecom */
	/* Modular Technology */
	/* Rockwell 33.6 DPF External PnP */
	/* BT Prologue 33.6 External PnP */
	/* Modular Technology 33.6 External PnP */
	{ .id = "RSS00A0", .driver_data = 0 },
	/* Viking 56K FAX INT */
	{ .id = "RSS0262", .driver_data = 0 },
	/* K56 par,VV,Voice,Speakphone,AudioSpan,PnP */
	{ .id = "RSS0250", .driver_data = 0 },
	/* SupraExpress 28.8 Data/Fax PnP modem */
	{ .id = "SUP1310", .driver_data = 0 },
	/* SupraExpress 336i PnP Voice Modem */
	{ .id = "SUP1381", .driver_data = 0 },
	/* SupraExpress 33.6 Data/Fax PnP modem */
	{ .id = "SUP1421", .driver_data = 0 },
	/* SupraExpress 33.6 Data/Fax PnP modem */
	{ .id = "SUP1590", .driver_data = 0 },
	/* SupraExpress 336i Sp ASVD */
	{ .id = "SUP1620", .driver_data = 0 },
	/* SupraExpress 33.6 Data/Fax PnP modem */
	{ .id = "SUP1760", .driver_data = 0 },
	/* SupraExpress 56i Sp Intl */
	{ .id = "SUP2171", .driver_data = 0 },
	/* Phoebe Micro */
	/* Phoebe Micro 33.6 Data Fax 1433VQH Plug & Play */
	{ .id = "TEX0011", .driver_data = 0 },
	/* Archtek America Corp. */
	/* Archtek SmartLink Modem 3334BT Plug & Play */
	{ .id = "UAC000F", .driver_data = 0 },
	/* 3Com Corp. */
	/* Gateway Telepath IIvi 33.6 */
	{ .id = "USR0000", .driver_data = 0 },
	/* U.S. Robotics Sporster 33.6K Fax INT PnP */
	{ .id = "USR0002", .driver_data = 0 },
	/*  Sportster Vi 14.4 PnP FAX Voicemail */
	{ .id = "USR0004", .driver_data = 0 },
	/* U.S. Robotics 33.6K Voice INT PnP */
	{ .id = "USR0006", .driver_data = 0 },
	/* U.S. Robotics 33.6K Voice EXT PnP */
	{ .id = "USR0007", .driver_data = 0 },
	/* U.S. Robotics Courier V.Everything INT PnP */
	{ .id = "USR0009", .driver_data = 0 },
	/* U.S. Robotics 33.6K Voice INT PnP */
	{ .id = "USR2002", .driver_data = 0 },
	/* U.S. Robotics 56K Voice INT PnP */
	{ .id = "USR2070", .driver_data = 0 },
	/* U.S. Robotics 56K Voice EXT PnP */
	{ .id = "USR2080", .driver_data = 0 },
	/* U.S. Robotics 56K FAX INT */
	{ .id = "USR3031", .driver_data = 0 },
	/* U.S. Robotics 56K FAX INT */
	{ .id = "USR3050", .driver_data = 0 },
	/* U.S. Robotics 56K Voice INT PnP */
	{ .id = "USR3070", .driver_data = 0 },
	/* U.S. Robotics 56K Voice EXT PnP */
	{ .id = "USR3080", .driver_data = 0 },
	/* U.S. Robotics 56K Voice INT PnP */
	{ .id = "USR3090", .driver_data = 0 },
	/* U.S. Robotics 56K Message  */
	{ .id = "USR9100", .driver_data = 0 },
	/* U.S. Robotics 56K FAX EXT PnP*/
	{ .id = "USR9160", .driver_data = 0 },
	/* U.S. Robotics 56K FAX INT PnP*/
	{ .id = "USR9170", .driver_data = 0 },
	/* U.S. Robotics 56K Voice EXT PnP*/
	{ .id = "USR9180", .driver_data = 0 },
	/* U.S. Robotics 56K Voice INT PnP*/
	{ .id = "USR9190", .driver_data = 0 },
	/* Wacom tablets */
	{ .id = "WACFXXX", .driver_data = 0 },
	/* Compaq touchscreen */
	{ .id = "FPI2002", .driver_data = 0 },
	/* Fujitsu Stylistic touchscreens */
	{ .id = "FUJ02B2", .driver_data = 0 },
	{ .id = "FUJ02B3", .driver_data = 0 },
	/* Fujitsu Stylistic LT touchscreens */
	{ .id = "FUJ02B4", .driver_data = 0 },
	/* Passive Fujitsu Stylistic touchscreens */
	{ .id = "FUJ02B6", .driver_data = 0 },
	{ .id = "FUJ02B7", .driver_data = 0 },
	{ .id = "FUJ02B8", .driver_data = 0 },
	{ .id = "FUJ02B9", .driver_data = 0 },
	{ .id = "FUJ02BC", .driver_data = 0 },
	/* Fujitsu Wacom Tablet PC device */
	{ .id = "FUJ02E5", .driver_data = 0 },
	/* Fujitsu P-series tablet PC device */
	{ .id = "FUJ02E6", .driver_data = 0 },
	/* Fujitsu Wacom 2FGT Tablet PC device */
	{ .id = "FUJ02E7", .driver_data = 0 },
	/* Fujitsu Wacom 1FGT Tablet PC device */
	{ .id = "FUJ02E9", .driver_data = 0 },
	/*
	 * LG C1 EXPRESS DUAL (C1-PB11A3) touch screen (actually a FUJ02E6
	 * in disguise).
	 */
	{ .id = "LTS0001", .driver_data = 0 },
	/* Rockwell's (PORALiNK) 33600 INT PNP */
	{ .id = "WCI0003", .driver_data = 0 },
	/* Unknown PnP modems */
	{ .id = "PNPCXXX", .driver_data = UNKNOWN_DEV },
	/* More unknown PnP modems */
	{ .id = "PNPDXXX", .driver_data = UNKNOWN_DEV },
	/*
	 * Winbond CIR port, should not be probed. We should keep track of
	 * it to prevent the legacy serial driver from probing it.
	 */
	{ .id = "WEC1022", .driver_data = CIR_PORT },
	/*
	 * SMSC IrCC SIR/FIR port, should not be probed by serial driver as
	 * well so its own driver can bind to it.
	 */
	{ .id = "SMCF010", .driver_data = CIR_PORT },
	{ }
};

MODULE_DEVICE_TABLE(pnp, pnp_dev_table);

static const char *modem_names[] = {
	"MODEM", "Modem", "modem", "FAX", "Fax", "fax",
	"56K", "56k", "K56", "33.6", "28.8", "14.4",
	"33,600", "28,800", "14,400", "33.600", "28.800", "14.400",
	"33600", "28800", "14400", "V.90", "V.34", "V.32", NULL
};

static bool check_name(const char *name)
{
	const char **tmp;

	for (tmp = modem_names; *tmp; tmp++)
		if (strstr(name, *tmp))
			return true;

	return false;
}

static bool check_resources(struct pnp_dev *dev)
{
	static const resource_size_t base[] = {0x2f8, 0x3f8, 0x2e8, 0x3e8};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(base); i++) {
		if (pnp_possible_config(dev, IORESOURCE_IO, base[i], 8))
			return true;
	}

	return false;
}

/*
 * Given a complete unknown PnP device, try to use some heuristics to
 * detect modems. Currently use such heuristic set:
 *     - dev->name or dev->bus->name must contain "modem" substring;
 *     - device must have only one IO region (8 byte long) with base address
 *       0x2e8, 0x3e8, 0x2f8 or 0x3f8.
 *
 * Such detection looks very ugly, but can detect at least some of numerous
 * PnP modems, alternatively we must hardcode all modems in pnp_devices[]
 * table.
 */
static int serial_pnp_guess_board(struct pnp_dev *dev)
{
	if (!(check_name(pnp_dev_name(dev)) ||
	    (dev->card && check_name(dev->card->name))))
		return -ENODEV;

	if (check_resources(dev))
		return 0;

	return -ENODEV;
}

static int
serial_pnp_probe(struct pnp_dev *dev, const struct pnp_device_id *dev_id)
{
	struct uart_8250_port uart, *port;
	int ret, flags = dev_id->driver_data;
	long line;

	if (flags & UNKNOWN_DEV) {
		ret = serial_pnp_guess_board(dev);
		if (ret < 0)
			return ret;
	}

	memset(&uart, 0, sizeof(uart));
	if ((flags & CIR_PORT) && pnp_port_valid(dev, 2)) {
		uart.port.iobase = pnp_port_start(dev, 2);
	} else if (pnp_port_valid(dev, 0)) {
		uart.port.iobase = pnp_port_start(dev, 0);
	} else if (pnp_mem_valid(dev, 0)) {
		uart.port.mapbase = pnp_mem_start(dev, 0);
		uart.port.mapsize = pnp_mem_len(dev, 0);
		uart.port.flags = UPF_IOREMAP;
	} else
		return -ENODEV;

	uart.port.uartclk = 1843200;
	uart.port.dev = &dev->dev;
	uart.port.flags |= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF;

	ret = uart_read_port_properties(&uart.port);
	/* no interrupt -> fall back to polling */
	if (ret == -ENXIO)
		ret = 0;
	if (ret)
		return ret;

	if (flags & CIR_PORT) {
		uart.port.flags |= UPF_FIXED_PORT | UPF_FIXED_TYPE;
		uart.port.type = PORT_8250_CIR;
	}

	dev_dbg(&dev->dev,
		 "Setup PNP port: port %#lx, mem %#llx, size %#llx, irq %u, type %u\n",
		 uart.port.iobase, (unsigned long long)uart.port.mapbase,
		 (unsigned long long)uart.port.mapsize, uart.port.irq, uart.port.iotype);

	line = serial8250_register_8250_port(&uart);
	if (line < 0 || (flags & CIR_PORT))
		return -ENODEV;

	port = serial8250_get_port(line);
	if (uart_console(&port->port))
		dev->capabilities |= PNP_CONSOLE;

	pnp_set_drvdata(dev, (void *)line);
	return 0;
}

static void serial_pnp_remove(struct pnp_dev *dev)
{
	long line = (long)pnp_get_drvdata(dev);

	dev->capabilities &= ~PNP_CONSOLE;
	serial8250_unregister_port(line);
}

static int serial_pnp_suspend(struct device *dev)
{
	long line = (long)dev_get_drvdata(dev);

	serial8250_suspend_port(line);
	return 0;
}

static int serial_pnp_resume(struct device *dev)
{
	long line = (long)dev_get_drvdata(dev);

	serial8250_resume_port(line);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(serial_pnp_pm_ops, serial_pnp_suspend, serial_pnp_resume);

static struct pnp_driver serial_pnp_driver = {
	.name		= "serial",
	.probe		= serial_pnp_probe,
	.remove		= serial_pnp_remove,
	.driver         = {
		.pm     = pm_sleep_ptr(&serial_pnp_pm_ops),
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
	},
	.id_table	= pnp_dev_table,
};

int serial8250_pnp_init(void)
{
	return pnp_register_driver(&serial_pnp_driver);
}

void serial8250_pnp_exit(void)
{
	pnp_unregister_driver(&serial_pnp_driver);
}

