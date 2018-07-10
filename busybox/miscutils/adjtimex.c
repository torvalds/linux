/* vi: set sw=4 ts=4: */
/*
 * adjtimex.c - read, and possibly modify, the Linux kernel 'timex' variables.
 *
 * Originally written: October 1997
 * Last hack: March 2001
 * Copyright 1997, 2000, 2001 Larry Doolittle <LRDoolittle@lbl.gov>
 *
 * busyboxed 20 March 2001, Larry Doolittle <ldoolitt@recycle.lbl.gov>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config ADJTIMEX
//config:	bool "adjtimex (4.5 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Adjtimex reads and optionally sets adjustment parameters for
//config:	the Linux clock adjustment algorithm.

//applet:IF_ADJTIMEX(APPLET_NOFORK(adjtimex, adjtimex, BB_DIR_SBIN, BB_SUID_DROP, adjtimex))

//kbuild:lib-$(CONFIG_ADJTIMEX) += adjtimex.o

//usage:#define adjtimex_trivial_usage
//usage:       "[-q] [-o OFF] [-f FREQ] [-p TCONST] [-t TICK]"
//usage:#define adjtimex_full_usage "\n\n"
//usage:       "Read or set kernel time variables. See adjtimex(2)\n"
//usage:     "\n	-q	Quiet"
//usage:     "\n	-o OFF	Time offset, microseconds"
//usage:     "\n	-f FREQ	Frequency adjust, integer kernel units (65536 is 1ppm)"
//usage:     "\n	-t TICK	Microseconds per tick, usually 10000"
//usage:     "\n		(positive -t or -f values make clock run faster)"
//usage:     "\n	-p TCONST"

#include "libbb.h"
#ifdef __BIONIC__
# include <linux/timex.h>
#else
# include <sys/timex.h>
#endif

static const uint16_t statlist_bit[] ALIGN2 = {
	STA_PLL,
	STA_PPSFREQ,
	STA_PPSTIME,
	STA_FLL,
	STA_INS,
	STA_DEL,
	STA_UNSYNC,
	STA_FREQHOLD,
	STA_PPSSIGNAL,
	STA_PPSJITTER,
	STA_PPSWANDER,
	STA_PPSERROR,
	STA_CLOCKERR,
	0
};
static const char statlist_name[] ALIGN1 =
	"PLL"       "\0"
	"PPSFREQ"   "\0"
	"PPSTIME"   "\0"
	"FFL"       "\0"
	"INS"       "\0"
	"DEL"       "\0"
	"UNSYNC"    "\0"
	"FREQHOLD"  "\0"
	"PPSSIGNAL" "\0"
	"PPSJITTER" "\0"
	"PPSWANDER" "\0"
	"PPSERROR"  "\0"
	"CLOCKERR"
;

static const char ret_code_descript[] ALIGN1 =
	"clock synchronized" "\0"
	"insert leap second" "\0"
	"delete leap second" "\0"
	"leap second in progress" "\0"
	"leap second has occurred" "\0"
	"clock not synchronized"
;

int adjtimex_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int adjtimex_main(int argc UNUSED_PARAM, char **argv)
{
	enum {
		OPT_quiet = 0x1
	};
	unsigned opt;
	char *opt_o, *opt_f, *opt_p, *opt_t;
	struct timex txc;
	int ret;
	const char *descript;

	memset(&txc, 0, sizeof(txc));

	opt = getopt32(argv, "^" "qo:f:p:t:"
			"\0" "=0"/*no valid non-option args*/,
			&opt_o, &opt_f, &opt_p, &opt_t
	);
	//if (opt & 0x1) // -q
	if (opt & 0x2) { // -o
		txc.offset = xatol(opt_o);
		txc.modes |= ADJ_OFFSET_SINGLESHOT;
	}
	if (opt & 0x4) { // -f
		txc.freq = xatol(opt_f);
		txc.modes |= ADJ_FREQUENCY;
	}
	if (opt & 0x8) { // -p
		txc.constant = xatol(opt_p);
		txc.modes |= ADJ_TIMECONST;
	}
	if (opt & 0x10) { // -t
		txc.tick = xatol(opt_t);
		txc.modes |= ADJ_TICK;
	}

	/* It's NOFORK applet because the code is very simple:
	 * just some printf. No opens, no allocs.
	 * If you need to make it more complex, feel free to downgrade to NOEXEC
	 */

	ret = adjtimex(&txc);
	if (ret < 0)
		bb_perror_nomsg_and_die();

	if (!(opt & OPT_quiet)) {
		const char *sep;
		const char *name;
		int i;

		printf(
			"    mode:         %d\n"
			"-o  offset:       %ld us\n"
			"-f  freq.adjust:  %ld (65536 = 1ppm)\n"
			"    maxerror:     %ld\n"
			"    esterror:     %ld\n"
			"    status:       %d (",
			txc.modes, txc.offset, txc.freq, txc.maxerror,
			txc.esterror, txc.status
		);

		/* representative output of next code fragment:
		 * "PLL | PPSTIME"
		 */
		name = statlist_name;
		sep = "";
		for (i = 0; statlist_bit[i]; i++) {
			if (txc.status & statlist_bit[i]) {
				printf("%s%s", sep, name);
				sep = " | ";
			}
			name += strlen(name) + 1;
		}

		descript = "error";
		if (ret <= 5)
			descript = nth_string(ret_code_descript, ret);
		printf(")\n"
			"-p  timeconstant: %ld\n"
			"    precision:    %ld us\n"
			"    tolerance:    %ld\n"
			"-t  tick:         %ld us\n"
			"    time.tv_sec:  %ld\n"
			"    time.tv_usec: %ld\n"
			"    return value: %d (%s)\n",
			txc.constant,
			txc.precision, txc.tolerance, txc.tick,
			(long)txc.time.tv_sec, (long)txc.time.tv_usec,
			ret, descript
		);
	}

	return 0;
}
