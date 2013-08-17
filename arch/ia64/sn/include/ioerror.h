/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_IOERROR_H
#define _ASM_IA64_SN_IOERROR_H

/*
 * IO error structure.
 *
 * This structure would expand to hold the information retrieved from
 * all IO related error registers.
 *
 * This structure is defined to hold all system specific
 * information related to a single error.
 *
 * This serves a couple of purpose.
 *      - Error handling often involves translating one form of address to other
 *        form. So, instead of having different data structures at each level,
 *        we have a single structure, and the appropriate fields get filled in
 *        at each layer.
 *      - This provides a way to dump all error related information in any layer
 *        of erorr handling (debugging aid).
 *
 * A second possibility is to allow each layer to define its own error
 * data structure, and fill in the proper fields. This has the advantage
 * of isolating the layers.
 * A big concern is the potential stack usage (and overflow), if each layer
 * defines these structures on stack (assuming we don't want to do kmalloc.
 *
 * Any layer wishing to pass extra information to a layer next to it in
 * error handling hierarchy, can do so as a separate parameter.
 */

typedef struct io_error_s {
    /* Bit fields indicating which structure fields are valid */
    union {
	struct {
	    unsigned                ievb_errortype:1;
	    unsigned                ievb_widgetnum:1;
	    unsigned                ievb_widgetdev:1;
	    unsigned                ievb_srccpu:1;
	    unsigned                ievb_srcnode:1;
	    unsigned                ievb_errnode:1;
	    unsigned                ievb_sysioaddr:1;
	    unsigned                ievb_xtalkaddr:1;
	    unsigned                ievb_busspace:1;
	    unsigned                ievb_busaddr:1;
	    unsigned                ievb_vaddr:1;
	    unsigned                ievb_memaddr:1;
	    unsigned		    ievb_epc:1;
	    unsigned		    ievb_ef:1;
	    unsigned		    ievb_tnum:1;
	} iev_b;
	unsigned                iev_a;
    } ie_v;

    short                   ie_errortype;	/* error type: extra info about error */
    short                   ie_widgetnum;	/* Widget number that's in error */
    short                   ie_widgetdev;	/* Device within widget in error */
    cpuid_t                 ie_srccpu;	/* CPU on srcnode generating error */
    cnodeid_t               ie_srcnode;		/* Node which caused the error   */
    cnodeid_t               ie_errnode;		/* Node where error was noticed  */
    iopaddr_t               ie_sysioaddr;	/* Sys specific IO address       */
    iopaddr_t               ie_xtalkaddr;	/* Xtalk (48bit) addr of Error   */
    iopaddr_t               ie_busspace;	/* Bus specific address space    */
    iopaddr_t               ie_busaddr;		/* Bus specific address          */
    caddr_t                 ie_vaddr;	/* Virtual address of error      */
    iopaddr_t               ie_memaddr;		/* Physical memory address       */
    caddr_t		    ie_epc;		/* pc when error reported	 */
    caddr_t		    ie_ef;		/* eframe when error reported	 */
    short		    ie_tnum;		/* Xtalk TNUM field */
} ioerror_t;

#define	IOERROR_INIT(e)		do { (e)->ie_v.iev_a = 0; } while (0)
#define	IOERROR_SETVALUE(e,f,v)	do { (e)->ie_ ## f = (v); (e)->ie_v.iev_b.ievb_ ## f = 1; } while (0)

#endif /* _ASM_IA64_SN_IOERROR_H */
