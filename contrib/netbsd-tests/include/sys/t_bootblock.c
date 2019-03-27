/* $NetBSD: t_bootblock.c,v 1.1 2010/07/17 19:26:27 jmmv Exp $ */

/*
 * Copyright (c) 2004, 2008, 2010 The NetBSD Foundation, Inc.
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

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008, 2010\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_bootblock.c,v 1.1 2010/07/17 19:26:27 jmmv Exp $");

#include <sys/types.h>
#include <sys/bootblock.h>

#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#include <atf-c.h>

ATF_TC_WITHOUT_HEAD(mbr_sector);
ATF_TC_BODY(mbr_sector, tc)
{
	ATF_CHECK_EQ(512, sizeof(struct mbr_sector));

	ATF_CHECK_EQ(MBR_BPB_OFFSET, offsetof(struct mbr_sector, mbr_bpb));
	ATF_CHECK_EQ(MBR_BS_OFFSET,  offsetof(struct mbr_sector, mbr_bootsel));

	ATF_CHECK_EQ(440, offsetof(struct mbr_sector, mbr_dsn));

	ATF_CHECK_EQ(446, MBR_PART_OFFSET);
	ATF_CHECK_EQ(MBR_PART_OFFSET, offsetof(struct mbr_sector, mbr_parts));

	ATF_CHECK_EQ(510, MBR_MAGIC_OFFSET);
	ATF_CHECK_EQ(MBR_MAGIC_OFFSET, offsetof(struct mbr_sector, mbr_magic));
}

ATF_TC_WITHOUT_HEAD(mbr_partition);
ATF_TC_BODY(mbr_partition, tc)
{
	ATF_CHECK_EQ(16, sizeof(struct mbr_partition));
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mbr_sector);
	ATF_TP_ADD_TC(tp, mbr_partition);

	return atf_no_error();
}
