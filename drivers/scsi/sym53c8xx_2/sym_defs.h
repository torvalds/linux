/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef SYM_DEFS_H
#define SYM_DEFS_H

#define SYM_VERSION "2.2.1"
#define SYM_DRIVER_NAME	"sym-" SYM_VERSION

/*
 *	SYM53C8XX device features descriptor.
 */
struct sym_chip {
	u_short	device_id;
	u_short	revision_id;
	char	*name;
	u_char	burst_max;	/* log-base-2 of max burst */
	u_char	offset_max;
	u_char	nr_divisor;
	u_char	lp_probe_bit;
	u_int	features;
#define FE_LED0		(1<<0)
#define FE_WIDE		(1<<1)    /* Wide data transfers */
#define FE_ULTRA	(1<<2)	  /* Ultra speed 20Mtrans/sec */
#define FE_ULTRA2	(1<<3)	  /* Ultra 2 - 40 Mtrans/sec */
#define FE_DBLR		(1<<4)	  /* Clock doubler present */
#define FE_QUAD		(1<<5)	  /* Clock quadrupler present */
#define FE_ERL		(1<<6)    /* Enable read line */
#define FE_CLSE		(1<<7)    /* Cache line size enable */
#define FE_WRIE		(1<<8)    /* Write & Invalidate enable */
#define FE_ERMP		(1<<9)    /* Enable read multiple */
#define FE_BOF		(1<<10)   /* Burst opcode fetch */
#define FE_DFS		(1<<11)   /* DMA fifo size */
#define FE_PFEN		(1<<12)   /* Prefetch enable */
#define FE_LDSTR	(1<<13)   /* Load/Store supported */
#define FE_RAM		(1<<14)   /* On chip RAM present */
#define FE_VARCLK	(1<<15)   /* Clock frequency may vary */
#define FE_RAM8K	(1<<16)   /* On chip RAM sized 8Kb */
#define FE_64BIT	(1<<17)   /* 64-bit PCI BUS interface */
#define FE_IO256	(1<<18)   /* Requires full 256 bytes in PCI space */
#define FE_NOPM		(1<<19)   /* Scripts handles phase mismatch */
#define FE_LEDC		(1<<20)   /* Hardware control of LED */
#define FE_ULTRA3	(1<<21)	  /* Ultra 3 - 80 Mtrans/sec DT */
#define FE_66MHZ	(1<<22)	  /* 66MHz PCI support */
#define FE_CRC		(1<<23)	  /* CRC support */
#define FE_DIFF		(1<<24)	  /* SCSI HVD support */
#define FE_DFBC		(1<<25)	  /* Have DFBC register */
#define FE_LCKFRQ	(1<<26)	  /* Have LCKFRQ */
#define FE_C10		(1<<27)	  /* Various C10 core (mis)features */
#define FE_U3EN		(1<<28)	  /* U3EN bit usable */
#define FE_DAC		(1<<29)	  /* Support PCI DAC (64 bit addressing) */
#define FE_ISTAT1 	(1<<30)   /* Have ISTAT1, MBOX0, MBOX1 registers */

#define FE_CACHE_SET	(FE_ERL|FE_CLSE|FE_WRIE|FE_ERMP)
#define FE_CACHE0_SET	(FE_CACHE_SET & ~FE_ERL)
};

/*
 *	SYM53C8XX IO register data structure.
 */
struct sym_reg {
/*00*/  u8	nc_scntl0;	/* full arb., ena parity, par->ATN  */

/*01*/  u8	nc_scntl1;	/* no reset                         */
        #define   ISCON   0x10  /* connected to scsi		    */
        #define   CRST    0x08  /* force reset                      */
        #define   IARB    0x02  /* immediate arbitration            */

/*02*/  u8	nc_scntl2;	/* no disconnect expected           */
	#define   SDU     0x80  /* cmd: disconnect will raise error */
	#define   CHM     0x40  /* sta: chained mode                */
	#define   WSS     0x08  /* sta: wide scsi send           [W]*/
	#define   WSR     0x01  /* sta: wide scsi received       [W]*/

/*03*/  u8	nc_scntl3;	/* cnf system clock dependent       */
	#define   EWS     0x08  /* cmd: enable wide scsi         [W]*/
	#define   ULTRA   0x80  /* cmd: ULTRA enable                */
				/* bits 0-2, 7 rsvd for C1010       */

/*04*/  u8	nc_scid;	/* cnf host adapter scsi address    */
	#define   RRE     0x40  /* r/w:e enable response to resel.  */
	#define   SRE     0x20  /* r/w:e enable response to select  */

/*05*/  u8	nc_sxfer;	/* ### Sync speed and count         */
				/* bits 6-7 rsvd for C1010          */

/*06*/  u8	nc_sdid;	/* ### Destination-ID               */

/*07*/  u8	nc_gpreg;	/* ??? IO-Pins                      */

/*08*/  u8	nc_sfbr;	/* ### First byte received          */

/*09*/  u8	nc_socl;
	#define   CREQ	  0x80	/* r/w: SCSI-REQ                    */
	#define   CACK	  0x40	/* r/w: SCSI-ACK                    */
	#define   CBSY	  0x20	/* r/w: SCSI-BSY                    */
	#define   CSEL	  0x10	/* r/w: SCSI-SEL                    */
	#define   CATN	  0x08	/* r/w: SCSI-ATN                    */
	#define   CMSG	  0x04	/* r/w: SCSI-MSG                    */
	#define   CC_D	  0x02	/* r/w: SCSI-C_D                    */
	#define   CI_O	  0x01	/* r/w: SCSI-I_O                    */

/*0a*/  u8	nc_ssid;

/*0b*/  u8	nc_sbcl;

/*0c*/  u8	nc_dstat;
        #define   DFE     0x80  /* sta: dma fifo empty              */
        #define   MDPE    0x40  /* int: master data parity error    */
        #define   BF      0x20  /* int: script: bus fault           */
        #define   ABRT    0x10  /* int: script: command aborted     */
        #define   SSI     0x08  /* int: script: single step         */
        #define   SIR     0x04  /* int: script: interrupt instruct. */
        #define   IID     0x01  /* int: script: illegal instruct.   */

/*0d*/  u8	nc_sstat0;
        #define   ILF     0x80  /* sta: data in SIDL register lsb   */
        #define   ORF     0x40  /* sta: data in SODR register lsb   */
        #define   OLF     0x20  /* sta: data in SODL register lsb   */
        #define   AIP     0x10  /* sta: arbitration in progress     */
        #define   LOA     0x08  /* sta: arbitration lost            */
        #define   WOA     0x04  /* sta: arbitration won             */
        #define   IRST    0x02  /* sta: scsi reset signal           */
        #define   SDP     0x01  /* sta: scsi parity signal          */

/*0e*/  u8	nc_sstat1;
	#define   FF3210  0xf0	/* sta: bytes in the scsi fifo      */

/*0f*/  u8	nc_sstat2;
        #define   ILF1    0x80  /* sta: data in SIDL register msb[W]*/
        #define   ORF1    0x40  /* sta: data in SODR register msb[W]*/
        #define   OLF1    0x20  /* sta: data in SODL register msb[W]*/
        #define   DM      0x04  /* sta: DIFFSENS mismatch (895/6 only) */
        #define   LDSC    0x02  /* sta: disconnect & reconnect      */

/*10*/  u8	nc_dsa;		/* --> Base page                    */
/*11*/  u8	nc_dsa1;
/*12*/  u8	nc_dsa2;
/*13*/  u8	nc_dsa3;

/*14*/  u8	nc_istat;	/* --> Main Command and status      */
        #define   CABRT   0x80  /* cmd: abort current operation     */
        #define   SRST    0x40  /* mod: reset chip                  */
        #define   SIGP    0x20  /* r/w: message from host to script */
        #define   SEM     0x10  /* r/w: message between host + script  */
        #define   CON     0x08  /* sta: connected to scsi           */
        #define   INTF    0x04  /* sta: int on the fly (reset by wr)*/
        #define   SIP     0x02  /* sta: scsi-interrupt              */
        #define   DIP     0x01  /* sta: host/script interrupt       */

/*15*/  u8	nc_istat1;	/* 896 only */
        #define   FLSH    0x04  /* sta: chip is flushing            */
        #define   SCRUN   0x02  /* sta: scripts are running         */
        #define   SIRQD   0x01  /* r/w: disable INT pin             */

/*16*/  u8	nc_mbox0;	/* 896 only */
/*17*/  u8	nc_mbox1;	/* 896 only */

/*18*/	u8	nc_ctest0;
/*19*/  u8	nc_ctest1;

/*1a*/  u8	nc_ctest2;
	#define   CSIGP   0x40
				/* bits 0-2,7 rsvd for C1010        */

/*1b*/  u8	nc_ctest3;
	#define   FLF     0x08  /* cmd: flush dma fifo              */
	#define   CLF	  0x04	/* cmd: clear dma fifo		    */
	#define   FM      0x02  /* mod: fetch pin mode              */
	#define   WRIE    0x01  /* mod: write and invalidate enable */
				/* bits 4-7 rsvd for C1010          */

/*1c*/  u32	nc_temp;	/* ### Temporary stack              */

/*20*/	u8	nc_dfifo;
/*21*/  u8	nc_ctest4;
	#define   BDIS    0x80  /* mod: burst disable               */
	#define   MPEE    0x08  /* mod: master parity error enable  */

/*22*/  u8	nc_ctest5;
	#define   DFS     0x20  /* mod: dma fifo size               */
				/* bits 0-1, 3-7 rsvd for C1010     */

/*23*/  u8	nc_ctest6;

/*24*/  u32	nc_dbc;		/* ### Byte count and command       */
/*28*/  u32	nc_dnad;	/* ### Next command register        */
/*2c*/  u32	nc_dsp;		/* --> Script Pointer               */
/*30*/  u32	nc_dsps;	/* --> Script pointer save/opcode#2 */

/*34*/  u8	nc_scratcha;	/* Temporary register a            */
/*35*/  u8	nc_scratcha1;
/*36*/  u8	nc_scratcha2;
/*37*/  u8	nc_scratcha3;

/*38*/  u8	nc_dmode;
	#define   BL_2    0x80  /* mod: burst length shift value +2 */
	#define   BL_1    0x40  /* mod: burst length shift value +1 */
	#define   ERL     0x08  /* mod: enable read line            */
	#define   ERMP    0x04  /* mod: enable read multiple        */
	#define   BOF     0x02  /* mod: burst op code fetch         */

/*39*/  u8	nc_dien;
/*3a*/  u8	nc_sbr;

/*3b*/  u8	nc_dcntl;	/* --> Script execution control     */
	#define   CLSE    0x80  /* mod: cache line size enable      */
	#define   PFF     0x40  /* cmd: pre-fetch flush             */
	#define   PFEN    0x20  /* mod: pre-fetch enable            */
	#define   SSM     0x10  /* mod: single step mode            */
	#define   IRQM    0x08  /* mod: irq mode (1 = totem pole !) */
	#define   STD     0x04  /* cmd: start dma mode              */
	#define   IRQD    0x02  /* mod: irq disable                 */
 	#define	  NOCOM   0x01	/* cmd: protect sfbr while reselect */
				/* bits 0-1 rsvd for C1010          */

/*3c*/  u32	nc_adder;

/*40*/  u16	nc_sien;	/* -->: interrupt enable            */
/*42*/  u16	nc_sist;	/* <--: interrupt status            */
        #define   SBMC    0x1000/* sta: SCSI Bus Mode Change (895/6 only) */
        #define   STO     0x0400/* sta: timeout (select)            */
        #define   GEN     0x0200/* sta: timeout (general)           */
        #define   HTH     0x0100/* sta: timeout (handshake)         */
        #define   MA      0x80  /* sta: phase mismatch              */
        #define   CMP     0x40  /* sta: arbitration complete        */
        #define   SEL     0x20  /* sta: selected by another device  */
        #define   RSL     0x10  /* sta: reselected by another device*/
        #define   SGE     0x08  /* sta: gross error (over/underflow)*/
        #define   UDC     0x04  /* sta: unexpected disconnect       */
        #define   RST     0x02  /* sta: scsi bus reset detected     */
        #define   PAR     0x01  /* sta: scsi parity error           */

/*44*/  u8	nc_slpar;
/*45*/  u8	nc_swide;
/*46*/  u8	nc_macntl;
/*47*/  u8	nc_gpcntl;
/*48*/  u8	nc_stime0;	/* cmd: timeout for select&handshake*/
/*49*/  u8	nc_stime1;	/* cmd: timeout user defined        */
/*4a*/  u16	nc_respid;	/* sta: Reselect-IDs                */

/*4c*/  u8	nc_stest0;

/*4d*/  u8	nc_stest1;
	#define   SCLK    0x80	/* Use the PCI clock as SCSI clock	*/
	#define   DBLEN   0x08	/* clock doubler running		*/
	#define   DBLSEL  0x04	/* clock doubler selected		*/
  

/*4e*/  u8	nc_stest2;
	#define   ROF     0x40	/* reset scsi offset (after gross error!) */
	#define   EXT     0x02  /* extended filtering                     */

/*4f*/  u8	nc_stest3;
	#define   TE     0x80	/* c: tolerAnt enable */
	#define   HSC    0x20	/* c: Halt SCSI Clock */
	#define   CSF    0x02	/* c: clear scsi fifo */

/*50*/  u16	nc_sidl;	/* Lowlevel: latched from scsi data */
/*52*/  u8	nc_stest4;
	#define   SMODE  0xc0	/* SCSI bus mode      (895/6 only) */
	#define    SMODE_HVD 0x40	/* High Voltage Differential       */
	#define    SMODE_SE  0x80	/* Single Ended                    */
	#define    SMODE_LVD 0xc0	/* Low Voltage Differential        */
	#define   LCKFRQ 0x20	/* Frequency Lock (895/6 only)     */
				/* bits 0-5 rsvd for C1010         */

/*53*/  u8	nc_53_;
/*54*/  u16	nc_sodl;	/* Lowlevel: data out to scsi data  */
/*56*/	u8	nc_ccntl0;	/* Chip Control 0 (896)             */
	#define   ENPMJ  0x80	/* Enable Phase Mismatch Jump       */
	#define   PMJCTL 0x40	/* Phase Mismatch Jump Control      */
	#define   ENNDJ  0x20	/* Enable Non Data PM Jump          */
	#define   DISFC  0x10	/* Disable Auto FIFO Clear          */
	#define   DILS   0x02	/* Disable Internal Load/Store      */
	#define   DPR    0x01	/* Disable Pipe Req                 */

/*57*/	u8	nc_ccntl1;	/* Chip Control 1 (896)             */
	#define   ZMOD   0x80	/* High Impedance Mode              */
	#define   DDAC   0x08	/* Disable Dual Address Cycle       */
	#define   XTIMOD 0x04	/* 64-bit Table Ind. Indexing Mode  */
	#define   EXTIBMV 0x02	/* Enable 64-bit Table Ind. BMOV    */
	#define   EXDBMV 0x01	/* Enable 64-bit Direct BMOV        */

/*58*/  u16	nc_sbdl;	/* Lowlevel: data from scsi data    */
/*5a*/  u16	nc_5a_;

/*5c*/  u8	nc_scr0;	/* Working register B               */
/*5d*/  u8	nc_scr1;
/*5e*/  u8	nc_scr2;
/*5f*/  u8	nc_scr3;

/*60*/  u8	nc_scrx[64];	/* Working register C-R             */
/*a0*/	u32	nc_mmrs;	/* Memory Move Read Selector        */
/*a4*/	u32	nc_mmws;	/* Memory Move Write Selector       */
/*a8*/	u32	nc_sfs;		/* Script Fetch Selector            */
/*ac*/	u32	nc_drs;		/* DSA Relative Selector            */
/*b0*/	u32	nc_sbms;	/* Static Block Move Selector       */
/*b4*/	u32	nc_dbms;	/* Dynamic Block Move Selector      */
/*b8*/	u32	nc_dnad64;	/* DMA Next Address 64              */
/*bc*/	u16	nc_scntl4;	/* C1010 only                       */
	#define   U3EN    0x80	/* Enable Ultra 3                   */
	#define   AIPCKEN 0x40  /* AIP checking enable              */
				/* Also enable AIP generation on C10-33*/
	#define   XCLKH_DT 0x08 /* Extra clock of data hold on DT edge */
	#define   XCLKH_ST 0x04 /* Extra clock of data hold on ST edge */
	#define   XCLKS_DT 0x02 /* Extra clock of data set  on DT edge */
	#define   XCLKS_ST 0x01 /* Extra clock of data set  on ST edge */
/*be*/	u8	nc_aipcntl0;	/* AIP Control 0 C1010 only         */
/*bf*/	u8	nc_aipcntl1;	/* AIP Control 1 C1010 only         */
	#define DISAIP  0x08	/* Disable AIP generation C10-66 only  */
/*c0*/	u32	nc_pmjad1;	/* Phase Mismatch Jump Address 1    */
/*c4*/	u32	nc_pmjad2;	/* Phase Mismatch Jump Address 2    */
/*c8*/	u8	nc_rbc;		/* Remaining Byte Count             */
/*c9*/	u8	nc_rbc1;
/*ca*/	u8	nc_rbc2;
/*cb*/	u8	nc_rbc3;

/*cc*/	u8	nc_ua;		/* Updated Address                  */
/*cd*/	u8	nc_ua1;
/*ce*/	u8	nc_ua2;
/*cf*/	u8	nc_ua3;
/*d0*/	u32	nc_esa;		/* Entry Storage Address            */
/*d4*/	u8	nc_ia;		/* Instruction Address              */
/*d5*/	u8	nc_ia1;
/*d6*/	u8	nc_ia2;
/*d7*/	u8	nc_ia3;
/*d8*/	u32	nc_sbc;		/* SCSI Byte Count (3 bytes only)   */
/*dc*/	u32	nc_csbc;	/* Cumulative SCSI Byte Count       */
                                /* Following for C1010 only         */
/*e0*/	u16    nc_crcpad;	/* CRC Value                        */
/*e2*/	u8     nc_crccntl0;	/* CRC control register             */
	#define   SNDCRC  0x10	/* Send CRC Request                 */
/*e3*/	u8     nc_crccntl1;	/* CRC control register             */
/*e4*/	u32    nc_crcdata;	/* CRC data register                */
/*e8*/	u32    nc_e8_;
/*ec*/	u32    nc_ec_;
/*f0*/	u16    nc_dfbc;		/* DMA FIFO byte count              */ 
};

/*-----------------------------------------------------------
 *
 *	Utility macros for the script.
 *
 *-----------------------------------------------------------
 */

#define REGJ(p,r) (offsetof(struct sym_reg, p ## r))
#define REG(r) REGJ (nc_, r)

/*-----------------------------------------------------------
 *
 *	SCSI phases
 *
 *-----------------------------------------------------------
 */

#define	SCR_DATA_OUT	0x00000000
#define	SCR_DATA_IN	0x01000000
#define	SCR_COMMAND	0x02000000
#define	SCR_STATUS	0x03000000
#define	SCR_DT_DATA_OUT	0x04000000
#define	SCR_DT_DATA_IN	0x05000000
#define SCR_MSG_OUT	0x06000000
#define SCR_MSG_IN      0x07000000
/* DT phases are illegal for non Ultra3 mode */
#define SCR_ILG_OUT	0x04000000
#define SCR_ILG_IN	0x05000000

/*-----------------------------------------------------------
 *
 *	Data transfer via SCSI.
 *
 *-----------------------------------------------------------
 *
 *	MOVE_ABS (LEN)
 *	<<start address>>
 *
 *	MOVE_IND (LEN)
 *	<<dnad_offset>>
 *
 *	MOVE_TBL
 *	<<dnad_offset>>
 *
 *-----------------------------------------------------------
 */

#define OPC_MOVE          0x08000000

#define SCR_MOVE_ABS(l) ((0x00000000 | OPC_MOVE) | (l))
/* #define SCR_MOVE_IND(l) ((0x20000000 | OPC_MOVE) | (l)) */
#define SCR_MOVE_TBL     (0x10000000 | OPC_MOVE)

#define SCR_CHMOV_ABS(l) ((0x00000000) | (l))
/* #define SCR_CHMOV_IND(l) ((0x20000000) | (l)) */
#define SCR_CHMOV_TBL     (0x10000000)

#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
/* We steal the `indirect addressing' flag for target mode MOVE in scripts */

#define OPC_TCHMOVE        0x08000000

#define SCR_TCHMOVE_ABS(l) ((0x20000000 | OPC_TCHMOVE) | (l))
#define SCR_TCHMOVE_TBL     (0x30000000 | OPC_TCHMOVE)

#define SCR_TMOV_ABS(l)    ((0x20000000) | (l))
#define SCR_TMOV_TBL        (0x30000000)
#endif

struct sym_tblmove {
        u32  size;
        u32  addr;
};

/*-----------------------------------------------------------
 *
 *	Selection
 *
 *-----------------------------------------------------------
 *
 *	SEL_ABS | SCR_ID (0..15)    [ | REL_JMP]
 *	<<alternate_address>>
 *
 *	SEL_TBL | << dnad_offset>>  [ | REL_JMP]
 *	<<alternate_address>>
 *
 *-----------------------------------------------------------
 */

#define	SCR_SEL_ABS	0x40000000
#define	SCR_SEL_ABS_ATN	0x41000000
#define	SCR_SEL_TBL	0x42000000
#define	SCR_SEL_TBL_ATN	0x43000000

#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
#define	SCR_RESEL_ABS     0x40000000
#define	SCR_RESEL_ABS_ATN 0x41000000
#define	SCR_RESEL_TBL     0x42000000
#define	SCR_RESEL_TBL_ATN 0x43000000
#endif

struct sym_tblsel {
        u_char  sel_scntl4;	/* C1010 only */
        u_char  sel_sxfer;
        u_char  sel_id;
        u_char  sel_scntl3;
};

#define SCR_JMP_REL     0x04000000
#define SCR_ID(id)	(((u32)(id)) << 16)

/*-----------------------------------------------------------
 *
 *	Waiting for Disconnect or Reselect
 *
 *-----------------------------------------------------------
 *
 *	WAIT_DISC
 *	dummy: <<alternate_address>>
 *
 *	WAIT_RESEL
 *	<<alternate_address>>
 *
 *-----------------------------------------------------------
 */

#define	SCR_WAIT_DISC	0x48000000
#define SCR_WAIT_RESEL  0x50000000

#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
#define	SCR_DISCONNECT	0x48000000
#endif

/*-----------------------------------------------------------
 *
 *	Bit Set / Reset
 *
 *-----------------------------------------------------------
 *
 *	SET (flags {|.. })
 *
 *	CLR (flags {|.. })
 *
 *-----------------------------------------------------------
 */

#define SCR_SET(f)     (0x58000000 | (f))
#define SCR_CLR(f)     (0x60000000 | (f))

#define	SCR_CARRY	0x00000400
#define	SCR_TRG		0x00000200
#define	SCR_ACK		0x00000040
#define	SCR_ATN		0x00000008


/*-----------------------------------------------------------
 *
 *	Memory to memory move
 *
 *-----------------------------------------------------------
 *
 *	COPY (bytecount)
 *	<< source_address >>
 *	<< destination_address >>
 *
 *	SCR_COPY   sets the NO FLUSH option by default.
 *	SCR_COPY_F does not set this option.
 *
 *	For chips which do not support this option,
 *	sym_fw_bind_script() will remove this bit.
 *
 *-----------------------------------------------------------
 */

#define SCR_NO_FLUSH 0x01000000

#define SCR_COPY(n) (0xc0000000 | SCR_NO_FLUSH | (n))
#define SCR_COPY_F(n) (0xc0000000 | (n))

/*-----------------------------------------------------------
 *
 *	Register move and binary operations
 *
 *-----------------------------------------------------------
 *
 *	SFBR_REG (reg, op, data)        reg  = SFBR op data
 *	<< 0 >>
 *
 *	REG_SFBR (reg, op, data)        SFBR = reg op data
 *	<< 0 >>
 *
 *	REG_REG  (reg, op, data)        reg  = reg op data
 *	<< 0 >>
 *
 *-----------------------------------------------------------
 *
 *	On 825A, 875, 895 and 896 chips the content 
 *	of SFBR register can be used as data (SCR_SFBR_DATA).
 *	The 896 has additionnal IO registers starting at 
 *	offset 0x80. Bit 7 of register offset is stored in 
 *	bit 7 of the SCRIPTS instruction first DWORD.
 *
 *-----------------------------------------------------------
 */

#define SCR_REG_OFS(ofs) ((((ofs) & 0x7f) << 16ul) + ((ofs) & 0x80)) 

#define SCR_SFBR_REG(reg,op,data) \
        (0x68000000 | (SCR_REG_OFS(REG(reg))) | (op) | (((data)&0xff)<<8ul))

#define SCR_REG_SFBR(reg,op,data) \
        (0x70000000 | (SCR_REG_OFS(REG(reg))) | (op) | (((data)&0xff)<<8ul))

#define SCR_REG_REG(reg,op,data) \
        (0x78000000 | (SCR_REG_OFS(REG(reg))) | (op) | (((data)&0xff)<<8ul))


#define      SCR_LOAD   0x00000000
#define      SCR_SHL    0x01000000
#define      SCR_OR     0x02000000
#define      SCR_XOR    0x03000000
#define      SCR_AND    0x04000000
#define      SCR_SHR    0x05000000
#define      SCR_ADD    0x06000000
#define      SCR_ADDC   0x07000000

#define      SCR_SFBR_DATA   (0x00800000>>8ul)	/* Use SFBR as data */

/*-----------------------------------------------------------
 *
 *	FROM_REG (reg)		  SFBR = reg
 *	<< 0 >>
 *
 *	TO_REG	 (reg)		  reg  = SFBR
 *	<< 0 >>
 *
 *	LOAD_REG (reg, data)	  reg  = <data>
 *	<< 0 >>
 *
 *	LOAD_SFBR(data) 	  SFBR = <data>
 *	<< 0 >>
 *
 *-----------------------------------------------------------
 */

#define	SCR_FROM_REG(reg) \
	SCR_REG_SFBR(reg,SCR_OR,0)

#define	SCR_TO_REG(reg) \
	SCR_SFBR_REG(reg,SCR_OR,0)

#define	SCR_LOAD_REG(reg,data) \
	SCR_REG_REG(reg,SCR_LOAD,data)

#define SCR_LOAD_SFBR(data) \
        (SCR_REG_SFBR (gpreg, SCR_LOAD, data))

/*-----------------------------------------------------------
 *
 *	LOAD  from memory   to register.
 *	STORE from register to memory.
 *
 *	Only supported by 810A, 860, 825A, 875, 895 and 896.
 *
 *-----------------------------------------------------------
 *
 *	LOAD_ABS (LEN)
 *	<<start address>>
 *
 *	LOAD_REL (LEN)        (DSA relative)
 *	<<dsa_offset>>
 *
 *-----------------------------------------------------------
 */

#define SCR_REG_OFS2(ofs) (((ofs) & 0xff) << 16ul)
#define SCR_NO_FLUSH2	0x02000000
#define SCR_DSA_REL2	0x10000000

#define SCR_LOAD_R(reg, how, n) \
        (0xe1000000 | how | (SCR_REG_OFS2(REG(reg))) | (n))

#define SCR_STORE_R(reg, how, n) \
        (0xe0000000 | how | (SCR_REG_OFS2(REG(reg))) | (n))

#define SCR_LOAD_ABS(reg, n)	SCR_LOAD_R(reg, SCR_NO_FLUSH2, n)
#define SCR_LOAD_REL(reg, n)	SCR_LOAD_R(reg, SCR_NO_FLUSH2|SCR_DSA_REL2, n)
#define SCR_LOAD_ABS_F(reg, n)	SCR_LOAD_R(reg, 0, n)
#define SCR_LOAD_REL_F(reg, n)	SCR_LOAD_R(reg, SCR_DSA_REL2, n)

#define SCR_STORE_ABS(reg, n)	SCR_STORE_R(reg, SCR_NO_FLUSH2, n)
#define SCR_STORE_REL(reg, n)	SCR_STORE_R(reg, SCR_NO_FLUSH2|SCR_DSA_REL2,n)
#define SCR_STORE_ABS_F(reg, n)	SCR_STORE_R(reg, 0, n)
#define SCR_STORE_REL_F(reg, n)	SCR_STORE_R(reg, SCR_DSA_REL2, n)


/*-----------------------------------------------------------
 *
 *	Waiting for Disconnect or Reselect
 *
 *-----------------------------------------------------------
 *
 *	JUMP            [ | IFTRUE/IFFALSE ( ... ) ]
 *	<<address>>
 *
 *	JUMPR           [ | IFTRUE/IFFALSE ( ... ) ]
 *	<<distance>>
 *
 *	CALL            [ | IFTRUE/IFFALSE ( ... ) ]
 *	<<address>>
 *
 *	CALLR           [ | IFTRUE/IFFALSE ( ... ) ]
 *	<<distance>>
 *
 *	RETURN          [ | IFTRUE/IFFALSE ( ... ) ]
 *	<<dummy>>
 *
 *	INT             [ | IFTRUE/IFFALSE ( ... ) ]
 *	<<ident>>
 *
 *	INT_FLY         [ | IFTRUE/IFFALSE ( ... ) ]
 *	<<ident>>
 *
 *	Conditions:
 *	     WHEN (phase)
 *	     IF   (phase)
 *	     CARRYSET
 *	     DATA (data, mask)
 *
 *-----------------------------------------------------------
 */

#define SCR_NO_OP       0x80000000
#define SCR_JUMP        0x80080000
#define SCR_JUMP64      0x80480000
#define SCR_JUMPR       0x80880000
#define SCR_CALL        0x88080000
#define SCR_CALLR       0x88880000
#define SCR_RETURN      0x90080000
#define SCR_INT         0x98080000
#define SCR_INT_FLY     0x98180000

#define IFFALSE(arg)   (0x00080000 | (arg))
#define IFTRUE(arg)    (0x00000000 | (arg))

#define WHEN(phase)    (0x00030000 | (phase))
#define IF(phase)      (0x00020000 | (phase))

#define DATA(D)        (0x00040000 | ((D) & 0xff))
#define MASK(D,M)      (0x00040000 | (((M ^ 0xff) & 0xff) << 8ul)|((D) & 0xff))

#define CARRYSET       (0x00200000)

/*-----------------------------------------------------------
 *
 *	SCSI  constants.
 *
 *-----------------------------------------------------------
 */

/*
 *	Messages
 */

#define	M_COMPLETE	COMMAND_COMPLETE
#define	M_EXTENDED	EXTENDED_MESSAGE
#define	M_SAVE_DP	SAVE_POINTERS
#define	M_RESTORE_DP	RESTORE_POINTERS
#define	M_DISCONNECT	DISCONNECT
#define	M_ID_ERROR	INITIATOR_ERROR
#define	M_ABORT		ABORT_TASK_SET
#define	M_REJECT	MESSAGE_REJECT
#define	M_NOOP		NOP
#define	M_PARITY	MSG_PARITY_ERROR
#define	M_LCOMPLETE	LINKED_CMD_COMPLETE
#define	M_FCOMPLETE	LINKED_FLG_CMD_COMPLETE
#define	M_RESET		TARGET_RESET
#define	M_ABORT_TAG	ABORT_TASK
#define	M_CLEAR_QUEUE	CLEAR_TASK_SET
#define	M_INIT_REC	INITIATE_RECOVERY
#define	M_REL_REC	RELEASE_RECOVERY
#define	M_TERMINATE	(0x11)
#define	M_SIMPLE_TAG	SIMPLE_QUEUE_TAG
#define	M_HEAD_TAG	HEAD_OF_QUEUE_TAG
#define	M_ORDERED_TAG	ORDERED_QUEUE_TAG
#define	M_IGN_RESIDUE	IGNORE_WIDE_RESIDUE

#define	M_X_MODIFY_DP	EXTENDED_MODIFY_DATA_POINTER
#define	M_X_SYNC_REQ	EXTENDED_SDTR
#define	M_X_WIDE_REQ	EXTENDED_WDTR
#define	M_X_PPR_REQ	EXTENDED_PPR

/*
 *	PPR protocol options
 */
#define	PPR_OPT_IU	(0x01)
#define	PPR_OPT_DT	(0x02)
#define	PPR_OPT_QAS	(0x04)
#define PPR_OPT_MASK	(0x07)

/*
 *	Status
 */

#define	S_GOOD		SAM_STAT_GOOD
#define	S_CHECK_COND	SAM_STAT_CHECK_CONDITION
#define	S_COND_MET	SAM_STAT_CONDITION_MET
#define	S_BUSY		SAM_STAT_BUSY
#define	S_INT		SAM_STAT_INTERMEDIATE
#define	S_INT_COND_MET	SAM_STAT_INTERMEDIATE_CONDITION_MET
#define	S_CONFLICT	SAM_STAT_RESERVATION_CONFLICT
#define	S_TERMINATED	SAM_STAT_COMMAND_TERMINATED
#define	S_QUEUE_FULL	SAM_STAT_TASK_SET_FULL
#define	S_ILLEGAL	(0xff)

#endif /* defined SYM_DEFS_H */
