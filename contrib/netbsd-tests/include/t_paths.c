/*	$NetBSD: t_paths.c,v 1.16 2015/05/07 06:23:23 pgoyette Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_paths.c,v 1.16 2015/05/07 06:23:23 pgoyette Exp $");

#include <sys/param.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#define PATH_DEV	__BIT(0)	/* A device node	*/
#define PATH_DIR	__BIT(1)	/* A directory		*/
#define PATH_FILE	__BIT(2)	/* A file		*/
#define PATH_ROOT	__BIT(3)	/* Access for root only */
#define PATH_OPT	__BIT(3)	/* Optional, ENODEV if not supported */

static const struct {
	const char	*path;
	int		 flags;
} paths[] = {
	{ _PATH_AUDIO,		PATH_DEV		},
	{ _PATH_AUDIO0,		PATH_DEV		},
	{ _PATH_AUDIOCTL,	PATH_DEV		},
	{ _PATH_AUDIOCTL0,	PATH_DEV		},
	{ _PATH_BPF,		PATH_DEV | PATH_ROOT	},
	{ _PATH_CLOCKCTL,	PATH_DEV | PATH_ROOT	},
	{ _PATH_CONSOLE,	PATH_DEV | PATH_ROOT	},
	{ _PATH_CONSTTY,	PATH_DEV | PATH_ROOT	},
	{ _PATH_CPUCTL,		PATH_DEV		},
	{ _PATH_CSMAPPER,	PATH_DIR		},
	{ _PATH_DEFTAPE,	PATH_DEV | PATH_ROOT	},
	{ _PATH_DEVCDB,		PATH_FILE		},
	{ _PATH_DEVDB,		PATH_FILE		},
	{ _PATH_DEVNULL,	PATH_DEV		},
	{ _PATH_DRUM,		PATH_DEV | PATH_ROOT	},
	{ _PATH_ESDB,		PATH_DIR		},
	{ _PATH_FTPUSERS,	PATH_FILE		},
	{ _PATH_GETTYTAB,	PATH_FILE		},
	{ _PATH_I18NMODULE,	PATH_DIR		},
	{ _PATH_ICONV,		PATH_DIR		},
	{ _PATH_KMEM,		PATH_DEV | PATH_ROOT	},
	{ _PATH_KSYMS,		PATH_DEV		},
	{ _PATH_KVMDB,		PATH_FILE		},
	{ _PATH_LOCALE,		PATH_DIR		},
	{ _PATH_MAILDIR,	PATH_DIR		},
	{ _PATH_MAN,		PATH_DIR		},
	{ _PATH_MEM,		PATH_DEV | PATH_ROOT	},
	{ _PATH_MIXER,		PATH_DEV		},
	{ _PATH_MIXER0,		PATH_DEV		},
	{ _PATH_NOLOGIN,	PATH_FILE		},
	{ _PATH_POWER,		PATH_DEV | PATH_ROOT | PATH_OPT	},
	{ _PATH_PRINTCAP,	PATH_FILE		},
	{ _PATH_PUD,		PATH_DEV | PATH_ROOT	},
	{ _PATH_PUFFS,		PATH_DEV | PATH_ROOT	},
	{ _PATH_RANDOM,		PATH_DEV		},
	{ _PATH_SENDMAIL,	PATH_FILE		},
	{ _PATH_SHELLS,		PATH_FILE		},
	{ _PATH_SKEYKEYS,	PATH_FILE | PATH_ROOT	},
	{ _PATH_SOUND,		PATH_DEV		},
	{ _PATH_SOUND0,		PATH_DEV		},
	{ _PATH_SYSMON,		PATH_DEV | PATH_OPT	},
	{ _PATH_TTY,		PATH_DEV		},
	{ _PATH_UNIX,		PATH_FILE | PATH_ROOT	},
	{ _PATH_URANDOM,	PATH_DEV		},
	{ _PATH_VIDEO,		PATH_DEV		},
	{ _PATH_VIDEO0,		PATH_DEV		},
	{ _PATH_WATCHDOG,	PATH_DEV | PATH_OPT	},

	{ _PATH_DEV,		PATH_DIR		},
	{ _PATH_DEV_PTS,	PATH_DIR		},
	{ _PATH_EMUL_AOUT,	PATH_DIR		},
	{ _PATH_TMP,		PATH_DIR		},
	{ _PATH_VARDB,		PATH_DIR		},
	{ _PATH_VARRUN,		PATH_DIR		},
	{ _PATH_VARTMP,		PATH_DIR		},

	{ _PATH_BSHELL,		PATH_FILE		},
	{ _PATH_CSHELL,		PATH_FILE		},
	{ _PATH_VI,		PATH_FILE		},
};

ATF_TC(paths);
ATF_TC_HEAD(paths, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test for <paths.h>");
}

ATF_TC_BODY(paths, tc)
{
	struct stat st;
	uid_t uid;
	mode_t m;
	size_t i;
	int fd;

#if defined(__sparc__)
	atf_tc_skip("PR port-sparc/45580");
#endif

	uid = getuid();

	for (i = 0; i < __arraycount(paths); i++) {

		(void)fprintf(stderr, "testing '%s'\n", paths[i].path);

		errno = 0;
		fd = open(paths[i].path, O_RDONLY);

		if (fd < 0) {

			switch (errno) {

			case ENODEV:
				if ((paths[i].flags & PATH_OPT) == 0) {

					atf_tc_fail("Required path %s does "
					    "not exist", paths[i].path);
				}
				break;

			case EPERM:	/* FALLTHROUGH */
			case EACCES:	/* FALLTHROUGH */

				if ((paths[i].flags & PATH_ROOT) == 0) {

					atf_tc_fail("UID %u failed to open %s, "
					    "error %d", (uint32_t)uid,
					     paths[i].path, errno);
				}

			case EBUSY:	/* FALLTHROUGH */
			case ENXIO:	/* FALLTHROUGH */
			case ENOENT:	/* FALLTHROUGH */

			default:
				continue;
			}
		}

		(void)memset(&st, 0, sizeof(struct stat));

		ATF_REQUIRE(fstat(fd, &st) == 0);

		m = st.st_mode;

		if ((paths[i].flags & PATH_DEV) != 0) {

			ATF_CHECK(S_ISBLK(m) != 0 || S_ISCHR(m) != 0);

			ATF_CHECK((paths[i].flags & PATH_DIR) == 0);
			ATF_CHECK((paths[i].flags & PATH_FILE) == 0);
		}

		if ((paths[i].flags & PATH_DIR) != 0) {

			ATF_CHECK(S_ISDIR(m) != 0);

			ATF_CHECK((paths[i].flags & PATH_DEV) == 0);
			ATF_CHECK((paths[i].flags & PATH_FILE) == 0);
		}

		if ((paths[i].flags & PATH_FILE) != 0) {

			ATF_CHECK(S_ISREG(m) != 0);

			ATF_CHECK((paths[i].flags & PATH_DEV) == 0);
			ATF_CHECK((paths[i].flags & PATH_DIR) == 0);
		}

		ATF_REQUIRE(close(fd) == 0);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, paths);

	return atf_no_error();
}
