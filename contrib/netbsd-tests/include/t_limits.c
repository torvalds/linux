/* $NetBSD: t_limits.c,v 1.2 2011/04/04 18:02:01 jruoho Exp $ */

/*
 * Copyright (c) 2008, 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
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

/*
 * Copyright (c) 2009, Stathis Kamperis
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008, 2010\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_limits.c,v 1.2 2011/04/04 18:02:01 jruoho Exp $");

#include <limits.h>

#include <atf-c.h>

struct psxentry {
	const char	*psx_desc;
	int		 psx_constant;
	int		 psx_minval;
} psxtable[] = {

	/*
	 * POSIX symbolic constants.
	 */
#ifdef	_POSIX_AIO_LISTIO_MAX
	{ "_POSIX_AIO_LISTIO_MAX",	_POSIX_AIO_LISTIO_MAX,		2    },
#endif
#ifdef	_POSIX_AIO_MAX
	{ "_POSIX_AIO_MAX",		_POSIX_AIO_MAX,			1    },
#endif
#ifdef	_POSIX_ARG_MAX
	{ "_POSIX_ARG_MAX",		_POSIX_ARG_MAX,			4096 },
#endif
#ifdef	_POSIX_CHILD_MAX
	{ "_POSIX_CHILD_MAX" ,		_POSIX_CHILD_MAX,		25   },
#endif
#ifdef	_POSIX_DELAYTIMER_MAX
	{ "_POSIX_DELAYTIMER_MAX",	_POSIX_DELAYTIMER_MAX,		32   },
#endif
#ifdef	_POSIX_HOST_NAME_MAX
	{ "_POSIX_HOST_NAME_MAX",	_POSIX_HOST_NAME_MAX,		255  },
#endif
#ifdef	_POSIX_LINK_MAX
	{ "_POSIX_LINK_MAX",		_POSIX_LINK_MAX,		8    },
#endif
#ifdef	_POSIX_LOGIN_NAME_MAX
	{ "_POSIX_LOGIN_NAME_MAX",	_POSIX_LOGIN_NAME_MAX,		9    },
#endif
#ifdef	_POSIX_MAX_CANON
	{ "_POSIX_MAX_CANON",		_POSIX_MAX_CANON,		255  },
#endif
#ifdef	_POSIX_MAX_INPUT
	{ "_POSIX_MAX_INPUT",		_POSIX_MAX_INPUT,		255  },
#endif
#ifdef	_POSIX_MQ_OPEN_MAX
	{ "_POSIX_MQ_OPEN_MAX",		_POSIX_MQ_OPEN_MAX,		8    },
#endif
#ifdef	_POSIX_MQ_PRIO_MAX
	{ "_POSIX_MQ_PRIO_MAX",		_POSIX_MQ_PRIO_MAX,		32   },
#endif
#ifdef	_POSIX_NAME_MAX
	{ "_POSIX_NAME_MAX",		_POSIX_NAME_MAX,		9    },
#endif
#ifdef	_POSIX_NGROUPS_MAX
	{ "_POSIX_NGROUPS_MAX",		_POSIX_NGROUPS_MAX,		8    },
#endif
#ifdef	_POSIX_OPEN_MAX
	{ "_POSIX_OPEN_MAX",		_POSIX_OPEN_MAX,		20   },
#endif
#ifdef	_POSIX_PATH_MAX
	{ "_POSIX_PATH_MAX",		_POSIX_PATH_MAX,		256  },
#endif
#ifdef	_POSIX_PIPE_BUF
	{ "_POSIX_PIPE_BUF",		_POSIX_PIPE_BUF,		512  },
#endif
#ifdef	_POSIX_RE_DUP_MAX
	{ "_POSIX_RE_DUP_MAX",		_POSIX_RE_DUP_MAX,		255  },
#endif
#ifdef	_POSIX_RTSIG_MAX
	{ "_POSIX_RTSIG_MAX",		_POSIX_RTSIG_MAX,		8    },
#endif
#ifdef	_POSIX_SEM_NSEMS_MAX
	{ "_POSIX_SEM_NSEMS_MAX",	_POSIX_SEM_NSEMS_MAX,		256  },
#endif
#ifdef	_POSIX_SEM_VALUE_MAX
	{ "_POSIX_SEM_VALUE_MAX",	_POSIX_SEM_VALUE_MAX,		32767},
#endif
#ifdef	_POSIX_SIGQUEUE_MAX
	{ "_POSIX_SIGQUEUE_MAX",	_POSIX_SIGQUEUE_MAX,		32   },
#endif
#ifdef	_POSIX_SSIZE_MAX
	{ "_POSIX_SSIZE_MAX",		_POSIX_SSIZE_MAX,		32767},
#endif
#ifdef	_POSIX_STREAM_MAX
	{ "_POSIX_STREAM_MAX",		_POSIX_STREAM_MAX,		8    },
#endif
#ifdef	_POSIX_SS_REPL_MAX
	{ "_POSIX_SS_REPL_MAX",		_POSIX_SS_REPL_MAX,		4    },
#endif
#ifdef	_POSIX_SYMLINK_MAX
	{ "_POSIX_SYMLINK_MAX",		_POSIX_SYMLINK_MAX,		255  },
#endif
#ifdef	_POSIX_SYMLOOP_MAX
	{ "_POSIX_SYMLOOP_MAX",		_POSIX_SYMLOOP_MAX,		8    },
#endif
#ifdef	_POSIX_THREAD_DESTRUCTOR_ITERATIONS
	{ "_POSIX_THREAD_DESTRUCTOR_ITERATIONS",
	  _POSIX_THREAD_DESTRUCTOR_ITERATIONS,				4    },
#endif
#ifdef	_POSIX_THREAD_KEYS_MAX
	{ "_POSIX_THREAD_KEYS_MAX",	_POSIX_THREAD_KEYS_MAX,		128  },
#endif
#ifdef	_POSIX_THREAD_THREADS_MAX
	{ "_POSIX_THREAD_THREADS_MAX",	_POSIX_THREAD_THREADS_MAX,	64   },
#endif
#ifdef	_POSIX_TIMER_MAX
	{ "_POSIX_TIMER_MAX",		_POSIX_TIMER_MAX,		32   },
#endif
#ifdef	_POSIX_TRACE_EVENT_NAME_MAX
	{ "_POSIX_TRACE_EVENT_NAME_MAX",_POSIX_TRACE_EVENT_NAME_MAX,	30   },
#endif
#ifdef	_POSIX_TRACE_NAME_MAX
	{ "_POSIX_TRACE_NAME_MAX",	_POSIX_TRACE_NAME_MAX,		8    },
#endif
#ifdef	_POSIX_TRACE_SYS_MAX
	{ "_POSIX_TRACE_SYS_MAX",	_POSIX_TRACE_SYS_MAX,		8    },
#endif
#ifdef	_POSIX_TRACE_USER_EVENT_MAX
	{ "_POSIX_TRACE_USER_EVENT_MAX",_POSIX_TRACE_USER_EVENT_MAX,	32   },
#endif
#ifdef	_POSIX_TTY_NAME_MAX
	{ "_POSIX_TTY_NAME_MAX",	_POSIX_TTY_NAME_MAX,		9    },
#endif
#ifdef	_POSIX_TZNAME_MAX
	{ "_POSIX_TZNAME_MAX",		_POSIX_TZNAME_MAX,		6    },
#endif
#ifdef	_POSIX2_BC_BASE_MAX
	{ "_POSIX2_BC_BASE_MAX",	_POSIX2_BC_BASE_MAX,		99   },
#endif
#ifdef	_POSIX2_BC_DIM_MAX
	{ "_POSIX2_BC_DIM_MAX",		_POSIX2_BC_DIM_MAX,		2048 },
#endif
#ifdef	_POSIX2_BC_SCALE_MAX
	{ "_POSIX2_BC_SCALE_MAX",	_POSIX2_BC_SCALE_MAX,		99   },
#endif
#ifdef	_POSIX2_BC_STRING_MAX
	{ "_POSIX2_BC_STRING_MAX",	_POSIX2_BC_STRING_MAX,		1000 },
#endif
#ifdef	_POSIX2_CHARCLASS_NAME_MAX
	{ "_POSIX2_CHARCLASS_NAME_MAX",	_POSIX2_CHARCLASS_NAME_MAX,	14   },
#endif
#ifdef	_POSIX2_COLL_WEIGHTS_MAX
	{ "_POSIX2_COLL_WEIGHTS_MAX",	_POSIX2_COLL_WEIGHTS_MAX,	2    },
#endif
#ifdef	_POSIX2_EXPR_NEST_MAX
	{ "_POSIX2_EXPR_NEST_MAX",	_POSIX2_EXPR_NEST_MAX,		32   },
#endif
#ifdef	_POSIX2_LINE_MAX
	{ "_POSIX2_LINE_MAX",		_POSIX2_LINE_MAX,		2048 },
#endif
#ifdef	_POSIX2_RE_DUP_MAX
	{ "_POSIX2_RE_DUP_MAX",		_POSIX2_RE_DUP_MAX,		255  },
#endif
#ifdef	_XOPEN_IOV_MAX
	{ "_XOPEN_IOV_MAX",		_XOPEN_IOV_MAX,			16   },
#endif
#ifdef	_XOPEN_NAME_MAX
	{ "_XOPEN_NAME_MAX",		_XOPEN_NAME_MAX,		255  },
#endif
#ifdef	_XOPEN_PATH_MAX
	{ "_XOPEN_PATH_MAX",		_XOPEN_PATH_MAX,		1024 },
#endif

	/*
	 * Other invariant values.
	 */
#ifdef	NL_ARGMAX
	{ "NL_ARGMAX",			NL_ARGMAX,			9    },
#endif
#ifdef	NL_LANGMAX
	{ "NL_LANGMAX",			NL_LANGMAX,			14   },
#endif
#ifdef	NL_MSGMAX
	{ "NL_MSGMAX",			NL_MSGMAX,			32767},
#endif
#ifdef	NL_SETMAX
	{ "NL_SETMAX",			NL_SETMAX,			255  },
#endif
#ifdef	NL_TEXTMAX
#ifdef	_POSIX2_LINE_MAX
	{ "NL_TEXTMAX",			NL_TEXTMAX,	     _POSIX2_LINE_MAX},
#endif
#endif
#ifdef	NZERO
	{ "NZERO",			NZERO,				20   },
#endif
};

ATF_TC_WITHOUT_HEAD(char);
ATF_TC_BODY(char, tc)
{
	ATF_CHECK(CHAR_MIN < UCHAR_MAX);
}

ATF_TC(posix);
ATF_TC_HEAD(posix, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test POSIX limits");
}

ATF_TC_BODY(posix, tc)
{
	size_t i;

	for (i = 0; i < __arraycount(psxtable); i++) {

		if (psxtable[i].psx_constant < psxtable[i].psx_minval) {

			atf_tc_fail("%s is less than the minimum",
			    psxtable[i].psx_desc);
		}
	}
}

ATF_TC_WITHOUT_HEAD(short);
ATF_TC_BODY(short, tc)
{
	ATF_CHECK(SHRT_MIN < USHRT_MAX);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, char);
	ATF_TP_ADD_TC(tp, posix);
	ATF_TP_ADD_TC(tp, short);

	return atf_no_error();
}
