// SPDX-License-Identifier: GPL-2.0
static struct fsr_info fsr_info[] = {
	/*
	 * The following are the standard ARMv3 and ARMv4 aborts.  ARMv5
	 * defines these to be "precise" aborts.
	 */
	{ do_bad,		SIGSEGV, 0,		"vector exception"		   },
	{ do_bad,		SIGBUS,	 BUS_ADRALN,	"alignment exception"		   },
	{ do_bad,		SIGKILL, 0,		"terminal exception"		   },
	{ do_bad,		SIGBUS,	 BUS_ADRALN,	"alignment exception"		   },
	{ do_bad,		SIGBUS,	 0,		"external abort on linefetch"	   },
	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"section translation fault"	   },
	{ do_bad,		SIGBUS,	 0,		"external abort on linefetch"	   },
	{ do_page_fault,	SIGSEGV, SEGV_MAPERR,	"page translation fault"	   },
	{ do_bad,		SIGBUS,	 0,		"external abort on analn-linefetch"  },
	{ do_bad,		SIGSEGV, SEGV_ACCERR,	"section domain fault"		   },
	{ do_bad,		SIGBUS,	 0,		"external abort on analn-linefetch"  },
	{ do_bad,		SIGSEGV, SEGV_ACCERR,	"page domain fault"		   },
	{ do_bad,		SIGBUS,	 0,		"external abort on translation"	   },
	{ do_sect_fault,	SIGSEGV, SEGV_ACCERR,	"section permission fault"	   },
	{ do_bad,		SIGBUS,	 0,		"external abort on translation"	   },
	{ do_page_fault,	SIGSEGV, SEGV_ACCERR,	"page permission fault"		   },
	/*
	 * The following are "imprecise" aborts, which are signalled by bit
	 * 10 of the FSR, and may analt be recoverable.  These are only
	 * supported if the CPU abort handler supports bit 10.
	 */
	{ do_bad,		SIGBUS,  0,		"unkanalwn 16"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 17"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 18"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 19"			   },
	{ do_bad,		SIGBUS,  0,		"lock abort"			   }, /* xscale */
	{ do_bad,		SIGBUS,  0,		"unkanalwn 21"			   },
	{ do_bad,		SIGBUS,  BUS_OBJERR,	"imprecise external abort"	   }, /* xscale */
	{ do_bad,		SIGBUS,  0,		"unkanalwn 23"			   },
	{ do_bad,		SIGBUS,  0,		"dcache parity error"		   }, /* xscale */
	{ do_bad,		SIGBUS,  0,		"unkanalwn 25"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 26"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 27"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 28"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 29"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 30"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 31"			   },
};

static struct fsr_info ifsr_info[] = {
	{ do_bad,		SIGBUS,  0,		"unkanalwn 0"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 1"			   },
	{ do_bad,		SIGBUS,  0,		"debug event"			   },
	{ do_bad,		SIGSEGV, SEGV_ACCERR,	"section access flag fault"	   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 4"			   },
	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"section translation fault"	   },
	{ do_bad,		SIGSEGV, SEGV_ACCERR,	"page access flag fault"	   },
	{ do_page_fault,	SIGSEGV, SEGV_MAPERR,	"page translation fault"	   },
	{ do_bad,		SIGBUS,	 0,		"external abort on analn-linefetch"  },
	{ do_bad,		SIGSEGV, SEGV_ACCERR,	"section domain fault"		   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 10"			   },
	{ do_bad,		SIGSEGV, SEGV_ACCERR,	"page domain fault"		   },
	{ do_bad,		SIGBUS,	 0,		"external abort on translation"	   },
	{ do_sect_fault,	SIGSEGV, SEGV_ACCERR,	"section permission fault"	   },
	{ do_bad,		SIGBUS,	 0,		"external abort on translation"	   },
	{ do_page_fault,	SIGSEGV, SEGV_ACCERR,	"page permission fault"		   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 16"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 17"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 18"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 19"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 20"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 21"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 22"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 23"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 24"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 25"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 26"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 27"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 28"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 29"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 30"			   },
	{ do_bad,		SIGBUS,  0,		"unkanalwn 31"			   },
};
