/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */

/*
 * Routines to do pattern matching.
 */

#include "less.h"

extern int caseless;

/*
 * Compile a search pattern, for future use by match_pattern.
 */
	static int
compile_pattern2(pattern, search_type, comp_pattern, show_error)
	char *pattern;
	int search_type;
	PATTERN_TYPE *comp_pattern;
	int show_error;
{
	if (search_type & SRCH_NO_REGEX)
		return (0);
  {
#if HAVE_GNU_REGEX
	struct re_pattern_buffer *comp = (struct re_pattern_buffer *)
		ecalloc(1, sizeof(struct re_pattern_buffer));
	re_set_syntax(RE_SYNTAX_POSIX_EXTENDED);
	if (re_compile_pattern(pattern, strlen(pattern), comp))
	{
		free(comp);
		if (show_error)
			error("Invalid pattern", NULL_PARG);
		return (-1);
	}
	if (*comp_pattern != NULL)
	{
		regfree(*comp_pattern);
		free(*comp_pattern);
	}
	*comp_pattern = comp;
#endif
#if HAVE_POSIX_REGCOMP
	regex_t *comp = (regex_t *) ecalloc(1, sizeof(regex_t));
	if (regcomp(comp, pattern, REGCOMP_FLAG))
	{
		free(comp);
		if (show_error)
			error("Invalid pattern", NULL_PARG);
		return (-1);
	}
	if (*comp_pattern != NULL)
	{
		regfree(*comp_pattern);
		free(*comp_pattern);
	}
	*comp_pattern = comp;
#endif
#if HAVE_PCRE
	pcre *comp;
	constant char *errstring;
	int erroffset;
	PARG parg;
	comp = pcre_compile(pattern, 0,
			&errstring, &erroffset, NULL);
	if (comp == NULL)
	{
		parg.p_string = (char *) errstring;
		if (show_error)
			error("%s", &parg);
		return (-1);
	}
	*comp_pattern = comp;
#endif
#if HAVE_RE_COMP
	PARG parg;
	if ((parg.p_string = re_comp(pattern)) != NULL)
	{
		if (show_error)
			error("%s", &parg);
		return (-1);
	}
	*comp_pattern = 1;
#endif
#if HAVE_REGCMP
	char *comp;
	if ((comp = regcmp(pattern, 0)) == NULL)
	{
		if (show_error)
			error("Invalid pattern", NULL_PARG);
		return (-1);
	}
	if (comp_pattern != NULL)
		free(*comp_pattern);
	*comp_pattern = comp;
#endif
#if HAVE_V8_REGCOMP
	struct regexp *comp;
	reg_show_error = show_error;
	comp = regcomp(pattern);
	reg_show_error = 1;
	if (comp == NULL)
	{
		/*
		 * regcomp has already printed an error message 
		 * via regerror().
		 */
		return (-1);
	}
	if (*comp_pattern != NULL)
		free(*comp_pattern);
	*comp_pattern = comp;
#endif
  }
	return (0);
}

/*
 * Like compile_pattern2, but convert the pattern to lowercase if necessary.
 */
	public int
compile_pattern(pattern, search_type, comp_pattern)
	char *pattern;
	int search_type;
	PATTERN_TYPE *comp_pattern;
{
	char *cvt_pattern;
	int result;

	if (caseless != OPT_ONPLUS)
		cvt_pattern = pattern;
	else
	{
		cvt_pattern = (char*) ecalloc(1, cvt_length(strlen(pattern), CVT_TO_LC));
		cvt_text(cvt_pattern, pattern, (int *)NULL, (int *)NULL, CVT_TO_LC);
	}
	result = compile_pattern2(cvt_pattern, search_type, comp_pattern, 1);
	if (cvt_pattern != pattern)
		free(cvt_pattern);
	return (result);
}

/*
 * Forget that we have a compiled pattern.
 */
	public void
uncompile_pattern(pattern)
	PATTERN_TYPE *pattern;
{
#if HAVE_GNU_REGEX
	if (*pattern != NULL)
	{
		regfree(*pattern);
		free(*pattern);
	}
	*pattern = NULL;
#endif
#if HAVE_POSIX_REGCOMP
	if (*pattern != NULL)
	{
		regfree(*pattern);
		free(*pattern);
	}
	*pattern = NULL;
#endif
#if HAVE_PCRE
	if (*pattern != NULL)
		pcre_free(*pattern);
	*pattern = NULL;
#endif
#if HAVE_RE_COMP
	*pattern = 0;
#endif
#if HAVE_REGCMP
	if (*pattern != NULL)
		free(*pattern);
	*pattern = NULL;
#endif
#if HAVE_V8_REGCOMP
	if (*pattern != NULL)
		free(*pattern);
	*pattern = NULL;
#endif
}

/*
 * Can a pattern be successfully compiled?
 */
	public int
valid_pattern(pattern)
	char *pattern;
{
	PATTERN_TYPE comp_pattern;
	int result;

	CLEAR_PATTERN(comp_pattern);
	result = compile_pattern2(pattern, 0, &comp_pattern, 0);
	if (result != 0)
		return (0);
	uncompile_pattern(&comp_pattern);
	return (1);
}

/*
 * Is a compiled pattern null?
 */
	public int
is_null_pattern(pattern)
	PATTERN_TYPE pattern;
{
#if HAVE_GNU_REGEX
	return (pattern == NULL);
#endif
#if HAVE_POSIX_REGCOMP
	return (pattern == NULL);
#endif
#if HAVE_PCRE
	return (pattern == NULL);
#endif
#if HAVE_RE_COMP
	return (pattern == 0);
#endif
#if HAVE_REGCMP
	return (pattern == NULL);
#endif
#if HAVE_V8_REGCOMP
	return (pattern == NULL);
#endif
#if NO_REGEX
	return (pattern == NULL);
#endif
}

/*
 * Simple pattern matching function.
 * It supports no metacharacters like *, etc.
 */
	static int
match(pattern, pattern_len, buf, buf_len, pfound, pend)
	char *pattern;
	int pattern_len;
	char *buf;
	int buf_len;
	char **pfound, **pend;
{
	char *pp, *lp;
	char *pattern_end = pattern + pattern_len;
	char *buf_end = buf + buf_len;

	for ( ;  buf < buf_end;  buf++)
	{
		for (pp = pattern, lp = buf;  ;  pp++, lp++)
		{
			char cp = *pp;
			char cl = *lp;
			if (caseless == OPT_ONPLUS && ASCII_IS_UPPER(cp))
				cp = ASCII_TO_LOWER(cp);
			if (cp != cl)
				break;
			if (pp == pattern_end || lp == buf_end)
				break;
		}
		if (pp == pattern_end)
		{
			if (pfound != NULL)
				*pfound = buf;
			if (pend != NULL)
				*pend = lp;
			return (1);
		}
	}
	return (0);
}

/*
 * Perform a pattern match with the previously compiled pattern.
 * Set sp and ep to the start and end of the matched string.
 */
	public int
match_pattern(pattern, tpattern, line, line_len, sp, ep, notbol, search_type)
	PATTERN_TYPE pattern;
	char *tpattern;
	char *line;
	int line_len;
	char **sp;
	char **ep;
	int notbol;
	int search_type;
{
	int matched;

	*sp = *ep = NULL;
#if NO_REGEX
	search_type |= SRCH_NO_REGEX;
#endif
	if (search_type & SRCH_NO_REGEX)
		matched = match(tpattern, strlen(tpattern), line, line_len, sp, ep);
	else
	{
#if HAVE_GNU_REGEX
	{
		struct re_registers search_regs;
		pattern->not_bol = notbol;
		pattern->regs_allocated = REGS_UNALLOCATED;
		matched = re_search(pattern, line, line_len, 0, line_len, &search_regs) >= 0;
		if (matched)
		{
			*sp = line + search_regs.start[0];
			*ep = line + search_regs.end[0];
		}
	}
#endif
#if HAVE_POSIX_REGCOMP
	{
		regmatch_t rm;
		int flags = (notbol) ? REG_NOTBOL : 0;
#ifdef REG_STARTEND
		flags |= REG_STARTEND;
		rm.rm_so = 0;
		rm.rm_eo = line_len;
#endif
		matched = !regexec(pattern, line, 1, &rm, flags);
		if (matched)
		{
#ifndef __WATCOMC__
			*sp = line + rm.rm_so;
			*ep = line + rm.rm_eo;
#else
			*sp = rm.rm_sp;
			*ep = rm.rm_ep;
#endif
		}
	}
#endif
#if HAVE_PCRE
	{
		int flags = (notbol) ? PCRE_NOTBOL : 0;
		int ovector[3];
		matched = pcre_exec(pattern, NULL, line, line_len,
			0, flags, ovector, 3) >= 0;
		if (matched)
		{
			*sp = line + ovector[0];
			*ep = line + ovector[1];
		}
	}
#endif
#if HAVE_RE_COMP
	matched = (re_exec(line) == 1);
	/*
	 * re_exec doesn't seem to provide a way to get the matched string.
	 */
	*sp = *ep = NULL;
#endif
#if HAVE_REGCMP
	*ep = regex(pattern, line);
	matched = (*ep != NULL);
	if (matched)
		*sp = __loc1;
#endif
#if HAVE_V8_REGCOMP
#if HAVE_REGEXEC2
	matched = regexec2(pattern, line, notbol);
#else
	matched = regexec(pattern, line);
#endif
	if (matched)
	{
		*sp = pattern->startp[0];
		*ep = pattern->endp[0];
	}
#endif
	}
	matched = (!(search_type & SRCH_NO_MATCH) && matched) ||
			((search_type & SRCH_NO_MATCH) && !matched);
	return (matched);
}

