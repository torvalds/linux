/*
 * 53c710 driver.  Modified from Drew Eckhardts driver
 * for 53c810 by Richard Hirst [richard@sleepie.demon.co.uk]
 *
 * I have left the code for the 53c8xx family in here, because it didn't
 * seem worth removing it.  The possibility of IO_MAPPED chips rather
 * than MEMORY_MAPPED remains, in case someone wants to add support for
 * 53c710 chips on Intel PCs (some older machines have them on the
 * motherboard).
 *
 * NOTE THERE MAY BE PROBLEMS WITH CASTS IN read8 AND Co.
 */

/*
 * NCR 53c{7,8}0x0 driver, header file
 *
 * Sponsored by
 *      iX Multiuser Multitasking Magazine
 *	Hannover, Germany
 *	hm@ix.de	
 *
 * Copyright 1993, 1994, 1995 Drew Eckhardt
 *      Visionary Computing 
 *      (Unix and Linux consulting and custom programming)
 *      drew@PoohSticks.ORG
 *	+1 (303) 786-7975
 *
 * TolerANT and SCSI SCRIPTS are registered trademarks of NCR Corporation.
 * 
 * PRE-ALPHA
 *
 * For more information, please consult 
 *
 * NCR 53C700/53C700-66
 * SCSI I/O Processor
 * Data Manual
 *
 * NCR 53C810
 * PCI-SCSI I/O Processor 
 * Data Manual
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * +1 (719) 578-3400
 *
 * Toll free literature number
 * +1 (800) 334-5454
 *
 */

#ifndef NCR53c710_H
#define NCR53c710_H

#ifndef HOSTS_C

/* SCSI control 0 rw, default = 0xc0 */ 
#define SCNTL0_REG 		0x00	
#define SCNTL0_ARB1		0x80	/* 0 0 = simple arbitration */
#define SCNTL0_ARB2		0x40	/* 1 1 = full arbitration */
#define SCNTL0_STRT		0x20	/* Start Sequence */
#define SCNTL0_WATN		0x10	/* Select with ATN */
#define SCNTL0_EPC		0x08	/* Enable parity checking */
/* Bit 2 is reserved on 800 series chips */
#define SCNTL0_EPG_700		0x04	/* Enable parity generation */
#define SCNTL0_AAP		0x02	/*  ATN/ on parity error */
#define SCNTL0_TRG		0x01	/* Target mode */

/* SCSI control 1 rw, default = 0x00 */

#define SCNTL1_REG 		0x01	
#define SCNTL1_EXC		0x80	/* Extra Clock Cycle of Data setup */
#define SCNTL1_ADB		0x40	/*  contents of SODL on bus */
#define SCNTL1_ESR_700		0x20	/* Enable SIOP response to selection 
					   and reselection */
#define SCNTL1_DHP_800		0x20	/* Disable halt on parity error or ATN
					   target mode only */
#define SCNTL1_CON		0x10	/* Connected */
#define SCNTL1_RST		0x08	/* SCSI RST/ */
#define SCNTL1_AESP		0x04	/* Force bad parity */
#define SCNTL1_SND_700		0x02	/* Start SCSI send */
#define SCNTL1_IARB_800		0x02	/* Immediate Arbitration, start
					   arbitration immediately after
					   busfree is detected */
#define SCNTL1_RCV_700		0x01	/* Start SCSI receive */
#define SCNTL1_SST_800		0x01	/* Start SCSI transfer */

/* SCSI control 2 rw, */

#define SCNTL2_REG_800		0x02	
#define SCNTL2_800_SDU		0x80	/* SCSI disconnect unexpected */

/* SCSI control 3 rw */

#define SCNTL3_REG_800 		0x03	
#define SCNTL3_800_SCF_SHIFT	4
#define SCNTL3_800_SCF_MASK	0x70
#define SCNTL3_800_SCF2		0x40	/* Synchronous divisor */
#define SCNTL3_800_SCF1		0x20	/* 0x00 = SCLK/3 */
#define SCNTL3_800_SCF0		0x10	/* 0x10 = SCLK/1 */
					/* 0x20 = SCLK/1.5 
					   0x30 = SCLK/2 
					   0x40 = SCLK/3 */
	    
#define SCNTL3_800_CCF_SHIFT	0
#define SCNTL3_800_CCF_MASK	0x07
#define SCNTL3_800_CCF2		0x04	/* 0x00 50.01 to 66 */
#define SCNTL3_800_CCF1		0x02	/* 0x01 16.67 to 25 */
#define SCNTL3_800_CCF0		0x01	/* 0x02	25.01 - 37.5 
					   0x03	37.51 - 50 
					   0x04 50.01 - 66 */

/*  
 * SCSI destination ID rw - the appropriate bit is set for the selected
 * target ID.  This is written by the SCSI SCRIPTS processor.
 * default = 0x00
 */
#define SDID_REG_700  		0x02	
#define SDID_REG_800		0x06

#define GP_REG_800		0x07	/* General purpose IO */
#define GP_800_IO1		0x02
#define GP_800_IO2		0x01

/* SCSI interrupt enable rw, default = 0x00 */
#define SIEN_REG_700		0x03	
#define SIEN0_REG_800		0x40
#define SIEN_MA			0x80	/* Phase mismatch (ini) or ATN (tgt) */
#define SIEN_FC			0x40	/* Function complete */
#define SIEN_700_STO		0x20	/* Selection or reselection timeout */
#define SIEN_800_SEL		0x20	/* Selected */
#define SIEN_700_SEL		0x10	/* Selected or reselected */
#define SIEN_800_RESEL		0x10	/* Reselected */
#define SIEN_SGE		0x08	/* SCSI gross error */
#define SIEN_UDC		0x04	/* Unexpected disconnect */
#define SIEN_RST		0x02	/* SCSI RST/ received */
#define SIEN_PAR		0x01	/* Parity error */

/* 
 * SCSI chip ID rw
 * NCR53c700 : 
 * 	When arbitrating, the highest bit is used, when reselection or selection
 * 	occurs, the chip responds to all IDs for which a bit is set.
 * 	default = 0x00 
 * NCR53c810 : 
 *	Uses bit mapping
 */
#define SCID_REG		0x04	
/* Bit 7 is reserved on 800 series chips */
#define SCID_800_RRE		0x40	/* Enable response to reselection */
#define SCID_800_SRE		0x20	/* Enable response to selection */
/* Bits four and three are reserved on 800 series chips */
#define SCID_800_ENC_MASK	0x07	/* Encoded SCSI ID */

/* SCSI transfer rw, default = 0x00 */
#define SXFER_REG		0x05
#define SXFER_DHP		0x80	/* Disable halt on parity */

#define SXFER_TP2		0x40	/* Transfer period msb */
#define SXFER_TP1		0x20
#define SXFER_TP0		0x10	/* lsb */
#define SXFER_TP_MASK		0x70
/* FIXME : SXFER_TP_SHIFT == 5 is right for '8xx chips */
#define SXFER_TP_SHIFT		5
#define SXFER_TP_4		0x00	/* Divisors */
#define SXFER_TP_5		0x10<<1
#define SXFER_TP_6		0x20<<1
#define SXFER_TP_7		0x30<<1
#define SXFER_TP_8		0x40<<1
#define SXFER_TP_9		0x50<<1
#define SXFER_TP_10		0x60<<1
#define SXFER_TP_11		0x70<<1

#define SXFER_MO3		0x08	/* Max offset msb */
#define SXFER_MO2		0x04
#define SXFER_MO1		0x02
#define SXFER_MO0		0x01	/* lsb */
#define SXFER_MO_MASK		0x0f
#define SXFER_MO_SHIFT		0

/* 
 * SCSI output data latch rw
 * The contents of this register are driven onto the SCSI bus when 
 * the Assert Data Bus bit of the SCNTL1 register is set and 
 * the CD, IO, and MSG bits of the SOCL register match the SCSI phase
 */
#define SODL_REG_700		0x06	
#define SODL_REG_800		0x54


/* 
 * SCSI output control latch rw, default = 0 
 * Note that when the chip is being manually programmed as an initiator,
 * the MSG, CD, and IO bits must be set correctly for the phase the target
 * is driving the bus in.  Otherwise no data transfer will occur due to 
 * phase mismatch.
 */

#define SOCL_REG		0x07
#define SOCL_REQ		0x80	/*  REQ */
#define SOCL_ACK		0x40	/*  ACK */
#define SOCL_BSY		0x20	/*  BSY */
#define SOCL_SEL		0x10	/*  SEL */
#define SOCL_ATN		0x08	/*  ATN */
#define SOCL_MSG		0x04	/*  MSG */
#define SOCL_CD			0x02	/*  C/D */
#define SOCL_IO			0x01	/*  I/O */

/* 
 * SCSI first byte received latch ro 
 * This register contains the first byte received during a block MOVE 
 * SCSI SCRIPTS instruction, including
 * 
 * Initiator mode	Target mode
 * Message in		Command
 * Status		Message out
 * Data in		Data out
 *
 * It also contains the selecting or reselecting device's ID and our 
 * ID.
 *
 * Note that this is the register the various IF conditionals can 
 * operate on.
 */
#define SFBR_REG		0x08	

/* 
 * SCSI input data latch ro
 * In initiator mode, data is latched into this register on the rising
 * edge of REQ/. In target mode, data is latched on the rising edge of 
 * ACK/
 */
#define SIDL_REG_700		0x09
#define SIDL_REG_800		0x50

/* 
 * SCSI bus data lines ro 
 * This register reflects the instantaneous status of the SCSI data 
 * lines.  Note that SCNTL0 must be set to disable parity checking, 
 * otherwise reading this register will latch new parity.
 */
#define SBDL_REG_700		0x0a
#define SBDL_REG_800		0x58

#define SSID_REG_800		0x0a
#define SSID_800_VAL		0x80	/* Exactly two bits asserted at sel */
#define SSID_800_ENCID_MASK	0x07	/* Device which performed operation */


/* 
 * SCSI bus control lines rw, 
 * instantaneous readout of control lines 
 */
#define SBCL_REG		0x0b 	
#define SBCL_REQ		0x80	/*  REQ ro */
#define SBCL_ACK		0x40	/*  ACK ro */
#define SBCL_BSY		0x20	/*  BSY ro */
#define SBCL_SEL		0x10	/*  SEL ro */
#define SBCL_ATN		0x08	/*  ATN ro */
#define SBCL_MSG		0x04	/*  MSG ro */
#define SBCL_CD			0x02	/*  C/D ro */
#define SBCL_IO			0x01	/*  I/O ro */
#define SBCL_PHASE_CMDOUT	SBCL_CD
#define SBCL_PHASE_DATAIN	SBCL_IO
#define SBCL_PHASE_DATAOUT	0
#define SBCL_PHASE_MSGIN	(SBCL_CD|SBCL_IO|SBCL_MSG)
#define SBCL_PHASE_MSGOUT	(SBCL_CD|SBCL_MSG)
#define SBCL_PHASE_STATIN	(SBCL_CD|SBCL_IO)
#define SBCL_PHASE_MASK		(SBCL_CD|SBCL_IO|SBCL_MSG)
/* 
 * Synchronous SCSI Clock Control bits 
 * 0 - set by DCNTL 
 * 1 - SCLK / 1.0
 * 2 - SCLK / 1.5
 * 3 - SCLK / 2.0 
 */
#define SBCL_SSCF1		0x02	/* wo, -66 only */
#define SBCL_SSCF0		0x01	/* wo, -66 only */
#define SBCL_SSCF_MASK		0x03

/* 
 * XXX note : when reading the DSTAT and STAT registers to clear interrupts,
 * insure that 10 clocks elapse between the two  
 */
/* DMA status ro */
#define DSTAT_REG		0x0c	
#define DSTAT_DFE		0x80	/* DMA FIFO empty */
#define DSTAT_800_MDPE		0x40	/* Master Data Parity Error */
#define DSTAT_800_BF		0x20	/* Bus Fault */
#define DSTAT_ABRT		0x10	/* Aborted - set on error */
#define DSTAT_SSI		0x08	/* SCRIPTS single step interrupt */
#define DSTAT_SIR		0x04	/* SCRIPTS interrupt received - 
					   set when INT instruction is 
					   executed */
#define DSTAT_WTD		0x02	/* Watchdog timeout detected */
#define DSTAT_OPC		0x01	/* Illegal instruction */
#define DSTAT_800_IID		0x01	/* Same thing, different name */


/* NCR53c800 moves this stuff into SIST0 */
#define SSTAT0_REG		0x0d	/* SCSI status 0 ro */
#define SIST0_REG_800		0x42	
#define SSTAT0_MA		0x80	/* ini : phase mismatch,
					 * tgt : ATN/ asserted 
					 */
#define SSTAT0_CMP		0x40	/* function complete */
#define SSTAT0_700_STO		0x20	/* Selection or reselection timeout */
#define SIST0_800_SEL		0x20	/* Selected */
#define SSTAT0_700_SEL		0x10	/* Selected or reselected */
#define SIST0_800_RSL		0x10	/* Reselected */
#define SSTAT0_SGE		0x08	/* SCSI gross error */
#define SSTAT0_UDC		0x04	/* Unexpected disconnect */
#define SSTAT0_RST		0x02	/* SCSI RST/ received */
#define SSTAT0_PAR		0x01	/* Parity error */

/* And uses SSTAT0 for what was SSTAT1 */

#define SSTAT1_REG		0x0e	/* SCSI status 1 ro */
#define SSTAT1_ILF		0x80	/* SIDL full */
#define SSTAT1_ORF		0x40	/* SODR full */
#define SSTAT1_OLF		0x20	/* SODL full */
#define SSTAT1_AIP		0x10	/* Arbitration in progress */
#define SSTAT1_LOA		0x08	/* Lost arbitration */
#define SSTAT1_WOA		0x04	/* Won arbitration */
#define SSTAT1_RST		0x02	/* Instant readout of RST/ */
#define SSTAT1_SDP		0x01	/* Instant readout of SDP/ */

#define SSTAT2_REG		0x0f	/* SCSI status 2 ro */
#define SSTAT2_FF3		0x80 	/* number of bytes in synchronous */
#define SSTAT2_FF2		0x40	/* data FIFO */
#define SSTAT2_FF1		0x20	
#define SSTAT2_FF0		0x10
#define SSTAT2_FF_MASK		0xf0
#define SSTAT2_FF_SHIFT		4

/* 
 * Latched signals, latched on the leading edge of REQ/ for initiators,
 * ACK/ for targets.
 */
#define SSTAT2_SDP		0x08	/* SDP */
#define SSTAT2_MSG		0x04	/* MSG */
#define SSTAT2_CD		0x02	/* C/D */
#define SSTAT2_IO		0x01	/* I/O */
#define SSTAT2_PHASE_CMDOUT	SSTAT2_CD
#define SSTAT2_PHASE_DATAIN	SSTAT2_IO
#define SSTAT2_PHASE_DATAOUT	0
#define SSTAT2_PHASE_MSGIN	(SSTAT2_CD|SSTAT2_IO|SSTAT2_MSG)
#define SSTAT2_PHASE_MSGOUT	(SSTAT2_CD|SSTAT2_MSG)
#define SSTAT2_PHASE_STATIN	(SSTAT2_CD|SSTAT2_IO)
#define SSTAT2_PHASE_MASK	(SSTAT2_CD|SSTAT2_IO|SSTAT2_MSG)


/* NCR53c700-66 only */
#define SCRATCHA_REG_00		0x10    /* through  0x13 Scratch A rw */
/* NCR53c710 and higher */
#define DSA_REG			0x10	/* DATA structure address */

#define CTEST0_REG_700		0x14	/* Chip test 0 ro */
#define CTEST0_REG_800		0x18	/* Chip test 0 rw, general purpose */
/* 0x80 - 0x04 are reserved */
#define CTEST0_700_RTRG		0x02	/* Real target mode */
#define CTEST0_700_DDIR		0x01	/* Data direction, 1 = 
					 * SCSI bus to host, 0  =
					 * host to SCSI.
					 */

#define CTEST1_REG_700		0x15	/* Chip test 1 ro */
#define CTEST1_REG_800		0x19	/* Chip test 1 ro */
#define CTEST1_FMT3		0x80	/* Identify which byte lanes are empty */
#define CTEST1_FMT2		0x40 	/* in the DMA FIFO */
#define CTEST1_FMT1		0x20
#define CTEST1_FMT0		0x10

#define CTEST1_FFL3		0x08	/* Identify which bytes lanes are full */
#define CTEST1_FFL2		0x04	/* in the DMA FIFO */
#define CTEST1_FFL1		0x02
#define CTEST1_FFL0		0x01

#define CTEST2_REG_700		0x16	/* Chip test 2 ro */
#define CTEST2_REG_800		0x1a	/* Chip test 2 ro */

#define CTEST2_800_DDIR		0x80	/* 1 = SCSI->host */
#define CTEST2_800_SIGP		0x40	/* A copy of SIGP in ISTAT.
					   Reading this register clears */
#define CTEST2_800_CIO		0x20	/* Configured as IO */.
#define CTEST2_800_CM		0x10	/* Configured as memory */

/* 0x80 - 0x40 are reserved on 700 series chips */
#define CTEST2_700_SOFF		0x20	/* SCSI Offset Compare,
					 * As an initiator, this bit is 
					 * one when the synchronous offset
					 * is zero, as a target this bit 
					 * is one when the synchronous 
					 * offset is at the maximum
					 * defined in SXFER
					 */
#define CTEST2_700_SFP		0x10	/* SCSI FIFO parity bit,
					 * reading CTEST3 unloads a byte
					 * from the FIFO and sets this
					 */
#define CTEST2_700_DFP		0x08	/* DMA FIFO parity bit,
					 * reading CTEST6 unloads a byte
					 * from the FIFO and sets this
					 */
#define CTEST2_TEOP		0x04	/* SCSI true end of process,
					 * indicates a totally finished
					 * transfer
					 */
#define CTEST2_DREQ		0x02	/* Data request signal */
/* 0x01 is reserved on 700 series chips */
#define CTEST2_800_DACK		0x01	

/* 
 * Chip test 3 ro 
 * Unloads the bottom byte of the eight deep SCSI synchronous FIFO,
 * check SSTAT2 FIFO full bits to determine size.  Note that a GROSS
 * error results if a read is attempted on this register.  Also note 
 * that 16 and 32 bit reads of this register will cause corruption.
 */
#define CTEST3_REG_700		0x17	
/*  Chip test 3 rw */
#define CTEST3_REG_800		0x1b
#define CTEST3_800_V3		0x80	/* Chip revision */
#define CTEST3_800_V2		0x40
#define CTEST3_800_V1		0x20
#define CTEST3_800_V0		0x10
#define CTEST3_800_FLF		0x08	/* Flush DMA FIFO */
#define CTEST3_800_CLF		0x04	/* Clear DMA FIFO */
#define CTEST3_800_FM		0x02	/* Fetch mode pin */
/* bit 0 is reserved on 800 series chips */

#define CTEST4_REG_700		0x18	/* Chip test 4 rw */
#define CTEST4_REG_800		0x21	/* Chip test 4 rw */
/* 0x80 is reserved on 700 series chips */
#define CTEST4_800_BDIS		0x80	/* Burst mode disable */
#define CTEST4_ZMOD		0x40	/* High impedance mode */
#define CTEST4_SZM		0x20	/* SCSI bus high impedance */
#define CTEST4_700_SLBE		0x10	/* SCSI loopback enabled */
#define CTEST4_800_SRTM		0x10	/* Shadow Register Test Mode */
#define CTEST4_700_SFWR		0x08	/* SCSI FIFO write enable, 
					 * redirects writes from SODL
					 * to the SCSI FIFO.
					 */
#define CTEST4_800_MPEE		0x08	/* Enable parity checking
					   during master cycles on PCI
					   bus */

/* 
 * These bits send the contents of the CTEST6 register to the appropriate
 * byte lane of the 32 bit DMA FIFO.  Normal operation is zero, otherwise 
 * the high bit means the low two bits select the byte lane.
 */
#define CTEST4_FBL2		0x04	
#define CTEST4_FBL1		0x02
#define CTEST4_FBL0		0x01	
#define CTEST4_FBL_MASK		0x07
#define CTEST4_FBL_0		0x04	/* Select DMA FIFO byte lane 0 */
#define CTEST4_FBL_1		0x05	/* Select DMA FIFO byte lane 1 */
#define CTEST4_FBL_2		0x06	/* Select DMA FIFO byte lane 2 */
#define CTEST4_FBL_3		0x07	/* Select DMA FIFO byte lane 3 */
#define CTEST4_800_SAVE		(CTEST4_800_BDIS)


#define CTEST5_REG_700		0x19	/* Chip test 5 rw */
#define CTEST5_REG_800		0x22	/* Chip test 5 rw */
/* 
 * Clock Address Incrementor.  When set, it increments the 
 * DNAD register to the next bus size boundary.  It automatically 
 * resets itself when the operation is complete.
 */
#define CTEST5_ADCK		0x80
/*
 * Clock Byte Counter.  When set, it decrements the DBC register to
 * the next bus size boundary.
 */
#define CTEST5_BBCK		0x40
/*
 * Reset SCSI Offset.  Setting this bit to 1 clears the current offset
 * pointer in the SCSI synchronous offset counter (SSTAT).  This bit
 * is set to 1 if a SCSI Gross Error Condition occurs.  The offset should
 * be cleared when a synchronous transfer fails.  When written, it is 
 * automatically cleared after the SCSI synchronous offset counter is 
 * reset.
 */
/* Bit 5 is reserved on 800 series chips */
#define CTEST5_700_ROFF		0x20
/* 
 * Master Control for Set or Reset pulses. When 1, causes the low 
 * four bits of register to set when set, 0 causes the low bits to
 * clear when set.
 */
#define CTEST5_MASR 		0x10	
#define CTEST5_DDIR		0x08	/* DMA direction */
/*
 * Bits 2-0 are reserved on 800 series chips
 */
#define CTEST5_700_EOP		0x04	/* End of process */
#define CTEST5_700_DREQ		0x02	/* Data request */
#define CTEST5_700_DACK		0x01	/* Data acknowledge */

/* 
 * Chip test 6 rw - writing to this register writes to the byte 
 * lane in the DMA FIFO as determined by the FBL bits in the CTEST4
 * register.
 */
#define CTEST6_REG_700		0x1a
#define CTEST6_REG_800		0x23

#define CTEST7_REG		0x1b	/* Chip test 7 rw */
/* 0x80 - 0x40 are reserved on NCR53c700 and NCR53c700-66 chips */
#define CTEST7_10_CDIS		0x80	/* Cache burst disable */
#define CTEST7_10_SC1		0x40	/* Snoop control bits */
#define CTEST7_10_SC0		0x20	
#define CTEST7_10_SC_MASK	0x60
/* 0x20 is reserved on the NCR53c700 */
#define CTEST7_0060_FM		0x20	/* Fetch mode */
#define CTEST7_STD		0x10	/* Selection timeout disable */
#define CTEST7_DFP		0x08	/* DMA FIFO parity bit for CTEST6 */
#define CTEST7_EVP		0x04	/* 1 = host bus even parity, 0 = odd */
#define CTEST7_10_TT1		0x02	/* Transfer type */
#define CTEST7_00_DC		0x02	/* Set to drive DC low during instruction 
					   fetch */
#define CTEST7_DIFF		0x01	/* Differential mode */

#define CTEST7_SAVE ( CTEST7_EVP | CTEST7_DIFF )


#define TEMP_REG		0x1c	/* through 0x1f Temporary stack rw */

#define DFIFO_REG		0x20	/* DMA FIFO rw */
/* 
 * 0x80 is reserved on the NCR53c710, the CLF and FLF bits have been
 * moved into the CTEST8 register.
 */
#define DFIFO_00_FLF		0x80	/* Flush DMA FIFO to memory */
#define DFIFO_00_CLF		0x40	/* Clear DMA and SCSI FIFOs */
#define DFIFO_BO6		0x40
#define DFIFO_BO5		0x20
#define DFIFO_BO4		0x10
#define DFIFO_BO3		0x08
#define DFIFO_BO2		0x04 
#define DFIFO_BO1		0x02
#define DFIFO_BO0		0x01
#define DFIFO_10_BO_MASK	0x7f	/* 7 bit counter */
#define DFIFO_00_BO_MASK	0x3f	/* 6 bit counter */

/* 
 * Interrupt status rw 
 * Note that this is the only register which can be read while SCSI
 * SCRIPTS are being executed.
 */
#define ISTAT_REG_700		0x21
#define ISTAT_REG_800		0x14
#define ISTAT_ABRT		0x80	/* Software abort, write 
					 *1 to abort, wait for interrupt. */
/* 0x40 and 0x20 are reserved on NCR53c700 and NCR53c700-66 chips */
#define ISTAT_10_SRST		0x40	/* software reset */
#define ISTAT_10_SIGP		0x20	/* signal script */
/* 0x10 is reserved on NCR53c700 series chips */
#define ISTAT_800_SEM		0x10	/* semaphore */
#define ISTAT_CON		0x08	/* 1 when connected */
#define ISTAT_800_INTF		0x04	/* Interrupt on the fly */
#define ISTAT_700_PRE		0x04	/* Pointer register empty.
					 * Set to 1 when DSPS and DSP
					 * registers are empty in pipeline
					 * mode, always set otherwise.
					 */
#define ISTAT_SIP		0x02	/* SCSI interrupt pending from
					 * SCSI portion of SIOP see
					 * SSTAT0
					 */
#define ISTAT_DIP		0x01	/* DMA interrupt pending 
					 * see DSTAT
					 */

/* NCR53c700-66 and NCR53c710 only */
#define CTEST8_REG		0x22	/* Chip test 8 rw */
#define CTEST8_0066_EAS		0x80	/* Enable alternate SCSI clock,
					 * ie read from SCLK/ rather than CLK/
					 */
#define CTEST8_0066_EFM		0x40	/* Enable fetch and master outputs */
#define CTEST8_0066_GRP		0x20	/* Generate Receive Parity for 
					 * pass through.  This insures that 
					 * bad parity won't reach the host 
					 * bus.
					 */
#define CTEST8_0066_TE		0x10	/* TolerANT enable.  Enable 
					 * active negation, should only
					 * be used for slow SCSI 
					 * non-differential.
					 */
#define CTEST8_0066_HSC		0x08	/* Halt SCSI clock */
#define CTEST8_0066_SRA		0x04	/* Shorten REQ/ACK filtering,
					 * must be set for fast SCSI-II
					 * speeds.
					 */
#define CTEST8_0066_DAS		0x02	/* Disable automatic target/initiator
					 * switching.
					 */
#define CTEST8_0066_LDE		0x01	/* Last disconnect enable.
					 * The status of pending 
					 * disconnect is maintained by
					 * the core, eliminating
					 * the possibility of missing a 
					 * selection or reselection
					 * while waiting to fetch a 
					 * WAIT DISCONNECT opcode.
					 */

#define CTEST8_10_V3		0x80	/* Chip revision */
#define CTEST8_10_V2		0x40
#define CTEST8_10_V1		0x20	
#define CTEST8_10_V0		0x10
#define CTEST8_10_V_MASK	0xf0	
#define CTEST8_10_FLF		0x08	/* Flush FIFOs */
#define CTEST8_10_CLF		0x04	/* Clear FIFOs */
#define CTEST8_10_FM		0x02	/* Fetch pin mode */
#define CTEST8_10_SM		0x01	/* Snoop pin mode */


/* 
 * The CTEST9 register may be used to differentiate between a
 * NCR53c700 and a NCR53c710.  
 *
 * Write 0xff to this register.
 * Read it.
 * If the contents are 0xff, it is a NCR53c700
 * If the contents are 0x00, it is a NCR53c700-66 first revision
 * If the contents are some other value, it is some other NCR53c700-66
 */
#define CTEST9_REG_00		0x23	/* Chip test 9 ro */
#define LCRC_REG_10		0x23	

/*
 * 0x24 through 0x27 are the DMA byte counter register.  Instructions
 * write their high 8 bits into the DCMD register, the low 24 bits into
 * the DBC register.
 *
 * Function is dependent on the command type being executed.
 */

 
#define DBC_REG			0x24
/* 
 * For Block Move Instructions, DBC is a 24 bit quantity representing 
 *     the number of bytes to transfer.
 * For Transfer Control Instructions, DBC is bit fielded as follows : 
 */
/* Bits 20 - 23 should be clear */
#define DBC_TCI_TRUE		(1 << 19) 	/* Jump when true */
#define DBC_TCI_COMPARE_DATA	(1 << 18)	/* Compare data */
#define DBC_TCI_COMPARE_PHASE	(1 << 17)	/* Compare phase with DCMD field */
#define DBC_TCI_WAIT_FOR_VALID	(1 << 16)	/* Wait for REQ */
/* Bits 8 - 15 are reserved on some implementations ? */
#define DBC_TCI_MASK_MASK	0xff00 		/* Mask for data compare */
#define DBC_TCI_MASK_SHIFT	8
#define DBC_TCI_DATA_MASK	0xff		/* Data to be compared */ 
#define DBC_TCI_DATA_SHIFT	0

#define DBC_RWRI_IMMEDIATE_MASK	0xff00		/* Immediate data */
#define DBC_RWRI_IMMEDIATE_SHIFT 8		/* Amount to shift */
#define DBC_RWRI_ADDRESS_MASK	0x3f0000	/* Register address */
#define DBC_RWRI_ADDRESS_SHIFT 	16


/*
 * DMA command r/w
 */
#define DCMD_REG		0x27	
#define DCMD_TYPE_MASK		0xc0	/* Masks off type */
#define DCMD_TYPE_BMI		0x00	/* Indicates a Block Move instruction */
#define DCMD_BMI_IO		0x01	/* I/O, CD, and MSG bits selecting   */
#define DCMD_BMI_CD		0x02	/* the phase for the block MOVE      */
#define DCMD_BMI_MSG		0x04	/* instruction 			     */

#define DCMD_BMI_OP_MASK	0x18	/* mask for opcode */
#define DCMD_BMI_OP_MOVE_T	0x00	/* MOVE */
#define DCMD_BMI_OP_MOVE_I	0x08	/* MOVE Initiator */

#define DCMD_BMI_INDIRECT	0x20	/*  Indirect addressing */

#define DCMD_TYPE_TCI		0x80	/* Indicates a Transfer Control 
					   instruction */
#define DCMD_TCI_IO		0x01	/* I/O, CD, and MSG bits selecting   */
#define DCMD_TCI_CD		0x02	/* the phase for the block MOVE      */
#define DCMD_TCI_MSG		0x04	/* instruction 			     */
#define DCMD_TCI_OP_MASK	0x38	/* mask for opcode */
#define DCMD_TCI_OP_JUMP	0x00	/* JUMP */
#define DCMD_TCI_OP_CALL	0x08	/* CALL */
#define DCMD_TCI_OP_RETURN	0x10	/* RETURN */
#define DCMD_TCI_OP_INT		0x18	/* INT */

#define DCMD_TYPE_RWRI		0x40	/* Indicates I/O or register Read/Write
					   instruction */
#define DCMD_RWRI_OPC_MASK	0x38	/* Opcode mask */
#define DCMD_RWRI_OPC_WRITE	0x28	/* Write SFBR to register */
#define DCMD_RWRI_OPC_READ	0x30	/* Read register to SFBR */
#define DCMD_RWRI_OPC_MODIFY	0x38	/* Modify in place */

#define DCMD_RWRI_OP_MASK	0x07
#define DCMD_RWRI_OP_MOVE	0x00
#define DCMD_RWRI_OP_SHL	0x01
#define DCMD_RWRI_OP_OR		0x02
#define DCMD_RWRI_OP_XOR	0x03
#define DCMD_RWRI_OP_AND	0x04
#define DCMD_RWRI_OP_SHR	0x05
#define DCMD_RWRI_OP_ADD	0x06
#define DCMD_RWRI_OP_ADDC	0x07

#define DCMD_TYPE_MMI		0xc0	/* Indicates a Memory Move instruction 
					   (three words) */


#define DNAD_REG		0x28	/* through 0x2b DMA next address for 
					   data */
#define DSP_REG			0x2c	/* through 0x2f DMA SCRIPTS pointer rw */
#define DSPS_REG		0x30	/* through 0x33 DMA SCRIPTS pointer 
					   save rw */
#define DMODE_REG_00		0x34 	/* DMA mode rw */
#define DMODE_00_BL1	0x80	/* Burst length bits */
#define DMODE_00_BL0	0x40
#define DMODE_BL_MASK	0xc0
/* Burst lengths (800) */
#define DMODE_BL_2	0x00	/* 2 transfer */
#define DMODE_BL_4	0x40	/* 4 transfers */
#define DMODE_BL_8	0x80	/* 8 transfers */
#define DMODE_BL_16	0xc0	/* 16 transfers */

#define DMODE_10_BL_1	0x00	/* 1 transfer */
#define DMODE_10_BL_2	0x40	/* 2 transfers */
#define DMODE_10_BL_4	0x80	/* 4 transfers */
#define DMODE_10_BL_8	0xc0	/* 8 transfers */
#define DMODE_10_FC2	0x20	/* Driven to FC2 pin */
#define DMODE_10_FC1	0x10	/* Driven to FC1 pin */
#define DMODE_710_PD	0x08	/* Program/data on FC0 pin */
#define DMODE_710_UO	0x02	/* User prog. output */

#define DMODE_700_BW16	0x20	/* Host buswidth = 16 */
#define DMODE_700_286	0x10	/* 286 mode */
#define DMODE_700_IOM	0x08	/* Transfer to IO port */
#define DMODE_700_FAM	0x04	/* Fixed address mode */
#define DMODE_700_PIPE	0x02	/* Pipeline mode disables 
					 * automatic fetch / exec 
					 */
#define DMODE_MAN	0x01		/* Manual start mode, 
					 * requires a 1 to be written
					 * to the start DMA bit in the DCNTL
					 * register to run scripts 
					 */

#define DMODE_700_SAVE ( DMODE_00_BL_MASK | DMODE_00_BW16 | DMODE_00_286 )

/* NCR53c800 series only */
#define SCRATCHA_REG_800	0x34	/* through 0x37 Scratch A rw */
/* NCR53c710 only */
#define SCRATCHB_REG_10		0x34	/* through 0x37 scratch B rw */

#define DMODE_REG_10    	0x38	/* DMA mode rw, NCR53c710 and newer */
#define DMODE_800_SIOM		0x20	/* Source IO = 1 */
#define DMODE_800_DIOM		0x10	/* Destination IO = 1 */
#define DMODE_800_ERL		0x08	/* Enable Read Line */

/* 35-38 are reserved on 700 and 700-66 series chips */
#define DIEN_REG		0x39	/* DMA interrupt enable rw */
/* 0x80, 0x40, and 0x20 are reserved on 700-series chips */
#define DIEN_800_MDPE		0x40	/* Master data parity error */
#define DIEN_800_BF		0x20	/* BUS fault */
#define DIEN_700_BF		0x20	/* BUS fault */
#define DIEN_ABRT		0x10	/* Enable aborted interrupt */
#define DIEN_SSI		0x08	/* Enable single step interrupt */
#define DIEN_SIR		0x04	/* Enable SCRIPTS INT command 
					 * interrupt
					 */
/* 0x02 is reserved on 800 series chips */
#define DIEN_700_WTD		0x02	/* Enable watchdog timeout interrupt */
#define DIEN_700_OPC		0x01	/* Enable illegal instruction 
					 * interrupt 
					 */
#define DIEN_800_IID		0x01	/*  Same meaning, different name */ 

/*
 * DMA watchdog timer rw
 * set in 16 CLK input periods.
 */
#define DWT_REG			0x3a

/* DMA control rw */
#define DCNTL_REG		0x3b
#define DCNTL_700_CF1		0x80	/* Clock divisor bits */
#define DCNTL_700_CF0		0x40
#define DCNTL_700_CF_MASK	0xc0
/* Clock divisors 			   Divisor SCLK range (MHZ) */
#define DCNTL_700_CF_2		0x00    /* 2.0	   37.51-50.00 */
#define DCNTL_700_CF_1_5	0x40	/* 1.5	   25.01-37.50 */
#define DCNTL_700_CF_1		0x80	/* 1.0     16.67-25.00 */
#define DCNTL_700_CF_3		0xc0	/* 3.0	   50.01-66.67 (53c700-66) */

#define DCNTL_700_S16		0x20	/* Load scripts 16 bits at a time */
#define DCNTL_SSM		0x10	/* Single step mode */
#define DCNTL_700_LLM		0x08	/* Low level mode, can only be set 
					 * after selection */
#define DCNTL_800_IRQM		0x08	/* Totem pole IRQ pin */
#define DCNTL_STD		0x04	/* Start DMA / SCRIPTS */
/* 0x02 is reserved */
#define DCNTL_00_RST		0x01	/* Software reset, resets everything
					 * but 286 mode bit  in DMODE. On the
					 * NCR53c710, this bit moved to CTEST8
					 */
#define DCNTL_10_COM		0x01	/* 700 software compatibility mode */
#define DCNTL_10_EA		0x20	/* Enable Ack - needed for MVME16x */

#define DCNTL_700_SAVE ( DCNTL_CF_MASK | DCNTL_S16)


/* NCR53c700-66 only */
#define SCRATCHB_REG_00		0x3c	/* through 0x3f scratch b rw */
#define SCRATCHB_REG_800	0x5c	/* through 0x5f scratch b rw */
/* NCR53c710 only */
#define ADDER_REG_10		0x3c	/* Adder, NCR53c710 only */

#define SIEN1_REG_800		0x41
#define SIEN1_800_STO		0x04	/* selection/reselection timeout */
#define SIEN1_800_GEN		0x02	/* general purpose timer */
#define SIEN1_800_HTH		0x01	/* handshake to handshake */

#define SIST1_REG_800		0x43
#define SIST1_800_STO		0x04	/* selection/reselection timeout */
#define SIST1_800_GEN		0x02	/* general purpose timer */
#define SIST1_800_HTH		0x01	/* handshake to handshake */

#define SLPAR_REG_800		0x44	/* Parity */

#define MACNTL_REG_800		0x46	/* Memory access control */
#define MACNTL_800_TYP3		0x80
#define MACNTL_800_TYP2		0x40
#define MACNTL_800_TYP1		0x20
#define MACNTL_800_TYP0		0x10
#define MACNTL_800_DWR		0x08
#define MACNTL_800_DRD		0x04
#define MACNTL_800_PSCPT	0x02
#define MACNTL_800_SCPTS	0x01

#define GPCNTL_REG_800		0x47	/* General Purpose Pin Control */

/* Timeouts are expressed such that 0=off, 1=100us, doubling after that */
#define STIME0_REG_800		0x48	/* SCSI Timer Register 0 */
#define STIME0_800_HTH_MASK	0xf0	/* Handshake to Handshake timeout */
#define STIME0_800_HTH_SHIFT	4
#define STIME0_800_SEL_MASK	0x0f	/* Selection timeout */
#define STIME0_800_SEL_SHIFT	0

#define STIME1_REG_800		0x49
#define STIME1_800_GEN_MASK	0x0f	/* General purpose timer */

#define RESPID_REG_800		0x4a	/* Response ID, bit fielded.  8
					   bits on narrow chips, 16 on WIDE */

#define STEST0_REG_800		0x4c	
#define STEST0_800_SLT		0x08	/* Selection response logic test */
#define STEST0_800_ART		0x04	/* Arbitration priority encoder test */
#define STEST0_800_SOZ		0x02	/* Synchronous offset zero */
#define STEST0_800_SOM		0x01	/* Synchronous offset maximum */

#define STEST1_REG_800		0x4d
#define STEST1_800_SCLK		0x80	/* Disable SCSI clock */

#define STEST2_REG_800		0x4e	
#define STEST2_800_SCE		0x80	/* Enable SOCL/SODL */
#define STEST2_800_ROF		0x40	/* Reset SCSI sync offset */
#define STEST2_800_SLB		0x10	/* Enable SCSI loopback mode */
#define STEST2_800_SZM		0x08	/* SCSI high impedance mode */
#define STEST2_800_EXT		0x02	/* Extend REQ/ACK filter 30 to 60ns */
#define STEST2_800_LOW		0x01	/* SCSI low level mode */

#define STEST3_REG_800		0x4f	 
#define STEST3_800_TE		0x80	/* Enable active negation */
#define STEST3_800_STR		0x40	/* SCSI FIFO test read */
#define STEST3_800_HSC		0x20	/* Halt SCSI clock */
#define STEST3_800_DSI		0x10	/* Disable single initiator response */
#define STEST3_800_TTM		0x04	/* Time test mode */
#define STEST3_800_CSF		0x02	/* Clear SCSI FIFO */
#define STEST3_800_STW		0x01	/* SCSI FIFO test write */

#define OPTION_PARITY 		0x1	/* Enable parity checking */
#define OPTION_TAGGED_QUEUE	0x2	/* Enable SCSI-II tagged queuing */
#define OPTION_700		0x8	/* Always run NCR53c700 scripts */
#define OPTION_INTFLY		0x10	/* Use INTFLY interrupts */
#define OPTION_DEBUG_INTR	0x20	/* Debug interrupts */
#define OPTION_DEBUG_INIT_ONLY	0x40	/* Run initialization code and 
					   simple test code, return
					   DID_NO_CONNECT if any SCSI
					   commands are attempted. */
#define OPTION_DEBUG_READ_ONLY	0x80	/* Return DID_ERROR if any 
					   SCSI write is attempted */
#define OPTION_DEBUG_TRACE	0x100	/* Animated trace mode, print 
					   each address and instruction 
					   executed to debug buffer. */
#define OPTION_DEBUG_SINGLE	0x200	/* stop after executing one 
					   instruction */
#define OPTION_SYNCHRONOUS	0x400	/* Enable sync SCSI.  */
#define OPTION_MEMORY_MAPPED	0x800	/* NCR registers have valid 
					   memory mapping */
#define OPTION_IO_MAPPED	0x1000  /* NCR registers have valid
					     I/O mapping */
#define OPTION_DEBUG_PROBE_ONLY	0x2000  /* Probe only, don't even init */
#define OPTION_DEBUG_TESTS_ONLY	0x4000  /* Probe, init, run selected tests */
#define OPTION_DEBUG_TEST0	0x08000 /* Run test 0 */
#define OPTION_DEBUG_TEST1	0x10000 /* Run test 1 */
#define OPTION_DEBUG_TEST2	0x20000 /* Run test 2 */
#define OPTION_DEBUG_DUMP	0x40000 /* Dump commands */
#define OPTION_DEBUG_TARGET_LIMIT 0x80000 /* Only talk to target+luns specified */
#define OPTION_DEBUG_NCOMMANDS_LIMIT 0x100000 /* Limit the number of commands */
#define OPTION_DEBUG_SCRIPT 0x200000 /* Print when checkpoints are passed */
#define OPTION_DEBUG_FIXUP 0x400000 /* print fixup values */
#define OPTION_DEBUG_DSA 0x800000
#define OPTION_DEBUG_CORRUPTION	0x1000000	/* Detect script corruption */
#define OPTION_DEBUG_SDTR       0x2000000	/* Debug SDTR problem */
#define OPTION_DEBUG_MISMATCH 	0x4000000 	/* Debug phase mismatches */
#define OPTION_DISCONNECT	0x8000000	/* Allow disconnect */
#define OPTION_DEBUG_DISCONNECT 0x10000000	
#define OPTION_ALWAYS_SYNCHRONOUS 0x20000000	/* Negotiate sync. transfers
						   on power up */
#define OPTION_DEBUG_QUEUES	0x80000000	
#define OPTION_DEBUG_ALLOCATION 0x100000000LL
#define OPTION_DEBUG_SYNCHRONOUS 0x200000000LL	/* Sanity check SXFER and 
						   SCNTL3 registers */
#define OPTION_NO_ASYNC	0x400000000LL		/* Don't automagically send
						   SDTR for async transfers when
						   we haven't been told to do
						   a synchronous transfer. */
#define OPTION_NO_PRINT_RACE 0x800000000LL	/* Don't print message when
						   the reselect/WAIT DISCONNECT
						   race condition hits */
#if !defined(PERM_OPTIONS)
#define PERM_OPTIONS 0
#endif
				
/*
 * Some data which is accessed by the NCR chip must be 4-byte aligned.
 * For some hosts the default is less than that (eg. 68K uses 2-byte).
 * Alignment has only been forced where it is important; also if one
 * 32 bit structure field is aligned then it is assumed that following
 * 32 bit fields are also aligned.  Take care when adding fields
 * which are other than 32 bit.
 */

struct NCR53c7x0_synchronous {
    u32 select_indirect			/* Value used for indirect selection */
	__attribute__ ((aligned (4)));
    u32 sscf_710;			/* Used to set SSCF bits for 710 */
    u32 script[8];			/* Size ?? Script used when target is 
						reselected */
    unsigned char synchronous_want[5];	/* Per target desired SDTR */
/* 
 * Set_synchronous programs these, select_indirect and current settings after
 * int_debug_should show a match.
 */
    unsigned char sxfer_sanity, scntl3_sanity;
};

#define CMD_FLAG_SDTR 		1	/* Initiating synchronous 
					   transfer negotiation */
#define CMD_FLAG_WDTR		2	/* Initiating wide transfer
					   negotiation */
#define CMD_FLAG_DID_SDTR	4	/* did SDTR */
#define CMD_FLAG_DID_WDTR	8	/* did WDTR */

struct NCR53c7x0_table_indirect {
    u32 count;
    void *address;
};

enum ncr_event { 
    EVENT_NONE = 0,
/* 
 * Order is IMPORTANT, since these must correspond to the event interrupts
 * in 53c7,8xx.scr 
 */

    EVENT_ISSUE_QUEUE = 0x5000000,	/* 0 Command was added to issue queue */
    EVENT_START_QUEUE,			/* 1 Command moved to start queue */
    EVENT_SELECT,			/* 2 Command completed selection */
    EVENT_DISCONNECT,			/* 3 Command disconnected */
    EVENT_RESELECT,			/* 4 Command reselected */
    EVENT_COMPLETE,		        /* 5 Command completed */
    EVENT_IDLE,				/* 6 */
    EVENT_SELECT_FAILED,		/* 7 */
    EVENT_BEFORE_SELECT,		/* 8 */
    EVENT_RESELECT_FAILED		/* 9 */
};

struct NCR53c7x0_event {
    enum ncr_event event;	/* What type of event */
    unsigned char target;
    unsigned char lun;
    struct timeval time;	
    u32 *dsa;			/* What's in the DSA register now (virt) */
/* 
 * A few things from that SCSI pid so we know what happened after 
 * the Scsi_Cmnd structure in question may have disappeared.
 */
    unsigned long pid;		/* The SCSI PID which caused this 
				   event */
    unsigned char cmnd[12];
};

/*
 * Things in the NCR53c7x0_cmd structure are split into two parts :
 *
 * 1.  A fixed portion, for things which are not accessed directly by static NCR
 *	code (ie, are referenced only by the Linux side of the driver,
 *	or only by dynamically generated code).  
 *
 * 2.  The DSA portion, for things which are accessed directly by static NCR
 *	code.
 *
 * This is a little ugly, but it 
 * 1.  Avoids conflicts between the NCR code's picture of the structure, and 
 * 	Linux code's idea of what it looks like.
 *
 * 2.  Minimizes the pain in the Linux side of the code needed 
 * 	to calculate real dsa locations for things, etc.
 * 
 */

struct NCR53c7x0_cmd {
    void *real;				/* Real, unaligned address for
					   free function */
    void (* free)(void *, int);		/* Command to deallocate; NULL
					   for structures allocated with
					   scsi_register, etc. */
    Scsi_Cmnd *cmd;			/* Associated Scsi_Cmnd 
					   structure, Scsi_Cmnd points
					   at NCR53c7x0_cmd using 
					   host_scribble structure */

    int size;				/* scsi_malloc'd size of this 
					   structure */

    int flags;				/* CMD_* flags */

    unsigned char      cmnd[12];	/* CDB, copied from Scsi_Cmnd */
    int                result;		/* Copy to Scsi_Cmnd when done */

    struct {				/* Private non-cached bounce buffer */
        unsigned char buf[256];
	u32	      addr;
        u32           len;
    } bounce;

/*
 * SDTR and WIDE messages are an either/or affair
 * in this message, since we will go into message out and send
 * _the whole mess_ without dropping out of message out to 
 * let the target go into message in after sending the first 
 * message.
 */

    unsigned char select[11];		/* Select message, includes
					   IDENTIFY
					   (optional) QUEUE TAG
 				 	   (optional) SDTR or WDTR
					 */


    volatile struct NCR53c7x0_cmd *next; /* Linux maintained lists (free,
					    running, eventually finished */
    					 

    u32 *data_transfer_start;		/* Start of data transfer routines */
    u32 *data_transfer_end;		/* Address after end of data transfer o
    	    	    	    	    	   routines */
/* 
 * The following three fields were moved from the DSA proper to here
 * since only dynamically generated NCR code refers to them, meaning
 * we don't need dsa_* absolutes, and it is simpler to let the 
 * host code refer to them directly.
 */

/* 
 * HARD CODED : residual and saved_residual need to agree with the sizes
 * used in NCR53c7,8xx.scr.  
 * 
 * FIXME: we want to consider the case where we have odd-length 
 *	scatter/gather buffers and a WIDE transfer, in which case 
 *	we'll need to use the CHAIN MOVE instruction.  Ick.
 */
    u32 residual[6] __attribute__ ((aligned (4)));
					/* Residual data transfer which
					   allows pointer code to work
					   right.

    	    	    	    	    	    [0-1] : Conditional call to 
    	    	    	    	    	    	appropriate other transfer 
    	    	    	    	    	    	routine.
    	    	    	    	    	    [2-3] : Residual block transfer
    	    	    	    	    	    	instruction.
    	    	    	    	    	    [4-5] : Jump to instruction
    	    	    	    	    	    	after splice.
					 */
    u32 saved_residual[6]; 		/* Copy of old residual, so we 
					   can get another partial 
					   transfer and still recover 
    	    	    	    	    	 */
    	    	
    u32 saved_data_pointer;		/* Saved data pointer */

    u32 dsa_next_addr;		        /* _Address_ of dsa_next field  
					   in this dsa for RISCy 
					   style constant. */

    u32 dsa_addr;			/* Address of dsa; RISCy style
					   constant */

    u32 dsa[0];				/* Variable length (depending
					   on host type, number of scatter /
					   gather buffers, etc).  */
};

struct NCR53c7x0_break {
    u32 *address, old_instruction[2];
    struct NCR53c7x0_break *next;
    unsigned char old_size;		/* Size of old instruction */
};

/* Indicates that the NCR is not executing code */
#define STATE_HALTED	0		
/* 
 * Indicates that the NCR is executing the wait for select / reselect 
 * script.  Only used when running NCR53c700 compatible scripts, only 
 * state during which an ABORT is _not_ considered an error condition.
 */
#define STATE_WAITING	1		
/* Indicates that the NCR is executing other code. */
#define STATE_RUNNING	2		
/* 
 * Indicates that the NCR was being aborted.
 */
#define STATE_ABORTING	3
/* Indicates that the NCR was successfully aborted. */
#define STATE_ABORTED 4
/* Indicates that the NCR has been disabled due to a fatal error */
#define STATE_DISABLED 5

/* 
 * Where knowledge of SCSI SCRIPT(tm) specified values are needed 
 * in an interrupt handler, an interrupt handler exists for each 
 * different SCSI script so we don't have name space problems.
 * 
 * Return values of these handlers are as follows : 
 */
#define SPECIFIC_INT_NOTHING 	0	/* don't even restart */
#define SPECIFIC_INT_RESTART	1	/* restart at the next instruction */
#define SPECIFIC_INT_ABORT	2	/* recoverable error, abort cmd */
#define SPECIFIC_INT_PANIC	3	/* unrecoverable error, panic */
#define SPECIFIC_INT_DONE	4	/* normal command completion */
#define SPECIFIC_INT_BREAK	5	/* break point encountered */

struct NCR53c7x0_hostdata {
    int size;				/* Size of entire Scsi_Host
					   structure */
    int board;				/* set to board type, useful if 
					   we have host specific things,
					   ie, a general purpose I/O 
					   bit is being used to enable
					   termination, etc. */

    int chip;				/* set to chip type; 700-66 is
					   700-66, rest are last three
					   digits of part number */

    char valid_ids[8];			/* Valid SCSI ID's for adapter */

    u32 *dsp;				/* dsp to restart with after
					   all stacked interrupts are
					   handled. */

    unsigned dsp_changed:1;		/* Has dsp changed within this
					   set of stacked interrupts ? */

    unsigned char dstat;		/* Most recent value of dstat */
    unsigned dstat_valid:1;

    unsigned expecting_iid:1;		/* Expect IID interrupt */
    unsigned expecting_sto:1;		/* Expect STO interrupt */
    
    /* 
     * The code stays cleaner if we use variables with function
     * pointers and offsets that are unique for the different
     * scripts rather than having a slew of switch(hostdata->chip) 
     * statements.
     * 
     * It also means that the #defines from the SCSI SCRIPTS(tm)
     * don't have to be visible outside of the script-specific
     * instructions, preventing name space pollution.
     */

    void (* init_fixup)(struct Scsi_Host *host);
    void (* init_save_regs)(struct Scsi_Host *host);
    void (* dsa_fixup)(struct NCR53c7x0_cmd *cmd);
    void (* soft_reset)(struct Scsi_Host *host);
    int (* run_tests)(struct Scsi_Host *host);

    /*
     * Called when DSTAT_SIR is set, indicating an interrupt generated
     * by the INT instruction, where values are unique for each SCSI
     * script.  Should return one of the SPEC_* values.
     */

    int (* dstat_sir_intr)(struct Scsi_Host *host, struct NCR53c7x0_cmd *cmd);

    int dsa_len; /* Size of DSA structure */

    /*
     * Location of DSA fields for the SCSI SCRIPT corresponding to this 
     * chip.  
     */

    s32 dsa_start;			
    s32 dsa_end;			
    s32 dsa_next;
    s32 dsa_prev;
    s32 dsa_cmnd;
    s32 dsa_select;
    s32 dsa_msgout;
    s32 dsa_cmdout;
    s32 dsa_dataout;
    s32 dsa_datain;
    s32 dsa_msgin;
    s32 dsa_msgout_other;
    s32 dsa_write_sync;
    s32 dsa_write_resume;
    s32 dsa_check_reselect;
    s32 dsa_status;
    s32 dsa_saved_pointer;
    s32 dsa_jump_dest;

    /* 
     * Important entry points that generic fixup code needs
     * to know about, fixed up.
     */

    s32 E_accept_message;
    s32 E_command_complete;		
    s32 E_data_transfer;
    s32 E_dsa_code_template;
    s32 E_dsa_code_template_end;
    s32 E_end_data_transfer;
    s32 E_msg_in;
    s32 E_initiator_abort;
    s32 E_other_transfer;
    s32 E_other_in;
    s32 E_other_out;
    s32 E_target_abort;
    s32 E_debug_break;	
    s32 E_reject_message;
    s32 E_respond_message;
    s32 E_select;
    s32 E_select_msgout;
    s32 E_test_0;
    s32 E_test_1;
    s32 E_test_2;
    s32 E_test_3;
    s32 E_dsa_zero;
    s32 E_cmdout_cmdout;
    s32 E_wait_reselect;
    s32 E_dsa_code_begin;

    long long options;			/* Bitfielded set of options enabled */
    volatile u32 test_completed;	/* Test completed */
    int test_running;			/* Test currently running */
    s32 test_source
	__attribute__ ((aligned (4)));
    volatile s32 test_dest;

    volatile int state;			/* state of driver, only used for 
					   OPTION_700 */

    unsigned char  dmode;		/* 
					 * set to the address of the DMODE 
					 * register for this chip.
					 */
    unsigned char istat;		/* 
    	    	    	    	    	 * set to the address of the ISTAT 
    	    	    	    	    	 * register for this chip.
    	    	    	    	    	 */
  
    int scsi_clock;			/* 
					 * SCSI clock in HZ. 0 may be used 
					 * for unknown, although this will
					 * disable synchronous negotiation.
					 */

    volatile int intrs;			/* Number of interrupts */
    volatile int resets;		/* Number of SCSI resets */
    unsigned char saved_dmode;	
    unsigned char saved_ctest4;
    unsigned char saved_ctest7;
    unsigned char saved_dcntl;
    unsigned char saved_scntl3;

    unsigned char this_id_mask;

    /* Debugger information */
    struct NCR53c7x0_break *breakpoints, /* Linked list of all break points */
	*breakpoint_current;		/* Current breakpoint being stepped 
					   through, NULL if we are running 
					   normally. */
#ifdef NCR_DEBUG
    int debug_size;			/* Size of debug buffer */
    volatile int debug_count;		/* Current data count */
    volatile char *debug_buf;		/* Output ring buffer */
    volatile char *debug_write;		/* Current write pointer */
    volatile char *debug_read;		/* Current read pointer */
#endif /* def NCR_DEBUG */

    /* XXX - primitive debugging junk, remove when working ? */
    int debug_print_limit;		/* Number of commands to print
					   out exhaustive debugging
					   information for if 
					   OPTION_DEBUG_DUMP is set */ 

    unsigned char debug_lun_limit[16];	/* If OPTION_DEBUG_TARGET_LIMIT
					   set, puke if commands are sent
					   to other target/lun combinations */

    int debug_count_limit;		/* Number of commands to execute
					   before puking to limit debugging 
					   output */
				    

    volatile unsigned idle:1;			/* set to 1 if idle */

    /* 
     * Table of synchronous+wide transfer parameters set on a per-target
     * basis.
     */
    
    volatile struct NCR53c7x0_synchronous sync[16]
	__attribute__ ((aligned (4)));

    volatile Scsi_Cmnd *issue_queue
	__attribute__ ((aligned (4)));
						/* waiting to be issued by
						   Linux driver */
    volatile struct NCR53c7x0_cmd *running_list;	
						/* commands running, maintained
						   by Linux driver */

    volatile struct NCR53c7x0_cmd *ncrcurrent;	/* currently connected 
						   nexus, ONLY valid for
						   NCR53c700/NCR53c700-66
						 */

    volatile struct NCR53c7x0_cmd *spare;	/* pointer to spare,
    	    	    	    	    	    	   allocated at probe time,
    	    	    	    	    	    	   which we can use for 
						   initialization */
    volatile struct NCR53c7x0_cmd *free;
    int max_cmd_size;				/* Maximum size of NCR53c7x0_cmd
					    	   based on number of 
						   scatter/gather segments, etc.
						   */
    volatile int num_cmds;			/* Number of commands 
						   allocated */
    volatile int extra_allocate;
    volatile unsigned char cmd_allocated[16];	/* Have we allocated commands
						   for this target yet?  If not,
						   do so ASAP */
    volatile unsigned char busy[16][8];     	/* number of commands 
						   executing on each target
    	    	    	    	    	    	 */
    /* 
     * Eventually, I'll switch to a coroutine for calling 
     * cmd->done(cmd), etc. so that we can overlap interrupt
     * processing with this code for maximum performance.
     */
    
    volatile struct NCR53c7x0_cmd *finished_queue;	
						
    /* Shared variables between SCRIPT and host driver */
    volatile u32 *schedule
	__attribute__ ((aligned (4)));		/* Array of JUMPs to dsa_begin
						   routines of various DSAs.  
						   When not in use, replace
						   with jump to next slot */


    volatile unsigned char msg_buf[16];		/* buffer for messages
						   other than the command
						   complete message */

    /* Per-target default synchronous and WIDE messages */
    volatile unsigned char synchronous_want[16][5];
    volatile unsigned char wide_want[16][4];

    /* Bit fielded set of targets we want to speak synchronously with */ 
    volatile u16 initiate_sdtr;	
    /* Bit fielded set of targets we want to speak wide with */
    volatile u16 initiate_wdtr;
    /* Bit fielded list of targets we've talked to. */
    volatile u16 talked_to;

    /* Array of bit-fielded lun lists that we need to request_sense */
    volatile unsigned char request_sense[16];

    u32 addr_reconnect_dsa_head
	__attribute__ ((aligned (4)));		/* RISCy style constant,
						   address of following */
    volatile u32 reconnect_dsa_head;	
    /* Data identifying nexus we are trying to match during reselection */
    volatile unsigned char reselected_identify; /* IDENTIFY message */
    volatile unsigned char reselected_tag;	/* second byte of queue tag 
						   message or 0 */

    /* These were static variables before we moved them */

    s32 NCR53c7xx_zero
	__attribute__ ((aligned (4)));
    s32 NCR53c7xx_sink;
    u32 NOP_insn;
    char NCR53c7xx_msg_reject;
    char NCR53c7xx_msg_abort;
    char NCR53c7xx_msg_nop;

    /*
     * Following item introduced by RGH to support NCRc710, which is
     * VERY brain-dead when it come to memory moves
     */

			  /* DSA save area used only by the NCR chip */
    volatile unsigned long saved2_dsa
	__attribute__ ((aligned (4)));

    volatile unsigned long emulated_intfly
	__attribute__ ((aligned (4)));

    volatile int event_size, event_index;
    volatile struct NCR53c7x0_event *events;

    /* If we need to generate code to kill off the currently connected 
       command, this is where we do it. Should have a BMI instruction
       to source or sink the current data, followed by a JUMP
       to abort_connected */

    u32 *abort_script;

    int script_count;				/* Size of script in words */
    u32 script[0];				/* Relocated SCSI script */

};

#define SCSI_IRQ_NONE	255
#define DMA_NONE	255
#define IRQ_AUTO	254
#define DMA_AUTO	254

#define BOARD_GENERIC	0

#define NCR53c7x0_insn_size(insn)					\
    (((insn) & DCMD_TYPE_MASK) == DCMD_TYPE_MMI ? 3 : 2)
    

#define NCR53c7x0_local_declare()					\
    volatile unsigned char *NCR53c7x0_address_memory;			\
    unsigned int NCR53c7x0_address_io;					\
    int NCR53c7x0_memory_mapped

#define NCR53c7x0_local_setup(host)					\
    NCR53c7x0_address_memory = (void *) (host)->base;			\
    NCR53c7x0_address_io = (unsigned int) (host)->io_port;		\
    NCR53c7x0_memory_mapped = ((struct NCR53c7x0_hostdata *) 		\
	host->hostdata[0])-> options & OPTION_MEMORY_MAPPED 

#ifdef BIG_ENDIAN
/* These could be more efficient, given that we are always memory mapped,
 * but they don't give the same problems as the write macros, so leave
 * them. */
#ifdef __mc68000__
#define NCR53c7x0_read8(address) 					\
    ((unsigned int)raw_inb((u32)NCR53c7x0_address_memory + ((u32)(address)^3)) )

#define NCR53c7x0_read16(address) 					\
    ((unsigned int)raw_inw((u32)NCR53c7x0_address_memory + ((u32)(address)^2)))
#else
#define NCR53c7x0_read8(address) 					\
    (NCR53c7x0_memory_mapped ? 						\
	(unsigned int)readb((u32)NCR53c7x0_address_memory + ((u32)(address)^3)) :	\
	inb(NCR53c7x0_address_io + (address)))

#define NCR53c7x0_read16(address) 					\
    (NCR53c7x0_memory_mapped ? 						\
	(unsigned int)readw((u32)NCR53c7x0_address_memory + ((u32)(address)^2)) :	\
	inw(NCR53c7x0_address_io + (address)))
#endif /* mc68000 */
#else
#define NCR53c7x0_read8(address) 					\
    (NCR53c7x0_memory_mapped ? 						\
	(unsigned int)readb((u32)NCR53c7x0_address_memory + (u32)(address)) :	\
	inb(NCR53c7x0_address_io + (address)))

#define NCR53c7x0_read16(address) 					\
    (NCR53c7x0_memory_mapped ? 						\
	(unsigned int)readw((u32)NCR53c7x0_address_memory + (u32)(address)) :	\
	inw(NCR53c7x0_address_io + (address)))
#endif

#ifdef __mc68000__
#define NCR53c7x0_read32(address) 					\
    ((unsigned int) raw_inl((u32)NCR53c7x0_address_memory + (u32)(address)))
#else
#define NCR53c7x0_read32(address) 					\
    (NCR53c7x0_memory_mapped ? 						\
	(unsigned int) readl((u32)NCR53c7x0_address_memory + (u32)(address)) : 	\
	inl(NCR53c7x0_address_io + (address)))
#endif /* mc68000*/

#ifdef BIG_ENDIAN
/* If we are big-endian, then we are not Intel, so probably don't have
 * an i/o map as well as a memory map.  So, let's assume memory mapped.
 * Also, I am having terrible problems trying to persuade the compiler
 * not to lay down code which does a read after write for these macros.
 * If you remove 'volatile' from writeb() and friends it is ok....
 */

#define NCR53c7x0_write8(address,value) 				\
	*(volatile unsigned char *)					\
		((u32)NCR53c7x0_address_memory + ((u32)(address)^3)) = (value)

#define NCR53c7x0_write16(address,value) 				\
	*(volatile unsigned short *)					\
		((u32)NCR53c7x0_address_memory + ((u32)(address)^2)) = (value)

#define NCR53c7x0_write32(address,value) 				\
	*(volatile unsigned long *)					\
		((u32)NCR53c7x0_address_memory + ((u32)(address))) = (value)

#else

#define NCR53c7x0_write8(address,value) 				\
    (NCR53c7x0_memory_mapped ? 						\
     ({writeb((value), (u32)NCR53c7x0_address_memory + (u32)(address)); mb();}) :	\
	outb((value), NCR53c7x0_address_io + (address)))

#define NCR53c7x0_write16(address,value) 				\
    (NCR53c7x0_memory_mapped ? 						\
     ({writew((value), (u32)NCR53c7x0_address_memory + (u32)(address)); mb();}) :	\
	outw((value), NCR53c7x0_address_io + (address)))

#define NCR53c7x0_write32(address,value) 				\
    (NCR53c7x0_memory_mapped ? 						\
     ({writel((value), (u32)NCR53c7x0_address_memory + (u32)(address)); mb();}) :	\
	outl((value), NCR53c7x0_address_io + (address)))

#endif

/* Patch arbitrary 32 bit words in the script */
#define patch_abs_32(script, offset, symbol, value)			\
    	for (i = 0; i < (sizeof (A_##symbol##_used) / sizeof 		\
    	    (u32)); ++i) {					\
	    (script)[A_##symbol##_used[i] - (offset)] += (value);	\
	    if (hostdata->options & OPTION_DEBUG_FIXUP) 		\
	      printk("scsi%d : %s reference %d at 0x%x in %s is now 0x%x\n",\
		host->host_no, #symbol, i, A_##symbol##_used[i] - 	\
		(int)(offset), #script, (script)[A_##symbol##_used[i] -	\
		(offset)]);						\
    	}

/* Patch read/write instruction immediate field */
#define patch_abs_rwri_data(script, offset, symbol, value)		\
    	for (i = 0; i < (sizeof (A_##symbol##_used) / sizeof 		\
    	    (u32)); ++i)					\
    	    (script)[A_##symbol##_used[i] - (offset)] =			\
	    	((script)[A_##symbol##_used[i] - (offset)] & 		\
	    	~DBC_RWRI_IMMEDIATE_MASK) | 				\
    	    	(((value) << DBC_RWRI_IMMEDIATE_SHIFT) &		\
		 DBC_RWRI_IMMEDIATE_MASK)

/* Patch transfer control instruction data field */
#define patch_abs_tci_data(script, offset, symbol, value)	        \
    	for (i = 0; i < (sizeof (A_##symbol##_used) / sizeof 		\
    	    (u32)); ++i)					\
    	    (script)[A_##symbol##_used[i] - (offset)] =			\
	    	((script)[A_##symbol##_used[i] - (offset)] & 		\
	    	~DBC_TCI_DATA_MASK) | 					\
    	    	(((value) << DBC_TCI_DATA_SHIFT) &			\
		 DBC_TCI_DATA_MASK)

/* Patch field in dsa structure (assignment should be +=?) */
#define patch_dsa_32(dsa, symbol, word, value)				\
	{								\
	(dsa)[(hostdata->##symbol - hostdata->dsa_start) / sizeof(u32)	\
	    + (word)] = (value);					\
	if (hostdata->options & OPTION_DEBUG_DSA)			\
	    printk("scsi : dsa %s symbol %s(%d) word %d now 0x%x\n",	\
		#dsa, #symbol, hostdata->##symbol, 			\
		(word), (u32) (value));					\
	}

/* Paranoid people could use panic() here. */
#define FATAL(host) shutdown((host));

extern int ncr53c7xx_init(Scsi_Host_Template *tpnt, int board, int chip,
			  unsigned long base, int io_port, int irq, int dma,
			  long long options, int clock);

#endif /* NCR53c710_C */
#endif /* NCR53c710_H */
