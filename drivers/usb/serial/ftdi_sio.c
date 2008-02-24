/*
 * USB FTDI SIO driver
 *
 *	Copyright (C) 1999 - 2001
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *          Bill Ryder (bryder@sgi.com)
 *	Copyright (C) 2002
 *	    Kuba Ober (kuba@mareimbrium.org)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 * See http://ftdi-usb-sio.sourceforge.net for upto date testing info
 *	and extra documentation
 *
 * Change entries from 2004 and earlier can be found in versions of this
 * file in kernel versions prior to the 2.6.24 release.
 *
 */

/* Bill Ryder - bryder@sgi.com - wrote the FTDI_SIO implementation */
/* Thanx to FTDI for so kindly providing details of the protocol required */
/*   to talk to the device */
/* Thanx to gkh and the rest of the usb dev group for all code I have assimilated :-) */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/serial.h>
#include <linux/usb/serial.h>
#include "ftdi_sio.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.4.3"
#define DRIVER_AUTHOR "Greg Kroah-Hartman <greg@kroah.com>, Bill Ryder <bryder@sgi.com>, Kuba Ober <kuba@mareimbrium.org>"
#define DRIVER_DESC "USB FTDI Serial Converters Driver"

static int debug;
static __u16 vendor = FTDI_VID;
static __u16 product;

struct ftdi_private {
	ftdi_chip_type_t chip_type;
				/* type of the device, either SIO or FT8U232AM */
	int baud_base;		/* baud base clock for divisor setting */
	int custom_divisor;	/* custom_divisor kludge, this is for baud_base (different from what goes to the chip!) */
	__u16 last_set_data_urb_value ;
				/* the last data state set - needed for doing a break */
        int write_offset;       /* This is the offset in the usb data block to write the serial data -
				 * it is different between devices
				 */
	int flags;		/* some ASYNC_xxxx flags are supported */
	unsigned long last_dtr_rts;	/* saved modem control outputs */
        wait_queue_head_t delta_msr_wait; /* Used for TIOCMIWAIT */
	char prev_status, diff_status;        /* Used for TIOCMIWAIT */
	__u8 rx_flags;		/* receive state flags (throttling) */
	spinlock_t rx_lock;	/* spinlock for receive state */
	struct delayed_work rx_work;
	struct usb_serial_port *port;
	int rx_processed;
	unsigned long rx_bytes;

	__u16 interface;	/* FT2232C port interface (0 for FT232/245) */

	speed_t force_baud;	/* if non-zero, force the baud rate to this value */
	int force_rtscts;	/* if non-zero, force RTS-CTS to always be enabled */

	spinlock_t tx_lock;	/* spinlock for transmit state */
	unsigned long tx_bytes;
	unsigned long tx_outstanding_bytes;
	unsigned long tx_outstanding_urbs;
};

/* struct ftdi_sio_quirk is used by devices requiring special attention. */
struct ftdi_sio_quirk {
	int (*probe)(struct usb_serial *);
	void (*port_probe)(struct ftdi_private *); /* Special settings for probed ports. */
};

static int   ftdi_jtag_probe		(struct usb_serial *serial);
static int   ftdi_mtxorb_hack_setup	(struct usb_serial *serial);
static void  ftdi_USB_UIRT_setup	(struct ftdi_private *priv);
static void  ftdi_HE_TIRA1_setup	(struct ftdi_private *priv);

static struct ftdi_sio_quirk ftdi_jtag_quirk = {
	.probe	= ftdi_jtag_probe,
};

static struct ftdi_sio_quirk ftdi_mtxorb_hack_quirk = {
	.probe  = ftdi_mtxorb_hack_setup,
};

static struct ftdi_sio_quirk ftdi_USB_UIRT_quirk = {
	.port_probe = ftdi_USB_UIRT_setup,
};

static struct ftdi_sio_quirk ftdi_HE_TIRA1_quirk = {
	.port_probe = ftdi_HE_TIRA1_setup,
};

/*
 * The 8U232AM has the same API as the sio except for:
 * - it can support MUCH higher baudrates; up to:
 *   o 921600 for RS232 and 2000000 for RS422/485 at 48MHz
 *   o 230400 at 12MHz
 *   so .. 8U232AM's baudrate setting codes are different
 * - it has a two byte status code.
 * - it returns characters every 16ms (the FTDI does it every 40ms)
 *
 * the bcdDevice value is used to differentiate FT232BM and FT245BM from
 * the earlier FT8U232AM and FT8U232BM.  For now, include all known VID/PID
 * combinations in both tables.
 * FIXME: perhaps bcdDevice can also identify 12MHz FT8U232AM devices,
 * but I don't know if those ever went into mass production. [Ian Abbott]
 */



static struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(FTDI_VID, FTDI_AMC232_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_CANUSB_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ACTZWAVE_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_IRTRANS_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_IPLUS_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_IPLUS2_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_DMX4ALL) },
	{ USB_DEVICE(FTDI_VID, FTDI_SIO_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_8U232AM_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_8U232AM_ALT_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_232RL_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_8U2232C_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MICRO_CHAMELEON_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_RELAIS_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_OPENDCC_PID) },
	{ USB_DEVICE(INTERBIOMETRICS_VID, INTERBIOMETRICS_IOBOARD_PID) },
	{ USB_DEVICE(INTERBIOMETRICS_VID, INTERBIOMETRICS_MINI_IOBOARD_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_XF_632_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_XF_634_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_XF_547_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_XF_633_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_XF_631_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_XF_635_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_XF_640_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_XF_642_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_DSS20_PID) },
	{ USB_DEVICE(FTDI_NF_RIC_VID, FTDI_NF_RIC_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_VNHCPCUSB_D_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MTXORB_0_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MTXORB_1_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MTXORB_2_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MTXORB_3_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MTXORB_4_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MTXORB_5_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MTXORB_6_PID) },
	{ USB_DEVICE(MTXORB_VK_VID, MTXORB_VK_PID),
		.driver_info = (kernel_ulong_t)&ftdi_mtxorb_hack_quirk },
	{ USB_DEVICE(FTDI_VID, FTDI_PERLE_ULTRAPORT_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_PIEGROUP_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_TNC_X_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_USBX_707_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2101_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2102_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2103_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2104_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2106_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2201_1_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2201_2_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2202_1_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2202_2_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2203_1_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2203_2_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2401_1_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2401_2_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2401_3_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2401_4_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2402_1_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2402_2_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2402_3_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2402_4_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2403_1_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2403_2_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2403_3_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2403_4_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2801_1_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2801_2_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2801_3_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2801_4_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2801_5_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2801_6_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2801_7_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2801_8_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2802_1_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2802_2_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2802_3_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2802_4_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2802_5_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2802_6_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2802_7_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2802_8_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2803_1_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2803_2_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2803_3_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2803_4_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2803_5_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2803_6_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2803_7_PID) },
	{ USB_DEVICE(SEALEVEL_VID, SEALEVEL_2803_8_PID) },
	{ USB_DEVICE(IDTECH_VID, IDTECH_IDT1221U_PID) },
	{ USB_DEVICE(OCT_VID, OCT_US101_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_HE_TIRA1_PID),
		.driver_info = (kernel_ulong_t)&ftdi_HE_TIRA1_quirk },
	{ USB_DEVICE(FTDI_VID, FTDI_USB_UIRT_PID),
		.driver_info = (kernel_ulong_t)&ftdi_USB_UIRT_quirk },
	{ USB_DEVICE(FTDI_VID, PROTEGO_SPECIAL_1) },
	{ USB_DEVICE(FTDI_VID, PROTEGO_R2X0) },
	{ USB_DEVICE(FTDI_VID, PROTEGO_SPECIAL_3) },
	{ USB_DEVICE(FTDI_VID, PROTEGO_SPECIAL_4) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E808_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E809_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E80A_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E80B_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E80C_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E80D_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E80E_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E80F_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E888_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E889_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E88A_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E88B_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E88C_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E88D_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E88E_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GUDEADS_E88F_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_UO100_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_UM100_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_UR100_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_ALC8500_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_PYRAMID_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_FHZ1000PC_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_IBS_US485_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_IBS_PICPRO_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_IBS_PCMCIA_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_IBS_PK1_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_IBS_RS232MON_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_IBS_APP70_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_IBS_PEDO_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_IBS_PROD_PID) },
	/*
	 * Due to many user requests for multiple ELV devices we enable
	 * them by default.
	 */
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_CLI7000_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_PPS7330_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_TFM100_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_UDF77_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_UIO88_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_UAD8_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_UDA7_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_USI2_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_T1100_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_PCD200_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_ULA200_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_CSI8_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_EM1000DL_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_PCK100_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_RFP500_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_FS20SIG_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_WS300PC_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_FHZ1300PC_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_EM1010PC_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELV_WS500_PID) },
	{ USB_DEVICE(FTDI_VID, LINX_SDMUSBQSS_PID) },
	{ USB_DEVICE(FTDI_VID, LINX_MASTERDEVEL2_PID) },
	{ USB_DEVICE(FTDI_VID, LINX_FUTURE_0_PID) },
	{ USB_DEVICE(FTDI_VID, LINX_FUTURE_1_PID) },
	{ USB_DEVICE(FTDI_VID, LINX_FUTURE_2_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_CCSICDU20_0_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_CCSICDU40_1_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_CCSMACHX_2_PID) },
	{ USB_DEVICE(FTDI_VID, INSIDE_ACCESSO) },
	{ USB_DEVICE(INTREPID_VID, INTREPID_VALUECAN_PID) },
	{ USB_DEVICE(INTREPID_VID, INTREPID_NEOVI_PID) },
	{ USB_DEVICE(FALCOM_VID, FALCOM_TWIST_PID) },
	{ USB_DEVICE(FALCOM_VID, FALCOM_SAMBA_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_SUUNTO_SPORTS_PID) },
	{ USB_DEVICE(TTI_VID, TTI_QL355P_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_RM_CANVIEW_PID) },
	{ USB_DEVICE(BANDB_VID, BANDB_USOTL4_PID) },
	{ USB_DEVICE(BANDB_VID, BANDB_USTL4_PID) },
	{ USB_DEVICE(BANDB_VID, BANDB_USO9ML2_PID) },
	{ USB_DEVICE(FTDI_VID, EVER_ECO_PRO_CDS) },
	{ USB_DEVICE(FTDI_VID, FTDI_4N_GALAXY_DE_1_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_4N_GALAXY_DE_2_PID) },
	{ USB_DEVICE(FTDI_VID, XSENS_CONVERTER_0_PID) },
	{ USB_DEVICE(FTDI_VID, XSENS_CONVERTER_1_PID) },
	{ USB_DEVICE(FTDI_VID, XSENS_CONVERTER_2_PID) },
	{ USB_DEVICE(FTDI_VID, XSENS_CONVERTER_3_PID) },
	{ USB_DEVICE(FTDI_VID, XSENS_CONVERTER_4_PID) },
	{ USB_DEVICE(FTDI_VID, XSENS_CONVERTER_5_PID) },
	{ USB_DEVICE(FTDI_VID, XSENS_CONVERTER_6_PID) },
	{ USB_DEVICE(FTDI_VID, XSENS_CONVERTER_7_PID) },
	{ USB_DEVICE(MOBILITY_VID, MOBILITY_USB_SERIAL_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ACTIVE_ROBOTS_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MHAM_KW_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MHAM_YS_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MHAM_Y6_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MHAM_Y8_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MHAM_IC_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MHAM_DB9_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MHAM_RS232_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MHAM_Y9_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_TERATRONIK_VCP_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_TERATRONIK_D2XX_PID) },
	{ USB_DEVICE(EVOLUTION_VID, EVOLUTION_ER1_PID) },
	{ USB_DEVICE(EVOLUTION_VID, EVO_HYBRID_PID) },
	{ USB_DEVICE(EVOLUTION_VID, EVO_RCM4_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ARTEMIS_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ATIK_ATK16_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ATIK_ATK16C_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ATIK_ATK16HR_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ATIK_ATK16HRC_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ATIK_ATK16IC_PID) },
	{ USB_DEVICE(KOBIL_VID, KOBIL_CONV_B1_PID) },
	{ USB_DEVICE(KOBIL_VID, KOBIL_CONV_KAAN_PID) },
	{ USB_DEVICE(POSIFLEX_VID, POSIFLEX_PP7000_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_TTUSB_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ECLO_COM_1WIRE_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_WESTREX_MODEL_777_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_WESTREX_MODEL_8900F_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_PCDJ_DAC2_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_RRCIRKITS_LOCOBUFFER_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ASK_RDR400_PID) },
	{ USB_DEVICE(ICOM_ID1_VID, ICOM_ID1_PID) },
	{ USB_DEVICE(PAPOUCH_VID, PAPOUCH_TMU_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ACG_HFDUAL_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_YEI_SERVOCENTER31_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_THORLABS_PID) },
	{ USB_DEVICE(TESTO_VID, TESTO_USB_INTERFACE_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_GAMMA_SCOUT_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_TACTRIX_OPENPORT_13M_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_TACTRIX_OPENPORT_13S_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_TACTRIX_OPENPORT_13U_PID) },
	{ USB_DEVICE(ELEKTOR_VID, ELEKTOR_FT323R_PID) },
	{ USB_DEVICE(TELLDUS_VID, TELLDUS_TELLSTICK_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_MAXSTREAM_PID) },
	{ USB_DEVICE(TML_VID, TML_USB_SERIAL_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_ELSTER_UNICOM_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_PROPOX_JTAGCABLEII_PID) },
	{ USB_DEVICE(OLIMEX_VID, OLIMEX_ARM_USB_OCD_PID),
		.driver_info = (kernel_ulong_t)&ftdi_jtag_quirk },
	{ USB_DEVICE(FIC_VID, FIC_NEO1973_DEBUG_PID),
		.driver_info = (kernel_ulong_t)&ftdi_jtag_quirk },
	{ USB_DEVICE(FTDI_VID, FTDI_OOCDLINK_PID),
		.driver_info = (kernel_ulong_t)&ftdi_jtag_quirk },
	{ },					/* Optional parameter entry */
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table_combined);

static struct usb_driver ftdi_driver = {
	.name =		"ftdi_sio",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table_combined,
	.no_dynamic_id =	1,
};

static const char *ftdi_chip_name[] = {
	[SIO] = "SIO",	/* the serial part of FT8U100AX */
	[FT8U232AM] = "FT8U232AM",
	[FT232BM] = "FT232BM",
	[FT2232C] = "FT2232C",
	[FT232RL] = "FT232RL",
};


/* Constants for read urb and write urb */
#define BUFSZ 512
#define PKTSZ 64

/* rx_flags */
#define THROTTLED		0x01
#define ACTUALLY_THROTTLED	0x02

/* Used for TIOCMIWAIT */
#define FTDI_STATUS_B0_MASK	(FTDI_RS0_CTS | FTDI_RS0_DSR | FTDI_RS0_RI | FTDI_RS0_RLSD)
#define FTDI_STATUS_B1_MASK	(FTDI_RS_BI)
/* End TIOCMIWAIT */

#define FTDI_IMPL_ASYNC_FLAGS = (ASYNC_SPD_HI | ASYNC_SPD_VHI \
 | ASYNC_SPD_CUST | ASYNC_SPD_SHI | ASYNC_SPD_WARP)

/* function prototypes for a FTDI serial converter */
static int  ftdi_sio_probe	(struct usb_serial *serial, const struct usb_device_id *id);
static void ftdi_shutdown		(struct usb_serial *serial);
static int  ftdi_sio_port_probe	(struct usb_serial_port *port);
static int  ftdi_sio_port_remove	(struct usb_serial_port *port);
static int  ftdi_open			(struct usb_serial_port *port, struct file *filp);
static void ftdi_close			(struct usb_serial_port *port, struct file *filp);
static int  ftdi_write			(struct usb_serial_port *port, const unsigned char *buf, int count);
static int  ftdi_write_room		(struct usb_serial_port *port);
static int  ftdi_chars_in_buffer	(struct usb_serial_port *port);
static void ftdi_write_bulk_callback	(struct urb *urb);
static void ftdi_read_bulk_callback	(struct urb *urb);
static void ftdi_process_read		(struct work_struct *work);
static void ftdi_set_termios		(struct usb_serial_port *port, struct ktermios * old);
static int  ftdi_tiocmget               (struct usb_serial_port *port, struct file *file);
static int  ftdi_tiocmset		(struct usb_serial_port *port, struct file * file, unsigned int set, unsigned int clear);
static int  ftdi_ioctl			(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
static void ftdi_break_ctl		(struct usb_serial_port *port, int break_state );
static void ftdi_throttle		(struct usb_serial_port *port);
static void ftdi_unthrottle		(struct usb_serial_port *port);

static unsigned short int ftdi_232am_baud_base_to_divisor (int baud, int base);
static unsigned short int ftdi_232am_baud_to_divisor (int baud);
static __u32 ftdi_232bm_baud_base_to_divisor (int baud, int base);
static __u32 ftdi_232bm_baud_to_divisor (int baud);

static struct usb_serial_driver ftdi_sio_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"ftdi_sio",
	},
	.description =		"FTDI USB Serial Device",
	.usb_driver = 		&ftdi_driver ,
	.id_table =		id_table_combined,
	.num_ports =		1,
	.probe =		ftdi_sio_probe,
	.port_probe =		ftdi_sio_port_probe,
	.port_remove =		ftdi_sio_port_remove,
	.open =			ftdi_open,
	.close =		ftdi_close,
	.throttle =		ftdi_throttle,
	.unthrottle =		ftdi_unthrottle,
	.write =		ftdi_write,
	.write_room =		ftdi_write_room,
	.chars_in_buffer =	ftdi_chars_in_buffer,
	.read_bulk_callback =	ftdi_read_bulk_callback,
	.write_bulk_callback =	ftdi_write_bulk_callback,
	.tiocmget =             ftdi_tiocmget,
	.tiocmset =             ftdi_tiocmset,
	.ioctl =		ftdi_ioctl,
	.set_termios =		ftdi_set_termios,
	.break_ctl =		ftdi_break_ctl,
	.shutdown =		ftdi_shutdown,
};


#define WDR_TIMEOUT 5000 /* default urb timeout */
#define WDR_SHORT_TIMEOUT 1000	/* shorter urb timeout */

/* High and low are for DTR, RTS etc etc */
#define HIGH 1
#define LOW 0

/* number of outstanding urbs to prevent userspace DoS from happening */
#define URB_UPPER_LIMIT	42

/*
 * ***************************************************************************
 * Utility functions
 * ***************************************************************************
 */

static unsigned short int ftdi_232am_baud_base_to_divisor(int baud, int base)
{
	unsigned short int divisor;
	int divisor3 = base / 2 / baud; // divisor shifted 3 bits to the left
	if ((divisor3 & 0x7) == 7) divisor3 ++; // round x.7/8 up to x+1
	divisor = divisor3 >> 3;
	divisor3 &= 0x7;
	if (divisor3 == 1) divisor |= 0xc000; else // 0.125
	if (divisor3 >= 4) divisor |= 0x4000; else // 0.5
	if (divisor3 != 0) divisor |= 0x8000;      // 0.25
	if (divisor == 1) divisor = 0;	/* special case for maximum baud rate */
	return divisor;
}

static unsigned short int ftdi_232am_baud_to_divisor(int baud)
{
	 return(ftdi_232am_baud_base_to_divisor(baud, 48000000));
}

static __u32 ftdi_232bm_baud_base_to_divisor(int baud, int base)
{
	static const unsigned char divfrac[8] = { 0, 3, 2, 4, 1, 5, 6, 7 };
	__u32 divisor;
	int divisor3 = base / 2 / baud; // divisor shifted 3 bits to the left
	divisor = divisor3 >> 3;
	divisor |= (__u32)divfrac[divisor3 & 0x7] << 14;
	/* Deal with special cases for highest baud rates. */
	if (divisor == 1) divisor = 0; else	// 1.0
	if (divisor == 0x4001) divisor = 1;	// 1.5
	return divisor;
}

static __u32 ftdi_232bm_baud_to_divisor(int baud)
{
	 return(ftdi_232bm_baud_base_to_divisor(baud, 48000000));
}

#define set_mctrl(port, set)		update_mctrl((port), (set), 0)
#define clear_mctrl(port, clear)	update_mctrl((port), 0, (clear))

static int update_mctrl(struct usb_serial_port *port, unsigned int set, unsigned int clear)
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	char *buf;
	unsigned urb_value;
	int rv;

	if (((set | clear) & (TIOCM_DTR | TIOCM_RTS)) == 0) {
		dbg("%s - DTR|RTS not being set|cleared", __func__);
		return 0;	/* no change */
	}

	buf = kmalloc(1, GFP_NOIO);
	if (!buf)
		return -ENOMEM;

	clear &= ~set;	/* 'set' takes precedence over 'clear' */
	urb_value = 0;
	if (clear & TIOCM_DTR)
		urb_value |= FTDI_SIO_SET_DTR_LOW;
	if (clear & TIOCM_RTS)
		urb_value |= FTDI_SIO_SET_RTS_LOW;
	if (set & TIOCM_DTR)
		urb_value |= FTDI_SIO_SET_DTR_HIGH;
	if (set & TIOCM_RTS)
		urb_value |= FTDI_SIO_SET_RTS_HIGH;
	rv = usb_control_msg(port->serial->dev,
			       usb_sndctrlpipe(port->serial->dev, 0),
			       FTDI_SIO_SET_MODEM_CTRL_REQUEST,
			       FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
			       urb_value, priv->interface,
			       buf, 0, WDR_TIMEOUT);

	kfree(buf);
	if (rv < 0) {
		err("%s Error from MODEM_CTRL urb: DTR %s, RTS %s",
				__func__,
				(set & TIOCM_DTR) ? "HIGH" :
				(clear & TIOCM_DTR) ? "LOW" : "unchanged",
				(set & TIOCM_RTS) ? "HIGH" :
				(clear & TIOCM_RTS) ? "LOW" : "unchanged");
	} else {
		dbg("%s - DTR %s, RTS %s", __func__,
				(set & TIOCM_DTR) ? "HIGH" :
				(clear & TIOCM_DTR) ? "LOW" : "unchanged",
				(set & TIOCM_RTS) ? "HIGH" :
				(clear & TIOCM_RTS) ? "LOW" : "unchanged");
		/* FIXME: locking on last_dtr_rts */
		priv->last_dtr_rts = (priv->last_dtr_rts & ~clear) | set;
	}
	return rv;
}


static __u32 get_ftdi_divisor(struct usb_serial_port * port);


static int change_speed(struct usb_serial_port *port)
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	char *buf;
        __u16 urb_value;
	__u16 urb_index;
	__u32 urb_index_value;
	int rv;

	buf = kmalloc(1, GFP_NOIO);
	if (!buf)
		return -ENOMEM;

	urb_index_value = get_ftdi_divisor(port);
	urb_value = (__u16)urb_index_value;
	urb_index = (__u16)(urb_index_value >> 16);
	if (priv->interface) {	/* FT2232C */
		urb_index = (__u16)((urb_index << 8) | priv->interface);
	}

	rv = usb_control_msg(port->serial->dev,
			    usb_sndctrlpipe(port->serial->dev, 0),
			    FTDI_SIO_SET_BAUDRATE_REQUEST,
			    FTDI_SIO_SET_BAUDRATE_REQUEST_TYPE,
			    urb_value, urb_index,
			    buf, 0, WDR_SHORT_TIMEOUT);

	kfree(buf);
	return rv;
}


static __u32 get_ftdi_divisor(struct usb_serial_port * port)
{ /* get_ftdi_divisor */
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	__u32 div_value = 0;
	int div_okay = 1;
	int baud;

	/*
	 * The logic involved in setting the baudrate can be cleanly split in 3 steps.
	 * Obtaining the actual baud rate is a little tricky since unix traditionally
	 * somehow ignored the possibility to set non-standard baud rates.
	 * 1. Standard baud rates are set in tty->termios->c_cflag
	 * 2. If these are not enough, you can set any speed using alt_speed as follows:
	 *    - set tty->termios->c_cflag speed to B38400
	 *    - set your real speed in tty->alt_speed; it gets ignored when
	 *      alt_speed==0, (or)
	 *    - call TIOCSSERIAL ioctl with (struct serial_struct) set as follows:
	 *      flags & ASYNC_SPD_MASK == ASYNC_SPD_[HI, VHI, SHI, WARP], this just
	 *      sets alt_speed to (HI: 57600, VHI: 115200, SHI: 230400, WARP: 460800)
	 * ** Steps 1, 2 are done courtesy of tty_get_baud_rate
	 * 3. You can also set baud rate by setting custom divisor as follows
	 *    - set tty->termios->c_cflag speed to B38400
	 *    - call TIOCSSERIAL ioctl with (struct serial_struct) set as follows:
	 *      o flags & ASYNC_SPD_MASK == ASYNC_SPD_CUST
	 *      o custom_divisor set to baud_base / your_new_baudrate
	 * ** Step 3 is done courtesy of code borrowed from serial.c - I should really
	 *    spend some time and separate+move this common code to serial.c, it is
	 *    replicated in nearly every serial driver you see.
	 */

	/* 1. Get the baud rate from the tty settings, this observes alt_speed hack */

	baud = tty_get_baud_rate(port->tty);
	dbg("%s - tty_get_baud_rate reports speed %d", __func__, baud);

	/* 2. Observe async-compatible custom_divisor hack, update baudrate if needed */

	if (baud == 38400 &&
	    ((priv->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST) &&
	     (priv->custom_divisor)) {
		baud = priv->baud_base / priv->custom_divisor;
		dbg("%s - custom divisor %d sets baud rate to %d", __func__, priv->custom_divisor, baud);
	}

	/* 3. Convert baudrate to device-specific divisor */

	if (!baud) baud = 9600;
	switch(priv->chip_type) {
	case SIO: /* SIO chip */
		switch(baud) {
		case 300: div_value = ftdi_sio_b300; break;
		case 600: div_value = ftdi_sio_b600; break;
		case 1200: div_value = ftdi_sio_b1200; break;
		case 2400: div_value = ftdi_sio_b2400; break;
		case 4800: div_value = ftdi_sio_b4800; break;
		case 9600: div_value = ftdi_sio_b9600; break;
		case 19200: div_value = ftdi_sio_b19200; break;
		case 38400: div_value = ftdi_sio_b38400; break;
		case 57600: div_value = ftdi_sio_b57600;  break;
		case 115200: div_value = ftdi_sio_b115200; break;
		} /* baud */
		if (div_value == 0) {
			dbg("%s - Baudrate (%d) requested is not supported", __func__,  baud);
			div_value = ftdi_sio_b9600;
			baud = 9600;
			div_okay = 0;
		}
		break;
	case FT8U232AM: /* 8U232AM chip */
		if (baud <= 3000000) {
			div_value = ftdi_232am_baud_to_divisor(baud);
		} else {
	                dbg("%s - Baud rate too high!", __func__);
			baud = 9600;
			div_value = ftdi_232am_baud_to_divisor(9600);
			div_okay = 0;
		}
		break;
	case FT232BM: /* FT232BM chip */
	case FT2232C: /* FT2232C chip */
	case FT232RL:
		if (baud <= 3000000) {
			div_value = ftdi_232bm_baud_to_divisor(baud);
		} else {
	                dbg("%s - Baud rate too high!", __func__);
			div_value = ftdi_232bm_baud_to_divisor(9600);
			div_okay = 0;
			baud = 9600;
		}
		break;
	} /* priv->chip_type */

	if (div_okay) {
		dbg("%s - Baud rate set to %d (divisor 0x%lX) on chip %s",
			__func__, baud, (unsigned long)div_value,
			ftdi_chip_name[priv->chip_type]);
	}

	tty_encode_baud_rate(port->tty, baud, baud);
	return(div_value);
}


static int get_serial_info(struct usb_serial_port * port, struct serial_struct __user * retinfo)
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	struct serial_struct tmp;

	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.flags = priv->flags;
	tmp.baud_base = priv->baud_base;
	tmp.custom_divisor = priv->custom_divisor;
	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
} /* get_serial_info */


static int set_serial_info(struct usb_serial_port * port, struct serial_struct __user * newinfo)
{ /* set_serial_info */
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	struct serial_struct new_serial;
	struct ftdi_private old_priv;

	if (copy_from_user(&new_serial, newinfo, sizeof(new_serial)))
		return -EFAULT;
	old_priv = * priv;

	/* Do error checking and permission checking */

	if (!capable(CAP_SYS_ADMIN)) {
		if (((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (priv->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		priv->flags = ((priv->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		priv->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	if ((new_serial.baud_base != priv->baud_base) &&
	    (new_serial.baud_base < 9600))
		return -EINVAL;

	/* Make the changes - these are privileged changes! */

	priv->flags = ((priv->flags & ~ASYNC_FLAGS) |
	               (new_serial.flags & ASYNC_FLAGS));
	priv->custom_divisor = new_serial.custom_divisor;

	port->tty->low_latency = (priv->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

check_and_exit:
	if ((old_priv.flags & ASYNC_SPD_MASK) !=
	     (priv->flags & ASYNC_SPD_MASK)) {
		if ((priv->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
			port->tty->alt_speed = 57600;
		else if ((priv->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
			port->tty->alt_speed = 115200;
		else if ((priv->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
			port->tty->alt_speed = 230400;
		else if ((priv->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
			port->tty->alt_speed = 460800;
		else
			port->tty->alt_speed = 0;
	}
	if (((old_priv.flags & ASYNC_SPD_MASK) !=
	     (priv->flags & ASYNC_SPD_MASK)) ||
	    (((priv->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST) &&
	     (old_priv.custom_divisor != priv->custom_divisor))) {
		change_speed(port);
	}

	return (0);

} /* set_serial_info */


/* Determine type of FTDI chip based on USB config and descriptor. */
static void ftdi_determine_type(struct usb_serial_port *port)
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	struct usb_device *udev = serial->dev;
	unsigned version;
	unsigned interfaces;

	/* Assume it is not the original SIO device for now. */
	priv->baud_base = 48000000 / 2;
	priv->write_offset = 0;

	version = le16_to_cpu(udev->descriptor.bcdDevice);
	interfaces = udev->actconfig->desc.bNumInterfaces;
	dbg("%s: bcdDevice = 0x%x, bNumInterfaces = %u", __func__,
			version, interfaces);
	if (interfaces > 1) {
		int inter;

		/* Multiple interfaces.  Assume FT2232C. */
		priv->chip_type = FT2232C;
		/* Determine interface code. */
		inter = serial->interface->altsetting->desc.bInterfaceNumber;
		if (inter == 0) {
			priv->interface = PIT_SIOA;
		} else {
			priv->interface = PIT_SIOB;
		}
		/* BM-type devices have a bug where bcdDevice gets set
		 * to 0x200 when iSerialNumber is 0.  */
		if (version < 0x500) {
			dbg("%s: something fishy - bcdDevice too low for multi-interface device",
					__func__);
		}
	} else if (version < 0x200) {
		/* Old device.  Assume its the original SIO. */
		priv->chip_type = SIO;
		priv->baud_base = 12000000 / 16;
		priv->write_offset = 1;
	} else if (version < 0x400) {
		/* Assume its an FT8U232AM (or FT8U245AM) */
		/* (It might be a BM because of the iSerialNumber bug,
		 * but it will still work as an AM device.) */
		priv->chip_type = FT8U232AM;
	} else if (version < 0x600) {
		/* Assume its an FT232BM (or FT245BM) */
		priv->chip_type = FT232BM;
	} else {
		/* Assume its an FT232R  */
		priv->chip_type = FT232RL;
	}
	info("Detected %s", ftdi_chip_name[priv->chip_type]);
}


/*
 * ***************************************************************************
 * Sysfs Attribute
 * ***************************************************************************
 */

static ssize_t show_latency_timer(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_serial_port *port = to_usb_serial_port(dev);
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	struct usb_device *udev = port->serial->dev;
	unsigned short latency = 0;
	int rv = 0;


	dbg("%s",__func__);

	rv = usb_control_msg(udev,
			     usb_rcvctrlpipe(udev, 0),
			     FTDI_SIO_GET_LATENCY_TIMER_REQUEST,
			     FTDI_SIO_GET_LATENCY_TIMER_REQUEST_TYPE,
			     0, priv->interface,
			     (char*) &latency, 1, WDR_TIMEOUT);

	if (rv < 0) {
		dev_err(dev, "Unable to read latency timer: %i\n", rv);
		return -EIO;
	}
	return sprintf(buf, "%i\n", latency);
}

/* Write a new value of the latency timer, in units of milliseconds. */
static ssize_t store_latency_timer(struct device *dev, struct device_attribute *attr, const char *valbuf,
				   size_t count)
{
	struct usb_serial_port *port = to_usb_serial_port(dev);
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	struct usb_device *udev = port->serial->dev;
	char buf[1];
	int v = simple_strtoul(valbuf, NULL, 10);
	int rv = 0;

	dbg("%s: setting latency timer = %i", __func__, v);

	rv = usb_control_msg(udev,
			     usb_sndctrlpipe(udev, 0),
			     FTDI_SIO_SET_LATENCY_TIMER_REQUEST,
			     FTDI_SIO_SET_LATENCY_TIMER_REQUEST_TYPE,
			     v, priv->interface,
			     buf, 0, WDR_TIMEOUT);

	if (rv < 0) {
		dev_err(dev, "Unable to write latency timer: %i\n", rv);
		return -EIO;
	}

	return count;
}

/* Write an event character directly to the FTDI register.  The ASCII
   value is in the low 8 bits, with the enable bit in the 9th bit. */
static ssize_t store_event_char(struct device *dev, struct device_attribute *attr, const char *valbuf,
				size_t count)
{
	struct usb_serial_port *port = to_usb_serial_port(dev);
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	struct usb_device *udev = port->serial->dev;
	char buf[1];
	int v = simple_strtoul(valbuf, NULL, 10);
	int rv = 0;

	dbg("%s: setting event char = %i", __func__, v);

	rv = usb_control_msg(udev,
			     usb_sndctrlpipe(udev, 0),
			     FTDI_SIO_SET_EVENT_CHAR_REQUEST,
			     FTDI_SIO_SET_EVENT_CHAR_REQUEST_TYPE,
			     v, priv->interface,
			     buf, 0, WDR_TIMEOUT);

	if (rv < 0) {
		dbg("Unable to write event character: %i", rv);
		return -EIO;
	}

	return count;
}

static DEVICE_ATTR(latency_timer, S_IWUSR | S_IRUGO, show_latency_timer, store_latency_timer);
static DEVICE_ATTR(event_char, S_IWUSR, NULL, store_event_char);

static int create_sysfs_attrs(struct usb_serial_port *port)
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	int retval = 0;

	dbg("%s",__func__);

	/* XXX I've no idea if the original SIO supports the event_char
	 * sysfs parameter, so I'm playing it safe.  */
	if (priv->chip_type != SIO) {
		dbg("sysfs attributes for %s", ftdi_chip_name[priv->chip_type]);
		retval = device_create_file(&port->dev, &dev_attr_event_char);
		if ((!retval) &&
		    (priv->chip_type == FT232BM ||
		     priv->chip_type == FT2232C ||
		     priv->chip_type == FT232RL)) {
			retval = device_create_file(&port->dev,
						    &dev_attr_latency_timer);
		}
	}
	return retval;
}

static void remove_sysfs_attrs(struct usb_serial_port *port)
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);

	dbg("%s",__func__);

	/* XXX see create_sysfs_attrs */
	if (priv->chip_type != SIO) {
		device_remove_file(&port->dev, &dev_attr_event_char);
		if (priv->chip_type == FT232BM ||
		    priv->chip_type == FT2232C ||
		    priv->chip_type == FT232RL) {
			device_remove_file(&port->dev, &dev_attr_latency_timer);
		}
	}

}

/*
 * ***************************************************************************
 * FTDI driver specific functions
 * ***************************************************************************
 */

/* Probe function to check for special devices */
static int ftdi_sio_probe (struct usb_serial *serial, const struct usb_device_id *id)
{
	struct ftdi_sio_quirk *quirk = (struct ftdi_sio_quirk *)id->driver_info;

	if (quirk && quirk->probe) {
		int ret = quirk->probe(serial);
		if (ret != 0)
			return ret;
	}

	usb_set_serial_data(serial, (void *)id->driver_info);

	return 0;
}

static int ftdi_sio_port_probe(struct usb_serial_port *port)
{
	struct ftdi_private *priv;
	struct ftdi_sio_quirk *quirk = usb_get_serial_data(port->serial);


	dbg("%s",__func__);

	priv = kzalloc(sizeof(struct ftdi_private), GFP_KERNEL);
	if (!priv){
		err("%s- kmalloc(%Zd) failed.", __func__, sizeof(struct ftdi_private));
		return -ENOMEM;
	}

	spin_lock_init(&priv->rx_lock);
	spin_lock_init(&priv->tx_lock);
        init_waitqueue_head(&priv->delta_msr_wait);
	/* This will push the characters through immediately rather
	   than queue a task to deliver them */
	priv->flags = ASYNC_LOW_LATENCY;

	if (quirk && quirk->port_probe)
		quirk->port_probe(priv);

	/* Increase the size of read buffers */
	kfree(port->bulk_in_buffer);
	port->bulk_in_buffer = kmalloc (BUFSZ, GFP_KERNEL);
	if (!port->bulk_in_buffer) {
		kfree (priv);
		return -ENOMEM;
	}
	if (port->read_urb) {
		port->read_urb->transfer_buffer = port->bulk_in_buffer;
		port->read_urb->transfer_buffer_length = BUFSZ;
	}

	INIT_DELAYED_WORK(&priv->rx_work, ftdi_process_read);
	priv->port = port;

	/* Free port's existing write urb and transfer buffer. */
	if (port->write_urb) {
		usb_free_urb (port->write_urb);
		port->write_urb = NULL;
	}
	kfree(port->bulk_out_buffer);
	port->bulk_out_buffer = NULL;

	usb_set_serial_port_data(port, priv);

	ftdi_determine_type (port);
	create_sysfs_attrs(port);
	return 0;
}

/* Setup for the USB-UIRT device, which requires hardwired
 * baudrate (38400 gets mapped to 312500) */
/* Called from usbserial:serial_probe */
static void ftdi_USB_UIRT_setup (struct ftdi_private *priv)
{
	dbg("%s",__func__);

	priv->flags |= ASYNC_SPD_CUST;
	priv->custom_divisor = 77;
	priv->force_baud = 38400;
} /* ftdi_USB_UIRT_setup */

/* Setup for the HE-TIRA1 device, which requires hardwired
 * baudrate (38400 gets mapped to 100000) and RTS-CTS enabled.  */
static void ftdi_HE_TIRA1_setup (struct ftdi_private *priv)
{
	dbg("%s",__func__);

	priv->flags |= ASYNC_SPD_CUST;
	priv->custom_divisor = 240;
	priv->force_baud = 38400;
	priv->force_rtscts = 1;
} /* ftdi_HE_TIRA1_setup */

/*
 * First port on JTAG adaptors such as Olimex arm-usb-ocd or the FIC/OpenMoko
 * Neo1973 Debug Board is reserved for JTAG interface and can be accessed from
 * userspace using openocd.
 */
static int ftdi_jtag_probe(struct usb_serial *serial)
{
	struct usb_device *udev = serial->dev;
	struct usb_interface *interface = serial->interface;

	dbg("%s",__func__);

	if (interface == udev->actconfig->interface[0]) {
		info("Ignoring serial port reserved for JTAG");
		return -ENODEV;
	}

	return 0;
}

/*
 * The Matrix Orbital VK204-25-USB has an invalid IN endpoint.
 * We have to correct it if we want to read from it.
 */
static int ftdi_mtxorb_hack_setup(struct usb_serial *serial)
{
	struct usb_host_endpoint *ep = serial->dev->ep_in[1];
	struct usb_endpoint_descriptor *ep_desc = &ep->desc;

	if (ep->enabled && ep_desc->wMaxPacketSize == 0) {
		ep_desc->wMaxPacketSize = 0x40;
		info("Fixing invalid wMaxPacketSize on read pipe");
	}

	return 0;
}

/* ftdi_shutdown is called from usbserial:usb_serial_disconnect
 *   it is called when the usb device is disconnected
 *
 *   usbserial:usb_serial_disconnect
 *      calls __serial_close for each open of the port
 *      shutdown is called then (ie ftdi_shutdown)
 */
static void ftdi_shutdown (struct usb_serial *serial)
{
	dbg("%s", __func__);
}

static int ftdi_sio_port_remove(struct usb_serial_port *port)
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);

	dbg("%s", __func__);

	remove_sysfs_attrs(port);

	/* all open ports are closed at this point
         *    (by usbserial.c:__serial_close, which calls ftdi_close)
	 */

	if (priv) {
		usb_set_serial_port_data(port, NULL);
		kfree(priv);
	}

	return 0;
}

static int  ftdi_open (struct usb_serial_port *port, struct file *filp)
{ /* ftdi_open */
	struct usb_device *dev = port->serial->dev;
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	int result = 0;
	char buf[1]; /* Needed for the usb_control_msg I think */

	dbg("%s", __func__);

	spin_lock_irqsave(&priv->tx_lock, flags);
	priv->tx_bytes = 0;
	spin_unlock_irqrestore(&priv->tx_lock, flags);
	spin_lock_irqsave(&priv->rx_lock, flags);
	priv->rx_bytes = 0;
	spin_unlock_irqrestore(&priv->rx_lock, flags);

	if (port->tty)
		port->tty->low_latency = (priv->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	/* No error checking for this (will get errors later anyway) */
	/* See ftdi_sio.h for description of what is reset */
	usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			FTDI_SIO_RESET_REQUEST, FTDI_SIO_RESET_REQUEST_TYPE,
			FTDI_SIO_RESET_SIO,
			priv->interface, buf, 0, WDR_TIMEOUT);

	/* Termios defaults are set by usb_serial_init. We don't change
	   port->tty->termios - this would loose speed settings, etc.
	   This is same behaviour as serial.c/rs_open() - Kuba */

	/* ftdi_set_termios  will send usb control messages */
	if (port->tty)
		ftdi_set_termios(port, port->tty->termios);

	/* FIXME: Flow control might be enabled, so it should be checked -
	   we have no control of defaults! */
	/* Turn on RTS and DTR since we are not flow controlling by default */
	set_mctrl(port, TIOCM_DTR | TIOCM_RTS);

	/* Not throttled */
	spin_lock_irqsave(&priv->rx_lock, flags);
	priv->rx_flags &= ~(THROTTLED | ACTUALLY_THROTTLED);
	spin_unlock_irqrestore(&priv->rx_lock, flags);

	/* Start reading from the device */
	priv->rx_processed = 0;
	usb_fill_bulk_urb(port->read_urb, dev,
		      usb_rcvbulkpipe(dev, port->bulk_in_endpointAddress),
		      port->read_urb->transfer_buffer, port->read_urb->transfer_buffer_length,
		      ftdi_read_bulk_callback, port);
	result = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (result)
		err("%s - failed submitting read urb, error %d", __func__, result);


	return result;
} /* ftdi_open */



/*
 * usbserial:__serial_close  only calls ftdi_close if the point is open
 *
 *   This only gets called when it is the last close
 *
 *
 */

static void ftdi_close (struct usb_serial_port *port, struct file *filp)
{ /* ftdi_close */
	unsigned int c_cflag = port->tty->termios->c_cflag;
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	char buf[1];

	dbg("%s", __func__);

	mutex_lock(&port->serial->disc_mutex);
	if (c_cflag & HUPCL && !port->serial->disconnected){
		/* Disable flow control */
		if (usb_control_msg(port->serial->dev,
				    usb_sndctrlpipe(port->serial->dev, 0),
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST,
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
				    0, priv->interface, buf, 0,
				    WDR_TIMEOUT) < 0) {
			err("error from flowcontrol urb");
		}

		/* drop RTS and DTR */
		clear_mctrl(port, TIOCM_DTR | TIOCM_RTS);
	} /* Note change no line if hupcl is off */
	mutex_unlock(&port->serial->disc_mutex);

	/* cancel any scheduled reading */
	cancel_delayed_work(&priv->rx_work);
	flush_scheduled_work();

	/* shutdown our bulk read */
	usb_kill_urb(port->read_urb);
} /* ftdi_close */



/* The SIO requires the first byte to have:
 *  B0 1
 *  B1 0
 *  B2..7 length of message excluding byte 0
 *
 * The new devices do not require this byte
 */
static int ftdi_write (struct usb_serial_port *port,
			   const unsigned char *buf, int count)
{ /* ftdi_write */
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	struct urb *urb;
	unsigned char *buffer;
	int data_offset ;       /* will be 1 for the SIO and 0 otherwise */
	int status;
	int transfer_size;
	unsigned long flags;

	dbg("%s port %d, %d bytes", __func__, port->number, count);

	if (count == 0) {
		dbg("write request of 0 bytes");
		return 0;
	}
	spin_lock_irqsave(&priv->tx_lock, flags);
	if (priv->tx_outstanding_urbs > URB_UPPER_LIMIT) {
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		dbg("%s - write limit hit\n", __func__);
		return 0;
	}
	priv->tx_outstanding_urbs++;
	spin_unlock_irqrestore(&priv->tx_lock, flags);

	data_offset = priv->write_offset;
        dbg("data_offset set to %d",data_offset);

	/* Determine total transfer size */
	transfer_size = count;
	if (data_offset > 0) {
		/* Original sio needs control bytes too... */
		transfer_size += (data_offset *
				((count + (PKTSZ - 1 - data_offset)) /
				 (PKTSZ - data_offset)));
	}

	buffer = kmalloc (transfer_size, GFP_ATOMIC);
	if (!buffer) {
		err("%s ran out of kernel memory for urb ...", __func__);
		count = -ENOMEM;
		goto error_no_buffer;
	}

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		err("%s - no more free urbs", __func__);
		count = -ENOMEM;
		goto error_no_urb;
	}

	/* Copy data */
	if (data_offset > 0) {
		/* Original sio requires control byte at start of each packet. */
		int user_pktsz = PKTSZ - data_offset;
		int todo = count;
		unsigned char *first_byte = buffer;
		const unsigned char *current_position = buf;

		while (todo > 0) {
			if (user_pktsz > todo) {
				user_pktsz = todo;
			}
			/* Write the control byte at the front of the packet*/
			*first_byte = 1 | ((user_pktsz) << 2);
			/* Copy data for packet */
			memcpy (first_byte + data_offset,
				current_position, user_pktsz);
			first_byte += user_pktsz + data_offset;
			current_position += user_pktsz;
			todo -= user_pktsz;
		}
	} else {
		/* No control byte required. */
		/* Copy in the data to send */
		memcpy (buffer, buf, count);
	}

	usb_serial_debug_data(debug, &port->dev, __func__, transfer_size, buffer);

	/* fill the buffer and send it */
	usb_fill_bulk_urb(urb, port->serial->dev,
		      usb_sndbulkpipe(port->serial->dev, port->bulk_out_endpointAddress),
		      buffer, transfer_size,
		      ftdi_write_bulk_callback, port);

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		err("%s - failed submitting write urb, error %d", __func__, status);
		count = status;
		goto error;
	} else {
		spin_lock_irqsave(&priv->tx_lock, flags);
		priv->tx_outstanding_bytes += count;
		priv->tx_bytes += count;
		spin_unlock_irqrestore(&priv->tx_lock, flags);
	}

	/* we are done with this urb, so let the host driver
	 * really free it when it is finished with it */
	usb_free_urb(urb);

	dbg("%s write returning: %d", __func__, count);
	return count;
error:
	usb_free_urb(urb);
error_no_urb:
	kfree (buffer);
error_no_buffer:
	spin_lock_irqsave(&priv->tx_lock, flags);
	priv->tx_outstanding_urbs--;
	spin_unlock_irqrestore(&priv->tx_lock, flags);
	return count;
} /* ftdi_write */


/* This function may get called when the device is closed */

static void ftdi_write_bulk_callback (struct urb *urb)
{
	unsigned long flags;
	struct usb_serial_port *port = urb->context;
	struct ftdi_private *priv;
	int data_offset;       /* will be 1 for the SIO and 0 otherwise */
	unsigned long countback;
	int status = urb->status;

	/* free up the transfer buffer, as usb_free_urb() does not do this */
	kfree (urb->transfer_buffer);

	dbg("%s - port %d", __func__, port->number);

	if (status) {
		dbg("nonzero write bulk status received: %d", status);
		return;
	}

	priv = usb_get_serial_port_data(port);
	if (!priv) {
		dbg("%s - bad port private data pointer - exiting", __func__);
		return;
	}
	/* account for transferred data */
	countback = urb->actual_length;
	data_offset = priv->write_offset;
	if (data_offset > 0) {
		/* Subtract the control bytes */
		countback -= (data_offset * DIV_ROUND_UP(countback, PKTSZ));
	}
	spin_lock_irqsave(&priv->tx_lock, flags);
	--priv->tx_outstanding_urbs;
	priv->tx_outstanding_bytes -= countback;
	spin_unlock_irqrestore(&priv->tx_lock, flags);

	usb_serial_port_softint(port);
} /* ftdi_write_bulk_callback */


static int ftdi_write_room( struct usb_serial_port *port )
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	int room;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->tx_lock, flags);
	if (priv->tx_outstanding_urbs < URB_UPPER_LIMIT) {
		/*
		 * We really can take anything the user throws at us
		 * but let's pick a nice big number to tell the tty
		 * layer that we have lots of free space
		 */
		room = 2048;
	} else {
		room = 0;
	}
	spin_unlock_irqrestore(&priv->tx_lock, flags);
	return room;
} /* ftdi_write_room */


static int ftdi_chars_in_buffer (struct usb_serial_port *port)
{ /* ftdi_chars_in_buffer */
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	int buffered;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->tx_lock, flags);
	buffered = (int)priv->tx_outstanding_bytes;
	spin_unlock_irqrestore(&priv->tx_lock, flags);
	if (buffered < 0) {
		err("%s outstanding tx bytes is negative!", __func__);
		buffered = 0;
	}
	return buffered;
} /* ftdi_chars_in_buffer */



static void ftdi_read_bulk_callback (struct urb *urb)
{ /* ftdi_read_bulk_callback */
	struct usb_serial_port *port = urb->context;
	struct tty_struct *tty;
	struct ftdi_private *priv;
	unsigned long countread;
	unsigned long flags;
	int status = urb->status;

	if (urb->number_of_packets > 0) {
		err("%s transfer_buffer_length %d actual_length %d number of packets %d",__func__,
		    urb->transfer_buffer_length, urb->actual_length, urb->number_of_packets );
		err("%s transfer_flags %x ", __func__,urb->transfer_flags );
	}

	dbg("%s - port %d", __func__, port->number);

	if (port->open_count <= 0)
		return;

	tty = port->tty;
	if (!tty) {
		dbg("%s - bad tty pointer - exiting",__func__);
		return;
	}

	priv = usb_get_serial_port_data(port);
	if (!priv) {
		dbg("%s - bad port private data pointer - exiting", __func__);
		return;
	}

	if (urb != port->read_urb) {
		err("%s - Not my urb!", __func__);
	}

	if (status) {
		/* This will happen at close every time so it is a dbg not an err */
		dbg("(this is ok on close) nonzero read bulk status received: "
		    "%d", status);
		return;
	}

	/* count data bytes, but not status bytes */
	countread = urb->actual_length;
	countread -= 2 * DIV_ROUND_UP(countread, PKTSZ);
	spin_lock_irqsave(&priv->rx_lock, flags);
	priv->rx_bytes += countread;
	spin_unlock_irqrestore(&priv->rx_lock, flags);

	ftdi_process_read(&priv->rx_work.work);

} /* ftdi_read_bulk_callback */


static void ftdi_process_read (struct work_struct *work)
{ /* ftdi_process_read */
	struct ftdi_private *priv =
		container_of(work, struct ftdi_private, rx_work.work);
	struct usb_serial_port *port = priv->port;
	struct urb *urb;
	struct tty_struct *tty;
	char error_flag;
	unsigned char *data;

	int i;
	int result;
	int need_flip;
	int packet_offset;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	if (port->open_count <= 0)
		return;

	tty = port->tty;
	if (!tty) {
		dbg("%s - bad tty pointer - exiting",__func__);
		return;
	}

	priv = usb_get_serial_port_data(port);
	if (!priv) {
		dbg("%s - bad port private data pointer - exiting", __func__);
		return;
	}

	urb = port->read_urb;
	if (!urb) {
		dbg("%s - bad read_urb pointer - exiting", __func__);
		return;
	}

	data = urb->transfer_buffer;

	if (priv->rx_processed) {
		dbg("%s - already processed: %d bytes, %d remain", __func__,
				priv->rx_processed,
				urb->actual_length - priv->rx_processed);
	} else {
		/* The first two bytes of every read packet are status */
		if (urb->actual_length > 2) {
			usb_serial_debug_data(debug, &port->dev, __func__, urb->actual_length, data);
		} else {
			dbg("Status only: %03oo %03oo",data[0],data[1]);
		}
	}


	/* TO DO -- check for hung up line and handle appropriately: */
	/*   send hangup  */
	/* See acm.c - you do a tty_hangup  - eg tty_hangup(tty) */
	/* if CD is dropped and the line is not CLOCAL then we should hangup */

	need_flip = 0;
	for (packet_offset = priv->rx_processed; packet_offset < urb->actual_length; packet_offset += PKTSZ) {
		int length;

		/* Compare new line status to the old one, signal if different */
		/* N.B. packet may be processed more than once, but differences
		 * are only processed once.  */
		if (priv != NULL) {
			char new_status = data[packet_offset+0] & FTDI_STATUS_B0_MASK;
			if (new_status != priv->prev_status) {
				priv->diff_status |= new_status ^ priv->prev_status;
				wake_up_interruptible(&priv->delta_msr_wait);
				priv->prev_status = new_status;
			}
		}

		length = min(PKTSZ, urb->actual_length-packet_offset)-2;
		if (length < 0) {
			err("%s - bad packet length: %d", __func__, length+2);
			length = 0;
		}

		if (priv->rx_flags & THROTTLED) {
			dbg("%s - throttled", __func__);
			break;
		}
		if (tty_buffer_request_room(tty, length) < length) {
			/* break out & wait for throttling/unthrottling to happen */
			dbg("%s - receive room low", __func__);
			break;
		}

		/* Handle errors and break */
		error_flag = TTY_NORMAL;
		/* Although the device uses a bitmask and hence can have multiple */
		/* errors on a packet - the order here sets the priority the */
		/* error is returned to the tty layer  */

		if ( data[packet_offset+1] & FTDI_RS_OE ) {
			error_flag = TTY_OVERRUN;
			dbg("OVERRRUN error");
		}
		if ( data[packet_offset+1] & FTDI_RS_BI ) {
			error_flag = TTY_BREAK;
			dbg("BREAK received");
		}
		if ( data[packet_offset+1] & FTDI_RS_PE ) {
			error_flag = TTY_PARITY;
			dbg("PARITY error");
		}
		if ( data[packet_offset+1] & FTDI_RS_FE ) {
			error_flag = TTY_FRAME;
			dbg("FRAMING error");
		}
		if (length > 0) {
			for (i = 2; i < length+2; i++) {
				/* Note that the error flag is duplicated for
				   every character received since we don't know
				   which character it applied to */
				tty_insert_flip_char(tty, data[packet_offset+i], error_flag);
			}
			need_flip = 1;
		}

#ifdef NOT_CORRECT_BUT_KEEPING_IT_FOR_NOW
		/* if a parity error is detected you get status packets forever
		   until a character is sent without a parity error.
		   This doesn't work well since the application receives a never
		   ending stream of bad data - even though new data hasn't been sent.
		   Therefore I (bill) have taken this out.
		   However - this might make sense for framing errors and so on
		   so I am leaving the code in for now.
		*/
		else {
			if (error_flag != TTY_NORMAL){
				dbg("error_flag is not normal");
				/* In this case it is just status - if that is an error send a bad character */
				if(tty->flip.count >= TTY_FLIPBUF_SIZE) {
					tty_flip_buffer_push(tty);
				}
				tty_insert_flip_char(tty, 0xff, error_flag);
				need_flip = 1;
			}
		}
#endif
	} /* "for(packet_offset=0..." */

	/* Low latency */
	if (need_flip) {
		tty_flip_buffer_push(tty);
	}

	if (packet_offset < urb->actual_length) {
		/* not completely processed - record progress */
		priv->rx_processed = packet_offset;
		dbg("%s - incomplete, %d bytes processed, %d remain",
				__func__, packet_offset,
				urb->actual_length - packet_offset);
		/* check if we were throttled while processing */
		spin_lock_irqsave(&priv->rx_lock, flags);
		if (priv->rx_flags & THROTTLED) {
			priv->rx_flags |= ACTUALLY_THROTTLED;
			spin_unlock_irqrestore(&priv->rx_lock, flags);
			dbg("%s - deferring remainder until unthrottled",
					__func__);
			return;
		}
		spin_unlock_irqrestore(&priv->rx_lock, flags);
		/* if the port is closed stop trying to read */
		if (port->open_count > 0){
			/* delay processing of remainder */
			schedule_delayed_work(&priv->rx_work, 1);
		} else {
			dbg("%s - port is closed", __func__);
		}
		return;
	}

	/* urb is completely processed */
	priv->rx_processed = 0;

	/* if the port is closed stop trying to read */
	if (port->open_count > 0){
		/* Continue trying to always read  */
		usb_fill_bulk_urb(port->read_urb, port->serial->dev,
			      usb_rcvbulkpipe(port->serial->dev, port->bulk_in_endpointAddress),
			      port->read_urb->transfer_buffer, port->read_urb->transfer_buffer_length,
			      ftdi_read_bulk_callback, port);

		result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
		if (result)
			err("%s - failed resubmitting read urb, error %d", __func__, result);
	}

	return;
} /* ftdi_process_read */


static void ftdi_break_ctl( struct usb_serial_port *port, int break_state )
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	__u16 urb_value = 0;
	char buf[1];

	/* break_state = -1 to turn on break, and 0 to turn off break */
	/* see drivers/char/tty_io.c to see it used */
	/* last_set_data_urb_value NEVER has the break bit set in it */

	if (break_state) {
		urb_value = priv->last_set_data_urb_value | FTDI_SIO_SET_BREAK;
	} else {
		urb_value = priv->last_set_data_urb_value;
	}


	if (usb_control_msg(port->serial->dev, usb_sndctrlpipe(port->serial->dev, 0),
			    FTDI_SIO_SET_DATA_REQUEST,
			    FTDI_SIO_SET_DATA_REQUEST_TYPE,
			    urb_value , priv->interface,
			    buf, 0, WDR_TIMEOUT) < 0) {
		err("%s FAILED to enable/disable break state (state was %d)", __func__,break_state);
	}

	dbg("%s break state is %d - urb is %d", __func__,break_state, urb_value);

}


/* old_termios contains the original termios settings and tty->termios contains
 * the new setting to be used
 * WARNING: set_termios calls this with old_termios in kernel space
 */

static void ftdi_set_termios (struct usb_serial_port *port, struct ktermios *old_termios)
{ /* ftdi_termios */
	struct usb_device *dev = port->serial->dev;
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	struct ktermios *termios = port->tty->termios;
	unsigned int cflag = termios->c_cflag;
	__u16 urb_value; /* will hold the new flags */
	char buf[1]; /* Perhaps I should dynamically alloc this? */

	// Added for xon/xoff support
	unsigned int iflag = termios->c_iflag;
	unsigned char vstop;
	unsigned char vstart;

	dbg("%s", __func__);

	/* Force baud rate if this device requires it, unless it is set to B0. */
	if (priv->force_baud && ((termios->c_cflag & CBAUD) != B0)) {
		dbg("%s: forcing baud rate for this device", __func__);
		tty_encode_baud_rate(port->tty, priv->force_baud,
					priv->force_baud);
	}

	/* Force RTS-CTS if this device requires it. */
	if (priv->force_rtscts) {
		dbg("%s: forcing rtscts for this device", __func__);
		termios->c_cflag |= CRTSCTS;
	}

	cflag = termios->c_cflag;

	/* FIXME -For this cut I don't care if the line is really changing or
	   not  - so just do the change regardless  - should be able to
	   compare old_termios and tty->termios */
	/* NOTE These routines can get interrupted by
	   ftdi_sio_read_bulk_callback  - need to examine what this
           means - don't see any problems yet */

	/* Set number of data bits, parity, stop bits */

	termios->c_cflag &= ~CMSPAR;

	urb_value = 0;
	urb_value |= (cflag & CSTOPB ? FTDI_SIO_SET_DATA_STOP_BITS_2 :
		      FTDI_SIO_SET_DATA_STOP_BITS_1);
	urb_value |= (cflag & PARENB ?
		      (cflag & PARODD ? FTDI_SIO_SET_DATA_PARITY_ODD :
		       FTDI_SIO_SET_DATA_PARITY_EVEN) :
		      FTDI_SIO_SET_DATA_PARITY_NONE);
	if (cflag & CSIZE) {
		switch (cflag & CSIZE) {
		case CS5: urb_value |= 5; dbg("Setting CS5"); break;
		case CS6: urb_value |= 6; dbg("Setting CS6"); break;
		case CS7: urb_value |= 7; dbg("Setting CS7"); break;
		case CS8: urb_value |= 8; dbg("Setting CS8"); break;
		default:
			err("CSIZE was set but not CS5-CS8");
		}
	}

	/* This is needed by the break command since it uses the same command - but is
	 *  or'ed with this value  */
	priv->last_set_data_urb_value = urb_value;

	if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			    FTDI_SIO_SET_DATA_REQUEST,
			    FTDI_SIO_SET_DATA_REQUEST_TYPE,
			    urb_value , priv->interface,
			    buf, 0, WDR_SHORT_TIMEOUT) < 0) {
		err("%s FAILED to set databits/stopbits/parity", __func__);
	}

	/* Now do the baudrate */
	if ((cflag & CBAUD) == B0 ) {
		/* Disable flow control */
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST,
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
				    0, priv->interface,
				    buf, 0, WDR_TIMEOUT) < 0) {
			err("%s error from disable flowcontrol urb", __func__);
		}
		/* Drop RTS and DTR */
		clear_mctrl(port, TIOCM_DTR | TIOCM_RTS);
	} else {
		/* set the baudrate determined before */
		if (change_speed(port)) {
			err("%s urb failed to set baudrate", __func__);
		}
		/* Ensure RTS and DTR are raised when baudrate changed from 0 */
		if (!old_termios || (old_termios->c_cflag & CBAUD) == B0) {
			set_mctrl(port, TIOCM_DTR | TIOCM_RTS);
		}
	}

	/* Set flow control */
	/* Note device also supports DTR/CD (ugh) and Xon/Xoff in hardware */
	if (cflag & CRTSCTS) {
		dbg("%s Setting to CRTSCTS flow control", __func__);
		if (usb_control_msg(dev,
				    usb_sndctrlpipe(dev, 0),
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST,
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
				    0 , (FTDI_SIO_RTS_CTS_HS | priv->interface),
				    buf, 0, WDR_TIMEOUT) < 0) {
			err("urb failed to set to rts/cts flow control");
		}

	} else {
		/*
		 * Xon/Xoff code
		 *
		 * Check the IXOFF status in the iflag component of the termios structure
		 * if IXOFF is not set, the pre-xon/xoff code is executed.
		*/
		if (iflag & IXOFF) {
			dbg("%s  request to enable xonxoff iflag=%04x",__func__,iflag);
			// Try to enable the XON/XOFF on the ftdi_sio
			// Set the vstart and vstop -- could have been done up above where
			// a lot of other dereferencing is done but that would be very
			// inefficient as vstart and vstop are not always needed
			vstart = termios->c_cc[VSTART];
			vstop = termios->c_cc[VSTOP];
			urb_value=(vstop << 8) | (vstart);

			if (usb_control_msg(dev,
					    usb_sndctrlpipe(dev, 0),
					    FTDI_SIO_SET_FLOW_CTRL_REQUEST,
					    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
					    urb_value , (FTDI_SIO_XON_XOFF_HS
							 | priv->interface),
					    buf, 0, WDR_TIMEOUT) < 0) {
				err("urb failed to set to xon/xoff flow control");
			}
		} else {
			/* else clause to only run if cfag ! CRTSCTS and iflag ! XOFF */
			/* CHECKME Assuming XON/XOFF handled by tty stack - not by device */
			dbg("%s Turning off hardware flow control", __func__);
			if (usb_control_msg(dev,
					    usb_sndctrlpipe(dev, 0),
					    FTDI_SIO_SET_FLOW_CTRL_REQUEST,
					    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
					    0, priv->interface,
					    buf, 0, WDR_TIMEOUT) < 0) {
				err("urb failed to clear flow control");
			}
		}

	}
	return;
} /* ftdi_termios */


static int ftdi_tiocmget (struct usb_serial_port *port, struct file *file)
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	unsigned char buf[2];
	int ret;

	dbg("%s TIOCMGET", __func__);
	switch (priv->chip_type) {
	case SIO:
		/* Request the status from the device */
		if ((ret = usb_control_msg(port->serial->dev,
					   usb_rcvctrlpipe(port->serial->dev, 0),
					   FTDI_SIO_GET_MODEM_STATUS_REQUEST,
					   FTDI_SIO_GET_MODEM_STATUS_REQUEST_TYPE,
					   0, 0,
					   buf, 1, WDR_TIMEOUT)) < 0 ) {
			err("%s Could not get modem status of device - err: %d", __func__,
			    ret);
			return(ret);
		}
		break;
	case FT8U232AM:
	case FT232BM:
	case FT2232C:
	case FT232RL:
		/* the 8U232AM returns a two byte value (the sio is a 1 byte value) - in the same
		   format as the data returned from the in point */
		if ((ret = usb_control_msg(port->serial->dev,
					   usb_rcvctrlpipe(port->serial->dev, 0),
					   FTDI_SIO_GET_MODEM_STATUS_REQUEST,
					   FTDI_SIO_GET_MODEM_STATUS_REQUEST_TYPE,
					   0, priv->interface,
					   buf, 2, WDR_TIMEOUT)) < 0 ) {
			err("%s Could not get modem status of device - err: %d", __func__,
			    ret);
			return(ret);
		}
		break;
	default:
		return -EFAULT;
		break;
	}

	return  (buf[0] & FTDI_SIO_DSR_MASK ? TIOCM_DSR : 0) |
		(buf[0] & FTDI_SIO_CTS_MASK ? TIOCM_CTS : 0) |
		(buf[0]  & FTDI_SIO_RI_MASK  ? TIOCM_RI  : 0) |
		(buf[0]  & FTDI_SIO_RLSD_MASK ? TIOCM_CD  : 0) |
		priv->last_dtr_rts;
}

static int ftdi_tiocmset(struct usb_serial_port *port, struct file * file, unsigned int set, unsigned int clear)
{
	dbg("%s TIOCMSET", __func__);
	return update_mctrl(port, set, clear);
}


static int ftdi_ioctl (struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);

	dbg("%s cmd 0x%04x", __func__, cmd);

	/* Based on code from acm.c and others */
	switch (cmd) {

	case TIOCGSERIAL: /* gets serial port data */
		return get_serial_info(port, (struct serial_struct __user *) arg);

	case TIOCSSERIAL: /* sets serial port data */
		return set_serial_info(port, (struct serial_struct __user *) arg);

	/*
	 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
	 * - mask passed in arg for lines of interest
	 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
	 * Caller should use TIOCGICOUNT to see which one it was.
	 *
	 * This code is borrowed from linux/drivers/char/serial.c
	 */
	case TIOCMIWAIT:
		while (priv != NULL) {
			interruptible_sleep_on(&priv->delta_msr_wait);
			/* see if a signal did it */
			if (signal_pending(current))
				return -ERESTARTSYS;
			else {
				char diff = priv->diff_status;

				if (diff == 0) {
					return -EIO; /* no change => error */
				}

				/* Consume all events */
				priv->diff_status = 0;

				/* Return 0 if caller wanted to know about these bits */
				if ( ((arg & TIOCM_RNG) && (diff & FTDI_RS0_RI)) ||
				     ((arg & TIOCM_DSR) && (diff & FTDI_RS0_DSR)) ||
				     ((arg & TIOCM_CD)  && (diff & FTDI_RS0_RLSD)) ||
				     ((arg & TIOCM_CTS) && (diff & FTDI_RS0_CTS)) ) {
					return 0;
				}
				/*
				 * Otherwise caller can't care less about what happened,
				 * and so we continue to wait for more events.
				 */
			}
		}
		return(0);
		break;
	default:
		break;

	}


	/* This is not necessarily an error - turns out the higher layers will do
	 *  some ioctls itself (see comment above)
	 */
	dbg("%s arg not supported - it was 0x%04x - check /usr/include/asm/ioctls.h", __func__, cmd);

	return(-ENOIOCTLCMD);
} /* ftdi_ioctl */


static void ftdi_throttle (struct usb_serial_port *port)
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->rx_lock, flags);
	priv->rx_flags |= THROTTLED;
	spin_unlock_irqrestore(&priv->rx_lock, flags);
}


static void ftdi_unthrottle (struct usb_serial_port *port)
{
	struct ftdi_private *priv = usb_get_serial_port_data(port);
	int actually_throttled;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->rx_lock, flags);
	actually_throttled = priv->rx_flags & ACTUALLY_THROTTLED;
	priv->rx_flags &= ~(THROTTLED | ACTUALLY_THROTTLED);
	spin_unlock_irqrestore(&priv->rx_lock, flags);

	if (actually_throttled)
		schedule_delayed_work(&priv->rx_work, 0);
}

static int __init ftdi_init (void)
{
	int retval;

	dbg("%s", __func__);
	if (vendor > 0 && product > 0) {
		/* Add user specified VID/PID to reserved element of table. */
		int i;
		for (i = 0; id_table_combined[i].idVendor; i++)
			;
		id_table_combined[i].match_flags = USB_DEVICE_ID_MATCH_DEVICE;
		id_table_combined[i].idVendor = vendor;
		id_table_combined[i].idProduct = product;
	}
	retval = usb_serial_register(&ftdi_sio_device);
	if (retval)
		goto failed_sio_register;
	retval = usb_register(&ftdi_driver);
	if (retval)
		goto failed_usb_register;

	info(DRIVER_VERSION ":" DRIVER_DESC);
	return 0;
failed_usb_register:
	usb_serial_deregister(&ftdi_sio_device);
failed_sio_register:
	return retval;
}


static void __exit ftdi_exit (void)
{

	dbg("%s", __func__);

	usb_deregister (&ftdi_driver);
	usb_serial_deregister (&ftdi_sio_device);

}


module_init(ftdi_init);
module_exit(ftdi_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
module_param(vendor, ushort, 0);
MODULE_PARM_DESC(vendor, "User specified vendor ID (default="
		__MODULE_STRING(FTDI_VID)")");
module_param(product, ushort, 0);
MODULE_PARM_DESC(vendor, "User specified product ID");

