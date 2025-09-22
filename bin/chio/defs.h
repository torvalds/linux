/*	$OpenBSD: defs.h,v 1.4 2002/02/16 21:27:06 millert Exp $	*/
/*	$NetBSD: defs.h,v 1.1.1.1 1996/04/03 00:34:38 thorpej Exp $	*/

/*
 * Copyright (c) 1996 Jason R. Thorpe <thorpej@and.com>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgments:
 *	This product includes software developed by Jason R. Thorpe
 *	for And Communications, http://www.and.com/
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

struct element_type {
	char	*et_name;	/* name; i.e. "picker, "slot", etc. */
	int	et_type;	/* type number */
};

struct changer_command {
	char	*cc_name;	/* command name */
				/* command handler */
	int	(*cc_handler)(char *, int, char **);
};

struct special_word {
	char	*sw_name;	/* special word */
	int	sw_value;	/* token value */
};

/* sw_value */
#define SW_INVERT	1	/* set "invert media" flag */
#define SW_INVERT1	2	/* set "invert media 1" flag */
#define SW_INVERT2	3	/* set "invert media 2" flag */

/* Environment variable to check for default changer. */
#define CHANGER_ENV_VAR		"CHANGER"
