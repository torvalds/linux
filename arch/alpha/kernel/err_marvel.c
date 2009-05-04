/*
 *	linux/arch/alpha/kernel/err_marvel.c
 *
 *	Copyright (C) 2001 Jeff Wiedemeier (Compaq Computer Corporation)
 *
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/console.h>
#include <asm/core_marvel.h>
#include <asm/hwrpb.h>
#include <asm/smp.h>
#include <asm/err_common.h>
#include <asm/err_ev7.h>

#include "err_impl.h"
#include "proto.h"

static void
marvel_print_680_frame(struct ev7_lf_subpackets *lf_subpackets)
{
#ifdef CONFIG_VERBOSE_MCHECK
	struct ev7_pal_environmental_subpacket *env;
	struct { int type; char *name; } ev_packets[] = {
		{ EL_TYPE__PAL__ENV__AMBIENT_TEMPERATURE,
		  "Ambient Temperature" },
		{ EL_TYPE__PAL__ENV__AIRMOVER_FAN,
		  "AirMover / Fan" },
		{ EL_TYPE__PAL__ENV__VOLTAGE,
		  "Voltage" },
		{ EL_TYPE__PAL__ENV__INTRUSION,
		  "Intrusion" },
		{ EL_TYPE__PAL__ENV__POWER_SUPPLY,
		  "Power Supply" },
		{ EL_TYPE__PAL__ENV__LAN,
		  "LAN" },
		{ EL_TYPE__PAL__ENV__HOT_PLUG,
		  "Hot Plug" },
		{ 0, NULL }
	};
	int i;

	for (i = 0; ev_packets[i].type != 0; i++) {
		env = lf_subpackets->env[ev7_lf_env_index(ev_packets[i].type)];
		if (!env)
			continue;

		printk("%s**%s event (cabinet %d, drawer %d)\n",
		       err_print_prefix,
		       ev_packets[i].name,
		       env->cabinet,
		       env->drawer);
		printk("%s   Module Type: 0x%x - Unit ID 0x%x - "
		       "Condition 0x%x\n",
		       err_print_prefix,
		       env->module_type,
		       env->unit_id,
		       env->condition);
	}
#endif /* CONFIG_VERBOSE_MCHECK */
}

static int
marvel_process_680_frame(struct ev7_lf_subpackets *lf_subpackets, int print)
{
	int status = MCHK_DISPOSITION_UNKNOWN_ERROR;
	int i;

	for (i = ev7_lf_env_index(EL_TYPE__PAL__ENV__AMBIENT_TEMPERATURE);
	     i <= ev7_lf_env_index(EL_TYPE__PAL__ENV__HOT_PLUG);
	     i++) {
		if (lf_subpackets->env[i])
			status = MCHK_DISPOSITION_REPORT;
	}

	if (print)
		marvel_print_680_frame(lf_subpackets);

	return status;
}

#ifdef CONFIG_VERBOSE_MCHECK

static void
marvel_print_err_cyc(u64 err_cyc)
{
	static char *packet_desc[] = {
		"No Error",
		"UNKNOWN",
		"1 cycle (1 or 2 flit packet)",
		"2 cycles (3 flit packet)",
		"9 cycles (18 flit packet)",
		"10 cycles (19 flit packet)",
		"UNKNOWN",
		"UNKNOWN",
		"UNKNOWN"
	};

#define IO7__ERR_CYC__ODD_FLT	(1UL <<  0)
#define IO7__ERR_CYC__EVN_FLT	(1UL <<  1)
#define IO7__ERR_CYC__PACKET__S	(6)
#define IO7__ERR_CYC__PACKET__M	(0x7)
#define IO7__ERR_CYC__LOC	(1UL <<  5)
#define IO7__ERR_CYC__CYCLE__S	(2)
#define IO7__ERR_CYC__CYCLE__M	(0x7)

	printk("%s        Packet In Error: %s\n"
	       "%s        Error in %s, cycle %ld%s%s\n",
	       err_print_prefix, 
	       packet_desc[EXTRACT(err_cyc, IO7__ERR_CYC__PACKET)],
	       err_print_prefix,
	       (err_cyc & IO7__ERR_CYC__LOC) ? "DATA" : "HEADER",
	       EXTRACT(err_cyc, IO7__ERR_CYC__CYCLE),
	       (err_cyc & IO7__ERR_CYC__ODD_FLT) ? " [ODD Flit]": "",
	       (err_cyc & IO7__ERR_CYC__EVN_FLT) ? " [Even Flit]": "");
}

static void
marvel_print_po7_crrct_sym(u64 crrct_sym)
{
#define IO7__PO7_CRRCT_SYM__SYN__S	(0)
#define IO7__PO7_CRRCT_SYM__SYN__M	(0x7f)
#define IO7__PO7_CRRCT_SYM__ERR_CYC__S	(7)   /* ERR_CYC + ODD_FLT + EVN_FLT */
#define IO7__PO7_CRRCT_SYM__ERR_CYC__M	(0x1ff)


	printk("%s      Correctable Error Symptoms:\n"
	       "%s        Syndrome: 0x%llx\n",
	       err_print_prefix,
	       err_print_prefix, EXTRACT(crrct_sym, IO7__PO7_CRRCT_SYM__SYN));
	marvel_print_err_cyc(EXTRACT(crrct_sym, IO7__PO7_CRRCT_SYM__ERR_CYC));
}

static void
marvel_print_po7_uncrr_sym(u64 uncrr_sym, u64 valid_mask)
{
	static char *clk_names[] = { "_h[0]", "_h[1]", "_n[0]", "_n[1]" };
	static char *clk_decode[] = {
		"No Error",
		"One extra rising edge",
		"Two extra rising edges",
		"Lost one clock"
	};
	static char *port_names[] = { "Port 0", 	"Port 1", 
				      "Port 2", 	"Port 3",
				      "Unknown Port",	"Unknown Port",
				      "Unknown Port",	"Port 7" };
	int scratch, i;

#define IO7__PO7_UNCRR_SYM__SYN__S	    (0)
#define IO7__PO7_UNCRR_SYM__SYN__M	    (0x7f)
#define IO7__PO7_UNCRR_SYM__ERR_CYC__S	    (7)      /* ERR_CYC + ODD_FLT... */
#define IO7__PO7_UNCRR_SYM__ERR_CYC__M	    (0x1ff)  /* ... + EVN_FLT        */
#define IO7__PO7_UNCRR_SYM__CLK__S	    (16)
#define IO7__PO7_UNCRR_SYM__CLK__M	    (0xff)
#define IO7__PO7_UNCRR_SYM__CDT_OVF_TO__REQ (1UL << 24)
#define IO7__PO7_UNCRR_SYM__CDT_OVF_TO__RIO (1UL << 25)
#define IO7__PO7_UNCRR_SYM__CDT_OVF_TO__WIO (1UL << 26)
#define IO7__PO7_UNCRR_SYM__CDT_OVF_TO__BLK (1UL << 27)
#define IO7__PO7_UNCRR_SYM__CDT_OVF_TO__NBK (1UL << 28)
#define IO7__PO7_UNCRR_SYM__OVF__READIO	    (1UL << 29)
#define IO7__PO7_UNCRR_SYM__OVF__WRITEIO    (1UL << 30)
#define IO7__PO7_UNCRR_SYM__OVF__FWD        (1UL << 31)
#define IO7__PO7_UNCRR_SYM__VICTIM_SP__S    (32)
#define IO7__PO7_UNCRR_SYM__VICTIM_SP__M    (0xff)
#define IO7__PO7_UNCRR_SYM__DETECT_SP__S    (40)
#define IO7__PO7_UNCRR_SYM__DETECT_SP__M    (0xff)
#define IO7__PO7_UNCRR_SYM__STRV_VTR__S     (48)
#define IO7__PO7_UNCRR_SYM__STRV_VTR__M     (0x3ff)

#define IO7__STRV_VTR__LSI__INTX__S	    (0)
#define IO7__STRV_VTR__LSI__INTX__M	    (0x3)
#define IO7__STRV_VTR__LSI__SLOT__S	    (2)
#define IO7__STRV_VTR__LSI__SLOT__M	    (0x7)
#define IO7__STRV_VTR__LSI__BUS__S	    (5)
#define IO7__STRV_VTR__LSI__BUS__M	    (0x3)
#define IO7__STRV_VTR__MSI__INTNUM__S	    (0)
#define IO7__STRV_VTR__MSI__INTNUM__M	    (0x1ff)
#define IO7__STRV_VTR__IS_MSI		    (1UL << 9)

	printk("%s      Uncorrectable Error Symptoms:\n", err_print_prefix);
	uncrr_sym &= valid_mask;

	if (EXTRACT(valid_mask, IO7__PO7_UNCRR_SYM__SYN))
		printk("%s        Syndrome: 0x%llx\n",
		       err_print_prefix, 
		       EXTRACT(uncrr_sym, IO7__PO7_UNCRR_SYM__SYN));

	if (EXTRACT(valid_mask, IO7__PO7_UNCRR_SYM__ERR_CYC))
		marvel_print_err_cyc(EXTRACT(uncrr_sym, 
					     IO7__PO7_UNCRR_SYM__ERR_CYC));

	scratch = EXTRACT(uncrr_sym, IO7__PO7_UNCRR_SYM__CLK);
	for (i = 0; i < 4; i++, scratch >>= 2) {
		if (scratch & 0x3)
			printk("%s        Clock %s: %s\n",
			       err_print_prefix,
			       clk_names[i], clk_decode[scratch & 0x3]);
	}

	if (uncrr_sym & IO7__PO7_UNCRR_SYM__CDT_OVF_TO__REQ) 
		printk("%s       REQ Credit Timeout or Overflow\n",
		       err_print_prefix);
	if (uncrr_sym & IO7__PO7_UNCRR_SYM__CDT_OVF_TO__RIO) 
		printk("%s       RIO Credit Timeout or Overflow\n",
		       err_print_prefix);
	if (uncrr_sym & IO7__PO7_UNCRR_SYM__CDT_OVF_TO__WIO) 
		printk("%s       WIO Credit Timeout or Overflow\n",
		       err_print_prefix);
	if (uncrr_sym & IO7__PO7_UNCRR_SYM__CDT_OVF_TO__BLK) 
		printk("%s       BLK Credit Timeout or Overflow\n",
		       err_print_prefix);
	if (uncrr_sym & IO7__PO7_UNCRR_SYM__CDT_OVF_TO__NBK) 
		printk("%s       NBK Credit Timeout or Overflow\n",
		       err_print_prefix);

	if (uncrr_sym & IO7__PO7_UNCRR_SYM__OVF__READIO) 
		printk("%s       Read I/O Buffer Overflow\n", 
		       err_print_prefix);
	if (uncrr_sym & IO7__PO7_UNCRR_SYM__OVF__WRITEIO) 
		printk("%s       Write I/O Buffer Overflow\n", 
		       err_print_prefix);
	if (uncrr_sym & IO7__PO7_UNCRR_SYM__OVF__FWD) 
		printk("%s       FWD Buffer Overflow\n", 
		       err_print_prefix);

	if ((scratch = EXTRACT(uncrr_sym, IO7__PO7_UNCRR_SYM__VICTIM_SP))) {
		int lost = scratch & (1UL << 4);
		scratch &= ~lost;
		for (i = 0; i < 8; i++, scratch >>= 1) {
			if (!(scratch & 1))
				continue;
			printk("%s        Error Response sent to %s",
			       err_print_prefix, port_names[i]);
		}
		if (lost)
			printk("%s        Lost Error sent somewhere else\n",
			       err_print_prefix);
	}
	
	if ((scratch = EXTRACT(uncrr_sym, IO7__PO7_UNCRR_SYM__DETECT_SP))) {
		for (i = 0; i < 8; i++, scratch >>= 1) {
			if (!(scratch & 1))
				continue;
			printk("%s        Error Reported by %s",
			       err_print_prefix, port_names[i]);
		}
	}

	if (EXTRACT(valid_mask, IO7__PO7_UNCRR_SYM__STRV_VTR)) {
		char starvation_message[80];

		scratch = EXTRACT(uncrr_sym, IO7__PO7_UNCRR_SYM__STRV_VTR);
		if (scratch & IO7__STRV_VTR__IS_MSI) 
			sprintf(starvation_message, 
				"MSI Interrupt 0x%x",
				EXTRACT(scratch, IO7__STRV_VTR__MSI__INTNUM));
		else
			sprintf(starvation_message,
				"LSI INT%c for Bus:Slot (%d:%d)\n",
				'A' + EXTRACT(scratch, 
					      IO7__STRV_VTR__LSI__INTX),
				EXTRACT(scratch, IO7__STRV_VTR__LSI__BUS),
				EXTRACT(scratch, IO7__STRV_VTR__LSI__SLOT));

		printk("%s        Starvation Int Trigger By: %s\n",
		       err_print_prefix, starvation_message);
	}
}

static void
marvel_print_po7_ugbge_sym(u64 ugbge_sym)
{
	char opcode_str[10];

#define IO7__PO7_UGBGE_SYM__UPH_PKT_OFF__S	(6)
#define IO7__PO7_UGBGE_SYM__UPH_PKT_OFF__M	(0xfffffffful)
#define IO7__PO7_UGBGE_SYM__UPH_OPCODE__S	(40)
#define IO7__PO7_UGBGE_SYM__UPH_OPCODE__M	(0xff)
#define IO7__PO7_UGBGE_SYM__UPH_SRC_PORT__S	(48)
#define IO7__PO7_UGBGE_SYM__UPH_SRC_PORT__M	(0xf)
#define IO7__PO7_UGBGE_SYM__UPH_DEST_PID__S	(52)
#define IO7__PO7_UGBGE_SYM__UPH_DEST_PID__M	(0x7ff)
#define IO7__PO7_UGBGE_SYM__VALID		(1UL << 63)

	if (!(ugbge_sym & IO7__PO7_UGBGE_SYM__VALID))
		return;

	switch(EXTRACT(ugbge_sym, IO7__PO7_UGBGE_SYM__UPH_OPCODE)) {
	case 0x51:
		sprintf(opcode_str, "Wr32");
		break;
	case 0x50:
		sprintf(opcode_str, "WrQW");
		break;
	case 0x54:
		sprintf(opcode_str, "WrIPR");
		break;
	case 0xD8:
		sprintf(opcode_str, "Victim");
		break;
	case 0xC5:
		sprintf(opcode_str, "BlkIO");
		break;
	default:
		sprintf(opcode_str, "0x%llx\n",
			EXTRACT(ugbge_sym, IO7__PO7_UGBGE_SYM__UPH_OPCODE));
		break;
	}

	printk("%s      Up Hose Garbage Symptom:\n"
	       "%s        Source Port: %ld - Dest PID: %ld - OpCode: %s\n", 
	       err_print_prefix,
	       err_print_prefix, 
	       EXTRACT(ugbge_sym, IO7__PO7_UGBGE_SYM__UPH_SRC_PORT),
	       EXTRACT(ugbge_sym, IO7__PO7_UGBGE_SYM__UPH_DEST_PID),
	       opcode_str);

	if (0xC5 != EXTRACT(ugbge_sym, IO7__PO7_UGBGE_SYM__UPH_OPCODE))
		printk("%s        Packet Offset 0x%08llx\n",
		       err_print_prefix,
		       EXTRACT(ugbge_sym, IO7__PO7_UGBGE_SYM__UPH_PKT_OFF));
}

static void
marvel_print_po7_err_sum(struct ev7_pal_io_subpacket *io)
{
	u64	uncrr_sym_valid = 0;

#define IO7__PO7_ERRSUM__CR_SBE		(1UL << 32)
#define IO7__PO7_ERRSUM__CR_SBE2	(1UL << 33)
#define IO7__PO7_ERRSUM__CR_PIO_WBYTE	(1UL << 34)
#define IO7__PO7_ERRSUM__CR_CSR_NXM	(1UL << 35)
#define IO7__PO7_ERRSUM__CR_RPID_ACV	(1UL << 36)
#define IO7__PO7_ERRSUM__CR_RSP_NXM	(1UL << 37)
#define IO7__PO7_ERRSUM__CR_ERR_RESP	(1UL << 38)
#define IO7__PO7_ERRSUM__CR_CLK_DERR	(1UL << 39)
#define IO7__PO7_ERRSUM__CR_DAT_DBE	(1UL << 40)
#define IO7__PO7_ERRSUM__CR_DAT_GRBG	(1UL << 41)
#define IO7__PO7_ERRSUM__MAF_TO		(1UL << 42)
#define IO7__PO7_ERRSUM__UGBGE		(1UL << 43)
#define IO7__PO7_ERRSUM__UN_MAF_LOST	(1UL << 44)
#define IO7__PO7_ERRSUM__UN_PKT_OVF	(1UL << 45)
#define IO7__PO7_ERRSUM__UN_CDT_OVF	(1UL << 46)
#define IO7__PO7_ERRSUM__UN_DEALLOC	(1UL << 47)
#define IO7__PO7_ERRSUM__BH_CDT_TO	(1UL << 51)
#define IO7__PO7_ERRSUM__BH_CLK_HDR	(1UL << 52)
#define IO7__PO7_ERRSUM__BH_DBE_HDR	(1UL << 53)
#define IO7__PO7_ERRSUM__BH_GBG_HDR	(1UL << 54)
#define IO7__PO7_ERRSUM__BH_BAD_CMD	(1UL << 55)
#define IO7__PO7_ERRSUM__HLT_INT	(1UL << 56)
#define IO7__PO7_ERRSUM__HP_INT		(1UL << 57)
#define IO7__PO7_ERRSUM__CRD_INT	(1UL << 58)
#define IO7__PO7_ERRSUM__STV_INT	(1UL << 59)
#define IO7__PO7_ERRSUM__HRD_INT	(1UL << 60)
#define IO7__PO7_ERRSUM__BH_SUM		(1UL << 61)
#define IO7__PO7_ERRSUM__ERR_LST	(1UL << 62)
#define IO7__PO7_ERRSUM__ERR_VALID	(1UL << 63)

#define IO7__PO7_ERRSUM__ERR_MASK	(IO7__PO7_ERRSUM__ERR_VALID |	\
					 IO7__PO7_ERRSUM__CR_SBE)

	/*
	 * Single bit errors aren't covered by ERR_VALID.
	 */
	if (io->po7_error_sum & IO7__PO7_ERRSUM__CR_SBE) {
		printk("%s    %sSingle Bit Error(s) detected/corrected\n",
		       err_print_prefix,
		       (io->po7_error_sum & IO7__PO7_ERRSUM__CR_SBE2) 
		       ? "Multiple " : "");
		marvel_print_po7_crrct_sym(io->po7_crrct_sym);
	}

	/*
	 * Neither are the interrupt status bits
	 */
	if (io->po7_error_sum & IO7__PO7_ERRSUM__HLT_INT)
		printk("%s    Halt Interrupt posted", err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__HP_INT) {
		printk("%s    Hot Plug Event Interrupt posted", 
		       err_print_prefix);
		uncrr_sym_valid |= GEN_MASK(IO7__PO7_UNCRR_SYM__DETECT_SP);
	}
	if (io->po7_error_sum & IO7__PO7_ERRSUM__CRD_INT)
		printk("%s    Correctable Error Interrupt posted", 
		       err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__STV_INT) {
		printk("%s    Starvation Interrupt posted", err_print_prefix);
		uncrr_sym_valid |= GEN_MASK(IO7__PO7_UNCRR_SYM__STRV_VTR);
	}
	if (io->po7_error_sum & IO7__PO7_ERRSUM__HRD_INT) {
		printk("%s    Hard Error Interrupt posted", err_print_prefix);
		uncrr_sym_valid |= GEN_MASK(IO7__PO7_UNCRR_SYM__DETECT_SP);
	}

	/*
	 * Everything else is valid only with ERR_VALID, so skip to the end
	 * (uncrr_sym check) unless ERR_VALID is set.
	 */
	if (!(io->po7_error_sum & IO7__PO7_ERRSUM__ERR_VALID)) 
		goto check_uncrr_sym;

	/*
	 * Since ERR_VALID is set, VICTIM_SP in uncrr_sym is valid.
	 * For bits [29:0] to also be valid, the following bits must
	 * not be set:
	 *	CR_PIO_WBYTE	CR_CSR_NXM	CR_RSP_NXM
	 *	CR_ERR_RESP	MAF_TO
	 */
	uncrr_sym_valid |= GEN_MASK(IO7__PO7_UNCRR_SYM__VICTIM_SP);
	if (!(io->po7_error_sum & (IO7__PO7_ERRSUM__CR_PIO_WBYTE |
				   IO7__PO7_ERRSUM__CR_CSR_NXM |
				   IO7__PO7_ERRSUM__CR_RSP_NXM |
				   IO7__PO7_ERRSUM__CR_ERR_RESP |
				   IO7__PO7_ERRSUM__MAF_TO)))
		uncrr_sym_valid |= 0x3ffffffful;

	if (io->po7_error_sum & IO7__PO7_ERRSUM__CR_PIO_WBYTE)
		printk("%s    Write byte into IO7 CSR\n", err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__CR_CSR_NXM)
		printk("%s    PIO to non-existent CSR\n", err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__CR_RPID_ACV)
		printk("%s    Bus Requester PID (Access Violation)\n",
		       err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__CR_RSP_NXM)
		printk("%s    Received NXM response from EV7\n",
		       err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__CR_ERR_RESP)
		printk("%s    Received ERROR RESPONSE\n", err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__CR_CLK_DERR)
		printk("%s    Clock error on data flit\n", err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__CR_DAT_DBE)
		printk("%s    Double Bit Error Data Error Detected\n",
		       err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__CR_DAT_GRBG)
		printk("%s    Garbage Encoding Detected on the data\n",
		       err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__UGBGE) {
		printk("%s    Garbage Encoding sent up hose\n",
		       err_print_prefix);
		marvel_print_po7_ugbge_sym(io->po7_ugbge_sym);
	}
	if (io->po7_error_sum & IO7__PO7_ERRSUM__UN_MAF_LOST)
		printk("%s    Orphan response (unexpected response)\n",
		       err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__UN_PKT_OVF)
		printk("%s    Down hose packet overflow\n", err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__UN_CDT_OVF)
		printk("%s    Down hose credit overflow\n", err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__UN_DEALLOC)
		printk("%s    Unexpected or bad dealloc field\n",
		       err_print_prefix);

	/*
	 * The black hole events.
	 */
	if (io->po7_error_sum & IO7__PO7_ERRSUM__MAF_TO)
		printk("%s    BLACK HOLE: Timeout for all responses\n",
		       err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__BH_CDT_TO)
		printk("%s    BLACK HOLE: Credit Timeout\n", err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__BH_CLK_HDR)
		printk("%s    BLACK HOLE: Clock check on header\n", 
		       err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__BH_DBE_HDR)
		printk("%s    BLACK HOLE: Uncorrectable Error on header\n",
		       err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__BH_GBG_HDR)
		printk("%s    BLACK HOLE: Garbage on header\n", 
		       err_print_prefix);
	if (io->po7_error_sum & IO7__PO7_ERRSUM__BH_BAD_CMD)
		printk("%s    BLACK HOLE: Bad EV7 command\n", 
		       err_print_prefix);

	if (io->po7_error_sum & IO7__PO7_ERRSUM__ERR_LST) 
		printk("%s    Lost Error\n", err_print_prefix);

	printk("%s    Failing Packet:\n"
	       "%s      Cycle 1: %016llx\n"
	       "%s      Cycle 2: %016llx\n",
	       err_print_prefix,
	       err_print_prefix, io->po7_err_pkt0,
	       err_print_prefix, io->po7_err_pkt1);
	/*
	 * If there are any valid bits in UNCRR sym for this err, 
	 * print UNCRR_SYM as well.
	 */
check_uncrr_sym:
	if (uncrr_sym_valid)
		marvel_print_po7_uncrr_sym(io->po7_uncrr_sym, uncrr_sym_valid);
}

static void
marvel_print_pox_tlb_err(u64 tlb_err)
{
	static char *tlb_errors[] = {
		"No Error",
		"North Port Signaled Error fetching TLB entry",
		"PTE invalid or UCC or GBG error on this entry",
		"Address did not hit any DMA window"
	};

#define IO7__POX_TLBERR__ERR_VALID		(1UL << 63)
#define IO7__POX_TLBERR__ERRCODE__S		(0)
#define IO7__POX_TLBERR__ERRCODE__M		(0x3)
#define IO7__POX_TLBERR__ERR_TLB_PTR__S		(3)
#define IO7__POX_TLBERR__ERR_TLB_PTR__M		(0x7)
#define IO7__POX_TLBERR__FADDR__S		(6)
#define IO7__POX_TLBERR__FADDR__M		(0x3fffffffffful)

	if (!(tlb_err & IO7__POX_TLBERR__ERR_VALID))
		return;

	printk("%s      TLB Error on index 0x%llx:\n"
	       "%s        - %s\n"
	       "%s        - Addr: 0x%016llx\n",
	       err_print_prefix,
	       EXTRACT(tlb_err, IO7__POX_TLBERR__ERR_TLB_PTR),
	       err_print_prefix,
	       tlb_errors[EXTRACT(tlb_err, IO7__POX_TLBERR__ERRCODE)],
	       err_print_prefix,
	       EXTRACT(tlb_err, IO7__POX_TLBERR__FADDR) << 6);
}

static  void
marvel_print_pox_spl_cmplt(u64 spl_cmplt)
{
	char message[80];

#define IO7__POX_SPLCMPLT__MESSAGE__S		(0)
#define IO7__POX_SPLCMPLT__MESSAGE__M		(0x0fffffffful)
#define IO7__POX_SPLCMPLT__SOURCE_BUS__S	(40)
#define IO7__POX_SPLCMPLT__SOURCE_BUS__M	(0xfful)
#define IO7__POX_SPLCMPLT__SOURCE_DEV__S	(35)
#define IO7__POX_SPLCMPLT__SOURCE_DEV__M	(0x1ful)
#define IO7__POX_SPLCMPLT__SOURCE_FUNC__S	(32)
#define IO7__POX_SPLCMPLT__SOURCE_FUNC__M	(0x07ul)

#define IO7__POX_SPLCMPLT__MSG_CLASS__S		(28)
#define IO7__POX_SPLCMPLT__MSG_CLASS__M		(0xf)
#define IO7__POX_SPLCMPLT__MSG_INDEX__S		(20)
#define IO7__POX_SPLCMPLT__MSG_INDEX__M		(0xff)
#define IO7__POX_SPLCMPLT__MSG_CLASSINDEX__S	(20)
#define IO7__POX_SPLCMPLT__MSG_CLASSINDEX__M    (0xfff)
#define IO7__POX_SPLCMPLT__REM_LOWER_ADDR__S	(12)
#define IO7__POX_SPLCMPLT__REM_LOWER_ADDR__M	(0x7f)
#define IO7__POX_SPLCMPLT__REM_BYTE_COUNT__S	(0)
#define IO7__POX_SPLCMPLT__REM_BYTE_COUNT__M	(0xfff)

	printk("%s      Split Completion Error:\n"	
	       "%s         Source (Bus:Dev:Func): %ld:%ld:%ld\n",
	       err_print_prefix,
	       err_print_prefix,
	       EXTRACT(spl_cmplt, IO7__POX_SPLCMPLT__SOURCE_BUS),
	       EXTRACT(spl_cmplt, IO7__POX_SPLCMPLT__SOURCE_DEV),
	       EXTRACT(spl_cmplt, IO7__POX_SPLCMPLT__SOURCE_FUNC));

	switch(EXTRACT(spl_cmplt, IO7__POX_SPLCMPLT__MSG_CLASSINDEX)) {
	case 0x000:
		sprintf(message, "Normal completion");
		break;
	case 0x100:
		sprintf(message, "Bridge - Master Abort");
		break;
	case 0x101:
		sprintf(message, "Bridge - Target Abort");
		break;
	case 0x102:
		sprintf(message, "Bridge - Uncorrectable Write Data Error");
		break;
	case 0x200:
		sprintf(message, "Byte Count Out of Range");
		break;
	case 0x201:
		sprintf(message, "Uncorrectable Split Write Data Error");
		break;
	default:
		sprintf(message, "%08llx\n",
			EXTRACT(spl_cmplt, IO7__POX_SPLCMPLT__MESSAGE));
		break;
	}
	printk("%s	   Message: %s\n", err_print_prefix, message);
}

static void
marvel_print_pox_trans_sum(u64 trans_sum)
{
	char *pcix_cmd[] = { "Interrupt Acknowledge",
			     "Special Cycle",
			     "I/O Read",
			     "I/O Write",
			     "Reserved",
			     "Reserved / Device ID Message",
			     "Memory Read",
			     "Memory Write",
			     "Reserved / Alias to Memory Read Block",
			     "Reserved / Alias to Memory Write Block",
			     "Configuration Read",
			     "Configuration Write",
			     "Memory Read Multiple / Split Completion",
			     "Dual Address Cycle",
			     "Memory Read Line / Memory Read Block",
			     "Memory Write and Invalidate / Memory Write Block"
	};

#define IO7__POX_TRANSUM__PCI_ADDR__S		(0)
#define IO7__POX_TRANSUM__PCI_ADDR__M		(0x3fffffffffffful)
#define IO7__POX_TRANSUM__DAC			(1UL << 50)
#define IO7__POX_TRANSUM__PCIX_MASTER_SLOT__S	(52)
#define IO7__POX_TRANSUM__PCIX_MASTER_SLOT__M	(0xf)
#define IO7__POX_TRANSUM__PCIX_CMD__S		(56)
#define IO7__POX_TRANSUM__PCIX_CMD__M		(0xf)
#define IO7__POX_TRANSUM__ERR_VALID		(1UL << 63)

	if (!(trans_sum & IO7__POX_TRANSUM__ERR_VALID))
		return;

	printk("%s      Transaction Summary:\n"
	       "%s        Command: 0x%llx - %s\n"
	       "%s        Address: 0x%016llx%s\n"
	       "%s        PCI-X Master Slot: 0x%llx\n",
	       err_print_prefix, 
	       err_print_prefix, 
	       EXTRACT(trans_sum, IO7__POX_TRANSUM__PCIX_CMD),
	       pcix_cmd[EXTRACT(trans_sum, IO7__POX_TRANSUM__PCIX_CMD)],
	       err_print_prefix,
	       EXTRACT(trans_sum, IO7__POX_TRANSUM__PCI_ADDR),
	       (trans_sum & IO7__POX_TRANSUM__DAC) ? " (DAC)" : "",
	       err_print_prefix,
	       EXTRACT(trans_sum, IO7__POX_TRANSUM__PCIX_MASTER_SLOT));
}

static void
marvel_print_pox_err(u64 err_sum, struct ev7_pal_io_one_port *port)
{
#define IO7__POX_ERRSUM__AGP_REQQ_OVFL    (1UL <<  4)
#define IO7__POX_ERRSUM__AGP_SYNC_ERR     (1UL <<  5)
#define IO7__POX_ERRSUM__MRETRY_TO        (1UL <<  6)
#define IO7__POX_ERRSUM__PCIX_UX_SPL      (1UL <<  7)
#define IO7__POX_ERRSUM__PCIX_SPLIT_TO    (1UL <<  8)
#define IO7__POX_ERRSUM__PCIX_DISCARD_SPL (1UL <<  9)
#define IO7__POX_ERRSUM__DMA_RD_TO        (1UL << 10)
#define IO7__POX_ERRSUM__CSR_NXM_RD       (1UL << 11)
#define IO7__POX_ERRSUM__CSR_NXM_WR       (1UL << 12)
#define IO7__POX_ERRSUM__DMA_TO           (1UL << 13)
#define IO7__POX_ERRSUM__ALL_MABORTS      (1UL << 14)
#define IO7__POX_ERRSUM__MABORT		  (1UL << 15)
#define IO7__POX_ERRSUM__MABORT_MASK	  (IO7__POX_ERRSUM__ALL_MABORTS|\
					   IO7__POX_ERRSUM__MABORT)
#define IO7__POX_ERRSUM__PT_TABORT        (1UL << 16)
#define IO7__POX_ERRSUM__PM_TABORT        (1UL << 17)
#define IO7__POX_ERRSUM__TABORT_MASK      (IO7__POX_ERRSUM__PT_TABORT | \
                                           IO7__POX_ERRSUM__PM_TABORT)
#define IO7__POX_ERRSUM__SERR             (1UL << 18)
#define IO7__POX_ERRSUM__ADDRERR_STB      (1UL << 19)
#define IO7__POX_ERRSUM__DETECTED_SERR    (1UL << 20)
#define IO7__POX_ERRSUM__PERR             (1UL << 21)
#define IO7__POX_ERRSUM__DATAERR_STB_NIOW (1UL << 22)
#define IO7__POX_ERRSUM__DETECTED_PERR    (1UL << 23)
#define IO7__POX_ERRSUM__PM_PERR          (1UL << 24)
#define IO7__POX_ERRSUM__PT_SCERROR       (1UL << 26)
#define IO7__POX_ERRSUM__HUNG_BUS         (1UL << 28)
#define IO7__POX_ERRSUM__UPE_ERROR__S     (51)
#define IO7__POX_ERRSUM__UPE_ERROR__M     (0xffUL)
#define IO7__POX_ERRSUM__UPE_ERROR        GEN_MASK(IO7__POX_ERRSUM__UPE_ERROR)
#define IO7__POX_ERRSUM__TLB_ERR          (1UL << 59)
#define IO7__POX_ERRSUM__ERR_VALID        (1UL << 63)

#define IO7__POX_ERRSUM__TRANS_SUM__MASK  (IO7__POX_ERRSUM__MRETRY_TO |       \
					   IO7__POX_ERRSUM__PCIX_UX_SPL |     \
					   IO7__POX_ERRSUM__PCIX_SPLIT_TO |   \
					   IO7__POX_ERRSUM__DMA_TO |          \
					   IO7__POX_ERRSUM__MABORT_MASK |     \
					   IO7__POX_ERRSUM__TABORT_MASK |     \
					   IO7__POX_ERRSUM__SERR |            \
					   IO7__POX_ERRSUM__ADDRERR_STB |     \
					   IO7__POX_ERRSUM__PERR |            \
					   IO7__POX_ERRSUM__DATAERR_STB_NIOW |\
					   IO7__POX_ERRSUM__DETECTED_PERR |   \
					   IO7__POX_ERRSUM__PM_PERR |         \
					   IO7__POX_ERRSUM__PT_SCERROR |      \
					   IO7__POX_ERRSUM__UPE_ERROR)

	if (!(err_sum & IO7__POX_ERRSUM__ERR_VALID))
		return;

	/*
	 * First the transaction summary errors
	 */
	if (err_sum & IO7__POX_ERRSUM__MRETRY_TO)
		printk("%s    IO7 Master Retry Timeout expired\n",
		       err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__PCIX_UX_SPL)
		printk("%s    Unexpected Split Completion\n",
		       err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__PCIX_SPLIT_TO)
		printk("%s    IO7 Split Completion Timeout expired\n",
		       err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__DMA_TO)
		printk("%s    Hung bus during DMA transaction\n",
		       err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__MABORT_MASK)
		printk("%s    Master Abort\n", err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__PT_TABORT)
		printk("%s    IO7 Asserted Target Abort\n", err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__PM_TABORT)
		printk("%s    IO7 Received Target Abort\n", err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__ADDRERR_STB) {
		printk("%s    Address or PCI-X Attribute Parity Error\n", 
		       err_print_prefix);
		if (err_sum & IO7__POX_ERRSUM__SERR)
			printk("%s     IO7 Asserted SERR\n", err_print_prefix);
	}
	if (err_sum & IO7__POX_ERRSUM__PERR) {
		if (err_sum & IO7__POX_ERRSUM__DATAERR_STB_NIOW)
			printk("%s    IO7 Detected Data Parity Error\n",
			       err_print_prefix);
		else
			printk("%s    Split Completion Response with "
			       "Parity Error\n", err_print_prefix);
	}
	if (err_sum & IO7__POX_ERRSUM__DETECTED_PERR)
		printk("%s    PERR detected\n", err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__PM_PERR)
		printk("%s    PERR while IO7 is master\n", err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__PT_SCERROR) {
		printk("%s    IO7 Received Split Completion Error message\n",
		       err_print_prefix);
		marvel_print_pox_spl_cmplt(port->pox_spl_cmplt);
	}
	if (err_sum & IO7__POX_ERRSUM__UPE_ERROR) {
		unsigned int upe_error = EXTRACT(err_sum,
						 IO7__POX_ERRSUM__UPE_ERROR);
		int i;
		static char *upe_errors[] = {
			"Parity Error on MSI write data",
			"MSI read (MSI window is write only",
			"TLB - Invalid WR transaction",
			"TLB - Invalid RD transaction",
			"DMA - WR error (see north port)",
			"DMA - RD error (see north port)",
			"PPR - WR error (see north port)",
			"PPR - RD error (see north port)"
		};

		printk("%s    UPE Error:\n", err_print_prefix);
		for (i = 0; i < 8; i++) {
			if (upe_error & (1 << i))
				printk("%s      %s\n", err_print_prefix,
				       upe_errors[i]);
		}
	}

	/*
	 * POx_TRANS_SUM, if appropriate.
	 */
	if (err_sum & IO7__POX_ERRSUM__TRANS_SUM__MASK) 
		marvel_print_pox_trans_sum(port->pox_trans_sum);

	/*
	 * Then TLB_ERR.
	 */
	if (err_sum & IO7__POX_ERRSUM__TLB_ERR) {
		printk("%s    TLB ERROR\n", err_print_prefix);
		marvel_print_pox_tlb_err(port->pox_tlb_err);
	}

	/*
	 * And the single bit status errors.
	 */
	if (err_sum & IO7__POX_ERRSUM__AGP_REQQ_OVFL)
		printk("%s    AGP Request Queue Overflow\n", err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__AGP_SYNC_ERR)
		printk("%s    AGP Sync Error\n", err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__PCIX_DISCARD_SPL)
		printk("%s    Discarded split completion\n", err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__DMA_RD_TO)
		printk("%s    DMA Read Timeout\n", err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__CSR_NXM_RD)
		printk("%s    CSR NXM READ\n", err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__CSR_NXM_WR)
		printk("%s    CSR NXM WRITE\n", err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__DETECTED_SERR)
		printk("%s    SERR detected\n", err_print_prefix);
	if (err_sum & IO7__POX_ERRSUM__HUNG_BUS)
		printk("%s    HUNG BUS detected\n", err_print_prefix);
}

#endif /* CONFIG_VERBOSE_MCHECK */

static struct ev7_pal_io_subpacket *
marvel_find_io7_with_error(struct ev7_lf_subpackets *lf_subpackets)
{
	struct ev7_pal_io_subpacket *io = lf_subpackets->io;
	struct io7 *io7;
	int i;

	/*
	 * Caller must provide the packet to fill
	 */
	if (!io)
		return NULL;

	/*
	 * Fill the subpacket with the console's standard fill pattern
	 */
	memset(io, 0x55, sizeof(*io));

	for (io7 = NULL; NULL != (io7 = marvel_next_io7(io7)); ) {
		unsigned long err_sum = 0;

		err_sum |= io7->csrs->PO7_ERROR_SUM.csr;
		for (i = 0; i < IO7_NUM_PORTS; i++) {
			if (!io7->ports[i].enabled)
				continue;
			err_sum |= io7->ports[i].csrs->POx_ERR_SUM.csr;
		}

		/*
		 * Is there at least one error? 
		 */
		if (err_sum & (1UL << 63))
			break;
	}

	/*
	 * Did we find an IO7 with an error?
	 */
	if (!io7)
		return NULL;

	/*
	 * We have an IO7 with an error. 
	 *
	 * Fill in the IO subpacket.
	 */
	io->io_asic_rev   = io7->csrs->IO_ASIC_REV.csr;
	io->io_sys_rev    = io7->csrs->IO_SYS_REV.csr;
	io->io7_uph       = io7->csrs->IO7_UPH.csr;
	io->hpi_ctl       = io7->csrs->HPI_CTL.csr;
	io->crd_ctl       = io7->csrs->CRD_CTL.csr;
	io->hei_ctl       = io7->csrs->HEI_CTL.csr;
	io->po7_error_sum = io7->csrs->PO7_ERROR_SUM.csr;
	io->po7_uncrr_sym = io7->csrs->PO7_UNCRR_SYM.csr;
	io->po7_crrct_sym = io7->csrs->PO7_CRRCT_SYM.csr;
	io->po7_ugbge_sym = io7->csrs->PO7_UGBGE_SYM.csr;
	io->po7_err_pkt0  = io7->csrs->PO7_ERR_PKT[0].csr;
	io->po7_err_pkt1  = io7->csrs->PO7_ERR_PKT[1].csr;
	
	for (i = 0; i < IO7_NUM_PORTS; i++) {
		io7_ioport_csrs *csrs = io7->ports[i].csrs;

		if (!io7->ports[i].enabled)
			continue;

		io->ports[i].pox_err_sum   = csrs->POx_ERR_SUM.csr;
		io->ports[i].pox_tlb_err   = csrs->POx_TLB_ERR.csr;
		io->ports[i].pox_spl_cmplt = csrs->POx_SPL_COMPLT.csr;
		io->ports[i].pox_trans_sum = csrs->POx_TRANS_SUM.csr;
		io->ports[i].pox_first_err = csrs->POx_FIRST_ERR.csr;
		io->ports[i].pox_mult_err  = csrs->POx_MULT_ERR.csr;
		io->ports[i].pox_dm_source = csrs->POx_DM_SOURCE.csr;
		io->ports[i].pox_dm_dest   = csrs->POx_DM_DEST.csr;
		io->ports[i].pox_dm_size   = csrs->POx_DM_SIZE.csr;
		io->ports[i].pox_dm_ctrl   = csrs->POx_DM_CTRL.csr;

		/*
		 * Ack this port's errors, if any. POx_ERR_SUM must be last.
		 *
		 * Most of the error registers get cleared and unlocked when
		 * the associated bits in POx_ERR_SUM are cleared (by writing
		 * 1). POx_TLB_ERR is an exception and must be explicitly 
		 * cleared.
		 */
		csrs->POx_TLB_ERR.csr = io->ports[i].pox_tlb_err;
		csrs->POx_ERR_SUM.csr =	io->ports[i].pox_err_sum;
		mb();
		csrs->POx_ERR_SUM.csr;		
	}

	/*
	 * Ack any port 7 error(s).
	 */
	io7->csrs->PO7_ERROR_SUM.csr = io->po7_error_sum;
	mb();
	io7->csrs->PO7_ERROR_SUM.csr;
	
	/*
	 * Correct the io7_pid.
	 */
	lf_subpackets->io_pid = io7->pe;

	return io;
}

static int
marvel_process_io_error(struct ev7_lf_subpackets *lf_subpackets, int print)
{
	int status = MCHK_DISPOSITION_UNKNOWN_ERROR;

#ifdef CONFIG_VERBOSE_MCHECK
	struct ev7_pal_io_subpacket *io = lf_subpackets->io;
	int i;
#endif /* CONFIG_VERBOSE_MCHECK */

#define MARVEL_IO_ERR_VALID(x)  ((x) & (1UL << 63))

	if (!lf_subpackets->logout || !lf_subpackets->io)
		return status;

	/*
	 * The PALcode only builds an IO subpacket if there is a 
	 * locally connected IO7. In the cases of
	 *	1) a uniprocessor kernel
	 *	2) an mp kernel before the local secondary has called in
	 * error interrupts are all directed to the primary processor.
	 * In that case, we may not have an IO subpacket at all and, event
	 * if we do, it may not be the right now. 
	 *
	 * If the RBOX indicates an I/O error interrupt, make sure we have
	 * the correct IO7 information. If we don't have an IO subpacket
	 * or it's the wrong one, try to find the right one.
	 *
	 * RBOX I/O error interrupts are indicated by RBOX_INT<29> and
	 * RBOX_INT<10>.
	 */
	if ((lf_subpackets->io->po7_error_sum & (1UL << 32)) ||
	    ((lf_subpackets->io->po7_error_sum        |
	      lf_subpackets->io->ports[0].pox_err_sum |
	      lf_subpackets->io->ports[1].pox_err_sum |
	      lf_subpackets->io->ports[2].pox_err_sum |
	      lf_subpackets->io->ports[3].pox_err_sum) & (1UL << 63))) {
		/*
		 * Either we have no IO subpacket or no error is
		 * indicated in the one we do have. Try find the
		 * one with the error.
		 */
		if (!marvel_find_io7_with_error(lf_subpackets))
			return status;
	}

	/*
	 * We have an IO7 indicating an error - we're going to report it
	 */
	status = MCHK_DISPOSITION_REPORT;

#ifdef CONFIG_VERBOSE_MCHECK

	if (!print)
		return status;

	printk("%s*Error occurred on IO7 at PID %u\n", 
	       err_print_prefix, lf_subpackets->io_pid);

	/*
	 * Check port 7 first
	 */
	if (lf_subpackets->io->po7_error_sum & IO7__PO7_ERRSUM__ERR_MASK) {
		marvel_print_po7_err_sum(io);

#if 0
		printk("%s  PORT 7 ERROR:\n"
		       "%s    PO7_ERROR_SUM: %016llx\n"
		       "%s    PO7_UNCRR_SYM: %016llx\n"
		       "%s    PO7_CRRCT_SYM: %016llx\n"
		       "%s    PO7_UGBGE_SYM: %016llx\n"
		       "%s    PO7_ERR_PKT0:  %016llx\n"
		       "%s    PO7_ERR_PKT1:  %016llx\n",
		       err_print_prefix,
		       err_print_prefix, io->po7_error_sum,
		       err_print_prefix, io->po7_uncrr_sym,
		       err_print_prefix, io->po7_crrct_sym,
		       err_print_prefix, io->po7_ugbge_sym,
		       err_print_prefix, io->po7_err_pkt0,
		       err_print_prefix, io->po7_err_pkt1);
#endif
	}

	/*
	 * Then loop through the ports
	 */
	for (i = 0; i < IO7_NUM_PORTS; i++) {
		if (!MARVEL_IO_ERR_VALID(io->ports[i].pox_err_sum))
			continue;

		printk("%s  PID %u PORT %d POx_ERR_SUM: %016llx\n",
		       err_print_prefix, 
		       lf_subpackets->io_pid, i, io->ports[i].pox_err_sum);
		marvel_print_pox_err(io->ports[i].pox_err_sum, &io->ports[i]);

		printk("%s  [ POx_FIRST_ERR: %016llx ]\n",
		       err_print_prefix, io->ports[i].pox_first_err);
		marvel_print_pox_err(io->ports[i].pox_first_err, 
				     &io->ports[i]);

	}


#endif /* CONFIG_VERBOSE_MCHECK */

	return status;
}

static int
marvel_process_logout_frame(struct ev7_lf_subpackets *lf_subpackets, int print)
{
	int status = MCHK_DISPOSITION_UNKNOWN_ERROR;

	/*
	 * I/O error? 
	 */
#define EV7__RBOX_INT__IO_ERROR__MASK 0x20000400ul
	if (lf_subpackets->logout &&
	    (lf_subpackets->logout->rbox_int & 0x20000400ul))
		status = marvel_process_io_error(lf_subpackets, print);

	/*
	 * Probing behind PCI-X bridges can cause machine checks on
	 * Marvel when the probe is handled by the bridge as a split
	 * completion transaction. The symptom is an ERROR_RESPONSE 
	 * to a CONFIG address. Since these errors will happen in
	 * normal operation, dismiss them.
	 *
	 * Dismiss if:
	 *	C_STAT		= 0x14 		(Error Reponse)
	 *	C_STS<3>	= 0    		(C_ADDR valid)
	 *	C_ADDR<42>	= 1    		(I/O)
	 *	C_ADDR<31:22>	= 111110xxb	(PCI Config space)
	 */
	if (lf_subpackets->ev7 &&
	    (lf_subpackets->ev7->c_stat == 0x14) &&
	    !(lf_subpackets->ev7->c_sts & 0x8) &&
	    ((lf_subpackets->ev7->c_addr & 0x400ff000000ul) 
	     == 0x400fe000000ul))
		status = MCHK_DISPOSITION_DISMISS;

	return status;
}

void
marvel_machine_check(u64 vector, u64 la_ptr)
{
	struct el_subpacket *el_ptr = (struct el_subpacket *)la_ptr;
	int (*process_frame)(struct ev7_lf_subpackets *, int) = NULL;
	struct ev7_lf_subpackets subpacket_collection = { NULL, };
	struct ev7_pal_io_subpacket scratch_io_packet = { 0, };
	struct ev7_lf_subpackets *lf_subpackets = NULL;
	int disposition = MCHK_DISPOSITION_UNKNOWN_ERROR;
	char *saved_err_prefix = err_print_prefix;
	char *error_type = NULL;

	/*
	 * Sync the processor
	 */
	mb();
	draina();

	switch(vector) {
	case SCB_Q_SYSEVENT:
		process_frame = marvel_process_680_frame;
		error_type = "System Event";
		break;

	case SCB_Q_SYSMCHK:
		process_frame = marvel_process_logout_frame;
		error_type = "System Uncorrectable Error";
		break;

	case SCB_Q_SYSERR:
		process_frame = marvel_process_logout_frame;
		error_type = "System Correctable Error";
		break;

	default:
		/* Don't know it - pass it up.  */
		ev7_machine_check(vector, la_ptr);
		return;
	}	

	/*
	 * A system event or error has occurred, handle it here.
	 *
	 * Any errors in the logout frame have already been cleared by the
	 * PALcode, so just parse it.
	 */
	err_print_prefix = KERN_CRIT;

	/* 
	 * Parse the logout frame without printing first. If the only error(s)
	 * found are classified as "dismissable", then just dismiss them and
	 * don't print any message
	 */
	lf_subpackets = 
		ev7_collect_logout_frame_subpackets(el_ptr,
						    &subpacket_collection);
	if (process_frame && lf_subpackets && lf_subpackets->logout) {
		/*
		 * We might not have the correct (or any) I/O subpacket.
		 * [ See marvel_process_io_error() for explanation. ]
		 * If we don't have one, point the io subpacket in
		 * lf_subpackets at scratch_io_packet so that 
		 * marvel_find_io7_with_error() will have someplace to
		 * store the info.
		 */
		if (!lf_subpackets->io)
			lf_subpackets->io = &scratch_io_packet;

		/*
		 * Default io_pid to the processor reporting the error
		 * [this will get changed in marvel_find_io7_with_error()
		 * if a different one is needed]
		 */
		lf_subpackets->io_pid = lf_subpackets->logout->whami;

		/*
		 * Evaluate the frames.
		 */
		disposition = process_frame(lf_subpackets, 0);
	}
	switch(disposition) {
	case MCHK_DISPOSITION_DISMISS:
		/* Nothing to do. */
		break;

	case MCHK_DISPOSITION_REPORT:
		/* Recognized error, report it. */
		printk("%s*%s (Vector 0x%x) reported on CPU %d\n",
		       err_print_prefix, error_type,
		       (unsigned int)vector, (int)smp_processor_id());
		el_print_timestamp(&lf_subpackets->logout->timestamp);
		process_frame(lf_subpackets, 1);
		break;

	default:
		/* Unknown - dump the annotated subpackets. */
		printk("%s*%s (Vector 0x%x) reported on CPU %d\n",
		       err_print_prefix, error_type,
		       (unsigned int)vector, (int)smp_processor_id());
		el_process_subpacket(el_ptr);
		break;

	}

	err_print_prefix = saved_err_prefix;

        /* Release the logout frame.  */
	wrmces(0x7);
	mb();
}

void __init
marvel_register_error_handlers(void)
{
	ev7_register_error_handlers();
}
