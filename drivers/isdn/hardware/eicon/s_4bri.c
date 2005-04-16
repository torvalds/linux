
/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    2.1
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include "platform.h"
#include "di_defs.h"
#include "pc.h"
#include "pr_pc.h"
#include "di.h"
#include "mi_pc.h"
#include "pc_maint.h"
#include "divasync.h"
#include "pc_init.h"
#include "io.h"
#include "helpers.h"
#include "dsrv4bri.h"
#include "dsp_defs.h"
#include "sdp_hdr.h"

/*****************************************************************************/
#define	MAX_XLOG_SIZE	(64 * 1024)

/* --------------------------------------------------------------------------
		Recovery XLOG from QBRI Card
	 -------------------------------------------------------------------------- */
static void qBri_cpu_trapped (PISDN_ADAPTER IoAdapter) {
	byte  __iomem *base ;
	word *Xlog ;
	dword   regs[4], TrapID, offset, size ;
	Xdesc   xlogDesc ;
	int factor = (IoAdapter->tasks == 1) ? 1 : 2;

/*
 *	check for trapped MIPS 46xx CPU, dump exception frame
 */

	base = DIVA_OS_MEM_ATTACH_CONTROL(IoAdapter);
	offset = IoAdapter->ControllerNumber * (IoAdapter->MemorySize >> factor) ;

	TrapID = READ_DWORD(&base[0x80]) ;

	if ( (TrapID == 0x99999999) || (TrapID == 0x99999901) )
	{
		dump_trap_frame (IoAdapter, &base[0x90]) ;
		IoAdapter->trapped = 1 ;
	}

	regs[0] = READ_DWORD((base + offset) + 0x70);
	regs[1] = READ_DWORD((base + offset) + 0x74);
	regs[2] = READ_DWORD((base + offset) + 0x78);
	regs[3] = READ_DWORD((base + offset) + 0x7c);
	regs[0] &= IoAdapter->MemorySize - 1 ;

	if ( (regs[0] >= offset)
	  && (regs[0] < offset + (IoAdapter->MemorySize >> factor) - 1) )
	{
		if ( !(Xlog = (word *)diva_os_malloc (0, MAX_XLOG_SIZE)) ) {
			DIVA_OS_MEM_DETACH_CONTROL(IoAdapter, base);
			return ;
		}

		size = offset + (IoAdapter->MemorySize >> factor) - regs[0] ;
		if ( size > MAX_XLOG_SIZE )
			size = MAX_XLOG_SIZE ;
		memcpy_fromio (Xlog, &base[regs[0]], size) ;
		xlogDesc.buf = Xlog ;
		xlogDesc.cnt = READ_WORD(&base[regs[1] & (IoAdapter->MemorySize - 1)]) ;
		xlogDesc.out = READ_WORD(&base[regs[2] & (IoAdapter->MemorySize - 1)]) ;
		dump_xlog_buffer (IoAdapter, &xlogDesc) ;
		diva_os_free (0, Xlog) ;
		IoAdapter->trapped = 2 ;
	}
	DIVA_OS_MEM_DETACH_CONTROL(IoAdapter, base);
}

/* --------------------------------------------------------------------------
		Reset QBRI Hardware
	 -------------------------------------------------------------------------- */
static void reset_qBri_hardware (PISDN_ADAPTER IoAdapter) {
	word volatile __iomem *qBriReset ;
	byte  volatile __iomem *qBriCntrl ;
	byte  volatile __iomem *p ;

	qBriReset = (word volatile __iomem *)DIVA_OS_MEM_ATTACH_PROM(IoAdapter);
	WRITE_WORD(qBriReset, READ_WORD(qBriReset) | PLX9054_SOFT_RESET) ;
	diva_os_wait (1) ;
	WRITE_WORD(qBriReset, READ_WORD(qBriReset) & ~PLX9054_SOFT_RESET) ;
	diva_os_wait (1);
	WRITE_WORD(qBriReset, READ_WORD(qBriReset) | PLX9054_RELOAD_EEPROM) ;
	diva_os_wait (1) ;
	WRITE_WORD(qBriReset, READ_WORD(qBriReset) & ~PLX9054_RELOAD_EEPROM) ;
	diva_os_wait (1);
	DIVA_OS_MEM_DETACH_PROM(IoAdapter, qBriReset);

	qBriCntrl = DIVA_OS_MEM_ATTACH_CTLREG(IoAdapter);
	p = &qBriCntrl[DIVA_4BRI_REVISION(IoAdapter) ? (MQ2_BREG_RISC) : (MQ_BREG_RISC)];
	WRITE_DWORD(p, 0) ;
	DIVA_OS_MEM_DETACH_CTLREG(IoAdapter, qBriCntrl);

	DBG_TRC(("resetted board @ reset addr 0x%08lx", qBriReset))
	DBG_TRC(("resetted board @ cntrl addr 0x%08lx", p))
}

/* --------------------------------------------------------------------------
		Start Card CPU
	 -------------------------------------------------------------------------- */
void start_qBri_hardware (PISDN_ADAPTER IoAdapter) {
	byte volatile __iomem *qBriReset ;
	byte volatile __iomem *p ;

	p = DIVA_OS_MEM_ATTACH_CTLREG(IoAdapter);
	qBriReset = &p[(DIVA_4BRI_REVISION(IoAdapter)) ? (MQ2_BREG_RISC) : (MQ_BREG_RISC)];
	WRITE_DWORD(qBriReset, MQ_RISC_COLD_RESET_MASK) ;
	diva_os_wait (2) ;
	WRITE_DWORD(qBriReset, MQ_RISC_WARM_RESET_MASK | MQ_RISC_COLD_RESET_MASK) ;
	diva_os_wait (10) ;
	DIVA_OS_MEM_DETACH_CTLREG(IoAdapter, p);

	DBG_TRC(("started processor @ addr 0x%08lx", qBriReset))
}

/* --------------------------------------------------------------------------
		Stop Card CPU
	 -------------------------------------------------------------------------- */
static void stop_qBri_hardware (PISDN_ADAPTER IoAdapter) {
	byte volatile __iomem *p ;
	dword volatile __iomem *qBriReset ;
	dword volatile __iomem *qBriIrq ;
	dword volatile __iomem *qBriIsacDspReset ;
	int rev2 = DIVA_4BRI_REVISION(IoAdapter);
	int reset_offset = rev2 ? (MQ2_BREG_RISC)      : (MQ_BREG_RISC);
	int irq_offset   = rev2 ? (MQ2_BREG_IRQ_TEST)  : (MQ_BREG_IRQ_TEST);
	int hw_offset    = rev2 ? (MQ2_ISAC_DSP_RESET) : (MQ_ISAC_DSP_RESET);

	if ( IoAdapter->ControllerNumber > 0 )
		return ;
	p = DIVA_OS_MEM_ATTACH_CTLREG(IoAdapter);
	qBriReset = (dword volatile __iomem *)&p[reset_offset];
	qBriIsacDspReset = (dword volatile __iomem *)&p[hw_offset];
/*
 *	clear interrupt line (reset Local Interrupt Test Register)
 */
	WRITE_DWORD(qBriReset, 0) ;
 	WRITE_DWORD(qBriIsacDspReset, 0) ;
	DIVA_OS_MEM_DETACH_CTLREG(IoAdapter, p);
	
	p = DIVA_OS_MEM_ATTACH_RESET(IoAdapter);
	WRITE_BYTE(&p[PLX9054_INTCSR], 0x00);	/* disable PCI interrupts */
	DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);
	
	p = DIVA_OS_MEM_ATTACH_CTLREG(IoAdapter);
	qBriIrq   = (dword volatile __iomem *)&p[irq_offset];
	WRITE_DWORD(qBriIrq, MQ_IRQ_REQ_OFF) ;
	DIVA_OS_MEM_DETACH_CTLREG(IoAdapter, p);

	DBG_TRC(("stopped processor @ addr 0x%08lx", qBriReset))

}

/* --------------------------------------------------------------------------
		FPGA download
	 -------------------------------------------------------------------------- */
#define FPGA_NAME_OFFSET         0x10

static byte * qBri_check_FPGAsrc (PISDN_ADAPTER IoAdapter, char *FileName,
                                  dword *Length, dword *code) {
	byte *File ;
	char  *fpgaFile, *fpgaType, *fpgaDate, *fpgaTime ;
	dword  fpgaFlen,  fpgaTlen,  fpgaDlen, cnt, year, i ;

	if (!(File = (byte *)xdiLoadFile (FileName, Length, 0))) {
		return (NULL) ;
	}
/*
 *	 scan file until FF and put id string into buffer
 */
	for ( i = 0 ; File[i] != 0xff ; )
	{
		if ( ++i >= *Length )
		{
			DBG_FTL(("FPGA download: start of data header not found"))
			xdiFreeFile (File) ;
			return (NULL) ;
		}
	}
	*code = i++ ;

	if ( (File[i] & 0xF0) != 0x20 )
	{
		DBG_FTL(("FPGA download: data header corrupted"))
		xdiFreeFile (File) ;
		return (NULL) ;
	}
	fpgaFlen = (dword)  File[FPGA_NAME_OFFSET - 1] ;
	if ( fpgaFlen == 0 )
		fpgaFlen = 12 ;
	fpgaFile = (char *)&File[FPGA_NAME_OFFSET] ;
	fpgaTlen = (dword)  fpgaFile[fpgaFlen + 2] ;
	if ( fpgaTlen == 0 )
		fpgaTlen = 10 ;
	fpgaType = (char *)&fpgaFile[fpgaFlen + 3] ;
	fpgaDlen = (dword)  fpgaType[fpgaTlen + 2] ;
	if ( fpgaDlen == 0 )
		fpgaDlen = 11 ;
	fpgaDate = (char *)&fpgaType[fpgaTlen + 3] ;
	fpgaTime = (char *)&fpgaDate[fpgaDlen + 3] ;
	cnt = (dword)(((File[  i  ] & 0x0F) << 20) + (File[i + 1] << 12)
	             + (File[i + 2]         <<  4) + (File[i + 3] >>  4)) ;

	if ( (dword)(i + (cnt / 8)) > *Length )
	{
		DBG_FTL(("FPGA download: '%s' file too small (%ld < %ld)",
		         FileName, *Length, code + ((cnt + 7) / 8) ))
		xdiFreeFile (File) ;
		return (NULL) ;
	}
	i = 0 ;
	do
	{
		while ( (fpgaDate[i] != '\0')
		     && ((fpgaDate[i] < '0') || (fpgaDate[i] > '9')) )
		{
			i++;
		}
		year = 0 ;
		while ( (fpgaDate[i] >= '0') && (fpgaDate[i] <= '9') )
			year = year * 10 + (fpgaDate[i++] - '0') ;
	} while ( (year < 2000) && (fpgaDate[i] != '\0') );

	switch (IoAdapter->cardType) {
		case CARDTYPE_DIVASRV_B_2F_PCI:
			break;

		default:
	    if ( year >= 2001 ) {
				IoAdapter->fpga_features |= PCINIT_FPGA_PLX_ACCESS_SUPPORTED ;
			}
	}

	DBG_LOG(("FPGA[%s] file %s (%s %s) len %d",
	         fpgaType, fpgaFile, fpgaDate, fpgaTime, cnt))
	return (File) ;
}

/******************************************************************************/

#define FPGA_PROG   0x0001		/* PROG enable low */
#define FPGA_BUSY   0x0002		/* BUSY high, DONE low */
#define	FPGA_CS     0x000C		/* Enable I/O pins */
#define FPGA_CCLK   0x0100
#define FPGA_DOUT   0x0400
#define FPGA_DIN    FPGA_DOUT   /* bidirectional I/O */

int qBri_FPGA_download (PISDN_ADAPTER IoAdapter) {
	int            bit ;
	byte           *File ;
	dword          code, FileLength ;
	word volatile __iomem *addr = (word volatile __iomem *)DIVA_OS_MEM_ATTACH_PROM(IoAdapter);
	word           val, baseval = FPGA_CS | FPGA_PROG ;



	if (DIVA_4BRI_REVISION(IoAdapter))
	{
		char* name;

		switch (IoAdapter->cardType) {
			case CARDTYPE_DIVASRV_B_2F_PCI:
				name = "dsbri2f.bit";
				break;

			case CARDTYPE_DIVASRV_B_2M_V2_PCI:
			case CARDTYPE_DIVASRV_VOICE_B_2M_V2_PCI:
				name = "dsbri2m.bit";
				break;

			default:
				name = "ds4bri2.bit";
		}

		File = qBri_check_FPGAsrc (IoAdapter, name,
	                           		&FileLength, &code);
	}
	else
	{
		File = qBri_check_FPGAsrc (IoAdapter, "ds4bri.bit",
		                           &FileLength, &code) ;
	}
	if ( !File ) {
		DIVA_OS_MEM_DETACH_PROM(IoAdapter, addr);
		return (0) ;
	}
/*
 *	prepare download, pulse PROGRAM pin down.
 */
	WRITE_WORD(addr, baseval & ~FPGA_PROG) ; /* PROGRAM low pulse */
	WRITE_WORD(addr, baseval) ;              /* release */
	diva_os_wait (50) ;  /* wait until FPGA finished internal memory clear */
/*
 *	check done pin, must be low
 */
	if ( READ_WORD(addr) & FPGA_BUSY )
	{
		DBG_FTL(("FPGA download: acknowledge for FPGA memory clear missing"))
		xdiFreeFile (File) ;
		DIVA_OS_MEM_DETACH_PROM(IoAdapter, addr);
		return (0) ;
	}
/*
 *	put data onto the FPGA
 */
	while ( code < FileLength )
	{
		val = ((word)File[code++]) << 3 ;

		for ( bit = 8 ; bit-- > 0 ; val <<= 1 ) /* put byte onto FPGA */
		{
			baseval &= ~FPGA_DOUT ;             /* clr  data bit */
			baseval |= (val & FPGA_DOUT) ;      /* copy data bit */
			WRITE_WORD(addr, baseval) ;
			WRITE_WORD(addr, baseval | FPGA_CCLK) ;     /* set CCLK hi */
			WRITE_WORD(addr, baseval | FPGA_CCLK) ;     /* set CCLK hi */
			WRITE_WORD(addr, baseval) ;                 /* set CCLK lo */
		}
	}
	xdiFreeFile (File) ;
	diva_os_wait (100) ;
	val = READ_WORD(addr) ;

	DIVA_OS_MEM_DETACH_PROM(IoAdapter, addr);

	if ( !(val & FPGA_BUSY) )
	{
		DBG_FTL(("FPGA download: chip remains in busy state (0x%04x)", val))
		return (0) ;
	}

	return (1) ;
}

static int load_qBri_hardware (PISDN_ADAPTER IoAdapter) {
	return (0);
}

/* --------------------------------------------------------------------------
		Card ISR
	 -------------------------------------------------------------------------- */
static int qBri_ISR (struct _ISDN_ADAPTER* IoAdapter) {
	dword volatile     __iomem *qBriIrq ;

	PADAPTER_LIST_ENTRY QuadroList = IoAdapter->QuadroList ;

	word              	i ;
	int             	serviced = 0 ;
	byte __iomem *p;

	p = DIVA_OS_MEM_ATTACH_RESET(IoAdapter);

	if ( !(READ_BYTE(&p[PLX9054_INTCSR]) & 0x80) ) {
		DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);
		return (0) ;
	}
	DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);

/*
 *	clear interrupt line (reset Local Interrupt Test Register)
 */
	p = DIVA_OS_MEM_ATTACH_CTLREG(IoAdapter);
	qBriIrq = (dword volatile __iomem *)(&p[DIVA_4BRI_REVISION(IoAdapter) ? (MQ2_BREG_IRQ_TEST)  : (MQ_BREG_IRQ_TEST)]);
	WRITE_DWORD(qBriIrq, MQ_IRQ_REQ_OFF) ;
	DIVA_OS_MEM_DETACH_CTLREG(IoAdapter, p);

	for ( i = 0 ; i < IoAdapter->tasks; ++i )
	{
		IoAdapter = QuadroList->QuadroAdapter[i] ;

		if ( IoAdapter && IoAdapter->Initialized
		  && IoAdapter->tst_irq (&IoAdapter->a) )
		{
			IoAdapter->IrqCount++ ;
			serviced = 1 ;
			diva_os_schedule_soft_isr (&IoAdapter->isr_soft_isr);
		}
	}

	return (serviced) ;
}

/* --------------------------------------------------------------------------
		Does disable the interrupt on the card
	 -------------------------------------------------------------------------- */
static void disable_qBri_interrupt (PISDN_ADAPTER IoAdapter) {
	dword volatile __iomem *qBriIrq ;
	byte __iomem *p;

	if ( IoAdapter->ControllerNumber > 0 )
		return ;
/*
 *	clear interrupt line (reset Local Interrupt Test Register)
 */
	p = DIVA_OS_MEM_ATTACH_RESET(IoAdapter);
	WRITE_BYTE(&p[PLX9054_INTCSR], 0x00);	/* disable PCI interrupts */
	DIVA_OS_MEM_DETACH_RESET(IoAdapter, p);

	p = DIVA_OS_MEM_ATTACH_CTLREG(IoAdapter);
	qBriIrq = (dword volatile __iomem *)(&p[DIVA_4BRI_REVISION(IoAdapter) ? (MQ2_BREG_IRQ_TEST)  : (MQ_BREG_IRQ_TEST)]);
	WRITE_DWORD(qBriIrq, MQ_IRQ_REQ_OFF) ;
	DIVA_OS_MEM_DETACH_CTLREG(IoAdapter, p);
}

/* --------------------------------------------------------------------------
		Install Adapter Entry Points
	 -------------------------------------------------------------------------- */
static void set_common_qBri_functions (PISDN_ADAPTER IoAdapter) {
	ADAPTER *a;

	a = &IoAdapter->a ;

	a->ram_in           = mem_in ;
	a->ram_inw          = mem_inw ;
	a->ram_in_buffer    = mem_in_buffer ;
	a->ram_look_ahead   = mem_look_ahead ;
	a->ram_out          = mem_out ;
	a->ram_outw         = mem_outw ;
	a->ram_out_buffer   = mem_out_buffer ;
	a->ram_inc          = mem_inc ;

	IoAdapter->out      = pr_out ;
	IoAdapter->dpc      = pr_dpc ;
	IoAdapter->tst_irq  = scom_test_int ;
	IoAdapter->clr_irq  = scom_clear_int ;
	IoAdapter->pcm      = (struct pc_maint *)MIPS_MAINT_OFFS ;

	IoAdapter->load     = load_qBri_hardware ;

	IoAdapter->disIrq   = disable_qBri_interrupt ;
	IoAdapter->rstFnc   = reset_qBri_hardware ;
	IoAdapter->stop     = stop_qBri_hardware ;
	IoAdapter->trapFnc  = qBri_cpu_trapped ;

	IoAdapter->diva_isr_handler = qBri_ISR;

	IoAdapter->a.io       = (void*)IoAdapter ;
}

static void set_qBri_functions (PISDN_ADAPTER IoAdapter) {
	if (!IoAdapter->tasks) {
		IoAdapter->tasks = MQ_INSTANCE_COUNT;
	}
	IoAdapter->MemorySize = MQ_MEMORY_SIZE ;
	set_common_qBri_functions (IoAdapter) ;
	diva_os_set_qBri_functions (IoAdapter) ;
}

static void set_qBri2_functions (PISDN_ADAPTER IoAdapter) {
	if (!IoAdapter->tasks) {
		IoAdapter->tasks = MQ_INSTANCE_COUNT;
	}
	IoAdapter->MemorySize = (IoAdapter->tasks == 1) ? BRI2_MEMORY_SIZE : MQ2_MEMORY_SIZE;
	set_common_qBri_functions (IoAdapter) ;
	diva_os_set_qBri2_functions (IoAdapter) ;
}

/******************************************************************************/

void prepare_qBri_functions (PISDN_ADAPTER IoAdapter) {

	set_qBri_functions (IoAdapter->QuadroList->QuadroAdapter[0]) ;
	set_qBri_functions (IoAdapter->QuadroList->QuadroAdapter[1]) ;
	set_qBri_functions (IoAdapter->QuadroList->QuadroAdapter[2]) ;
	set_qBri_functions (IoAdapter->QuadroList->QuadroAdapter[3]) ;

}

void prepare_qBri2_functions (PISDN_ADAPTER IoAdapter) {
	if (!IoAdapter->tasks) {
		IoAdapter->tasks = MQ_INSTANCE_COUNT;
	}

	set_qBri2_functions (IoAdapter->QuadroList->QuadroAdapter[0]) ;
	if (IoAdapter->tasks > 1) {
		set_qBri2_functions (IoAdapter->QuadroList->QuadroAdapter[1]) ;
		set_qBri2_functions (IoAdapter->QuadroList->QuadroAdapter[2]) ;
		set_qBri2_functions (IoAdapter->QuadroList->QuadroAdapter[3]) ;
	}

}

/* -------------------------------------------------------------------------- */
