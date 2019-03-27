/*      $NetBSD: meta.h,v 1.5 2016/05/12 20:28:34 sjg Exp $ */

/*
 * Things needed for 'meta' mode.
 */
/*
 * Copyright (c) 2009-2010, Juniper Networks, Inc.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.  
 * 3. Neither the name of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

typedef struct BuildMon {
    char	meta_fname[MAXPATHLEN];
    int		filemon_fd;
    int		mon_fd;
    FILE	*mfp;
} BuildMon;

extern Boolean useMeta;

struct Job;				/* not defined yet */
void meta_init(void);
void meta_finish(void);
void meta_mode_init(const char *);
void meta_job_start(struct Job *, GNode *);
void meta_job_child(struct Job *);
void meta_job_error(struct Job *, GNode *, int, int);
void meta_job_output(struct Job *, char *, const char *);
int  meta_cmd_finish(void *);
int  meta_job_finish(struct Job *);
Boolean meta_oodate(GNode *, Boolean);
void meta_compat_start(void);
void meta_compat_child(void);
void meta_compat_parent(void);
