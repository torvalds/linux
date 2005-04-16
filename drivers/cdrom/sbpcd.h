/*
 * sbpcd.h   Specify interface address and interface type here.
 */

/*
 * Attention! This file contains user-serviceable parts!
 * I recommend to make use of it...
 * If you feel helpless, look into Documentation/cdrom/sbpcd
 * (good idea anyway, at least before mailing me).
 *
 * The definitions for the first controller can get overridden by
 * the kernel command line ("lilo boot option").
 * Examples:
 *                                 sbpcd=0x300,LaserMate
 *                             or
 *                                 sbpcd=0x230,SoundBlaster
 *                             or
 *                                 sbpcd=0x338,SoundScape
 *                             or
 *                                 sbpcd=0x2C0,Teac16bit
 *
 * If sbpcd gets used as a module, you can load it with
 *     insmod sbpcd.o sbpcd=0x300,0
 * or
 *     insmod sbpcd.o sbpcd=0x230,1
 * or
 *     insmod sbpcd.o sbpcd=0x338,2
 * or
 *     insmod sbpcd.o sbpcd=0x2C0,3
 * respective to override the configured address and type.
 */

/*
 * define your CDROM port base address as CDROM_PORT
 * and specify the type of your interface card as SBPRO.
 *
 * address:
 * ========
 * SBPRO type addresses typically are 0x0230 (=0x220+0x10), 0x0250, ...
 * LASERMATE type (CI-101P, WDH-7001C) addresses typically are 0x0300, ...
 * SOUNDSCAPE addresses are from the LASERMATE type and range. You have to
 * specify the REAL address here, not the configuration port address. Look
 * at the CDROM driver's invoking line within your DOS CONFIG.SYS, or let
 * sbpcd auto-probe, if you are not firm with the address.
 * There are some soundcards on the market with 0x0630, 0x0650, ...; their
 * type is not obvious (both types are possible).
 *
 * example: if your SBPRO audio address is 0x220, specify 0x230 and SBPRO 1.
 *          if your soundcard has its CDROM port above 0x300, specify
 *          that address and try SBPRO 0 first.
 *          if your SoundScape configuration port is at 0x330, specify
 *          0x338 and SBPRO 2.
 *
 * interface type:
 * ===============
 * set SBPRO to 1 for "true" SoundBlaster card
 * set SBPRO to 0 for "compatible" soundcards and
 *                for "poor" (no sound) interface cards.
 * set SBPRO to 2 for Ensonic SoundScape or SPEA Media FX cards
 * set SBPRO to 3 for Teac 16bit interface cards
 *
 * Almost all "compatible" sound boards need to set SBPRO to 0.
 * If SBPRO is set wrong, the drives will get found - but any
 * data access will give errors (audio access will work).
 * The "OmniCD" no-sound interface card from CreativeLabs and most Teac
 * interface cards need SBPRO 1.
 *
 * sound base:
 * ===========
 * The SOUND_BASE definition tells if we should try to turn the CD sound
 * channels on. It will only be of use regarding soundcards with a SbPro
 * compatible mixer.
 *
 * Example: #define SOUND_BASE 0x220 enables the sound card's CD channels
 *          #define SOUND_BASE 0     leaves the soundcard untouched
 */
#define CDROM_PORT 0x340 /* <-----------<< port address                      */
#define SBPRO      0     /* <-----------<< interface type                    */
#define MAX_DRIVES 4     /* set to 1 if the card does not use "drive select" */
#define SOUND_BASE 0x220 /* <-----------<< sound address of this card or 0   */

/*
 * some more or less user dependent definitions - service them!
 */

/* Set this to 0 once you have configured your interface definitions right. */
#define DISTRIBUTION 1

/*
 * Time to wait after giving a message.
 * This gets important if you enable non-standard DBG_xxx flags.
 * You will see what happens if you omit the pause or make it
 * too short. Be warned!
 */
#define KLOGD_PAUSE 1

/* tray control: eject tray if no disk is in */
#if DISTRIBUTION
#define JUKEBOX 0
#else
#define JUKEBOX 1
#endif /* DISTRIBUTION */

/* tray control: eject tray after last use */
#if DISTRIBUTION
#define EJECT 0
#else
#define EJECT 1
#endif /* DISTRIBUTION */

/* max. number of audio frames to read with one     */
/* request (allocates n* 2352 bytes kernel memory!) */
/* may be freely adjusted, f.e. 75 (= 1 sec.), at   */
/* runtime by use of the CDROMAUDIOBUFSIZ ioctl.    */
#define READ_AUDIO 0

/* Optimizations for the Teac CD-55A drive read performance.
 * SBP_TEAC_SPEED can be changed here, or one can set the 
 * variable "teac" when loading as a module.
 * Valid settings are:
 *   0 - very slow - the recommended "DISTRIBUTION 1" setup.
 *   1 - 2x performance with little overhead. No busy waiting.
 *   2 - 4x performance with 5ms overhead per read. Busy wait.
 *
 * Setting SBP_TEAC_SPEED or the variable 'teac' to anything
 * other than 0 may cause problems. If you run into them, first
 * change SBP_TEAC_SPEED back to 0 and see if your drive responds
 * normally. If yes, you are "allowed" to report your case - to help
 * me with the driver, not to solve your hassle. Don´t mail if you
 * simply are stuck into your own "tuning" experiments, you know?
 */
#define SBP_TEAC_SPEED 1

/*==========================================================================*/
/*==========================================================================*/
/*
 * nothing to change below here if you are not fully aware what you're doing
 */
#ifndef _LINUX_SBPCD_H

#define _LINUX_SBPCD_H
/*==========================================================================*/
/*==========================================================================*/
/*
 * driver's own read_ahead, data mode
 */
#define SBP_BUFFER_FRAMES 8 

#define LONG_TIMING 0 /* test against timeouts with "gold" CDs on CR-521 */
#undef  FUTURE
#undef SAFE_MIXED

#define TEST_UPC 0
#define SPEA_TEST 0
#define TEST_STI 0
#define OLD_BUSY 0
#undef PATH_CHECK
#ifndef SOUND_BASE
#define SOUND_BASE 0
#endif
#if DISTRIBUTION
#undef SBP_TEAC_SPEED
#define SBP_TEAC_SPEED 0
#endif
/*==========================================================================*/
/*
 * DDI interface definitions
 * "invented" by Fred N. van Kempen..
 */
#define DDIOCSDBG	0x9000

/*==========================================================================*/
/*
 * "private" IOCTL functions
 */
#define CDROMAUDIOBUFSIZ	0x5382 /* set the audio buffer size */

/*==========================================================================*/
/*
 * Debug output levels
 */
#define DBG_INF	1	/* necessary information */
#define DBG_BSZ	2	/* BLOCK_SIZE trace */
#define DBG_REA	3	/* READ status trace */
#define DBG_CHK	4	/* MEDIA CHECK trace */
#define DBG_TIM	5	/* datarate timer test */
#define DBG_INI	6	/* initialization trace */
#define DBG_TOC	7	/* tell TocEntry values */
#define DBG_IOC	8	/* ioctl trace */
#define DBG_STA	9	/* ResponseStatus() trace */
#define DBG_ERR	10	/* cc_ReadError() trace */
#define DBG_CMD	11	/* cmd_out() trace */
#define DBG_WRN	12	/* give explanation before auto-probing */
#define DBG_MUL	13	/* multi session code test */
#define DBG_IDX	14	/* test code for drive_id !=0 */
#define DBG_IOX	15	/* some special information */
#define DBG_DID	16	/* drive ID test */
#define DBG_RES	17	/* drive reset info */
#define DBG_SPI	18	/* SpinUp test */
#define DBG_IOS	19	/* ioctl trace: subchannel functions */
#define DBG_IO2	20	/* ioctl trace: general */
#define DBG_UPC	21	/* show UPC information */
#define DBG_XA1	22	/* XA mode debugging */
#define DBG_LCK	23	/* door (un)lock info */
#define DBG_SQ1	24	/* dump SubQ frame */
#define DBG_AUD	25	/* READ AUDIO debugging */
#define DBG_SEQ	26	/* Sequoia interface configuration trace */
#define DBG_LCS	27	/* Longshine LCS-7260 debugging trace */
#define DBG_CD2	28	/* MKE/Funai CD200 debugging trace */
#define DBG_TEA	29	/* TEAC CD-55A debugging trace */
#define DBG_ECS	30	/* ECS-AT (Vertos 100) debugging trace */
#define DBG_000	31	/* unnecessary information */

/*==========================================================================*/
/*==========================================================================*/

/*
 * bits of flags_cmd_out:
 */
#define f_respo3		0x100
#define f_putcmd		0x80
#define f_respo2		0x40
#define f_lopsta		0x20
#define f_getsta		0x10
#define f_ResponseStatus	0x08
#define f_obey_p_check		0x04
#define f_bit1			0x02
#define f_wait_if_busy		0x01

/*
 * diskstate_flags:
 */
#define x80_bit			0x80
#define upc_bit			0x40
#define volume_bit		0x20
#define toc_bit			0x10
#define multisession_bit	0x08
#define cd_size_bit		0x04
#define subq_bit		0x02
#define frame_size_bit		0x01

/*
 * disk states (bits of diskstate_flags):
 */
#define upc_valid		(current_drive->diskstate_flags&upc_bit)
#define volume_valid		(current_drive->diskstate_flags&volume_bit)
#define toc_valid		(current_drive->diskstate_flags&toc_bit)
#define cd_size_valid		(current_drive->diskstate_flags&cd_size_bit)
#define subq_valid		(current_drive->diskstate_flags&subq_bit)
#define frame_size_valid	(current_drive->diskstate_flags&frame_size_bit)

/*
 * the status_bits variable
 */
#define p_success	0x100
#define p_door_closed	0x80
#define p_caddy_in	0x40
#define p_spinning	0x20
#define p_check		0x10
#define p_busy_new	0x08
#define p_door_locked	0x04
#define p_disk_ok	0x01

/*
 * LCS-7260 special status result bits:
 */
#define p_lcs_door_locked	0x02
#define p_lcs_door_closed	0x01 /* probably disk_in */

/*
 * CR-52x special status result bits:
 */
#define p_caddin_old	0x40
#define p_success_old	0x08
#define p_busy_old	0x04
#define p_bit_1		0x02	/* hopefully unused now */

/*
 * "generation specific" defs of the status result bits:
 */
#define p0_door_closed	0x80
#define p0_caddy_in	0x40
#define p0_spinning	0x20
#define p0_check	0x10
#define p0_success	0x08 /* unused */
#define p0_busy		0x04
#define p0_bit_1	0x02 /* unused */
#define p0_disk_ok	0x01

#define pL_disk_in	0x40
#define pL_spinning	0x20
#define pL_check	0x10
#define pL_success	0x08 /* unused ?? */
#define pL_busy		0x04
#define pL_door_locked	0x02
#define pL_door_closed	0x01

#define pV_door_closed	0x40
#define pV_spinning	0x20
#define pV_check	0x10
#define pV_success	0x08
#define pV_busy		0x04
#define pV_door_locked	0x02
#define pV_disk_ok	0x01

#define p1_door_closed	0x80
#define p1_disk_in	0x40
#define p1_spinning	0x20
#define p1_check	0x10
#define p1_busy		0x08
#define p1_door_locked	0x04
#define p1_bit_1	0x02 /* unused */
#define p1_disk_ok	0x01

#define p2_disk_ok	0x80
#define p2_door_locked	0x40
#define p2_spinning	0x20
#define p2_busy2	0x10
#define p2_busy1	0x08
#define p2_door_closed	0x04
#define p2_disk_in	0x02
#define p2_check	0x01

/*
 * used drive states:
 */
#define st_door_closed	(current_drive->status_bits&p_door_closed)
#define st_caddy_in	(current_drive->status_bits&p_caddy_in)
#define st_spinning	(current_drive->status_bits&p_spinning)
#define st_check	(current_drive->status_bits&p_check)
#define st_busy		(current_drive->status_bits&p_busy_new)
#define st_door_locked	(current_drive->status_bits&p_door_locked)
#define st_diskok	(current_drive->status_bits&p_disk_ok)

/*
 * bits of the CDi_status register:
 */
#define s_not_result_ready	0x04 /* 0: "result ready" */
#define s_not_data_ready	0x02 /* 0: "data ready"   */
#define s_attention		0x01 /* 1: "attention required" */
/*
 * usable as:
 */
#define DRV_ATTN	((inb(CDi_status)&s_attention)!=0)
#define DATA_READY	((inb(CDi_status)&s_not_data_ready)==0)
#define RESULT_READY	((inb(CDi_status)&s_not_result_ready)==0)

/*
 * drive families and types (firmware versions):
 */
#define drv_fam0	0x0100		/* CR-52x family */
#define drv_199		(drv_fam0+0x01)	/* <200 */
#define drv_200		(drv_fam0+0x02)	/* <201 */
#define drv_201		(drv_fam0+0x03)	/* <210 */
#define drv_210		(drv_fam0+0x04)	/* <211 */
#define drv_211		(drv_fam0+0x05)	/* <300 */
#define drv_300		(drv_fam0+0x06)	/* >=300 */

#define drv_fam1	0x0200		/* CR-56x family */
#define drv_099		(drv_fam1+0x01)	/* <100 */
#define drv_100		(drv_fam1+0x02)	/* >=100, only 1.02 and 5.00 known */

#define drv_fam2	0x0400		/* CD200 family */

#define drv_famT	0x0800		/* TEAC CD-55A */

#define drv_famL	0x1000		/* Longshine family */
#define drv_260		(drv_famL+0x01)	/* LCS-7260 */
#define drv_e1		(drv_famL+0x01)	/* LCS-7260, firmware "A E1" */
#define drv_f4		(drv_famL+0x02)	/* LCS-7260, firmware "A4F4" */

#define drv_famV	0x2000		/* ECS-AT (vertos-100) family */
#define drv_at		(drv_famV+0x01)	/* ECS-AT, firmware "1.00" */

#define fam0_drive	(current_drive->drv_type&drv_fam0)
#define famL_drive	(current_drive->drv_type&drv_famL)
#define famV_drive	(current_drive->drv_type&drv_famV)
#define fam1_drive	(current_drive->drv_type&drv_fam1)
#define fam2_drive	(current_drive->drv_type&drv_fam2)
#define famT_drive	(current_drive->drv_type&drv_famT)
#define fam0L_drive	(current_drive->drv_type&(drv_fam0|drv_famL))
#define fam0V_drive	(current_drive->drv_type&(drv_fam0|drv_famV))
#define famLV_drive	(current_drive->drv_type&(drv_famL|drv_famV))
#define fam0LV_drive	(current_drive->drv_type&(drv_fam0|drv_famL|drv_famV))
#define fam1L_drive	(current_drive->drv_type&(drv_fam1|drv_famL))
#define fam1V_drive	(current_drive->drv_type&(drv_fam1|drv_famV))
#define fam1LV_drive	(current_drive->drv_type&(drv_fam1|drv_famL|drv_famV))
#define fam01_drive	(current_drive->drv_type&(drv_fam0|drv_fam1))
#define fam12_drive	(current_drive->drv_type&(drv_fam1|drv_fam2))
#define fam2T_drive	(current_drive->drv_type&(drv_fam2|drv_famT))

/*
 * audio states:
 */
#define audio_completed	3 /* Forgot this one! --AJK */
#define audio_playing	2
#define audio_pausing	1

/*
 * drv_pattern, drv_options:
 */
#define speed_auto	0x80
#define speed_300	0x40
#define speed_150	0x20
#define audio_mono	0x04

/*
 * values of cmd_type (0 else):
 */
#define READ_M1	0x01	/* "data mode 1": 2048 bytes per frame */
#define READ_M2	0x02	/* "data mode 2": 12+2048+280 bytes per frame */
#define READ_SC	0x04	/* "subchannel info": 96 bytes per frame */
#define READ_AU	0x08	/* "audio frame": 2352 bytes per frame */

/*
 * sense_byte:
 *
 *          values: 00
 *                  01
 *                  81
 *                  82 "raw audio" mode
 *                  xx from infobuf[0] after 85 00 00 00 00 00 00
 */

/* audio status (bin) */
#define aud_00 0x00 /* Audio status byte not supported or not valid */
#define audx11 0x0b /* Audio play operation in progress             */
#define audx12 0x0c /* Audio play operation paused                  */
#define audx13 0x0d /* Audio play operation successfully completed  */
#define audx14 0x0e /* Audio play operation stopped due to error    */
#define audx15 0x0f /* No current audio status to return            */
/* audio status (bcd) */
#define aud_11 0x11 /* Audio play operation in progress             */
#define aud_12 0x12 /* Audio play operation paused                  */
#define aud_13 0x13 /* Audio play operation successfully completed  */
#define aud_14 0x14 /* Audio play operation stopped due to error    */
#define aud_15 0x15 /* No current audio status to return            */

/*
 * highest allowed drive number (MINOR+1)
 */
#define NR_SBPCD	4

/*
 * we try to never disable interrupts - seems to work
 */
#define SBPCD_DIS_IRQ	0

/*
 * "write byte to port"
 */
#define OUT(x,y)	outb(y,x)

/*==========================================================================*/

#define MIXER_addr SOUND_BASE+4 /* sound card's address register */
#define MIXER_data SOUND_BASE+5 /* sound card's data register */
#define MIXER_CD_Volume	0x28	/* internal SB Pro register address */

/*==========================================================================*/

#define MAX_TRACKS	99

#define ERR_DISKCHANGE 615

/*==========================================================================*/
/*
 * To make conversions easier (machine dependent!)
 */
typedef union _msf
{
	u_int n;
	u_char c[4];
} MSF;

typedef union _blk
{
	u_int n;
	u_char c[4];
} BLK;

/*==========================================================================*/

/*============================================================================
==============================================================================

COMMAND SET of "old" drives like CR-521, CR-522
               (the CR-562 family is different):

No.	Command			       Code
--------------------------------------------

Drive Commands:
 1	Seek				01	
 2	Read Data			02
 3	Read XA-Data			03
 4	Read Header			04
 5	Spin Up				05
 6	Spin Down			06
 7	Diagnostic			07
 8	Read UPC			08
 9	Read ISRC			09
10	Play Audio			0A
11	Play Audio MSF			0B
12	Play Audio Track/Index		0C

Status Commands:
13	Read Status			81	
14	Read Error			82
15	Read Drive Version		83
16	Mode Select			84
17	Mode Sense			85
18	Set XA Parameter		86
19	Read XA Parameter		87
20	Read Capacity			88
21	Read SUB_Q			89
22	Read Disc Code			8A
23	Read Disc Information		8B
24	Read TOC			8C
25	Pause/Resume			8D
26	Read Packet			8E
27	Read Path Check			00
 
 
all numbers (lba, msf-bin, msf-bcd, counts) to transfer high byte first

mnemo     7-byte command        #bytes response (r0...rn)
________ ____________________  ____ 

Read Status:
status:  81.                    (1)  one-byte command, gives the main
                                                          status byte
Read Error:
check1:  82 00 00 00 00 00 00.  (6)  r1: audio status

Read Packet:
check2:  8e xx 00 00 00 00 00. (xx)  gets xx bytes response, relating
                                        to commands 01 04 05 07 08 09

Play Audio:
play:    0a ll-bb-aa nn-nn-nn.  (0)  play audio, ll-bb-aa: starting block (lba),
                                                 nn-nn-nn: #blocks
Play Audio MSF:
         0b mm-ss-ff mm-ss-ff   (0)  play audio from/to

Play Audio Track/Index:
         0c ...

Pause/Resume:
pause:   8d pr 00 00 00 00 00.  (0)  pause (pr=00) 
                                     resume (pr=80) audio playing

Mode Select:
         84 00 nn-nn ??.?? 00   (0)  nn-nn: 2048 or 2340
                                     possibly defines transfer size

set_vol: 84 83 00 00 sw le 00.  (0)  sw(itch): lrxxxxxx (off=1)
                                     le(vel): min=0, max=FF, else half
				     (firmware 2.11)

Mode Sense:
get_vol: 85 03 00 00 00 00 00.  (2)  tell current audio volume setting

Read Disc Information:
tocdesc: 8b 00 00 00 00 00 00.  (6)  read the toc descriptor ("msf-bin"-format)

Read TOC:
tocent:  8c fl nn 00 00 00 00.  (8)  read toc entry #nn
                                       (fl=0:"lba"-, =2:"msf-bin"-format)

Read Capacity:
capacit: 88 00 00 00 00 00 00.  (5)  "read CD-ROM capacity"


Read Path Check:
ping:    00 00 00 00 00 00 00.  (2)  r0=AA, r1=55
                                     ("ping" if the drive is connected)

Read Drive Version:
ident:   83 00 00 00 00 00 00. (12)  gives "MATSHITAn.nn" 
                                     (n.nn = 2.01, 2.11., 3.00, ...)

Seek:
seek:    01 00 ll-bb-aa 00 00.  (0)  
seek:    01 02 mm-ss-ff 00 00.  (0)  

Read Data:
read:    02 xx-xx-xx nn-nn fl.  (?)  read nn-nn blocks of 2048 bytes,
                                     starting at block xx-xx-xx  
                                     fl=0: "lba"-, =2:"msf-bcd"-coded xx-xx-xx

Read XA-Data:
read:    03 xx-xx-xx nn-nn fl.  (?)  read nn-nn blocks of 2340 bytes, 
                                     starting at block xx-xx-xx
                                     fl=0: "lba"-, =2:"msf-bcd"-coded xx-xx-xx

Read SUB_Q:
         89 fl 00 00 00 00 00. (13)  r0: audio status, r4-r7: lba/msf, 
                                       fl=0: "lba", fl=2: "msf"

Read Disc Code:
         8a 00 00 00 00 00 00. (14)  possibly extended "check condition"-info

Read Header:
         04 00 ll-bb-aa 00 00.  (0)   4 bytes response with "check2"
         04 02 mm-ss-ff 00 00.  (0)   4 bytes response with "check2"

Spin Up:
         05 00 ll-bb-aa 00 00.  (0)  possibly implies a "seek"

Spin Down:
         06 ...

Diagnostic:
         07 00 ll-bb-aa 00 00.  (2)   2 bytes response with "check2"
         07 02 mm-ss-ff 00 00.  (2)   2 bytes response with "check2"

Read UPC:
         08 00 ll-bb-aa 00 00. (16)  
         08 02 mm-ss-ff 00 00. (16)  

Read ISRC:
         09 00 ll-bb-aa 00 00. (15)  15 bytes response with "check2"
         09 02 mm-ss-ff 00 00. (15)  15 bytes response with "check2"

Set XA Parameter:
         86 ...

Read XA Parameter:
         87 ...

==============================================================================
============================================================================*/

/*
 * commands
 *
 * CR-52x:      CMD0_
 * CR-56x:      CMD1_
 * CD200:       CMD2_
 * LCS-7260:    CMDL_
 * TEAC CD-55A: CMDT_
 * ECS-AT:      CMDV_
 */
#define CMD1_RESET	0x0a
#define CMD2_RESET	0x01
#define CMDT_RESET	0xc0

#define CMD1_LOCK_CTL	0x0c
#define CMD2_LOCK_CTL	0x1e
#define CMDT_LOCK_CTL	CMD2_LOCK_CTL
#define CMDL_LOCK_CTL	0x0e
#define CMDV_LOCK_CTL	CMDL_LOCK_CTL

#define CMD1_TRAY_CTL	0x07
#define CMD2_TRAY_CTL	0x1b
#define CMDT_TRAY_CTL	CMD2_TRAY_CTL
#define CMDL_TRAY_CTL	0x0d
#define CMDV_TRAY_CTL	CMDL_TRAY_CTL

#define CMD1_MULTISESS	0x8d
#define CMDL_MULTISESS	0x8c
#define CMDV_MULTISESS	CMDL_MULTISESS

#define CMD1_SUBCHANINF	0x11
#define CMD2_SUBCHANINF	0x??

#define CMD1_ABORT	0x08
#define CMD2_ABORT	0x08
#define CMDT_ABORT	0x08

#define CMD2_x02	0x02

#define CMD2_SETSPEED	0xda

#define CMD0_PATH_CHECK	0x00
#define CMD1_PATH_CHECK	0x???
#define CMD2_PATH_CHECK	0x???
#define CMDT_PATH_CHECK	0x???
#define CMDL_PATH_CHECK	CMD0_PATH_CHECK
#define CMDV_PATH_CHECK	CMD0_PATH_CHECK

#define CMD0_SEEK	0x01
#define CMD1_SEEK	CMD0_SEEK
#define CMD2_SEEK	0x2b
#define CMDT_SEEK	CMD2_SEEK
#define CMDL_SEEK	CMD0_SEEK
#define CMDV_SEEK	CMD0_SEEK

#define CMD0_READ	0x02
#define CMD1_READ	0x10
#define CMD2_READ	0x28
#define CMDT_READ	CMD2_READ
#define CMDL_READ	CMD0_READ
#define CMDV_READ	CMD0_READ

#define CMD0_READ_XA	0x03
#define CMD2_READ_XA	0xd4
#define CMD2_READ_XA2	0xd5
#define CMDL_READ_XA	CMD0_READ_XA /* really ?? */
#define CMDV_READ_XA	CMD0_READ_XA

#define CMD0_READ_HEAD	0x04

#define CMD0_SPINUP	0x05
#define CMD1_SPINUP	0x02
#define CMD2_SPINUP	CMD2_TRAY_CTL
#define CMDL_SPINUP	CMD0_SPINUP
#define CMDV_SPINUP	CMD0_SPINUP

#define CMD0_SPINDOWN	0x06 /* really??? */
#define CMD1_SPINDOWN	0x06
#define CMD2_SPINDOWN	CMD2_TRAY_CTL
#define CMDL_SPINDOWN	0x0d
#define CMDV_SPINDOWN	CMD0_SPINDOWN

#define CMD0_DIAG	0x07

#define CMD0_READ_UPC	0x08
#define CMD1_READ_UPC	0x88
#define CMD2_READ_UPC	0x???
#define CMDL_READ_UPC	CMD0_READ_UPC
#define CMDV_READ_UPC	0x8f

#define CMD0_READ_ISRC	0x09

#define CMD0_PLAY	0x0a
#define CMD1_PLAY	0x???
#define CMD2_PLAY	0x???
#define CMDL_PLAY	CMD0_PLAY
#define CMDV_PLAY	CMD0_PLAY

#define CMD0_PLAY_MSF	0x0b
#define CMD1_PLAY_MSF	0x0e
#define CMD2_PLAY_MSF	0x47
#define CMDT_PLAY_MSF	CMD2_PLAY_MSF
#define CMDL_PLAY_MSF	0x???

#define CMD0_PLAY_TI	0x0c
#define CMD1_PLAY_TI	0x0f

#define CMD0_STATUS	0x81
#define CMD1_STATUS	0x05
#define CMD2_STATUS	0x00
#define CMDT_STATUS	CMD2_STATUS
#define CMDL_STATUS	CMD0_STATUS
#define CMDV_STATUS	CMD0_STATUS
#define CMD2_SEEK_LEADIN 0x00

#define CMD0_READ_ERR	0x82
#define CMD1_READ_ERR	CMD0_READ_ERR
#define CMD2_READ_ERR	0x03
#define CMDT_READ_ERR	CMD2_READ_ERR /* get audio status */
#define CMDL_READ_ERR	CMD0_READ_ERR
#define CMDV_READ_ERR	CMD0_READ_ERR

#define CMD0_READ_VER	0x83
#define CMD1_READ_VER	CMD0_READ_VER
#define CMD2_READ_VER	0x12
#define CMDT_READ_VER	CMD2_READ_VER /* really ?? */
#define CMDL_READ_VER	CMD0_READ_VER
#define CMDV_READ_VER	CMD0_READ_VER

#define CMD0_SETMODE	0x84
#define CMD1_SETMODE	0x09
#define CMD2_SETMODE	0x55
#define CMDT_SETMODE	CMD2_SETMODE
#define CMDL_SETMODE	CMD0_SETMODE

#define CMD0_GETMODE	0x85
#define CMD1_GETMODE	0x84
#define CMD2_GETMODE	0x5a
#define CMDT_GETMODE	CMD2_GETMODE
#define CMDL_GETMODE	CMD0_GETMODE

#define CMD0_SET_XA	0x86

#define CMD0_GET_XA	0x87

#define CMD0_CAPACITY	0x88
#define CMD1_CAPACITY	0x85
#define CMD2_CAPACITY	0x25
#define CMDL_CAPACITY	CMD0_CAPACITY /* missing in some firmware versions */

#define CMD0_READSUBQ	0x89
#define CMD1_READSUBQ	0x87
#define CMD2_READSUBQ	0x42
#define CMDT_READSUBQ	CMD2_READSUBQ
#define CMDL_READSUBQ	CMD0_READSUBQ
#define CMDV_READSUBQ	CMD0_READSUBQ

#define CMD0_DISKCODE	0x8a

#define CMD0_DISKINFO	0x8b
#define CMD1_DISKINFO	CMD0_DISKINFO
#define CMD2_DISKINFO	0x43
#define CMDT_DISKINFO	CMD2_DISKINFO
#define CMDL_DISKINFO	CMD0_DISKINFO
#define CMDV_DISKINFO	CMD0_DISKINFO

#define CMD0_READTOC	0x8c
#define CMD1_READTOC	CMD0_READTOC
#define CMD2_READTOC	0x???
#define CMDL_READTOC	CMD0_READTOC
#define CMDV_READTOC	CMD0_READTOC

#define CMD0_PAU_RES	0x8d
#define CMD1_PAU_RES	0x0d
#define CMD2_PAU_RES	0x4b
#define CMDT_PAUSE	CMD2_PAU_RES
#define CMDL_PAU_RES	CMD0_PAU_RES
#define CMDV_PAUSE	CMD0_PAU_RES

#define CMD0_PACKET	0x8e
#define CMD1_PACKET	CMD0_PACKET
#define CMD2_PACKET	0x???
#define CMDL_PACKET	CMD0_PACKET
#define CMDV_PACKET	0x???

/*==========================================================================*/
/*==========================================================================*/
#endif /* _LINUX_SBPCD_H */
/*==========================================================================*/
/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file. 
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * End:
 */
