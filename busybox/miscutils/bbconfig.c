/* vi: set sw=4 ts=4: */
/*
 * This file was released into the public domain by Paul Fox.
 */
//config:config BBCONFIG
//config:	bool "bbconfig (9.7 kb)"
//config:	default n
//config:	help
//config:	The bbconfig applet will print the config file with which
//config:	busybox was built.
//config:
//config:config FEATURE_COMPRESS_BBCONFIG
//config:	bool "Compress bbconfig data"
//config:	default y
//config:	depends on BBCONFIG
//config:	help
//config:	Store bbconfig data in compressed form, uncompress them on-the-fly
//config:	before output.
//config:
//config:	If you have a really tiny busybox with few applets enabled (and
//config:	bunzip2 isn't one of them), the overhead of the decompressor might
//config:	be noticeable. Also, if you run executables directly from ROM
//config:	and have very little memory, this might not be a win. Otherwise,
//config:	you probably want this.

//applet:IF_BBCONFIG(APPLET(bbconfig, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_BBCONFIG) += bbconfig.o

//usage:#define bbconfig_trivial_usage
//usage:       ""
//usage:#define bbconfig_full_usage "\n\n"
//usage:       "Print the config file used by busybox build"

#include "libbb.h"
#include "bbconfigopts.h"
#if ENABLE_FEATURE_COMPRESS_BBCONFIG
# include "bb_archive.h"
# include "bbconfigopts_bz2.h"
#endif

int bbconfig_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int bbconfig_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
#if ENABLE_FEATURE_COMPRESS_BBCONFIG
	bunzip_data *bd;
	int i;
	jmp_buf jmpbuf;

	/* Setup for I/O error handling via longjmp */
	i = setjmp(jmpbuf);
	if (i == 0) {
		i = start_bunzip(&jmpbuf,
			&bd,
			/* src_fd: */ -1,
			/* inbuf:  */ bbconfig_config_bz2,
			/* len:    */ sizeof(bbconfig_config_bz2)
		);
	}
	/* read_bunzip can longjmp and end up here with i != 0
	 * on read data errors! Not trivial */
	if (i == 0) {
		/* Cannot use xmalloc: will leak bd in NOFORK case! */
		char *outbuf = malloc_or_warn(sizeof(bbconfig_config));
		if (outbuf) {
			read_bunzip(bd, outbuf, sizeof(bbconfig_config));
			full_write1_str(outbuf);
		}
	}
#else
	full_write1_str(bbconfig_config);
#endif
	return 0;
}
