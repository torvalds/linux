/*
 * Broadcom device-specific manifest constants.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: bcmdevs.h 484136 2014-06-12 04:36:10Z $
 */

#ifndef	_BCMDEVS_H
#define	_BCMDEVS_H

/* PCI vendor IDs */
#define	VENDOR_EPIGRAM		0xfeda
#define	VENDOR_BROADCOM		0x14e4
#define	VENDOR_3COM		0x10b7
#define	VENDOR_NETGEAR		0x1385
#define	VENDOR_DIAMOND		0x1092
#define	VENDOR_INTEL		0x8086
#define	VENDOR_DELL		0x1028
#define	VENDOR_HP		0x103c
#define	VENDOR_HP_COMPAQ	0x0e11
#define	VENDOR_APPLE		0x106b
#define VENDOR_SI_IMAGE		0x1095		/* Silicon Image, used by Arasan SDIO Host */
#define VENDOR_BUFFALO		0x1154		/* Buffalo vendor id */
#define VENDOR_TI		0x104c		/* Texas Instruments */
#define VENDOR_RICOH		0x1180		/* Ricoh */
#define VENDOR_JMICRON		0x197b


/* PCMCIA vendor IDs */
#define	VENDOR_BROADCOM_PCMCIA	0x02d0

/* SDIO vendor IDs */
#define	VENDOR_BROADCOM_SDIO	0x00BF

/* DONGLE VID/PIDs */
#define BCM_DNGL_VID		0x0a5c
#define BCM_DNGL_BL_PID_4328	0xbd12
#define BCM_DNGL_BL_PID_4322	0xbd13
#define BCM_DNGL_BL_PID_4319    0xbd16
#define BCM_DNGL_BL_PID_43236   0xbd17
#define BCM_DNGL_BL_PID_4332	0xbd18
#define BCM_DNGL_BL_PID_4330	0xbd19
#define BCM_DNGL_BL_PID_4334	0xbd1a
#define BCM_DNGL_BL_PID_43239   0xbd1b
#define BCM_DNGL_BL_PID_4324	0xbd1c
#define BCM_DNGL_BL_PID_4360	0xbd1d
#define BCM_DNGL_BL_PID_43143	0xbd1e
#define BCM_DNGL_BL_PID_43242	0xbd1f
#define BCM_DNGL_BL_PID_43342	0xbd21
#define BCM_DNGL_BL_PID_4335	0xbd20
#define BCM_DNGL_BL_PID_43341	0xbd22
#define BCM_DNGL_BL_PID_4350    0xbd23
#define BCM_DNGL_BL_PID_4345    0xbd24
#define BCM_DNGL_BL_PID_4349	0xbd25
#define BCM_DNGL_BL_PID_4354	0xbd26
#define BCM_DNGL_BL_PID_43569   0xbd27
#define BCM_DNGL_BL_PID_43909	0xbd28

#define BCM_DNGL_BDC_PID	0x0bdc
#define BCM_DNGL_JTAG_PID	0x4a44

/* HW USB BLOCK [CPULESS USB] PIDs */
#define BCM_HWUSB_PID_43239     43239

/* PCI Device IDs */
#define	BCM4210_DEVICE_ID	0x1072		/* never used */
#define	BCM4230_DEVICE_ID	0x1086		/* never used */
#define	BCM4401_ENET_ID		0x170c		/* 4401b0 production enet cards */
#define	BCM3352_DEVICE_ID	0x3352		/* bcm3352 device id */
#define	BCM3360_DEVICE_ID	0x3360		/* bcm3360 device id */
#define	BCM4211_DEVICE_ID	0x4211
#define	BCM4231_DEVICE_ID	0x4231
#define	BCM4303_D11B_ID		0x4303		/* 4303 802.11b */
#define	BCM4311_D11G_ID		0x4311		/* 4311 802.11b/g id */
#define	BCM4311_D11DUAL_ID	0x4312		/* 4311 802.11a/b/g id */
#define	BCM4311_D11A_ID		0x4313		/* 4311 802.11a id */
#define	BCM4328_D11DUAL_ID	0x4314		/* 4328/4312 802.11a/g id */
#define	BCM4328_D11G_ID		0x4315		/* 4328/4312 802.11g id */
#define	BCM4328_D11A_ID		0x4316		/* 4328/4312 802.11a id */
#define	BCM4318_D11G_ID		0x4318		/* 4318 802.11b/g id */
#define	BCM4318_D11DUAL_ID	0x4319		/* 4318 802.11a/b/g id */
#define	BCM4318_D11A_ID		0x431a		/* 4318 802.11a id */
#define	BCM4325_D11DUAL_ID	0x431b		/* 4325 802.11a/g id */
#define	BCM4325_D11G_ID		0x431c		/* 4325 802.11g id */
#define	BCM4325_D11A_ID		0x431d		/* 4325 802.11a id */
#define	BCM4306_D11G_ID		0x4320		/* 4306 802.11g */
#define	BCM4306_D11A_ID		0x4321		/* 4306 802.11a */
#define	BCM4306_UART_ID		0x4322		/* 4306 uart */
#define	BCM4306_V90_ID		0x4323		/* 4306 v90 codec */
#define	BCM4306_D11DUAL_ID	0x4324		/* 4306 dual A+B */
#define	BCM4306_D11G_ID2	0x4325		/* BCM4306_D11G_ID; INF w/loose binding war */
#define	BCM4321_D11N_ID		0x4328		/* 4321 802.11n dualband id */
#define	BCM4321_D11N2G_ID	0x4329		/* 4321 802.11n 2.4Ghz band id */
#define	BCM4321_D11N5G_ID	0x432a		/* 4321 802.11n 5Ghz band id */
#define BCM4322_D11N_ID		0x432b		/* 4322 802.11n dualband device */
#define BCM4322_D11N2G_ID	0x432c		/* 4322 802.11n 2.4GHz device */
#define BCM4322_D11N5G_ID	0x432d		/* 4322 802.11n 5GHz device */
#define BCM4329_D11N_ID		0x432e		/* 4329 802.11n dualband device */
#define BCM4329_D11N2G_ID	0x432f		/* 4329 802.11n 2.4G device */
#define BCM4329_D11N5G_ID	0x4330		/* 4329 802.11n 5G device */
#define	BCM4315_D11DUAL_ID	0x4334		/* 4315 802.11a/g id */
#define	BCM4315_D11G_ID		0x4335		/* 4315 802.11g id */
#define	BCM4315_D11A_ID		0x4336		/* 4315 802.11a id */
#define BCM4319_D11N_ID		0x4337		/* 4319 802.11n dualband device */
#define BCM4319_D11N2G_ID	0x4338		/* 4319 802.11n 2.4G device */
#define BCM4319_D11N5G_ID	0x4339		/* 4319 802.11n 5G device */
#define BCM43231_D11N2G_ID	0x4340		/* 43231 802.11n 2.4GHz device */
#define BCM43221_D11N2G_ID	0x4341		/* 43221 802.11n 2.4GHz device */
#define BCM43222_D11N_ID	0x4350		/* 43222 802.11n dualband device */
#define BCM43222_D11N2G_ID	0x4351		/* 43222 802.11n 2.4GHz device */
#define BCM43222_D11N5G_ID	0x4352		/* 43222 802.11n 5GHz device */
#define BCM43224_D11N_ID	0x4353		/* 43224 802.11n dualband device */
#define BCM43224_D11N_ID_VEN1	0x0576		/* Vendor specific 43224 802.11n db device */
#define BCM43226_D11N_ID	0x4354		/* 43226 802.11n dualband device */
#define BCM43236_D11N_ID	0x4346		/* 43236 802.11n dualband device */
#define BCM43236_D11N2G_ID	0x4347		/* 43236 802.11n 2.4GHz device */
#define BCM43236_D11N5G_ID	0x4348		/* 43236 802.11n 5GHz device */
#define BCM43225_D11N2G_ID	0x4357		/* 43225 802.11n 2.4GHz device */
#define BCM43421_D11N_ID	0xA99D		/* 43421 802.11n dualband device */
#define BCM4313_D11N2G_ID	0x4727		/* 4313 802.11n 2.4G device */
#define BCM4330_D11N_ID         0x4360          /* 4330 802.11n dualband device */
#define BCM4330_D11N2G_ID       0x4361          /* 4330 802.11n 2.4G device */
#define BCM4330_D11N5G_ID       0x4362          /* 4330 802.11n 5G device */
#define BCM4336_D11N_ID		0x4343		/* 4336 802.11n 2.4GHz device */
#define BCM6362_D11N_ID		0x435f		/* 6362 802.11n dualband device */
#define BCM6362_D11N2G_ID	0x433f		/* 6362 802.11n 2.4Ghz band id */
#define BCM6362_D11N5G_ID	0x434f		/* 6362 802.11n 5Ghz band id */
#define BCM4331_D11N_ID		0x4331		/* 4331 802.11n dualband id */
#define BCM4331_D11N2G_ID	0x4332		/* 4331 802.11n 2.4Ghz band id */
#define BCM4331_D11N5G_ID	0x4333		/* 4331 802.11n 5Ghz band id */
#define BCM43237_D11N_ID	0x4355		/* 43237 802.11n dualband device */
#define BCM43237_D11N5G_ID	0x4356		/* 43237 802.11n 5GHz device */
#define BCM43227_D11N2G_ID	0x4358		/* 43228 802.11n 2.4GHz device */
#define BCM43228_D11N_ID	0x4359		/* 43228 802.11n DualBand device */
#define BCM43228_D11N5G_ID	0x435a		/* 43228 802.11n 5GHz device */
#define BCM43362_D11N_ID	0x4363		/* 43362 802.11n 2.4GHz device */
#define BCM43239_D11N_ID	0x4370		/* 43239 802.11n dualband device */
#define BCM4324_D11N_ID		0x4374		/* 4324 802.11n dualband device */
#define BCM43217_D11N2G_ID	0x43a9		/* 43217 802.11n 2.4GHz device */
#define BCM43131_D11N2G_ID	0x43aa		/* 43131 802.11n 2.4GHz device */
#define BCM4314_D11N2G_ID	0x4364		/* 4314 802.11n 2.4G device */
#define BCM43142_D11N2G_ID	0x4365		/* 43142 802.11n 2.4G device */
#define BCM43143_D11N2G_ID	0x4366		/* 43143 802.11n 2.4G device */
#define BCM4334_D11N_ID		0x4380		/* 4334 802.11n dualband device */
#define BCM4334_D11N2G_ID	0x4381		/* 4334 802.11n 2.4G device */
#define BCM4334_D11N5G_ID	0x4382		/* 4334 802.11n 5G device */
#define BCM43342_D11N_ID	0x4383		/* 43342 802.11n dualband device */
#define BCM43342_D11N2G_ID	0x4384		/* 43342 802.11n 2.4G device */
#define BCM43342_D11N5G_ID	0x4385		/* 43342 802.11n 5G device */
#define BCM43341_D11N_ID	0x4386		/* 43341 802.11n dualband device */
#define BCM43341_D11N2G_ID	0x4387		/* 43341 802.11n 2.4G device */
#define BCM43341_D11N5G_ID	0x4388		/* 43341 802.11n 5G device */
#define BCM4360_D11AC_ID	0x43a0
#define BCM4360_D11AC2G_ID	0x43a1
#define BCM4360_D11AC5G_ID	0x43a2
#define BCM4345_D11AC_ID	0x43ab		/* 4345 802.11ac dualband device */
#define BCM4345_D11AC2G_ID	0x43ac		/* 4345 802.11ac 2.4G device */
#define BCM4345_D11AC5G_ID	0x43ad		/* 4345 802.11ac 5G device */
#define BCM4335_D11AC_ID	0x43ae
#define BCM4335_D11AC2G_ID	0x43af
#define BCM4335_D11AC5G_ID	0x43b0
#define BCM4352_D11AC_ID	0x43b1		/* 4352 802.11ac dualband device */
#define BCM4352_D11AC2G_ID	0x43b2		/* 4352 802.11ac 2.4G device */
#define BCM4352_D11AC5G_ID	0x43b3		/* 4352 802.11ac 5G device */
#define BCM43602_D11AC_ID	0x43ba		/* ac dualband PCI devid SPROM programmed */
#define BCM43602_D11AC2G_ID	0x43bb		/* 43602 802.11ac 2.4G device */
#define BCM43602_D11AC5G_ID	0x43bc		/* 43602 802.11ac 5G device */
#define BCM4349_D11AC_ID	0x4349		/* 4349 802.11ac dualband device */
#define BCM4349_D11AC2G_ID	0x43dd		/* 4349 802.11ac 2.4G device */
#define BCM4349_D11AC5G_ID	0x43de		/* 4349 802.11ac 5G device */
#define BCM4355_D11AC_ID	0x43d3		/* 4355 802.11ac dualband device */
#define BCM4355_D11AC2G_ID	0x43d4		/* 4355 802.11ac 2.4G device */
#define BCM4355_D11AC5G_ID	0x43d5		/* 4355 802.11ac 5G device */
#define BCM4359_D11AC_ID	0x43d6		/* 4359 802.11ac dualband device */
#define BCM4359_D11AC2G_ID	0x43d7		/* 4359 802.11ac 2.4G device */
#define BCM4359_D11AC5G_ID	0x43d8		/* 4359 802.11ac 5G device */

/* PCI Subsystem ID */
#define BCM943228HMB_SSID_VEN1	0x0607
#define BCM94313HMGBL_SSID_VEN1	0x0608
#define BCM94313HMG_SSID_VEN1	0x0609
#define BCM943142HM_SSID_VEN1	0x0611

#define BCM43143_D11N2G_ID	0x4366		/* 43143 802.11n 2.4G device */

#define BCM43242_D11N_ID	0x4367		/* 43242 802.11n dualband device */
#define BCM43242_D11N2G_ID	0x4368		/* 43242 802.11n 2.4G device */
#define BCM43242_D11N5G_ID	0x4369		/* 43242 802.11n 5G device */

#define BCM4350_D11AC_ID	0x43a3
#define BCM4350_D11AC2G_ID	0x43a4
#define BCM4350_D11AC5G_ID	0x43a5

#define BCM43556_D11AC_ID	0x43b7
#define BCM43556_D11AC2G_ID	0x43b8
#define BCM43556_D11AC5G_ID	0x43b9

#define BCM43558_D11AC_ID	0x43c0
#define BCM43558_D11AC2G_ID	0x43c1
#define BCM43558_D11AC5G_ID	0x43c2

#define BCM43566_D11AC_ID	0x43d3
#define BCM43566_D11AC2G_ID	0x43d4
#define BCM43566_D11AC5G_ID	0x43d5

#define BCM43568_D11AC_ID	0x43d6
#define BCM43568_D11AC2G_ID	0x43d7
#define BCM43568_D11AC5G_ID	0x43d8

#define BCM43569_D11AC_ID	0x43d9
#define BCM43569_D11AC2G_ID	0x43da
#define BCM43569_D11AC5G_ID	0x43db

#define BCM43570_D11AC_ID	0x43d9
#define BCM43570_D11AC2G_ID	0x43da
#define BCM43570_D11AC5G_ID	0x43db

#define BCM4354_D11AC_ID	0x43df		/* 4354 802.11ac dualband device */
#define BCM4354_D11AC2G_ID	0x43e0		/* 4354 802.11ac 2.4G device */
#define BCM4354_D11AC5G_ID	0x43e1		/* 4354 802.11ac 5G device */
#define BCM43430_D11N2G_ID	0x43e2		/* 43430 802.11n 2.4G device */


#define BCM43349_D11N_ID	0x43e6		/* 43349 802.11n dualband id */
#define BCM43349_D11N2G_ID	0x43e7		/* 43349 802.11n 2.4Ghz band id */
#define BCM43349_D11N5G_ID	0x43e8		/* 43349 802.11n 5Ghz band id */

#define BCM4358_D11AC_ID        0x43e9          /* 4358 802.11ac dualband device */
#define BCM4358_D11AC2G_ID      0x43ea          /* 4358 802.11ac 2.4G device */
#define BCM4358_D11AC5G_ID      0x43eb          /* 4358 802.11ac 5G device */

#define BCM4356_D11AC_ID	0x43ec		/* 4356 802.11ac dualband device */
#define BCM4356_D11AC2G_ID	0x43ed		/* 4356 802.11ac 2.4G device */
#define BCM4356_D11AC5G_ID	0x43ee		/* 4356 802.11ac 5G device */

#define	BCMGPRS_UART_ID		0x4333		/* Uart id used by 4306/gprs card */
#define	BCMGPRS2_UART_ID	0x4344		/* Uart id used by 4306/gprs card */
#define FPGA_JTAGM_ID		0x43f0		/* FPGA jtagm device id */
#define BCM_JTAGM_ID		0x43f1		/* BCM jtagm device id */
#define SDIOH_FPGA_ID		0x43f2		/* sdio host fpga */
#define BCM_SDIOH_ID		0x43f3		/* BCM sdio host id */
#define SDIOD_FPGA_ID		0x43f4		/* sdio device fpga */
#define SPIH_FPGA_ID		0x43f5		/* PCI SPI Host Controller FPGA */
#define BCM_SPIH_ID		0x43f6		/* Synopsis SPI Host Controller */
#define MIMO_FPGA_ID		0x43f8		/* FPGA mimo minimacphy device id */
#define BCM_JTAGM2_ID		0x43f9		/* BCM alternate jtagm device id */
#define SDHCI_FPGA_ID		0x43fa		/* Standard SDIO Host Controller FPGA */
#define	BCM4402_ENET_ID		0x4402		/* 4402 enet */
#define	BCM4402_V90_ID		0x4403		/* 4402 v90 codec */
#define	BCM4410_DEVICE_ID	0x4410		/* bcm44xx family pci iline */
#define	BCM4412_DEVICE_ID	0x4412		/* bcm44xx family pci enet */
#define	BCM4430_DEVICE_ID	0x4430		/* bcm44xx family cardbus iline */
#define	BCM4432_DEVICE_ID	0x4432		/* bcm44xx family cardbus enet */
#define	BCM4704_ENET_ID		0x4706		/* 4704 enet (Use 47XX_ENET_ID instead!) */
#define	BCM4710_DEVICE_ID	0x4710		/* 4710 primary function 0 */
#define	BCM47XX_AUDIO_ID	0x4711		/* 47xx audio codec */
#define	BCM47XX_V90_ID		0x4712		/* 47xx v90 codec */
#define	BCM47XX_ENET_ID		0x4713		/* 47xx enet */
#define	BCM47XX_EXT_ID		0x4714		/* 47xx external i/f */
#define	BCM47XX_GMAC_ID		0x4715		/* 47xx Unimac based GbE */
#define	BCM47XX_USBH_ID		0x4716		/* 47xx usb host */
#define	BCM47XX_USBD_ID		0x4717		/* 47xx usb device */
#define	BCM47XX_IPSEC_ID	0x4718		/* 47xx ipsec */
#define	BCM47XX_ROBO_ID		0x4719		/* 47xx/53xx roboswitch core */
#define	BCM47XX_USB20H_ID	0x471a		/* 47xx usb 2.0 host */
#define	BCM47XX_USB20D_ID	0x471b		/* 47xx usb 2.0 device */
#define	BCM47XX_ATA100_ID	0x471d		/* 47xx parallel ATA */
#define	BCM47XX_SATAXOR_ID	0x471e		/* 47xx serial ATA & XOR DMA */
#define	BCM47XX_GIGETH_ID	0x471f		/* 47xx GbE (5700) */
#define	BCM4712_MIPS_ID		0x4720		/* 4712 base devid */
#define	BCM4716_DEVICE_ID	0x4722		/* 4716 base devid */
#define	BCM47XX_USB30H_ID	0x472a		/* 47xx usb 3.0 host */
#define	BCM47XX_USB30D_ID	0x472b		/* 47xx usb 3.0 device */
#define BCM47XX_SMBUS_EMU_ID	0x47fe		/* 47xx emulated SMBus device */
#define	BCM47XX_XOR_EMU_ID	0x47ff		/* 47xx emulated XOR engine */
#define	EPI41210_DEVICE_ID	0xa0fa		/* bcm4210 */
#define	EPI41230_DEVICE_ID	0xa10e		/* bcm4230 */
#define JINVANI_SDIOH_ID	0x4743		/* Jinvani SDIO Gold Host */
#define BCM27XX_SDIOH_ID	0x2702		/* BCM27xx Standard SDIO Host */
#define PCIXX21_FLASHMEDIA_ID	0x803b		/* TI PCI xx21 Standard Host Controller */
#define PCIXX21_SDIOH_ID	0x803c		/* TI PCI xx21 Standard Host Controller */
#define R5C822_SDIOH_ID		0x0822		/* Ricoh Co Ltd R5C822 SD/SDIO/MMC/MS/MSPro Host */
#define JMICRON_SDIOH_ID	0x2381		/* JMicron Standard SDIO Host Controller */

/* Chip IDs */
#define	BCM4306_CHIP_ID		0x4306		/* 4306 chipcommon chipid */
#define	BCM4311_CHIP_ID		0x4311		/* 4311 PCIe 802.11a/b/g */
#define	BCM43111_CHIP_ID	43111		/* 43111 chipcommon chipid (OTP chipid) */
#define	BCM43112_CHIP_ID	43112		/* 43112 chipcommon chipid (OTP chipid) */
#define	BCM4312_CHIP_ID		0x4312		/* 4312 chipcommon chipid */
#define BCM4313_CHIP_ID		0x4313		/* 4313 chip id */
#define	BCM43131_CHIP_ID	43131		/* 43131 chip id (OTP chipid) */
#define	BCM4315_CHIP_ID		0x4315		/* 4315 chip id */
#define	BCM4318_CHIP_ID		0x4318		/* 4318 chipcommon chipid */
#define	BCM4319_CHIP_ID		0x4319		/* 4319 chip id */
#define	BCM4320_CHIP_ID		0x4320		/* 4320 chipcommon chipid */
#define	BCM4321_CHIP_ID		0x4321		/* 4321 chipcommon chipid */
#define	BCM43217_CHIP_ID	43217		/* 43217 chip id (OTP chipid) */
#define	BCM4322_CHIP_ID		0x4322		/* 4322 chipcommon chipid */
#define	BCM43221_CHIP_ID	43221		/* 43221 chipcommon chipid (OTP chipid) */
#define	BCM43222_CHIP_ID	43222		/* 43222 chipcommon chipid */
#define	BCM43224_CHIP_ID	43224		/* 43224 chipcommon chipid */
#define	BCM43225_CHIP_ID	43225		/* 43225 chipcommon chipid */
#define	BCM43227_CHIP_ID	43227		/* 43227 chipcommon chipid */
#define	BCM43228_CHIP_ID	43228		/* 43228 chipcommon chipid */
#define	BCM43226_CHIP_ID	43226		/* 43226 chipcommon chipid */
#define	BCM43231_CHIP_ID	43231		/* 43231 chipcommon chipid (OTP chipid) */
#define	BCM43234_CHIP_ID	43234		/* 43234 chipcommon chipid */
#define	BCM43235_CHIP_ID	43235		/* 43235 chipcommon chipid */
#define	BCM43236_CHIP_ID	43236		/* 43236 chipcommon chipid */
#define	BCM43237_CHIP_ID	43237		/* 43237 chipcommon chipid */
#define	BCM43238_CHIP_ID	43238		/* 43238 chipcommon chipid */
#define	BCM43239_CHIP_ID	43239		/* 43239 chipcommon chipid */
#define	BCM43420_CHIP_ID	43420		/* 43222 chipcommon chipid (OTP, RBBU) */
#define	BCM43421_CHIP_ID	43421		/* 43224 chipcommon chipid (OTP, RBBU) */
#define	BCM43428_CHIP_ID	43428		/* 43228 chipcommon chipid (OTP, RBBU) */
#define	BCM43431_CHIP_ID	43431		/* 4331  chipcommon chipid (OTP, RBBU) */
#define	BCM43460_CHIP_ID	43460		/* 4360  chipcommon chipid (OTP, RBBU) */
#define	BCM4325_CHIP_ID		0x4325		/* 4325 chip id */
#define	BCM4328_CHIP_ID		0x4328		/* 4328 chip id */
#define	BCM4329_CHIP_ID		0x4329		/* 4329 chipcommon chipid */
#define	BCM4331_CHIP_ID		0x4331		/* 4331 chipcommon chipid */
#define BCM4336_CHIP_ID		0x4336		/* 4336 chipcommon chipid */
#define BCM43362_CHIP_ID	43362		/* 43362 chipcommon chipid */
#define BCM4330_CHIP_ID		0x4330		/* 4330 chipcommon chipid */
#define BCM6362_CHIP_ID		0x6362		/* 6362 chipcommon chipid */
#define BCM4314_CHIP_ID		0x4314		/* 4314 chipcommon chipid */
#define BCM43142_CHIP_ID	43142		/* 43142 chipcommon chipid */
#define BCM43143_CHIP_ID	43143		/* 43143 chipcommon chipid */
#define	BCM4324_CHIP_ID		0x4324		/* 4324 chipcommon chipid */
#define	BCM43242_CHIP_ID	43242		/* 43242 chipcommon chipid */
#define	BCM43243_CHIP_ID	43243		/* 43243 chipcommon chipid */
#define BCM4334_CHIP_ID		0x4334		/* 4334 chipcommon chipid */
#define BCM4335_CHIP_ID		0x4335		/* 4335 chipcommon chipid */
#define BCM4339_CHIP_ID		0x4339		/* 4339 chipcommon chipid */
#define BCM43349_CHIP_ID	43349			/* 43349(0xA955) chipcommon chipid */
#define BCM4360_CHIP_ID		0x4360          /* 4360 chipcommon chipid */
#define BCM4352_CHIP_ID		0x4352          /* 4352 chipcommon chipid */
#define BCM43526_CHIP_ID	0xAA06
#define BCM43340_CHIP_ID	43340		/* 43340 chipcommon chipid */
#define BCM43341_CHIP_ID	43341		/* 43341 chipcommon chipid */
#define BCM43342_CHIP_ID	43342		/* 43342 chipcommon chipid */
#define BCM4350_CHIP_ID		0x4350          /* 4350 chipcommon chipid */
#define BCM4354_CHIP_ID		0x4354          /* 4354 chipcommon chipid */
#define BCM4356_CHIP_ID		0x4356          /* 4356 chipcommon chipid */
#define BCM43556_CHIP_ID	0xAA24          /* 43556 chipcommon chipid */
#define BCM43558_CHIP_ID	0xAA26          /* 43558 chipcommon chipid */
#define BCM43566_CHIP_ID	0xAA2E          /* 43566 chipcommon chipid */
#define BCM43567_CHIP_ID	0xAA2F          /* 43567 chipcommon chipid */
#define BCM43568_CHIP_ID	0xAA30          /* 43568 chipcommon chipid */
#define BCM43569_CHIP_ID	0xAA31          /* 43569 chipcommon chipid */
#define BCM43570_CHIP_ID	0xAA32          /* 43570 chipcommon chipid */
#define BCM4358_CHIP_ID         0x4358          /* 4358 chipcommon chipid */
#define BCM4371_CHIP_ID		0x4371          /* 4371 chipcommon chipid */
#define BCM4350_CHIP(chipid)	((CHIPID(chipid) == BCM4350_CHIP_ID) || \
				(CHIPID(chipid) == BCM4354_CHIP_ID) || \
				(CHIPID(chipid) == BCM4356_CHIP_ID) || \
				(CHIPID(chipid) == BCM43556_CHIP_ID) || \
				(CHIPID(chipid) == BCM43558_CHIP_ID) || \
				(CHIPID(chipid) == BCM43566_CHIP_ID) || \
				(CHIPID(chipid) == BCM43567_CHIP_ID) || \
				(CHIPID(chipid) == BCM43568_CHIP_ID) || \
				(CHIPID(chipid) == BCM43569_CHIP_ID) || \
				(CHIPID(chipid) == BCM43570_CHIP_ID) || \
				(CHIPID(chipid) == BCM4358_CHIP_ID)) /* 4350 variations */
#define BCM4345_CHIP_ID		0x4345		/* 4345 chipcommon chipid */
#define BCM43430_CHIP_ID	43430		/* 43430 chipcommon chipid */
#define BCM4349_CHIP_ID		0x4349		/* 4349 chipcommon chipid */
#define BCM4355_CHIP_ID		0x4355		/* 4355 chipcommon chipid */
#define BCM4359_CHIP_ID		0x4359		/* 4359 chipcommon chipid */
#define BCM4349_CHIP(chipid)	((CHIPID(chipid) == BCM4349_CHIP_ID) || \
				(CHIPID(chipid) == BCM4355_CHIP_ID) || \
				(CHIPID(chipid) == BCM4359_CHIP_ID))
#define BCM4349_CHIP_GRPID		BCM4349_CHIP_ID: \
					case BCM4355_CHIP_ID: \
					case BCM4359_CHIP_ID

#define BCM43602_CHIP_ID	0xaa52		/* 43602 chipcommon chipid */
#define BCM43462_CHIP_ID	0xa9c6		/* 43462 chipcommon chipid */

#define	BCM4342_CHIP_ID		4342		/* 4342 chipcommon chipid (OTP, RBBU) */
#define	BCM4402_CHIP_ID		0x4402		/* 4402 chipid */
#define	BCM4704_CHIP_ID		0x4704		/* 4704 chipcommon chipid */
#define	BCM4706_CHIP_ID		0x5300		/* 4706 chipcommon chipid */
#define BCM4707_CHIP_ID		53010		/* 4707 chipcommon chipid */
#define BCM53018_CHIP_ID	53018		/* 53018 chipcommon chipid */
#define BCM4707_CHIP(chipid)	(((chipid) == BCM4707_CHIP_ID) || ((chipid) == BCM53018_CHIP_ID))
#define	BCM4710_CHIP_ID		0x4710		/* 4710 chipid */
#define	BCM4712_CHIP_ID		0x4712		/* 4712 chipcommon chipid */
#define	BCM4716_CHIP_ID		0x4716		/* 4716 chipcommon chipid */
#define	BCM47162_CHIP_ID	47162		/* 47162 chipcommon chipid */
#define	BCM4748_CHIP_ID		0x4748		/* 4716 chipcommon chipid (OTP, RBBU) */
#define	BCM4749_CHIP_ID		0x4749		/* 5357 chipcommon chipid (OTP, RBBU) */
#define BCM4785_CHIP_ID		0x4785		/* 4785 chipcommon chipid */
#define	BCM5350_CHIP_ID		0x5350		/* 5350 chipcommon chipid */
#define	BCM5352_CHIP_ID		0x5352		/* 5352 chipcommon chipid */
#define	BCM5354_CHIP_ID		0x5354		/* 5354 chipcommon chipid */
#define BCM5365_CHIP_ID		0x5365		/* 5365 chipcommon chipid */
#define	BCM5356_CHIP_ID		0x5356		/* 5356 chipcommon chipid */
#define	BCM5357_CHIP_ID		0x5357		/* 5357 chipcommon chipid */
#define	BCM53572_CHIP_ID	53572		/* 53572 chipcommon chipid */

/* Package IDs */
#define	BCM4303_PKG_ID		2		/* 4303 package id */
#define	BCM4309_PKG_ID		1		/* 4309 package id */
#define	BCM4712LARGE_PKG_ID	0		/* 340pin 4712 package id */
#define	BCM4712SMALL_PKG_ID	1		/* 200pin 4712 package id */
#define	BCM4712MID_PKG_ID	2		/* 225pin 4712 package id */
#define BCM4328USBD11G_PKG_ID	2		/* 4328 802.11g USB package id */
#define BCM4328USBDUAL_PKG_ID	3		/* 4328 802.11a/g USB package id */
#define BCM4328SDIOD11G_PKG_ID	4		/* 4328 802.11g SDIO package id */
#define BCM4328SDIODUAL_PKG_ID	5		/* 4328 802.11a/g SDIO package id */
#define BCM4329_289PIN_PKG_ID	0		/* 4329 289-pin package id */
#define BCM4329_182PIN_PKG_ID	1		/* 4329N 182-pin package id */
#define BCM5354E_PKG_ID		1		/* 5354E package id */
#define	BCM4716_PKG_ID		8		/* 4716 package id */
#define	BCM4717_PKG_ID		9		/* 4717 package id */
#define	BCM4718_PKG_ID		10		/* 4718 package id */
#define BCM5356_PKG_NONMODE	1		/* 5356 package without nmode suppport */
#define BCM5358U_PKG_ID		8		/* 5358U package id */
#define BCM5358_PKG_ID		9		/* 5358 package id */
#define BCM47186_PKG_ID		10		/* 47186 package id */
#define BCM5357_PKG_ID		11		/* 5357 package id */
#define BCM5356U_PKG_ID		12		/* 5356U package id */
#define BCM53572_PKG_ID		8		/* 53572 package id */
#define BCM5357C0_PKG_ID	8		/* 5357c0 package id (the same as 53572) */
#define BCM47188_PKG_ID		9		/* 47188 package id */
#define BCM5358C0_PKG_ID	0xa		/* 5358c0 package id */
#define BCM5356C0_PKG_ID	0xb		/* 5356c0 package id */
#define BCM4331TT_PKG_ID        8		/* 4331 12x12 package id */
#define BCM4331TN_PKG_ID        9		/* 4331 12x9 package id */
#define BCM4331TNA0_PKG_ID     0xb		/* 4331 12x9 package id */
#define	BCM4706L_PKG_ID		1		/* 4706L package id */

#define HDLSIM5350_PKG_ID	1		/* HDL simulator package id for a 5350 */
#define HDLSIM_PKG_ID		14		/* HDL simulator package id */
#define HWSIM_PKG_ID		15		/* Hardware simulator package id */
#define BCM43224_FAB_CSM	0x8		/* the chip is manufactured by CSM */
#define BCM43224_FAB_SMIC	0xa		/* the chip is manufactured by SMIC */
#define BCM4336_WLBGA_PKG_ID	0x8
#define BCM4330_WLBGA_PKG_ID	0x0
#define BCM4314PCIE_ARM_PKG_ID		(8 | 0)	/* 4314 QFN PCI package id, bit 3 tie high */
#define BCM4314SDIO_PKG_ID		(8 | 1)	/* 4314 QFN SDIO package id */
#define BCM4314PCIE_PKG_ID		(8 | 2)	/* 4314 QFN PCI (ARM-less) package id */
#define BCM4314SDIO_ARM_PKG_ID		(8 | 3)	/* 4314 QFN SDIO (ARM-less) package id */
#define BCM4314SDIO_FPBGA_PKG_ID	(8 | 4)	/* 4314 FpBGA SDIO package id */
#define BCM4314DEV_PKG_ID		(8 | 6)	/* 4314 Developement package id */

#define BCM4707_PKG_ID		1		/* 4707 package id */
#define BCM4708_PKG_ID		2		/* 4708 package id */
#define BCM4709_PKG_ID		0		/* 4709 package id */

#define PCIXX21_FLASHMEDIA0_ID	0x8033		/* TI PCI xx21 Standard Host Controller */
#define PCIXX21_SDIOH0_ID	0x8034		/* TI PCI xx21 Standard Host Controller */

#define BCM4335_WLCSP_PKG_ID	(0x0)	/* WLCSP Module/Mobile SDIO/HSIC. */
#define BCM4335_FCBGA_PKG_ID	(0x1)	/* FCBGA PC/Embeded/Media PCIE/SDIO */
#define BCM4335_WLBGA_PKG_ID	(0x2)	/* WLBGA COB/Mobile SDIO/HSIC. */
#define BCM4335_FCBGAD_PKG_ID	(0x3)	/* FCBGA Debug Debug/Dev All if's. */
#define BCM4335_PKG_MASK	(0x3)

/* boardflags */
#define	BFL_BTC2WIRE		0x00000001  /* old 2wire Bluetooth coexistence, OBSOLETE */
#define BFL_BTCOEX      0x00000001      /* Board supports BTCOEX */
#define	BFL_PACTRL		0x00000002  /* Board has gpio 9 controlling the PA */
#define BFL_AIRLINEMODE	0x00000004  /* Board implements gpio 13 radio disable indication, UNUSED */
#define	BFL_ADCDIV		0x00000008  /* Board has the rssi ADC divider */
#define BFL_DIS_256QAM		0x00000008
#define	BFL_ENETROBO		0x00000010  /* Board has robo switch or core */
#define	BFL_TSSIAVG   		0x00000010  /* TSSI averaging for ACPHY chips */
#define	BFL_NOPLLDOWN		0x00000020  /* Not ok to power down the chip pll and oscillator */
#define	BFL_CCKHIPWR		0x00000040  /* Can do high-power CCK transmission */
#define	BFL_ENETADM		0x00000080  /* Board has ADMtek switch */
#define	BFL_ENETVLAN		0x00000100  /* Board has VLAN capability */
#define	BFL_LTECOEX		0x00000200  /* LTE Coex enabled */
#define BFL_NOPCI		0x00000400  /* Board leaves PCI floating */
#define BFL_FEM			0x00000800  /* Board supports the Front End Module */
#define BFL_EXTLNA		0x00001000  /* Board has an external LNA in 2.4GHz band */
#define BFL_HGPA		0x00002000  /* Board has a high gain PA */
#define	BFL_BTC2WIRE_ALTGPIO	0x00004000  /* Board's BTC 2wire is in the alternate gpios */
#define	BFL_ALTIQ		0x00008000  /* Alternate I/Q settings */
#define BFL_NOPA		0x00010000  /* Board has no PA */
#define BFL_RSSIINV		0x00020000  /* Board's RSSI uses positive slope(not TSSI) */
#define BFL_PAREF		0x00040000  /* Board uses the PARef LDO */
#define BFL_3TSWITCH		0x00080000  /* Board uses a triple throw switch shared with BT */
#define BFL_PHASESHIFT		0x00100000  /* Board can support phase shifter */
#define BFL_BUCKBOOST		0x00200000  /* Power topology uses BUCKBOOST */
#define BFL_FEM_BT		0x00400000  /* Board has FEM and switch to share antenna w/ BT */
#define BFL_NOCBUCK		0x00800000  /* Power topology doesn't use CBUCK */
#define BFL_CCKFAVOREVM		0x01000000  /* Favor CCK EVM over spectral mask */
#define BFL_PALDO		0x02000000  /* Power topology uses PALDO */
#define BFL_LNLDO2_2P5		0x04000000  /* Select 2.5V as LNLDO2 output voltage */
#define BFL_FASTPWR		0x08000000
#define BFL_UCPWRCTL_MININDX	0x08000000  /* Enforce min power index to avoid FEM damage */
#define BFL_EXTLNA_5GHz		0x10000000  /* Board has an external LNA in 5GHz band */
#define BFL_TRSW_1by2		0x20000000  /* Board has 2 TRSW's in 1by2 designs */
#define BFL_GAINBOOSTA01        0x20000000  /* 5g Gainboost for core0 and core1 */
#define BFL_LO_TRSW_R_5GHz	0x40000000  /* In 5G do not throw TRSW to T for clipLO gain */
#define BFL_ELNA_GAINDEF	0x80000000  /* Backoff InitGain based on elna_2g/5g field
					     * when this flag is set
					     */
#define BFL_EXTLNA_TX	0x20000000	/* Temp boardflag to indicate to */

/* boardflags2 */
#define BFL2_RXBB_INT_REG_DIS	0x00000001  /* Board has an external rxbb regulator */
#define BFL2_APLL_WAR		0x00000002  /* Flag to implement alternative A-band PLL settings */
#define BFL2_TXPWRCTRL_EN	0x00000004  /* Board permits enabling TX Power Control */
#define BFL2_2X4_DIV		0x00000008  /* Board supports the 2X4 diversity switch */
#define BFL2_5G_PWRGAIN		0x00000010  /* Board supports 5G band power gain */
#define BFL2_PCIEWAR_OVR	0x00000020  /* Board overrides ASPM and Clkreq settings */
#define BFL2_CAESERS_BRD	0x00000040  /* Board is Caesers brd (unused by sw) */
#define BFL2_BTC3WIRE		0x00000080  /* Board support legacy 3 wire or 4 wire */
#define BFL2_BTCLEGACY          0x00000080  /* Board support legacy 3/4 wire, to replace
					     * BFL2_BTC3WIRE
					     */
#define BFL2_SKWRKFEM_BRD	0x00000100  /* 4321mcm93 board uses Skyworks FEM */
#define BFL2_SPUR_WAR		0x00000200  /* Board has a WAR for clock-harmonic spurs */
#define BFL2_GPLL_WAR		0x00000400  /* Flag to narrow G-band PLL loop b/w */
#define BFL2_TRISTATE_LED	0x00000800  /* Tri-state the LED */
#define BFL2_SINGLEANT_CCK	0x00001000  /* Tx CCK pkts on Ant 0 only */
#define BFL2_2G_SPUR_WAR	0x00002000  /* WAR to reduce and avoid clock-harmonic spurs in 2G */
#define BFL2_BPHY_ALL_TXCORES	0x00004000  /* Transmit bphy frames using all tx cores */
#define BFL2_FCC_BANDEDGE_WAR	0x00008000  /* Activates WAR to improve FCC bandedge performance */
#define BFL2_DAC_SPUR_IMPROVEMENT 0x00008000       /* Reducing DAC Spurs */
#define BFL2_GPLL_WAR2	        0x00010000  /* Flag to widen G-band PLL loop b/w */
#define BFL2_REDUCED_PA_TURNONTIME 0x00010000  /* Flag to reduce PA turn on Time */
#define BFL2_IPALVLSHIFT_3P3    0x00020000
#define BFL2_INTERNDET_TXIQCAL  0x00040000  /* Use internal envelope detector for TX IQCAL */
#define BFL2_XTALBUFOUTEN       0x00080000  /* Keep the buffered Xtal output from radio on */
				/* Most drivers will turn it off without this flag */
				/* to save power. */

#define BFL2_ANAPACTRL_2G	0x00100000  /* 2G ext PAs are controlled by analog PA ctrl lines */
#define BFL2_ANAPACTRL_5G	0x00200000  /* 5G ext PAs are controlled by analog PA ctrl lines */
#define BFL2_ELNACTRL_TRSW_2G	0x00400000  /* AZW4329: 2G gmode_elna_gain controls TR Switch */
#define BFL2_BT_SHARE_ANT0	0x00800000  /* share core0 antenna with BT */
#define BFL2_TEMPSENSE_HIGHER	0x01000000  /* The tempsense threshold can sustain higher value
					     * than programmed. The exact delta is decided by
					     * driver per chip/boardtype. This can be used
					     * when tempsense qualification happens after shipment
					     */
#define BFL2_BTC3WIREONLY       0x02000000  /* standard 3 wire btc only.  4 wire not supported */
#define BFL2_PWR_NOMINAL	0x04000000  /* 0: power reduction on, 1: no power reduction */
#define BFL2_EXTLNA_PWRSAVE	0x08000000  /* boardflag to enable ucode to apply power save */
						/* ucode control of eLNA during Tx */
#define BFL2_4313_RADIOREG	0x10000000
									   /*  board rework */
#define BFL2_DYNAMIC_VMID	0x10000000  /* enable dynamic Vmid in idle TSSI CAL for 4331 */

#define BFL2_SDR_EN		0x20000000  /* SDR enabled or disabled */
#define BFL2_DYNAMIC_VMID	0x10000000  /* boardflag to enable dynamic Vmid idle TSSI CAL */
#define BFL2_LNA1BYPFORTR2G	0x40000000  /* acphy, enable lna1 bypass for clip gain, 2g */
#define BFL2_LNA1BYPFORTR5G	0x80000000  /* acphy, enable lna1 bypass for clip gain, 5g */

/* SROM 11 - 11ac boardflag definitions */
#define BFL_SROM11_BTCOEX  0x00000001  /* Board supports BTCOEX */
#define BFL_SROM11_WLAN_BT_SH_XTL  0x00000002  /* bluetooth and wlan share same crystal */
#define BFL_SROM11_EXTLNA	0x00001000  /* Board has an external LNA in 2.4GHz band */
#define BFL_SROM11_EPA_TURNON_TIME     0x00018000  /* 2 bits for different PA turn on times */
#define BFL_SROM11_EPA_TURNON_TIME_SHIFT  15
#define BFL_SROM11_EXTLNA_5GHz	0x10000000  /* Board has an external LNA in 5GHz band */
#define BFL_SROM11_GAINBOOSTA01	0x20000000  /* 5g Gainboost for core0 and core1 */
#define BFL2_SROM11_APLL_WAR	0x00000002  /* Flag to implement alternative A-band PLL settings */
#define BFL2_SROM11_ANAPACTRL_2G  0x00100000  /* 2G ext PAs are ctrl-ed by analog PA ctrl lines */
#define BFL2_SROM11_ANAPACTRL_5G  0x00200000  /* 5G ext PAs are ctrl-ed by analog PA ctrl lines */
#define BFL2_SROM11_SINGLEANT_CCK	0x00001000  /* Tx CCK pkts on Ant 0 only */

/* boardflags3 */
#define BFL3_FEMCTRL_SUB	  0x00000007  /* acphy, subrevs of femctrl on top of srom_femctrl */
#define BFL3_RCAL_WAR		  0x00000008  /* acphy, rcal war active on this board (4335a0) */
#define BFL3_TXGAINTBLID	  0x00000070  /* acphy, txgain table id */
#define BFL3_TXGAINTBLID_SHIFT	  0x4         /* acphy, txgain table id shift bit */
#define BFL3_TSSI_DIV_WAR	  0x00000080  /* acphy, Seperate paparam for 20/40/80 */
#define BFL3_TSSI_DIV_WAR_SHIFT	  0x7         /* acphy, Seperate paparam for 20/40/80 shift bit */
#define BFL3_FEMTBL_FROM_NVRAM    0x00000100  /* acphy, femctrl table is read from nvram */
#define BFL3_FEMTBL_FROM_NVRAM_SHIFT 0x8         /* acphy, femctrl table is read from nvram */
#define BFL3_AGC_CFG_2G           0x00000200  /* acphy, gain control configuration for 2G */
#define BFL3_AGC_CFG_5G           0x00000400  /* acphy, gain control configuration for 5G */
#define BFL3_PPR_BIT_EXT          0x00000800  /* acphy, bit position for 1bit extension for ppr */
#define BFL3_PPR_BIT_EXT_SHIFT    11          /* acphy, bit shift for 1bit extension for ppr */
#define BFL3_BBPLL_SPR_MODE_DIS	  0x00001000  /* acphy, disables bbpll spur modes */
#define BFL3_RCAL_OTP_VAL_EN      0x00002000  /* acphy, to read rcal_trim value from otp */
#define BFL3_2GTXGAINTBL_BLANK	  0x00004000  /* acphy, blank the first X ticks of 2g gaintbl */
#define BFL3_2GTXGAINTBL_BLANK_SHIFT 14       /* acphy, blank the first X ticks of 2g gaintbl */
#define BFL3_5GTXGAINTBL_BLANK	  0x00008000  /* acphy, blank the first X ticks of 5g gaintbl */
#define BFL3_5GTXGAINTBL_BLANK_SHIFT 15       /* acphy, blank the first X ticks of 5g gaintbl */
#define BFL3_PHASETRACK_MAX_ALPHABETA	  0x00010000  /* acphy, to max out alpha,beta to 511 */
#define BFL3_PHASETRACK_MAX_ALPHABETA_SHIFT 16       /* acphy, to max out alpha,beta to 511 */
/* acphy, to use backed off gaintbl for lte-coex */
#define BFL3_LTECOEX_GAINTBL_EN           0x00060000
/* acphy, to use backed off gaintbl for lte-coex */
#define BFL3_LTECOEX_GAINTBL_EN_SHIFT 17
#define BFL3_5G_SPUR_WAR          0x00080000  /* acphy, enable spur WAR in 5G band */
#define BFL3_1X1_RSDB_ANT	  0x01000000  /* to find if 2-ant RSDB board or 1-ant RSDB board */
#define BFL3_1X1_RSDB_ANT_SHIFT           24

/* acphy: lpmode2g and lpmode_5g related boardflags */
#define BFL3_ACPHY_LPMODE_2G	  0x00300000  /* bits 20:21 for lpmode_2g choice */
#define BFL3_ACPHY_LPMODE_2G_SHIFT	  20

#define BFL3_ACPHY_LPMODE_5G	  0x00C00000  /* bits 22:23 for lpmode_5g choice */
#define BFL3_ACPHY_LPMODE_5G_SHIFT	  22

#define BFL3_EXT_LPO_ISCLOCK      0x02000000  /* External LPO is clock, not x-tal */
#define BFL3_FORCE_INT_LPO_SEL    0x04000000  /* Force internal lpo */
#define BFL3_FORCE_EXT_LPO_SEL    0x08000000  /* Force external lpo */

#define BFL3_EN_BRCM_IMPBF        0x10000000  /* acphy, Allow BRCM Implicit TxBF */
#define BFL3_AVVMID_FROM_NVRAM    0x40000000  /* Read Av Vmid from NVRAM  */
#define BFL3_VLIN_EN_FROM_NVRAM    0x80000000  /* Read Vlin En from NVRAM  */

#define BFL3_AVVMID_FROM_NVRAM_SHIFT   30   /* Read Av Vmid from NVRAM  */
#define BFL3_VLIN_EN_FROM_NVRAM_SHIFT   31   /* Enable Vlin  from NVRAM  */


/* board specific GPIO assignment, gpio 0-3 are also customer-configurable led */
#define	BOARD_GPIO_BTC3W_IN	0x850	/* bit 4 is RF_ACTIVE, bit 6 is STATUS, bit 11 is PRI */
#define	BOARD_GPIO_BTC3W_OUT	0x020	/* bit 5 is TX_CONF */
#define	BOARD_GPIO_BTCMOD_IN	0x010	/* bit 4 is the alternate BT Coexistence Input */
#define	BOARD_GPIO_BTCMOD_OUT	0x020	/* bit 5 is the alternate BT Coexistence Out */
#define	BOARD_GPIO_BTC_IN	0x080	/* bit 7 is BT Coexistence Input */
#define	BOARD_GPIO_BTC_OUT	0x100	/* bit 8 is BT Coexistence Out */
#define	BOARD_GPIO_PACTRL	0x200	/* bit 9 controls the PA on new 4306 boards */
#define BOARD_GPIO_12		0x1000	/* gpio 12 */
#define BOARD_GPIO_13		0x2000	/* gpio 13 */
#define BOARD_GPIO_BTC4_IN	0x0800	/* gpio 11, coex4, in */
#define BOARD_GPIO_BTC4_BT	0x2000	/* gpio 12, coex4, bt active */
#define BOARD_GPIO_BTC4_STAT	0x4000	/* gpio 14, coex4, status */
#define BOARD_GPIO_BTC4_WLAN	0x8000	/* gpio 15, coex4, wlan active */
#define	BOARD_GPIO_1_WLAN_PWR	0x02	/* throttle WLAN power on X21 board */
#define	BOARD_GPIO_2_WLAN_PWR	0x04	/* throttle WLAN power on X29C board */
#define	BOARD_GPIO_3_WLAN_PWR	0x08	/* throttle WLAN power on X28 board */
#define	BOARD_GPIO_4_WLAN_PWR	0x10	/* throttle WLAN power on X19 board */

#define GPIO_BTC4W_OUT_4312  0x010  /* bit 4 is BT_IODISABLE */
#define GPIO_BTC4W_OUT_43224  0x020  /* bit 5 is BT_IODISABLE */
#define GPIO_BTC4W_OUT_43224_SHARED  0x0e0  /* bit 5 is BT_IODISABLE */
#define GPIO_BTC4W_OUT_43225  0x0e0  /* bit 5 BT_IODISABLE, bit 6 SW_BT, bit 7 SW_WL */
#define GPIO_BTC4W_OUT_43421  0x020  /* bit 5 is BT_IODISABLE */
#define GPIO_BTC4W_OUT_4313  0x060  /* bit 5 SW_BT, bit 6 SW_WL */
#define GPIO_BTC4W_OUT_4331_SHARED  0x010  /* GPIO 4  */

#define	PCI_CFG_GPIO_SCS	0x10	/* PCI config space bit 4 for 4306c0 slow clock source */
#define PCI_CFG_GPIO_HWRAD	0x20	/* PCI config space GPIO 13 for hw radio disable */
#define PCI_CFG_GPIO_XTAL	0x40	/* PCI config space GPIO 14 for Xtal power-up */
#define PCI_CFG_GPIO_PLL	0x80	/* PCI config space GPIO 15 for PLL power-down */

/* power control defines */
#define PLL_DELAY		150		/* us pll on delay */
#define FREF_DELAY		200		/* us fref change delay */
#define MIN_SLOW_CLK		32		/* us Slow clock period */
#define	XTAL_ON_DELAY		1000		/* us crystal power-on delay */

#ifndef LINUX_POSTMOGRIFY_REMOVAL
/* Reference Board Types */
#define	BU4710_BOARD		0x0400
#define	VSIM4710_BOARD		0x0401
#define	QT4710_BOARD		0x0402

#define	BU4309_BOARD		0x040a
#define	BCM94309CB_BOARD	0x040b
#define	BCM94309MP_BOARD	0x040c
#define	BCM4309AP_BOARD		0x040d

#define	BCM94302MP_BOARD	0x040e

#define	BU4306_BOARD		0x0416
#define	BCM94306CB_BOARD	0x0417
#define	BCM94306MP_BOARD	0x0418

#define	BCM94710D_BOARD		0x041a
#define	BCM94710R1_BOARD	0x041b
#define	BCM94710R4_BOARD	0x041c
#define	BCM94710AP_BOARD	0x041d

#define	BU2050_BOARD		0x041f

#define	BCM94306P50_BOARD	0x0420

#define	BCM94309G_BOARD		0x0421

#define	BU4704_BOARD		0x0423
#define	BU4702_BOARD		0x0424

#define	BCM94306PC_BOARD	0x0425		/* pcmcia 3.3v 4306 card */

#define	MPSG4306_BOARD		0x0427

#define	BCM94702MN_BOARD	0x0428

/* BCM4702 1U CompactPCI Board */
#define	BCM94702CPCI_BOARD	0x0429

/* BCM4702 with BCM95380 VLAN Router */
#define	BCM95380RR_BOARD	0x042a

/* cb4306 with SiGe PA */
#define	BCM94306CBSG_BOARD	0x042b

/* cb4306 with SiGe PA */
#define	PCSG94306_BOARD		0x042d

/* bu4704 with sdram */
#define	BU4704SD_BOARD		0x042e

/* Dual 11a/11g Router */
#define	BCM94704AGR_BOARD	0x042f

/* 11a-only minipci */
#define	BCM94308MP_BOARD	0x0430

/* 4306/gprs combo */
#define	BCM94306GPRS_BOARD	0x0432

/* BCM5365/BCM4704 FPGA Bringup Board */
#define BU5365_FPGA_BOARD	0x0433

#define BU4712_BOARD		0x0444
#define	BU4712SD_BOARD		0x045d
#define	BU4712L_BOARD		0x045f

/* BCM4712 boards */
#define BCM94712AP_BOARD	0x0445
#define BCM94712P_BOARD		0x0446

/* BCM4318 boards */
#define BU4318_BOARD		0x0447
#define CB4318_BOARD		0x0448
#define MPG4318_BOARD		0x0449
#define MP4318_BOARD		0x044a
#define SD4318_BOARD		0x044b

/* BCM4313 boards */
#define BCM94313BU_BOARD	0x050f
#define BCM94313HM_BOARD	0x0510
#define BCM94313EPA_BOARD	0x0511
#define BCM94313HMG_BOARD       0x051C

/* BCM63XX boards */
#define BCM96338_BOARD		0x6338
#define BCM96348_BOARD		0x6348
#define BCM96358_BOARD		0x6358
#define BCM96368_BOARD		0x6368

/* Another mp4306 with SiGe */
#define	BCM94306P_BOARD		0x044c

/* mp4303 */
#define	BCM94303MP_BOARD	0x044e

/* mpsgh4306 */
#define	BCM94306MPSGH_BOARD	0x044f

/* BRCM 4306 w/ Front End Modules */
#define BCM94306MPM		0x0450
#define BCM94306MPL		0x0453

/* 4712agr */
#define	BCM94712AGR_BOARD	0x0451

/* pcmcia 4303 */
#define	PC4303_BOARD		0x0454

/* 5350K */
#define	BCM95350K_BOARD		0x0455

/* 5350R */
#define	BCM95350R_BOARD		0x0456

/* 4306mplna */
#define	BCM94306MPLNA_BOARD	0x0457

/* 4320 boards */
#define	BU4320_BOARD		0x0458
#define	BU4320S_BOARD		0x0459
#define	BCM94320PH_BOARD	0x045a

/* 4306mph */
#define	BCM94306MPH_BOARD	0x045b

/* 4306pciv */
#define	BCM94306PCIV_BOARD	0x045c

#define	BU4712SD_BOARD		0x045d

#define	BCM94320PFLSH_BOARD	0x045e

#define	BU4712L_BOARD		0x045f
#define	BCM94712LGR_BOARD	0x0460
#define	BCM94320R_BOARD		0x0461

#define	BU5352_BOARD		0x0462

#define	BCM94318MPGH_BOARD	0x0463

#define	BU4311_BOARD		0x0464
#define	BCM94311MC_BOARD	0x0465
#define	BCM94311MCAG_BOARD	0x0466

#define	BCM95352GR_BOARD	0x0467

/* bcm95351agr */
#define	BCM95351AGR_BOARD	0x0470

/* bcm94704mpcb */
#define	BCM94704MPCB_BOARD	0x0472

/* 4785 boards */
#define BU4785_BOARD		0x0478

/* 4321 boards */
#define BU4321_BOARD		0x046b
#define BU4321E_BOARD		0x047c
#define MP4321_BOARD		0x046c
#define CB2_4321_BOARD		0x046d
#define CB2_4321_AG_BOARD	0x0066
#define MC4321_BOARD		0x046e

/* 4328 boards */
#define BU4328_BOARD		0x0481
#define BCM4328SDG_BOARD	0x0482
#define BCM4328SDAG_BOARD	0x0483
#define BCM4328UG_BOARD		0x0484
#define BCM4328UAG_BOARD	0x0485
#define BCM4328PC_BOARD		0x0486
#define BCM4328CF_BOARD		0x0487

/* 4325 boards */
#define BCM94325DEVBU_BOARD	0x0490
#define BCM94325BGABU_BOARD	0x0491

#define BCM94325SDGWB_BOARD	0x0492

#define BCM94325SDGMDL_BOARD	0x04aa
#define BCM94325SDGMDL2_BOARD	0x04c6
#define BCM94325SDGMDL3_BOARD	0x04c9

#define BCM94325SDABGWBA_BOARD	0x04e1

/* 4322 boards */
#define BCM94322MC_SSID		0x04a4
#define BCM94322USB_SSID	0x04a8	/* dualband */
#define BCM94322HM_SSID		0x04b0
#define BCM94322USB2D_SSID	0x04bf	/* single band discrete front end */

/* 4312 boards */
#define	BCM4312MCGSG_BOARD	0x04b5

/* 4315 boards */
#define BCM94315DEVBU_SSID	0x04c2
#define BCM94315USBGP_SSID	0x04c7
#define BCM94315BGABU_SSID	0x04ca
#define BCM94315USBGP41_SSID	0x04cb

/* 4319 boards */
#define BCM94319DEVBU_SSID	0X04e5
#define BCM94319USB_SSID	0X04e6
#define BCM94319SD_SSID		0X04e7

/* 4716 boards */
#define BCM94716NR2_SSID	0x04cd

/* 4319 boards */
#define BCM94319DEVBU_SSID	0X04e5
#define BCM94319USBNP4L_SSID	0X04e6
#define BCM94319WLUSBN4L_SSID	0X04e7
#define BCM94319SDG_SSID	0X04ea
#define BCM94319LCUSBSDN4L_SSID	0X04eb
#define BCM94319USBB_SSID       0x04ee
#define BCM94319LCSDN4L_SSID	0X0507
#define BCM94319LSUSBN4L_SSID	0X0508
#define BCM94319SDNA4L_SSID	0X0517
#define BCM94319SDELNA4L_SSID	0X0518
#define BCM94319SDELNA6L_SSID	0X0539
#define BCM94319ARCADYAN_SSID	0X0546
#define BCM94319WINDSOR_SSID    0x0561
#define BCM94319MLAP_SSID       0x0562
#define BCM94319SDNA_SSID       0x058b
#define BCM94319BHEMU3_SSID     0x0563
#define BCM94319SDHMB_SSID     0x058c
#define BCM94319SDBREF_SSID     0x05a1
#define BCM94319USBSDB_SSID     0x05a2


/* 4329 boards */
#define BCM94329AGB_SSID	0X04b9
#define BCM94329TDKMDL1_SSID	0X04ba
#define BCM94329TDKMDL11_SSID	0X04fc
#define BCM94329OLYMPICN18_SSID	0X04fd
#define BCM94329OLYMPICN90_SSID	0X04fe
#define BCM94329OLYMPICN90U_SSID 0X050c
#define BCM94329OLYMPICN90M_SSID 0X050b
#define BCM94329AGBF_SSID	0X04ff
#define BCM94329OLYMPICX17_SSID	0X0504
#define BCM94329OLYMPICX17M_SSID	0X050a
#define BCM94329OLYMPICX17U_SSID	0X0509
#define BCM94329OLYMPICUNO_SSID	0X0564
#define BCM94329MOTOROLA_SSID   0X0565
#define BCM94329OLYMPICLOCO_SSID	0X0568
/* 4336 SDIO board types */
#define BCM94336SD_WLBGABU_SSID		0x0511
#define BCM94336SD_WLBGAREF_SSID	0x0519
#define BCM94336SDGP_SSID	0x0538
#define BCM94336SDG_SSID	0x0519
#define BCM94336SDGN_SSID	0x0538
#define BCM94336SDGFC_SSID	0x056B

/* 4330 SDIO board types */
#define BCM94330SDG_SSID	0x0528
#define BCM94330SD_FCBGABU_SSID	0x052e
#define BCM94330SD_WLBGABU_SSID	0x052f
#define BCM94330SD_FCBGA_SSID	0x0530
#define BCM94330FCSDAGB_SSID		0x0532
#define BCM94330OLYMPICAMG_SSID		0x0549
#define BCM94330OLYMPICAMGEPA_SSID		0x054F
#define BCM94330OLYMPICUNO3_SSID	0x0551
#define BCM94330WLSDAGB_SSID	0x0547
#define BCM94330CSPSDAGBB_SSID	0x054A

/* 43224 boards */
#define BCM943224X21        0x056e
#define BCM943224X21_FCC    0x00d1
#define BCM943224X21B	    0x00e9
#define BCM943224M93	    0x008b
#define BCM943224M93A	    0x0090
#define BCM943224X16	    0x0093
#define BCM94322X9		    0x008d
#define BCM94322M35e	    0x008e

/* 43228 Boards */
#define BCM943228BU8_SSID	0x0540
#define BCM943228BU9_SSID	0x0541
#define BCM943228BU_SSID	0x0542
#define BCM943227HM4L_SSID	0x0543
#define BCM943227HMB_SSID	0x0544
#define BCM943228HM4L_SSID	0x0545
#define BCM943228SD_SSID	0x0573

/* 43239 Boards */
#define BCM943239MOD_SSID	0x05ac
#define BCM943239REF_SSID	0x05aa

/* 4331 boards */
#define BCM94331X19               0x00D6	/* X19B */
#define BCM94331X28               0x00E4	/* X28 */
#define BCM94331X28B              0x010E	/* X28B */
#define BCM94331PCIEBT3Ax_SSID    BCM94331X28
#define BCM94331X12_2G_SSID       0x00EC	/* X12 2G */
#define BCM94331X12_5G_SSID       0x00ED	/* X12 5G */
#define BCM94331X29B              0x00EF	/* X29B */
#define BCM94331X29D              0x010F	/* X29D */
#define BCM94331CSAX_SSID         BCM94331X29B
#define BCM94331X19C              0x00F5	/* X19C */
#define BCM94331X33	          0x00F4	/* X33 */
#define BCM94331BU_SSID           0x0523
#define BCM94331S9BU_SSID         0x0524
#define BCM94331MC_SSID           0x0525
#define BCM94331MCI_SSID          0x0526
#define BCM94331PCIEBT4_SSID      0x0527
#define BCM94331HM_SSID           0x0574
#define BCM94331PCIEDUAL_SSID     0x059B
#define BCM94331MCH5_SSID         0x05A9
#define BCM94331CS_SSID           0x05C6
#define BCM94331CD_SSID           0x05DA

/* 4314 Boards */
#define BCM94314BU_SSID         0x05b1

/* 53572 Boards */
#define BCM953572BU_SSID       0x058D
#define BCM953572NR2_SSID      0x058E
#define BCM947188NR2_SSID      0x058F
#define BCM953572SDRNR2_SSID   0x0590

/* 43236 boards */
#define BCM943236OLYMPICSULLEY_SSID 0x594
#define BCM943236PREPROTOBLU2O3_SSID 0x5b9
#define BCM943236USBELNA_SSID 0x5f8

/* 4314 Boards */
#define BCM94314BUSDIO_SSID	0x05c8
#define BCM94314BGABU_SSID	0x05c9
#define BCM94314HMEPA_SSID	0x05ca
#define BCM94314HMEPABK_SSID	0x05cb
#define BCM94314SUHMEPA_SSID	0x05cc
#define BCM94314SUHM_SSID	0x05cd
#define BCM94314HM_SSID		0x05d1

/* 4334 Boards */
#define BCM94334FCAGBI_SSID	0x05df
#define BCM94334WLAGBI_SSID	0x05dd

/* 4335 Boards */
#define BCM94335X52             0x0114

/* 4345 Boards */
#define BCM94345_SSID           0x0687

/* 4360 Boards */
#define BCM94360X52C            0X0117
#define BCM94360X52D            0X0137
#define BCM94360X29C            0X0112
#define BCM94360X29CP2          0X0134
#define BCM94360X29CP3          0X013B
#define BCM94360X51             0x0111
#define BCM94360X51P2           0x0129
#define BCM94360X51P3           0x0142
#define BCM94360X51A            0x0135
#define BCM94360X51B            0x0136
#define BCM94360CS              0x061B
#define BCM94360J28_D11AC2G     0x0c00
#define BCM94360J28_D11AC5G     0x0c01
#define BCM94360USBH5_D11AC5G   0x06aa
#define BCM94360MCM5            0x06d8

/* 4350 Boards */
#define BCM94350X52B            0X0116
#define BCM94350X14             0X0131

/* 43217 Boards */
#define BCM943217BU_SSID	0x05d5
#define BCM943217HM2L_SSID	0x05d6
#define BCM943217HMITR2L_SSID	0x05d7

/* 43142 Boards */
#define BCM943142HM_SSID	0x05e0
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

/* 43341 Boards */
#define BCM943341WLABGS_SSID	0x062d

/* 43342 Boards */
#define BCM943342FCAGBI_SSID	0x0641

/* 43602 Boards, unclear yet what boards will be created. */
#define BCM943602RSVD1_SSID	0x06a5
#define BCM943602RSVD2_SSID	0x06a6
#define BCM943602X87            0X0133
#define BCM943602X238           0X0132

/* # of GPIO pins */
#define GPIO_NUMPINS		32

/* These values are used by dhd host driver. */
#define RDL_RAM_BASE_4319 0x60000000
#define RDL_RAM_BASE_4329 0x60000000
#define RDL_RAM_SIZE_4319 0x48000
#define RDL_RAM_SIZE_4329  0x48000
#define RDL_RAM_SIZE_43236 0x70000
#define RDL_RAM_BASE_43236 0x60000000
#define RDL_RAM_SIZE_4328 0x60000
#define RDL_RAM_BASE_4328 0x80000000
#define RDL_RAM_SIZE_4322 0x60000
#define RDL_RAM_BASE_4322 0x60000000
#define RDL_RAM_SIZE_4360  0xA0000
#define RDL_RAM_BASE_4360  0x60000000
#define RDL_RAM_SIZE_43242  0x90000
#define RDL_RAM_BASE_43242  0x60000000
#define RDL_RAM_SIZE_43143  0x70000
#define RDL_RAM_BASE_43143  0x60000000
#define RDL_RAM_SIZE_4350  0xC0000
#define RDL_RAM_BASE_4350  0x180800

/* generic defs for nvram "muxenab" bits
* Note: these differ for 4335a0. refer bcmchipc.h for specific mux options.
*/
#define MUXENAB_UART		0x00000001
#define MUXENAB_GPIO		0x00000002
#define MUXENAB_ERCX		0x00000004	/* External Radio BT coex */
#define MUXENAB_JTAG		0x00000008
#define MUXENAB_HOST_WAKE	0x00000010	/* configure GPIO for SDIO host_wake */
#define MUXENAB_I2S_EN		0x00000020
#define MUXENAB_I2S_MASTER	0x00000040
#define MUXENAB_I2S_FULL	0x00000080
#define MUXENAB_SFLASH		0x00000100
#define MUXENAB_RFSWCTRL0	0x00000200
#define MUXENAB_RFSWCTRL1	0x00000400
#define MUXENAB_RFSWCTRL2	0x00000800
#define MUXENAB_SECI		0x00001000
#define MUXENAB_BT_LEGACY	0x00002000
#define MUXENAB_HOST_WAKE1	0x00004000	/* configure alternative GPIO for SDIO host_wake */

/* Boot flags */
#define FLASH_KERNEL_NFLASH	0x00000001
#define FLASH_BOOT_NFLASH	0x00000002

#endif /* _BCMDEVS_H */
