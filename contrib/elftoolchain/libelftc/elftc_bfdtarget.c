/*-
 * Copyright (c) 2008,2009 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <string.h>
#include <libelftc.h>

#include "_libelftc.h"

ELFTC_VCSID("$Id: elftc_bfdtarget.c 3174 2015-03-27 17:13:41Z emaste $");

Elftc_Bfd_Target *
elftc_bfd_find_target(const char *tgt_name)
{
	Elftc_Bfd_Target *tgt;

	for (tgt = _libelftc_targets; tgt->bt_name; tgt++)
		if (!strcmp(tgt_name, tgt->bt_name))
			return (tgt);

	return (NULL);		/* not found */
}

Elftc_Bfd_Target_Flavor
elftc_bfd_target_flavor(Elftc_Bfd_Target *tgt)
{

	return (tgt->bt_type);
}

unsigned int
elftc_bfd_target_byteorder(Elftc_Bfd_Target *tgt)
{

	return (tgt->bt_byteorder);
}

unsigned int
elftc_bfd_target_class(Elftc_Bfd_Target *tgt)
{

	return (tgt->bt_elfclass);
}

unsigned int
elftc_bfd_target_machine(Elftc_Bfd_Target *tgt)
{

	return (tgt->bt_machine);
}
