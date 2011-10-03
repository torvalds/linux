/*
 * vendor/product IDs (VID/PID) of devices using FTDI USB serial converters.
 * Please keep numerically sorted within individual areas, thanks!
 *
 * Philipp Gühring - pg@futureware.at - added the Device ID of the USB relais
 * from Rudolf Gugler
 *
 */


/**********************************/
/***** devices using FTDI VID *****/
/**********************************/


#define FTDI_VID	0x0403	/* Vendor Id */


/*** "original" FTDI device PIDs ***/

#define FTDI_8U232AM_PID 0x6001 /* Similar device to SIO above */
#define FTDI_8U232AM_ALT_PID 0x6006 /* FTDI's alternate PID for above */
#define FTDI_8U2232C_PID 0x6010 /* Dual channel device */
#define FTDI_4232H_PID 0x6011 /* Quad channel hi-speed device */
#define FTDI_232H_PID  0x6014 /* Single channel hi-speed device */
#define FTDI_SIO_PID	0x8372	/* Product Id SIO application of 8U100AX */
#define FTDI_232RL_PID  0xFBFA  /* Product ID for FT232RL */


/*** third-party PIDs (using FTDI_VID) ***/

/*
 * Marvell OpenRD Base, Client
 * http://www.open-rd.org
 * OpenRD Base, Client use VID 0x0403
 */
#define MARVELL_OPENRD_PID	0x9e90

/* www.candapter.com Ewert Energy Systems CANdapter device */
#define FTDI_CANDAPTER_PID 0x9F80 /* Product Id */

#define FTDI_NXTCAM_PID		0xABB8 /* NXTCam for Mindstorms NXT */

/* US Interface Navigator (http://www.usinterface.com/) */
#define FTDI_USINT_CAT_PID	0xb810	/* Navigator CAT and 2nd PTT lines */
#define FTDI_USINT_WKEY_PID	0xb811	/* Navigator WKEY and FSK lines */
#define FTDI_USINT_RS232_PID	0xb812	/* Navigator RS232 and CONFIG lines */

/* OOCDlink by Joern Kaipf <joernk@web.de>
 * (http://www.joernonline.de/) */
#define FTDI_OOCDLINK_PID	0xbaf8	/* Amontec JTAGkey */

/* Luminary Micro Stellaris Boards, VID = FTDI_VID */
/* FTDI 2332C Dual channel device, side A=245 FIFO (JTAG), Side B=RS232 UART */
#define LMI_LM3S_DEVEL_BOARD_PID	0xbcd8
#define LMI_LM3S_EVAL_BOARD_PID		0xbcd9

#define FTDI_TURTELIZER_PID	0xBDC8 /* JTAG/RS-232 adapter by egnite GmbH */

/* OpenDCC (www.opendcc.de) product id */
#define FTDI_OPENDCC_PID	0xBFD8
#define FTDI_OPENDCC_SNIFFER_PID	0xBFD9
#define FTDI_OPENDCC_THROTTLE_PID	0xBFDA
#define FTDI_OPENDCC_GATEWAY_PID	0xBFDB
#define FTDI_OPENDCC_GBM_PID	0xBFDC

/*
 * RR-CirKits LocoBuffer USB (http://www.rr-cirkits.com)
 */
#define FTDI_RRCIRKITS_LOCOBUFFER_PID	0xc7d0	/* LocoBuffer USB */

/* DMX4ALL DMX Interfaces */
#define FTDI_DMX4ALL 0xC850

/*
 * ASK.fr devices
 */
#define FTDI_ASK_RDR400_PID	0xC991	/* ASK RDR 400 series card reader */

/* www.starting-point-systems.com µChameleon device */
#define FTDI_MICRO_CHAMELEON_PID	0xCAA0	/* Product Id */

/*
 * Tactrix OpenPort (ECU) devices.
 * OpenPort 1.3M submitted by Donour Sizemore.
 * OpenPort 1.3S and 1.3U submitted by Ian Abbott.
 */
#define FTDI_TACTRIX_OPENPORT_13M_PID	0xCC48	/* OpenPort 1.3 Mitsubishi */
#define FTDI_TACTRIX_OPENPORT_13S_PID	0xCC49	/* OpenPort 1.3 Subaru */
#define FTDI_TACTRIX_OPENPORT_13U_PID	0xCC4A	/* OpenPort 1.3 Universal */

/* SCS HF Radio Modems PID's (http://www.scs-ptc.com) */
/* the VID is the standard ftdi vid (FTDI_VID) */
#define FTDI_SCS_DEVICE_0_PID 0xD010    /* SCS PTC-IIusb */
#define FTDI_SCS_DEVICE_1_PID 0xD011    /* SCS Tracker / DSP TNC */
#define FTDI_SCS_DEVICE_2_PID 0xD012
#define FTDI_SCS_DEVICE_3_PID 0xD013
#define FTDI_SCS_DEVICE_4_PID 0xD014
#define FTDI_SCS_DEVICE_5_PID 0xD015
#define FTDI_SCS_DEVICE_6_PID 0xD016
#define FTDI_SCS_DEVICE_7_PID 0xD017

/* iPlus device */
#define FTDI_IPLUS_PID 0xD070 /* Product Id */
#define FTDI_IPLUS2_PID 0xD071 /* Product Id */

/*
 * Gamma Scout (http://gamma-scout.com/). Submitted by rsc@runtux.com.
 */
#define FTDI_GAMMA_SCOUT_PID		0xD678	/* Gamma Scout online */

/* Propox devices */
#define FTDI_PROPOX_JTAGCABLEII_PID	0xD738

/* Lenz LI-USB Computer Interface. */
#define FTDI_LENZ_LIUSB_PID	0xD780

/* Vardaan Enterprises Serial Interface VEUSB422R3 */
#define FTDI_VARDAAN_PID	0xF070

/*
 * Xsens Technologies BV products (http://www.xsens.com).
 */
#define XSENS_CONVERTER_0_PID	0xD388
#define XSENS_CONVERTER_1_PID	0xD389
#define XSENS_CONVERTER_2_PID	0xD38A
#define XSENS_CONVERTER_3_PID	0xD38B
#define XSENS_CONVERTER_4_PID	0xD38C
#define XSENS_CONVERTER_5_PID	0xD38D
#define XSENS_CONVERTER_6_PID	0xD38E
#define XSENS_CONVERTER_7_PID	0xD38F

/*
 * NDI (www.ndigital.com) product ids
 */
#define FTDI_NDI_HUC_PID		0xDA70	/* NDI Host USB Converter */
#define FTDI_NDI_SPECTRA_SCU_PID	0xDA71	/* NDI Spectra SCU */
#define FTDI_NDI_FUTURE_2_PID		0xDA72	/* NDI future device #2 */
#define FTDI_NDI_FUTURE_3_PID		0xDA73	/* NDI future device #3 */
#define FTDI_NDI_AURORA_SCU_PID		0xDA74	/* NDI Aurora SCU */

/*
 * ChamSys Limited (www.chamsys.co.uk) USB wing/interface product IDs
 */
#define FTDI_CHAMSYS_24_MASTER_WING_PID        0xDAF8
#define FTDI_CHAMSYS_PC_WING_PID       0xDAF9
#define FTDI_CHAMSYS_USB_DMX_PID       0xDAFA
#define FTDI_CHAMSYS_MIDI_TIMECODE_PID 0xDAFB
#define FTDI_CHAMSYS_MINI_WING_PID     0xDAFC
#define FTDI_CHAMSYS_MAXI_WING_PID     0xDAFD
#define FTDI_CHAMSYS_MEDIA_WING_PID    0xDAFE
#define FTDI_CHAMSYS_WING_PID  0xDAFF

/*
 * Westrex International devices submitted by Cory Lee
 */
#define FTDI_WESTREX_MODEL_777_PID	0xDC00	/* Model 777 */
#define FTDI_WESTREX_MODEL_8900F_PID	0xDC01	/* Model 8900F */

/*
 * ACG Identification Technologies GmbH products (http://www.acg.de/).
 * Submitted by anton -at- goto10 -dot- org.
 */
#define FTDI_ACG_HFDUAL_PID		0xDD20	/* HF Dual ISO Reader (RFID) */

/*
 * Definitions for Artemis astronomical USB based cameras
 * Check it at http://www.artemisccd.co.uk/
 */
#define FTDI_ARTEMIS_PID	0xDF28	/* All Artemis Cameras */

/*
 * Definitions for ATIK Instruments astronomical USB based cameras
 * Check it at http://www.atik-instruments.com/
 */
#define FTDI_ATIK_ATK16_PID	0xDF30	/* ATIK ATK-16 Grayscale Camera */
#define FTDI_ATIK_ATK16C_PID	0xDF32	/* ATIK ATK-16C Colour Camera */
#define FTDI_ATIK_ATK16HR_PID	0xDF31	/* ATIK ATK-16HR Grayscale Camera */
#define FTDI_ATIK_ATK16HRC_PID	0xDF33	/* ATIK ATK-16HRC Colour Camera */
#define FTDI_ATIK_ATK16IC_PID   0xDF35  /* ATIK ATK-16IC Grayscale Camera */

/*
 * Yost Engineering, Inc. products (www.yostengineering.com).
 * PID 0xE050 submitted by Aaron Prose.
 */
#define FTDI_YEI_SERVOCENTER31_PID	0xE050	/* YEI ServoCenter3.1 USB */

/*
 * ELV USB devices submitted by Christian Abt of ELV (www.elv.de).
 * All of these devices use FTDI's vendor ID (0x0403).
 * Further IDs taken from ELV Windows .inf file.
 *
 * The previously included PID for the UO 100 module was incorrect.
 * In fact, that PID was for ELV's UR 100 USB-RS232 converter (0xFB58).
 *
 * Armin Laeuger originally sent the PID for the UM 100 module.
 */
#define FTDI_ELV_USR_PID	0xE000	/* ELV Universal-Sound-Recorder */
#define FTDI_ELV_MSM1_PID	0xE001	/* ELV Mini-Sound-Modul */
#define FTDI_ELV_KL100_PID	0xE002	/* ELV Kfz-Leistungsmesser KL 100 */
#define FTDI_ELV_WS550_PID	0xE004	/* WS 550 */
#define FTDI_ELV_EC3000_PID	0xE006	/* ENERGY CONTROL 3000 USB */
#define FTDI_ELV_WS888_PID	0xE008	/* WS 888 */
#define FTDI_ELV_TWS550_PID	0xE009	/* Technoline WS 550 */
#define FTDI_ELV_FEM_PID	0xE00A	/* Funk Energie Monitor */
#define FTDI_ELV_FHZ1300PC_PID	0xE0E8	/* FHZ 1300 PC */
#define FTDI_ELV_WS500_PID	0xE0E9	/* PC-Wetterstation (WS 500) */
#define FTDI_ELV_HS485_PID	0xE0EA	/* USB to RS-485 adapter */
#define FTDI_ELV_UMS100_PID	0xE0EB	/* ELV USB Master-Slave Schaltsteckdose UMS 100 */
#define FTDI_ELV_TFD128_PID	0xE0EC	/* ELV Temperatur-Feuchte-Datenlogger TFD 128 */
#define FTDI_ELV_FM3RX_PID	0xE0ED	/* ELV Messwertuebertragung FM3 RX */
#define FTDI_ELV_WS777_PID	0xE0EE	/* Conrad WS 777 */
#define FTDI_ELV_EM1010PC_PID	0xE0EF	/* Energy monitor EM 1010 PC */
#define FTDI_ELV_CSI8_PID	0xE0F0	/* Computer-Schalt-Interface (CSI 8) */
#define FTDI_ELV_EM1000DL_PID	0xE0F1	/* PC-Datenlogger fuer Energiemonitor (EM 1000 DL) */
#define FTDI_ELV_PCK100_PID	0xE0F2	/* PC-Kabeltester (PCK 100) */
#define FTDI_ELV_RFP500_PID	0xE0F3	/* HF-Leistungsmesser (RFP 500) */
#define FTDI_ELV_FS20SIG_PID	0xE0F4	/* Signalgeber (FS 20 SIG) */
#define FTDI_ELV_UTP8_PID	0xE0F5	/* ELV UTP 8 */
#define FTDI_ELV_WS300PC_PID	0xE0F6	/* PC-Wetterstation (WS 300 PC) */
#define FTDI_ELV_WS444PC_PID	0xE0F7	/* Conrad WS 444 PC */
#define FTDI_PHI_FISCO_PID      0xE40B  /* PHI Fisco USB to Serial cable */
#define FTDI_ELV_UAD8_PID	0xF068	/* USB-AD-Wandler (UAD 8) */
#define FTDI_ELV_UDA7_PID	0xF069	/* USB-DA-Wandler (UDA 7) */
#define FTDI_ELV_USI2_PID	0xF06A	/* USB-Schrittmotoren-Interface (USI 2) */
#define FTDI_ELV_T1100_PID	0xF06B	/* Thermometer (T 1100) */
#define FTDI_ELV_PCD200_PID	0xF06C	/* PC-Datenlogger (PCD 200) */
#define FTDI_ELV_ULA200_PID	0xF06D	/* USB-LCD-Ansteuerung (ULA 200) */
#define FTDI_ELV_ALC8500_PID	0xF06E	/* ALC 8500 Expert */
#define FTDI_ELV_FHZ1000PC_PID	0xF06F	/* FHZ 1000 PC */
#define FTDI_ELV_UR100_PID	0xFB58	/* USB-RS232-Umsetzer (UR 100) */
#define FTDI_ELV_UM100_PID	0xFB5A	/* USB-Modul UM 100 */
#define FTDI_ELV_UO100_PID	0xFB5B	/* USB-Modul UO 100 */
/* Additional ELV PIDs that default to using the FTDI D2XX drivers on
 * MS Windows, rather than the FTDI Virtual Com Port drivers.
 * Maybe these will be easier to use with the libftdi/libusb user-space
 * drivers, or possibly the Comedi drivers in some cases. */
#define FTDI_ELV_CLI7000_PID	0xFB59	/* Computer-Light-Interface (CLI 7000) */
#define FTDI_ELV_PPS7330_PID	0xFB5C	/* Processor-Power-Supply (PPS 7330) */
#define FTDI_ELV_TFM100_PID	0xFB5D	/* Temperatur-Feuchte-Messgeraet (TFM 100) */
#define FTDI_ELV_UDF77_PID	0xFB5E	/* USB DCF Funkuhr (UDF 77) */
#define FTDI_ELV_UIO88_PID	0xFB5F	/* USB-I/O Interface (UIO 88) */

/*
 * EVER Eco Pro UPS (http://www.ever.com.pl/)
 */

#define	EVER_ECO_PRO_CDS	0xe520	/* RS-232 converter */

/*
 * Active Robots product ids.
 */
#define FTDI_ACTIVE_ROBOTS_PID	0xE548	/* USB comms board */

/* Pyramid Computer GmbH */
#define FTDI_PYRAMID_PID	0xE6C8	/* Pyramid Appliance Display */

/* www.elsterelectricity.com Elster Unicom III Optical Probe */
#define FTDI_ELSTER_UNICOM_PID		0xE700 /* Product Id */

/*
 * Gude Analog- und Digitalsysteme GmbH
 */
#define FTDI_GUDEADS_E808_PID    0xE808
#define FTDI_GUDEADS_E809_PID    0xE809
#define FTDI_GUDEADS_E80A_PID    0xE80A
#define FTDI_GUDEADS_E80B_PID    0xE80B
#define FTDI_GUDEADS_E80C_PID    0xE80C
#define FTDI_GUDEADS_E80D_PID    0xE80D
#define FTDI_GUDEADS_E80E_PID    0xE80E
#define FTDI_GUDEADS_E80F_PID    0xE80F
#define FTDI_GUDEADS_E888_PID    0xE888  /* Expert ISDN Control USB */
#define FTDI_GUDEADS_E889_PID    0xE889  /* USB RS-232 OptoBridge */
#define FTDI_GUDEADS_E88A_PID    0xE88A
#define FTDI_GUDEADS_E88B_PID    0xE88B
#define FTDI_GUDEADS_E88C_PID    0xE88C
#define FTDI_GUDEADS_E88D_PID    0xE88D
#define FTDI_GUDEADS_E88E_PID    0xE88E
#define FTDI_GUDEADS_E88F_PID    0xE88F

/*
 * Eclo (http://www.eclo.pt/) product IDs.
 * PID 0xEA90 submitted by Martin Grill.
 */
#define FTDI_ECLO_COM_1WIRE_PID	0xEA90	/* COM to 1-Wire USB adaptor */

/* TNC-X USB-to-packet-radio adapter, versions prior to 3.0 (DLP module) */
#define FTDI_TNC_X_PID		0xEBE0

/*
 * Teratronik product ids.
 * Submitted by O. Wölfelschneider.
 */
#define FTDI_TERATRONIK_VCP_PID	 0xEC88	/* Teratronik device (preferring VCP driver on windows) */
#define FTDI_TERATRONIK_D2XX_PID 0xEC89	/* Teratronik device (preferring D2XX driver on windows) */

/* Rig Expert Ukraine devices */
#define FTDI_REU_TINY_PID		0xED22	/* RigExpert Tiny */

/*
 * Hameg HO820 and HO870 interface (using VID 0x0403)
 */
#define HAMEG_HO820_PID			0xed74
#define HAMEG_HO730_PID			0xed73
#define HAMEG_HO720_PID			0xed72
#define HAMEG_HO870_PID			0xed71

/*
 *  MaxStream devices	www.maxstream.net
 */
#define FTDI_MAXSTREAM_PID	0xEE18	/* Xbee PKG-U Module */

/*
 * microHAM product IDs (http://www.microham.com).
 * Submitted by Justin Burket (KL1RL) <zorton@jtan.com>
 * and Mike Studer (K6EEP) <k6eep@hamsoftware.org>.
 * Ian Abbott <abbotti@mev.co.uk> added a few more from the driver INF file.
 */
#define FTDI_MHAM_KW_PID	0xEEE8	/* USB-KW interface */
#define FTDI_MHAM_YS_PID	0xEEE9	/* USB-YS interface */
#define FTDI_MHAM_Y6_PID	0xEEEA	/* USB-Y6 interface */
#define FTDI_MHAM_Y8_PID	0xEEEB	/* USB-Y8 interface */
#define FTDI_MHAM_IC_PID	0xEEEC	/* USB-IC interface */
#define FTDI_MHAM_DB9_PID	0xEEED	/* USB-DB9 interface */
#define FTDI_MHAM_RS232_PID	0xEEEE	/* USB-RS232 interface */
#define FTDI_MHAM_Y9_PID	0xEEEF	/* USB-Y9 interface */

/* Domintell products  http://www.domintell.com */
#define FTDI_DOMINTELL_DGQG_PID	0xEF50	/* Master */
#define FTDI_DOMINTELL_DUSB_PID	0xEF51	/* DUSB01 module */

/*
 * The following are the values for the Perle Systems
 * UltraPort USB serial converters
 */
#define FTDI_PERLE_ULTRAPORT_PID 0xF0C0	/* Perle UltraPort Product Id */

/* Sprog II (Andrew Crosland's SprogII DCC interface) */
#define FTDI_SPROG_II		0xF0C8

/* an infrared receiver for user access control with IR tags */
#define FTDI_PIEGROUP_PID	0xF208	/* Product Id */

/* ACT Solutions HomePro ZWave interface
   (http://www.act-solutions.com/HomePro-Product-Matrix.html) */
#define FTDI_ACTZWAVE_PID	0xF2D0

/*
 * 4N-GALAXY.DE PIDs for CAN-USB, USB-RS232, USB-RS422, USB-RS485,
 * USB-TTY aktiv, USB-TTY passiv.  Some PIDs are used by several devices
 * and I'm not entirely sure which are used by which.
 */
#define FTDI_4N_GALAXY_DE_1_PID	0xF3C0
#define FTDI_4N_GALAXY_DE_2_PID	0xF3C1
#define FTDI_4N_GALAXY_DE_3_PID	0xF3C2

/*
 * Linx Technologies product ids
 */
#define LINX_SDMUSBQSS_PID	0xF448	/* Linx SDM-USB-QS-S */
#define LINX_MASTERDEVEL2_PID   0xF449	/* Linx Master Development 2.0 */
#define LINX_FUTURE_0_PID   0xF44A	/* Linx future device */
#define LINX_FUTURE_1_PID   0xF44B	/* Linx future device */
#define LINX_FUTURE_2_PID   0xF44C	/* Linx future device */

/*
 * Oceanic product ids
 */
#define FTDI_OCEANIC_PID	0xF460  /* Oceanic dive instrument */

/*
 * SUUNTO product ids
 */
#define FTDI_SUUNTO_SPORTS_PID	0xF680	/* Suunto Sports instrument */

/* USB-UIRT - An infrared receiver and transmitter using the 8U232AM chip */
/* http://www.usbuirt.com/ */
#define FTDI_USB_UIRT_PID	0xF850	/* Product Id */

/* CCS Inc. ICDU/ICDU40 product ID -
 * the FT232BM is used in an in-circuit-debugger unit for PIC16's/PIC18's */
#define FTDI_CCSICDU20_0_PID    0xF9D0
#define FTDI_CCSICDU40_1_PID    0xF9D1
#define FTDI_CCSMACHX_2_PID     0xF9D2
#define FTDI_CCSLOAD_N_GO_3_PID 0xF9D3
#define FTDI_CCSICDU64_4_PID    0xF9D4
#define FTDI_CCSPRIME8_5_PID    0xF9D5

/*
 * The following are the values for the Matrix Orbital LCD displays,
 * which are the FT232BM ( similar to the 8U232AM )
 */
#define FTDI_MTXORB_0_PID      0xFA00  /* Matrix Orbital Product Id */
#define FTDI_MTXORB_1_PID      0xFA01  /* Matrix Orbital Product Id */
#define FTDI_MTXORB_2_PID      0xFA02  /* Matrix Orbital Product Id */
#define FTDI_MTXORB_3_PID      0xFA03  /* Matrix Orbital Product Id */
#define FTDI_MTXORB_4_PID      0xFA04  /* Matrix Orbital Product Id */
#define FTDI_MTXORB_5_PID      0xFA05  /* Matrix Orbital Product Id */
#define FTDI_MTXORB_6_PID      0xFA06  /* Matrix Orbital Product Id */

/*
 * Home Electronics (www.home-electro.com) USB gadgets
 */
#define FTDI_HE_TIRA1_PID	0xFA78	/* Tira-1 IR transceiver */

/* Inside Accesso contactless reader (http://www.insidecontactless.com/) */
#define INSIDE_ACCESSO		0xFAD0

/*
 * ThorLabs USB motor drivers
 */
#define FTDI_THORLABS_PID		0xfaf0 /* ThorLabs USB motor drivers */

/*
 * Protego product ids
 */
#define PROTEGO_SPECIAL_1	0xFC70	/* special/unknown device */
#define PROTEGO_R2X0		0xFC71	/* R200-USB TRNG unit (R210, R220, and R230) */
#define PROTEGO_SPECIAL_3	0xFC72	/* special/unknown device */
#define PROTEGO_SPECIAL_4	0xFC73	/* special/unknown device */

/*
 * Sony Ericsson product ids
 */
#define FTDI_DSS20_PID		0xFC82	/* DSS-20 Sync Station for Sony Ericsson P800 */
#define FTDI_URBAN_0_PID	0xFC8A	/* Sony Ericsson Urban, uart #0 */
#define FTDI_URBAN_1_PID	0xFC8B	/* Sony Ericsson Urban, uart #1 */

/* www.irtrans.de device */
#define FTDI_IRTRANS_PID 0xFC60 /* Product Id */

/*
 * RM Michaelides CANview USB (http://www.rmcan.com) (FTDI_VID)
 * CAN fieldbus interface adapter, added by port GmbH www.port.de)
 * Ian Abbott changed the macro names for consistency.
 */
#define FTDI_RM_CANVIEW_PID	0xfd60	/* Product Id */
/* www.thoughttechnology.com/ TT-USB provide with procomp use ftdi_sio */
#define FTDI_TTUSB_PID 0xFF20 /* Product Id */

#define FTDI_USBX_707_PID 0xF857	/* ADSTech IR Blaster USBX-707 (FTDI_VID) */

#define FTDI_RELAIS_PID	0xFA10  /* Relais device from Rudolf Gugler */

/*
 * PCDJ use ftdi based dj-controllers. The following PID is
 * for their DAC-2 device http://www.pcdjhardware.com/DAC2.asp
 * (the VID is the standard ftdi vid (FTDI_VID), PID sent by Wouter Paesen)
 */
#define FTDI_PCDJ_DAC2_PID 0xFA88

#define FTDI_R2000KU_TRUE_RNG	0xFB80  /* R2000KU TRUE RNG (FTDI_VID) */

/*
 * DIEBOLD BCS SE923 (FTDI_VID)
 */
#define DIEBOLD_BCS_SE923_PID	0xfb99

/* www.crystalfontz.com devices
 * - thanx for providing free devices for evaluation !
 * they use the ftdi chipset for the USB interface
 * and the vendor id is the same
 */
#define FTDI_XF_632_PID 0xFC08	/* 632: 16x2 Character Display */
#define FTDI_XF_634_PID 0xFC09	/* 634: 20x4 Character Display */
#define FTDI_XF_547_PID 0xFC0A	/* 547: Two line Display */
#define FTDI_XF_633_PID 0xFC0B	/* 633: 16x2 Character Display with Keys */
#define FTDI_XF_631_PID 0xFC0C	/* 631: 20x2 Character Display */
#define FTDI_XF_635_PID 0xFC0D	/* 635: 20x4 Character Display */
#define FTDI_XF_640_PID 0xFC0E	/* 640: Two line Display */
#define FTDI_XF_642_PID 0xFC0F	/* 642: Two line Display */

/*
 * Video Networks Limited / Homechoice in the UK use an ftdi-based device
 * for their 1Mb broadband internet service.  The following PID is exhibited
 * by the usb device supplied (the VID is the standard ftdi vid (FTDI_VID)
 */
#define FTDI_VNHCPCUSB_D_PID 0xfe38 /* Product Id */

/* AlphaMicro Components AMC-232USB01 device (FTDI_VID) */
#define FTDI_AMC232_PID 0xFF00 /* Product Id */

/*
 * IBS elektronik product ids (FTDI_VID)
 * Submitted by Thomas Schleusener
 */
#define FTDI_IBS_US485_PID	0xff38  /* IBS US485 (USB<-->RS422/485 interface) */
#define FTDI_IBS_PICPRO_PID	0xff39  /* IBS PIC-Programmer */
#define FTDI_IBS_PCMCIA_PID	0xff3a  /* IBS Card reader for PCMCIA SRAM-cards */
#define FTDI_IBS_PK1_PID	0xff3b  /* IBS PK1 - Particel counter */
#define FTDI_IBS_RS232MON_PID	0xff3c  /* IBS RS232 - Monitor */
#define FTDI_IBS_APP70_PID	0xff3d  /* APP 70 (dust monitoring system) */
#define FTDI_IBS_PEDO_PID	0xff3e  /* IBS PEDO-Modem (RF modem 868.35 MHz) */
#define FTDI_IBS_PROD_PID	0xff3f  /* future device */
/* www.canusb.com Lawicel CANUSB device (FTDI_VID) */
#define FTDI_CANUSB_PID 0xFFA8 /* Product Id */

/*
 * TavIR AVR product ids (FTDI_VID)
 */
#define FTDI_TAVIR_STK500_PID	0xFA33	/* STK500 AVR programmer */



/********************************/
/** third-party VID/PID combos **/
/********************************/



/*
 * Atmel STK541
 */
#define ATMEL_VID		0x03eb /* Vendor ID */
#define STK541_PID		0x2109 /* Zigbee Controller */

/*
 * Blackfin gnICE JTAG
 * http://docs.blackfin.uclinux.org/doku.php?id=hw:jtag:gnice
 */
#define ADI_VID			0x0456
#define ADI_GNICE_PID		0xF000
#define ADI_GNICEPLUS_PID	0xF001

/*
 * RATOC REX-USB60F
 */
#define RATOC_VENDOR_ID		0x0584
#define RATOC_PRODUCT_ID_USB60F	0xb020

/*
 * Acton Research Corp.
 */
#define ACTON_VID		0x0647	/* Vendor ID */
#define ACTON_SPECTRAPRO_PID	0x0100

/*
 * Contec products (http://www.contec.com)
 * Submitted by Daniel Sangorrin
 */
#define CONTEC_VID		0x06CE	/* Vendor ID */
#define CONTEC_COM1USBH_PID	0x8311	/* COM-1(USB)H */

/*
 * Definitions for B&B Electronics products.
 */
#define BANDB_VID		0x0856	/* B&B Electronics Vendor ID */
#define BANDB_USOTL4_PID	0xAC01	/* USOTL4 Isolated RS-485 Converter */
#define BANDB_USTL4_PID		0xAC02	/* USTL4 RS-485 Converter */
#define BANDB_USO9ML2_PID	0xAC03	/* USO9ML2 Isolated RS-232 Converter */
#define BANDB_USOPTL4_PID	0xAC11
#define BANDB_USPTL4_PID	0xAC12
#define BANDB_USO9ML2DR_2_PID	0xAC16
#define BANDB_USO9ML2DR_PID	0xAC17
#define BANDB_USOPTL4DR2_PID	0xAC18	/* USOPTL4R-2 2-port Isolated RS-232 Converter */
#define BANDB_USOPTL4DR_PID	0xAC19
#define BANDB_485USB9F_2W_PID	0xAC25
#define BANDB_485USB9F_4W_PID	0xAC26
#define BANDB_232USB9M_PID	0xAC27
#define BANDB_485USBTB_2W_PID	0xAC33
#define BANDB_485USBTB_4W_PID	0xAC34
#define BANDB_TTL5USB9M_PID	0xAC49
#define BANDB_TTL3USB9M_PID	0xAC50
#define BANDB_ZZ_PROG1_USB_PID	0xBA02

/*
 * Intrepid Control Systems (http://www.intrepidcs.com/) ValueCAN and NeoVI
 */
#define INTREPID_VID		0x093C
#define INTREPID_VALUECAN_PID	0x0601
#define INTREPID_NEOVI_PID	0x0701

/*
 * Definitions for ID TECH (www.idt-net.com) devices
 */
#define IDTECH_VID		0x0ACD	/* ID TECH Vendor ID */
#define IDTECH_IDT1221U_PID	0x0300	/* IDT1221U USB to RS-232 adapter */

/*
 * Definitions for Omnidirectional Control Technology, Inc. devices
 */
#define OCT_VID			0x0B39	/* OCT vendor ID */
/* Note: OCT US101 is also rebadged as Dick Smith Electronics (NZ) XH6381 */
/* Also rebadged as Dick Smith Electronics (Aus) XH6451 */
/* Also rebadged as SIIG Inc. model US2308 hardware version 1 */
#define OCT_DK201_PID		0x0103	/* OCT DK201 USB docking station */
#define OCT_US101_PID		0x0421	/* OCT US101 USB to RS-232 */

/*
 * Definitions for Icom Inc. devices
 */
#define ICOM_VID		0x0C26 /* Icom vendor ID */
/* Note: ID-1 is a communications tranceiver for HAM-radio operators */
#define ICOM_ID_1_PID		0x0004 /* ID-1 USB to RS-232 */
/* Note: OPC is an Optional cable to connect an Icom Tranceiver */
#define ICOM_OPC_U_UC_PID	0x0018 /* OPC-478UC, OPC-1122U cloning cable */
/* Note: ID-RP* devices are Icom Repeater Devices for HAM-radio */
#define ICOM_ID_RP2C1_PID	0x0009 /* ID-RP2C Asset 1 to RS-232 */
#define ICOM_ID_RP2C2_PID	0x000A /* ID-RP2C Asset 2 to RS-232 */
#define ICOM_ID_RP2D_PID	0x000B /* ID-RP2D configuration port*/
#define ICOM_ID_RP2VT_PID	0x000C /* ID-RP2V Transmit config port */
#define ICOM_ID_RP2VR_PID	0x000D /* ID-RP2V Receive config port */
#define ICOM_ID_RP4KVT_PID	0x0010 /* ID-RP4000V Transmit config port */
#define ICOM_ID_RP4KVR_PID	0x0011 /* ID-RP4000V Receive config port */
#define ICOM_ID_RP2KVT_PID	0x0012 /* ID-RP2000V Transmit config port */
#define ICOM_ID_RP2KVR_PID	0x0013 /* ID-RP2000V Receive config port */

/*
 * GN Otometrics (http://www.otometrics.com)
 * Submitted by Ville Sundberg.
 */
#define GN_OTOMETRICS_VID	0x0c33	/* Vendor ID */
#define AURICAL_USB_PID		0x0010	/* Aurical USB Audiometer */

/*
 * The following are the values for the Sealevel SeaLINK+ adapters.
 * (Original list sent by Tuan Hoang.  Ian Abbott renamed the macros and
 * removed some PIDs that don't seem to match any existing products.)
 */
#define SEALEVEL_VID		0x0c52	/* Sealevel Vendor ID */
#define SEALEVEL_2101_PID	0x2101	/* SeaLINK+232 (2101/2105) */
#define SEALEVEL_2102_PID	0x2102	/* SeaLINK+485 (2102) */
#define SEALEVEL_2103_PID	0x2103	/* SeaLINK+232I (2103) */
#define SEALEVEL_2104_PID	0x2104	/* SeaLINK+485I (2104) */
#define SEALEVEL_2106_PID	0x9020	/* SeaLINK+422 (2106) */
#define SEALEVEL_2201_1_PID	0x2211	/* SeaPORT+2/232 (2201) Port 1 */
#define SEALEVEL_2201_2_PID	0x2221	/* SeaPORT+2/232 (2201) Port 2 */
#define SEALEVEL_2202_1_PID	0x2212	/* SeaPORT+2/485 (2202) Port 1 */
#define SEALEVEL_2202_2_PID	0x2222	/* SeaPORT+2/485 (2202) Port 2 */
#define SEALEVEL_2203_1_PID	0x2213	/* SeaPORT+2 (2203) Port 1 */
#define SEALEVEL_2203_2_PID	0x2223	/* SeaPORT+2 (2203) Port 2 */
#define SEALEVEL_2401_1_PID	0x2411	/* SeaPORT+4/232 (2401) Port 1 */
#define SEALEVEL_2401_2_PID	0x2421	/* SeaPORT+4/232 (2401) Port 2 */
#define SEALEVEL_2401_3_PID	0x2431	/* SeaPORT+4/232 (2401) Port 3 */
#define SEALEVEL_2401_4_PID	0x2441	/* SeaPORT+4/232 (2401) Port 4 */
#define SEALEVEL_2402_1_PID	0x2412	/* SeaPORT+4/485 (2402) Port 1 */
#define SEALEVEL_2402_2_PID	0x2422	/* SeaPORT+4/485 (2402) Port 2 */
#define SEALEVEL_2402_3_PID	0x2432	/* SeaPORT+4/485 (2402) Port 3 */
#define SEALEVEL_2402_4_PID	0x2442	/* SeaPORT+4/485 (2402) Port 4 */
#define SEALEVEL_2403_1_PID	0x2413	/* SeaPORT+4 (2403) Port 1 */
#define SEALEVEL_2403_2_PID	0x2423	/* SeaPORT+4 (2403) Port 2 */
#define SEALEVEL_2403_3_PID	0x2433	/* SeaPORT+4 (2403) Port 3 */
#define SEALEVEL_2403_4_PID	0x2443	/* SeaPORT+4 (2403) Port 4 */
#define SEALEVEL_2801_1_PID	0X2811	/* SeaLINK+8/232 (2801) Port 1 */
#define SEALEVEL_2801_2_PID	0X2821	/* SeaLINK+8/232 (2801) Port 2 */
#define SEALEVEL_2801_3_PID	0X2831	/* SeaLINK+8/232 (2801) Port 3 */
#define SEALEVEL_2801_4_PID	0X2841	/* SeaLINK+8/232 (2801) Port 4 */
#define SEALEVEL_2801_5_PID	0X2851	/* SeaLINK+8/232 (2801) Port 5 */
#define SEALEVEL_2801_6_PID	0X2861	/* SeaLINK+8/232 (2801) Port 6 */
#define SEALEVEL_2801_7_PID	0X2871	/* SeaLINK+8/232 (2801) Port 7 */
#define SEALEVEL_2801_8_PID	0X2881	/* SeaLINK+8/232 (2801) Port 8 */
#define SEALEVEL_2802_1_PID	0X2812	/* SeaLINK+8/485 (2802) Port 1 */
#define SEALEVEL_2802_2_PID	0X2822	/* SeaLINK+8/485 (2802) Port 2 */
#define SEALEVEL_2802_3_PID	0X2832	/* SeaLINK+8/485 (2802) Port 3 */
#define SEALEVEL_2802_4_PID	0X2842	/* SeaLINK+8/485 (2802) Port 4 */
#define SEALEVEL_2802_5_PID	0X2852	/* SeaLINK+8/485 (2802) Port 5 */
#define SEALEVEL_2802_6_PID	0X2862	/* SeaLINK+8/485 (2802) Port 6 */
#define SEALEVEL_2802_7_PID	0X2872	/* SeaLINK+8/485 (2802) Port 7 */
#define SEALEVEL_2802_8_PID	0X2882	/* SeaLINK+8/485 (2802) Port 8 */
#define SEALEVEL_2803_1_PID	0X2813	/* SeaLINK+8 (2803) Port 1 */
#define SEALEVEL_2803_2_PID	0X2823	/* SeaLINK+8 (2803) Port 2 */
#define SEALEVEL_2803_3_PID	0X2833	/* SeaLINK+8 (2803) Port 3 */
#define SEALEVEL_2803_4_PID	0X2843	/* SeaLINK+8 (2803) Port 4 */
#define SEALEVEL_2803_5_PID	0X2853	/* SeaLINK+8 (2803) Port 5 */
#define SEALEVEL_2803_6_PID	0X2863	/* SeaLINK+8 (2803) Port 6 */
#define SEALEVEL_2803_7_PID	0X2873	/* SeaLINK+8 (2803) Port 7 */
#define SEALEVEL_2803_8_PID	0X2883	/* SeaLINK+8 (2803) Port 8 */

/*
 * JETI SPECTROMETER SPECBOS 1201
 * http://www.jeti.com/cms/index.php/instruments/other-instruments/specbos-2101
 */
#define JETI_VID		0x0c6c
#define JETI_SPC1201_PID	0x04b2

/*
 * FTDI USB UART chips used in construction projects from the
 * Elektor Electronics magazine (http://www.elektor.com/)
 */
#define ELEKTOR_VID		0x0C7D
#define ELEKTOR_FT323R_PID	0x0005	/* RFID-Reader, issue 09-2006 */

/*
 * Posiflex inc retail equipment (http://www.posiflex.com.tw)
 */
#define POSIFLEX_VID		0x0d3a  /* Vendor ID */
#define POSIFLEX_PP7000_PID	0x0300  /* PP-7000II thermal printer */

/*
 * The following are the values for two KOBIL chipcard terminals.
 */
#define KOBIL_VID		0x0d46	/* KOBIL Vendor ID */
#define KOBIL_CONV_B1_PID	0x2020	/* KOBIL Konverter for B1 */
#define KOBIL_CONV_KAAN_PID	0x2021	/* KOBIL_Konverter for KAAN */

#define FTDI_NF_RIC_VID	0x0DCD	/* Vendor Id */
#define FTDI_NF_RIC_PID	0x0001	/* Product Id */

/*
 * Falcom Wireless Communications GmbH
 */
#define FALCOM_VID		0x0F94	/* Vendor Id */
#define FALCOM_TWIST_PID	0x0001	/* Falcom Twist USB GPRS modem */
#define FALCOM_SAMBA_PID	0x0005	/* Falcom Samba USB GPRS modem */

/* Larsen and Brusgaard AltiTrack/USBtrack */
#define LARSENBRUSGAARD_VID		0x0FD8
#define LB_ALTITRACK_PID		0x0001

/*
 * TTi (Thurlby Thandar Instruments)
 */
#define TTI_VID			0x103E	/* Vendor Id */
#define TTI_QL355P_PID		0x03E8	/* TTi QL355P power supply */

/* Interbiometrics USB I/O Board */
/* Developed for Interbiometrics by Rudolf Gugler */
#define INTERBIOMETRICS_VID              0x1209
#define INTERBIOMETRICS_IOBOARD_PID      0x1002
#define INTERBIOMETRICS_MINI_IOBOARD_PID 0x1006

/*
 * Testo products (http://www.testo.com/)
 * Submitted by Colin Leroy
 */
#define TESTO_VID			0x128D
#define TESTO_USB_INTERFACE_PID		0x0001

/*
 * Mobility Electronics products.
 */
#define MOBILITY_VID			0x1342
#define MOBILITY_USB_SERIAL_PID		0x0202	/* EasiDock USB 200 serial */

/*
 * FIC / OpenMoko, Inc. http://wiki.openmoko.org/wiki/Neo1973_Debug_Board_v3
 * Submitted by Harald Welte <laforge@openmoko.org>
 */
#define	FIC_VID			0x1457
#define	FIC_NEO1973_DEBUG_PID	0x5118

/* Olimex */
#define OLIMEX_VID			0x15BA
#define OLIMEX_ARM_USB_OCD_PID		0x0003
#define OLIMEX_ARM_USB_OCD_H_PID	0x002b

/*
 * Telldus Technologies
 */
#define TELLDUS_VID			0x1781	/* Vendor ID */
#define TELLDUS_TELLSTICK_PID		0x0C30	/* RF control dongle 433 MHz using FT232RL */

/*
 * RT Systems programming cables for various ham radios
 */
#define RTSYSTEMS_VID			0x2100	/* Vendor ID */
#define RTSYSTEMS_SERIAL_VX7_PID	0x9e52	/* Serial converter for VX-7 Radios using FT232RL */
#define RTSYSTEMS_CT29B_PID		0x9e54	/* CT29B Radio Cable */

/*
 * Bayer Ascensia Contour blood glucose meter USB-converter cable.
 * http://winglucofacts.com/cables/
 */
#define BAYER_VID                      0x1A79
#define BAYER_CONTOUR_CABLE_PID        0x6001

/*
 * The following are the values for the Matrix Orbital FTDI Range
 * Anything in this range will use an FT232RL.
 */
#define MTXORB_VID			0x1B3D
#define MTXORB_FTDI_RANGE_0100_PID	0x0100
#define MTXORB_FTDI_RANGE_0101_PID	0x0101
#define MTXORB_FTDI_RANGE_0102_PID	0x0102
#define MTXORB_FTDI_RANGE_0103_PID	0x0103
#define MTXORB_FTDI_RANGE_0104_PID	0x0104
#define MTXORB_FTDI_RANGE_0105_PID	0x0105
#define MTXORB_FTDI_RANGE_0106_PID	0x0106
#define MTXORB_FTDI_RANGE_0107_PID	0x0107
#define MTXORB_FTDI_RANGE_0108_PID	0x0108
#define MTXORB_FTDI_RANGE_0109_PID	0x0109
#define MTXORB_FTDI_RANGE_010A_PID	0x010A
#define MTXORB_FTDI_RANGE_010B_PID	0x010B
#define MTXORB_FTDI_RANGE_010C_PID	0x010C
#define MTXORB_FTDI_RANGE_010D_PID	0x010D
#define MTXORB_FTDI_RANGE_010E_PID	0x010E
#define MTXORB_FTDI_RANGE_010F_PID	0x010F
#define MTXORB_FTDI_RANGE_0110_PID	0x0110
#define MTXORB_FTDI_RANGE_0111_PID	0x0111
#define MTXORB_FTDI_RANGE_0112_PID	0x0112
#define MTXORB_FTDI_RANGE_0113_PID	0x0113
#define MTXORB_FTDI_RANGE_0114_PID	0x0114
#define MTXORB_FTDI_RANGE_0115_PID	0x0115
#define MTXORB_FTDI_RANGE_0116_PID	0x0116
#define MTXORB_FTDI_RANGE_0117_PID	0x0117
#define MTXORB_FTDI_RANGE_0118_PID	0x0118
#define MTXORB_FTDI_RANGE_0119_PID	0x0119
#define MTXORB_FTDI_RANGE_011A_PID	0x011A
#define MTXORB_FTDI_RANGE_011B_PID	0x011B
#define MTXORB_FTDI_RANGE_011C_PID	0x011C
#define MTXORB_FTDI_RANGE_011D_PID	0x011D
#define MTXORB_FTDI_RANGE_011E_PID	0x011E
#define MTXORB_FTDI_RANGE_011F_PID	0x011F
#define MTXORB_FTDI_RANGE_0120_PID	0x0120
#define MTXORB_FTDI_RANGE_0121_PID	0x0121
#define MTXORB_FTDI_RANGE_0122_PID	0x0122
#define MTXORB_FTDI_RANGE_0123_PID	0x0123
#define MTXORB_FTDI_RANGE_0124_PID	0x0124
#define MTXORB_FTDI_RANGE_0125_PID	0x0125
#define MTXORB_FTDI_RANGE_0126_PID	0x0126
#define MTXORB_FTDI_RANGE_0127_PID	0x0127
#define MTXORB_FTDI_RANGE_0128_PID	0x0128
#define MTXORB_FTDI_RANGE_0129_PID	0x0129
#define MTXORB_FTDI_RANGE_012A_PID	0x012A
#define MTXORB_FTDI_RANGE_012B_PID	0x012B
#define MTXORB_FTDI_RANGE_012C_PID	0x012C
#define MTXORB_FTDI_RANGE_012D_PID	0x012D
#define MTXORB_FTDI_RANGE_012E_PID	0x012E
#define MTXORB_FTDI_RANGE_012F_PID	0x012F
#define MTXORB_FTDI_RANGE_0130_PID	0x0130
#define MTXORB_FTDI_RANGE_0131_PID	0x0131
#define MTXORB_FTDI_RANGE_0132_PID	0x0132
#define MTXORB_FTDI_RANGE_0133_PID	0x0133
#define MTXORB_FTDI_RANGE_0134_PID	0x0134
#define MTXORB_FTDI_RANGE_0135_PID	0x0135
#define MTXORB_FTDI_RANGE_0136_PID	0x0136
#define MTXORB_FTDI_RANGE_0137_PID	0x0137
#define MTXORB_FTDI_RANGE_0138_PID	0x0138
#define MTXORB_FTDI_RANGE_0139_PID	0x0139
#define MTXORB_FTDI_RANGE_013A_PID	0x013A
#define MTXORB_FTDI_RANGE_013B_PID	0x013B
#define MTXORB_FTDI_RANGE_013C_PID	0x013C
#define MTXORB_FTDI_RANGE_013D_PID	0x013D
#define MTXORB_FTDI_RANGE_013E_PID	0x013E
#define MTXORB_FTDI_RANGE_013F_PID	0x013F
#define MTXORB_FTDI_RANGE_0140_PID	0x0140
#define MTXORB_FTDI_RANGE_0141_PID	0x0141
#define MTXORB_FTDI_RANGE_0142_PID	0x0142
#define MTXORB_FTDI_RANGE_0143_PID	0x0143
#define MTXORB_FTDI_RANGE_0144_PID	0x0144
#define MTXORB_FTDI_RANGE_0145_PID	0x0145
#define MTXORB_FTDI_RANGE_0146_PID	0x0146
#define MTXORB_FTDI_RANGE_0147_PID	0x0147
#define MTXORB_FTDI_RANGE_0148_PID	0x0148
#define MTXORB_FTDI_RANGE_0149_PID	0x0149
#define MTXORB_FTDI_RANGE_014A_PID	0x014A
#define MTXORB_FTDI_RANGE_014B_PID	0x014B
#define MTXORB_FTDI_RANGE_014C_PID	0x014C
#define MTXORB_FTDI_RANGE_014D_PID	0x014D
#define MTXORB_FTDI_RANGE_014E_PID	0x014E
#define MTXORB_FTDI_RANGE_014F_PID	0x014F
#define MTXORB_FTDI_RANGE_0150_PID	0x0150
#define MTXORB_FTDI_RANGE_0151_PID	0x0151
#define MTXORB_FTDI_RANGE_0152_PID	0x0152
#define MTXORB_FTDI_RANGE_0153_PID	0x0153
#define MTXORB_FTDI_RANGE_0154_PID	0x0154
#define MTXORB_FTDI_RANGE_0155_PID	0x0155
#define MTXORB_FTDI_RANGE_0156_PID	0x0156
#define MTXORB_FTDI_RANGE_0157_PID	0x0157
#define MTXORB_FTDI_RANGE_0158_PID	0x0158
#define MTXORB_FTDI_RANGE_0159_PID	0x0159
#define MTXORB_FTDI_RANGE_015A_PID	0x015A
#define MTXORB_FTDI_RANGE_015B_PID	0x015B
#define MTXORB_FTDI_RANGE_015C_PID	0x015C
#define MTXORB_FTDI_RANGE_015D_PID	0x015D
#define MTXORB_FTDI_RANGE_015E_PID	0x015E
#define MTXORB_FTDI_RANGE_015F_PID	0x015F
#define MTXORB_FTDI_RANGE_0160_PID	0x0160
#define MTXORB_FTDI_RANGE_0161_PID	0x0161
#define MTXORB_FTDI_RANGE_0162_PID	0x0162
#define MTXORB_FTDI_RANGE_0163_PID	0x0163
#define MTXORB_FTDI_RANGE_0164_PID	0x0164
#define MTXORB_FTDI_RANGE_0165_PID	0x0165
#define MTXORB_FTDI_RANGE_0166_PID	0x0166
#define MTXORB_FTDI_RANGE_0167_PID	0x0167
#define MTXORB_FTDI_RANGE_0168_PID	0x0168
#define MTXORB_FTDI_RANGE_0169_PID	0x0169
#define MTXORB_FTDI_RANGE_016A_PID	0x016A
#define MTXORB_FTDI_RANGE_016B_PID	0x016B
#define MTXORB_FTDI_RANGE_016C_PID	0x016C
#define MTXORB_FTDI_RANGE_016D_PID	0x016D
#define MTXORB_FTDI_RANGE_016E_PID	0x016E
#define MTXORB_FTDI_RANGE_016F_PID	0x016F
#define MTXORB_FTDI_RANGE_0170_PID	0x0170
#define MTXORB_FTDI_RANGE_0171_PID	0x0171
#define MTXORB_FTDI_RANGE_0172_PID	0x0172
#define MTXORB_FTDI_RANGE_0173_PID	0x0173
#define MTXORB_FTDI_RANGE_0174_PID	0x0174
#define MTXORB_FTDI_RANGE_0175_PID	0x0175
#define MTXORB_FTDI_RANGE_0176_PID	0x0176
#define MTXORB_FTDI_RANGE_0177_PID	0x0177
#define MTXORB_FTDI_RANGE_0178_PID	0x0178
#define MTXORB_FTDI_RANGE_0179_PID	0x0179
#define MTXORB_FTDI_RANGE_017A_PID	0x017A
#define MTXORB_FTDI_RANGE_017B_PID	0x017B
#define MTXORB_FTDI_RANGE_017C_PID	0x017C
#define MTXORB_FTDI_RANGE_017D_PID	0x017D
#define MTXORB_FTDI_RANGE_017E_PID	0x017E
#define MTXORB_FTDI_RANGE_017F_PID	0x017F
#define MTXORB_FTDI_RANGE_0180_PID	0x0180
#define MTXORB_FTDI_RANGE_0181_PID	0x0181
#define MTXORB_FTDI_RANGE_0182_PID	0x0182
#define MTXORB_FTDI_RANGE_0183_PID	0x0183
#define MTXORB_FTDI_RANGE_0184_PID	0x0184
#define MTXORB_FTDI_RANGE_0185_PID	0x0185
#define MTXORB_FTDI_RANGE_0186_PID	0x0186
#define MTXORB_FTDI_RANGE_0187_PID	0x0187
#define MTXORB_FTDI_RANGE_0188_PID	0x0188
#define MTXORB_FTDI_RANGE_0189_PID	0x0189
#define MTXORB_FTDI_RANGE_018A_PID	0x018A
#define MTXORB_FTDI_RANGE_018B_PID	0x018B
#define MTXORB_FTDI_RANGE_018C_PID	0x018C
#define MTXORB_FTDI_RANGE_018D_PID	0x018D
#define MTXORB_FTDI_RANGE_018E_PID	0x018E
#define MTXORB_FTDI_RANGE_018F_PID	0x018F
#define MTXORB_FTDI_RANGE_0190_PID	0x0190
#define MTXORB_FTDI_RANGE_0191_PID	0x0191
#define MTXORB_FTDI_RANGE_0192_PID	0x0192
#define MTXORB_FTDI_RANGE_0193_PID	0x0193
#define MTXORB_FTDI_RANGE_0194_PID	0x0194
#define MTXORB_FTDI_RANGE_0195_PID	0x0195
#define MTXORB_FTDI_RANGE_0196_PID	0x0196
#define MTXORB_FTDI_RANGE_0197_PID	0x0197
#define MTXORB_FTDI_RANGE_0198_PID	0x0198
#define MTXORB_FTDI_RANGE_0199_PID	0x0199
#define MTXORB_FTDI_RANGE_019A_PID	0x019A
#define MTXORB_FTDI_RANGE_019B_PID	0x019B
#define MTXORB_FTDI_RANGE_019C_PID	0x019C
#define MTXORB_FTDI_RANGE_019D_PID	0x019D
#define MTXORB_FTDI_RANGE_019E_PID	0x019E
#define MTXORB_FTDI_RANGE_019F_PID	0x019F
#define MTXORB_FTDI_RANGE_01A0_PID	0x01A0
#define MTXORB_FTDI_RANGE_01A1_PID	0x01A1
#define MTXORB_FTDI_RANGE_01A2_PID	0x01A2
#define MTXORB_FTDI_RANGE_01A3_PID	0x01A3
#define MTXORB_FTDI_RANGE_01A4_PID	0x01A4
#define MTXORB_FTDI_RANGE_01A5_PID	0x01A5
#define MTXORB_FTDI_RANGE_01A6_PID	0x01A6
#define MTXORB_FTDI_RANGE_01A7_PID	0x01A7
#define MTXORB_FTDI_RANGE_01A8_PID	0x01A8
#define MTXORB_FTDI_RANGE_01A9_PID	0x01A9
#define MTXORB_FTDI_RANGE_01AA_PID	0x01AA
#define MTXORB_FTDI_RANGE_01AB_PID	0x01AB
#define MTXORB_FTDI_RANGE_01AC_PID	0x01AC
#define MTXORB_FTDI_RANGE_01AD_PID	0x01AD
#define MTXORB_FTDI_RANGE_01AE_PID	0x01AE
#define MTXORB_FTDI_RANGE_01AF_PID	0x01AF
#define MTXORB_FTDI_RANGE_01B0_PID	0x01B0
#define MTXORB_FTDI_RANGE_01B1_PID	0x01B1
#define MTXORB_FTDI_RANGE_01B2_PID	0x01B2
#define MTXORB_FTDI_RANGE_01B3_PID	0x01B3
#define MTXORB_FTDI_RANGE_01B4_PID	0x01B4
#define MTXORB_FTDI_RANGE_01B5_PID	0x01B5
#define MTXORB_FTDI_RANGE_01B6_PID	0x01B6
#define MTXORB_FTDI_RANGE_01B7_PID	0x01B7
#define MTXORB_FTDI_RANGE_01B8_PID	0x01B8
#define MTXORB_FTDI_RANGE_01B9_PID	0x01B9
#define MTXORB_FTDI_RANGE_01BA_PID	0x01BA
#define MTXORB_FTDI_RANGE_01BB_PID	0x01BB
#define MTXORB_FTDI_RANGE_01BC_PID	0x01BC
#define MTXORB_FTDI_RANGE_01BD_PID	0x01BD
#define MTXORB_FTDI_RANGE_01BE_PID	0x01BE
#define MTXORB_FTDI_RANGE_01BF_PID	0x01BF
#define MTXORB_FTDI_RANGE_01C0_PID	0x01C0
#define MTXORB_FTDI_RANGE_01C1_PID	0x01C1
#define MTXORB_FTDI_RANGE_01C2_PID	0x01C2
#define MTXORB_FTDI_RANGE_01C3_PID	0x01C3
#define MTXORB_FTDI_RANGE_01C4_PID	0x01C4
#define MTXORB_FTDI_RANGE_01C5_PID	0x01C5
#define MTXORB_FTDI_RANGE_01C6_PID	0x01C6
#define MTXORB_FTDI_RANGE_01C7_PID	0x01C7
#define MTXORB_FTDI_RANGE_01C8_PID	0x01C8
#define MTXORB_FTDI_RANGE_01C9_PID	0x01C9
#define MTXORB_FTDI_RANGE_01CA_PID	0x01CA
#define MTXORB_FTDI_RANGE_01CB_PID	0x01CB
#define MTXORB_FTDI_RANGE_01CC_PID	0x01CC
#define MTXORB_FTDI_RANGE_01CD_PID	0x01CD
#define MTXORB_FTDI_RANGE_01CE_PID	0x01CE
#define MTXORB_FTDI_RANGE_01CF_PID	0x01CF
#define MTXORB_FTDI_RANGE_01D0_PID	0x01D0
#define MTXORB_FTDI_RANGE_01D1_PID	0x01D1
#define MTXORB_FTDI_RANGE_01D2_PID	0x01D2
#define MTXORB_FTDI_RANGE_01D3_PID	0x01D3
#define MTXORB_FTDI_RANGE_01D4_PID	0x01D4
#define MTXORB_FTDI_RANGE_01D5_PID	0x01D5
#define MTXORB_FTDI_RANGE_01D6_PID	0x01D6
#define MTXORB_FTDI_RANGE_01D7_PID	0x01D7
#define MTXORB_FTDI_RANGE_01D8_PID	0x01D8
#define MTXORB_FTDI_RANGE_01D9_PID	0x01D9
#define MTXORB_FTDI_RANGE_01DA_PID	0x01DA
#define MTXORB_FTDI_RANGE_01DB_PID	0x01DB
#define MTXORB_FTDI_RANGE_01DC_PID	0x01DC
#define MTXORB_FTDI_RANGE_01DD_PID	0x01DD
#define MTXORB_FTDI_RANGE_01DE_PID	0x01DE
#define MTXORB_FTDI_RANGE_01DF_PID	0x01DF
#define MTXORB_FTDI_RANGE_01E0_PID	0x01E0
#define MTXORB_FTDI_RANGE_01E1_PID	0x01E1
#define MTXORB_FTDI_RANGE_01E2_PID	0x01E2
#define MTXORB_FTDI_RANGE_01E3_PID	0x01E3
#define MTXORB_FTDI_RANGE_01E4_PID	0x01E4
#define MTXORB_FTDI_RANGE_01E5_PID	0x01E5
#define MTXORB_FTDI_RANGE_01E6_PID	0x01E6
#define MTXORB_FTDI_RANGE_01E7_PID	0x01E7
#define MTXORB_FTDI_RANGE_01E8_PID	0x01E8
#define MTXORB_FTDI_RANGE_01E9_PID	0x01E9
#define MTXORB_FTDI_RANGE_01EA_PID	0x01EA
#define MTXORB_FTDI_RANGE_01EB_PID	0x01EB
#define MTXORB_FTDI_RANGE_01EC_PID	0x01EC
#define MTXORB_FTDI_RANGE_01ED_PID	0x01ED
#define MTXORB_FTDI_RANGE_01EE_PID	0x01EE
#define MTXORB_FTDI_RANGE_01EF_PID	0x01EF
#define MTXORB_FTDI_RANGE_01F0_PID	0x01F0
#define MTXORB_FTDI_RANGE_01F1_PID	0x01F1
#define MTXORB_FTDI_RANGE_01F2_PID	0x01F2
#define MTXORB_FTDI_RANGE_01F3_PID	0x01F3
#define MTXORB_FTDI_RANGE_01F4_PID	0x01F4
#define MTXORB_FTDI_RANGE_01F5_PID	0x01F5
#define MTXORB_FTDI_RANGE_01F6_PID	0x01F6
#define MTXORB_FTDI_RANGE_01F7_PID	0x01F7
#define MTXORB_FTDI_RANGE_01F8_PID	0x01F8
#define MTXORB_FTDI_RANGE_01F9_PID	0x01F9
#define MTXORB_FTDI_RANGE_01FA_PID	0x01FA
#define MTXORB_FTDI_RANGE_01FB_PID	0x01FB
#define MTXORB_FTDI_RANGE_01FC_PID	0x01FC
#define MTXORB_FTDI_RANGE_01FD_PID	0x01FD
#define MTXORB_FTDI_RANGE_01FE_PID	0x01FE
#define MTXORB_FTDI_RANGE_01FF_PID	0x01FF



/*
 * The Mobility Lab (TML)
 * Submitted by Pierre Castella
 */
#define TML_VID			0x1B91	/* Vendor ID */
#define TML_USB_SERIAL_PID	0x0064	/* USB - Serial Converter */

/* Alti-2 products  http://www.alti-2.com */
#define ALTI2_VID	0x1BC9
#define ALTI2_N3_PID	0x6001	/* Neptune 3 */

/*
 * Ionics PlugComputer
 */
#define IONICS_VID			0x1c0c
#define IONICS_PLUGCOMPUTER_PID		0x0102

/*
 * Dresden Elektronik Sensor Terminal Board
 */
#define DE_VID			0x1cf1 /* Vendor ID */
#define STB_PID			0x0001 /* Sensor Terminal Board */
#define WHT_PID			0x0004 /* Wireless Handheld Terminal */

/*
 * STMicroelectonics
 */
#define ST_VID			0x0483
#define ST_STMCLT1030_PID	0x3747 /* ST Micro Connect Lite STMCLT1030 */

/*
 * Papouch products (http://www.papouch.com/)
 * Submitted by Folkert van Heusden
 */

#define PAPOUCH_VID			0x5050	/* Vendor ID */
#define PAPOUCH_SB485_PID		0x0100	/* Papouch SB485 USB-485/422 Converter */
#define PAPOUCH_AP485_PID		0x0101	/* AP485 USB-RS485 Converter */
#define PAPOUCH_SB422_PID		0x0102	/* Papouch SB422 USB-RS422 Converter  */
#define PAPOUCH_SB485_2_PID		0x0103	/* Papouch SB485 USB-485/422 Converter */
#define PAPOUCH_AP485_2_PID		0x0104	/* AP485 USB-RS485 Converter */
#define PAPOUCH_SB422_2_PID		0x0105	/* Papouch SB422 USB-RS422 Converter  */
#define PAPOUCH_SB485S_PID		0x0106	/* Papouch SB485S USB-485/422 Converter */
#define PAPOUCH_SB485C_PID		0x0107	/* Papouch SB485C USB-485/422 Converter */
#define PAPOUCH_LEC_PID			0x0300	/* LEC USB Converter */
#define PAPOUCH_SB232_PID		0x0301	/* Papouch SB232 USB-RS232 Converter */
#define PAPOUCH_TMU_PID			0x0400	/* TMU USB Thermometer */
#define PAPOUCH_IRAMP_PID		0x0500	/* Papouch IRAmp Duplex */
#define PAPOUCH_DRAK5_PID		0x0700	/* Papouch DRAK5 */
#define PAPOUCH_QUIDO8x8_PID		0x0800	/* Papouch Quido 8/8 Module */
#define PAPOUCH_QUIDO4x4_PID		0x0900	/* Papouch Quido 4/4 Module */
#define PAPOUCH_QUIDO2x2_PID		0x0a00	/* Papouch Quido 2/2 Module */
#define PAPOUCH_QUIDO10x1_PID		0x0b00	/* Papouch Quido 10/1 Module */
#define PAPOUCH_QUIDO30x3_PID		0x0c00	/* Papouch Quido 30/3 Module */
#define PAPOUCH_QUIDO60x3_PID		0x0d00	/* Papouch Quido 60(100)/3 Module */
#define PAPOUCH_QUIDO2x16_PID		0x0e00	/* Papouch Quido 2/16 Module */
#define PAPOUCH_QUIDO3x32_PID		0x0f00	/* Papouch Quido 3/32 Module */
#define PAPOUCH_DRAK6_PID		0x1000	/* Papouch DRAK6 */
#define PAPOUCH_UPSUSB_PID		0x8000	/* Papouch UPS-USB adapter */
#define PAPOUCH_MU_PID			0x8001	/* MU controller */
#define PAPOUCH_SIMUKEY_PID		0x8002	/* Papouch SimuKey */
#define PAPOUCH_AD4USB_PID		0x8003	/* AD4USB Measurement Module */
#define PAPOUCH_GMUX_PID		0x8004	/* Papouch GOLIATH MUX */
#define PAPOUCH_GMSR_PID		0x8005	/* Papouch GOLIATH MSR */

/*
 * Marvell SheevaPlug
 */
#define MARVELL_VID		0x9e88
#define MARVELL_SHEEVAPLUG_PID	0x9e8f

/*
 * Evolution Robotics products (http://www.evolution.com/).
 * Submitted by Shawn M. Lavelle.
 */
#define EVOLUTION_VID		0xDEEE	/* Vendor ID */
#define EVOLUTION_ER1_PID	0x0300	/* ER1 Control Module */
#define EVO_8U232AM_PID		0x02FF	/* Evolution robotics RCM2 (FT232AM)*/
#define EVO_HYBRID_PID		0x0302	/* Evolution robotics RCM4 PID (FT232BM)*/
#define EVO_RCM4_PID		0x0303	/* Evolution robotics RCM4 PID */

/*
 * MJS Gadgets HD Radio / XM Radio / Sirius Radio interfaces (using VID 0x0403)
 */
#define MJSG_GENERIC_PID	0x9378
#define MJSG_SR_RADIO_PID	0x9379
#define MJSG_XM_RADIO_PID	0x937A
#define MJSG_HD_RADIO_PID	0x937C

/*
 * D.O.Tec products (http://www.directout.eu)
 */
#define FTDI_DOTEC_PID 0x9868

/*
 * Xverve Signalyzer tools (http://www.signalyzer.com/)
 */
#define XVERVE_SIGNALYZER_ST_PID	0xBCA0
#define XVERVE_SIGNALYZER_SLITE_PID	0xBCA1
#define XVERVE_SIGNALYZER_SH2_PID	0xBCA2
#define XVERVE_SIGNALYZER_SH4_PID	0xBCA4

/*
 * Segway Robotic Mobility Platform USB interface (using VID 0x0403)
 * Submitted by John G. Rogers
 */
#define SEGWAY_RMP200_PID	0xe729


/*
 * Accesio USB Data Acquisition products (http://www.accesio.com/)
 */
#define ACCESIO_COM4SM_PID 	0xD578

/* www.sciencescope.co.uk educational dataloggers */
#define FTDI_SCIENCESCOPE_LOGBOOKML_PID		0xFF18
#define FTDI_SCIENCESCOPE_LS_LOGBOOK_PID	0xFF1C
#define FTDI_SCIENCESCOPE_HS_LOGBOOK_PID	0xFF1D

/*
 * Milkymist One JTAG/Serial
 */
#define QIHARDWARE_VID			0x20B7
#define MILKYMISTONE_JTAGSERIAL_PID	0x0713

/*
 * CTI GmbH RS485 Converter http://www.cti-lean.com/
 */
/* USB-485-Mini*/
#define FTDI_CTI_MINI_PID	0xF608
/* USB-Nano-485*/
#define FTDI_CTI_NANO_PID	0xF60B


